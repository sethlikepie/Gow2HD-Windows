#!/usr/bin/env python3
"""Task 4: materialize PPC switch jump-table em func_002B11B8.

O lifter deixava a tabela do switch como /* TODO: .word */ e despachava por
TOC-0x151C + bctr. Depois do R_PermA streamar por inteiro, esse caminho da'
ICALL-BAD ctr=0x27182818 e os type loaders (ICGLdrShader) nunca correm.

Fix fiel: usar os 28 offsets originais com base guest 0x2B1228 (todos os
alvos sao casos registados). Idempotente.

CORRECCAO 2026-07-25 (shape-outro + chunk-fixo)
-----------------------------------------------
1) chunk-fixo: abria "ppu_recomp_001.cpp" por nome. O lifter passou de 31
   para 7 chunks e a funcao pode migrar; agora usa resolve_lift_paths(), que
   aceita um DIRECTORIO e varre todos os chunks.
2) shape-outro (agulha literal gigante): a agulha antiga incluia o
   "ps3_indirect_call(ctx); return;" e as 28 linhas "/* TODO: .word ... */".
   No lifter actual o bctr JA' e' resolvido para um
   "switch ((uint32_t)ctx->ctr) { case 0x002B12C4u: goto loc_002B12C4; ... }"
   com 22 alvos, e o cast do rlwinm passou de (uint32_t) para (uint64_t).
   Por isso a agulha nunca casava.
   A substituicao passa a ser MINIMA e ciru'rgica: troca-se apenas o calculo
   do CTR (as 6 linhas que leem a tabela da memoria guest) e deixa-se intacto
   o despacho que vier a seguir -- no lift antigo o ps3_indirect_call, no lift
   novo o switch de gotos locais. Assim o patch casa nas duas formas.

Verificacao da base 0x2B1228 contra o lift novo (nao e' chute): base + cada
offset da tabela da' exactamente os 22 alvos distintos que o proprio lifter
enumerou no switch (0x9C->0x2B12C4, 0xE4->0x2B130C, ... 0x70->0x2B1298).

Os registos r11/r9/r0 continuam a ser escritos com os mesmos valores que o
console teria (r11 = base da tabela, r9 = (ty*4)&0x3FFC, r0 = alvo), para nao
divergir do PPC original no estado visivel a jusante.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARKER = "Task4 FIX: PPC switch jump-table"

# Agulha tolerante: so' o calculo do CTR a partir da tabela em memoria guest.
# Tolera (uint32_t) vs (uint64_t) no rlwinm e espacos variaveis.
OLD_RE = re.compile(
    r"[ \t]*ctx->gpr\[11\][ \t]*=[ \t]*vm_read32\(ctx->gpr\[2\][ \t]*\+[ \t]*-0x151C\);[ \t]*\n"
    r"[ \t]*ctx->gpr\[9\][ \t]*=[ \t]*\((?:uint32_t|uint64_t)\)ppc_rlwinm\(\(uint32_t\)ctx->gpr\[0\],[ \t]*2,[ \t]*18,[ \t]*29\);[ \t]*\n"
    r"[ \t]*ctx->gpr\[0\][ \t]*=[ \t]*vm_read32\(\(ctx->gpr\[9\][ \t]*\+[ \t]*ctx->gpr\[11\]\)\);[ \t]*\n"
    r"[ \t]*ctx->gpr\[0\][ \t]*=[ \t]*\(int64_t\)\(int32_t\)ctx->gpr\[0\];[ \t]*\n"
    r"[ \t]*ctx->gpr\[0\][ \t]*=[ \t]*ctx->gpr\[0\][ \t]*\+[ \t]*ctx->gpr\[11\];[ \t]*\n"
    r"[ \t]*ctx->ctr[ \t]*=[ \t]*\(uint32_t\)ctx->gpr\[0\];[ \t]*\n"
)

NEW = """        /* Task4 FIX: PPC switch jump-table — o lifter deixou a tabela como
         * comentarios TODO .word e o despacho dependia de ler TOC-0x151C da
         * memoria guest, o que logo apos o R_PermA da' CTR lixo (ICALL-BAD
         * 0x27182818). Materializa-se a tabela original (base guest 0x2B1228
         * = primeiro .word) e calcula-se o alvo pelo tipo. O despacho a
         * seguir (switch de gotos locais no lifter novo, ps3_indirect_call no
         * antigo) fica intacto e usa este ctx->ctr. Tipos 0..0x1B inclusive. */
        {
          static const uint32_t k_jt[0x1C] = {
            0x0000009C, 0x000000E4, 0x00000114, 0x00000144,
            0x00000354, 0x00000384, 0x00000434, 0x00000638,
            0x0000083C, 0x0000086C, 0x00000A7C, 0x00000B1C,
            0x00000BC8, 0x00000BF8, 0x0000009C, 0x0000009C,
            0x00000C28, 0x0000009C, 0x00000C58, 0x00000C88,
            0x00000CB8, 0x00000CE8, 0x00000D18, 0x0000009C,
            0x0000009C, 0x00000F48, 0x0000009C, 0x00000070
          };
          const uint32_t ty = (uint32_t)ctx->gpr[0];
          const uint32_t base = 0x002B1228u;
          const uint32_t tgt = base + k_jt[ty < 0x1Cu ? ty : 0];
          { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYDISP")?1:0;}
            if(on){ static int n=0; if(n++<40)
              fprintf(stderr,"[TYDISP] type=%u -> 0x%08X\\n", ty, tgt); } }
          /* mesmo estado de registos que o PPC original deixaria */
          ctx->gpr[11] = (uint64_t)base;
          ctx->gpr[9]  = (uint64_t)((ty << 2) & 0x3FFCu);
          ctx->gpr[0]  = (int64_t)(int32_t)tgt;
          ctx->ctr = tgt;
        }
"""


def main() -> int:
    paths = resolve_lift_paths(
        sys.argv[1:],
        str(Path(__file__).resolve().parent.parent / "recomp_macos_v2"),
    )
    if not paths:
        print("FAIL: nenhum ficheiro de lift", file=sys.stderr)
        return 1
    already = False
    for path in paths:
        if not path.is_file():
            print(f"skip {path}")
            continue
        src = path.read_text(encoding="utf-8", errors="replace")
        if MARKER in src:
            print(f"OK: already patched ({path.name})")
            already = True
            continue
        m = OLD_RE.search(src)
        if not m:
            continue
        path.write_text(src[: m.start()] + NEW + src[m.end():],
                        encoding="utf-8", newline="\n")
        print(f"OK: patched {path}")
        return 0
    if already:
        return 0
    print("FAIL: needle not found", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
