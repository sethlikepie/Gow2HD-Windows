#!/usr/bin/env python3
"""After WADLD-T1R, capture ~texture packages via ps3_host_wad_tex_capture.

DIAGNOSTICO 2026-07-25 (re-lift)
--------------------------------
Corrido isolado contra um lift limpo este script morria com

    WADLD-T1R probe missing - run patch_type1_result.py first

Isso NAO e' mudanca de forma do lifter: e' uma DEPENDENCIA de outro patch.
A sonda [WADLD-T1R] e' escrita por `patch_type1_result.py`; este script so'
lhe pendura a captura. Em `apply_all_patches.sh` a ordem alfabetica do glob
`patch_*.py` ja' garante `patch_type1_result.py` antes de
`patch_wad_tex_capture.py`, por isso no pipeline real o pre-requisito esta'
satisfeito e o script aplica. Verificado: com o pre-requisito aplicado ao
lift limpo, este script da' "OK wad_tex capture (fallback)" e "already" nas
corridas seguintes.

CORRECCAO aplicada mesmo assim (chunk-fixo)
-------------------------------------------
O script abria `ppu_recomp_001.cpp` por NOME. O lifter passou de 31 para 7
chunks e a func_002B0E78 (onde a sonda T1R vive) pode migrar de ficheiro a
qualquer re-lift -- nesse caso o script rebentava com rc!=0 (FAILED no
apply_all_patches.sh) mesmo com o pre-requisito aplicado. Passa a usar
`resolve_lift_paths` (aceita DIRECTORIO) e a procurar a sonda T1R em
QUALQUER chunk; a mensagem de pre-requisito em falta so' aparece se nenhum
chunk a tiver.

Limpeza: removidas as variaveis mortas da tentativa original de ancoragem
(`needle`, `shgx_end`, `k`, `close`) que nao eram usadas em lado nenhum.
As DUAS estrategias de colocacao mantem-se e pela mesma ordem:
  1) a seguir ao bloco `if(on){...}` da sonda EXPANDIDA de sessao (a que tem
     o dump SHGX e declara nm/buf/sz em escopo exterior);
  2) fallback: logo a seguir ao `fflush(stderr);` da linha [WADLD-T1R] --
     dentro do `if(n++<80)`, que e' o unico escopo onde nm/buf/sz existem na
     sonda produzida hoje por `patch_type1_result.py`.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

DECL = 'extern "C" void ps3_host_wad_tex_capture(const char* name, uint32_t buf, uint32_t size);\n'
MARKER = "ps3_host_wad_tex_capture(nm"

INSERT_AFTER = """            if(nm[0]=='~' && buf && sz > 1024){
              ps3_host_wad_tex_capture(nm, buf, sz);
            }
"""

T1R_FPRINTF = 'fprintf(stderr,"[WADLD-T1R]'
SHGX_ANCHOR = "if(nm[0]=='S'&&nm[1]=='H'&&nm[2]=='G'&&nm[3]=='X'"


def _add_decl(s: str) -> str:
    if "ps3_host_wad_tex_capture(const char*" in s:
        return s
    if "#include <math.h>\n" in s:
        return s.replace("#include <math.h>\n", "#include <math.h>\n\n" + DECL, 1)
    return DECL + s


def patch_file(p: Path) -> str:
    s = p.read_text(encoding="utf-8", errors="replace")
    if "[WADLD-T1R]" not in s:
        return "SKIP"
    if MARKER in s:
        return "already"

    i = s.find(T1R_FPRINTF)
    if i < 0:
        raise SystemExit("%s: [WADLD-T1R] presente mas fprintf nao encontrado" % p.name)

    # 1) sonda expandida de sessao (nm/buf/sz em escopo exterior ao cap)
    j = s.find(SHGX_ANCHOR, i)
    if j > 0:
        end_on = s.find("\n            } }", j)
        if end_on > 0:
            pos = end_on + len("\n            } }")
            s = _add_decl(s[:pos] + "\n" + INSERT_AFTER + s[pos:])
            p.write_text(s, encoding="utf-8", newline="\n")
            return "OK wad_tex capture (after if-on)"

    # 2) fallback: logo apos o fflush da linha T1R (dentro do cap, unico
    #    escopo onde nm/buf/sz existem na sonda actual)
    ff = s.find("fflush(stderr);", i)
    if ff < 0:
        raise SystemExit("%s: fflush a seguir ao T1R nao encontrado" % p.name)
    pos = ff + len("fflush(stderr);")
    s = _add_decl(s[:pos] + "\n" + INSERT_AFTER + s[pos:])
    p.write_text(s, encoding="utf-8", newline="\n")
    return "OK wad_tex capture (fallback)"


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], str(Path(__file__).resolve().parent))
    hit = False
    for p in paths:
        if not p.is_file():
            print("skip %s" % p)
            continue
        r = patch_file(p)
        if r == "SKIP":
            continue
        hit = True
        print("%s: %s" % (p.name, r))
    if not hit:
        raise SystemExit(
            "WADLD-T1R probe missing em todos os chunks -- corra "
            "patch_type1_result.py primeiro (no apply_all_patches.sh a ordem "
            "alfabetica ja' o garante)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
