/*
 * ps3recomp - integrated PPU boot harness (first-boot attempt).
 *
 * Links the whole PPU runtime half into one executable and starts executing
 * the recompiled game's entry point:
 *
 *   lifted code (ppu_recomp.c) + loader (ppu_loader.cpp) + HLE bridge
 *   (ppu_hle.cpp + generated NID table) + HLE libs (cellGcmSys, rsx_commands)
 *
 * It loads the real EBOOT image, registers the lifted functions and the HLE
 * NID handlers, then dispatches the entry. Execution runs real Uncharted boot
 * code until it reaches a function outside the lifted subset (logged by the
 * unlifted stub), an unimplemented firmware import (logged by ps3_hle_call),
 * or an lv2 syscall (logged by lv2_syscall) -- telling us exactly what to
 * implement next.
 *
 * This proves the integration builds + runs; a full-image build additionally
 * needs the lifter to split output into multiple TUs (88 MB single-file
 * otherwise).
 */
#include "ppu_recomp.h"
#include "ps3emu/guest_call.h"     /* ps3_guest_caller_fn */
#include <stdint.h>
#include <stdio.h>

/* Windows-only harness (task F0-4: varredura de portabilidade dos testes):
 * reserva/comita o VM guest via VirtualAlloc em offsets fixos e usa
 * GetModuleHandleW/Sleep para o resto -- nao ha equivalente POSIX aqui
 * (precisaria mmap(MAP_FIXED) nos mesmos offsets + revisao propria, o que
 * nao e um fix trivial de Windows-ismo). O harness real de boot no macOS
 * vive em gow2-recomp (fora deste monorepo), nao aqui; este ficheiro nao e
 * invocado por nenhum script/CMake deste repo. Falha explicita e imediata
 * em vez de uma parede de erros de "windows.h nao encontrado".
 */
#ifndef _WIN32
#error boot_main (runtime/ppu/tests) e um harness so-Windows (VirtualAlloc/GetModuleHandleW/Sleep); sem porte POSIX aqui.
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>               /* VirtualAlloc -- sparse guest RAM */
#include <stdlib.h>

extern "C" {
uint32_t ppu_load_elf(const char* path);
void     ppu_recomp_register(void);
void     ppu_hle_init(void);
void     ppu_sysprx_register(void);
void     ppu_fs_register(void);
uint32_t ppu_resolve_imports(void);
void     lv2_init_syscalls(void);   /* runtime/syscalls/lv2_register.c (real lv2 table) */
int      ppu_run(uint32_t entry_opd, uint32_t stack_top);
int      ppu_opd_resolve(uint32_t opd, uint32_t* code, uint32_t* toc);
void     ps3_indirect_call(ppu_context* ctx);
#if defined(_MSC_VER)
extern __declspec(thread) void (*g_trampoline_fn)(void*);  /* shared with loader */
#else
extern __thread void (*g_trampoline_fn)(void*);  /* shared with loader */
#endif
extern void (*g_ppu_thread_entry_trampoline)(ppu_context*); /* sys_ppu_thread.c */
extern const char* ppu_vfs_root;   /* host dir that PS3 mount points map into */
}

#include <string.h>
#include <stdlib.h>

/* Derive the VFS root (the dir containing PS3_GAME) from the EBOOT path
 * <root>/PS3_GAME/USRDIR/EBOOT.elf  -> <root>. $PS3_VFS_ROOT overrides. */
static char s_vfs_root[1024];
static void derive_vfs_root(const char* eboot)
{
    const char* env = getenv("PS3_VFS_ROOT");
    if (env && *env) { ppu_vfs_root = env; return; }
    strncpy(s_vfs_root, eboot, sizeof s_vfs_root - 1);
    for (char* p = s_vfs_root; *p; p++) if (*p == '\\') *p = '/';
    /* strip three trailing components: EBOOT.elf / USRDIR / PS3_GAME */
    for (int i = 0; i < 3; i++) { char* s = strrchr(s_vfs_root, '/'); if (s) *s = 0; }
    if (!s_vfs_root[0]) strcpy(s_vfs_root, ".");
    ppu_vfs_root = s_vfs_root;
}

/* Host-provided symbols the runtime + HLE libs need. */
extern "C" uint8_t* vm_base = nullptr;

/* Re-enter guest code to fire a callback the game registered (its "function
 * pointer" is a guest OPD). Sets up a clean context on a dedicated callback
 * stack, plumbs r3..r6, dispatches through the indirect-call resolver, and
 * drains cross-fragment trampolines before returning. Synchronous. A separate
 * sub-stack per nesting level keeps re-entrant callbacks from clobbering each
 * other. */
#define CB_STACK_TOP 0x0D000000u
static void boot_guest_caller(uint32_t opd_addr,
                              uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
    if (!opd_addr) return;
    uint32_t code = 0, toc = 0;
    ppu_opd_resolve(opd_addr, &code, &toc);
    if (!code) return;

    static uint32_t cb_depth = 0;
    ++cb_depth;
    ppu_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.gpr[1] = CB_STACK_TOP - 0x10000u * cb_depth;
    ctx.gpr[2] = toc;
    ctx.gpr[3] = a0; ctx.gpr[4] = a1; ctx.gpr[5] = a2; ctx.gpr[6] = a3;
    ctx.ctr    = code;
    ps3_indirect_call(&ctx);
    while (g_trampoline_fn) { void (*tf)(void*) = g_trampoline_fn; g_trampoline_fn = 0; tf(&ctx); }
    --cb_depth;
}
/* g_ps3_guest_caller itself is defined by the HLE libs (cellSysutil.c); we only
 * install our implementation into it in main(). (In the libs-less smoke build,
 * boot_main provided the storage; with the real HLE libs linked, that would be a
 * duplicate definition, so we assign instead.) */

/* Entry point trampoline for guest PPU threads created via sys_ppu_thread_create.
 * The thread's context already has cia=entry (an OPD or raw code addr), gpr[3]=arg,
 * gpr[1]=stack, gpr[2]=toc. The upstream ps3_indirect_call expects CTR to hold
 * an already-OPD-resolved code address (it only does ppu_lookup), and
 * sys_ppu_thread_create sets cia = the raw entry the game passed -- which for a
 * PPC thread is an OPD ({code, toc}). So resolve the OPD here before dispatch
 * (fall back to treating cia as raw code if it doesn't resolve), then drain
 * cross-fragment trampolines. Runs on the new host thread (g_trampoline_fn is
 * TLS, so per-thread). */
static void boot_thread_trampoline(ppu_context* ctx)
{
    uint32_t code = 0, toc = 0;
    if (ppu_opd_resolve((uint32_t)ctx->cia, &code, &toc) && code) {
        ctx->ctr = code;
        if (toc) ctx->gpr[2] = toc;
    } else {
        ctx->ctr = ctx->cia;   /* already a raw code address */
    }
    ps3_indirect_call(ctx);
    while (g_trampoline_fn) { void (*tf)(void*) = g_trampoline_fn; g_trampoline_fn = 0; tf(ctx); }
}

/* Flat guest RAM. Must cover every region the address map uses, including the
 * PPU thread-stack region at VM_STACK_BASE=0xD0000000..0xE0000000 (vm.h) -- a
 * 1.36 GB buffer left spawned-thread stacks outside the VM, so their writes were
 * silently dropped by the OOB guard and the threads crashed. We size to
 * 0xE0000000 (3.5 GB). calloc commits lazily on Windows (demand-zero pages), so
 * untouched regions cost commit charge, not physical RAM. */
#define VM_SIZE    0xE0000000u
#define STACK_TOP  0x0FF00000u   /* main-thread stack: free region below the image */

/* Host crash handler (diagnostic): on an unhandled access violation (e.g. a
 * recompiled function dereferencing a bad pointer), dump the faulting host RIP
 * and address before dying. The exe is built with --disable-dynamicbase, so RIP
 * maps directly to a func_<addr> symbol via `nm`. Lets us locate boot crashes
 * without gdb (which the host commit limit can't always run under). */
static LONG WINAPI ps3_crash_handler(EXCEPTION_POINTERS* ep)
{
    fprintf(stderr, "\n[CRASH] code=0x%08lX rip=0x%016llX",
            (unsigned long)ep->ExceptionRecord->ExceptionCode,
            (unsigned long long)ep->ContextRecord->Rip);
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        ep->ExceptionRecord->NumberParameters >= 2)
    {   unsigned long long flt = (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1];
        fprintf(stderr, " fault=0x%016llX (%s)",
                flt, ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read");
        if (vm_base && flt >= (unsigned long long)(uintptr_t)vm_base &&
            flt < (unsigned long long)(uintptr_t)vm_base + VM_SIZE)
            fprintf(stderr, " GUEST=0x%08X", (unsigned)(flt - (unsigned long long)(uintptr_t)vm_base));
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "  rcx=0x%016llX rdx=0x%016llX r8=0x%016llX r9=0x%016llX\n",
            (unsigned long long)ep->ContextRecord->Rcx, (unsigned long long)ep->ContextRecord->Rdx,
            (unsigned long long)ep->ContextRecord->R8,  (unsigned long long)ep->ContextRecord->R9);
    /* Heuristic backtrace (no unwind tables needed): scan the stack near RSP
     * for QWORDs that look like a return address inside this no-ASLR module
     * (ImageBase 0x140000000). Good enough to nm-map the caller chain. */
    {
        uintptr_t base = (uintptr_t)GetModuleHandleW(NULL);
        unsigned long long* sp = (unsigned long long*)ep->ContextRecord->Rsp;
        fprintf(stderr, "  stack scan (module base=0x%llX):\n", (unsigned long long)base);
        int found = 0;
        for (int i = 0; i < 400 && found < 16; i++) {
            unsigned long long v = sp[i];
            if (v >= base + 0x1000 && v < base + 0x08000000ull) {
                fprintf(stderr, "    [rsp+0x%X] = 0x%016llX\n", i*8, v);
                found++;
            }
        }
    }
    /* Precise RBP-walk (ghost-free, unlike the heuristic scan above): -O0
     * keeps push-rbp/mov-rbp,rsp in every lifted function, so [rbp+0]=saved
     * rbp / [rbp+8]=return addr gives the EXACT live call chain. One known
     * exception: ps3_indirect_call has a non-standard prologue (push rbp +
     * 7 more callee-saved regs + sub $0x98,%rsp + lea 0x90(%rsp),%rbp) that
     * shifts its own saved-rbp/return-addr to [rbp+0x40]/[rbp+0x48] instead
     * -- detected by checking if the just-read return address lands inside
     * its range before trusting the next frame's default offsets. */
    {
        uintptr_t base = (uintptr_t)GetModuleHandleW(NULL);
        unsigned long long rbp = (unsigned long long)ep->ContextRecord->Rbp;
        fprintf(stderr, "  rbp walk:\n");
        bool next_is_indirect_call_frame = false;
        for (int depth = 0; depth < 20 && rbp; depth++) {
            unsigned long long* frame = (unsigned long long*)rbp;
            unsigned long long saved_rbp, ret;
            if (next_is_indirect_call_frame) { saved_rbp = frame[8]; ret = frame[9]; }
            else                             { saved_rbp = frame[0]; ret = frame[1]; }
            unsigned long long nm = ret >= base ? ret - base + 0x140000000ull : 0;
            fprintf(stderr, "    #%d rbp=0x%016llX ret=0x%016llX (nm=0x%llX)\n", depth, rbp, ret, nm);
            next_is_indirect_call_frame = (nm >= 0x14732f4fdull && nm < 0x14732f8c4ull);
            if (!saved_rbp || saved_rbp <= rbp) break;
            rbp = saved_rbp;
        }
    }
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

#include <tlhelp32.h>
extern "C" long ppu_giant_lock_current_holder(void);
extern "C" void* ppu_giant_lock_current_holder_ra(void);
extern "C" void ppu_register_committed_range(uint32_t lo, uint32_t hi);
/* Watchdog (PS3_WATCHDOG_SEC=N): after N seconds, suspend every other thread and
 * dump its host RIP. The exe is no-ASLR, so RIP maps to func_<addr> via nm --
 * this locates PPU-only spin loops (no HLE call, no crash) that are invisible to
 * the HLE trace and the crash handler, and that gdb can't reach under the host
 * commit limit. */
extern "C" void ppu_blockmark_dump(void);
extern "C" long movie_hle_overlay_done(void);   /* movie_hle.c (C linkage) */
extern uint32_t g_movie_eos_ea;                 /* ppu_loader.cpp EOS read-hook (C++ linkage) */
extern "C" void movie_vt_clear_overlay_done(void);   /* winport: mingw */ /* win_compat.c */
extern "C" const char* movie_hle_cache_path(void);   /* winport: movie timing */
extern "C" void movie_hle_from_guest(const char*);
extern "C" int  movie_hle_start_pending_ingame(void);
extern "C" void movie_hle_stop(void);
static DWORD WINAPI ps3_watchdog(LPVOID arg)
{
    DWORD secs = (DWORD)(uintptr_t)arg;
    DWORD myproc = GetCurrentProcessId(), mytid = GetCurrentThreadId();
    /* Loop instead of firing once: a single snapshot can't tell a genuine
     * permanent deadlock apart from a fast release/reacquire loop that the
     * sample just happened to catch mid-hold. Repeated snapshots showing the
     * *same* holder tid + acquire_ra across multiple intervals is the actual
     * deadlock signal; a healthy loop should show it churning. */
    for (DWORD elapsed = secs; ; elapsed += secs) {
    Sleep(secs * 1000);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te; te.dwSize = sizeof(te);
    fprintf(stderr, "\n[watchdog] +%lus — giant lock held by tid=%ld acquire_ra=%p — thread RIPs (map via nm):\n",
            elapsed, ppu_giant_lock_current_holder(), ppu_giant_lock_current_holder_ra());
    ppu_blockmark_dump();
    long holder_tid = ppu_giant_lock_current_holder();
    uintptr_t modbase = (uintptr_t)GetModuleHandleW(NULL);
    /* winport: watchdog prints only after ResumeThread */
    /* The old loop called fprintf while the thread it had just suspended was still
     * suspended. When that thread held the CRT stderr lock (it was mid-fprintf, which the
     * trace-heavy runs make likely), the watchdog blocked forever with the thread still
     * suspended, and every other thread that logged -- the giant-lock holder and the movie
     * overlay included -- blocked behind it: the v14 "freeze" at +150 s (profile_v14:
     * the giant-lock holder parked in fprintf for 12 windows). Capture under suspension,
     * print after resuming. */
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != myproc || te.th32ThreadID == mytid) continue;
            HANDLE h = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (!h) continue;
            const int is_holder = ((long)te.th32ThreadID == holder_tid);
            unsigned long long rip_ = 0, fr_rbp[40], fr_ret[40], sc_val[100];
            unsigned sc_off[100];
            int got_ = 0, nfr = 0, nsc = 0;
            SuspendThread(h);
            CONTEXT c; c.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(h, &c)) {
                got_ = 1; rip_ = c.Rip;
                if (is_holder) {
                    unsigned long long rbp = c.Rbp, prev = c.Rsp;
                    for (int depth = 0; depth < 40; depth++) {
                        if (rbp < prev || rbp - prev > 0x100000 || (rbp & 7)) break;
                        MEMORY_BASIC_INFORMATION mbi;
                        if (!VirtualQuery((void*)rbp, &mbi, sizeof mbi) ||
                            mbi.State != MEM_COMMIT ||
                            (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                            break;
                        unsigned long long* frame = (unsigned long long*)rbp;
                        fr_rbp[nfr] = rbp; fr_ret[nfr] = frame[1]; nfr++;
                        if (!frame[0]) break;
                        prev = rbp; rbp = frame[0];
                    }
                    unsigned long long* sp = (unsigned long long*)c.Rsp;
                    for (int i = 0; i < 400 && nsc < 100; i++) {
                        MEMORY_BASIC_INFORMATION mbi;
                        if (!VirtualQuery(&sp[i], &mbi, sizeof mbi) ||
                            mbi.State != MEM_COMMIT ||
                            (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                            break;
                        unsigned long long v = sp[i];
                        if (v >= modbase + 0x1000 && v < modbase + 0x08000000ull) {
                            sc_off[nsc] = (unsigned)(i * 8); sc_val[nsc] = v; nsc++;
                        }
                    }
                }
            }
            ResumeThread(h);
            CloseHandle(h);
            if (!got_) continue;
            fprintf(stderr, "[watchdog]   tid=%lu rip=0x%016llX\n", te.th32ThreadID, rip_);
            if (is_holder) {
                fprintf(stderr, "  rbp frame walk (module base=0x%llX):\n", (unsigned long long)modbase);
                for (int k = 0; k < nfr; k++)
                    fprintf(stderr, "    #%d rbp=0x%016llX ret=0x%016llX%s\n", k, fr_rbp[k], fr_ret[k],
                            (fr_ret[k] >= modbase + 0x1000 && fr_ret[k] < modbase + 0x08000000ull) ? "" : "  (outside module)");
                fprintf(stderr, "  stack scan (module base=0x%llX):\n", (unsigned long long)modbase);
                for (int k = 0; k < nsc; k++)
                    fprintf(stderr, "    [rsp+0x%X] = 0x%016llX\n", sc_off[k], sc_val[k]);
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    fflush(stderr);
    }
    return 0;
}

#ifdef _WIN32
/* winport release (winport/host/wp_release.c): with no argument the exe runs EBOOT.ELF from its own folder. */
extern "C" const char* wp_release_default_eboot(void);
#endif
int main(int argc, char** argv)
{
#ifdef _WIN32
    static char* wp_argv[3];
    if (argc < 2 && wp_release_default_eboot()) {
        wp_argv[0] = argv[0]; wp_argv[1] = (char*)wp_release_default_eboot(); wp_argv[2] = NULL;
        argv = wp_argv; argc = 2;
    }
#endif
    if (argc < 2) { printf("usage: %s <EBOOT.elf>\n", argv[0]); return 2; }
    fprintf(stderr, "[BASE] module_base=%p\n", (void*)GetModuleHandle(NULL)); fflush(stderr);
    SetUnhandledExceptionFilter(ps3_crash_handler);
    { const char* w = getenv("PS3_WATCHDOG_SEC");
      if (w) CreateThread(NULL, 0, ps3_watchdog, (LPVOID)(uintptr_t)strtoul(w, 0, 10), 0, NULL); }
    /* PS3_TRACE_MOVIEOBJ: sample the movie/demo-player singleton ([TOC-0x1124]
     * slot at 0x540054 -> object) once a second: state word +0x620, the
     * command flags +0x71C/+0x714/+0x718, +0x744/+0x746 (see func_002C07xx).
     * Read-only, commit-checked; shows where the SMPD playback state machine
     * parks while the studio logo never finishes. */
    /* The thread now runs if EITHER PS3_TRACE_MOVIEOBJ (verbose sampling) OR
     * PS3_MOVIE_EOS (the EOS-arm HLE) is set. Previously the arm lived inside a
     * thread that only existed under TRACE_MOVIEOBJ, so PS3_MOVIE_EOS=1 alone
     * NEVER armed the read-hook (silent no-op). Prints are split: [MOVIEOBJ]/
     * [MOVIEOP] verbose dumps stay gated by TRACE_MOVIEOBJ; the edge-triggered
     * [MOVIEFSM] state-transition log and the EOS arm run under either flag. */
    { const char* mo = getenv("PS3_TRACE_MOVIEOBJ");
      const char* eos_env = getenv("PS3_MOVIE_EOS");
      if (mo || eos_env) CreateThread(NULL, 0, [](LPVOID)->DWORD {
          static int s_mo = getenv("PS3_TRACE_MOVIEOBJ") ? 1 : 0;
          static int s_eos = getenv("PS3_MOVIE_EOS") ? 1 : 0;
          static uint32_t s_dumped_io = 0;
          static uint32_t s_fsm_prev = 0xFFFFFFFFu; /* edge-trigger for [MOVIEFSM] */
          for (;;) {
              Sleep(200);
              if (!vm_base) continue;
              MEMORY_BASIC_INFORMATION mbi;
              const uint8_t* slot = vm_base + 0x540054;
              if (!VirtualQuery(slot, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) continue;
              uint32_t obj = ((uint32_t)slot[0]<<24)|((uint32_t)slot[1]<<16)|((uint32_t)slot[2]<<8)|slot[3];
              if (!obj || obj > 0x4F000000u) { if (s_mo) fprintf(stderr, "[MOVIEOBJ] obj=0x%08X (unset)\n", obj); continue; }
              const uint8_t* p = vm_base + obj;
              if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) continue;
              #define MO32(off) (((uint32_t)p[(off)]<<24)|((uint32_t)p[(off)+1]<<16)|((uint32_t)p[(off)+2]<<8)|p[(off)+3])
              /* Edge-triggered FSM log: one line only when st620 changes value.
               * Cheap and always printed (either flag), so the parked state is
               * legible without the 5x/s [MOVIEOBJ] spam. */
              { uint32_t st = MO32(0x620);
                if (st != s_fsm_prev) {
                    fprintf(stderr, "[MOVIEFSM] st620 %u -> %u  f744=%u f746=%u eos_ea=0x%08X overlay_done=%d\n",
                            s_fsm_prev, st, p[0x744], p[0x746], g_movie_eos_ea, movie_hle_overlay_done()?1:0);
                    fflush(stderr);
                    if (st == 0u && g_movie_eos_ea) {   /* winport: mingw */ /* idle: disarm */
                        fprintf(stderr, "[MOVIEEOS] clear hook 0x%08X on st620->0\n", g_movie_eos_ea);
                        g_movie_eos_ea = 0;
                    }
                    if (s_fsm_prev == 0u && st >= 1u && st != 0xFFFFFFFFu) {   /* later Play */
                        movie_vt_clear_overlay_done();
                        fprintf(stderr, "[MOVIEEOS] clear overlay_done on st620 0->%u\n", st);
                    }
                    s_fsm_prev = st;
                } }
              { static uint32_t s_wp_prev = 0xFFFFFFFFu;   /* winport: movie timing */
                static int s_wp_intro = 0, s_wp_ingame = 0;
                uint32_t st_ = MO32(0x620);
                if (st_ != s_wp_prev) {
                    if (!s_wp_intro && s_wp_prev == 0u && st_ >= 1u && st_ != 0xFFFFFFFFu) {
                        s_wp_intro = 1;
                        if (!movie_hle_cache_path()) {
                            fprintf(stderr, "[movie] intro player active (st620 %u) and no movie opened through movie_io -> start SmLogo_v2 from the cache\n", st_);
                            movie_hle_from_guest("/_movies/SmLogo_v2.m2v");
                        }
                    }
                    if (st_ == 11u && s_wp_prev != 11u && movie_hle_start_pending_ingame())
                        s_wp_ingame = 1;
                    if (st_ == 0u && s_wp_ingame) {
                        s_wp_ingame = 0;
                        fprintf(stderr, "[movie] player idle after the in-game picture -> stop the overlay\n");
                        movie_hle_stop();
                    }
                    s_wp_prev = st_;
                } }
              /* Follow the FIOS op the state-1 poll (func_002B4224) waits on:
               * op-container = obj+0x62C; io = [container+8]; done = [io+0x90]. */
              /* Read the completion word [io+0x90] of an FIOS io handle. */
              auto iodone = [&](uint32_t io)->long long {
                  if (!io || io >= 0x4F000000u) return -1;
                  const uint8_t* q = vm_base + io; MEMORY_BASIC_INFORMATION m2;
                  if (!VirtualQuery(q, &m2, sizeof m2) || m2.State != MEM_COMMIT) return -2;
                  return (long long)(((uint32_t)q[0x90]<<24)|((uint32_t)q[0x91]<<16)|((uint32_t)q[0x92]<<8)|q[0x93]);
              };
              uint32_t io_open = MO32(0x630);   /* open handle (out of the m2v FIOS open) */
              uint32_t io_read = MO32(0x634);   /* handle state-1 polls (func_002B4224) */
              /* HLE part 2: once the host overlay finished the logo video, arm
               * the vm_read8 EOS hook on [obj+0x744] so the player advances.
               * Gated by PS3_MOVIE_EOS. EMPIRICAL (visual_trace_test.log): the
               * FSM runs 0->1->11 and PARKS at 11; overlay_done only turns true
               * once st620 is already 11, so the old "== 1" guard never armed.
               * We now arm one-shot on overlay_done in ANY active state (the
               * one-shot g_movie_eos_ea==0 check already guarantees a single
               * arm) and log the state we armed in for diagnosis. */
              if (s_eos && !g_movie_eos_ea && movie_hle_overlay_done() &&   /* winport: mingw */
                  MO32(0x620) >= 5u && MO32(0x620) != 0xFFFFFFFFu) {   /* stream opened */
                  uint32_t st_arm = MO32(0x620);
                  g_movie_eos_ea = obj + 0x744;
                  fprintf(stderr,"[MOVIEEOS] overlay done (st620=%u) -> arming EOS read-hook at 0x%08X ([obj+0x744])\n", st_arm, g_movie_eos_ea); fflush(stderr);
              }
              /* One-shot dump of the read op the state-1 poll waits on: reveals
               * which file/offset/kind the movie player asked FIOS to fetch. */
              if (s_mo && io_read && io_read < 0x4F000000u && io_read != s_dumped_io) {
                  const uint8_t* q = vm_base + io_read; MEMORY_BASIC_INFORMATION m3;
                  if (VirtualQuery(q, &m3, sizeof m3) && m3.State == MEM_COMMIT) {
                      s_dumped_io = io_read;
                      fprintf(stderr, "[MOVIEOP] read-io 0x%08X:", io_read);
                      for (int i = 0; i < 0xA0; i += 4)
                          fprintf(stderr, "%s%02X%02X%02X%02X", (i%32)?" ":"\n[MOVIEOP]  ", q[i],q[i+1],q[i+2],q[i+3]);
                      fprintf(stderr, "\n"); fflush(stderr);
                  }
              }
              if (s_mo)
                  fprintf(stderr, "[MOVIEOBJ] obj=0x%08X st620=%u f714=0x%X f71C=%u f744=%u f746=%u | 60C=0x%08X 650=%u 66C=%u | open=0x%08X d=%lld read=0x%08X d=%lld\n",
                      obj, MO32(0x620), MO32(0x714), p[0x71C], p[0x744], p[0x746],
                      MO32(0x60C), MO32(0x650), MO32(0x66C),
                      io_open, iodone(io_open), io_read, iodone(io_read));
              #undef MO32
              fflush(stderr);
          }
          return 0; }, NULL, 0, NULL); }

    /* Unbuffered so HLE/CRT logs are visible even when the boot hangs (a fully
     * buffered stdout never flushes while the process is stuck). */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    /* Sparse guest RAM: a flat calloc of the full 3.5 GB span fails the host
     * commit limit, so reserve the whole range and commit only the regions the
     * address map actually uses -- the low area (image, heap, main stack, TLS,
     * mmapper) and the PPU thread-stack region at 0xD0000000. The gap between
     * stays reserved (untouched by the boot). */
    vm_base = (uint8_t*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE, PAGE_READWRITE);
    if (!vm_base) { printf("vm reserve failed\n"); return 1; }
    /* Commit sizes are env-tunable so a memory-pressured host can run a
     * reduced footprint (the early boot only touches main mem + TLS/mmapper +
     * a few thread stacks; RSX/SPU regions stay reserved until used). Defaults
     * preserve the original full commit. */
    { const char* e = getenv("PS3_VM_LOW_MB");
      uint64_t low = e ? ((uint64_t)strtoul(e, 0, 10) << 20) : 0x51000000u;
      if (!VirtualAlloc(vm_base, (SIZE_T)low, MEM_COMMIT, PAGE_READWRITE)) {
          printf("vm commit (low) failed, GetLastError=%lu vm_base=%p size=0x%llX\n",
                 GetLastError(), (void*)vm_base, (unsigned long long)low); return 1; }
      ppu_register_committed_range(0, (uint32_t)low); }
    { const char* e = getenv("PS3_VM_STACK_MB");
      uint64_t stk = e ? ((uint64_t)strtoul(e, 0, 10) << 20) : 0x10000000u;
      if (!VirtualAlloc(vm_base + 0xD0000000u, (SIZE_T)stk, MEM_COMMIT, PAGE_READWRITE)) {
          printf("vm commit (stacks) failed\n"); return 1; }
      ppu_register_committed_range(0xD0000000u, 0xD0000000u + (uint32_t)stk);
      if (getenv("PS3_SELFTEST_STACKBAND")) {
          fprintf(stderr, "[selftest] probing stack band 0x%08X..0x%08X at page granularity...\n",
                  0xD0000000u, 0xD0000000u + (uint32_t)stk);
          fflush(stderr);
          uint32_t bad = 0;
          for (uint64_t off = 0; off < stk; off += 0x1000) {
              volatile uint8_t* p = vm_base + 0xD0000000u + off;
              uint8_t v = *p;
              *p = (uint8_t)(v ^ 0xFF);
              if (*p != (uint8_t)(v ^ 0xFF)) bad++;
              *p = v;
          }
          fprintf(stderr, "[selftest] stack band probe done, pages_bad=%u\n", bad);
          fflush(stderr);
      } }
    /* Commit the sys_memory_allocate / sys_mmapper window (0x40000000-0x50000000):
     * the guest C heap acquires its arena here (sys_memory.c bump allocator), so
     * it must be backed even when PS3_VM_LOW_MB trims the low commit below 0x40000000.
     * Env-tunable (PS3_VM_MMAPPER_MB) for memory-pressured hosts. */
    { const char* e = getenv("PS3_VM_MMAPPER_MB");
      uint64_t mm = e ? ((uint64_t)strtoul(e, 0, 10) << 20) : 0x20000000u;  /* default 512 MB = full window (matches SYS_MEM_ALLOC_END) */
      if (mm && !VirtualAlloc(vm_base + 0x40000000u, (SIZE_T)mm, MEM_COMMIT, PAGE_READWRITE)) {
          printf("vm commit (sys_memory window) failed\n"); return 1; }
      if (mm) ppu_register_committed_range(0x40000000u, 0x40000000u + (uint32_t)mm); }
    /* Commit the RSX local-memory region (0xC0000000, cellGcmGetConfiguration's
     * localAddress/localSize = 256 MB). The GPU allocator places its arena here
     * and the render path writes framebuffers/labels into it (e.g. 0xC0400000).
     * Env-tunable (PS3_VM_RSX_MB) for memory-pressured hosts. */
    { const char* e = getenv("PS3_VM_RSX_MB");
      uint64_t rsx = e ? ((uint64_t)strtoul(e, 0, 10) << 20) : 0x10000000u;  /* default 256 MB */
      if (rsx && !VirtualAlloc(vm_base + 0xC0000000u, (SIZE_T)rsx, MEM_COMMIT, PAGE_READWRITE)) {
          printf("vm commit (RSX local) failed\n"); return 1; }
      if (rsx) ppu_register_committed_range(0xC0000000u, 0xC0000000u + (uint32_t)rsx); }
    extern uint32_t ppu_vm_size; ppu_vm_size = VM_SIZE;   /* enable OOB guard */

    uint32_t entry = ppu_load_elf(argv[1]);
    if (!entry) { printf("load failed\n"); return 1; }

    derive_vfs_root(argv[1]);
    printf("[boot] VFS root: %s\n", ppu_vfs_root);

    g_ps3_guest_caller = boot_guest_caller;            /* HLE bridges re-enter guest code */
    g_ppu_thread_entry_trampoline = boot_thread_trampoline; /* guest threads run lifted code */

    ppu_recomp_register();   /* lifted function table -> address map */
    ppu_hle_init();          /* firmware import NID -> HLE handlers */
    ppu_sysprx_register();   /* boot-critical CRT (sys_initialize_tls, ...) */
    ppu_fs_register();       /* cellFs VFS over the real game directory */
    lv2_init_syscalls();     /* real lv2 syscall table (event/spu/mutex/fs/...) */
    ppu_resolve_imports();   /* patch .lib.stub slots -> HLE bridge */

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n", entry, STACK_TOP);
    int rc = ppu_run(entry, STACK_TOP);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);
    return 0;
}
