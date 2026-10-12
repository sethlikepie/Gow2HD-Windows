#!/usr/bin/env python3
"""FIOS/asset: fix func_0042A1D4 empty-count mid-function jump (R_PermA wall).

Measured 2026-07-21 (Mac arm64 GoW2):
  After R_LglScA F2B open+DONE, guest hits func_0042A0C8 with count byte
  at obj+0x80 signed-negative (empty table). O caminho vai a
  func_0042A1D4 que poe r28=0 e salta para o MEIO de func_0042A118
  (walk da lista em r28+0x38). Com r28=0 isso percorre a EA absoluta
  0x38 -> lixo -> UNCOMMITTED read32 0x676C7367 ("glsg" bytes de path/TOC
  interpretados como ponteiro). O guest nunca chega ao open do R_PermA.

  O caminho de tabela vazia tem de tomar o epilogo de 0042A0C8/0042A118
  (r28=0 -> ret r3=4), nao o corpo do walk.

In-boot after fix (/tmp/vdec_emptyfix.log ~55s):
  42A1D4-EMPTY skip -> 002B4340 nome='R_PermA' -> F2B-MOVIEIO 20169344
  -> DONE #3. R_LglScA still GREEN. UNCOMMITTED 0x676C7367 gone.

NOTA DE HONESTIDADE (verificado 2026-07-25 desassemblando o EBOOT):
  0x0042A1E0 e' mesmo `b 0x0042A118` (4BFFFF38) e 0x0042A1D8 e' mesmo
  `li r28,0`. Ou seja, o lifter NAO errou o alvo aqui: este patch e' um
  OVERRIDE de comportamento com evidencia in-boot, nao uma correccao de
  forma do lifter. A causa raiz a montante (porque e' que o byte de
  contagem em obj+0x80 chega negativo) continua em aberto. Mantido tal e
  qual estava commitado; so' a agulha foi actualizada.

Markers: FIOS-42A1D4-EMPTY
Idempotent. Lift is gitignored -- re-run after re-lift.

Usage:
  python3 recomp_mid_v2/patch_fios_42a1d4_empty.py [DIR_DE_LIFT]

CORRECCAO 2026-07-25 (shape-outro + chunk-fixo)
-----------------------------------------------
1) A agulha era o corpo LITERAL de func_0042A1D4. O lifter actual mudou
   duas coisas nesse corpo:
     - o addi passou de "(int64_t)(int32_t)(ctx->gpr[9] + -1)" para
       "ctx->gpr[9] + (int64_t)(-1)";
     - desapareceu a segunda linha morta
       "{ g_trampoline_fn = (void(*)(void*))func_0042A1E4; return; }"
       (fallthrough inalcancavel que o lifter ja' nao emite).
   Passa a usar-se regex tolerante que casa as duas formas.
2) chunk-fixo: a procura era por uma lista fixa de nomes de chunk
   ("003","001","005"). Passa a varrer todos os chunks via
   resolve_lift_paths().
3) shape-LR: o epilogo inlined passa a carimbar ctx->lr antes de cada
   chamada (0x0042A19C / 0x0042A1A4), exactamente como o lifter actual
   faz no mesmo sitio guest (loc_0042A198). Os callee-save continuam a
   ser lidos da pilha guest (o lifter novo usa locais _cs_NN cacheados a'
   entrada de func_0042A118, que nao existem aqui; os offsets sao os
   mesmos: r27..r31 em 0x78/0x80/0x88/0x90/0x98 e lr em 0xB0).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARKER = "FIOS-42A1D4-EMPTY"

# Corpo de func_0042A1D4 em qualquer das formas emitidas pelo lifter.
OLD_RE = re.compile(
    r"void func_0042A1D4\(ppu_context\* ctx\) \{[ \t]*\n"
    r"[ \t]*ctx->gpr\[0\][ \t]*=[ \t]*"
    r"(?:\(int64_t\)\(int32_t\)\(ctx->gpr\[9\][ \t]*\+[ \t]*-1\)"
    r"|ctx->gpr\[9\][ \t]*\+[ \t]*\(int64_t\)\(-1\));[ \t]*\n"
    r"[ \t]*ctx->gpr\[28\][ \t]*=[ \t]*\(int64_t\)\(int32_t\)\(0\);[ \t]*\n"
    r"[ \t]*vm_write8\(ctx->gpr\[3\][ \t]*\+[ \t]*0x80,[ \t]*ctx->gpr\[0\]\);[ \t]*\n"
    r"[ \t]*\{[ \t]*g_trampoline_fn[ \t]*=[ \t]*\(void\(\*\)\(void\*\)\)func_0042A118;[ \t]*return;[ \t]*\}[ \t]*\n"
    r"(?:[ \t]*\{[ \t]*g_trampoline_fn[ \t]*=[ \t]*\(void\(\*\)\(void\*\)\)func_0042A1E4;[ \t]*return;[ \t]*\}[ \t]*\n)?"
    r"\}"
)

NEW = """void func_0042A1D4(ppu_context* ctx) {
        /* FIOS-42A1D4-EMPTY: count byte at obj+0x80 is signed-negative
         * (empty table). Intencao = saltar o walk da lista e tomar o
         * epilogo de 0042A0C8/0042A118 com r28=0 (ret r3=r28+4=4).
         * Sem isto vai-se para o meio de 0042A118 que percorre r28+0x38
         * com r28=0 -> EA absoluta 0x38 -> lixo -> UNCOMMITTED 0x676C7367.
         * Always-on correctness fix; log when PS3_TRACE_FIOSOPEN=1. */
        ctx->gpr[0] = ctx->gpr[9] + (int64_t)(-1);
        ctx->gpr[28] = (int64_t)(int32_t)(0);
        vm_write8(ctx->gpr[3] + 0x80, ctx->gpr[0]);
        { static int _on=-1; if(_on<0){const char* e=getenv("PS3_TRACE_FIOSOPEN");
            _on=(e&&*e&&*e!='0')?1:0;}
          if(_on){ static int _n=0; if(_n++<4){
            fprintf(stderr,"[FIOSOPEN] 42A1D4-EMPTY skip list walk r3=0x%08X count_was=%d\\n",
              (uint32_t)ctx->gpr[3], (int)(int8_t)(uint8_t)ctx->gpr[9]);
            fflush(stderr); } } }
        /* Epilogue of 0042A118 (loc_0042A198) -- guest frame already live
         * from 0042A0C8 entry before the branch. ctx->lr carimbado como o
         * lifter actual faz nas mesmas duas chamadas. */
        ctx->lr = 0x0042A19C; func_002AAB84(ctx); DRAIN_TRAMPOLINE(ctx);
        ctx->lr = 0x0042A1A4; func_00262C8C(ctx); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0xB0);
        ctx->gpr[27] = vm_read64(ctx->gpr[1] + 0x78);
        ctx->gpr[3] = ctx->gpr[28] + (int64_t)(4);
        ctx->gpr[29] = vm_read64(ctx->gpr[1] + 0x88);
        ctx->gpr[28] = vm_read64(ctx->gpr[1] + 0x80);
        ctx->lr = ctx->gpr[0];
        ctx->gpr[3] = ppc_rldicl(ctx->gpr[3], 0, 32);
        ctx->gpr[30] = vm_read64(ctx->gpr[1] + 0x90);
        ctx->gpr[31] = vm_read64(ctx->gpr[1] + 0x98);
        ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0xA0);
        return;
}"""


def main() -> int:
    paths = resolve_lift_paths(
        sys.argv[1:],
        str(Path(__file__).resolve().parent.parent / "recomp_macos_v2"),
    )
    seen = False
    for target in paths:
        if not target.is_file():
            continue
        text = target.read_text(errors="replace")
        if MARKER in text:
            print(f"{target.name}: {MARKER} already applied")
            return 0
        m = OLD_RE.search(text)
        if not m:
            if "void func_0042A1D4" in text:
                seen = True
            continue
        target.write_text(text[: m.start()] + NEW + text[m.end():])
        print(f"{target.name}: applied {MARKER}")
        return 0
    if seen:
        print("expected 0042A1D4 body not found (lift drifted?)")
        return 2
    print("func_0042A1D4 not found in lift")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
