#!/usr/bin/env python3
"""POOLCTOR-PROBE: onde nasce o pool 0x80000000 do gestor de func_002A43DC.

Contexto (medido 2026-09-20, 4 relatorios de crash de 18 a 20/09):
o SIGBUS de gameplay morre em func_00263554 a ler *(pool+4) = 0x80000004,
com pool = *(gestor+0xD4) = 0x80000000. O construtor do gestor e'
func_002A4120, que escreve *(this+0xD4) a partir do 3.o argumento (r5).
Esse construtor so' e' chamado por vtable -- nao ha chamador estatico --
por isso a origem do r5 mau so' e' observavel em runtime.

A sonda responde a UMA pergunta, numa corrida:
  * o gestor foi construido com pool ja' mau (r5 == 0x80000000)?
  * foi construido bem e o campo foi pisado depois?
  * ou nunca passou por este construtor (objecto de outro tipo / stale)?

Dois ganchos, ambos READ-ONLY (nao mexem em registo nem em memoria guest),
gated por PS3_TRACE_POOLCTOR (ausente/0 = no-op no baseline):
  1. entrada de func_002A4120  -> regista (this, pool=r5, lr)
  2. func_002A43DC, logo apos   -> se o pool estiver fora da faixa comitada,
     `r3 = *(r30+0xD4)`            despeja gestor + campos + veredicto do ctor

Alvo: o ppu_recomp_XXX.cpp que define AS DUAS funcoes (no lift e435 e' o
ppu_recomp_001.cpp). Idempotente: correr duas vezes imprime ALREADY.

Uso:
  python3 tools/patch_poolctor_probe.py \
      ../gow2-recomp/recomp_macos_e435/ppu_recomp_001.cpp
"""
from __future__ import annotations

import argparse
from pathlib import Path

MARK = "POOLCTOR-PROBE"

CTOR_SIG = "void func_002A4120(ppu_context* PPU_RESTRICT ctx) {"
CHECK_ANCHOR = (
    "        ctx->gpr[3] = vm_read32(ctx->gpr[30] + 0xD4);\n"
    "        ctx->lr = 0x002A43E4; func_00263554(ctx);"
)

HELPERS = r"""
/* ===================== POOLCTOR-PROBE =================================
 * Gated por PS3_TRACE_POOLCTOR. OFF por default -> no-op no baseline.
 *
 * Porque existe: o SIGBUS de gameplay (4 relatorios, 18-20/09/2026, sempre
 * a mesma pilha e sempre EA 0x80000004) morre em func_00263554 a ler
 * *(pool+4) com pool = *(gestor+0xD4) = 0x80000000. O construtor do gestor
 * e' func_002A4120 -- escreve *(this+0xD4) a partir de r5 -- e so' e'
 * chamado por vtable, logo a origem do r5 nao e' visivel estaticamente.
 *
 * READ-ONLY: nenhum registo do ctx e nenhuma memoria guest sao alterados.
 * ====================================================================== */
static int poolctor_on(void) {
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_TRACE_POOLCTOR");
                  on = (e && *e && *e != '0') ? 1 : 0; }
    return on;
}
/* Faixa comitada do guest (ver "[vm] committed range" no arranque:
 * 0..0x51000000). Um pool fora disto e' ponteiro morto por definicao --
 * e' exactamente o que o guard de nao-comitado engolia antes de o caminho
 * NOCHECK de gameplay o deixar passar em cru. */
static int poolctor_sane(uint32_t p) { return p >= 0x10000u && p < 0x51000000u; }
struct Ps3PoolCtorRec { uint32_t self, pool, lr; };
static Ps3PoolCtorRec g_poolctor[512];
static int g_poolctor_n = 0;
/* Cadeia HOST, nao o ctx->lr do guest: o lr fica CONGELADO no ultimo `bl`,
 * e num despacho por vtable aponta para o chamador errado (mesma licao da
 * sonda FLHEAD em func_00263554). E' esta cadeia que identifica QUEM
 * constroi o gestor, que estaticamente nao se ve (a chamada e' por vtable).
 *
 * noinline de proposito: com a funcao inlinada os niveis de
 * __builtin_return_address deslizam e a leitura passa a mentir. Os dois
 * primeiros niveis sao esta sonda e a funcao liftada onde ela vive; o
 * chamador interessante aparece a partir de #2 (tipicamente
 * ps3_indirect_call e, por baixo, o func_<EA> que despachou). */
static __attribute__((noinline)) void poolctor_host_chain(const char* tag) {
    struct { const char* fn; void* fb; const char* sn; void* sa; } _di;
    void* _ra[6] = { __builtin_return_address(0), __builtin_return_address(1),
                     __builtin_return_address(2), __builtin_return_address(3),
                     __builtin_return_address(4), __builtin_return_address(5) };
    for (int i = 0; i < 6; i++)
        if (_ra[i] && dladdr(_ra[i], &_di) && _di.sn)
            fprintf(stderr, "          %s host#%d %s+0x%lX\n", tag, i, _di.sn,
                    (unsigned long)((char*)_ra[i] - (char*)_di.sa));
}
static void poolctor_note(uint32_t self, uint32_t pool, uint32_t lr) {
    if (g_poolctor_n < (int)(sizeof(g_poolctor) / sizeof(g_poolctor[0]))) {
        g_poolctor[g_poolctor_n].self = self;
        g_poolctor[g_poolctor_n].pool = pool;
        g_poolctor[g_poolctor_n].lr   = lr;
        g_poolctor_n++;
    }
    if (!poolctor_sane(pool)) {
        /* O caso que procuramos: o pool ja' nasce mau. SEM cap -- se isto
         * dispara muitas vezes, o numero de vezes E' a medida. */
        fprintf(stderr, "[POOLCTOR] CTOR-MAU this=0x%08X pool=0x%08X lr=0x%08X ctors=%d\n",
                self, pool, lr, g_poolctor_n);
        poolctor_host_chain("ctor");
        fflush(stderr);
        return;
    }
    { /* Construcoes sas: so' as primeiras, para ver a forma do pool bom. */
      static int k = 0;
      if (k++ < 12) {
          fprintf(stderr, "[POOLCTOR] ctor ok  this=0x%08X pool=0x%08X lr=0x%08X ctors=%d\n",
                  self, pool, lr, g_poolctor_n);
          fflush(stderr);
      } }
}
static void poolctor_check(uint32_t mgr, uint32_t pool, uint32_t node) {
    if (poolctor_sane(pool)) return;
    static int n = 0;
    if (n++ >= 8) return;
    int found = -1;
    for (int i = 0; i < g_poolctor_n; i++)
        if (g_poolctor[i].self == mgr) { found = i; break; }
    fprintf(stderr,
            "[POOLCTOR] POOL-MAU mgr=0x%08X pool=0x%08X node=0x%08X ctor=%s",
            mgr, pool, node, (found >= 0) ? "SIM" : "NAO");
    if (found >= 0)
        fprintf(stderr, " ctor_pool=0x%08X ctor_lr=0x%08X",
                g_poolctor[found].pool, g_poolctor[found].lr);
    /* Campos do gestor pelo layout que o construtor escreve (func_002A4120):
     * +0x70/+0x74 lista circular, +0xBA tamanho (u16), +0xCE contador (u8),
     * +0xD0 e +0xD4 o par que precede/e' o pool. mgr esta' comitado -- a
     * leitura de +0xD4 acabou de correr sem faltar. */
    fprintf(stderr, " | w0=0x%08X l70=0x%08X l74=0x%08X sz=0x%04X cnt=%d d0=0x%08X ctors=%d\n",
            (unsigned)vm_read32(mgr + 0x0), (unsigned)vm_read32(mgr + 0x70),
            (unsigned)vm_read32(mgr + 0x74), (unsigned)vm_read16(mgr + 0xBA),
            (int)(signed char)vm_read8(mgr + 0xCE), (unsigned)vm_read32(mgr + 0xD0),
            g_poolctor_n);
    poolctor_host_chain("pop ");
    fflush(stderr);
}
/* ===================== fim POOLCTOR-PROBE ============================= */

"""

CTOR_HOOK = (
    "\n        /* POOLCTOR-PROBE: r3/r4/r5 ainda sao os argumentos aqui. */\n"
    "        if (poolctor_on()) poolctor_note((uint32_t)ctx->gpr[3],\n"
    "                                         (uint32_t)ctx->gpr[5],\n"
    "                                         (uint32_t)ctx->lr);"
)

CHECK_HOOK = (
    "        ctx->gpr[3] = vm_read32(ctx->gpr[30] + 0xD4);\n"
    "        /* POOLCTOR-PROBE: gpr[3] e' o pool, gpr[30] o gestor, gpr[29] o no. */\n"
    "        if (poolctor_on()) poolctor_check((uint32_t)ctx->gpr[30],\n"
    "                                          (uint32_t)ctx->gpr[3],\n"
    "                                          (uint32_t)ctx->gpr[29]);\n"
    "        ctx->lr = 0x002A43E4; func_00263554(ctx);"
)


def transform(text: str) -> tuple[str, str]:
    if MARK in text:
        return text, "ALREADY"
    if text.count(CTOR_SIG) != 1 or text.count(CHECK_ANCHOR) != 1:
        return text, "NO-EFFECT"
    out = text.replace(CTOR_SIG, HELPERS + CTOR_SIG + CTOR_HOOK, 1)
    out = out.replace(CHECK_ANCHOR, CHECK_HOOK, 1)
    if "int dladdr(" not in out:
        return text, "NO-EFFECT"
    return out, "APPLIED"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("file", type=Path, help="ppu_recomp_XXX.cpp com func_002A4120 e func_002A43DC")
    args = ap.parse_args()
    original = args.file.read_text(encoding="utf-8", errors="replace")
    updated, state = transform(original)
    if state == "NO-EFFECT":
        print(f"NO-EFFECT {args.file}")
        return 1
    if state == "APPLIED":
        args.file.write_text(updated, encoding="utf-8")
    print(f"{state} {args.file.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
