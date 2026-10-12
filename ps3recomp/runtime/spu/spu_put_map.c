/*
 * spu_put_map.c -- who wrote this guest buffer? (diagnostic, OFF by default)
 *
 * A recorded draw only knows the guest address of its vertex array. When the
 * data in it is wrong, the first question is which SPU job produced it -- and
 * an SPU PUT lands in vm_base by memcpy, so nothing else in the runtime sees
 * it. With PS3_TRACE_SPU_PUT=1 every PUT is kept in a small ring, and a
 * consumer (the Metal backend, for the inflated particle quads) asks which
 * job's DMA covers an address.
 *
 * Gated: without the env var spu_put_record() returns on its first test and
 * the lookup finds nothing, so the baseline is untouched.
 */
#include "spu_put_map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPU_PUT_RING 8192u

static spu_put_rec s_ring[SPU_PUT_RING];
static unsigned    s_head;          /* next slot to write */
static unsigned long long s_seq;
static int         s_on = -1;

int spu_put_map_on(void)
{
    if (s_on < 0) {
        const char* e = getenv("PS3_TRACE_SPU_PUT");
        s_on = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return s_on;
}

static void spu_xfer_record(unsigned int ea, unsigned int size, int image_id,
                            unsigned int pc, unsigned int lr, int is_get)
{
    if (!spu_put_map_on() || !size) return;
    const unsigned slot = (s_head++) % SPU_PUT_RING;
    spu_put_rec* r = &s_ring[slot];
    r->ea = ea;
    r->size = size;
    r->image_id = image_id;
    r->pc = pc;
    r->lr = lr;
    r->is_get = is_get;
    r->seq = ++s_seq;
}

static void spu_range_note(unsigned int ea, unsigned int size, int image_id,
                           unsigned int pc, int is_get);

extern volatile unsigned int g_wp32_watch_ea;   /* winport v32-gcmdiag-w */
extern void wp32_store_hit_c(unsigned int, unsigned int, int, void*);
void spu_put_record(unsigned int ea, unsigned int size, int image_id,
                    unsigned int pc, unsigned int lr)
{
    if (__builtin_expect(ea <= g_wp32_watch_ea + 3u && g_wp32_watch_ea < ea + size, 0))
        wp32_store_hit_c(ea, size, 100 + (image_id & 15), (void*)(unsigned long long)pc);
    spu_range_note(ea, size, image_id, pc, 0);
    spu_xfer_record(ea, size, image_id, pc, lr, 0);
}

/* PS3_TRACE_SPU_RANGE=<lo>:<hi> (hex, diagnostico, OFF): cada DMA do SPU que
 * toca esse intervalo de EA, GET ou PUT, com imagem e pc. Existe para saber se
 * algum job SPU LE' um render target que o jogo desenhou em memoria principal
 * -- no nosso backend esses pixels so' existem no Metal, a memoria do guest
 * nunca os recebe (o "Write Color Buffers" do RPCS3). */
static void spu_range_note(unsigned int ea, unsigned int size, int image_id,
                           unsigned int pc, int is_get)
{
    static int parsed = 0;
    static unsigned lo = 0, hi = 0;
    static unsigned n = 0;
    if (!parsed) {
        const char* e = getenv("PS3_TRACE_SPU_RANGE");
        if (e && *e) {
            lo = (unsigned)strtoul(e, NULL, 16);
            const char* c = strchr(e, ':');
            hi = c ? (unsigned)strtoul(c + 1, NULL, 16) : lo;
        }
        parsed = 1;
    }
    if (!hi || ea >= hi || ea + size <= lo || n >= 60u) return;
    n++;
    fprintf(stderr, "[SPURANGE] %s ea=%08X size=%u img=%d pc=0x%05X\n",
            is_get ? "GET" : "PUT", ea, size, image_id, pc);
    fflush(stderr);
}

extern volatile unsigned int g_wp32_lbl_ea;   /* winport v32-gcmdiag-l */
extern void wp32_lbl_get_note(unsigned int, int);
void spu_get_record(unsigned int ea, unsigned int size, int image_id,
                    unsigned int pc, unsigned int lr)
{
    if (__builtin_expect(ea <= g_wp32_lbl_ea && g_wp32_lbl_ea < ea + size, 0)) wp32_lbl_get_note(ea, 0);
    spu_range_note(ea, size, image_id, pc, 1);
    spu_xfer_record(ea, size, image_id, pc, lr, 1);
}

/* Regions the SPU touched, coalesced by 64 KB. The particle STATE lives in a
 * region the job both reads and writes every frame; the vertex buffer it only
 * writes. That is what separates them in the output. */
void spu_put_map_summary(void)
{
    if (!spu_put_map_on()) return;
    struct { unsigned page; unsigned long long rd, wr; unsigned nr, nw; unsigned pc_w, pc_r; }
        agg[64];
    unsigned na = 0;
    for (unsigned i = 0; i < SPU_PUT_RING; i++) {
        const spu_put_rec* r = &s_ring[i];
        if (!r->size) continue;
        const unsigned page = r->ea >> 16;
        unsigned k = 0;
        for (; k < na; k++) if (agg[k].page == page) break;
        if (k == na) {
            if (na == sizeof agg / sizeof agg[0]) continue;
            na++;
            agg[k].page = page; agg[k].rd = agg[k].wr = 0;
            agg[k].nr = agg[k].nw = 0; agg[k].pc_w = agg[k].pc_r = 0;
        }
        if (r->is_get) { agg[k].rd += r->size; agg[k].nr++; agg[k].pc_r = r->pc; }
        else           { agg[k].wr += r->size; agg[k].nw++; agg[k].pc_w = r->pc; }
    }
    for (unsigned k = 0; k < na; k++)
        if (agg[k].nr && agg[k].nw)     /* lido E escrito = estado, nao saida */
            fprintf(stderr, "[SPUMAP] %08X0000 le=%llu B/%u escreve=%llu B/%u "
                    "pc_r=0x%05X pc_w=0x%05X\n", agg[k].page, agg[k].rd, agg[k].nr,
                    agg[k].wr, agg[k].nw, agg[k].pc_r, agg[k].pc_w);
    fflush(stderr);
}

unsigned long long spu_put_seq(void) { return s_seq; }

int spu_put_owner(unsigned int ea, spu_put_rec* out)
{
    if (!spu_put_map_on()) return 0;
    /* The most recent transfer covering ea wins: a buffer can be written more
     * than once per frame and only the last write is what the RSX reads. */
    const spu_put_rec* best = NULL;
    for (unsigned i = 0; i < SPU_PUT_RING; i++) {
        const spu_put_rec* r = &s_ring[i];
        if (!r->size || ea < r->ea || ea >= r->ea + r->size) continue;
        if (!best || r->seq > best->seq) best = r;
    }
    if (!best) return 0;
    if (out) *out = *best;
    return 1;
}


/* ---- PS3_TRACE_SPU_PC: o caminho de codigo que produziu um PUT ----------- */
void spu_pc_ring_dump(const unsigned int* ring, unsigned int n,
                      unsigned int ea, unsigned int size, float v)
{
    /* Um dump por assinatura do caminho: o que interessa e' o conjunto de
     * funcoes, e este PUT repete-se dezenas de vezes por segundo. */
    static unsigned long long seen[16];
    static unsigned nseen;
    const unsigned count = n < 32u ? n : 32u;
    unsigned long long key = 0;
    for (unsigned i = 0; i < count; i++)
        key = key * 1000003ull + ring[(n - count + i) & 31u];
    for (unsigned i = 0; i < nseen; i++) if (seen[i] == key) return;
    if (nseen == sizeof seen / sizeof seen[0]) return;
    seen[nseen++] = key;
    fprintf(stderr, "[SPUPC] ea=%08X size=%u v=%g caminho:", ea, size, (double)v);
    for (unsigned i = 0; i < count; i++)
        fprintf(stderr, " %05X", ring[(n - count + i) & 31u]);
    fprintf(stderr, "\n");
    fflush(stderr);
}

/* Onde o valor inflado apareceu primeiro em local store (ver spu_context.h). */
void spu_huge_dump(const unsigned int* pc, const unsigned int* lsa,
                   const float* v, unsigned int n)
{
    static int done;
    if (done >= 6 || !n) return;
    done++;
    fprintf(stderr, "[SPUHUGE] --- lote %d ---\n", done);
    for (unsigned i = 0; i < n && i < 8u; i++)
        fprintf(stderr, "[SPUHUGE] #%u pc=0x%05X ls=0x%05X v=%g\n", i, pc[i], lsa[i], (double)v[i]);
    fflush(stderr);
}

/* O banco de registadores no instante da primeira escrita inflada. */
void spu_huge_regs_dump(const float* regs, int taken)
{
    static int done;
    if (!taken || done) return;
    done = 1;
    for (int i = 0; i < 128; i += 4)
        fprintf(stderr, "[SPUREG] r%-3d %-14g r%-3d %-14g r%-3d %-14g r%-3d %g\n",
                i, (double)regs[i], i+1, (double)regs[i+1],
                i+2, (double)regs[i+2], i+3, (double)regs[i+3]);
    fflush(stderr);
}
