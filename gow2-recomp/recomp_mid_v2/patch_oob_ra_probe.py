#!/usr/bin/env python3
"""Probe de argumentos nos dois sitios que fazem [vm] OOB access em 0xFFFF90D4.

Porque existe
-------------
Com a pipeline de assets parada, o boot faz `[vm] OOB access 0xFFFF90D4` (= 0 -
0x6F2C) logo a seguir a arrancarem as threads do FIOS, vindo de func_00380124
(leitura) e func_00372740 (escrita). Duas hipoteses foram REFUTADAS por medicao:

  - stack mau  -> todas as threads tem stack valida (0xD0001000+, r1 no topo-48);
  - TOC mau    -> r2 e resolvido correctamente do OPD (0x00541178).

Logo o ponteiro mau nao e relativo a r1 nem a r2: chega ja podre como ARGUMENTO.
Falta saber QUAL registo o traz e com que companhia. Os enderecos de retorno do
host nao servem (sao quadros de trampolim -- o CLAUDE.md avisa, e confirmou-se:
func_0037401C nem sequer chama func_00380124 directamente).

O que faz
---------
Insere, no topo de cada uma das duas funcoes, um dump de r3..r10 (+ r1/r2/lr)
que so dispara quando algum dos argumentos cai na banda podre (>= 0xFFF00000).
Auto-filtrante: em execucao sa nao imprime nada.

Aviso sobre `lr`: o lifter NAO escreve ctx->lr nas chamadas directas (nao ha um
unico `ctx->lr = 0x...` no lift), portanto o lr impresso e ESTALE -- serve como
pista fraca, nunca como identificacao do chamador. Os chamadores tem de sair de
fluxo estatico (grep das call sites), como manda o CLAUDE.md.

Gated por PS3_TRACE_OOBARG, OFF por default (no-op no baseline).

Alvos: ppu_recomp_001.cpp (func_00380124), ppu_recomp_002.cpp (func_00372740).
Ambas as edicoes sao FUNCTION-SCOPED (fatiam a regiao da funcao antes de
substituir) para nao colidirem com outros patches no mesmo chunk.
"""
from pathlib import Path
import sys

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent

MARKER = "[OOBARG]"

# (chunk, funcao, funcao seguinte usada so para delimitar a regiao)
TARGETS = [
    ("ppu_recomp_001.cpp", "func_00380124"),
    ("ppu_recomp_002.cpp", "func_00372740"),
]


def probe_block(fname: str) -> str:
    """Bloco C a inserir. Sem re.sub em lado nenhum: substituicao literal."""
    return (
        '        { static int on=-1; if(on<0){extern char* getenv(const char*); '
        'on=getenv("PS3_TRACE_OOBARG")?1:0;}\n'
        '          if(on){ uint32_t oa[8]; int ok, obad=0;\n'
        '            for(ok=0;ok<8;ok++){ oa[ok]=(uint32_t)ctx->gpr[3+ok]; '
        'if(oa[ok]>=0xFFF00000u) obad=1; }\n'
        '            if(obad){ static int on_n=0; if(on_n++<24){\n'
        '              fprintf(stderr,"[OOBARG] ' + fname + ' r3=0x%08X r4=0x%08X r5=0x%08X '
        'r6=0x%08X r7=0x%08X r8=0x%08X r9=0x%08X r10=0x%08X | r1=0x%08X r2=0x%08X lr=0x%08X\\n",\n'
        '                oa[0],oa[1],oa[2],oa[3],oa[4],oa[5],oa[6],oa[7],\n'
        '                (uint32_t)ctx->gpr[1],(uint32_t)ctx->gpr[2],(uint32_t)ctx->lr);\n'
        '              fflush(stderr);} } } }\n'
    )


def patch_one(chunk: str, fname: str) -> str:
    p = ROOT / chunk
    if not p.is_file():
        raise SystemExit("chunk ausente: %s" % p)
    s = p.read_text(encoding="utf-8", errors="replace")

    head = "void %s(ppu_context* ctx) {\n" % fname
    i = s.find(head)
    if i < 0:
        raise SystemExit("needle ausente: %s em %s (shape do lift mudou?)" % (fname, chunk))

    # Regiao = da abertura desta funcao ate a proxima definicao de funcao.
    j = s.find("\nvoid func_", i + len(head))
    if j < 0:
        j = len(s)
    region = s[i:j]

    if MARKER in region:
        return "ALREADY %s (%s)" % (fname, chunk)

    new_region = region.replace(head, head + probe_block(fname), 1)
    if new_region == region:
        raise SystemExit("falhou a inserir em %s (%s)" % (fname, chunk))

    s = s[:i] + new_region + s[j:]
    p.write_text(s, encoding="utf-8", newline="\n")
    return "PATCHED %s (%s)" % (fname, chunk)


results = [patch_one(chunk, fname) for chunk, fname in TARGETS]
for r in results:
    print("  " + r)
print("OK patch_oob_ra_probe")
