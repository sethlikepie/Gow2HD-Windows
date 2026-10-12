/*
 * win_compat.c -- Windows-only definitions for symbols the GoW2 port references that exist
 * only in its macOS host pieces. Linked into g2play.exe by winport/build_windows.sh.
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <mmsystem.h>

/* Windows' default timer tick is 15.6 ms, so every Sleep(1)/usleep(<16 ms) in the runtime (SPURS
 * 2 ms task tick, lwcond 100 ms slices, giant-lock yields, vdec BUSY backoff) really sleeps
 * ~15.6 ms -- the macOS/Linux builds these paths were tuned on sleep ~1 ms. Ask for 1 ms for the
 * life of the process, like most emulators and games do. PS3_WIN_TIMER_1MS=0 skips it (A/B). */
__attribute__((constructor)) static void winport_timer_resolution(void)
{
    const char* e = getenv("PS3_WIN_TIMER_1MS");
    if (e && *e == '0') return;
    timeBeginPeriod(1);
}

/* ---------------------------------------------------------------------------------------------
 * Sampling profiler (PS3_PROFILE=1). Guest PPU code only runs while its host thread holds the
 * runtime's giant lock, so every 2 ms this samples the instruction pointer of whichever thread
 * holds it (suspend -> GetThreadContext -> resume, nothing else while it is suspended). Every
 * PS3_PROFILE_WINDOW_SEC seconds (default 15) the top addresses and holder threads are appended
 * to PS3_PROFILE_OUT (default logs/profile.txt) and the counters restart, so each window is its
 * own profile. The exe is linked at a fixed base (0x140000000), so the addresses map straight to
 * symbols (func_XXXXXXXX = lifted guest function at that PS3 address).
 * -------------------------------------------------------------------------------------------*/
#include <stdio.h>
extern long ppu_giant_lock_current_holder(void);

#define PROF_SLOTS 16384
static struct { DWORD64 rip; unsigned n; } s_prof[PROF_SLOTS];
/* Same histogram keyed by the first stack frame inside g2play.exe: when the sampled thread is
 * inside Windows (ntdll, the GPU driver, user32...), this names OUR code that called it. */
static struct { DWORD64 rip; unsigned n; } s_prof_exe[PROF_SLOTS];
static DWORD64 s_exe_lo, s_exe_hi;
static void prof_exe_range(void)
{
    HMODULE m = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)m;
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)((BYTE*)m + dos->e_lfanew);
    s_exe_lo = (DWORD64)m; s_exe_hi = s_exe_lo + nt->OptionalHeader.SizeOfImage;
}

/* winport v35: lock-free unwind lookup for the samplers. RtlLookupFunctionEntry takes loader / function-table locks;
 * calling it while the target thread is SUSPENDED deadlocks when that thread holds one of them (seen 2026-10-06: the
 * whole process froze ~35 s into combat-bench runs with PS3_TPROF=1, never with it off). Instead: binary-search the
 * .pdata of the exe and of every module in a table refreshed OUTSIDE the suspend window (tp_mod_refresh), and bound
 * every stack read to the target's stack region (VirtualQuery is a plain syscall, no user-mode lock). */
#define TP_MAXMOD 256
BOOL WINAPI K32EnumProcessModules(HANDLE, HMODULE*, DWORD, LPDWORD);   /* kernel32 (psapi v2) */
typedef struct { DWORD64 lo, hi; PRUNTIME_FUNCTION rf; DWORD n; } TpMod;
static TpMod s_tpmod[TP_MAXMOD];
static volatile LONG s_tpmod_n;
static void tp_mod_add(DWORD64 base, int slot)
{
    /* only real image mappings: data/resource-mapped modules (LOAD_LIBRARY_AS_DATAFILE) have flag bits in the
     * handle and file-offset layout, so RVAs point outside the mapping (v35 first try crashed there). */
    s_tpmod[slot].lo = s_tpmod[slot].hi = 0; s_tpmod[slot].rf = NULL; s_tpmod[slot].n = 0;
    if (base & 0xFFFF) return;
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery((LPCVOID)base, &mi, sizeof mi) || mi.Type != MEM_IMAGE || mi.State != MEM_COMMIT) return;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096) return;
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return;
    IMAGE_DATA_DIRECTORY* d = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (d->Size && (DWORD64)d->VirtualAddress + d->Size > nt->OptionalHeader.SizeOfImage) return;
    s_tpmod[slot].lo = base; s_tpmod[slot].hi = base + nt->OptionalHeader.SizeOfImage;
    s_tpmod[slot].rf = d->Size ? (PRUNTIME_FUNCTION)(base + d->VirtualAddress) : NULL;
    s_tpmod[slot].n = d->Size / sizeof(RUNTIME_FUNCTION);
}
static void tp_mod_refresh(void)   /* call only while NO thread is suspended */
{
    HMODULE mods[TP_MAXMOD]; DWORD need = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need)) return;
    int n = (int)(need / sizeof(HMODULE)); if (n > TP_MAXMOD) n = TP_MAXMOD;
    s_tpmod_n = 0; MemoryBarrier();
    for (int i = 0; i < n; i++) tp_mod_add((DWORD64)mods[i], i);
    MemoryBarrier(); s_tpmod_n = n;
}
static PRUNTIME_FUNCTION tp_lookup(DWORD64 rip, DWORD64* base)
{
    int n = s_tpmod_n;
    for (int i = 0; i < n; i++) {
        const TpMod* m = &s_tpmod[i];
        if (rip < m->lo || rip >= m->hi || !m->rf) continue;
        DWORD rva = (DWORD)(rip - m->lo); DWORD a = 0, b = m->n;
        while (a < b) {
            DWORD k = (a + b) / 2; const RUNTIME_FUNCTION* f = &m->rf[k];
            if (rva < f->BeginAddress) b = k; else if (rva >= f->EndAddress) a = k + 1;
            else { *base = m->lo; return (PRUNTIME_FUNCTION)f; }
        }
        return NULL;
    }
    return NULL;
}
static DWORD64 s_tp_stk_lo, s_tp_stk_hi;   /* stack region of the thread being walked (sampler thread only) */
static void tp_stack_bounds(DWORD64 rsp)
{
    MEMORY_BASIC_INFORMATION mi; s_tp_stk_lo = s_tp_stk_hi = 0;
    if (VirtualQuery((LPCVOID)rsp, &mi, sizeof mi) && mi.State == MEM_COMMIT) {
        s_tp_stk_lo = (DWORD64)mi.BaseAddress; s_tp_stk_hi = s_tp_stk_lo + mi.RegionSize; }
}
static int tp_stack_ok(DWORD64 a) { return a >= s_tp_stk_lo && a + 8 <= s_tp_stk_hi; }
/* unwinding reads saved registers ABOVE rsp (frame size + pushes): keep a margin, or a bogus/top frame faults at the
 * end of the stack region (v35 second try: AV inside RtlVirtualUnwind on a 64 KB boundary). */
static int tp_unw_ok(DWORD64 a) { return a >= s_tp_stk_lo && a + 0x4000 <= s_tp_stk_hi; }
/* RtlVirtualUnwind reads the frame's saved registers from the stack: refuse frames whose RSP left the region. */
/* Walk the suspended thread's stack with the x64 unwind tables (.pdata; the OS DLLs and our
 * clang/mingw objects all carry them) until a frame lands inside the exe. Frameless leaf
 * functions have no entry: their return address is at [rsp]. */
static DWORD64 prof_first_exe_frame(const CONTEXT* c0)
{
    CONTEXT c = *c0;
    for (int d = 0; d < 32; d++) {
        DWORD64 rip = c.Rip;
        if (!rip) return 0;
        if (rip >= s_exe_lo && rip < s_exe_hi) return rip;
        DWORD64 base = 0;
        PRUNTIME_FUNCTION fn = tp_lookup(rip, &base);
        if (!fn) {
            if (!tp_stack_ok(c.Rsp)) return 0;
            c.Rip = *(DWORD64*)c.Rsp; c.Rsp += 8;
        } else {
            PVOID hd = NULL; DWORD64 ef = 0;
            if (!tp_unw_ok(c.Rsp)) return 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, rip, fn, &c, &hd, &ef, NULL);
        }
    }
    return 0;
}
static void prof_record_exe(DWORD64 rip)
{
    unsigned h = (unsigned)((rip * 0x9E3779B97F4A7C15ull) >> 50) & (PROF_SLOTS - 1);
    for (unsigned i = 0; i < PROF_SLOTS; i++) {
        unsigned k = (h + i) & (PROF_SLOTS - 1);
        if (s_prof_exe[k].rip == rip) { s_prof_exe[k].n++; return; }
        if (s_prof_exe[k].rip == 0) { s_prof_exe[k].rip = rip; s_prof_exe[k].n = 1; return; }
    }
}
static struct { DWORD tid; unsigned n; } s_prof_tid[64];
static unsigned s_prof_total, s_prof_idle, s_prof_fail;

static void prof_record(DWORD64 rip)
{
    unsigned h = (unsigned)((rip * 0x9E3779B97F4A7C15ull) >> 50) & (PROF_SLOTS - 1);
    for (unsigned i = 0; i < PROF_SLOTS; i++) {
        unsigned k = (h + i) & (PROF_SLOTS - 1);
        if (s_prof[k].rip == rip) { s_prof[k].n++; return; }
        if (s_prof[k].rip == 0) { s_prof[k].rip = rip; s_prof[k].n = 1; return; }
    }
}
static void prof_record_tid(DWORD tid)
{
    for (int i = 0; i < 64; i++) {
        if (s_prof_tid[i].tid == tid) { s_prof_tid[i].n++; return; }
        if (s_prof_tid[i].tid == 0) { s_prof_tid[i].tid = tid; s_prof_tid[i].n = 1; return; }
    }
}
static int prof_cmp(const void* a, const void* b)
{
    unsigned x = ((const unsigned*)a)[2], y = ((const unsigned*)b)[2];   /* .n after DWORD64 */
    return (x < y) - (x > y);
}
static void prof_dump(const char* path, int window, unsigned secs)
{
    FILE* f = fopen(path, window == 1 ? "w" : "a");
    if (!f) return;
    fprintf(f, "=== window %d (%u s): samples=%u giant-idle=%u sample-fail=%u\n",
            window, secs, s_prof_total, s_prof_idle, s_prof_fail);
    fprintf(f, "holder threads:");
    for (int i = 0; i < 64 && s_prof_tid[i].tid; i++)
        fprintf(f, " %lu:%u", (unsigned long)s_prof_tid[i].tid, s_prof_tid[i].n);
    fprintf(f, "\n");
    qsort(s_prof, PROF_SLOTS, sizeof s_prof[0], prof_cmp);
    fprintf(f, "-- top of stack\n");
    for (int i = 0; i < 60 && s_prof[i].n; i++)
        fprintf(f, "%8u  %5.1f%%  0x%016llX\n", s_prof[i].n,
                s_prof_total ? 100.0 * s_prof[i].n / s_prof_total : 0.0,
                (unsigned long long)s_prof[i].rip);
    qsort(s_prof_exe, PROF_SLOTS, sizeof s_prof_exe[0], prof_cmp);
    fprintf(f, "-- first frame inside g2play.exe\n");
    for (int i = 0; i < 60 && s_prof_exe[i].n; i++)
        fprintf(f, "%8u  %5.1f%%  E 0x%016llX\n", s_prof_exe[i].n,
                s_prof_total ? 100.0 * s_prof_exe[i].n / s_prof_total : 0.0,
                (unsigned long long)s_prof_exe[i].rip);
    fclose(f);
    memset(s_prof, 0, sizeof s_prof); memset(s_prof_exe, 0, sizeof s_prof_exe);
    memset(s_prof_tid, 0, sizeof s_prof_tid);
    s_prof_total = s_prof_idle = s_prof_fail = 0;
}
static DWORD WINAPI prof_thread(LPVOID arg)
{
    (void)arg;
    const char* out = getenv("PS3_PROFILE_OUT"); if (!out || !*out) out = "logs/profile.txt";
    const char* w = getenv("PS3_PROFILE_WINDOW_SEC");
    unsigned win_ms = (w && atoi(w) > 0) ? (unsigned)atoi(w) * 1000u : 15000u;
    DWORD last_tid = 0; HANDLE h = NULL;
    DWORD t0 = GetTickCount(); int window = 0;
    prof_exe_range(); tp_mod_refresh();   /* winport v35 */
    for (;;) {
        Sleep(2);
        DWORD tid = (DWORD)ppu_giant_lock_current_holder();
        s_prof_total++;
        if (!tid) { s_prof_idle++; }
        else {
            if (tid != last_tid) {
                if (h) CloseHandle(h);
                h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                               FALSE, tid);
                last_tid = tid;
            }
            DWORD64 rip = 0, exe = 0;
            if (h && SuspendThread(h) != (DWORD)-1) {
                CONTEXT c; memset(&c, 0, sizeof c); c.ContextFlags = CONTEXT_FULL;
                if (GetThreadContext(h, &c)) { rip = c.Rip; tp_stack_bounds(c.Rsp); exe = prof_first_exe_frame(&c); }
                ResumeThread(h);
            }
            if (rip) { prof_record(rip); prof_record_tid(tid); if (exe) prof_record_exe(exe); }
            else s_prof_fail++;
        }
        if (GetTickCount() - t0 >= win_ms) { prof_dump(out, ++window, win_ms / 1000u); t0 = GetTickCount(); }
    }
    return 0;
}
__attribute__((constructor)) static void winport_profiler_start(void)
{
    const char* e = getenv("PS3_PROFILE");
    if (!e || *e != '1') return;
    HANDLE t = CreateThread(NULL, 0, prof_thread, NULL, 0, NULL);
    if (t) { SetThreadPriority(t, THREAD_PRIORITY_TIME_CRITICAL); CloseHandle(t); }
}

/* ---------------------------------------------------------------------------------------------
 * v23: all-thread CPU profiler (PS3_TPROF=1).  The old profiler above only samples whichever thread holds the
 * PPU giant lock, which cannot show SPU workers, the RSX/command thread, audio, or where the PPU spends time
 * OUTSIDE the lock.  This one, every window (PS3_TPROF_WINDOW_SEC, default 5 s):
 *   - reads the CPU time of EVERY thread (GetThreadTimes) -> "tid, cpu% of one core, start address";
 *   - samples every thread that is busy (>4% CPU in the last 250 ms): suspend -> GetThreadContext -> walk the
 *     x64 unwind info to the first frame inside g2play.exe -> histogram per thread (+ how often the thread was
 *     inside Windows/driver code instead of our code).
 * Output: one [TPROF] line per busy thread in the run log, the full histograms in PS3_TPROF_OUT
 * (default logs/tprof.txt). Resolve the 0x14... addresses with `llvm-nm` of the exe (same as the old profile).
 * -------------------------------------------------------------------------------------------*/
#include <tlhelp32.h>
#define TP_MAXT  128
#define TP_SLOTS 4096
typedef struct { DWORD64 rip; unsigned n; } TpSlot;
typedef struct { DWORD64 a, b, c; unsigned n; } TpChain;   /* function starts: innermost exe frame, its caller, its caller's caller */
#define TP_CHAINS 1024
typedef struct {
    DWORD tid; HANDLE h; int dead;
    ULONGLONG cpu_total, cpu_win0, cpu_250;   /* 100 ns units */
    ULONG_PTR start; char name[40];
    int hot; unsigned samples, os_samples;
    TpSlot* exe;        /* first frame inside g2play.exe (allocated when the thread first becomes hot) */
    TpSlot* raw;        /* raw rip when the thread is inside the OS/driver */
    TpChain* chain;     /* v24: innermost 3 exe functions */
    struct TpFn* fn;    /* winport v25: per-FUNCTION histogram (100% coverage, unlike the top-25 address slots) */
    TpChain* gchain;    /* winport v25: guest (func_*) chains: innermost func_* <- 2 nearest func_* callers */
    unsigned fn_drop;   /* samples that did not fit in the fn table */
} TpThread;
/* winport v25: per-thread function aggregation. key = function start (1 = no exe frame at all = pure OS). */
#define TP_FN 8192
typedef struct TpFn { DWORD64 fn; unsigned n, os; } TpFn;
static TpThread s_tp[TP_MAXT];
static int s_tp_n;
typedef LONG (NTAPI *WpNtQIT)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef HRESULT (WINAPI *WpGetThreadDesc)(HANDLE, PWSTR*);
/* ---- v24: in-process symbol names (COFF symbol table of our own exe) ---- */
static DWORD64* s_ts_addr; static const char** s_ts_name; static int s_ts_n; static char* s_ts_strs;
typedef struct { DWORD64 a; const char* n; } TsSym;
static int ts_cmp(const void* x, const void* y) { DWORD64 a = ((const TsSym*)x)->a, b = ((const TsSym*)y)->a; return (a > b) - (a < b); }
static void ts_load(void)
{
    char path[MAX_PATH]; if (!GetModuleFileNameA(NULL, path, sizeof path)) return;
    FILE* f = fopen(path, "rb"); if (!f) return;
    IMAGE_DOS_HEADER dos; IMAGE_FILE_HEADER fh; DWORD sig;
    if (fread(&dos, sizeof dos, 1, f) != 1 || fseek(f, dos.e_lfanew, SEEK_SET) || fread(&sig, 4, 1, f) != 1 || fread(&fh, sizeof fh, 1, f) != 1) { fclose(f); return; }
    if (!fh.PointerToSymbolTable || !fh.NumberOfSymbols) { fclose(f); return; }
    IMAGE_OPTIONAL_HEADER64 oh; fread(&oh, sizeof oh < fh.SizeOfOptionalHeader ? sizeof oh : fh.SizeOfOptionalHeader, 1, f);
    fseek(f, (long)dos.e_lfanew + 4 + (long)sizeof fh + fh.SizeOfOptionalHeader, SEEK_SET);
    int ns = fh.NumberOfSections; IMAGE_SECTION_HEADER* sh = (IMAGE_SECTION_HEADER*)calloc((size_t)ns, sizeof *sh);
    if (!sh || fread(sh, sizeof *sh, (size_t)ns, f) != (size_t)ns) { free(sh); fclose(f); return; }
    DWORD nsym = fh.NumberOfSymbols;
    unsigned char* st = (unsigned char*)malloc((size_t)nsym * 18);
    if (!st) { free(sh); fclose(f); return; }
    fseek(f, (long)fh.PointerToSymbolTable, SEEK_SET);
    if (fread(st, 18, nsym, f) != nsym) { free(st); free(sh); fclose(f); return; }
    DWORD slen = 0; if (fread(&slen, 4, 1, f) != 1 || slen < 4) slen = 4;
    char* strs = (char*)calloc(slen + 1, 1); if (strs && slen > 4) fread(strs + 4, 1, slen - 4, f);
    fclose(f);
    TsSym* v = (TsSym*)malloc(sizeof(TsSym) * (size_t)nsym); int nv = 0;
    char* shortn = (char*)calloc((size_t)nsym, 9);   /* private copy of inline 8-byte names */
    for (DWORD i = 0; i < nsym; ) {
        unsigned char* e = st + (size_t)i * 18;
        DWORD value = *(DWORD*)(e + 8); SHORT secn = *(SHORT*)(e + 12);         BYTE cls = e[16], aux = e[17];
        if (secn > 0 && secn <= ns && (sh[secn - 1].Characteristics & IMAGE_SCN_CNT_CODE) && (cls == 2 || cls == 3)) {
            const char* nm;
            if (*(DWORD*)e == 0) { DWORD off = *(DWORD*)(e + 4); nm = (off < slen) ? strs + off : "?"; }
            else { char* d = shortn + (size_t)i * 9; memcpy(d, e, 8); d[8] = 0; nm = d; }
            if (nm[0] && nm[0] != '.') { v[nv].a = (DWORD64)(ULONG_PTR)GetModuleHandleA(NULL) + sh[secn - 1].VirtualAddress + value; v[nv].n = nm; nv++; }
        }
        i += 1 + aux;
    }
    free(st); free(sh);
    qsort(v, (size_t)nv, sizeof(TsSym), ts_cmp);
    s_ts_addr = (DWORD64*)malloc(sizeof(DWORD64) * (size_t)(nv ? nv : 1)); s_ts_name = (const char**)malloc(sizeof(char*) * (size_t)(nv ? nv : 1));
    for (int i = 0; i < nv; i++) { s_ts_addr[i] = v[i].a; s_ts_name[i] = v[i].n; }
    s_ts_n = nv; s_ts_strs = strs; free(v);
}
/* function containing addr: returns its start address (0 if unknown) and name */
static DWORD64 ts_fn(DWORD64 a, const char** name)
{
    if (name) *name = "?";
    if (!s_ts_n || a < s_ts_addr[0]) return 0;
    int lo = 0, hi = s_ts_n - 1;
    while (lo < hi) { int m = (lo + hi + 1) / 2; if (s_ts_addr[m] <= a) lo = m; else hi = m - 1; }
    if (name) *name = s_ts_name[lo];
    return s_ts_addr[lo];
}
static void tp_chain_rec(TpChain* t, DWORD64 a, DWORD64 b, DWORD64 c)
{
    unsigned h = (unsigned)(((a * 0x9E3779B97F4A7C15ull) ^ (b * 0xC2B2AE3D27D4EB4Full) ^ (c * 0x165667B19E3779F9ull)) >> 50) & (TP_CHAINS - 1);
    for (unsigned i = 0; i < TP_CHAINS; i++) {
        unsigned k = (h + i) & (TP_CHAINS - 1);
        if (t[k].n && t[k].a == a && t[k].b == b && t[k].c == c) { t[k].n++; return; }
        if (!t[k].n) { t[k].a = a; t[k].b = b; t[k].c = c; t[k].n = 1; return; }
    }
}
static int tp_chain_cmp(const void* x, const void* y) { unsigned a = ((const TpChain*)x)->n, b = ((const TpChain*)y)->n; return (a < b) - (a > b); }
/* first 3 frames inside the exe (function starts). Returns how many were found. */
static int tp_exe_chain(const CONTEXT* c0, DWORD64 out[3])
{
    CONTEXT c = *c0; int n = 0;
    for (int d = 0; d < 48 && n < 3; d++) {
        DWORD64 rip = c.Rip; if (!rip) break;
        if (rip >= s_exe_lo && rip < s_exe_hi) { DWORD64 f = ts_fn(rip, NULL); out[n++] = f ? f : rip; }
        DWORD64 base = 0; PRUNTIME_FUNCTION fn = tp_lookup(rip, &base);
        if (!fn) { if (!tp_stack_ok(c.Rsp)) break; c.Rip = *(DWORD64*)c.Rsp; c.Rsp += 8; }
        else { PVOID hd = NULL; DWORD64 ef = 0; if (!tp_unw_ok(c.Rsp)) break; RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, rip, fn, &c, &hd, &ef, NULL); }
    }
    for (int k = n; k < 3; k++) out[k] = 0;
    return n;
}
/* winport v25: classification of a symbol name into the tprof_report.py categories (done at report time only). */
enum { TC_GUEST, TC_LOCK, TC_GPU, TC_VM, TC_RSX, TC_VTX, TC_SPU, TC_DBG, TC_OS, TC_OTHER, TC_N };
static const char* const s_tc_name[TC_N] = { "guest_func", "lockwait", "gpuwait", "vm_helpers", "gcm_rsx_render", "vertex_fetch", "spurs_spu", "debug_trace", "OS", "other" };
static const char* tp_unmangle(const char* n) { if (n[0] == '_' && n[1] == 'Z') { n += 2; if (*n == 'L') n++; while (*n >= '0' && *n <= '9') n++; } return n; }
static int tp_is_guest(const char* n) { n = tp_unmangle(n); return !strncmp(n, "func_", 5) || !strncmp(n, "__imp_func", 10); }
static int tp_cat(const char* nm)
{
    const char* n = tp_unmangle(nm);
    if (!strncmp(n, "ppu_giant_lock", 14)) return TC_LOCK;
    if (!strcmp(n, "wait_for_gpu")) return TC_GPU;
    if (tp_is_guest(nm)) return TC_GUEST;
    if (strstr(n, "fetch_attrib") || !strncmp(n, "vpf_", 4) || strstr(n, "rd_half") || strstr(n, "vertex_fetch")) return TC_VTX;
    if (!strncmp(n, "vm_", 3)) return TC_VM;
    if (!strncmp(n, "spurs", 5) || !strncmp(n, "spu", 3)) return TC_SPU;
    if (!strncmp(n, "ps3_gcm", 7) || !strncmp(n, "gcm_", 4) || !strncmp(n, "rsx_", 4) || !strncmp(n, "d3d12", 5) || !strncmp(n, "up_rsx", 6) || !strcmp(n, "render_frame") || strstr(n, "cellGcm")) return TC_RSX;
    if (strstr(n, "trace") || strstr(n, "tprof") || strstr(n, "hook") || !strncmp(n, "prof_", 5) || strstr(n, "dbg") || strstr(n, "debug") || !strncmp(n, "log_", 4)) return TC_DBG;
    return TC_OTHER;
}
static void tp_fn_rec(TpThread* t, DWORD64 fn, int os)
{
    DWORD64 key = fn ? fn : 1;
    unsigned h = (unsigned)((key * 0x9E3779B97F4A7C15ull) >> 40) & (TP_FN - 1);
    for (unsigned i = 0; i < 256; i++) {
        TpFn* e = &t->fn[(h + i) & (TP_FN - 1)];
        if (e->fn == key) { e->n++; e->os += os; return; }
        if (!e->fn) { e->fn = key; e->n = 1; e->os = os; return; }
    }
    t->fn_drop++;
}
static int tp_fn_cmp(const void* x, const void* y) { unsigned a = ((const TpFn*)x)->n, b = ((const TpFn*)y)->n; return (a < b) - (a > b); }
/* nearest 3 frames whose function is guest code (func_*), innermost first; returns how many */
static int tp_guest_chain(const CONTEXT* c0, DWORD64 out[3])
{
    CONTEXT c = *c0; int n = 0;
    for (int d = 0; d < 64 && n < 3; d++) {
        DWORD64 rip = c.Rip; if (!rip) break;
        if (rip >= s_exe_lo && rip < s_exe_hi) { const char* nm; DWORD64 f = ts_fn(rip, &nm); if (f && tp_is_guest(nm)) out[n++] = f; }
        DWORD64 base = 0; PRUNTIME_FUNCTION fn = tp_lookup(rip, &base);
        if (!fn) { if (!tp_stack_ok(c.Rsp)) break; c.Rip = *(DWORD64*)c.Rsp; c.Rsp += 8; }
        else { PVOID hd = NULL; DWORD64 ef = 0; if (!tp_unw_ok(c.Rsp)) break; RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, rip, fn, &c, &hd, &ef, NULL); }
    }
    for (int k = n; k < 3; k++) out[k] = 0;
    return n;
}
/* winport v25: per-window report for one thread: top-40 functions, category split of ALL samples, top-15 guest chains. */
static void tp_report_fn(TpThread* t, int window, int is_main, FILE* f)
{
    char who[32]; if (is_main) strcpy(who, "main"); else snprintf(who, sizeof who, "tid=%lu", (unsigned long)t->tid);
    unsigned tot = 0, cat[TC_N] = {0}, inos = 0;
    for (int k = 0; k < TP_FN; k++) {
        TpFn* e = &t->fn[k]; if (!e->fn) continue;
        const char* nm = "?"; if (e->fn != 1) ts_fn(e->fn, &nm);
        cat[e->fn == 1 ? TC_OS : tp_cat(nm)] += e->n; tot += e->n; inos += e->os;
    }
    tot += t->fn_drop; cat[TC_OTHER] += t->fn_drop;
    if (!tot) return;
    double d = 100.0 / tot;
    char line[512]; int o = snprintf(line, sizeof line, "[TPROF] w%d %s CAT", window, who);
    for (int c = 0; c < TC_N; c++) o += snprintf(line + o, sizeof line - (size_t)o, "%s %s %.1f%%", c ? " |" : "", s_tc_name[c], cat[c] * d);
    snprintf(line + o, sizeof line - (size_t)o, " | (in-OS %.1f%% of %u samples)", inos * d, tot);
    fprintf(stderr, "%s\n", line); if (f) fprintf(f, "  %s\n", line);
    qsort(t->fn, TP_FN, sizeof(TpFn), tp_fn_cmp);
    /* winport v84: PS3_TPROF_FNALL=1 writes EVERY sampled function of the thread to the tprof file (log keeps the top 40) */
    static int s_fnall = -1; if (s_fnall < 0) { const char* fa = getenv("PS3_TPROF_FNALL"); s_fnall = (fa && fa[0] == '1') ? 1 : 0; }
    for (int k = 0; k < (s_fnall ? TP_FN : 40) && t->fn[k].n; k++) {
        const char* nm = "<OS>"; if (t->fn[k].fn != 1) ts_fn(t->fn[k].fn, &nm);
        if (k < 40) fprintf(stderr, "[TPROF] w%d %s F %5.1f%% (os %4.1f%%) %s\n", window, who, t->fn[k].n * d, t->fn[k].os * d, nm);
        if (f) fprintf(f, "  F %6u %5.1f%% os=%u %s\n", t->fn[k].n, t->fn[k].n * d, t->fn[k].os, nm);
    }
    if (is_main && t->gchain) {
        qsort(t->gchain, TP_CHAINS, sizeof(TpChain), tp_chain_cmp);
        for (int k = 0; k < 15 && t->gchain[k].n; k++) {
            const char *n0, *n1, *n2; ts_fn(t->gchain[k].a, &n0); ts_fn(t->gchain[k].b, &n1); ts_fn(t->gchain[k].c, &n2);
            fprintf(stderr, "[TPROF] w%d %s G %5.1f%%  %s <- %s <- %s\n", window, who, t->gchain[k].n * d, n0, t->gchain[k].b ? n1 : "-", t->gchain[k].c ? n2 : "-");
            if (f) fprintf(f, "  G %6u %5.1f%% %s <- %s <- %s\n", t->gchain[k].n, t->gchain[k].n * d, n0, t->gchain[k].b ? n1 : "-", t->gchain[k].c ? n2 : "-");
        }
    }
}
static DWORD s_tp_main_tid;
extern long ppu_giant_lock_current_holder(void);
extern void* ppu_giant_lock_current_holder_ra(void);
/* giant-lock statistics (whole process, per window) */
static unsigned s_gl_rounds, s_gl_free, s_gl_main, s_gl_other;
static unsigned s_gl_main_samples, s_gl_main_waiting, s_gl_wait_free, s_gl_wait_holder_os, s_gl_wait_holder_exe;
static struct { DWORD tid; unsigned n; } s_gl_holders[24];
static TpChain* s_gl_hchain;     /* where the holder was while main waited */
static TpChain* s_gl_ra;         /* where the holder acquired the lock (return address of ppu_giant_lock_acquire) */
static void tp_rec(TpSlot* t, DWORD64 rip)
{
    unsigned h = (unsigned)((rip * 0x9E3779B97F4A7C15ull) >> 52) & (TP_SLOTS - 1);
    for (unsigned i = 0; i < TP_SLOTS; i++) {
        unsigned k = (h + i) & (TP_SLOTS - 1);
        if (t[k].rip == rip) { t[k].n++; return; }
        if (!t[k].rip) { t[k].rip = rip; t[k].n = 1; return; }
    }
}
static int tp_cmp(const void* a, const void* b)
{
    unsigned x = ((const TpSlot*)a)->n, y = ((const TpSlot*)b)->n;
    return (x < y) - (x > y);
}
static ULONGLONG tp_cpu(HANDLE h)
{
    FILETIME c, e, k, u;
    if (!GetThreadTimes(h, &c, &e, &k, &u)) return 0;
    return (((ULONGLONG)k.dwHighDateTime << 32) | k.dwLowDateTime) + (((ULONGLONG)u.dwHighDateTime << 32) | u.dwLowDateTime);
}
static void tp_refresh_threads(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te; te.dwSize = sizeof te;
    static WpNtQIT qit = NULL; static WpGetThreadDesc gtd = NULL;
    if (!qit) { HMODULE nt = GetModuleHandleA("ntdll.dll"); if (nt) qit = (WpNtQIT)(void*)GetProcAddress(nt, "NtQueryInformationThread");
                HMODULE k32 = GetModuleHandleA("kernel32.dll"); if (k32) gtd = (WpGetThreadDesc)(void*)GetProcAddress(k32, "GetThreadDescription"); }
    DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
    for (int i = 0; i < s_tp_n; i++) s_tp[i].dead = 1;
    if (Thread32First(snap, &te)) do {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        int idx = -1;
        for (int i = 0; i < s_tp_n; i++) if (s_tp[i].tid == te.th32ThreadID) { idx = i; break; }
        if (idx < 0) {
            if (s_tp_n >= TP_MAXT) continue;
            idx = s_tp_n++;
            TpThread* t = &s_tp[idx]; memset(t, 0, sizeof *t);
            t->tid = te.th32ThreadID;
            t->h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, t->tid);
            if (!t->h) t->h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, t->tid);
            if (t->h && qit) { ULONG_PTR sa = 0; if (qit(t->h, 9, &sa, sizeof sa, NULL) >= 0) t->start = sa; }
            if (t->h && gtd) { PWSTR nm = NULL; if (SUCCEEDED(gtd(t->h, &nm)) && nm) { WideCharToMultiByte(CP_ACP, 0, nm, -1, t->name, sizeof t->name - 1, NULL, NULL); LocalFree(nm); } }
            t->cpu_win0 = t->cpu_250 = tp_cpu(t->h);
        }
        s_tp[idx].dead = 0;
    } while (Thread32Next(snap, &te));
    CloseHandle(snap);
}
static DWORD WINAPI tprof_thread(LPVOID arg)
{
    (void)arg;
    const char* out = getenv("PS3_TPROF_OUT"); if (!out || !*out) out = "logs/tprof.txt";
    const char* w = getenv("PS3_TPROF_WINDOW_SEC");
    unsigned win_ms = (w && atoi(w) > 0) ? (unsigned)atoi(w) * 1000u : 5000u;
    prof_exe_range(); tp_mod_refresh();   /* winport v35 */
    ts_load();
    s_gl_hchain = (TpChain*)calloc(TP_CHAINS, sizeof(TpChain)); s_gl_ra = (TpChain*)calloc(TP_CHAINS, sizeof(TpChain));
    fprintf(stderr, "[TPROF] started: %d code symbols loaded, main tid=%lu\n", s_ts_n, (unsigned long)s_tp_main_tid);
    tp_refresh_threads();
    DWORD t_win = GetTickCount(), t_250 = t_win; int window = 0; unsigned ticks = 0;
    for (;;) {
        Sleep(2); ticks++;
        for (int i = 0; i < s_tp_n; i++) {
            TpThread* t = &s_tp[i];
            if (!t->hot || !t->h || t->dead) continue;
            if (!t->exe) { t->exe = (TpSlot*)calloc(TP_SLOTS, sizeof(TpSlot)); t->raw = (TpSlot*)calloc(TP_SLOTS, sizeof(TpSlot)); t->chain = (TpChain*)calloc(TP_CHAINS, sizeof(TpChain)); t->fn = (TpFn*)calloc(TP_FN, sizeof(TpFn)); t->gchain = (TpChain*)calloc(TP_CHAINS, sizeof(TpChain)); if (!t->exe || !t->raw || !t->chain || !t->fn || !t->gchain) continue; }   /* winport v25: +fn/gchain */
            if (SuspendThread(t->h) == (DWORD)-1) continue;
            CONTEXT c; memset(&c, 0, sizeof c); c.ContextFlags = CONTEXT_FULL;
            DWORD64 rip = 0, exe = 0, ch[3] = {0, 0, 0}, gch[3] = {0, 0, 0}; int isg = 0;   /* winport v25 */
            if (GetThreadContext(t->h, &c)) { rip = c.Rip; tp_stack_bounds(c.Rsp); exe = (rip >= s_exe_lo && rip < s_exe_hi) ? rip : prof_first_exe_frame(&c); tp_exe_chain(&c, ch);
                /* winport v25: guest-code chain, only when the innermost exe frame is func_* (unwind must happen while suspended) */
                if (ch[0]) { const char* gn; ts_fn(ch[0], &gn); if (tp_is_guest(gn)) { isg = 1; tp_guest_chain(&c, gch); } } }
            ResumeThread(t->h);
            if (!rip) continue;
            t->samples++;
            if (!(rip >= s_exe_lo && rip < s_exe_hi)) { t->os_samples++; tp_rec(t->raw, rip); }
            if (exe) tp_rec(t->exe, exe);
            if (ch[0]) tp_chain_rec(t->chain, ch[0], ch[1], ch[2]);
            tp_fn_rec(t, ch[0], !(rip >= s_exe_lo && rip < s_exe_hi));   /* winport v25 */
            if (isg && gch[0]) tp_chain_rec(t->gchain, gch[0], gch[1], gch[2]);
            if (t->tid == s_tp_main_tid) {      /* the PPU main thread: is it waiting for the giant lock, and who holds it? */
                const char* nm; ts_fn(ch[0], &nm);
                s_gl_main_samples++;
                if (ch[0] && !strncmp(nm, "ppu_giant_lock_acquire", 22)) {
                    s_gl_main_waiting++;
                    long h = ppu_giant_lock_current_holder();
                    if (!h) s_gl_wait_free++;
                    else if ((DWORD)h != s_tp_main_tid) {
                        int found = -1, fe = -1;
                        for (int q = 0; q < 24; q++) { if (s_gl_holders[q].tid == (DWORD)h) { found = q; break; } if (fe < 0 && !s_gl_holders[q].tid) fe = q; }
                        if (found < 0 && fe >= 0) { s_gl_holders[fe].tid = (DWORD)h; found = fe; }
                        if (found >= 0) s_gl_holders[found].n++;
                        HANDLE hh = NULL; for (int q = 0; q < s_tp_n; q++) if (s_tp[q].tid == (DWORD)h) { hh = s_tp[q].h; break; }
                        if (hh && SuspendThread(hh) != (DWORD)-1) {
                            CONTEXT c2; memset(&c2, 0, sizeof c2); c2.ContextFlags = CONTEXT_FULL;
                            DWORD64 hc[3] = {0, 0, 0};
                            if (GetThreadContext(hh, &c2)) { tp_stack_bounds(c2.Rsp);
                                if (c2.Rip >= s_exe_lo && c2.Rip < s_exe_hi) s_gl_wait_holder_exe++; else s_gl_wait_holder_os++;
                                tp_exe_chain(&c2, hc);
                            }
                            ResumeThread(hh);
                            if (hc[0]) tp_chain_rec(s_gl_hchain, hc[0], hc[1], hc[2]);
                        }
                        DWORD64 ra = (DWORD64)(ULONG_PTR)ppu_giant_lock_current_holder_ra();
                        if (ra) { DWORD64 f = ts_fn(ra, NULL); tp_chain_rec(s_gl_ra, f ? f : ra, ra, 0); }
                    }
                }
            }
        }
        { long h = ppu_giant_lock_current_holder(); s_gl_rounds++; if (!h) s_gl_free++; else if ((DWORD)h == s_tp_main_tid) s_gl_main++; else s_gl_other++; }
        DWORD now = GetTickCount();
        if (now - t_250 >= 250) {   /* who is busy right now? */
            t_250 = now;
            if ((ticks & 7) == 0) tp_refresh_threads();
            for (int i = 0; i < s_tp_n; i++) {
                TpThread* t = &s_tp[i]; if (!t->h || t->dead) { t->hot = 0; continue; }
                ULONGLONG c = tp_cpu(t->h);
                t->hot = (c - t->cpu_250) > 10 * 10000ull;   /* > 10 ms of CPU in 250 ms */
                t->cpu_250 = c;
            }
        }
        if (now - t_win >= win_ms) {
            tp_mod_refresh();   /* winport v35: new DLLs (drivers) since the last window; no thread is suspended here */
            unsigned ms = now - t_win; t_win = now; window++;
            FILE* f = fopen(out, window == 1 ? "w" : "a");
            double tot = 0; const char* fnm; unsigned allsamp = 0;
            for (int i = 0; i < s_tp_n; i++) allsamp += s_tp[i].samples;   /* winport v25 */
            if (f) fprintf(f, "=== TPROF window %d (%u ms)\n", window, ms);
            for (int i = 0; i < s_tp_n; i++) {
                TpThread* t = &s_tp[i]; if (!t->h) continue;
                ULONGLONG c = tp_cpu(t->h);
                double pct = 100.0 * (double)(c - t->cpu_win0) / 10000.0 / (double)ms;
                t->cpu_win0 = c; tot += pct;
                if (pct >= 2.0) {
                    fprintf(stderr, "[TPROF] w%d tid=%lu cpu=%.0f%% start=%s samples=%u in-OS=%u%%\n", window, (unsigned long)t->tid, pct,
                            (ts_fn((DWORD64)t->start, &fnm), fnm), t->samples, t->samples ? 100u * t->os_samples / t->samples : 0u);
                    if (f) {
                        fprintf(f, "thread tid=%lu cpu=%.0f%% start=0x%llX name=%s samples=%u in-OS=%u\n", (unsigned long)t->tid, pct,
                                (unsigned long long)t->start, t->name[0] ? t->name : "-", t->samples, t->os_samples);
                        if (t->exe) {
                            qsort(t->exe, TP_SLOTS, sizeof(TpSlot), tp_cmp);
                            static int s_etop = -1; if (s_etop < 0) { const char* et = getenv("PS3_TPROF_ETOP"); s_etop = (et && atoi(et) > 0) ? atoi(et) : 25; }   /* winport v84: main thread E lines */
                            for (int k = 0; k < (t->tid == s_tp_main_tid ? s_etop : 25) && k < TP_SLOTS && t->exe[k].n; k++)
                                fprintf(f, "   E %6u %5.1f%%  0x%016llX\n", t->exe[k].n, t->samples ? 100.0 * t->exe[k].n / t->samples : 0.0, (unsigned long long)t->exe[k].rip);
                            qsort(t->raw, TP_SLOTS, sizeof(TpSlot), tp_cmp);
                            for (int k = 0; k < 6 && t->raw[k].n; k++)
                                fprintf(f, "   O %6u %5.1f%%  0x%016llX\n", t->raw[k].n, t->samples ? 100.0 * t->raw[k].n / t->samples : 0.0, (unsigned long long)t->raw[k].rip);
                        }
                    }
                    if (t->fn && (t->tid == s_tp_main_tid || (allsamp && t->samples * 20u > allsamp))) tp_report_fn(t, window, t->tid == s_tp_main_tid, f);   /* winport v25: main or >5% of samples */
                    if (t->chain) {
                        qsort(t->chain, TP_CHAINS, sizeof(TpChain), tp_chain_cmp);
                        int nshow = (pct >= 10.0) ? 8 : 3;
                        for (int k = 0; k < nshow && t->chain[k].n; k++) {
                            const char *n0, *n1, *n2; ts_fn(t->chain[k].a, &n0); ts_fn(t->chain[k].b, &n1); ts_fn(t->chain[k].c, &n2);
                            fprintf(stderr, "[TPROF]    %4.1f%%  %s <- %s <- %s\n", t->samples ? 100.0 * t->chain[k].n / t->samples : 0.0, n0, t->chain[k].b ? n1 : "-", t->chain[k].c ? n2 : "-");
                            if (f) fprintf(f, "   C %6u %5.1f%%  %s <- %s <- %s\n", t->chain[k].n, t->samples ? 100.0 * t->chain[k].n / t->samples : 0.0, n0, t->chain[k].b ? n1 : "-", t->chain[k].c ? n2 : "-");
                        }
                    }
                }
                if (t->chain) memset(t->chain, 0, TP_CHAINS * sizeof(TpChain));
                if (t->fn) memset(t->fn, 0, TP_FN * sizeof(TpFn)); if (t->gchain) memset(t->gchain, 0, TP_CHAINS * sizeof(TpChain)); t->fn_drop = 0;   /* winport v25 */
                if (t->exe) { memset(t->exe, 0, TP_SLOTS * sizeof(TpSlot)); memset(t->raw, 0, TP_SLOTS * sizeof(TpSlot)); }
                t->samples = t->os_samples = 0;
            }
            if (s_gl_rounds) {
                fprintf(stderr, "[TPROF] w%d GIANT LOCK: free %u%% | held by main %u%% | held by other threads %u%% (of %u sample rounds)\n", window,
                        100u * s_gl_free / s_gl_rounds, 100u * s_gl_main / s_gl_rounds, 100u * s_gl_other / s_gl_rounds, s_gl_rounds);
                fprintf(stderr, "[TPROF] w%d GIANT LOCK: main thread waiting for it in %u%% of its %u samples; lock free at that moment %u, holder was in our code %u / in OS %u\n", window,
                        s_gl_main_samples ? 100u * s_gl_main_waiting / s_gl_main_samples : 0u, s_gl_main_samples, s_gl_wait_free, s_gl_wait_holder_exe, s_gl_wait_holder_os);
                for (int q = 0; q < 24; q++) if (s_gl_holders[q].tid && s_gl_main_waiting) {
                    const char* hn = "?"; for (int z = 0; z < s_tp_n; z++) if (s_tp[z].tid == s_gl_holders[q].tid) { ts_fn((DWORD64)s_tp[z].start, &hn); break; }
                    if (100u * s_gl_holders[q].n / s_gl_main_waiting >= 3u)
                        fprintf(stderr, "[TPROF] w%d   while main waited, holder tid=%lu (thread start %s) = %u%%\n", window, (unsigned long)s_gl_holders[q].tid, hn, 100u * s_gl_holders[q].n / s_gl_main_waiting);
                }
                qsort(s_gl_hchain, TP_CHAINS, sizeof(TpChain), tp_chain_cmp);
                for (int k = 0; k < 6 && s_gl_hchain[k].n; k++) {
                    const char *n0, *n1, *n2; ts_fn(s_gl_hchain[k].a, &n0); ts_fn(s_gl_hchain[k].b, &n1); ts_fn(s_gl_hchain[k].c, &n2);
                    fprintf(stderr, "[TPROF] w%d   holder was in: %u x  %s <- %s <- %s\n", window, s_gl_hchain[k].n, n0, s_gl_hchain[k].b ? n1 : "-", s_gl_hchain[k].c ? n2 : "-");
                    if (f) fprintf(f, "  H %u %s <- %s <- %s\n", s_gl_hchain[k].n, n0, s_gl_hchain[k].b ? n1 : "-", s_gl_hchain[k].c ? n2 : "-");
                }
                qsort(s_gl_ra, TP_CHAINS, sizeof(TpChain), tp_chain_cmp);
                for (int k = 0; k < 4 && s_gl_ra[k].n; k++) {
                    const char* n0; ts_fn(s_gl_ra[k].a, &n0);
                    fprintf(stderr, "[TPROF] w%d   holder had acquired the lock from: %u x %s (+0x%llX)\n", window, s_gl_ra[k].n, n0, (unsigned long long)(s_gl_ra[k].b - s_gl_ra[k].a));
                }
                memset(s_gl_hchain, 0, TP_CHAINS * sizeof(TpChain)); memset(s_gl_ra, 0, TP_CHAINS * sizeof(TpChain));
                memset(s_gl_holders, 0, sizeof s_gl_holders);
                s_gl_rounds = s_gl_free = s_gl_main = s_gl_other = 0;
                s_gl_main_samples = s_gl_main_waiting = s_gl_wait_free = s_gl_wait_holder_os = s_gl_wait_holder_exe = 0;
            }
            fprintf(stderr, "[TPROF] w%d total process CPU = %.0f%% of one core (%d threads)\n", window, tot, s_tp_n);
            if (f) { fprintf(f, "total %.0f%%\n", tot); fclose(f); }
        }
    }
    return 0;
}
/* winport v84: PGO training dump. The benches kill the game (no atexit), so an instrumented build
 * (-fprofile-instr-generate) writes its profile from here: WINPORT_PGO_DUMP_S=N -> after N s, and again every
 * WINPORT_PGO_EVERY_S (default 0 = once). File: LLVM_PROFILE_FILE. Weak symbols: normal builds are unaffected. */
extern int __llvm_profile_write_file(void) __attribute__((weak));
extern void __llvm_profile_reset_counters(void) __attribute__((weak));
static DWORD WINAPI wp84_pgo_thread(LPVOID arg)
{
    (void)arg;
    const char* e = getenv("WINPORT_PGO_DUMP_S"); const char* ev = getenv("WINPORT_PGO_EVERY_S");
    const char* rs = getenv("WINPORT_PGO_RESET_S");   /* optional: zero the counters at this time (skip boot/menus) */
    int s = atoi(e), every = ev ? atoi(ev) : 0, reset = rs ? atoi(rs) : 0, t = 0;
    if (reset > 0 && reset < s) { Sleep((DWORD)reset * 1000u); t = reset; if (__llvm_profile_reset_counters) __llvm_profile_reset_counters();
                                  fprintf(stderr, "[PGO84] counters reset at %d s\n", t); fflush(stderr); }
    Sleep((DWORD)(s - t) * 1000u);
    for (;;) {
        int r = __llvm_profile_write_file ? __llvm_profile_write_file() : -99;
        fprintf(stderr, "[PGO84] profile written (rc=%d)\n", r); fflush(stderr);
        if (every <= 0) break;
        Sleep((DWORD)every * 1000u);
    }
    return 0;
}
__attribute__((constructor)) static void wp84_pgo_start(void)
{
    const char* e = getenv("WINPORT_PGO_DUMP_S");
    if (!e || atoi(e) <= 0) return;
    HANDLE t = CreateThread(NULL, 0, wp84_pgo_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
__attribute__((constructor)) static void winport_tprof_start(void)
{
    const char* e = getenv("PS3_TPROF");
    if (!e || *e != '1') return;
    s_tp_main_tid = GetCurrentThreadId();
    HANDLE t = CreateThread(NULL, 0, tprof_thread, NULL, 0, NULL);
    if (t) { SetThreadPriority(t, THREAD_PRIORITY_ABOVE_NORMAL); CloseHandle(t); }
}

/* The CE03C wait-idle hook (hooks/gow2_midasm_hooks.cpp) re-arms the host movie overlay so the
 * second single-pass movie after the intro can start ("CE03C Play#2 needs a clean slate").
 * On macOS that overlay is the VideoToolbox player and the flag lives in movie_vt_metal.m.
 * On Windows the overlay is movie_hle.c's ffmpeg player and its sticky end-of-stream flag is
 * g_movie_overlay_done (movie_hle_start_mode() deliberately never resets it), so this is the
 * equivalent reset. */
extern volatile long g_movie_overlay_done;
void movie_vt_clear_overlay_done(void)
{
    g_movie_overlay_done = 0;
}

/* dladdr(): gated diagnostic probes injected into the lift call it to name a host return
 * address. Windows has no dladdr; reporting "not found" makes those probes print the raw
 * address, which is what they already do when dladdr fails on POSIX. Dl_info is four
 * pointers (dli_fname, dli_fbase, dli_sname, dli_saddr). */
int dladdr(const void* addr, void* info)
{
    (void)addr;
    if (info) memset(info, 0, 4 * sizeof(void*));
    return 0;
}

/* ---------------------------------------------------------------------------------------------
 * wp_getenv_cached -- getenv() for the renderer (build_windows.sh compiles the d3d12_up sources
 * with -Dgetenv=wp_getenv_cached). The renderer has dozens of diagnostic switches read with
 * getenv() in per-draw / per-texture paths; UCRT's getenv scans the whole environment block
 * every call, and the v16 gameplay profile put 7 such call sites at ~10% of the main thread.
 * Nothing in the runtime changes the environment after start-up, so one snapshot is enough.
 * -------------------------------------------------------------------------------------------*/
#define WPENV_SLOTS 4096u
typedef struct { const char* name; const char* val; } WpEnvEnt;
static WpEnvEnt s_wpenv[WPENV_SLOTS];
static INIT_ONCE s_wpenv_once = INIT_ONCE_STATIC_INIT;
static unsigned wpenv_hash(const char* s, size_t n) { unsigned h = 2166136261u; while (n--) { h ^= (unsigned char)*s++; h *= 16777619u; } return h; }
static BOOL CALLBACK wpenv_init(PINIT_ONCE o, PVOID p, PVOID* c)
{
    (void)o; (void)p; (void)c;
    char** env = _environ;
    for (size_t i = 0; env && env[i]; i++) {
        const char* eq = strchr(env[i], '=');
        if (!eq || eq == env[i]) continue;
        size_t nl = (size_t)(eq - env[i]);
        char* nm = (char*)malloc(nl + 1); if (!nm) continue;
        memcpy(nm, env[i], nl); nm[nl] = 0;
        char* vl = _strdup(eq + 1);       if (!vl) { free(nm); continue; }
        unsigned h = wpenv_hash(nm, nl) & (WPENV_SLOTS - 1u);
        for (unsigned j = 0; j < WPENV_SLOTS; j++, h = (h + 1u) & (WPENV_SLOTS - 1u))
            if (!s_wpenv[h].name) { s_wpenv[h].name = nm; s_wpenv[h].val = vl; break; }
    }
    return TRUE;
}
char* wp_getenv_cached(const char* name)
{
    if (!name) return NULL;
    InitOnceExecuteOnce(&s_wpenv_once, wpenv_init, NULL, NULL);
    size_t nl = strlen(name);
    unsigned h = wpenv_hash(name, nl) & (WPENV_SLOTS - 1u);
    for (unsigned j = 0; j < WPENV_SLOTS; j++, h = (h + 1u) & (WPENV_SLOTS - 1u)) {
        const WpEnvEnt* e = &s_wpenv[h];
        if (!e->name) return NULL;
        if (strlen(e->name) == nl && memcmp(e->name, name, nl) == 0) return (char*)e->val;
    }
    return NULL;
}

/* winport release: the once-per-second statistics lines ([FIF77], [ASYNC], [AUDIO2], [SPURS76], [SPUJOB76],
 * [DEFER64], [DEFER64L], [RES68] 1s, [D3D12] draw_arrays) are logged only with WINPORT_STATS=1 (benchmarks and
 * analyzers need it); a normal session keeps its log small. */
int wp_stats_on(void)
{
    static int v = -1;
    if (v < 0) { const char* e = getenv("WINPORT_STATS"); v = (e && e[0] == '1') ? 1 : 0; }
    return v;
}
