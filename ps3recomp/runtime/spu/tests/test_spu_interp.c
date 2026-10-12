/*
 * Unit test for the SPU interpreter (runtime/spu/spu_interp.c).
 *
 * Small hand-assembled programs run in a local store; results are checked
 * against the spu_helpers.h semantics (the same helpers lifted code calls) and
 * against the channel / MFC glue of the real runtime (spu_channels.c +
 * spu_dma.h, linked in): a DMA list that stalls raises the Sn event, the
 * interrupt is taken at a `bie` window, the handler acks the stall and the
 * list completes.
 */
#define _DARWIN_C_SOURCE 1
#define _GNU_SOURCE 1
#include "../spu_interp.h"
#include "../spu_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- stubs for what spu_channels.c / spu_dma.h import from the runtime ---- */
uint8_t* vm_base;
uint32_t ppu_vm_size = 0;                   /* 0 = no range check (test harness) */
int ppu_guest_range_committed(uint32_t addr, uint32_t n) { (void)addr; (void)n; return 1; }
int ps3_trace_on(int s) { (void)s; return 0; }
void ps3_trace_emit(int s, const char* fn, const char* ev, const char* fmt, ...)
{ (void)s; (void)fn; (void)ev; (void)fmt; }
void ps3_watch_store_bulk(uint32_t ea, uint32_t len, const char* who) { (void)ea; (void)len; (void)who; }
void spu_get_record(unsigned a, unsigned b, int c, unsigned d, unsigned e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; }
void spu_put_record(unsigned a, unsigned b, int c, unsigned d, unsigned e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; }

/* ---- tiny assembler ---- */
static uint32_t RR(uint32_t op11, uint32_t rb, uint32_t ra, uint32_t rt)
{ return (op11 << 21) | ((rb & 0x7F) << 14) | ((ra & 0x7F) << 7) | (rt & 0x7F); }
static uint32_t RRR(uint32_t op4, uint32_t rt, uint32_t rb, uint32_t ra, uint32_t rc)
{ return (op4 << 28) | ((rt & 0x7F) << 21) | ((rb & 0x7F) << 14) | ((ra & 0x7F) << 7) | (rc & 0x7F); }
static uint32_t RI10(uint32_t op8, int32_t i10, uint32_t ra, uint32_t rt)
{ return (op8 << 24) | (((uint32_t)i10 & 0x3FF) << 14) | ((ra & 0x7F) << 7) | (rt & 0x7F); }
static uint32_t RI16(uint32_t op9, int32_t i16, uint32_t rt)
{ return (op9 << 23) | (((uint32_t)i16 & 0xFFFF) << 7) | (rt & 0x7F); }
static uint32_t RI18(uint32_t op7, uint32_t i18, uint32_t rt)
{ return (op7 << 25) | ((i18 & 0x3FFFF) << 7) | (rt & 0x7F); }

#define IL(rt, v)        RI16(0x081, (v), (rt))
#define ILHU(rt, v)      RI16(0x082, (v), (rt))
#define IOHL(rt, v)      RI16(0x0C1, (v), (rt))
#define ILA(rt, v)       RI18(0x21, (v), (rt))
#define AI(rt, ra, v)    RI10(0x1C, (v), (ra), (rt))
#define CEQHI(rt, ra, v) RI10(0x7D, (v), (ra), (rt))
#define LQD(rt, ra, off) RI10(0x34, (off) >> 4, (ra), (rt))
#define STQD(rt, ra, off) RI10(0x24, (off) >> 4, (ra), (rt))
#define LQA(rt, a)       RI16(0x061, (a) >> 2, (rt))
#define BRHZ(rt, pc, t)  RI16(0x044, ((int32_t)(t) - (int32_t)(pc)) >> 2, (rt))
#define BR(pc, t)        RI16(0x064, ((int32_t)(t) - (int32_t)(pc)) >> 2, 0)
#define BRA(t)           RI16(0x060, (t) >> 2, 0)
#define BRSL(rt, pc, t)  RI16(0x066, ((int32_t)(t) - (int32_t)(pc)) >> 2, (rt))
#define SHUFB(rt, ra, rb, rc) RRR(0xB, (rt), (rb), (ra), (rc))
#define ROTQBY(rt, ra, rb) RR(0x1DC, (rb), (ra), (rt))
#define BI(ra)           RR(0x1A8, 0, (ra), 0)
#define BIE(ra)          RR(0x1A8, 0x10, (ra), 0)
#define IRETE()          RR(0x1AA, 0x10, 0, 0)
#define RDCH(rt, ch)     RR(0x00D, 0, (ch), (rt))
#define WRCH(ch, rt)     RR(0x10D, 0, (ch), (rt))
#define STOP(code)       ((uint32_t)(code) & 0x3FFF)

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static spu_context* new_ctx(void)
{
    spu_context* c = NULL;
    if (posix_memalign((void**)&c, 64, sizeof *c) != 0) { perror("alloc"); exit(2); }
    spu_context_init(c, 0);
    return c;
}

/* Write a program at `at`; returns the address after it. */
static uint32_t emit(spu_context* c, uint32_t at, const uint32_t* p, size_t n)
{
    for (size_t i = 0; i < n; i++) spu_ls_write32(c, at + 4u * (uint32_t)i, p[i]);
    return at + 4u * (uint32_t)n;
}

static int eq128(u128 a, u128 b) { return memcmp(&a, &b, sizeof a) == 0; }

static void test_alu_and_branches(void)
{
    spu_context* c = new_ctx();
    /* quadwords for shufb / rotqby at LS 0x2000.. */
    for (uint32_t i = 0; i < 16; i++) {
        c->ls[0x2000 + i] = (uint8_t)(0x10 + i);
        c->ls[0x2010 + i] = (uint8_t)(0x80 + i);
        c->ls[0x2020 + i] = (uint8_t)((i * 7 + 3) & 0x1F) | (i == 5 ? 0xC0 : 0) | (i == 9 ? 0x80 : 0);
    }
    const uint32_t P = 0x1000;
    const uint32_t prog[] = {
        /* 1000 */ IL(3, 5),
        /* 1004 */ AI(4, 3, -3),              /* r4 = 2 */
        /* 1008 */ LQA(10, 0x2000),
        /* 100C */ LQA(11, 0x2010),
        /* 1010 */ LQA(12, 0x2020),
        /* 1014 */ SHUFB(13, 10, 11, 12),
        /* 1018 */ IL(5, 4),
        /* 101C */ ROTQBY(14, 10, 5),
        /* 1020 */ IL(1, 0x3000),
        /* 1024 */ STQD(13, 1, 0x20),
        /* 1028 */ LQD(15, 1, 0x20),
        /* 102C */ ILHU(6, 0x0090),
        /* 1030 */ IOHL(6, 0x0001),           /* word 0x00900001: pref halfword = 0x0001 */
        /* 1034 */ CEQHI(7, 6, 1),            /* pref halfword == 1 -> 0xFFFF */
        /* 1038 */ BRHZ(7, 0x1038, 0x1044),   /* not taken */
        /* 103C */ BRSL(0, 0x103C, 0x1060),   /* call */
        /* 1040 */ STOP(0x11),                /* expected exit */
        /* 1044 */ STOP(0x66),                /* wrong: brhz taken */
        0, 0, 0, 0, 0, 0,
        /* 1060 */ IL(8, 7),
        /* 1064 */ BI(0),                     /* return */
    };
    emit(c, P, prog, sizeof prog / sizeof prog[0]);
    int r = spu_interp_run(c, P, 1000);
    CHECK(r == SPU_INTERP_STOP, "result %s", spu_interp_result_name(r));
    CHECK(c->stop_code == 0x11, "stop code 0x%X (0x66 = brhz tested the wrong halfword)", c->stop_code);
    CHECK(c->gpr[4]._u32[0] == 2 && c->gpr[4]._u32[3] == 2, "ai: r4=%u", c->gpr[4]._u32[0]);
    CHECK(eq128(c->gpr[13], spu_shufb(c->gpr[10], c->gpr[11], c->gpr[12])), "shufb");
    CHECK(eq128(c->gpr[14], spu_rotqby(c->gpr[10], spu_pref_u32(4))), "rotqby");
    CHECK(c->ls[0x3020] == c->ls[0x3020] && eq128(c->gpr[15], c->gpr[13]), "lqd/stqd round trip");
    CHECK(eq128(c->gpr[7], spu_ceqhi(c->gpr[6], 1)), "ceqhi");
    CHECK(c->gpr[8]._u32[0] == 7, "callee ran (r8=%u)", c->gpr[8]._u32[0]);
    CHECK(c->gpr[0]._u32[0] == 0x1040 && c->gpr[0]._u32[1] == 0, "brsl link = {0x1040,0,0,0}");
    free(c);
}

/* Budget: a loop resumes exactly where it stopped. */
static void test_budget_resume(void)
{
    spu_context* c = new_ctx();
    const uint32_t prog[] = {
        /* 1000 */ IL(3, 100),
        /* 1004 */ AI(3, 3, -1),
        /* 1008 */ RI16(0x042, -1, 3),        /* brnz r3, 1004 */
        /* 100C */ STOP(0x22),
    };
    emit(c, 0x1000, prog, 4);
    int r = spu_interp_run(c, 0x1000, 51);
    CHECK(r == SPU_INTERP_BUDGET, "budget: %s", spu_interp_result_name(r));
    uint64_t total = spu_interp_last_count();
    int guard = 0;
    while (r == SPU_INTERP_BUDGET && guard++ < 100) {
        r = spu_interp_run(c, c->pc, 37);
        total += spu_interp_last_count();
    }
    CHECK(r == SPU_INTERP_STOP && c->stop_code == 0x22, "loop end %s", spu_interp_result_name(r));
    CHECK(total == 1 + 200 + 1, "insn count %llu", (unsigned long long)total);
    CHECK(c->gpr[3]._u32[0] == 0, "r3=%u", c->gpr[3]._u32[0]);
    free(c);
}

static void dma_list_program(spu_context* c, int with_bie)
{
    /* guest memory: two 16-byte sources */
    for (int i = 0; i < 16; i++) { vm_base[0x1000 + i] = (uint8_t)(0xA0 + i); vm_base[0x2000 + i] = (uint8_t)(0xB0 + i); }
    /* list at LS 0x2000: elem0 stall-and-notify, elem1 plain */
    spu_ls_write32(c, 0x2000, 0x80000010u); spu_ls_write32(c, 0x2004, 0x1000);
    spu_ls_write32(c, 0x2008, 0x00000010u); spu_ls_write32(c, 0x200C, 0x2000);
    /* interrupt vector + handler */
    spu_ls_write32(c, 0x0, BRA(0x800));
    const uint32_t h[] = {
        RDCH(10, 25),                         /* MFC_RdListStallStat */
        IL(11, 2), WRCH(2, 11),               /* SPU_WrEventAck Sn */
        IL(12, 5), WRCH(26, 12),              /* MFC_WrListStallAck tag 5 */
        IL(20, 1),
        IRETE(),
    };
    emit(c, 0x800, h, sizeof h / sizeof h[0]);
    const uint32_t L = 0x1044;
    const uint32_t m[] = {
        /* 1000 */ IL(3, 2), WRCH(1, 3),      /* SPU_WrEventMask = Sn */
        /* 1008 */ IL(4, 0x3000), WRCH(16, 4),
        /* 1010 */ IL(4, 0), WRCH(17, 4),
        /* 1018 */ IL(4, 0x2000), WRCH(18, 4),
        /* 1020 */ IL(4, 16), WRCH(19, 4),
        /* 1028 */ IL(4, 5), WRCH(20, 4),
        /* 1030 */ IL(4, 0x44), WRCH(21, 4),  /* GETL: stops at elem0, raises Sn */
        /* 1038 */ ILA(5, L),
        /* 103C */ with_bie ? BIE(5) : BI(5),
        /* 1040 */ STOP(0x77),
        /* 1044 */ IL(4, 0x20), WRCH(22, 4),  /* tag mask = tag 5 */
        /* 104C */ IL(4, 2), WRCH(23, 4),     /* ALL */
        /* 1054 */ RDCH(6, 24),               /* MFC_RdTagStat (blocks until the list is done) */
        /* 1058 */ STOP(0x42),
    };
    emit(c, 0x1000, m, sizeof m / sizeof m[0]);
}

static void test_dma_list_stall_interrupt(void)
{
    spu_context* c = new_ctx();
    dma_list_program(c, 1);
    int r = spu_interp_run(c, 0x1000, 10000);
    CHECK(r == SPU_INTERP_STOP && c->stop_code == 0x42, "result %s code 0x%X pc 0x%X",
          spu_interp_result_name(r), c->stop_code, c->pc);
    CHECK(c->gpr[20]._u32[0] == 1, "interrupt handler ran");
    CHECK(c->gpr[10]._u32[0] == 0x20, "RdListStallStat = 0x%X", c->gpr[10]._u32[0]);
    CHECK(c->srr0 == 0x1044, "SRR0 = bie target (0x%X)", c->srr0);
    CHECK(c->interp_ie == 1, "irete re-enabled interrupts");
    CHECK(c->gpr[6]._u32[0] == 0x20, "RdTagStat = 0x%X", c->gpr[6]._u32[0]);
    CHECK(c->ls[0x3000] == 0xA0 && c->ls[0x300F] == 0xAF, "elem0 data");
    CHECK(c->ls[0x3010] == 0xB0 && c->ls[0x301F] == 0xBF, "elem1 data (after the stall ack)");
    CHECK((c->event_status & 2u) == 0, "Sn acked");
    free(c);
}

/* Without the interrupt window nobody acks the stall: RdTagStat(ALL) must
 * block (WAIT at the rdch), not return a partial mask. */
static void test_dma_list_stall_blocks(void)
{
    spu_context* c = new_ctx();
    dma_list_program(c, 0);
    int r = spu_interp_run(c, 0x1000, 10000);
    CHECK(r == SPU_INTERP_WAIT && c->pc == 0x1054, "result %s pc 0x%X", spu_interp_result_name(r), c->pc);
    CHECK(c->ls[0x3010] == 0, "elem1 not transferred yet");
    free(c);
}

/* Self-modifying code: the program writes an instruction and runs it. */
static void test_self_modifying(void)
{
    spu_context* c = new_ctx();
    const uint32_t prog[] = {
        /* 1000 */ LQA(3, 0x1100),              /* quadword holding the new code */
        /* 1004 */ RI16(0x041, 0x1020 >> 2, 3), /* stqa r3, 0x1020 */
        /* 1008 */ BR(0x1008, 0x1020),
    };
    emit(c, 0x1000, prog, 3);
    const uint32_t gen[] = { IL(9, 1234), STOP(0x33), 0, 0 };
    emit(c, 0x1100, gen, 4);
    int r = spu_interp_run(c, 0x1000, 100);
    CHECK(r == SPU_INTERP_STOP && c->stop_code == 0x33 && c->gpr[9]._u32[0] == 1234,
          "generated code ran (%s, code 0x%X)", spu_interp_result_name(r), c->stop_code);
    free(c);
}

/* Decrementer counts down with host time. */
static void test_decrementer(void)
{
    spu_context* c = new_ctx();
    const uint32_t prog[] = { IL(3, 0x7FFF), WRCH(7, 3), RDCH(4, 8), STOP(1) };
    emit(c, 0x1000, prog, 4);
    spu_interp_run(c, 0x1000, 100);
    uint32_t d1 = c->gpr[4]._u32[0];
    struct timespec ts = { 0, 2000000 }; nanosleep(&ts, NULL);
    const uint32_t p2[] = { RDCH(5, 8), STOP(2) };
    emit(c, 0x1100, p2, 2);
    spu_interp_run(c, 0x1100, 100);
    uint32_t d2 = c->gpr[5]._u32[0];
    CHECK(d1 <= 0x7FFF && (int32_t)(d1 - d2) >= 100000, "decrementer moved %d ticks in 2 ms", (int32_t)(d1 - d2));
    free(c);
}

int main(void)
{
    vm_base = (uint8_t*)calloc(1, 1 << 20);
    test_alu_and_branches();
    test_budget_resume();
    test_dma_list_stall_interrupt();
    test_dma_list_stall_blocks();
    test_self_modifying();
    test_decrementer();
    if (g_fail) { printf("test_spu_interp: %d FAILED\n", g_fail); return 1; }
    printf("test_spu_interp: all passed\n");
    return 0;
}
