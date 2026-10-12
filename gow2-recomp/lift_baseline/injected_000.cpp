extern "C" void ps3_type15_note_resolve(uint32_t idx, uint32_t obj, uint32_t vt);
extern "C" int ps3_type15_repair_if_needed(uint32_t obj);
extern "C" void ps3_type15_product_list_reset(uint32_t prod);
extern "C" int ps3_factory_repair_vt(uint32_t obj);
extern "C" uint32_t ps3_factory_reuse_product(uint32_t fo);
extern "C" int ps3_factory_freelist_replenish(uint32_t fo, uint16_t type_id);

#ifndef _WIN32
#include <unistd.h>
#endif
extern "C" void ppu_giant_lock_release(void);
extern "C" void ppu_giant_lock_acquire(void);
/* M1 disc budget: shared across CC9D0/CB56C DISC lines (cap 20). */
static int g_ps3_type15_disc_n = 0;

/* FLIPPATH-PROBE: R7 enter counters pre vs post R_Perm (PS3_TRACE_FLIPPATH=1) */
#include <signal.h>
extern "C" volatile int g_ps3_rperma_full;
enum { PS3_FLIPP_FN_N = 6 };
static const char* const g_ps3_flipp_fn_name[PS3_FLIPP_FN_N] = {
    "14FE18","156680","B71B8","2B2E74","2EFD60","2EFDA4"
};
static const int g_ps3_flipp_log_cap[PS3_FLIPP_FN_N] = {
    8,4,4,8,8,8
};
unsigned long long g_ps3_flipp_tot[PS3_FLIPP_FN_N];
unsigned long long g_ps3_flipp_post[PS3_FLIPP_FN_N];
unsigned long long g_ps3_flipp_grand;
static int g_ps3_flipp_atexit_reg = 0;
static int g_ps3_flipp_gate = -1;

static int ps3_flipp_gate_on(void) {
    if (g_ps3_flipp_gate < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_FLIPPATH");
        /* M1 disc contract: ON only if first char is '1'. */
        g_ps3_flipp_gate = (e && *e == '1') ? 1 : 0;
    }
    return g_ps3_flipp_gate;
}

static void ps3_flipp_dump_summary(void) {
    if (!ps3_flipp_gate_on()) return;
    fprintf(stderr, "[FLIPPATH] SUMMARY grand=%llu rperma_full=%d\n",
            (unsigned long long)g_ps3_flipp_grand, (int)g_ps3_rperma_full);
    for (int i = 0; i < PS3_FLIPP_FN_N; i++) {
        unsigned long long t = g_ps3_flipp_tot[i];
        unsigned long long p = g_ps3_flipp_post[i];
        /* Always print every site so tot=0 is visible. */
        fprintf(stderr,
                "[FLIPPATH] SUMMARY fn=%s tot=%llu post=%llu pre=%llu\n",
                g_ps3_flipp_fn_name[i],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned long long)(t >= p ? t - p : 0));
    }
    fflush(stderr);
}

static void ps3_flipp_on_sigterm(int sig) {
    (void)sig;
    ps3_flipp_dump_summary();
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void ps3_flipp_ctor(void) {
    if (!ps3_flipp_gate_on()) return;
    fprintf(stderr, "[FLIPPATH] probe armed (post=g_ps3_rperma_full)\n");
    fflush(stderr);
    if (!g_ps3_flipp_atexit_reg) {
        g_ps3_flipp_atexit_reg = 1;
        atexit(ps3_flipp_dump_summary);
#ifndef _WIN32
        signal(SIGTERM, ps3_flipp_on_sigterm);
#endif
    }
}

void ps3_flipp_on_enter(int id, ppu_context* ctx) {
    if (!ps3_flipp_gate_on()) return;
    if (id < 0 || id >= PS3_FLIPP_FN_N || !ctx) return;
    if (!g_ps3_flipp_atexit_reg) {
        g_ps3_flipp_atexit_reg = 1;
        atexit(ps3_flipp_dump_summary);
#ifndef _WIN32
        signal(SIGTERM, ps3_flipp_on_sigterm);
#endif
    }
    unsigned long long t = ++g_ps3_flipp_tot[id];
    unsigned long long p = g_ps3_flipp_post[id];
    if (g_ps3_rperma_full) p = ++g_ps3_flipp_post[id];
    unsigned long long g = ++g_ps3_flipp_grand;
    int cap = g_ps3_flipp_log_cap[id];
    int do_log = (t <= (unsigned long long)cap) || ((g % 10000ull) == 0);
    if (do_log) {
        fprintf(stderr,
                "[FLIPPATH] enter fn=%s tot=%llu post=%llu r3=0x%08X r4=0x%08X\n",
                g_ps3_flipp_fn_name[id],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned)(uint32_t)ctx->gpr[3],
                (unsigned)(uint32_t)ctx->gpr[4]);
        fflush(stderr);
    }
    if ((g % 10000ull) == 0) ps3_flipp_dump_summary();
}
/* FLIPPATH-PROBE end helpers */

/* PRESENTLOOP-PROBE: R8 parents of 156680 pre vs post R_Perm (PS3_TRACE_PRESENTLOOP=1) */
#include <signal.h>
extern "C" volatile int g_ps3_rperma_full;
enum { PS3_PLP_FN_N = 11 };
static const char* const g_ps3_plp_fn_name[PS3_PLP_FN_N] = {
    "CDBA4","CE0A0","194FFC","2B21C4","2C07F8",
    "CDC08","CDCF8","CDD00","CDD88","2B21F8","2B2288"
};
static const int g_ps3_plp_log_cap[PS3_PLP_FN_N] = {
    4,4,4,4,4, 4,4,4,8,4,4
};
unsigned long long g_ps3_plp_tot[PS3_PLP_FN_N];
unsigned long long g_ps3_plp_post[PS3_PLP_FN_N];
unsigned long long g_ps3_plp_grand;
static int g_ps3_plp_atexit_reg = 0;
static int g_ps3_plp_gate = -1;

static int ps3_plp_gate_on(void) {
    if (g_ps3_plp_gate < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_PRESENTLOOP");
        /* M1 disc contract: ON only if first char is '1'. */
        g_ps3_plp_gate = (e && *e == '1') ? 1 : 0;
    }
    return g_ps3_plp_gate;
}

static void ps3_plp_dump_summary(void) {
    if (!ps3_plp_gate_on()) return;
    fprintf(stderr, "[PRESENTLOOP] SUMMARY grand=%llu rperma_full=%d\n",
            (unsigned long long)g_ps3_plp_grand, (int)g_ps3_rperma_full);
    for (int i = 0; i < PS3_PLP_FN_N; i++) {
        unsigned long long t = g_ps3_plp_tot[i];
        unsigned long long p = g_ps3_plp_post[i];
        fprintf(stderr,
                "[PRESENTLOOP] SUMMARY fn=%s tot=%llu post=%llu pre=%llu\n",
                g_ps3_plp_fn_name[i],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned long long)(t >= p ? t - p : 0));
    }
    fflush(stderr);
}

static void ps3_plp_on_sigterm(int sig) {
    (void)sig;
    ps3_plp_dump_summary();
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void ps3_plp_ctor(void) {
    if (!ps3_plp_gate_on()) return;
    fprintf(stderr, "[PRESENTLOOP] probe armed (post=g_ps3_rperma_full)\n");
    fflush(stderr);
    if (!g_ps3_plp_atexit_reg) {
        g_ps3_plp_atexit_reg = 1;
        atexit(ps3_plp_dump_summary);
#ifndef _WIN32
        signal(SIGTERM, ps3_plp_on_sigterm);
#endif
    }
}

void ps3_plp_on_enter(int id, ppu_context* ctx) {
    if (!ps3_plp_gate_on()) return;
    if (id < 0 || id >= PS3_PLP_FN_N || !ctx) return;
    if (!g_ps3_plp_atexit_reg) {
        g_ps3_plp_atexit_reg = 1;
        atexit(ps3_plp_dump_summary);
#ifndef _WIN32
        signal(SIGTERM, ps3_plp_on_sigterm);
#endif
    }
    unsigned long long t = ++g_ps3_plp_tot[id];
    unsigned long long p = g_ps3_plp_post[id];
    if (g_ps3_rperma_full) p = ++g_ps3_plp_post[id];
    unsigned long long g = ++g_ps3_plp_grand;
    int cap = g_ps3_plp_log_cap[id];
    int do_log = (t <= (unsigned long long)cap) || ((g % 10000ull) == 0);
    if (do_log) {
        fprintf(stderr,
                "[PRESENTLOOP] enter fn=%s tot=%llu post=%llu r3=0x%08X r4=0x%08X\n",
                g_ps3_plp_fn_name[id],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned)(uint32_t)ctx->gpr[3],
                (unsigned)(uint32_t)ctx->gpr[4]);
        fflush(stderr);
    }
    if ((g % 10000ull) == 0) ps3_plp_dump_summary();
}
/* PRESENTLOOP-PROBE end helpers */

/* MENUPRESENT-PROBE: R9 menu present + schedule parents pre/post R_Perm (PS3_TRACE_MENUPRESENT=1) */
#include <signal.h>
extern "C" volatile int g_ps3_rperma_full;
enum { PS3_MP_FN_N = 15 };
static const char* const g_ps3_mp_fn_name[PS3_MP_FN_N] = {
    "194FFC","2B21C4","2B21F8","2B2288","2C07F8",
    "BB4E0","BB424","B61F4","2B25AC","2C0508",
    "CE03C","CE0A0","B71B8","2B2E74","25C838"
};
static const int g_ps3_mp_log_cap[PS3_MP_FN_N] = {
    4,4,4,4,4, 4,4,4,4,8, 4,4,4,4,4
};
unsigned long long g_ps3_mp_tot[PS3_MP_FN_N];
unsigned long long g_ps3_mp_post[PS3_MP_FN_N];
unsigned long long g_ps3_mp_grand;
static int g_ps3_mp_atexit_reg = 0;
static int g_ps3_mp_gate = -1;

static int ps3_mp_gate_on(void) {
    if (g_ps3_mp_gate < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_MENUPRESENT");
        /* M1 disc contract: ON only if first char is '1'. */
        g_ps3_mp_gate = (e && *e == '1') ? 1 : 0;
    }
    return g_ps3_mp_gate;
}

static void ps3_mp_dump_summary(void) {
    if (!ps3_mp_gate_on()) return;
    fprintf(stderr, "[MENUPRESENT] SUMMARY grand=%llu rperma_full=%d\n",
            (unsigned long long)g_ps3_mp_grand, (int)g_ps3_rperma_full);
    for (int i = 0; i < PS3_MP_FN_N; i++) {
        unsigned long long t = g_ps3_mp_tot[i];
        unsigned long long p = g_ps3_mp_post[i];
        fprintf(stderr,
                "[MENUPRESENT] SUMMARY fn=%s tot=%llu post=%llu pre=%llu\n",
                g_ps3_mp_fn_name[i],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned long long)(t >= p ? t - p : 0));
    }
    fflush(stderr);
}

static void ps3_mp_on_sigterm(int sig) {
    (void)sig;
    ps3_mp_dump_summary();
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void ps3_mp_ctor(void) {
    if (!ps3_mp_gate_on()) return;
    fprintf(stderr, "[MENUPRESENT] probe armed (post=g_ps3_rperma_full)\n");
    fflush(stderr);
    if (!g_ps3_mp_atexit_reg) {
        g_ps3_mp_atexit_reg = 1;
        atexit(ps3_mp_dump_summary);
#ifndef _WIN32
        signal(SIGTERM, ps3_mp_on_sigterm);
#endif
    }
}

void ps3_mp_on_enter(int id, ppu_context* ctx) {
    if (!ps3_mp_gate_on()) return;
    if (id < 0 || id >= PS3_MP_FN_N || !ctx) return;
    if (!g_ps3_mp_atexit_reg) {
        g_ps3_mp_atexit_reg = 1;
        atexit(ps3_mp_dump_summary);
#ifndef _WIN32
        signal(SIGTERM, ps3_mp_on_sigterm);
#endif
    }
    unsigned long long t = ++g_ps3_mp_tot[id];
    unsigned long long p = g_ps3_mp_post[id];
    if (g_ps3_rperma_full) p = ++g_ps3_mp_post[id];
    unsigned long long g = ++g_ps3_mp_grand;
    int cap = g_ps3_mp_log_cap[id];
    int do_log = (t <= (unsigned long long)cap) || ((g % 10000ull) == 0);
    if (do_log) {
        fprintf(stderr,
                "[MENUPRESENT] enter fn=%s tot=%llu post=%llu r3=0x%08X r4=0x%08X\n",
                g_ps3_mp_fn_name[id],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned)(uint32_t)ctx->gpr[3],
                (unsigned)(uint32_t)ctx->gpr[4]);
        fflush(stderr);
    }
    if ((g % 10000ull) == 0) ps3_mp_dump_summary();
}
/* MENUPRESENT-PROBE end helpers */

/* SCHEDARM-PROBE: R10 who arms menu/frame present schedule pre/post R_Perm (PS3_TRACE_SCHEDARM=1) */
#include <signal.h>
extern "C" volatile int g_ps3_rperma_full;
enum { PS3_SA_FN_N = 19 };
static const char* const g_ps3_sa_fn_name[PS3_SA_FN_N] = {
    "B61F4","BB424","BB4E0","25C838","2B25AC","CE03C",
    "B6150","B94EC","B94D4","B7888","BB414","BB37C",
    "10354","36A598","2B25A4","2B24E0","CDFDC","CE000","CE02C"
};
static const int g_ps3_sa_log_cap[PS3_SA_FN_N] = {
    4,4,4,4,4,4, 8,4,4,8,4,8, 4,4,4,8,4,4,4
};
unsigned long long g_ps3_sa_tot[PS3_SA_FN_N];
unsigned long long g_ps3_sa_post[PS3_SA_FN_N];
unsigned long long g_ps3_sa_grand;
static int g_ps3_sa_atexit_reg = 0;
static int g_ps3_sa_gate = -1;

static int ps3_sa_gate_on(void) {
    if (g_ps3_sa_gate < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_SCHEDARM");
        /* M1 disc contract: ON only if first char is '1'. */
        if (e && *e == '1') {
            g_ps3_sa_gate = 1;
        } else {
            /* alias: allow MENUPRESENT=1 to also arm SCHEDARM (shared smoke) */
            const char* m = getenv("PS3_TRACE_MENUPRESENT");
            g_ps3_sa_gate = (m && *m == '1') ? 1 : 0;
        }
    }
    return g_ps3_sa_gate;
}

static void ps3_sa_dump_summary(void) {
    if (!ps3_sa_gate_on()) return;
    fprintf(stderr, "[SCHEDARM] SUMMARY grand=%llu rperma_full=%d\n",
            (unsigned long long)g_ps3_sa_grand, (int)g_ps3_rperma_full);
    for (int i = 0; i < PS3_SA_FN_N; i++) {
        unsigned long long t = g_ps3_sa_tot[i];
        unsigned long long p = g_ps3_sa_post[i];
        fprintf(stderr,
                "[SCHEDARM] SUMMARY fn=%s tot=%llu post=%llu pre=%llu\n",
                g_ps3_sa_fn_name[i],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned long long)(t >= p ? t - p : 0));
    }
    fflush(stderr);
}

static void ps3_sa_on_sigterm(int sig) {
    (void)sig;
    ps3_sa_dump_summary();
}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void ps3_sa_ctor(void) {
    if (!ps3_sa_gate_on()) return;
    fprintf(stderr, "[SCHEDARM] probe armed (post=g_ps3_rperma_full)\n");
    fflush(stderr);
    if (!g_ps3_sa_atexit_reg) {
        g_ps3_sa_atexit_reg = 1;
        atexit(ps3_sa_dump_summary);
#ifndef _WIN32
        signal(SIGTERM, ps3_sa_on_sigterm);
#endif
    }
}

void ps3_sa_on_enter(int id, ppu_context* ctx) {
    if (!ps3_sa_gate_on()) return;
    if (id < 0 || id >= PS3_SA_FN_N || !ctx) return;
    if (!g_ps3_sa_atexit_reg) {
        g_ps3_sa_atexit_reg = 1;
        atexit(ps3_sa_dump_summary);
#ifndef _WIN32
        signal(SIGTERM, ps3_sa_on_sigterm);
#endif
    }
    unsigned long long t = ++g_ps3_sa_tot[id];
    unsigned long long p = g_ps3_sa_post[id];
    if (g_ps3_rperma_full) p = ++g_ps3_sa_post[id];
    unsigned long long g = ++g_ps3_sa_grand;
    int cap = g_ps3_sa_log_cap[id];
    int do_log = (t <= (unsigned long long)cap) || ((g % 10000ull) == 0);
    if (do_log) {
        fprintf(stderr,
                "[SCHEDARM] enter fn=%s tot=%llu post=%llu r3=0x%08X r4=0x%08X\n",
                g_ps3_sa_fn_name[id],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned)(uint32_t)ctx->gpr[3],
                (unsigned)(uint32_t)ctx->gpr[4]);
        fflush(stderr);
    }
    if ((g % 10000ull) == 0) ps3_sa_dump_summary();
}
/* SCHEDARM-PROBE end helpers */

/* POSTTHR-PROBE: R11 sample guest enters after thr_auto_load end (PS3_TRACE_POSTTHR_PC=1) */
#include <signal.h>
#include <time.h>
extern "C" volatile int g_ps3_rperma_full;
/* Set once when thr body (147038) returns — post-thr window. */
extern "C" volatile int g_ps3_postthr = 0;
enum { PS3_PT_FN_N = 17 };
static const char* const g_ps3_pt_fn_name[PS3_PT_FN_N] = {
    "147038","25C838","2B2E74","B71B8","36A598","2B2DD0","B951C",
    "CC9D0","CCBF0","CBB2C","CB56C",
    "B6150","B94EC","B61F4","BB424",
    "CDBA4","2C0508"
};
static const int g_ps3_pt_log_cap[PS3_PT_FN_N] = {
    4,4,4,4,4,4,4, 8,4,4,4, 4,4,4,4, 4,4
};
unsigned long long g_ps3_pt_tot[PS3_PT_FN_N];
unsigned long long g_ps3_pt_post[PS3_PT_FN_N];
unsigned long long g_ps3_pt_grand;
static int g_ps3_pt_atexit_reg = 0;
static int g_ps3_pt_gate = -1;
static unsigned long long g_ps3_pt_thr_ns = 0;
static unsigned long long g_ps3_pt_sample_until_ns = 0;

static unsigned long long ps3_pt_now_ns(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (unsigned long long)ts.tv_sec * 1000000000ull
             + (unsigned long long)ts.tv_nsec;
#endif
    return 0;
}

static int ps3_pt_gate_on(void) {
    /* POSTTHR-R12-GATE: also accept PS3_TRACE_POSTTHR=1 */
    if (g_ps3_pt_gate < 0) {
        extern char* getenv(const char*);
        const char* a = getenv("PS3_TRACE_POSTTHR");
        const char* b = getenv("PS3_TRACE_POSTTHR_PC");
        g_ps3_pt_gate = ((a && *a == '1') || (b && *b == '1')) ? 1 : 0;
    }
    return g_ps3_pt_gate;
}

static void ps3_pt_dump_summary(void) {
    if (!ps3_pt_gate_on()) return;
    fprintf(stderr,
            "[POSTTHR] SUMMARY grand=%llu postthr=%d rperma_full=%d thr_ns=%llu\n",
            (unsigned long long)g_ps3_pt_grand,
            (int)g_ps3_postthr,
            (int)g_ps3_rperma_full,
            (unsigned long long)g_ps3_pt_thr_ns);
    for (int i = 0; i < PS3_PT_FN_N; i++) {
        unsigned long long t = g_ps3_pt_tot[i];
        unsigned long long p = g_ps3_pt_post[i];
        fprintf(stderr,
                "[POSTTHR] SUMMARY fn=%s tot=%llu post=%llu pre=%llu\n",
                g_ps3_pt_fn_name[i],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned long long)(t >= p ? t - p : 0));
    }
    fflush(stderr);
}

static void ps3_pt_on_sigterm(int sig) {
    (void)sig;
    ps3_pt_dump_summary();
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void ps3_pt_ctor(void) {
    if (!ps3_pt_gate_on()) return;
    fprintf(stderr, "[POSTTHR] probe armed (post=g_ps3_postthr after 147038 return)\n");
    fflush(stderr);
    if (!g_ps3_pt_atexit_reg) {
        g_ps3_pt_atexit_reg = 1;
        atexit(ps3_pt_dump_summary);
#ifndef _WIN32
        signal(SIGTERM, ps3_pt_on_sigterm);
#endif
    }
}

void ps3_pt_mark_thr_end(void) {
    if (!ps3_pt_gate_on()) return;
    if (g_ps3_postthr) return;
    g_ps3_postthr = 1;
    g_ps3_pt_thr_ns = ps3_pt_now_ns();
    /* Sample ~2s wall after thr for dense enter logs; counters keep forever. */
    g_ps3_pt_sample_until_ns = g_ps3_pt_thr_ns
        ? (g_ps3_pt_thr_ns + 2000000000ull) : 0;
    fprintf(stderr, "[POSTTHR] thr_end flag set (sample_window=2s)\n");
    fflush(stderr);
}

void ps3_pt_on_enter(int id, ppu_context* ctx) {
    if (!ps3_pt_gate_on()) return;
    if (id < 0 || id >= PS3_PT_FN_N || !ctx) return;
    if (!g_ps3_pt_atexit_reg) {
        g_ps3_pt_atexit_reg = 1;
        atexit(ps3_pt_dump_summary);
#ifndef _WIN32
        signal(SIGTERM, ps3_pt_on_sigterm);
#endif
    }
    unsigned long long t = ++g_ps3_pt_tot[id];
    unsigned long long p = g_ps3_pt_post[id];
    if (g_ps3_postthr) p = ++g_ps3_pt_post[id];
    unsigned long long g = ++g_ps3_pt_grand;
    int cap = g_ps3_pt_log_cap[id];
    int in_window = 0;
    if (g_ps3_postthr && g_ps3_pt_sample_until_ns) {
        unsigned long long now = ps3_pt_now_ns();
        if (now && now <= g_ps3_pt_sample_until_ns) in_window = 1;
    }
    int do_log = (t <= (unsigned long long)cap)
              || (in_window && (t % 500ull) == 0)
              || ((g % 20000ull) == 0);
    if (do_log) {
        fprintf(stderr,
                "[POSTTHR] enter fn=%s tot=%llu post=%llu r3=0x%08X lr=0x%08X\n",
                g_ps3_pt_fn_name[id],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned)(uint32_t)ctx->gpr[3],
                (unsigned)(uint32_t)ctx->lr);
        fflush(stderr);
    }
    if ((g % 20000ull) == 0) ps3_pt_dump_summary();
}
/* POSTTHR-PROBE end helpers */

/* POSTTHR-R12-DISC: sample TYPE15 f4/f54 from parent tick (PS3_TRACE_POSTTHR=1) */
static int g_ps3_pt_disc_n = 0;
static int g_ps3_pt_icall_n = 0;
enum { PS3_PT_DISC_CAP = 20, PS3_PT_ICALL_CAP = 8 };
static const uint32_t g_ps3_pt_ty15[2] = { 0x4066D798u, 0x4066D804u };

void ps3_pt_disc_sample(const char* where, ppu_context* ctx) {
    if (!ps3_pt_gate_on() || !ctx) return;
    /* Prefer post-thr; also sample after R_Perm full so we still get lines
     * if thr_end races or the window is short. Cap hard at 20. */
    if (!g_ps3_postthr && !g_ps3_rperma_full) return;
    if (g_ps3_pt_disc_n >= PS3_PT_DISC_CAP) return;
    /* Throttle: at most one pair every 256 enters of the triangle after
     * the first 4 pairs (so pre thr doesn't exhaust the budget). */
    static unsigned long long s_calls = 0;
    s_calls++;
    if (g_ps3_pt_disc_n >= 8 && (s_calls & 0xFFull) != 0) return;
    /* Sample both known TYPE15 components (shared budget ≤20). */
    for (int i = 0; i < 2 && g_ps3_pt_disc_n < PS3_PT_DISC_CAP; i++) {
        uint32_t th = g_ps3_pt_ty15[i];
        uint32_t f4 = 0, prod = 0, child_head = 0;
        uint8_t f54 = 0;
        /* vm_* is host-safe on unmapped: still use try-range. */
        if (th >= 0x10000u && th < 0x10000000u) {
            f4 = vm_read32((uint64_t)th + 0x4u);
            f54 = (uint8_t)vm_read8((uint64_t)th + 0x54u);
            prod = vm_read32((uint64_t)th + 0x8u);
            if (prod >= 0x10000u && prod < 0x10000000u)
                child_head = vm_read32((uint64_t)prod + 0x70u);
        }
        fprintf(stderr,
                "[POSTTHR-DISC] where=%s r3=0x%08X this=0x%08X f4=0x%08X f54=0x%02X prod=0x%08X child_head=0x%08X post=%d rperm=%d n=%d\n",
                where ? where : "?",
                (unsigned)(uint32_t)ctx->gpr[3],
                (unsigned)th,
                (unsigned)f4,
                (unsigned)f54,
                (unsigned)prod,
                (unsigned)child_head,
                (int)g_ps3_postthr,
                (int)g_ps3_rperma_full,
                g_ps3_pt_disc_n + 1);
        fflush(stderr);
        g_ps3_pt_disc_n++;
    }
}

void ps3_pt_icall_log(const char* slot, ppu_context* ctx) {
    if (!ps3_pt_gate_on() || !ctx) return;
    if (!g_ps3_postthr) return;
    if (g_ps3_pt_icall_n >= PS3_PT_ICALL_CAP) return;
    uint32_t ctr = (uint32_t)ctx->ctr;
    fprintf(stderr,
            "[POSTTHR-ICALL] slot=%s ctr=0x%08X r3=0x%08X lr=0x%08X n=%d\n",
            slot ? slot : "?",
            (unsigned)ctr,
            (unsigned)(uint32_t)ctx->gpr[3],
            (unsigned)(uint32_t)ctx->lr,
            g_ps3_pt_icall_n + 1);
    fflush(stderr);
    g_ps3_pt_icall_n++;
}
/* POSTTHR-R12-DISC end */

/* 17ACC-PROBE: R5 enter counters pre vs post R_Perm (PS3_TRACE_17ACC=1) */
#include <signal.h>
extern "C" volatile int g_ps3_rperma_full;
enum { PS3_17ACC_FN_N = 10 };
static const char* const g_ps3_17acc_fn_name[PS3_17ACC_FN_N] = {
    "17ACC","1E1A8","1E34C","1EAD8","25064","25614","1E3DC","1E3E8","1E400","1E4AC"
};
static const int g_ps3_17acc_log_cap[PS3_17ACC_FN_N] = {
    8,4,4,4,4,4,4,4,4,4
};
unsigned long long g_ps3_17acc_tot[PS3_17ACC_FN_N];
unsigned long long g_ps3_17acc_post[PS3_17ACC_FN_N];
unsigned long long g_ps3_17acc_grand;
static int g_ps3_17acc_atexit_reg = 0;
static int g_ps3_17acc_gate = -1;

static int ps3_17acc_gate_on(void) {
    if (g_ps3_17acc_gate < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_17ACC");
        /* M1 disc contract: ON only if first char is '1'. */
        g_ps3_17acc_gate = (e && *e == '1') ? 1 : 0;
    }
    return g_ps3_17acc_gate;
}

static void ps3_17acc_dump_summary(void) {
    if (!ps3_17acc_gate_on()) return;
    fprintf(stderr, "[17ACC] SUMMARY grand=%llu rperma_full=%d\n",
            (unsigned long long)g_ps3_17acc_grand, (int)g_ps3_rperma_full);
    for (int i = 0; i < PS3_17ACC_FN_N; i++) {
        unsigned long long t = g_ps3_17acc_tot[i];
        unsigned long long p = g_ps3_17acc_post[i];
        /* Always print every site so tot=0 is visible. */
        fprintf(stderr,
                "[17ACC] SUMMARY fn=%s tot=%llu post=%llu pre=%llu\n",
                g_ps3_17acc_fn_name[i],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned long long)(t >= p ? t - p : 0));
    }
    fflush(stderr);
}

static void ps3_17acc_on_sigterm(int sig) {
    (void)sig;
    ps3_17acc_dump_summary();
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void ps3_17acc_ctor(void) {
    if (!ps3_17acc_gate_on()) return;
    fprintf(stderr, "[17ACC] probe armed (post=g_ps3_rperma_full)\n");
    fflush(stderr);
    if (!g_ps3_17acc_atexit_reg) {
        g_ps3_17acc_atexit_reg = 1;
        atexit(ps3_17acc_dump_summary);
#ifndef _WIN32
        signal(SIGTERM, ps3_17acc_on_sigterm);
#endif
    }
}

void ps3_17acc_on_enter(int id, ppu_context* ctx) {
    if (!ps3_17acc_gate_on()) return;
    if (id < 0 || id >= PS3_17ACC_FN_N || !ctx) return;
    if (!g_ps3_17acc_atexit_reg) {
        g_ps3_17acc_atexit_reg = 1;
        atexit(ps3_17acc_dump_summary);
    }
    unsigned long long t = ++g_ps3_17acc_tot[id];
    unsigned long long p = g_ps3_17acc_post[id];
    if (g_ps3_rperma_full) p = ++g_ps3_17acc_post[id];
    unsigned long long g = ++g_ps3_17acc_grand;
    int cap = g_ps3_17acc_log_cap[id];
    int do_log = (t <= (unsigned long long)cap) || ((g % 10000ull) == 0);
    if (do_log) {
        fprintf(stderr,
                "[17ACC] enter fn=%s tot=%llu post=%llu r3=0x%08X r4=0x%08X\n",
                g_ps3_17acc_fn_name[id],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned)(uint32_t)ctx->gpr[3],
                (unsigned)(uint32_t)ctx->gpr[4]);
        fflush(stderr);
    }
    if ((g % 10000ull) == 0) ps3_17acc_dump_summary();
}
/* 17ACC-PROBE end helpers */



extern "C" int host_res_inflate(ppu_context* ctx);
/* BCTR-TAIL: despacho de `bctr` (salto, SEM link). Ver
 * recomp_mid_v2/patch_bctr_tail.py e ps3_indirect_tail em
 * ps3recomp/runtime/ppu/ppu_loader.cpp. */
extern "C" void ps3_indirect_tail(ppu_context* ctx);

extern "C" void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);

