#!/usr/bin/env python3
"""Task 3 (Boot N1 discriminador, 2026-07-21): instala os dois marcadores que o
script de contagem do plano espera literalmente ([LDRSH], [SHADERSRC] N=) e que
nenhum patch anterior instala com esse nome exacto.

  - [LDRSH] entry probe em func_0032109C (corpo do ICGLdrShader, tag
    0x283F6879 -- ver anchors confirmadas em ppu_recomp_001.cpp:136228).
    Sinaliza "o corpo da instancia ICGLdrShader correu de facto". O
    comentario do runtime em ppu_loader.cpp:2077 ("LDRSH=0") refere-se a
    esta funcao. Gated por PS3_TRACE_LDRSH (fallback PS3_TRACE_TYMAP, mesma
    convencao dos outros probes deste plano).

  - [SHADERSRC] N=<n> em func_003CC208 (defs SHADERSRC -- anchor confirmada
    em ppu_recomp_001.cpp:291118), logo apos o campo de contagem de 4 bytes
    ser lido do stream via func_001856A8 (r3=stream, r4=&local, r5=4) para
    ctx->gpr[1]+0x70 -> ctx->gpr[25]. E o mesmo valor que o probe (retirado)
    do lado Windows reportava -- ver smoke_asset_pipeline.sh: "N=-1" =
    nenhum shader encontrado, "N>=0" = contagem real. Gated por
    PS3_TRACE_SHADERSRC (fallback PS3_TRACE_TYMAP).

Nota de confianca (RE estatica, nao documentacao oficial): gpr[25] e lido
como u32 logo no entry do stream e depois usado como limite de um loop
(comparado contra um contador gpr[28] que incrementa de 0 em 1) -- e
consistente com "N = numero de records declarados no header do stream",
mas fica sujeito a confirmacao pelos valores reais observados no boot N1
(se N vier sempre um inteiro pequeno ou -1, confirma; se vier lixo, ver
nota no relatorio).

Ambos sao diagnosticos puros em stderr (nao alteram control-flow),
idempotentes, e OFF por default (sem env = no-op), regra 6 do CLAUDE.md.

Correccao 2026-07-25 (relift): as duas agulhas eram literais e partiram-se
com tres mudancas de forma do lifter, nenhuma delas semantica:
  a) shape-callee-save -- func_0032109C passou a abrir com
     "uint64_t _cs_27 = ctx->gpr[27];" (x5) antes do prologo, por isso a
     agulha "header + vm_write64(...-0xB0...)" deixou de casar. Deixamos de
     reescrever o header: o probe e' inserido IMEDIATAMENTE ANTES da linha
     de prologo, seja qual for o que a precede.
  b) shape-LR -- as chamadas passaram a ter prefixo "ctx->lr = 0x003CC26C;"
     colado ao "func_001856A8(ctx);", partindo a agulha do SHADERSRC. A
     regex torna esse prefixo opcional.
  c) chunk-fixo -- ROOT/"ppu_recomp_001.cpp" fixo; o lifter passou de 31 para
     7 chunks e o apply_all_patches.sh passa um DIRECTORIO. Passa a usar
     resolve_lift_paths() e a procurar as funcoes em qualquer chunk.
Todas as agulhas sao tolerantes a espacos/indentacao, para casarem com o lift
antigo E com o novo.
"""
from pathlib import Path
import re
import sys

from lift_paths import resolve_lift_paths


def _flex(literal: str) -> str:
    """Literal -> regex tolerante a variacoes de espacos/indentacao/quebras."""
    return r"\s*".join(re.escape(tok) for tok in literal.split())


LDRSH_FN_RE = re.compile(r"void\s+func_0032109C\s*\(\s*ppu_context\s*\*\s*ctx\s*\)\s*\{")
# Ponto de insercao: a linha de prologo da propria func_0032109C.
LDRSH_ANCHOR_RE = re.compile(
    r"([ \t]*)"
    + _flex("vm_write64(ctx->gpr[1] + -0xB0, ctx->gpr[1]); ctx->gpr[1] += -0xB0;")
)

LDRSH_PROBE = '''{ static int on=-1; if(on<0){extern char* getenv(const char*); on=(getenv("PS3_TRACE_LDRSH")||getenv("PS3_TRACE_TYMAP"))?1:0;}
          if(on){ static int n=0; if(n++<32)
            fprintf(stderr,"[LDRSH] #%d entry r3=0x%08X r4=0x%08X\\n", n,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[4]); fflush(stderr);} }
'''

SRC_FN_RE = re.compile(r"void\s+func_003CC208\s*\(\s*ppu_context\s*\*\s*ctx\s*\)\s*\{")
# Leitura do campo de contagem do stream; "ctx->lr = 0x...;" e' o prefixo novo
# (shape-LR) e por isso opcional.
SRC_RE = re.compile(
    r"(?:ctx->lr\s*=\s*0x[0-9A-Fa-f]+;\s*)?"
    + _flex("func_001856A8(ctx); DRAIN_TRAMPOLINE(ctx);")
    + r"\s*"
    + _flex("/* nop */;")
    + r"\s*"
    + _flex("ctx->gpr[25] = vm_read32(ctx->gpr[1] + 0x70);")
    + r"(?=\s*"
    + _flex("ctx->gpr[0] = (int64_t)(int32_t)((uint32_t)0x4EC << 16);")
    + r"\s*"
    + _flex("ctx->gpr[30] = ppc_rldicl(ctx->gpr[26], 0, 32);")
    + r"\s*"
    + _flex("ctx->gpr[0] = ctx->gpr[0] | 0x4EC4;")
    + r")"
)

SRC_PROBE = '''
        { static int on=-1; if(on<0){extern char* getenv(const char*); on=(getenv("PS3_TRACE_SHADERSRC")||getenv("PS3_TRACE_TYMAP"))?1:0;}
          if(on){ static int n=0; if(n++<32)
            fprintf(stderr,"[SHADERSRC] #%d N=%d obj=0x%08X\\n", n,(int32_t)ctx->gpr[25],(uint32_t)ctx->gpr[26]); fflush(stderr);} }'''


def _apply_ldrsh(s: str) -> tuple[str, str]:
    if "[LDRSH]" in s:
        return s, "LDRSH already present"
    fn = LDRSH_FN_RE.search(s)
    if not fn:
        return s, ""  # nao vive neste chunk
    m = LDRSH_ANCHOR_RE.search(s, fn.end())
    if not m:
        raise SystemExit("0032109C: prologo -0xB0 nao encontrado (lift shape changed?)")
    indent = m.group(1)
    return s[: m.start()] + indent + LDRSH_PROBE + s[m.start():], "LDRSH entry probe added"


def _apply_shadersrc(s: str) -> tuple[str, str]:
    if "[SHADERSRC]" in s:
        return s, "SHADERSRC already present"
    fn = SRC_FN_RE.search(s)
    if not fn:
        return s, ""  # nao vive neste chunk
    hits = list(SRC_RE.finditer(s, fn.end()))
    if len(hits) < 1:
        raise SystemExit("003CC208 count-read needle nao encontrada (lift shape changed?)")
    m = hits[0]
    return s[: m.end()] + SRC_PROBE + s[m.end():], "SHADERSRC N= probe added"


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2/ppu_recomp_001.cpp")
    rc = 0
    done = {"LDRSH": False, "SHADERSRC": False}
    for p in paths:
        if not p.exists():
            print(f"skip {p} (inexistente)")
            continue
        s = orig = p.read_text(encoding="utf-8", errors="replace")
        for key, fn in (("LDRSH", _apply_ldrsh), ("SHADERSRC", _apply_shadersrc)):
            try:
                s, msg = fn(s)
            except SystemExit as e:
                print(f"FAILED {p}: {e}")
                rc = 1
                continue
            if msg:
                done[key] = True
                print(f"{p.name}: {msg}")
        if s != orig:
            p.write_text(s, encoding="utf-8", newline="\n")
    missing = [k for k, v in done.items() if not v]
    if missing:
        print(f"FAILED: alvos ausentes de todos os chunks: {missing}")
        rc = 1
    print("OK patch_ldrsh_shadersrc_entry" if rc == 0 else "NOK patch_ldrsh_shadersrc_entry")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
