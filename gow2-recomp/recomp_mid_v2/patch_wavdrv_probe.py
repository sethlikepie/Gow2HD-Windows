#!/usr/bin/env python3
"""Probe gated do driver de open do .wav da intro (marcador [WAVDRV]).

CORRECCAO AO BRIEF QUE ORIGINOU ESTA PROBE
------------------------------------------
O brief mandou instrumentar `func_0045FB50` por assumir que o bctr do open do
snd_stream aterra la'. A medicao (patch_snd_open_probe.py, PS3_TRACE_SNDOPEN)
mostra que NAO:

    [SNDOPEN] 00461658 OPEN enter stream=... f110=0x00000000 ramo=A-004618A0
    [SNDOPEN]   00461658 drvA[TOC+0x748] ... code=0x0045FB50   <- so' informativo
    [SNDOPEN] 004618A0 ramoA ...
    [SNDOPEN]   004618A0 open() ... code=0x002B47D4            <- o ALVO REAL

`func_00461658` escolhe o driver por `[sub+0x110] & 2`:
  * bit 0 (SmLogo, sempre)  -> ramo A = func_004618A0 -> bctr por [[TOC+0x750]]
                               -> OPD 0x0052F650 -> code = func_002B47D4
  * bit 1 (nunca visto)     -> ramo inline por [TOC+0x748]
                               -> OPD 0x00533940 -> code = func_0045FB50

Ou seja `func_0045FB50` e' o slot do ramo que o SmLogo NUNCA toma. E `func_0045FB50`
lido a' mao nem sequer abre ficheiros: varre a tabela em [TOC+0x6C0] contando
entradas e escreve book-keeping de slots -- um alocador/registador, nao um open.
Instrumenta-lo nao responderia a pergunta. Esta probe instrumenta o open a
serio, `func_002B47D4`.

O QUE func_002B47D4 FAZ (lido em recomp_macos_v2/ppu_recomp_001.cpp)
-------------------------------------------------------------------
    func_002B47D4(r3=sub, r4=sub+0x104, r6=modo, r7=[sub+0x304])
      buf = r1+0x70
      func_0037750C(buf,0,0x104)      ; memset
      func_003731A0(buf, sub)         ; strcpy(buf, sub)  -- sub+0 = caminho
      func_002B3890(buf)              ; tolower in-place
      r3 = [[TOC-0x1460]+0x118]       ; objecto de fs -- o MESMO que o video usa
      func_0030D578(r3, r4=0, r5=buf, r6=1, r7=sub+0x104)   ; open de MEMBRO
      func_003067DC(open_ret, 0)      ; decodifica/espera o resultado
      if (resultado != 0) -> func_002B4884  ; devolve -2, sai "couldn't open file"
      else return 0                          ; sucesso

=> audio e video PARTILHAM func_0030D578 (o dearchiver do psarc ja' montado) e
   o MESMO objecto de fs [[TOC-0x1460]+0x118]. O video ja' abre o .m2v por aqui
   com sucesso; logo o I/O de membro NAO esta' partido. A unica diferenca e' a
   string do caminho. Esta probe imprime, para CADA open do .wav:
     1. entrada: sub, a string em sub+0, o objecto de fs;
     2. os argumentos exactos passados a func_0030D578 (o buf ja' minusculo);
     3. o retorno de func_0030D578 (0 = sem op/erro);
     4. o retorno de func_003067DC (!=0 => -2 => "couldn't open file").

Assim distingue-se, sem adivinhar, entre "membro nao encontrado" (buf com o wav
mas open devolve erro) e "esgotamento do pool de ops assincronas" (open devolve
0 porque nao ha' op livre -- consequencia a jusante do filme nunca completar).

So' LE memoria do guest; nunca escreve. Com PS3_TRACE_WAVDRV desligada e' um
no-op exacto sobre o baseline (regra 6 do CLAUDE.md do motor). Marcador proprio
[WAVDRV], disjunto de [SNDOPEN]/[FIOSOPEN]/[MOVIEFSM]/[MOVIEOBJ].

Uso:  patch_wavdrv_probe.py [DIR_DE_LIFT]     (default ../recomp_macos_v2)
      PS3_TRACE_WAVDRV=1 ./boot_gow2 EBOOT.ELF 2>&1 | grep WAVDRV
Reaplicado por ../apply_all_patches.sh apos cada re-lift. Idempotente.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARKER = "WAVDRV-PROBE"
ENV = "PS3_TRACE_WAVDRV"
FUNC = "func_002B47D4"

# Helpers no topo do chunk que aloja func_002B47D4. Read-only sobre a memoria do
# guest. Mesma ancora/estilo de patch_snd_open_probe.py.
HELPERS = r'''
/* ===================== WAVDRV-PROBE ====================================
 * Probe gated por PS3_TRACE_WAVDRV. Read-only. Ver
 * recomp_mid_v2/patch_wavdrv_probe.py para a cadeia estatica.
 * ====================================================================== */
static int wavdrv_on(void) {
    static int on = -1;
    if (on < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_WAVDRV");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on;
}
static int wavdrv_cstr(uint32_t ea, char* out, int cap) {
    int i;
    out[0] = 0;
    /* Stacks de threads de servico do snd_stream ficam altas (~0xD0000000);
     * o buf do open vive la'. So' excluir a base e a gama de tags HLE. O
     * vm_read8 tem a sua propria guarda de OOB e devolve 0 em falta. */
    if (ea < 0x00010000u || ea >= 0xF0000000u) return 0;
    for (i = 0; i < cap - 1; i++) {
        unsigned c = (unsigned)vm_read8((uint64_t)(ea + (uint32_t)i)) & 0xFFu;
        if (c == 0) break;
        if (c < 0x20u || c > 0x7Eu) { out[i] = '?'; continue; }
        out[i] = (char)c;
    }
    out[i] = 0;
    return i;
}
'''

ENTRY = (
    "        /* " + MARKER + ": entrada do open real do .wav. r3=sub (caminho em\n"
    "         * sub+0), r6=modo. fsobj=[[TOC-0x1460]+0x118] (== objecto do video). */\n"
    "        if (wavdrv_on()) { static int _wn=0; if(_wn++<12){\n"
    "            uint32_t _sub=(uint32_t)ctx->gpr[3];\n"
    "            uint32_t _toc=(uint32_t)ctx->gpr[2];\n"
    "            uint32_t _r9=(_toc>=0x10000u)?vm_read32((uint64_t)(_toc-0x1460u)):0u;\n"
    "            uint32_t _fs=(_r9>=0x10000u&&_r9<0x4F000000u)?vm_read32((uint64_t)(_r9+0x118u)):0u;\n"
    "            char _s[192]; wavdrv_cstr(_sub,_s,(int)sizeof _s);\n"
    '            fprintf(stderr,"[WAVDRV] 002B47D4 enter #%d sub=0x%08X sub0=\'%s\''
    ' r4=0x%08X r6=%d r7=0x%08X fsobj=0x%08X\\n",\n'
    "                _wn,_sub,_s,(uint32_t)ctx->gpr[4],(int)(int32_t)ctx->gpr[6],\n"
    "                (uint32_t)ctx->gpr[7],_fs);\n"
    "            fflush(stderr);\n"
    "        } }\n"
)

# Inserida ENTRE `r3 = [r9+0x118]` e a chamada a func_0030D578: r3=fsobj, r5=buf.
PRE = (
    "        /* " + MARKER + ": argumentos exactos do open de membro. */\n"
    "        if (wavdrv_on()) { static int _wn=0; if(_wn++<12){\n"
    "            uint32_t _buf=(uint32_t)ctx->gpr[5];\n"
    "            char _s[192]; wavdrv_cstr(_buf,_s,(int)sizeof _s);\n"
    '            fprintf(stderr,"[WAVDRV] 002B47D4 ->0030D578 #%d fsobj=0x%08X'
    ' buf=0x%08X path=\'%s\' r6=%d out=0x%08X\\n",\n'
    "                _wn,(uint32_t)ctx->gpr[3],_buf,_s,(int)(int32_t)ctx->gpr[6],\n"
    "                (uint32_t)ctx->gpr[7]);\n"
    "            fflush(stderr);\n"
    "        } }\n"
)

# Logo apos func_0030D578: r3 = op handle devolvido (0 = sem op/erro).
POST_OPEN = (
    "        /* " + MARKER + ": retorno do open de membro (op handle). */\n"
    "        if (wavdrv_on()) { static int _wn=0; if(_wn++<12){\n"
    '            fprintf(stderr,"[WAVDRV] 002B47D4 0030D578ret #%d op=0x%08X%s\\n",\n'
    "                _wn,(uint32_t)ctx->gpr[3],\n"
    '                ((uint32_t)ctx->gpr[3]==0u)?"  <-- SEM OP/ERRO":"");\n'
    "            fflush(stderr);\n"
    "        } }\n"
)

# Logo apos func_003067DC (antes de r3 ser zerado): r3 = resultado decodificado.
POST_DEC = (
    "        /* " + MARKER + ": resultado decodificado. !=0 => devolve -2 e sai\n"
    "         * a mensagem \"couldn't open file\"; ==0 => open OK. */\n"
    "        if (wavdrv_on()) { static int _wn=0; if(_wn++<12){\n"
    "            uint32_t _rc=(uint32_t)ctx->gpr[3];\n"
    '            fprintf(stderr,"[WAVDRV] 002B47D4 003067DCret #%d rc=0x%08X -> %s\\n",\n'
    '                _wn,_rc,(_rc!=0u)?"FAIL(-2) couldn\'t open":"OK(0)");\n'
    "            fflush(stderr);\n"
    "        } }\n"
)

SIG = "void " + FUNC + "(ppu_context* ctx) {\n"

# Alvos DENTRO da regiao de func_002B47D4 (a ordem/linhas do lift podem mudar,
# por isso o alvo e' textual sobre a regiao, nunca por numero de linha).
#
# REPARACAO 2026-07-25 (re-lift com lifter novo): shape-LR. O lifter passou a
# emitir `ctx->lr = 0x002B4844; ` imediatamente antes de `func_0030D578(ctx);`
# (e `ctx->lr = 0x002B4854; ` antes de `func_003067DC(ctx);`), o que partia as
# agulhas literais. As agulhas passam a regex com o prefixo de LR OPCIONAL, para
# casarem com o lift antigo (sem prefixo) E com o novo; a linha da chamada e'
# reemitida tal e qual foi encontrada (grupo 1), sem inventar o valor de LR.
_LR = r"(?:ctx->lr = 0x[0-9A-Fa-f]+; )?"

OPEN_LOAD = "        ctx->gpr[3] = vm_read32(ctx->gpr[9] + 0x118);\n"
NOP = "        /* nop */;\n"

OPEN_RE = re.compile(
    re.escape(OPEN_LOAD)
    + r"(        " + _LR + r"func_0030D578\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n)"
    + re.escape(NOP)
)


def open_repl(m: "re.Match[str]") -> str:
    return OPEN_LOAD + PRE + m.group(1) + POST_OPEN + NOP


DEC_RE = re.compile(
    r"(        " + _LR + r"func_003067DC\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n)"
    + re.escape(NOP)
)


def dec_repl(m: "re.Match[str]") -> str:
    return m.group(1) + POST_DEC + NOP

HELPER_NEEDLES = ("#include <stdlib.h>\n", "#include <math.h>\n")


def patch_file(path: Path) -> str:
    src = path.read_text(encoding="utf-8", errors="replace")
    if SIG not in src:
        return "SKIP"
    if MARKER in src:
        return "ALREADY"

    # 1) helpers no topo
    needle = next((n for n in HELPER_NEEDLES if n in src[:4000]), None)
    if needle is None:
        raise SystemExit(
            f"{path.name}: sem ancora {HELPER_NEEDLES!r} no preambulo -- o shape "
            "do lift mudou; reveja a probe antes de forcar"
        )
    src = src.replace(needle, needle + HELPERS, 1)

    # 2) instrumentar SO' a regiao de func_002B47D4
    i = src.find(SIG)
    j = src.find("\nvoid func_", i + len(SIG))
    region = src[i:j] if j > i else src[i:]

    r = region.replace(SIG, SIG + ENTRY, 1)
    if r == region:
        raise SystemExit(f"{path.name}: falhou insercao da entrada em {FUNC}")
    region = r

    region, n_open = OPEN_RE.subn(open_repl, region, count=1)
    if not n_open:
        raise SystemExit(
            f"{path.name}: needle do open (func_0030D578) ausente em {FUNC} -- "
            "a cadeia mudou; reveja a probe"
        )

    region, n_dec = DEC_RE.subn(dec_repl, region, count=1)
    if not n_dec:
        raise SystemExit(
            f"{path.name}: needle do decode (func_003067DC) ausente em {FUNC} -- "
            "a cadeia mudou; reveja a probe"
        )

    src = src[:i] + region + (src[j:] if j > i else "")
    path.write_text(src, encoding="utf-8", newline="\n")
    return "APPLIED"


def main() -> int:
    # chunk-fixo: o numero de chunks deixou de ser estavel (31 -> 7 no lifter
    # novo) e o argumento pode ser um DIRECTORIO ou um ficheiro; resolve_lift_paths
    # trata os dois casos e expande o directorio para todos os ppu_recomp_*.cpp.
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else \
        Path(__file__).resolve().parent.parent / "recomp_macos_v2"
    chunks = [p for p in resolve_lift_paths(sys.argv[1:], str(root)) if p.exists()]
    if not chunks:
        print(f"FAIL: nenhum ppu_recomp_*.cpp em {root}", file=sys.stderr)
        return 1
    any_app = False
    already = False
    for p in chunks:
        r = patch_file(p)
        if r != "SKIP":
            print(f"{p.name}: {r}")
        if r == "APPLIED":
            any_app = True
        if r == "ALREADY":
            already = True
    if not any_app and not already:
        print(f"FAIL: {FUNC} ausente no lift em {root}", file=sys.stderr)
        return 1
    print(f"probe {MARKER} pronta -- corra com {ENV}=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
