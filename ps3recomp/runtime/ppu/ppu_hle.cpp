/*
 * ps3recomp - PPU HLE bridge (NID -> host function dispatch)
 *
 * Connects the recompiled game's firmware imports to our HLE C libraries.
 * The game calls an imported function through its import stub; the lifter
 * emits `ps3_hle_call(<NID>, ctx)` for those addresses (see ppu_lifter.py
 * --imports). This resolves the NID to a registered HLE handler and marshals
 * the PPC calling convention into a native C call.
 *
 * PPC64 ELFv1 integer/pointer ABI: arguments in r3..r10 (gpr[3..10]), return
 * value in r3. The generic adapter casts the handler to a uint64-in/uint64-out
 * function and passes the 8 GPR argument slots; this covers the large majority
 * of cellXxx APIs (integer args/handles, s32 return). Functions that take or
 * return *pointers* need host<->guest address translation and so require a
 * per-function wrapper -- the generic path passes the raw value through.
 *
 * Compiled as C++ (matches the lifted output). Game-agnostic.
 */
#include "ppu_recomp.h"   /* ppu_context */
#include "ps3emu/nid.h"   /* ps3_nid_table, ps3_nid_entry */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>       /* getenv */
#include <string.h>       /* strncmp */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>      /* VirtualQuery, for the safe guest-stack scan below */
#endif

/* ---------------------------------------------------------------------------
 * SPURS bring-up trace (Marco 1). Opt-in via the PS3_TRACE_SPURS env var so
 * normal runs are untouched:
 *   PS3_TRACE_SPURS=1     -> trace cellSpurs / cellSync / sys_ HLE calls
 *   PS3_TRACE_SPURS=all   -> trace every resolved HLE call
 * Each line: [TRACE] <name>(r3,r4,r5,r6). Order in stderr == call order.
 * -----------------------------------------------------------------------*/
static int hle_trace_mode(void)   /* 0=off, 1=filtered, 2=all (cached) */
{
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("PS3_TRACE_SPURS");
        cached = !v ? 0 : (strcmp(v, "all") == 0 ? 2 : (v[0] && v[0] != '0' ? 1 : 0));
    }
    return cached;
}
static int hle_trace_match(const char* name)
{
    int m = hle_trace_mode();
    if (m == 0) return 0;
    if (m == 2 || !name) return m == 2;
    return strncmp(name, "cellSpurs", 9) == 0 ||
           strncmp(name, "cellSync",  8) == 0 ||
           strncmp(name, "sys_",      4) == 0 ||
           strncmp(name, "_cellSpurs",10) == 0;
}
static void hle_trace_log(const char* name, ppu_context* ctx)
{
    if (!hle_trace_match(name)) return;
    fprintf(stderr, "[TRACE] %s(r3=0x%llX r4=0x%llX r5=0x%llX r6=0x%llX)\n",
            name ? name : "?",
            (unsigned long long)ctx->gpr[3], (unsigned long long)ctx->gpr[4],
            (unsigned long long)ctx->gpr[5], (unsigned long long)ctx->gpr[6]);
    fflush(stderr);
}

/* Single flat NID -> handler table (all modules share it; resolution is by
 * NID which is globally unique). Sized for the firmware import surface. */
#define HLE_NID_CAP 4096
static ps3_nid_entry  g_hle_storage[HLE_NID_CAP];
static ps3_nid_table  g_hle_nids;
static int            g_hle_inited = 0;

extern "C" void ps3_hle_register(uint32_t nid, const char* name, void* handler)
{
    if (!g_hle_inited) { ps3_nid_table_init(&g_hle_nids, g_hle_storage, HLE_NID_CAP); g_hle_inited = 1; }
    ps3_nid_table_add(&g_hle_nids, nid, name, handler);
}

extern "C" uint32_t ps3_hle_count(void) { return g_hle_inited ? g_hle_nids.count : 0; }

/* Context-aware handlers: functions that need the full ppu_context (to read
 * args beyond the generic ABI, set registers like r13, touch memory, etc.).
 * Registered separately and dispatched before the generic table. */
typedef void (*hle_ctx_fn)(ppu_context*);
#define HLE_CTX_CAP 256
static struct { uint32_t nid; hle_ctx_fn fn; const char* name; } g_ctx[HLE_CTX_CAP];
static uint32_t g_ctx_count = 0;

extern "C" void ps3_hle_register_ctx(uint32_t nid, const char* name, hle_ctx_fn fn)
{
    if (g_ctx_count < HLE_CTX_CAP) {
        g_ctx[g_ctx_count].nid = nid; g_ctx[g_ctx_count].fn = fn;
        g_ctx[g_ctx_count].name = name; g_ctx_count++;
    }
}

/* Generic PPC integer/pointer ABI adapter. */
typedef uint64_t (*hle_generic)(uint64_t, uint64_t, uint64_t, uint64_t,
                                uint64_t, uint64_t, uint64_t, uint64_t);

extern "C" __thread uint64_t g_last_hle_r1;
/* Em bloco extern "C": o corpo declara TLS `extern __thread ...` a nivel de
 * bloco, e essas declaracoes tem de manter a MESMA linkage das definicoes
 * extern "C" mais abaixo neste ficheiro. */
extern "C" { static void ps3_hle_call_impl(uint32_t nid, ppu_context* ctx); }

/* Funil 2 de 2 por onde o codigo liftado alcanca o host (o outro e'
 * lv2_syscall). Com PS3_GIANT_HLE_ONLY o giant lock e' tomado AQUI em vez de na
 * entrada da thread guest, de modo que o codigo liftado corre concorrente e o
 * lock protege so' o estado HLE. Ver o comentario de ppu_giant_hle_only em
 * ppu_loader.cpp. */
extern "C" {
int  ppu_giant_hle_only_on(void);
void ppu_giant_lock_acquire(void);
void ppu_giant_lock_release(void);
int  ppu_giant_lock_held_by_caller(void);
}

extern "C" int wp75_on(void); extern "C" long long wp75_now(void); extern "C" void wp75_add(unsigned long long, long long);   /* winport v75-syswait */
extern "C" const char* wp75_hle_name(uint32_t nid)
{
    for (uint32_t i = 0; i < g_ctx_count; i++) if (g_ctx[i].nid == nid) return g_ctx[i].name ? g_ctx[i].name : "?";
    ps3_nid_entry* e = g_hle_inited ? ps3_nid_table_find(&g_hle_nids, nid) : nullptr;
    return (e && e->name) ? e->name : "?nid";
}
/* ---- winport v92a-ckpt-ingame (engine_fixes_v92a_ckpt_ingame.py): in-game checkpoint load ----
 * cellPadGetData is read by the main game thread once per frame. When cellPad.c has a checkpoint blob waiting
 * (wp92_pending) and the game is in gameplay / its pause menu, the blob goes into the checkpoint buffer and the game's own
 * pause-menu "Restart from last checkpoint" YES handler (func_000B8254) runs here as a nested guest call. */
extern "C" { extern volatile long wp92_pending; int wp92_main_prepare(void); void wp92_main_done(void);
             void ps3_indirect_call(ppu_context* ctx); }
static void wp92_hle_hook(ppu_context* ctx)
{
    if (!wp92_main_prepare()) return;
    ppu_context sv = *ctx;
    ctx->gpr[1] = (ctx->gpr[1] - 0x1000ull) & ~0xFull;   /* below the caller's frame + the ABI's 288-byte zone */
    ctx->gpr[2] = 0x00541178ull;                         /* EBOOT TOC */
    ctx->gpr[3] = 0; ctx->lr = 0; ctx->ctr = 0x000B8254ull;
    ps3_indirect_call(ctx);
    *ctx = sv;
    wp92_main_done();
}
/* ---- end winport v92a-ckpt-ingame ---- */
extern "C" void ps3_hle_call(uint32_t nid, ppu_context* ctx)
{
    if (__builtin_expect(wp92_pending != 0, 0) && (nid == 0x8B72CDA1u || nid == 0x8B72CDA5u)) wp92_hle_hook(ctx);   /* winport v92a-ckpt-ingame */
    if (wp75_on()) { const long long t75 = wp75_now();   /* winport v75-syswait */
        if (!ppu_giant_hle_only_on()) ps3_hle_call_impl(nid, ctx);
        else { ppu_giant_lock_acquire(); ps3_hle_call_impl(nid, ctx); if (ppu_giant_lock_held_by_caller()) ppu_giant_lock_release(); }
        wp75_add(0x100000000ull | nid, t75); return; }
    if (!ppu_giant_hle_only_on()) { ps3_hle_call_impl(nid, ctx); return; }
    ppu_giant_lock_acquire();
    ps3_hle_call_impl(nid, ctx);
    if (ppu_giant_lock_held_by_caller())
        ppu_giant_lock_release();
}

extern "C" { static void ps3_hle_call_impl(uint32_t nid, ppu_context* ctx)
{
    /* Always stamp HLE watermark before dispatch so C handlers (cellGcmSys,
     * MAINLOOP) can see which NID and guest LR selected this import. Guest
     * stack scan for residual code EAs runs only on the generic path below. */
    { extern __thread uint32_t g_last_hle_nid;
      extern __thread uint64_t g_last_hle_caller_lr;
      g_last_hle_nid = nid;
      g_last_hle_caller_lr = ctx->lr; }
    { static int dw=-1; if(dw<0){extern char* getenv(const char*); dw=getenv("PS3_TRACE_ALLOC")?1:0;}
      if(dw && nid==0x21AC3697u && (uint32_t)ctx->gpr[3]>=0xC0F40000u && (uint32_t)ctx->gpr[3]<0xC1000000u)
        fprintf(stderr,"[A2O] addr=0x%X out=0x%llX lr=0x%llX\n",(uint32_t)ctx->gpr[3],(unsigned long long)ctx->gpr[4],(unsigned long long)ctx->lr); }
#ifdef _WIN32
    if (nid == 0xE6F2C1E7u) {
        static int n = 0; if (n < 3) {
            /* Host RBP walk on OUR OWN live stack (not a suspended thread's) --
             * always safe to read, no VirtualQuery needed. -O0 keeps a proper
             * push-rbp/mov-rbp,rsp frame in every function, so this gives the
             * EXACT lifted-C++ caller chain (ps3_hle_call -> ps3_indirect_call
             * -> ps3_import_thunk -> func_XXXXXXXX...), unlike the guest-stack
             * heuristic which can surface stale/ghost values. */
            uintptr_t modbase = (uintptr_t)GetModuleHandleW(NULL);
            unsigned long long rbp = (unsigned long long)(uintptr_t)__builtin_frame_address(0);
            fprintf(stderr, "[EXIT-CALLER] exitcode=%lld modbase=0x%llX rbp_chain:\n",
                    (long long)ctx->gpr[3], (unsigned long long)modbase);
            bool next_is_indirect_call_frame = false;
            /* The chain ends in lifted-game frames whose saved-rbp slot can
             * hold garbage (observed: a pointer into .data, then a deref of
             * ASCII text -> real SIGSEGV that masked the game's clean exit).
             * Only follow rbp while it stays inside our own stack region,
             * above where we started and 8-byte aligned. */
            unsigned long long stack_lo = rbp;
            unsigned long long stack_hi = rbp + 0x200000ull;
            for (int depth = 0; depth < 12 && rbp; depth++) {
                if (rbp < stack_lo || rbp + 0x50 >= stack_hi || (rbp & 7)) {
                    fprintf(stderr, "  #%d rbp=0x%llX (left stack region, stopping)\n", depth, rbp);
                    break;
                }
                unsigned long long* frame = (unsigned long long*)rbp;
                /* ps3_indirect_call has a non-standard prologue (push rbp; push
                 * 7 more callee-saved regs; sub $0x98,%rsp; lea 0x90(%rsp),%rbp)
                 * -- its rbp sits offset INTO the local area, not at the top of
                 * the frame, so ITS saved-rbp/return-addr pair live at
                 * [rbp+0x40]/[rbp+0x48], not [rbp+0]/[rbp+8] (confirmed via
                 * objdump). Every other frame here (ps3_hle_call,
                 * ps3_import_thunk, the lifted func_XXXXXXXX chain) uses the
                 * plain push-rbp/mov-rbp,rsp prologue where +0/+8 is correct.
                 * We know the NEXT rbp belongs to ps3_indirect_call once the
                 * CURRENT frame's ret lands inside its address range. */
                unsigned long long saved_rbp, ret;
                if (next_is_indirect_call_frame) { saved_rbp = frame[8]; ret = frame[9]; }
                else                             { saved_rbp = frame[0]; ret = frame[1]; }
                unsigned long long nm = ret >= modbase ? ret - modbase + 0x140000000ull : 0;
                fprintf(stderr, "  #%d rbp=0x%llX ret=0x%llX (nm=0x%llX)\n", depth, rbp, ret, nm);
                next_is_indirect_call_frame = (nm >= 0x14732f4fdull && nm < 0x14732f8c4ull);
                if (!saved_rbp || saved_rbp <= rbp) break;
                rbp = saved_rbp;
            }
            fflush(stderr);
            n++;
        }
    }
#endif
    ps3_nid_entry* e = nullptr;
    static int wp63_hc = -1;   /* winport v63: WINPORT_HLE_CACHE=1 -> cached nid lookup (engine_fixes_v63_hlecache_wstore.py) */
    if (wp63_hc < 0) { const char* ev = getenv("WINPORT_HLE_CACHE"); wp63_hc = (ev && ev[0] == '1') ? 1 : 0;
                       if (wp63_hc) { fprintf(stderr, "[HLECACHE63] cached HLE NID dispatch\n"); fflush(stderr); } }
    if (wp63_hc) {
        struct Wp63Slot { uint32_t nid, gen; int idx; ps3_nid_entry* e; };
        static Wp63Slot s_wp63[1024];
        const uint32_t gen = (g_ctx_count << 1) | (g_hle_inited ? 1u : 0u);
        Wp63Slot* c = &s_wp63[(nid * 2654435761u) >> 22];
        if (c->nid == nid && c->gen == gen && (c->idx >= 0 || c->e)) {
            if (c->idx >= 0) { if (hle_trace_mode()) hle_trace_log(g_ctx[c->idx].name, ctx); g_ctx[c->idx].fn(ctx); return; }
            e = c->e;
        } else {
            for (uint32_t i = 0; i < g_ctx_count; i++)
                if (g_ctx[i].nid == nid) {
                    c->nid = nid; c->gen = gen; c->idx = (int)i; c->e = nullptr;
                    if (hle_trace_mode()) hle_trace_log(g_ctx[i].name, ctx);
                    g_ctx[i].fn(ctx); return;
                }
            e = g_hle_inited ? ps3_nid_table_find(&g_hle_nids, nid) : nullptr;
            if (e && e->handler) { c->nid = nid; c->gen = gen; c->idx = -1; c->e = e; }
        }
    } else {
        for (uint32_t i = 0; i < g_ctx_count; i++)
            if (g_ctx[i].nid == nid) {
                if (hle_trace_mode()) hle_trace_log(g_ctx[i].name, ctx);
                g_ctx[i].fn(ctx); return;
            }
        e = g_hle_inited ? ps3_nid_table_find(&g_hle_nids, nid) : nullptr;
    }
    if (!e || !e->handler) {
        static int logged = 0;
        if (logged < 40) {
            int in_ctx = -1;
            for (uint32_t i = 0; i < g_ctx_count; i++) if (g_ctx[i].nid == nid) { in_ctx = (int)i; break; }
            fprintf(stderr, "[hle] unresolved NID 0x%08X (g_ctx_count=%u in_ctx=%d gen_inited=%d)\n",
                    nid, g_ctx_count, in_ctx, g_hle_inited);
            logged++;
        }
        ctx->gpr[3] = 0;   /* CELL_OK-ish so the game keeps going */
        return;
    }
    if (hle_trace_mode()) hle_trace_log(e->name, ctx);
    {
      /* ctx->lr is 0 through DRAIN_TRAMPOLINE tail-call chains (documented
       * elsewhere this session) -- heuristically scan the GUEST stack
       * (ctx->gpr[1] upward) for a plausible code address instead, same
       * technique as the host-side crash-handler stack scan. Only the
       * slice actually committed by vm_stack_allocate() is safe to touch
       * -- boot_main.cpp reserves the whole 0xD0000000+ region but commits
       * it in per-thread slices, so blindly scanning 0x400 bytes past SP
       * previously hit a genuine access violation (GUEST=0xD0009000) on a
       * thread with a small committed slice. On Windows, VirtualQuery the
       * page range before reading it and stop at the first uncommitted
       * page; the boundary is cached so we don't pay a syscall per word.
       *
       * Caveat (R5/R6): hits here can be residual words, not live return
       * addresses. Prefer host backtrace (PS3_TRACE_MAINLOOP on Darwin/Win)
       * for real flip issuers. */
      extern __thread uint32_t g_last_hle_caller_guest[4];
      extern uint32_t vm_read32(uint64_t);
      extern uint32_t ppu_vm_size;
      uint32_t sp = (uint32_t)ctx->gpr[1]; int found = 0;
#ifdef _WIN32
      extern uint8_t* vm_base;
      uint64_t commit_lo = 0, commit_hi = 0;
      static int s_scan_on = -1;   /* winport: mingw */ /* diagnostic-only scan, off by default */
      if (s_scan_on < 0) { const char* e_ = getenv("PS3_HLE_CALLER_SCAN"); s_scan_on = (e_ && *e_ == '1') ? 1 : 0; }
      const uint32_t SCAN_LIMIT = s_scan_on ? 0x2000u : 0u;
#else
      /* Darwin: guest stack is fully committed via mmap; scan a bit more than
       * the old 0x40 so residual noise is not the only sample, but still
       * cheap. Real issuer ID uses host backtrace in cellGcmSys. */
      const uint32_t SCAN_LIMIT = 0x200;
#endif
      for (uint32_t off = 0; off < SCAN_LIMIT && found < 4; off += 4) {
          uint64_t addr = (uint64_t)sp + off;
          if (addr + 4 > ppu_vm_size) break;
#ifdef _WIN32
          if (addr < commit_lo || addr >= commit_hi) {
              MEMORY_BASIC_INFORMATION mbi;
              if (VirtualQuery(vm_base + addr, &mbi, sizeof(mbi)) != sizeof(mbi) ||
                  mbi.State != MEM_COMMIT)
                  break;   /* past the committed guest-stack slice -- stop */
              commit_lo = (uint64_t)((uint8_t*)mbi.BaseAddress - vm_base);
              commit_hi = commit_lo + mbi.RegionSize;
          }
#endif
          uint32_t v = vm_read32(addr);
          if (v >= 0x10000u && v < 0x500000u) g_last_hle_caller_guest[found++] = v;
      }
      for (; found < 4; found++) g_last_hle_caller_guest[found] = 0;
    }
    hle_generic fn = (hle_generic)e->handler;
    /* E168: r1 guest no momento da chamada, para HLEs com >8 argumentos lerem
     * os que vem na area de parametros da stack (9o arg em r1+0x70). */
    g_last_hle_r1 = ctx->gpr[1];
    uint64_t r = fn(ctx->gpr[3], ctx->gpr[4], ctx->gpr[5], ctx->gpr[6],
                    ctx->gpr[7], ctx->gpr[8], ctx->gpr[9], ctx->gpr[10]);
    ctx->gpr[3] = r;   /* PPC return value */
}
}  /* extern "C" do ps3_hle_call_impl */
/* Use __thread (not C++ thread_local) so C TUs (sysPrxForUser.c,
 * cellSpursLFQueue.c) can reference the same symbol. On Apple, C++ thread_local
 * binds the TLV locally and the C side sees an undefined g_last_hle_caller_*. */
extern "C" __thread uint64_t g_last_hle_caller_lr = 0;
extern "C" __thread uint64_t g_last_hle_r1 = 0;
extern "C" __thread uint32_t g_last_hle_caller_guest[4] = {0,0,0,0};
extern "C" __thread uint32_t g_last_hle_nid = 0;

/* Populated by the generated registration unit (gen_hle_nids.py). Weak so a
 * build without it still links (no HLE registered -> imports log + return 0). */
extern "C" void ppu_hle_register_all(void) __attribute__((weak));
extern "C" void ppu_hle_register_all(void) {}

extern "C" void ppu_hle_init(void) { ppu_hle_register_all(); }
