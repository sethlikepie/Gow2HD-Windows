/*
 * ps3recomp - sysPrxForUser CRT (boot-critical HLE)
 *
 * The first firmware functions a PS3 program calls at startup come from
 * sysPrxForUser (the libc/CRT bridge). Some need the full ppu_context (e.g.
 * sys_initialize_tls sets the thread pointer r13), so they register as
 * context-aware handlers (ps3_hle_register_ctx) rather than through the generic
 * integer-ABI table.
 *
 * NIDs are computed from the names (ps3_compute_nid), so this stays correct
 * without hand-written NID literals.
 */
#include "ppu_recomp.h"       /* ppu_context */
#include "ps3emu/nid.h"       /* ps3_compute_nid */
#include "ps3emu/guest_call.h" /* g_ps3_guest_caller (re-enter guest code) */
extern "C" void spurs_kernel_start_default(uint32_t nSpus);
extern "C" void spurs_kernel_stop(void);
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>       /* getenv, strtoul */
#ifndef _WIN32
#include <pthread.h>      /* pthread_self (PS3_LWM_TRACE) */
#endif

/* Real PPU thread implementations live in runtime/syscalls/sys_ppu_thread.c.
 * We forward-declare them here instead of including sys_ppu_thread.h, because
 * that header pulls in a second copy of ppu_context (via lv2_syscall_table.h ->
 * ppu_context.h) that clashes with the generated ppu_recomp.h definition. Same
 * struct layout + C linkage, so the pointer + symbol match at the ABI level. */
extern "C" {
int64_t sys_ppu_thread_create(ppu_context* ctx);
int64_t sys_ppu_thread_exit(ppu_context* ctx);
int64_t sys_ppu_thread_join(ppu_context* ctx);
int64_t sys_ppu_thread_detach(ppu_context* ctx);
int64_t sys_ppu_thread_yield(ppu_context* ctx);
int64_t sys_ppu_thread_set_priority(ppu_context* ctx);
int64_t sys_ppu_thread_get_priority(ppu_context* ctx);
int64_t sys_ppu_thread_get_stack_information(ppu_context* ctx);
int64_t sys_ppu_thread_rename(ppu_context* ctx);
int64_t sys_ppu_thread_get_join_state(ppu_context* ctx);
}

extern "C" uint8_t* vm_base;
extern "C" void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));
extern "C" uint8_t  vm_read8 (uint64_t a);
extern "C" uint32_t vm_read32(uint64_t a);
extern "C" void     vm_write8 (uint64_t a, uint8_t  v);
extern "C" void     vm_write32(uint64_t a, uint32_t v);
extern "C" void     vm_write64(uint64_t a, uint64_t v);

/* Simple bump allocator for TLS areas, in a free vm region below the stack. */
static uint32_t s_tls_next = 0x0E000000u;

/* sys_initialize_tls(u64 main_thread_id, u32 tls_seg_addr, u32 tls_seg_size,
 *                     u32 tls_mem_size) -- set up the main thread's TLS block
 * and point r13 (the PPC64 thread pointer) at it. TLS variables are accessed
 * at r13 - 0x7000 (the static TLS block bias). */
static void sys_initialize_tls(ppu_context* ctx)
{
    uint32_t seg_addr = (uint32_t)ctx->gpr[4];
    uint32_t seg_size = (uint32_t)ctx->gpr[5];
    uint32_t mem_size = (uint32_t)ctx->gpr[6];

    uint32_t block = s_tls_next;
    uint32_t total = ((mem_size + 0x7000u + 0x1000u) + 0xFFFu) & ~0xFFFu;
    s_tls_next += total;

    if (seg_addr && seg_size) memcpy(vm_base + block, vm_base + seg_addr, seg_size);
    if (mem_size > seg_size)  memset(vm_base + block + seg_size, 0, mem_size - seg_size);

    ctx->gpr[13] = block + 0x7000u;   /* thread pointer; TLS data at r13-0x7000 */
    ctx->gpr[3]  = 0;                  /* CELL_OK */
    fprintf(stderr, "[crt] sys_initialize_tls: block 0x%08X, r13=0x%08X (seg 0x%X+%u, mem %u)\n",
            block, (uint32_t)ctx->gpr[13], seg_addr, seg_size, mem_size);
}

/* sys_time_get_system_time() -> microseconds since boot (monotonic-ish). */
static void sys_time_get_system_time(ppu_context* ctx)
{
    static uint64_t t = 0;
    t += 1000;                         /* advance so callers see time progress */
    ctx->gpr[3] = t;
}

/* sys_process_is_stack(u32 addr) -> 1 if addr is in the stack region. We model
 * a single stack just below the TLS region; good enough for boot checks. */
static void sys_process_is_stack(ppu_context* ctx)
{
    uint32_t a = (uint32_t)ctx->gpr[3];
    ctx->gpr[3] = (a >= 0x0E000000u && a < 0x10000000u) ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * Lightweight mutex (sys_lwmutex) — sysPrxForUser.
 *
 * The CRT guards global/singleton initialization with lwmutexes. If create is
 * a no-op that never initializes the structure, the guarded init is skipped
 * and the protected registry is left with null function pointers (the early
 * boot then spins calling a null vtable entry). We model the structure for
 * real; locking is a no-op owner stamp (the boot is single-threaded).
 *
 * sys_lwmutex_t (big-endian, 24 bytes):
 *   +0x00 owner (u32)   +0x04 waiter (u32)   +0x08 attribute (u32)
 *   +0x0C recursive_count (u32)   +0x10 sleep_queue (u32)   +0x14 pad
 * sys_lwmutex_attribute_t: +0x00 protocol  +0x04 recursive  +0x08 name[8]
 * -----------------------------------------------------------------------*/
#define LWM_OWNER  0x00
#define LWM_ATTR   0x08
#define LWM_RECUR  0x0C
#define LWM_TID    1u   /* single-thread boot: one fixed owner id */
/* Default: REAL (E417/E466: frontend carrega; stub sem exclusao → arena
 * NULL → FREELIST-TAG-GUARD no open do WAD pos-intro).
 * PS3_LWMUTEX_STUB=1 ou PS3_LWMUTEX_REAL=0 restauram o stub. */
static int lwm_stub_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* stub = getenv("PS3_LWMUTEX_STUB");
        const char* real = getenv("PS3_LWMUTEX_REAL");
        if (stub && *stub && *stub != '0') on = 1;
        else if (real && *real == '0') on = 1;
        else on = 0;
    }
    return on;
}

/* Allocates a REAL host sync slot (sysPrxForUser.c's s_lwmutex[], with an
 * actual CRITICAL_SECTION/pthread_mutex_t) and stores its 1-based id in
 * sleep_queue -- without this, sleep_queue stays 0 and any sys_lwcond_wait
 * built on this mutex (lwmutex_id = sleep_queue-1 = 0xFFFFFFFF) always fails
 * CELL_ESRCH, which games retry in a tight non-blocking spin. */
extern "C" int32_t sys_lwmutex_alloc_real_slot(int recursive);

static void sys_lwmutex_create(ppu_context* ctx)
{
    { extern char* getenv(const char*); static int on=-1; if(on<0)on=getenv("PS3_TRACE_HEAPCFG")?1:0;
      if(on){ static int n=0; if(n<300){ fprintf(stderr,"[SYSPRX-LWM] sys_lwmutex_create(ppu_sysprx) RAN lwm=0x%X -> ret 0\n",(unsigned)ctx->gpr[3]); fflush(stderr);} } }
    uint32_t lwm  = (uint32_t)ctx->gpr[3];
    uint32_t attr = (uint32_t)ctx->gpr[4];
    uint32_t protocol  = attr ? vm_read32(attr + 0) : 0;
    uint32_t recursive = attr ? vm_read32(attr + 4) : 0;
    int32_t  real_slot = sys_lwmutex_alloc_real_slot((recursive & 0x10) != 0); /* SYS_SYNC_RECURSIVE */
    /* Modo real (PS3_LWMUTEX_REAL=1): campos como na consola (owner FREE=0xFFFFFFFF, attr com RECURSIVE,
     * medido E208); no stub mantem-se o encoding antigo (owner 0/1) de que o resto do stub depende. */
    vm_write32(lwm + 0x00, lwm_stub_on() ? 0u : 0xFFFFFFFFu);
    vm_write32(lwm + 0x04, 0);          /* waiter */
    vm_write32(lwm + LWM_ATTR, lwm_stub_on() ? protocol : (protocol | (recursive & 0x10)));
    vm_write32(lwm + LWM_RECUR, 0);     /* recursive_count */
    vm_write32(lwm + 0x10, (uint32_t)real_slot); /* sleep_queue: real slot id (0 if pool exhausted) */
    vm_write32(lwm + 0x14, 0);
    ctx->gpr[3] = 0;
}
extern "C" { extern __thread int g_ppu_lwm_depth; }   /* E416-LWM-DEPTH: defined in ppu_loader.cpp */
extern "C" int32_t sys_lwmutex_slot_lock(int32_t id, int try_only);
extern "C" int32_t sys_lwmutex_slot_unlock(int32_t id);
/* E211: exclusao mutua REAL (slot pthread alocado no create) com handoff do giant lock; campos guest
 * como na consola: owner = tid ou 0xFFFFFFFF (livre), recursive_count. PS3_LWMUTEX_STUB=1 repoe o stub. */
#ifndef _WIN32
#define LWM_SELF() pthread_self()
#else
#define LWM_SELF() 0
#endif
static void lwm_lock_common(ppu_context* ctx, int try_only)
{
    uint32_t lwm = (uint32_t)ctx->gpr[3];
    uint32_t me = (uint32_t)ctx->thread_id; if (!me) me = LWM_TID;
    if (lwm_stub_on()) { vm_write32(lwm + LWM_OWNER, LWM_TID); vm_write32(lwm + LWM_RECUR, vm_read32(lwm + LWM_RECUR) + 1);
        g_ppu_lwm_depth++;   /* E416-LWM-DEPTH */
        { static int on=-1, n=0; if(on<0){ const char* e=getenv("PS3_TRACE_LWMDEPTH"); on=(e&&*e&&*e!='0')?1:0; }
          if(on && n++<80){ fprintf(stderr,"[LWMDEPTH] lock   lwm=0x%08X tid=%u depth=%d\n",lwm,(unsigned)ctx->thread_id,g_ppu_lwm_depth); fflush(stderr);} }   /* E416b-LWMDEPTH-TRACE */
        ctx->gpr[3] = 0; return; }
    int32_t id = (int32_t)vm_read32(lwm + 0x10);
    int32_t rc = id > 0 ? sys_lwmutex_slot_lock(id, try_only) : 0;
    { static int on = -1; if (on < 0) on = getenv("PS3_LWM_TRACE") ? 1 : 0;
      if (on) fprintf(stderr, "[LWMEA] lock   lwm=0x%08X id=%d tid=%u rc=%d thr=%p\n", lwm, id, me, rc, (void*)LWM_SELF()); }
    if (rc == 0) { vm_write32(lwm + LWM_OWNER, me); vm_write32(lwm + LWM_RECUR, vm_read32(lwm + LWM_RECUR) + 1); }
    ctx->gpr[3] = (uint64_t)(int64_t)rc;
}
static void sys_lwmutex_lock(ppu_context* ctx)    { lwm_lock_common(ctx, 0); }
static void sys_lwmutex_trylock(ppu_context* ctx) { lwm_lock_common(ctx, 1); }
/* winport v98-lwmrec (engine_fixes_v98_lwmrec.py): first 8 recursive unlocks that released (or skipped) a real-slot level */
static void wp98_lwm_note(uint32_t lwm, int32_t rid, uint32_t rc, int released)
{
    static volatile long n98 = 0;
    const long k = __atomic_add_fetch(&n98, 1, __ATOMIC_RELAXED);
    if (k <= 8) { fprintf(stderr, "[LWM98] recursive unlock #%ld lwm=0x%08X slot=%d count %u->%u: %s\n", k, lwm, (int)rid, rc, rc - 1,
                          released ? "one real-slot level released" : "not the owner, slot left as is"); fflush(stderr); }
}
static void sys_lwmutex_unlock(ppu_context* ctx)
{
    uint32_t lwm = (uint32_t)ctx->gpr[3];
    uint32_t rc = vm_read32(lwm + LWM_RECUR);
    if (lwm_stub_on()) { if (rc) { vm_write32(lwm + LWM_RECUR, rc - 1); if (g_ppu_lwm_depth > 0) g_ppu_lwm_depth--; /* E416-LWM-DEPTH */ }
        { static int on=-1, n=0; if(on<0){ const char* e=getenv("PS3_TRACE_LWMDEPTH"); on=(e&&*e&&*e!='0')?1:0; }
          if(on && n++<80){ fprintf(stderr,"[LWMDEPTH] unlock lwm=0x%08X tid=%u rc=%u depth=%d\n",lwm,(unsigned)ctx->thread_id,rc,g_ppu_lwm_depth); fflush(stderr);} }   /* E416b-LWMDEPTH-TRACE */
        if (rc <= 1) vm_write32(lwm + LWM_OWNER, 0); ctx->gpr[3] = 0; return; }
    if (rc > 1) { vm_write32(lwm + LWM_RECUR, rc - 1);
        { static int on = -1; if (on < 0) { const char* e = getenv("WINPORT_LWM_RECUNLOCK"); on = !(e && e[0] == '0'); }   /* winport v98-lwmrec */
          if (on) { const int32_t rid = (int32_t)vm_read32(lwm + 0x10);
            uint32_t me = (uint32_t)ctx->thread_id; if (!me) me = LWM_TID;
            if (rid > 0) { const int mine = vm_read32(lwm + LWM_OWNER) == me; if (mine) sys_lwmutex_slot_unlock(rid); wp98_lwm_note(lwm, rid, rc, mine); } } }
        ctx->gpr[3] = 0; return; }
    vm_write32(lwm + LWM_RECUR, 0);
    vm_write32(lwm + LWM_OWNER, 0xFFFFFFFFu);
    int32_t id = (int32_t)vm_read32(lwm + 0x10);
    { static int on = -1; if (on < 0) on = getenv("PS3_LWM_TRACE") ? 1 : 0;
      if (on) fprintf(stderr, "[LWMEA] unlock lwm=0x%08X id=%d tid=%u thr=%p\n", lwm, id, (unsigned)ctx->thread_id, (void*)LWM_SELF()); }
    if (id > 0) sys_lwmutex_slot_unlock(id);
    ctx->gpr[3] = 0;
}
/* sys_ppu_thread_get_id(vm::ptr<u64> id) -> *id = main thread id (1). */
static void sys_ppu_thread_get_id(ppu_context* ctx)
{
    uint32_t p = (uint32_t)ctx->gpr[3];
    if (p) vm_write64(p, 1);
    ctx->gpr[3] = 0;
}

/* sys_mmapper_allocate_memory(u32 size, u64 flags, vm::ptr<u32> mem_id) ->
 * hand back a unique opaque id; the backing is the flat VM, so the later
 * search_and_map just needs a non-zero id to track. */
static void sys_mmapper_allocate_memory(ppu_context* ctx)
{
    static uint32_t s_next_id = 0x1000;
    uint32_t mem_id_ptr = (uint32_t)ctx->gpr[5];
    if (mem_id_ptr) vm_write32(mem_id_ptr, s_next_id);
    s_next_id++;
    ctx->gpr[3] = 0;
}

/* sys_ppu_thread_once(once_ctrl, init) -- pthread_once-style one-shot guard.
 * r3 = ptr to a u32 control word (0 = SYS_PPU_THREAD_ONCE_INIT, 1 = done),
 * r4 = OPD of the init routine. If still un-run, mark done and call init in
 * the guest exactly once. Stubbing this to a bare return (the previous
 * behaviour) skips every CRT singleton/global ctor guarded by it, leaving
 * those objects null and the boot calling through garbage. */
static void sys_ppu_thread_once(ppu_context* ctx)
{
    uint32_t once_ctrl = (uint32_t)ctx->gpr[3];
    uint32_t init_opd  = (uint32_t)ctx->gpr[4];
    uint32_t cur = once_ctrl ? vm_read32(once_ctrl) : 1;
    if (cur == 0 /*SYS_PPU_THREAD_ONCE_INIT*/) {
        vm_write32(once_ctrl, 1 /*done*/);
        if (g_ps3_guest_caller && init_opd)
            g_ps3_guest_caller(init_opd, 0, 0, 0, 0);
    }
    ctx->gpr[3] = 0;
}

/* PPU thread management. The real implementations live in sys_ppu_thread.c
 * (they spin up real host threads running lifted code via the entry trampoline)
 * and return int64_t; wrap each so the result lands in r3 for the HLE path.
 * The game imports these through sysPrxForUser NIDs, not lv2 syscalls. */
#define THREAD_HLE(fn) static void hle_##fn(ppu_context* ctx) { ctx->gpr[3] = (uint64_t)fn(ctx); }
THREAD_HLE(sys_ppu_thread_create)
THREAD_HLE(sys_ppu_thread_exit)
THREAD_HLE(sys_ppu_thread_join)
THREAD_HLE(sys_ppu_thread_detach)
THREAD_HLE(sys_ppu_thread_yield)
THREAD_HLE(sys_ppu_thread_set_priority)
THREAD_HLE(sys_ppu_thread_get_priority)
THREAD_HLE(sys_ppu_thread_get_stack_information)
THREAD_HLE(sys_ppu_thread_rename)
THREAD_HLE(sys_ppu_thread_get_join_state)
#undef THREAD_HLE

/* cellSpurs attribute init — BE-aware shim. The HLE lib's cellSpursAttributeInitialize
 * derefs a HOST pointer and writes host-endian fields, neither valid for our
 * big-endian guest. We initialize the attribute struct directly in guest memory
 * (vm_write32 is BE) instead. CELL_SPURS_ATTRIBUTE_SIZE is 512 bytes (opaque). */
static void hle_cellSpursAttributeInitialize(ppu_context* ctx)
{
    uint32_t attr    = (uint32_t)ctx->gpr[3];
    int32_t  nSpus   = (int32_t)ctx->gpr[4];
    int32_t  spuPrio = (int32_t)ctx->gpr[5];
    if (attr) {
        memset(vm_base + attr, 0, 512);
        vm_write32(attr + 0, (nSpus > 0 && nSpus <= 8) ? (uint32_t)nSpus : 1);
        for (int i = 0; i < 8; i++) vm_write32(attr + 4 + 4 * i, (uint32_t)spuPrio);
    }
    ctx->gpr[3] = 0;
}

/* cellSpurs init path — BE-aware stubs that override the host-pointer lib
 * versions (which segfault on a guest address). These let the boot get past
 * SPURS setup; real SPURS needs SPU execution (the kernel + jobs actually
 * running on the interpreter), so this only carries the boot to the first point
 * the game blocks on SPU results. CellSpurs/CellSpursTaskset are page-sized
 * (4096 B) opaque structs on real hardware. */
/* SPURS bring-up Marco 2 (MVP) — unblock the CRT-init spin-wait.
 *
 * After cellSpursInitialize[WithAttribute], GoW2's CRT-init (func_002B3CB0)
 * spins on a status object until its byte at +0x4 goes non-zero. On real hw
 * that write is a side effect of the SPURS subsystem coming up (SPU group +
 * kernel). We don't run the SPURS kernel yet (that's Marco 2.5), but the boot
 * only needs the status byte set to advance past the spin and expose the next
 * blocker. The status object pointer lives at a fixed TOC-relative global in
 * the GoW2 EBOOT: TOC(0x541178) - 0x1438 = 0x53FD40, which already points to
 * the (CRT-pre-allocated) object before Initialize runs (per the M1 trace).
 *
 * Defensive & non-breaking: read the pointer; only write if it is non-null
 * (never allocate, never write through null). The address is GoW2-specific,
 * so this is gated behind PS3_SPURS_STATUS_PTR (set to the pointer EA, e.g.
 * 0x53FD40); unset => behave exactly as the old stub for any other title. */
static void spurs_unblock_crt_status(void)
{
    const char* env = getenv("PS3_SPURS_STATUS_PTR");
    if (!env || !env[0]) return;                  /* opt-in; default = old behaviour */
    uint32_t ptr_ea = (uint32_t)strtoul(env, NULL, 0);
    uint32_t sp = vm_read32(ptr_ea);              /* deref the global -> status object EA */
    if (sp) {
        uint8_t before = vm_read8(sp + 0x4);
        vm_write8(sp + 0x4, 1);
        fprintf(stderr, "[spurs] init: status obj @0x%08X (via [0x%08X]); byte+0x4 %u->1\n",
                sp, ptr_ea, before);
    } else {
        fprintf(stderr, "[spurs] init: status ptr [0x%08X] is null, skipped\n", ptr_ea);
    }
    fflush(stderr);
}
/* CellSpursAttribute layout: nSpus is the first word (big-endian in guest
 * memory). Read defensively -- a null or bogus attr just yields 0 SPUs, which
 * the kernel records without pretending the call did more than it did. */
static uint32_t spurs_attr_num_spus(uint32_t attr_ea)
{
    return attr_ea ? vm_read32(attr_ea) : 0;
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
/* PS3_TRACE_SPURS_CALLER: when cellSpursInitializeWithAttribute is called with
 * spurs==NULL, walk the HOST rbp chain to find which lifted func_XXXXXXXX made
 * the call (same technique as ps3_hle_call's EXIT-CALLER trace). Answers
 * "who passes NULL for spurs" without hand-reading the lifted PPC chunk. */
static void trace_spurs_null_caller(ppu_context* ctx)
{
#ifdef _WIN32
    static int on = -1;
    if (on < 0) on = getenv("PS3_TRACE_SPURS_CALLER") ? 1 : 0;
    if (!on) return;
    static int shown = 0;
    if (shown >= 6) return;
    shown++;
    /* ctx->lr is the GUEST (PPC) link register -- if the import was reached via
     * a normal `bl`, this is the exact guest return address, immune to the
     * host-side trampoline-flattening that makes the host RBP walk ambiguous
     * (a tail-dispatched func_XXXX's frame can alias its DRAIN_TRAMPOLINE
     * caller instead of its true logical caller). Print this FIRST as the
     * primary signal; the RBP walk below is corroborating context only. */
    fprintf(stderr, "[SPURS-NULL-CALLER] guest lr=0x%llX r1(sp)=0x%llX r4=0x%llX r5=0x%llX r6=0x%llX\n",
            (unsigned long long)ctx->lr, (unsigned long long)ctx->gpr[1],
            (unsigned long long)ctx->gpr[4], (unsigned long long)ctx->gpr[5],
            (unsigned long long)ctx->gpr[6]);
    uintptr_t modbase = (uintptr_t)GetModuleHandleW(NULL);
    unsigned long long rbp = (unsigned long long)(uintptr_t)__builtin_frame_address(0);
    fprintf(stderr, "[SPURS-NULL-CALLER] modbase=0x%llX rbp_chain:\n", (unsigned long long)modbase);
    for (int depth = 0; depth < 14 && rbp; depth++) {
        unsigned long long* frame = (unsigned long long*)rbp;
        unsigned long long saved_rbp = frame[0], ret = frame[1];
        unsigned long long nm = ret >= modbase ? ret - modbase + 0x140000000ull : 0;
        fprintf(stderr, "  #%d rbp=0x%llX ret=0x%llX (nm=0x%llX)\n", depth, rbp, ret, nm);
        if (!saved_rbp || saved_rbp <= rbp) break;
        rbp = saved_rbp;
    }
    fflush(stderr);
#endif
}
static void hle_cellSpursInitializeWithAttribute(ppu_context* ctx)
{
    uint32_t spurs = (uint32_t)ctx->gpr[3];
    uint32_t attr  = (uint32_t)ctx->gpr[4];
    if (!spurs) trace_spurs_null_caller(ctx);

    if (spurs) memset(vm_base + spurs, 0, 4096);
    spurs_kernel_start_default(spurs_attr_num_spus(attr));
    spurs_unblock_crt_status();
    ctx->gpr[3] = 0;
}
static void hle_cellSpursInitialize(ppu_context* ctx)
{
    uint32_t spurs = (uint32_t)ctx->gpr[3];
    uint32_t nSpus = (uint32_t)ctx->gpr[4];   /* nSpus passed directly here */
    if (!spurs) trace_spurs_null_caller(ctx);
    if (spurs) memset(vm_base + spurs, 0, 4096);
    spurs_kernel_start_default(nSpus);
    spurs_unblock_crt_status();
    ctx->gpr[3] = 0;
}
static void hle_cellSpursFinalize(ppu_context* ctx)
{
    spurs_kernel_stop();
    ctx->gpr[3] = 0;
}
static void hle_cellSpursGetNumSpuThread(ppu_context* ctx)
{
    uint32_t nThreads = (uint32_t)ctx->gpr[4];  /* u32* out */
    if (nThreads) vm_write32(nThreads, 5);       /* report 5 SPUs */
    ctx->gpr[3] = 0;
}

/* Write a NUL-terminated C string into guest memory (one byte at a time so it
 * goes through the bounds-checked accessor, not a host memcpy on a guest addr). */
static void vm_put_cstr(uint32_t addr, const char* s)
{
    if (!addr) return;
    for (; *s; ++s, ++addr) vm_write8(addr, (uint8_t)*s);
    vm_write8(addr, 0);
}

/* cellGameContentPermit — BE-aware shim (lib version snprintf's into a guest
 * address taken as a host pointer, corrupting the host heap). Returns the PS3
 * content + USRDIR paths the game then feeds to cellFs (mapped to the VFS root). */
static void hle_cellGameContentPermit(ppu_context* ctx)
{
    vm_put_cstr((uint32_t)ctx->gpr[3], "/dev_hdd0/game/NPUA80491");
    vm_put_cstr((uint32_t)ctx->gpr[4], "/dev_hdd0/game/NPUA80491/USRDIR");
    ctx->gpr[3] = 0;
}

/* cellGameBootCheck — BE-aware shim (lib version derefs host pointers).
 * out: type r3, attributes r4, CellGameContentSize* r5 {hddFreeKB,sizeKB,sysKB},
 * dirName r6 (the content dir name). */
static void hle_cellGameBootCheck(ppu_context* ctx)
{
    uint32_t type = (uint32_t)ctx->gpr[3], attrs = (uint32_t)ctx->gpr[4];
    uint32_t size = (uint32_t)ctx->gpr[5], dir = (uint32_t)ctx->gpr[6];
    if (type)  vm_write32(type, 2);          /* CELL_GAME_GAMETYPE_HDD */
    if (attrs) vm_write32(attrs, 0);
    if (size) { vm_write32(size + 0, 1024 * 1024); /* hddFreeSizeKB = 1GB */
                vm_write32(size + 4, 0xFFFFFFFFu);  /* sizeKB = NOTCALC */
                vm_write32(size + 8, 0); }
    if (dir) vm_write8(dir, 0);              /* empty content dir name */
    ctx->gpr[3] = 0;                         /* CELL_GAME_RET_OK */
}

/* A handful of CRT helpers the early boot tends to hit; accept and continue. */
static void crt_ok(ppu_context* ctx) { ctx->gpr[3] = 0; }

/* sys_lwmutex_destroy — heap-aware. GoW2's heap allocator probes liveness with
 * sys_lwmutex_destroy(heap_mutex): on real lv2 a *held* (in-use) lwmutex cannot
 * be destroyed and returns CELL_EBUSY, which the title reads as "heap alive ->
 * proceed with the allocation". A plain CELL_OK (crt_ok) makes the title think
 * the heap is dead, so the decompression-buffer malloc returns 0 and the config
 * (console.cfg) inflate aborts with "vector<T> too long". The heap lwmutexes
 * live in the 0x880000-0x882000 control-block region; report EBUSY for those so
 * the heap stays "alive", and CELL_OK elsewhere so unrelated teardown works. */
extern "C" void sys_lwmutex_free_real_slot(int32_t slot_id);

static void crt_lwmutex_destroy(ppu_context* ctx) {
    uint32_t ga = (uint32_t)ctx->gpr[3];
    /* The heap's liveness mutex (0x881B50) must report EBUSY: GoW2's allocator
     * probes it via sys_lwmutex_destroy and treats EBUSY as "heap alive ->
     * allocate". CELL_OK makes it think the heap is dead, so large mallocs
     * (e.g. the console.cfg inflate buffer) return 0 -> "vector<T> too long". */
    bool is_heap_probe = (ga >= 0x881B40u && ga < 0x881B60u);
    { static int o=-1; if(o<0){extern char* getenv(const char*); o=getenv("PS3_TRACE_ALLOC")?1:0;}
      if(o && is_heap_probe){ static int k=0; if(k++<30) fprintf(stderr,"[LWMD] destroy 0x%X lr=0x%llX\n", ga, (unsigned long long)ctx->lr); } }
    /* NOTE: tried NOT freeing the real slot here (theory: destroy racing a
     * concurrent waiter's CRITICAL_SECTION) -- did not fix the sys_lwcond_wait
     * ESRCH-spin below (item 44). Reverted to freeing on genuine destroy;
     * the real cause is still open. See item 44 in memory. */
    if (!is_heap_probe) {
        uint32_t sleep_queue = vm_read32(ga + 0x10);
        if (sleep_queue != 0) {
            sys_lwmutex_free_real_slot((int32_t)sleep_queue);
            vm_write32(ga + 0x10, 0);
        }
    }
    ctx->gpr[3] = 0;
}

/* Report a feature as unavailable (negative error in r3). Used to disable the
 * SPU printf subsystem: it is a debug-only feature, and HLE-ing it would
 * deadlock the boot (the game spawns _SPU_printf_server and waits for its SPU
 * to register an object, but we don't execute SPU). Returning failure makes the
 * game's "enable SPU printf if available" path skip it cleanly. */
static void hle_unavailable(ppu_context* ctx) { ctx->gpr[3] = (uint64_t)(int64_t)-1; }

extern "C" void ppu_sysprx_register(void)
{
    ps3_hle_register_ctx(ps3_compute_nid("sys_initialize_tls"),       "sys_initialize_tls",       sys_initialize_tls);
    ps3_hle_register_ctx(ps3_compute_nid("sys_time_get_system_time"), "sys_time_get_system_time", sys_time_get_system_time);
    ps3_hle_register_ctx(ps3_compute_nid("sys_process_is_stack"),     "sys_process_is_stack",     sys_process_is_stack);
    /* Atexit registration: nothing to do at boot, just succeed. */
    ps3_hle_register_ctx(ps3_compute_nid("_sys_process_atexitspawn"), "_sys_process_atexitspawn", crt_ok);
    ps3_hle_register_ctx(ps3_compute_nid("_sys_process_at_Exitspawn"),"_sys_process_at_Exitspawn",crt_ok);

    /* Lightweight mutex family (guards global/singleton init in the CRT). */
    ps3_hle_register_ctx(ps3_compute_nid("sys_lwmutex_create"),  "sys_lwmutex_create",  sys_lwmutex_create);
    ps3_hle_register_ctx(ps3_compute_nid("sys_lwmutex_destroy"), "sys_lwmutex_destroy", crt_lwmutex_destroy);
    ps3_hle_register_ctx(ps3_compute_nid("sys_lwmutex_lock"),    "sys_lwmutex_lock",    sys_lwmutex_lock);
    ps3_hle_register_ctx(ps3_compute_nid("sys_lwmutex_unlock"),  "sys_lwmutex_unlock",  sys_lwmutex_unlock);
    ps3_hle_register_ctx(ps3_compute_nid("sys_lwmutex_trylock"), "sys_lwmutex_trylock", sys_lwmutex_trylock);

    /* Thread id + memory manager (high-frequency boot imports). The flat VM
     * means map/unmap/free are no-ops: the memory already exists everywhere. */
    ps3_hle_register_ctx(ps3_compute_nid("sys_ppu_thread_get_id"),      "sys_ppu_thread_get_id",      sys_ppu_thread_get_id);
    ps3_hle_register_ctx(ps3_compute_nid("sys_ppu_thread_once"),        "sys_ppu_thread_once",        sys_ppu_thread_once);

    /* Real PPU thread management (spawns host threads running lifted code). */
    ps3_hle_register_ctx(ps3_compute_nid("sys_ppu_thread_create"),              "sys_ppu_thread_create",              hle_sys_ppu_thread_create);
    ps3_hle_register_ctx(ps3_compute_nid("sys_ppu_thread_exit"),                "sys_ppu_thread_exit",                hle_sys_ppu_thread_exit);
    ps3_hle_register_ctx(ps3_compute_nid("sys_ppu_thread_join"),                "sys_ppu_thread_join",                hle_sys_ppu_thread_join);
    ps3_hle_register_ctx(ps3_compute_nid("sys_ppu_thread_detach"),              "sys_ppu_thread_detach",              hle_sys_ppu_thread_detach);
    ps3_hle_register_ctx(ps3_compute_nid("sys_ppu_thread_yield"),               "sys_ppu_thread_yield",               hle_sys_ppu_thread_yield);
    ps3_hle_register_ctx(ps3_compute_nid("sys_ppu_thread_set_priority"),        "sys_ppu_thread_set_priority",        hle_sys_ppu_thread_set_priority);
    ps3_hle_register_ctx(ps3_compute_nid("sys_ppu_thread_get_priority"),        "sys_ppu_thread_get_priority",        hle_sys_ppu_thread_get_priority);
    ps3_hle_register_ctx(ps3_compute_nid("sys_ppu_thread_get_stack_information"),"sys_ppu_thread_get_stack_information",hle_sys_ppu_thread_get_stack_information);
    ps3_hle_register_ctx(ps3_compute_nid("sys_ppu_thread_rename"),              "sys_ppu_thread_rename",              hle_sys_ppu_thread_rename);
    ps3_hle_register_ctx(ps3_compute_nid("sys_ppu_thread_get_join_state"),      "sys_ppu_thread_get_join_state",      hle_sys_ppu_thread_get_join_state);

    /* cellSpurs init path — BE-aware stubs overriding the host-pointer lib versions. */
    ps3_hle_register_ctx(ps3_compute_nid("_cellSpursAttributeInitialize"),       "_cellSpursAttributeInitialize",       hle_cellSpursAttributeInitialize);
    ps3_hle_register_ctx(ps3_compute_nid("cellSpursInitialize"),                 "cellSpursInitialize",                 hle_cellSpursInitialize);
    ps3_hle_register_ctx(ps3_compute_nid("cellSpursInitializeWithAttribute"),    "cellSpursInitializeWithAttribute",    hle_cellSpursInitializeWithAttribute);
    ps3_hle_register_ctx(ps3_compute_nid("cellSpursFinalize"),                   "cellSpursFinalize",                   hle_cellSpursFinalize);
    ps3_hle_register_ctx(ps3_compute_nid("cellSpursAttributeSetNamePrefix"),     "cellSpursAttributeSetNamePrefix",     crt_ok);
    ps3_hle_register_ctx(ps3_compute_nid("cellSpursAttributeSetSpuThreadGroupType"),"cellSpursAttributeSetSpuThreadGroupType",crt_ok);
    ps3_hle_register_ctx(ps3_compute_nid("cellSpursAttributeEnableSpuPrintfIfAvailable"),"cellSpursAttributeEnableSpuPrintfIfAvailable",crt_ok);
    ps3_hle_register_ctx(ps3_compute_nid("cellSpursGetNumSpuThread"),            "cellSpursGetNumSpuThread",            hle_cellSpursGetNumSpuThread);
    ps3_hle_register_ctx(ps3_compute_nid("cellSpursSetMaxContention"),           "cellSpursSetMaxContention",           crt_ok);

    /* cellGame (BE-aware shims; lib versions are host-pointer/host-endian). */
    ps3_hle_register_ctx(ps3_compute_nid("cellGameBootCheck"),     "cellGameBootCheck",     hle_cellGameBootCheck);
    ps3_hle_register_ctx(ps3_compute_nid("cellGameContentPermit"), "cellGameContentPermit", hle_cellGameContentPermit);

    /* SPU debug printf — disabled (return failure) so the game skips spawning
     * _SPU_printf_server and waiting on its SPU, which we don't execute. */
    ps3_hle_register_ctx(ps3_compute_nid("_sys_spu_printf_initialize"),   "_sys_spu_printf_initialize",   hle_unavailable);
    ps3_hle_register_ctx(ps3_compute_nid("_sys_spu_printf_finalize"),     "_sys_spu_printf_finalize",     crt_ok);
    ps3_hle_register_ctx(ps3_compute_nid("_sys_spu_printf_attach_group"), "_sys_spu_printf_attach_group", crt_ok);
    ps3_hle_register_ctx(ps3_compute_nid("_sys_spu_printf_detach_group"), "_sys_spu_printf_detach_group", crt_ok);
    ps3_hle_register_ctx(ps3_compute_nid("sys_mmapper_allocate_memory"), "sys_mmapper_allocate_memory", sys_mmapper_allocate_memory);
    ps3_hle_register_ctx(ps3_compute_nid("sys_mmapper_map_memory"),     "sys_mmapper_map_memory",     crt_ok);
    ps3_hle_register_ctx(ps3_compute_nid("sys_mmapper_unmap_memory"),   "sys_mmapper_unmap_memory",   crt_ok);
    ps3_hle_register_ctx(ps3_compute_nid("sys_mmapper_free_memory"),    "sys_mmapper_free_memory",    crt_ok);
}
