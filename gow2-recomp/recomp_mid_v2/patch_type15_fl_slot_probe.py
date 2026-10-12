#!/usr/bin/env python3
"""Probe do slot da free-list TYPE15 à entrada/saída de func_0039E794.

Contexto
--------
notes/2026-07-31-type15-a-premissa-estava-errada.md fechou que a fábrica
`func_0039E794` (this=0x47D00000 / vt=0x00516D70, a mesma pinada por
ps3_type15_note_resolve REHOME) é chamada DUAS vezes com aritmética de índice
IDÊNTICA (+0x24, +0x44, +0xD4), mas devolve produto válido na 1ª chamada e
`'Orbo'` (0x4F72626F) na 2ª. Ou seja: não é o índice que muda — é o CONTEÚDO
da memória guest que essa aritmética resolve. O candidato óbvio é a free-list
em `this+0x24` (fl → head → slot), que `ps3_type15_freelist_replenish`
(host_gow2_factory.cpp:495) e `ty15_force_pin_freelist` tentam manter válida
depois de cada construct.

Esta probe mede, sem adivinhar, à ENTRADA e à SAÍDA de cada chamada à
fábrica:

    fl    = vm_read32(this + 0x24)
    head  = vm_read32(fl)      (se fl for um EA plausível)
    slot  = vm_read32(head)    (se head for um EA plausível)
    count = vm_read32(fl + 4)

À entrada: o estado que o construct guest vai consumir (herdado da reparação
da chamada anterior, ou do estado inicial). À saída: o estado deixado para a
PRÓXIMA chamada, já depois do bloco de reparação host (ty15_force_pin_freelist
+ ps3_type15_freelist_replenish) que corre no fim de func_0039E794. Comparar
saída da 1ª chamada com entrada da 2ª decide se a corrupção acontece DENTRO do
par (guest, entre as duas invocações da fábrica) ou se o reparador já entrega
um slot ruim e a construção seguinte simplesmente o consome.

Gate (default OFF, regra 6 do CLAUDE.md)
  PS3_TRACE_TYPE15_FL=1   -> ON
  ausente / '' / '0' / outro -> OFF, no-op total no baseline

Telemetria (só para this==0x47D00000 ou vt(this)==0x00516D70 — a fábrica
pinada; outras fábricas não interessam a este ponto):

  [TYPE15FL] pre#N  this=0x........ fl=0x........ head=0x........ slot=0x........ count=N
  [TYPE15FL] post#N this=0x........ fl=0x........ head=0x........ slot=0x........ count=N

`pre#N`/`post#N` partilham o mesmo N (a N-ésima chamada à fábrica pinada) —
compare-os par a par.

NÃO altera fluxo de controlo guest -- lê `ctx` e memória guest, imprime, sai.
Todas as leituras são guardadas por range check (0x10000..0x4F000000), a
mesma banda que os blocos TYPE15 já em produção usam.

Convivência: insere-se no PREÂMBULO do corpo de func_0039E794 (entre os dois
`uint64_t _cs_2N = ...` de save de callee-save e a atribuição de `gpr[10]`) e
no EPÍLOGO (imediatamente antes do primeiro `ctx->gpr[0] = vm_read64(...)` do
restore), depois do bloco "Always snap successful products" que já existe em
produção. Não toca em nenhuma das agulhas desse bloco.

Idempotente: marcador presente -> ALREADY, rc=0.
rc: 0 aplicado ou já-aplicado; 2 nenhum chunk com a função; 3 agulha em falta
    ou forma inesperada (lift mudou).
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARKER = "TYPE15-FL-SLOT-PROBE"
DEFAULT = "recomp_macos_v2/ppu_recomp_001.cpp"
FN_SIG = "void func_0039E794(ppu_context* ctx) {\n"

PRE_NEEDLE = ("        uint64_t _cs_28 = ctx->gpr[28];\n"
              "        uint64_t _cs_29 = ctx->gpr[29];\n")

POST_NEEDLE = ("          }\n"
               "        }\n"
               "        ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0xA0);\n")

HELPER = r'''
/* TYPE15-FL-SLOT-PROBE: fl/head/slot/count da free-list TYPE15 à entrada e
 * saída de cada chamada à fábrica pinada (PS3_TRACE_TYPE15_FL=1). Read-only. */
static int ps3_ty15fl_gate_on(void) {
    static int on = -1;
    if (on < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_TYPE15_FL");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on;
}
static int ps3_ty15fl_ea_ok(uint32_t ea) {
    return ea >= 0x10000u && ea < 0x4F000000u;
}
static int ps3_ty15fl_is_factory(uint32_t this_ea) {
    if (this_ea == 0x47D00000u) return 1;
    if (!ps3_ty15fl_ea_ok(this_ea)) return 0;
    return vm_read32(this_ea) == 0x00516D70u;
}
static unsigned long long g_ps3_ty15fl_n = 0;
static void ps3_ty15fl_dump(const char* tag, uint32_t this_ea, unsigned long long n) {
    uint32_t fl    = vm_read32(this_ea + 0x24u);
    uint32_t head  = ps3_ty15fl_ea_ok(fl)   ? vm_read32(fl)   : 0u;
    uint32_t slot  = ps3_ty15fl_ea_ok(head) ? vm_read32(head) : 0u;
    uint32_t count = ps3_ty15fl_ea_ok(fl)   ? vm_read32(fl + 4u) : 0u;
    fprintf(stderr,
        "[TYPE15FL] %s#%llu this=0x%08X fl=0x%08X head=0x%08X slot=0x%08X count=%u\n",
        tag, n, this_ea, fl, head, slot, count);
    fflush(stderr);
}
static void ps3_ty15fl_pre(uint32_t this_ea) {
    if (!ps3_ty15fl_gate_on() || !ps3_ty15fl_is_factory(this_ea)) return;
    g_ps3_ty15fl_n++;
    ps3_ty15fl_dump("pre", this_ea, g_ps3_ty15fl_n);
}
static void ps3_ty15fl_post(uint32_t this_ea) {
    if (!ps3_ty15fl_gate_on() || !ps3_ty15fl_is_factory(this_ea)) return;
    ps3_ty15fl_dump("post", this_ea, g_ps3_ty15fl_n);
}
'''


def patch(path: Path) -> tuple[bool, int]:
    """Devolve (mudou_algo, rc)."""
    t = path.read_text(encoding="utf-8", errors="replace")

    if f"/* {MARKER} pre */" in t:
        print(f"  {path.name}: ALREADY (marcador presente)")
        return False, 0

    if FN_SIG not in t:
        print(f"  {path.name}: skip (sem {FN_SIG.strip()})")
        return False, -1

    if t.count(FN_SIG) != 1:
        print(f"ERRO: {FN_SIG.strip()} aparece {t.count(FN_SIG)}x em {path.name} "
              f"(esperado 1)", file=sys.stderr)
        return False, 3

    body_start = t.find(FN_SIG) + len(FN_SIG)
    body_end = t.find("\n}\n", body_start)
    if body_end < 0:
        print(f"ERRO: {path.name} nao tem fim de func_0039E794", file=sys.stderr)
        return False, 3
    body = t[body_start:body_end]

    if body.count(PRE_NEEDLE) != 1:
        print(f"ERRO: agulha PRE aparece {body.count(PRE_NEEDLE)}x no corpo de "
              f"func_0039E794 em {path.name} (esperado 1) -- lift mudou de "
              f"forma; NAO aplicado", file=sys.stderr)
        return False, 3
    if body.count(POST_NEEDLE) != 1:
        print(f"ERRO: agulha POST aparece {body.count(POST_NEEDLE)}x no corpo de "
              f"func_0039E794 em {path.name} (esperado 1) -- lift mudou de "
              f"forma; NAO aplicado", file=sys.stderr)
        return False, 3

    pre_at = body.find(PRE_NEEDLE)
    post_at = body.find(POST_NEEDLE)
    if post_at < pre_at:
        print(f"ERRO: agulha POST aparece antes da PRE em {path.name} -- "
              f"forma inesperada", file=sys.stderr)
        return False, 3

    # Aplica de tras para a frente: inserir na PRE deslocaria o offset da POST.
    body = (body[:post_at + len(POST_NEEDLE)]
            + f"        /* {MARKER} post */\n"
              "        { ps3_ty15fl_post((uint32_t)ctx->gpr[29]); }\n"
            + body[post_at + len(POST_NEEDLE):])
    body = (body[:pre_at + len(PRE_NEEDLE)]
            + f"        /* {MARKER} pre */\n"
              "        { ps3_ty15fl_pre((uint32_t)ctx->gpr[3]); }\n"
            + body[pre_at + len(PRE_NEEDLE):])

    t = t[:body_start] + body + t[body_end:]

    anchor_at = t.find("\nvoid func_")
    if anchor_at < 0:
        print(f"ERRO: {path.name} nao tem nenhuma funcao lifted onde ancorar",
              file=sys.stderr)
        return False, 3
    t = t[:anchor_at + 1] + HELPER + t[anchor_at + 1:]

    try:
        path.write_text(t, encoding="utf-8", newline="\n")
    except TypeError:                                # macOS system python3 (3.9.6)
        path.write_text(t, encoding="utf-8")
    print(f"  {path.name}: APPLIED (pre-entry + post-epilogue probe)")
    return True, 0


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], DEFAULT)
    print(f"[{MARKER}] alvos: {[str(p) for p in paths]}")

    applied_any = False
    already_any = False
    hard_fail = 0
    n_missing = 0
    for p in paths:
        if not p.exists():
            print(f"  {p}: MISSING")
            n_missing += 1
            continue
        changed, rc = patch(p)
        if rc == 3:
            hard_fail += 1
        elif rc == 0 and changed:
            applied_any = True
        elif rc == 0 and not changed:
            already_any = True
        # rc == -1: func nao esta' neste chunk, ignora

    if hard_fail:
        print(f"[{MARKER}] rc=3 ({hard_fail} falha(s) de forma)")
        return 3
    if n_missing == len(paths):
        print(f"[{MARKER}] rc=2 (nenhum chunk de lift encontrado)")
        return 2
    if applied_any or already_any:
        print(f"[{MARKER}] rc=0")
        return 0
    print(f"[{MARKER}] rc=2 (func_0039E794 nao encontrada em nenhum chunk)")
    return 2


if __name__ == "__main__":
    sys.exit(main())
