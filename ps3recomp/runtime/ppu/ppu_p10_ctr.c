#include "ppu_p10_ctr.h"

#include <stdio.h>
#include <stdlib.h>

#define P10_SITES 4u
#define P10_UNIQ 32u
#define P10_SUM_EVERY 8192ull

static int g_on = -1;
static uint32_t g_ctr[P10_SITES][P10_UNIQ];
static unsigned long long g_n[P10_SITES][P10_UNIQ];
static unsigned g_nu[P10_SITES];
static unsigned long long g_total[P10_SITES];

static int p10_enabled(void)
{
    if (g_on < 0) {
        const char* e = getenv("PS3_TRACE_P10_CTR");
        g_on = (e && e[0] && e[0] != '0') ? 1 : 0;
        if (g_on) {
            fprintf(stderr, "[P10CTR] enabled\n");
            fflush(stderr);
        }
    }
    return g_on;
}

void ps3_p10_ctr_reset_for_test(void)
{
    g_on = -1;
    for (unsigned s = 0; s < P10_SITES; s++) {
        g_nu[s] = 0;
        g_total[s] = 0;
        for (unsigned i = 0; i < P10_UNIQ; i++) {
            g_ctr[s][i] = 0;
            g_n[s][i] = 0;
        }
    }
}

void ps3_p10_ctr_note(unsigned site, uint32_t ctr)
{
    unsigned i;
    if (!p10_enabled())
        return;
    if (site >= P10_SITES)
        return;
    g_total[site]++;
    for (i = 0; i < g_nu[site]; i++) {
        if (g_ctr[site][i] == ctr) {
            g_n[site][i]++;
            if ((g_total[site] % P10_SUM_EVERY) == 0ull) {
                fprintf(stderr, "[P10CTR-SUM] site=%u ctr=0x%08X n=%llu total=%llu\n",
                        site, ctr, g_n[site][i], g_total[site]);
                fflush(stderr);
            }
            return;
        }
    }
    if (g_nu[site] >= P10_UNIQ) {
        fprintf(stderr, "[P10CTR] site=%u FULL ctr=0x%08X total=%llu\n",
                site, ctr, g_total[site]);
        fflush(stderr);
        return;
    }
    i = g_nu[site]++;
    g_ctr[site][i] = ctr;
    g_n[site][i] = 1;
    fprintf(stderr, "[P10CTR] site=%u ctr=0x%08X n=1\n", site, ctr);
    fflush(stderr);
}

unsigned long long ps3_p10_ctr_count(unsigned site, uint32_t ctr)
{
    unsigned i;
    if (site >= P10_SITES)
        return 0;
    for (i = 0; i < g_nu[site]; i++) {
        if (g_ctr[site][i] == ctr)
            return g_n[site][i];
    }
    return 0;
}

unsigned ps3_p10_ctr_nunique(unsigned site)
{
    if (site >= P10_SITES)
        return 0;
    return g_nu[site];
}
