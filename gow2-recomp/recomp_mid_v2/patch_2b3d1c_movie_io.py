#!/usr/bin/env python3
r"""AREAD-HLE: early-out em func_002B3D1C que liga o read assincrono FIOS
ao movie_io do .m2v.

Porque existe (Fase 10 -- "A parede do DecodeAu")
--------------------------------------------------
Medicao da CONTEXT.md da Fase 10 (2026-07-31): o guest abre os quatro
ficheiros certos (.m2v + os dois WAD), abre o decoder, faz os dois StartSeq
-- e depois `cellVdecDecodeAu` e' chamado **0** vezes, contra **10** no
binario de referencia. Tudo identico ate' ali.

O inventario da Task 1 (`inventory_lift_markers.py`) confirma que o marcador
`AREAD-HLE` existe em `recomp_macos_v2.pre_v4/ppu_recomp_001.cpp` (o lift que
gera o binario bom) e esta' AUSENTE do lift actual -- e que este proprio
ficheiro era o candidato "sem escritor": a versao anterior so' fazia
`print()` de instrucoes e devolvia rc=0, exactamente a armadilha #2 da
CONTEXT.md ("Um patch que nao aplica nada"), a mesma classe do
`patch_fios_f2a_f2b_wad.py` ja' conhecido da Fase 9.

O que o bloco faz
-----------------
`func_002B3D1C` e' o ponto onde o guest submete um read assincrono FIOS
(ABI do lift: r3=op, r4=dst EA, r5=n). O corpo natural (Cell) trata isto como
um op FIOS local -- mas quando o op esta' vinculado a um FO backed pelo F2B
(a camada host que liga FIOS ao ficheiro real via `movie_io`), o caminho
natural nunca consulta o ficheiro: precisa deste early-out para resolver o
mfd (via `movie_io_is` directo em op+0x4, ou via `f2b_fo_mfd_get` da FO) e
po-lo a andar via `ps3_fios_aread_hle`. Quando `got>0`, devolve o resultado
directamente (`ctx->gpr[3]=got; return;`) sem tocar no corpo natural. Quando
`got==0` ou nao ha' mfd (FO genuina do dearch, nao backed por F2B), cai no
corpo natural inalterado -- nao e' um bypass, e' o mesmo early-out que ja'
existia no binario de referencia.

Sem isto, o guest abre o `.m2v` mas nunca recebe bytes de volta do read
assincrono -- e por isso `cellVdecDecodeAu` nunca e' chamado.

Metodo (verbatim, nunca reescrito de memoria)
----------------------------------------------
Confirmado nesta sessao (Fase 10, Task 2):
  - `func_002B3D1C` aparece exactamente 1x em `recomp_macos_v2/ppu_recomp_001.cpp`
    (o lift actual tem 7 chunks, nao os 31 do `pre_v4` -- por isso localiza-se
    a funcao pela ASSINATURA via `resolve_lift_paths`, nunca por nome fixo de
    ficheiro).
  - O fecho do bloco `AREAD-PROBE` ja' presente em `func_002B3D1C` (14 linhas,
    de `recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:23862-23875`) e' BYTE-
    IDENTICO entre o lift de referencia e o lift actual -- serve de ancora.
  - O bloco a inserir (25 linhas) foi extraido VERBATIM de
    `recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:23876-23900` -- nao reescrito.
  - `movie_io_is`, `f2b_fo_mfd_get` e `ps3_fios_aread_hle` ja' estao
    declarados/definidos no MESMO chunk (`ppu_recomp_001.cpp`) pelo preambulo
    instalado por `patch_f2b_multimb_install.py` (confirmado:
    `grep -l "static unsigned f2b_fo_mfd_get" recomp_macos_v2/*.cpp` so'
    devolve `ppu_recomp_001.cpp`, o mesmo chunk de `func_002B3D1C`). Se esse
    preambulo migrar de chunk num re-lift futuro, este patch RECUSA (rc=2)
    em vez de assumir que os simbolos existem no chunk-alvo.

Contrato de rc (mesmo de `patch_fios_stream_guards_install.py` /
`patch_fios_host_pop.py`)
--------------------------------------------------------------------------
  marcador AREAD-HLE ja' presente no corpo de func_002B3D1C -> ALREADY, rc=0
  ancora encontrada exactamente 1x                          -> insere, rc=0
  func_002B3D1C ausente de todos os chunks                  -> rc=2
  func_002B3D1C presente em >1 chunk (ambiguo)               -> rc=2
  ancora ausente ou duplicada dentro do corpo                -> rc=2 (recusa,
                                                                 nao adivinha)
  preambulo F2B (f2b_fo_mfd_get) ausente do MESMO chunk      -> rc=2 (recusa;
                                                                 o bloco
                                                                 chamaria
                                                                 simbolos
                                                                 indefinidos)
Idempotente: 2a corrida = ALREADY.

Uso: python3 recomp_mid_v2/patch_2b3d1c_movie_io.py [LIFT_DIR]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

MARKER = "AREAD-HLE"
FUNC_SIG = "void func_002B3D1C(ppu_context* ctx) {"

# Ancora: fecho verbatim do bloco AREAD-PROBE ja' existente em func_002B3D1C,
# de recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:23862-23875 -- confirmado
# byte-identico ao lift actual (varredura desta sessao).
ANCHOR = (
    "        { static int on=-1; if(on<0){extern char* getenv(const char*);\n"
    "            const char* e=getenv(\"PS3_TRACE_AREAD\"); on=(e&&*e&&*e!='0')?1:0;}\n"
    "          if(on){ static int n=0; if(n++<256){\n"
    "            uint32_t _op=(uint32_t)ctx->gpr[3];\n"
    "            fprintf(stderr,\n"
    "              \"[AREAD] #%d op=0x%08X r4=0x%08X r5=%u\"\n"
    "              \" +00=%08X +04=%08X +08=%08X +0C=%08X\"\n"
    "              \" +10=%08X +14=%08X +18=%08X +1C=%08X\\n\",\n"
    "              n, _op, (uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[5],\n"
    "              _op?vm_read32(_op+0x00):0u, _op?vm_read32(_op+0x04):0u,\n"
    "              _op?vm_read32(_op+0x08):0u, _op?vm_read32(_op+0x0C):0u,\n"
    "              _op?vm_read32(_op+0x10):0u, _op?vm_read32(_op+0x14):0u,\n"
    "              _op?vm_read32(_op+0x18):0u, _op?vm_read32(_op+0x1C):0u);\n"
    "            fflush(stderr); } } }\n"
)

# Bloco a inserir, extraido VERBATIM de
# recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:23876-23900.
BLOCK = (
    "        /* AREAD-HLE: fulfill from movie_io when op is backed by F2B FO\n"
    "         * (FO+0x38 = mfd) or op+0x4 is already a movie_io token. Natural\n"
    "         * dearch FO (m2v) has FO+0x38=0 → decline → natural body. */\n"
    "        { uint32_t _op=(uint32_t)ctx->gpr[3];\n"
    "          uint32_t _dst=(uint32_t)ctx->gpr[4];\n"
    "          uint32_t _n=(uint32_t)ctx->gpr[5];\n"
    "          unsigned _fd=0;\n"
    "          if(_op>=0x10000u && _op<0x4F000000u){\n"
    "            uint32_t _x=vm_read32(_op+0x4u);\n"
    "            if(_x && movie_io_is(_x)) _fd=_x;\n"
    "            else if(_x>=0x10000u && _x<0x4F000000u\n"
    "                    && vm_read32(_x+0x0u)==0x46494F53u){\n"
    "              unsigned _m=f2b_fo_mfd_get(_x);\n"
    "              if(!_m){ uint32_t t=vm_read32(_x+0x38u); if(t&&movie_io_is(t)) _m=t; }\n"
    "              if(_m && movie_io_is(_m)) _fd=_m;\n"
    "            }\n"
    "          }\n"
    "          if(_fd){\n"
    "            unsigned got=ps3_fios_aread_hle(_op,_dst,_n,_fd);\n"
    "            if(got>0u){\n"
    "              ctx->gpr[3]=got;\n"
    "              return;\n"
    "            }\n"
    "          }\n"
    "        }\n"
)


def func_span(text: str, sig: str):
    """(inicio, fim) do corpo da 1a funcao com esta assinatura, ou None."""
    i = text.find(sig)
    if i < 0:
        return None
    j = text.find("\nvoid func_", i + len(sig))
    return (i, len(text) if j < 0 else j)


def has_f2b_preamble(text: str) -> bool:
    return "static unsigned f2b_fo_mfd_get" in text


def patch_one(path: Path) -> str:
    """Devolve um de: skip, already, applied, refuse-anchor, refuse-preamble."""
    text = path.read_text(encoding="utf-8", errors="replace")
    span = func_span(text, FUNC_SIG)
    if span is None:
        return "skip"
    b0, b1 = span
    body = text[b0:b1]
    if MARKER in body:
        return "already"
    if not has_f2b_preamble(text):
        return "refuse-preamble"
    if body.count(ANCHOR) != 1:
        return "refuse-anchor"
    new_body = body.replace(ANCHOR, ANCHOR + BLOCK, 1)
    new_text = text[:b0] + new_body + text[b1:]
    try:
        path.write_text(new_text, encoding="utf-8", newline="\n")
    except TypeError:
        path.write_text(new_text, encoding="utf-8")
    return "applied"


def main() -> int:
    paths = [p for p in resolve_lift_paths(
        sys.argv[1:], str(Path(__file__).resolve().parent.parent / "recomp_macos_v2"))
        if p.is_file()]
    if not paths:
        print("ERRO: nenhum chunk de lift legivel", file=sys.stderr)
        return 2

    hits = [(p, patch_one(p)) for p in paths]
    found = [(p, r) for p, r in hits if r != "skip"]

    if not found:
        print("ERRO: func_002B3D1C ausente de todos os chunks (%s)" %
              ", ".join(p.name for p in paths), file=sys.stderr)
        return 2
    if len(found) > 1:
        print("ERRO: func_002B3D1C presente em >1 chunk (ambiguo): %s" %
              ", ".join(p.name for p, _ in found), file=sys.stderr)
        print("  NADA foi escrito (recusa atomica).", file=sys.stderr)
        return 2

    path, result = found[0]
    if result == "already":
        print("%s: %s ja' presente (ALREADY)" % (path.name, MARKER))
        return 0
    if result == "refuse-preamble":
        print("ERRO: preambulo F2B (f2b_fo_mfd_get) ausente de %s -- "
              "o bloco AREAD-HLE chamaria simbolos indefinidos. "
              "Corre patch_f2b_multimb_install.py primeiro, ou revalida se o "
              "preambulo migrou de chunk." % path.name, file=sys.stderr)
        return 2
    if result == "refuse-anchor":
        print("ERRO: ancora AREAD-PROBE ausente ou duplicada em %s :: func_002B3D1C. "
              "O lifter mudou -- revalida o bloco contra um lift de producao "
              "conhecido-bom e regenera este script." % path.name, file=sys.stderr)
        return 2

    print("%s: %s APPLIED" % (path.name, MARKER))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
