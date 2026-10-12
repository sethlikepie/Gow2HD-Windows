#!/usr/bin/env python3
"""Re-arm media+0x16C ("alive" flag) at grow-open entry when left 0 (F2a).

WHY
---
Segunda peca do inventario FIOS-* (regressao do marco v1.1, 2026-07-31, apos
patch_fios_freelist_rebuild.py + patch_fios_host_pop.py ainda nao fecharem o
gate sozinhos -- ver notas dessas sessoes). func_00307CC4 (parte do caminho
de open/grow de R_LglScA/R_PermA) le media+0x16C logo a entrada; se estiver
0 (o mesmo "alive flag" que FREELIST-REBUILD/HOST-POP tambem tocam, mas
medido a poder voltar a 0 entre o MovieStop e este open), a funcao desiste
cedo (`{ g_trampoline_fn = ...func_00307FA0; return; }`) SEM sequer tentar
o grow da freelist -- falha F2a antes de chegar a op_alloc.

Bloco extraido *verbatim* de
`recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:110113-110126` -- nao foi
reescrito de memoria. So' existe 1 sitio em todo o lift antigo e o mesmo 1
sitio existe no lift actual (func_00307CC4, confirmado por varredura:
`vm_read32(ctx->gpr[31] + 0x16C);\n        /* isync... */;` da' 1 unica
ocorrencia em todo o lift, apesar de "+ 0x16C" sozinho aparecer dezenas de
vezes noutras funcoes nao relacionadas).

FIX
---
Se media+0x16C==0, reescreve para 1 e forca ctx->gpr[0]=1 (o valor que a
CR-eval a seguir le), para que o branch de desistencia precoce nao dispare.
Gated por PS3_FIOS_16C_SELFHEAL (default ON, 0 desliga -- mesmo default do
original).

Marker: FIOS-16C-SELFHEAL. Idempotente (whole-file marker check).
"""
from pathlib import Path
import re
import sys

MARKER = "FIOS-16C-SELFHEAL"
ROOT = (Path(sys.argv[1]) if len(sys.argv) > 1
        else Path(__file__).resolve().parent.parent / "recomp_macos_v2")

# Ancora: leitura de media+0x16C seguida do "isync" -- confirmado
# unico (1 ocorrencia) em todo o lift, apesar de "+ 0x16C" sozinho nao ser
# especifico. O isync aceita as duas formas: o no-op do lifter antigo e o
# PPU_FENCE(acquire) do lifter com fences (porte de upstream 981930fd, ou
# patch_ppu_fences.py sobre um lift antigo).
NEEDLE_RE = re.compile(
    r"( *ctx->gpr\[0\] = vm_read32\(ctx->gpr\[31\] \+ 0x16C\);\n"
    r" *(?:/\* isync: cache/sync — no-op \*/;|PPU_FENCE\(acquire\);[^\n]*)\n)"
)

BLOCK = (
    "        /* " + MARKER + ": media+0x16C is the \"alive\" flag. After MovieStop it\n"
    "         * is sometimes left 0 while freelist is also empty, so WAD open\n"
    "         * (R_LglScA) hard-fails before grow. Re-arm to 1 so freelist grow\n"
    "         * path can run (Task 4b F2a). Gated PS3_FIOS_16C_SELFHEAL (default ON). */\n"
    "        { static int _on=-1; if(_on<0){const char* e=getenv(\"PS3_FIOS_16C_SELFHEAL\");\n"
    "            _on = (!e || *e!='0') ? 1 : 0;}\n"
    "          if(_on && (uint32_t)ctx->gpr[0]==0u){\n"
    "            uint32_t m=(uint32_t)ctx->gpr[31];\n"
    "            fprintf(stderr,\"[FIOSOPEN] 16C-SELFHEAL media=0x%08X was0 head=0x%08X +218=%u ->1\\n\",\n"
    "              m, m?vm_read32(m+0x200u):0u, m?vm_read32(m+0x218u):0u);\n"
    "            fflush(stderr);\n"
    "            vm_write32(m+0x16Cu, 1u);\n"
    "            ctx->gpr[0]=1;\n"
    "          } }\n"
)


def _insert(m: "re.Match[str]") -> str:
    return m.group(1) + BLOCK


def patch_file(p: Path) -> str:
    t = p.read_text(encoding="utf-8", errors="replace")
    if MARKER in t:
        return "ALREADY"
    matches = list(NEEDLE_RE.finditer(t))
    if not matches:
        return "SKIP"
    n = 0

    def repl(m: "re.Match[str]") -> str:
        nonlocal n
        n += 1
        return _insert(m)

    t = NEEDLE_RE.sub(repl, t)
    p.write_text(t, encoding="utf-8")
    return "APPLIED x%d" % n


def main() -> int:
    files = sorted(ROOT.glob("ppu_recomp_*.cpp"))
    if not files:
        print("nenhum ppu_recomp_*.cpp em %s" % ROOT)
        return 1
    any_hit = False
    for f in files:
        r = patch_file(f)
        if r != "SKIP":
            any_hit = True
        print("%s: %s" % (f.name, r))
    if not any_hit:
        print("SKIP: needle not found (func_00307CC4 +0x16C read)")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
