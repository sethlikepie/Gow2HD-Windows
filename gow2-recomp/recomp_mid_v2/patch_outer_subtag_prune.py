#!/usr/bin/env python3
"""Fase 11 (11-03) -- fix candidato B: poda por subtag no walk exterior
`func_0041FF70` (chunk 001).

Contexto medido (nao re-derivado aqui -- ver os ledgers):
  - docs/re_sessions/2026-08-03-E1-typewalk-trace.md: o push interior
    (`func_0041F700`) mede in-boot um registo WAD (`h=0x4077AD5C`,
    `w0=0x40030001`) cuja `+0x7C` e' `matriz[0][3]` (0.0f), lida como
    cabeca de lista -- `sent_head=0x00000000`, um `for` que nunca chega a
    sentinela, 179 459 343 iteracoes numa unica chamada de push.
  - docs/re_sessions/2026-08-04-E2-quem-forca-low16.md: o desserializador
    `0x00254C40` preserva o `subtag` e forca `low16 := 1` em DOIS caminhos
    do stream -- para um registo WAD (`subtag=0x003`) isso produz
    `w0 = 0x40030001`, exactamente o valor medido pela E1.
  - docs/re_sessions/2026-08-04-E3-outer-walk-subtag-census.md: a sonda de
    contagem D-11.2 mediu, em 3/3 corridas deterministicas, EXACTAMENTE 1
    objecto que passa os dois filtros de flags de `0x0041FF70` e tem
    `subtag != 1` -- DECISAO: B (nao A).
  - docs/re_sessions/2026-08-04-E4-fix-verification-parede-4.md: a PRIMEIRA
    versao deste patch (que mutava `ctx->gpr[25]` antes da comparacao
    `iVar6==iVar2`, gating so' o dispatch condicional `tab[low16(h_child)]`)
    foi verificada in-boot e teve ZERO efeito -- medido, nao inferido. A
    causa: `h_child` (`ctx->gpr[27] = vm_read32(ctx->gpr[28]+0x88)`) e'
    LIXO para o registo WAD (`0x3F800000`, um float da regiao da matriz --
    a MESMA leitura que a sonda de censo do 11-01/E3 ja tinha medido e
    marcado como ressalva nao fechada). O push real do WAD nao vem desse
    dispatch condicional: vem de um SEGUNDO dispatch, INCONDICIONAL, mais
    abaixo em `loc_00420090` (sempre alcancado, quer se caia lá por
    fallthrough quer por goto), que usa `low16 = vm_read16(ctx->gpr[28]+6)`
    -- o low16 do PROPRIO no' de topo (gpr28+4 = gpr28's w0, MEDIDO
    directamente por debug ad-hoc: `vm_read32(gpr28+4) = 0x40030001` para o
    no' patologico) -- e route sempre para `tab[low16]`, sem nenhum branch
    condicional para gatilhar.

Onde vai o fix (v2, corrigido apos a medicao E4): em vez de tentar guiar um
compare condicional que so' protege UM dos dois dispatches, este patch
insere um TERCEIRO filtro logo a seguir a `ctx->gpr[28] = ppc_rldicl(...)`
em `loc_0042002C` -- o MESMO ponto onde o gate "tem filho" ja existe
duas linhas abaixo, e o MESMO destino de skip (`goto loc_0041FFE8`) que
esse gate e os dois filtros de flags no topo do laco JA usam para "este
no' nao tem nada para fazer aqui, avanca para o proximo". Le
`w0 = vm_read32(gpr28+4)` (a classificacao do PROPRIO no' de topo -- nao a
de um "filho"), e quando `low16(w0)==1 && subtag(w0)!=1` -- o predicado
exacto de D-11.1 candidato B, o mesmo de
tools/test_wall4_subtag_fix.py::prune(w0), NAO re-derivado -- salta para
`loc_0041FFE8`, o MESMO rotulo que os filtros de flags (`flags & 0x10`) e o
gate "tem filho" (`*(gpr28+0x70)==0`) ja usam para a MESMA semantica
("nada a processar neste no', avanca"). Isto reutiliza um destino de saida
JA EXISTENTE e um padrao de filtro JA EXISTENTE (D-11.1 descreve B
literalmente como "filtrar tambem por subtag, nao so' por flags") -- nao
inventa push/pop nem reimplementa o walk; apenas adiciona um TERCEIRO
filtro de enumeracao ao lado dos dois que ja' ha'.

Candidato C (validar a lista antes de a percorrer, em `func_0041F700`) e'
PROIBIDO como fix (D-11.1, CLAUDE.md regra 5) -- este patch nao o e': nao
toca em `func_0041F700` nem no ponteiro de lista que ele desreferencia;
filtra a ENUMERACAO no walk exterior, antes de qualquer push acontecer,
pela classificacao do proprio no' -- exactamente a definicao de B.

Este patch e' INCONDICIONAL (sem PS3_*, sem gate) -- por D-11.1/criterio 2
("correccao fiel ao CELL, nao um gate").

A leitura `vm_read32(gpr28+4)` e' tao arriscada quanto a leitura
`vm_read32(gpr28+0x70)` que o gate "tem filho" ja faz, sem guarda, duas
linhas abaixo -- por isso o patch usa o mesmo bounds-check preguicoso
(`>= 0x10000 && < 0x4F000000`) que a instrumentacao OUTER-WALK-PROBE ja usa
nesta mesma funcao, so' para nao ler fora do intervalo de EA guest
plausivel.

Idempotente. Roda a partir de recomp_mid_v2/ (ou com o path do lift no
argv[1], como todos os outros patch_*.py deste catalogo -- ver
apply_all_patches.sh, que invoca `python3 <patch> <LIFT_DIR>`).
"""
from __future__ import annotations

import sys
from pathlib import Path

NEEDLE = (
    "loc_0042002C:\n"
    "        ctx->gpr[28] = ppc_rldicl(ctx->gpr[26], 0, 32);\n"
    "        ctx->gpr[0] = vm_read32(ctx->gpr[28] + 0x70);\n"
)

REPL = (
    "loc_0042002C:\n"
    "        ctx->gpr[28] = ppc_rldicl(ctx->gpr[26], 0, 32);\n"
    "        /* Fase 11-03 FIX v2 (candidato B, D-11.2, corrigido apos\n"
    "         * docs/re_sessions/2026-08-04-E4-fix-verification-parede-4.md):\n"
    "         * classificacao do PROPRIO no' de topo (nao de um h_child --\n"
    "         * ver ledger para o porque). O desserializador 0x00254C40\n"
    "         * preserva o subtag mas forca low16:=1 para qualquer registo do\n"
    "         * stream com subtag!=1 (E2); um registo WAD (subtag=0x003) fica\n"
    "         * mal-classificado como low16=1 e passaria pelos dois filtros\n"
    "         * de flags acima. Terceiro filtro, mesma forma dos dois\n"
    "         * primeiros: se a classificacao do no' bate o predicado de poda,\n"
    "         * salta para loc_0041FFE8 -- o MESMO destino que o filtro\n"
    "         * flags&0x10 e o gate \"tem filho\" (duas linhas abaixo) ja usam\n"
    "         * para \"nada a processar neste no', avanca para o proximo\".\n"
    "         * Predicado identico, byte a byte, ao\n"
    "         * tools/test_wall4_subtag_fix.py::prune(w0): low16(w0)==1 e\n"
    "         * subtag(w0)!=1. Incondicional (sem gate PS3_) -- fiel ao CELL\n"
    "         * per D-11.1/criterio 2, nao e' um interruptor de diagnostico. */\n"
    "        {\n"
    "            uint32_t hself = (uint32_t)ctx->gpr[28];\n"
    "            if (hself >= 0x10000u && hself < 0x4F000000u) {\n"
    "                uint32_t w0 = vm_read32(hself + 4);\n"
    "                uint32_t low16 = w0 & 0xFFFFu;\n"
    "                uint32_t subtag = (w0 >> 16) & 0xFFFu;\n"
    "                if (low16 == 1 && subtag != 1) {\n"
    "                    goto loc_0041FFE8;\n"
    "                }\n"
    "            }\n"
    "        }\n"
    "        ctx->gpr[0] = vm_read32(ctx->gpr[28] + 0x70);\n"
)

MARKER = "Fase 11-03 FIX v2 (candidato B, D-11.2, corrigido apos"


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    path = root / "ppu_recomp_001.cpp"
    if not path.is_file():
        print(f"FAIL: {path} not found", file=sys.stderr)
        return 1
    src = path.read_text(encoding="utf-8", errors="replace")

    if MARKER in src:
        print("OK: already patched")
        return 0

    if src.count(NEEDLE) != 1:
        print(
            f"FAIL: needle not found (or not unique) in {path} -- "
            "lifted source layout changed? (needle occurrences="
            f"{src.count(NEEDLE)})",
            file=sys.stderr,
        )
        return 2

    # NB: Path.write_text(newline=...) so' existe em py3.10+ (armadilha ja paga,
    # ver CLAUDE.md) e o python3 do sistema desta maquina e' 3.9.6. O texto de
    # origem ja usa so' "\n" -- escrever bytes directamente e' portavel em
    # qualquer versao e nao arrisca traducao de fim de linha.
    new_src = src.replace(NEEDLE, REPL, 1)
    path.write_bytes(new_src.encode("utf-8"))
    print(f"OK: patched {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
