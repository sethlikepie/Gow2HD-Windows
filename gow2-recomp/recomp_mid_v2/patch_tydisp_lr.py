#!/usr/bin/env python3
"""Add LR to TYDISP logs so post-WAD callers are identifiable.

Correccao 2026-07-25 (relift):
  1) chunk-fixo -- abria sempre ROOT/"ppu_recomp_001.cpp". O lifter passou de
     31 para 7 chunks e o argumento do apply_all_patches.sh e' um DIRECTORIO,
     por isso isto rebentava com IsADirectoryError / ficheiro errado. Passa a
     usar resolve_lift_paths(), que expande o directorio para todos os chunks.
  2) agulhas literais -- viraram regex tolerantes a espacos/indentacao, para
     casarem com o lift antigo E com o novo (e com o probe base tal como o
     patch_jumptable_2b11b8.py o venha a reinstalar sobre a shape nova).
  3) pre-requisito explicito -- este script NAO cria o probe [TYDISP]; so' lhe
     acrescenta lr=. Quem o instala e' o patch_jumptable_2b11b8.py (e o
     patch_tymap_probes2.py enriquece-o). Sem esse probe no lift nao ha' nada
     para enriquecer: em vez de "needle missing" com rc=1 (indistinguivel de
     erro real), imprime SKIP declarado e nao-fatal, como ja' faz o
     patch_tymap_probes2.py na situacao simetrica. O check NAO foi enfraquecido:
     quando o probe existe, a ausencia da agulha continua a ser FAILED/rc=1.
"""
from pathlib import Path
import re
import sys

from lift_paths import resolve_lift_paths


def _flex(literal: str) -> str:
    """Literal -> regex tolerante a variacoes de espacos/indentacao/quebras."""
    return r"\s*".join(re.escape(tok) for tok in literal.split())


# Forma enriquecida (patch_tymap_probes2.py ja' correu): acrescenta lr= no fim.
OLD_RICH = '''fprintf(stderr,"[TYDISP] type=%u -> 0x%08X desc=0x%08X raw=0x%08X w1=0x%08X w2=0x%08X r3=0x%08X\\n",
                ty, tgt, (uint32_t)ctx->gpr[4], raw, w1, w2, (uint32_t)ctx->gpr[3]); } } }'''
NEW_RICH = '''fprintf(stderr,"[TYDISP] type=%u -> 0x%08X desc=0x%08X raw=0x%08X w1=0x%08X w2=0x%08X r3=0x%08X lr=0x%08X\\n",
                ty, tgt, (uint32_t)ctx->gpr[4], raw, w1, w2, (uint32_t)ctx->gpr[3], (uint32_t)ctx->lr); } } }'''

# Forma simples (so' o patch_jumptable_2b11b8.py correu).
OLD_SIMPLE = '''fprintf(stderr,"[TYDISP] type=%u -> 0x%08X\\n", ty, tgt); } }'''
NEW_SIMPLE = '''fprintf(stderr,"[TYDISP] type=%u -> 0x%08X lr=0x%08X r3=0x%08X r4=0x%08X\\n",
                ty, tgt, (uint32_t)ctx->lr, (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[4]); } }'''

RICH_RE = re.compile(_flex(OLD_RICH))
SIMPLE_RE = re.compile(_flex(OLD_SIMPLE))
# Ja' aplicado: um fprintf de [TYDISP] que ja' imprime lr=.
DONE_RE = re.compile(r'\[TYDISP\][^;]*lr=0x%08X', re.S)


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2/ppu_recomp_001.cpp")
    rc = 0
    seen_probe = False
    for p in paths:
        if not p.exists():
            print(f"skip {p} (inexistente)")
            continue
        s = p.read_text(encoding="utf-8", errors="replace")
        if "[TYDISP]" not in s:
            continue  # probe base nao vive neste chunk
        seen_probe = True
        if DONE_RE.search(s):
            print(f"ALREADY-APPLIED {p} (TYDISP ja' imprime lr)")
            continue
        m = RICH_RE.search(s)
        if m:
            p.write_text(s[: m.start()] + NEW_RICH + s[m.end():],
                         encoding="utf-8", newline="\n")
            print(f"APPLIED {p} (lr enhanced)")
            continue
        m = SIMPLE_RE.search(s)
        if m:
            p.write_text(s[: m.start()] + NEW_SIMPLE + s[m.end():],
                         encoding="utf-8", newline="\n")
            print(f"APPLIED {p} (simple lr)")
            continue
        # Probe [TYDISP] existe mas nao em nenhuma das formas conhecidas:
        # isto e' drift a serio, continua a ser falha.
        print(f"FAILED {p}: [TYDISP] presente mas fprintf em forma desconhecida")
        rc = 1
    if not seen_probe:
        print("SKIP nao-fatal: probe [TYDISP] ausente do lift — instalado pelo "
              "patch_jumptable_2b11b8.py (pre-requisito); nada para enriquecer")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
