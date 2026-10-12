"""Resolucao de caminhos de lift partilhada pelos patch_*.py.

Porque existe
-------------
O apply_all_patches.sh passa o DIRECTORIO do lift em argv[1] (e' esse o seu
contrato: "./apply_all_patches.sh [DIR_DE_LIFT]"). Varios patch_*.py foram
escritos a assumir que argv[1] era um FICHEIRO e faziam Path(argv[1]).read_text()
directamente -- o que rebenta com

    IsADirectoryError: [Errno 21] Is a directory: .../recomp_macos_v2

Medido a 2026-07-24: 5 dos 71 patches falhavam so' por isto, sem qualquer relacao
com o lift ter mudado de forma.

Alem disso o numero de chunks deixou de ser estavel: o lift de 20 jul tinha 31
ficheiros ppu_recomp_*.cpp e o lifter actual produz 7. Um patch que assuma
"a minha funcao vive no ppu_recomp_000.cpp" passa a falhar quando ela migra de
chunk. Expandir um directorio para TODOS os chunks resolve as duas coisas de
uma vez: as substituicoes destes scripts sao no-op quando a agulha nao esta'
presente, por isso varrer chunks a mais e' inofensivo e torna-os imunes ao
layout.
"""

from __future__ import annotations

from pathlib import Path
from typing import Iterable, List


def resolve_lift_paths(args: Iterable[str], default: str) -> List[Path]:
    """Converte os argumentos de linha de comando em ficheiros de lift concretos.

    - argumento que e' DIRECTORIO -> todos os ppu_recomp_*.cpp la' dentro (ordenados)
    - argumento que e' FICHEIRO   -> o proprio ficheiro
    - sem argumentos              -> `default` (relativo ao cwd, como era antes)

    Um argumento inexistente e' devolvido tal e qual, para o chamador manter o
    seu proprio tratamento (tipicamente imprimir "skip <path>").
    """
    out: List[Path] = []
    for a in list(args) or [default]:
        p = Path(a)
        if p.is_dir():
            out.extend(sorted(p.glob("ppu_recomp_*.cpp")))
        else:
            out.append(p)
    return out
