#!/usr/bin/env python3
"""Fix na origem: REHOME da free-list TYPE15 deixava um ponteiro auto-referente
apontar para dentro da arena antiga (nao pinada).

Contexto (fecha a cadeia causal de notes/2026-07-31-type15-a-premissa-estava-
errada.md e da medicao subsequente com PS3_TRACE_TYPE15_FL=1, 3/3 corridas
identicas)
----------------------------------------------------------------------------
`ps3_type15_note_resolve` (REHOME, dentro de func_0039E794's classe) copia
~0x200 bytes da free-list ANTIGA (`fl`, em 0x401xxxxx) para a zona pinada
(`k_fl_pin = k_ty15_pin + 0x400`) com um `vm_write32(k_fl_pin+off,
vm_read32(fl+off))` RAW -- byte a byte, sem traduzir ponteiros internos.

Medido (3/3 corridas, PS3_TRACE_TYPE15_FL=1):

    pre#1  fl=0x47D00400 head=0x40100638 slot=0x40100844 count=1   <- head
           aponta para dentro da arena ANTIGA (fl_antigo+0x18), nao para
           k_fl_pin+0x18 -- o copy nunca traduziu esse auto-ponteiro.
    [construct #1 devolve produto valido 0x42F85AE4; freelist nao mexe]
    post#1 fl=0x47D00400 head=0x40100638 slot=0x40100844 count=1   <- identico
    [guest constroi ~15 OUTRAS fabricas; algo escreve ASCII "Orbs"
     (0x4F726273) em 0x40100638 -- reuso normal de heap na arena antiga,
     que o REHOME nunca protegeu porque so' redireccionou o ponteiro
     EXTERIOR (this+0x24), nao o interior]
    pre#2  fl=0x47D00400 head=0x40100638 slot=0x4F726273 count=1   <- corrompido
    [construct #2 le' esse slot corrompido -> devolve 'Orbo' 0x4F72626F]
    [ps3_type15_freelist_replenish DISPARA (a condicao slot>=0x4F000000
     cobre) -- mas so' no EPILOGO desta MESMA chamada, DEPOIS do construct
     ja' ter lido o lixo. Repara para a chamada seguinte, nunca para esta.]
    post#2 fl=0x47D00400 head=0x47D00418 slot=0x47D00804 count=1   <- reparado,
           agora inteiramente dentro da zona pinada

Root cause: o copy RAW deixa a free-list copiada com um ponteiro auto-
referente (o "head" que aponta para o proprio "mid" da mesma estrutura, a
0x18 do inicio) a apontar para a arena ANTIGA em vez da zona pinada -- um
nivel de indireccao ficou pinado, o seguinte ficou pendurado (dangling) em
memoria que o guest reutiliza livremente.

Fix (na origem, nao mascara nem reparador tardio): depois do copy RAW,
traduz qualquer palavra copiada cujo valor caia dentro de [fl, fl+0x200)
para o mesmo deslocamento dentro de [k_fl_pin, k_fl_pin+0x200) -- exactamente
a relocacao que um copy de estrutura ligada precisa para ficar auto-contido.
E' o MESMO layout que `ps3_type15_freelist_replenish` ja' constroi de raiz
(inteiramente dentro do pin), so' que agora tambem se aplica ao caminho de
REHOME que copia uma free-list ja' existente.

Gate: nenhum -- e' FUNCIONAL (fix de correctude), sempre activo. Nao ha
env var: like every REHOME call, so' corre uma vez por processo
(`g_ty15_rehomed`), e o `[TYPE15FL]` probe (patch_type15_fl_slot_probe.py,
PS3_TRACE_TYPE15_FL=1) e' quem mede o efeito -- head/slot devem passar a
apontar para dentro de [k_fl_pin, k_fl_pin+0x200) em AMBAS pre#2 e post#1.

Fonte de verdade versionada: ../host_gow2_factory.cpp (mesmo fix, aplicado a
mao la' primeiro e extraido literalmente para este patch).

Idempotente: marcador presente -> ALREADY, rc=0.
rc: 0 aplicado ou já-aplicado; 2 nenhum chunk com a função; 3 agulha em falta
    ou forma inesperada (lift mudou).
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARKER = "TYPE15-FL-REHOME-FIXUP"
DEFAULT = "recomp_macos_v2/ppu_recomp_001.cpp"
FN_SIG = "extern \"C\" void ps3_type15_note_resolve(uint32_t idx, uint32_t obj, uint32_t vt) {\n"

NEEDLE = ("                    uint32_t fl = vm_read32(k_ty15_pin + 0x24u);\n"
          "                    if (fl >= 0x40000000u && fl < 0x47D00000u) {\n"
          "                        for (uint32_t off = 0; off < 0x200u; off += 4u)\n"
          "                            vm_write32(k_fl_pin + off, vm_read32(fl + off));\n"
          "                        vm_write32(k_ty15_pin + 0x24u, k_fl_pin);\n")

FIXUP = (
    "                        /* {marker} (2026-07-31): o copy acima e' RAW --\n"
    "                         * qualquer ponteiro AUTO-REFERENTE dentro do proprio\n"
    "                         * blob (ex.: o head em fl+0 que apontava para fl+0x18,\n"
    "                         * o \"mid\" da mesma free-list) continua a apontar para\n"
    "                         * dentro da arena ANTIGA (nao protegida). Medido 3/3\n"
    "                         * corridas: a 2a chamada da fabrica le esse ponteiro\n"
    "                         * auto-referente -- stream-stomped para ASCII (\"Orbs\",\n"
    "                         * 0x4F726273) por outra alocacao do guest reusando a\n"
    "                         * arena antiga -- e devolve 'Orbo' (0x4F72626F) em vez\n"
    "                         * de um produto valido. O reparador so' dispara no\n"
    "                         * epilogo da chamada SEGUINTE -- tarde para esta.\n"
    "                         * Traduz qualquer palavra copiada dentro de\n"
    "                         * [fl, fl+0x200) para o mesmo deslocamento dentro de\n"
    "                         * [k_fl_pin, k_fl_pin+0x200): so' assim o blob fica\n"
    "                         * inteiramente auto-contido na zona pinada. */\n"
    "                        for (uint32_t off = 0; off < 0x200u; off += 4u) {{\n"
    "                            uint32_t w = vm_read32(k_fl_pin + off);\n"
    "                            if (w >= fl && w < fl + 0x200u)\n"
    "                                vm_write32(k_fl_pin + off, k_fl_pin + (w - fl));\n"
    "                        }}\n"
).format(marker=MARKER)


def patch(path: Path) -> tuple[bool, int]:
    t = path.read_text(encoding="utf-8", errors="replace")

    if f"/* {MARKER} " in t:
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
        print(f"ERRO: {path.name} nao tem fim de ps3_type15_note_resolve",
              file=sys.stderr)
        return False, 3
    body = t[body_start:body_end]

    n = body.count(NEEDLE)
    if n != 1:
        print(f"ERRO: agulha REHOME aparece {n}x no corpo de "
              f"ps3_type15_note_resolve em {path.name} (esperado 1) -- lift "
              f"mudou de forma; NAO aplicado", file=sys.stderr)
        return False, 3

    at = body.find(NEEDLE)
    insert_at = at + len(NEEDLE) - len(
        "                        vm_write32(k_ty15_pin + 0x24u, k_fl_pin);\n")
    body = body[:insert_at] + FIXUP + body[insert_at:]

    t = t[:body_start] + body + t[body_end:]

    try:
        path.write_text(t, encoding="utf-8", newline="\n")
    except TypeError:                                # macOS system python3 (3.9.6)
        path.write_text(t, encoding="utf-8")
    print(f"  {path.name}: APPLIED (rehome fixup: traduz auto-ponteiros para o pin)")
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
        # rc == -1: funcao nao esta' neste chunk, ignora

    if hard_fail:
        print(f"[{MARKER}] rc=3 ({hard_fail} falha(s) de forma)")
        return 3
    if n_missing == len(paths):
        print(f"[{MARKER}] rc=2 (nenhum chunk de lift encontrado)")
        return 2
    if applied_any or already_any:
        print(f"[{MARKER}] rc=0")
        return 0
    print(f"[{MARKER}] rc=2 (ps3_type15_note_resolve nao encontrada em nenhum chunk)")
    return 2


if __name__ == "__main__":
    sys.exit(main())
