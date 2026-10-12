#!/usr/bin/env python3
"""Garante #include <stdlib.h> nos chunks liftados (linkagem do getenv das probes).

Porque: as probes que os outros patch_*.py inserem usam o idioma

    { static int on=-1; if(on<0){extern char* getenv(const char*); ...} ... }

Em C++, uma declaracao `extern` em ambito de BLOCO so herda linkagem C se o
mesmo nome ja tiver sido declarado com linkagem C em ambito de namespace. O
preambulo gerado pelo ppu_lifter.py inclui <stdio.h>, <string.h> e <math.h>,
mas NAO <stdlib.h> — onde vive o getenv. No MinGW/Windows o <stdlib.h> vinha
por arrasto de outro header e o link passava; no clang++/macOS nao vem, a
declaracao de bloco cria uma entidade NOVA com mangling C++ e o link falha:

    Undefined symbols: "getenv(char const*)", referenced from: func_...
    NOTE: found '_getenv' in libsystem_c.tbd, declaration possibly missing 'extern "C"'

Fix: incluir <stdlib.h> a seguir ao <math.h> do preambulo. Aplica-se a TODOS os
chunks (nao so aos que hoje tem probes) para ficar independente da ordem em que
o apply_all_patches.sh corre os patches.

Correcao a montante (nao feita aqui, exigiria re-lift): emitir <stdlib.h> no
preambulo do ppu_lifter.py.

Idempotente. Roda a partir de recomp_mid_v2/ (ou com o dir de lift no argv).
"""
from __future__ import annotations

import sys
from pathlib import Path

ANCHOR = "#include <math.h>\n"
INCLUDE = "#include <stdlib.h>\n"


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    chunks = sorted(root.glob("ppu_recomp_*.cpp"))
    if not chunks:
        print(f"FAIL: nenhum ppu_recomp_*.cpp em {root}", file=sys.stderr)
        return 1

    patched = already = 0
    for path in chunks:
        src = path.read_text(encoding="utf-8", errors="replace")
        if INCLUDE in src[:4000]:
            already += 1
            continue
        if ANCHOR not in src[:4000]:
            print(f"FAIL: ancora {ANCHOR.strip()!r} ausente em {path.name}", file=sys.stderr)
            return 2
        path.write_text(src.replace(ANCHOR, ANCHOR + INCLUDE, 1), encoding="utf-8", newline="\n")
        patched += 1

    print(f"OK stdlib.h: {patched} chunk(s) patcheado(s), {already} ja tinha(m)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
