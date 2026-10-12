#!/usr/bin/env python3
"""Host-pop one FIOS op from the freelist when the guest CAS returns r3==0.

WHY
---
Segunda metade em falta do REG-03 (regressao do marco v1.1, 2026-07-31).
patch_fios_freelist_rebuild.py repoe a free-list (re-semeia a head em
media+0x200 quando estava vazia), mas medido no gate de 6 corridas apos
esse fix (0/6, thr_end=0 em todas): a free-list e' reconstruida
(`[FIOSOPEN] FREELIST-REBUILD ... head=0x430094C0`), st620 estabiliza em
11/11 em 6/6 corridas -- mas `op_alloc` continua a devolver 0
(`[FIOSOPEN] 0030D5CC op_alloc #N r3=0x00000000`).

Causa: o CAS do guest (a instrucao lwarx/stwcx que tenta popar a head da
free-list em func_0030D5CC via func_00307C8C) falha em popar mesmo com a
lista reconstruida pelo host -- o comentario original no lift antigo
(`recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:116216`) documenta exactamente
isto:

    "FIOS-HOST-POP: guest freelist CAS often fails to pop after rebuild
     (F2a with head!=0). Host-pop one op when r3==0 and head!=0."

E o comentario em `...:116318` liga isto ao F2B-MOVIEIO (ja presente no
lift actual): "HOST-POP salvages the op but FO stays null without this
path" -- ou seja, o F2B-MOVIEIO so' funciona de verdade quando o HOST-POP
lhe entrega uma op nao-nula primeiro.

O bloco abaixo foi extraido *verbatim* de
`recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:116216-116240` -- nao foi
reescrito de memoria. So' existe 1 sitio em todo o lift antigo (confirmado
por varredura: `grep -c FIOS-HOST-POP` da' 1 em ppu_recomp_001.cpp e 0em
todos os outros ~30 chunks), e o mesmo 1 sitio existe no lift actual
(func_0030D5CC, confirmado por `grep -c "SEM OP LIVRE (F2a)"` = 1).

FIX
---
Logo depois do bloco de trace FIOS-OPEN-PROBE(op_alloc) (que so' imprime,
gated por PS3_TRACE_FIOSOPEN) e ANTES do primeiro uso de ctx->gpr[3] pelo
caminho de sucesso/falha: se r3==0 (CAS falhou) e a free-list (media+0x200,
media = ctx->gpr[26], herdado do parse de func_0030D5CC) tem head!=0, o
host desencadeia manualmente a op do topo da lista -- avanca a head,
zera os campos de estado da op (+0x0 next, +0x90 done, +0x40/+0x44
mfd/erro), consome qualquer sticky done-word residual de uma vida anterior
da mesma op, e entrega-a em ctx->gpr[3] como se o CAS do guest tivesse
sido bem sucedido. Gated por PS3_FIOS_HOST_POP (default ON, 0 desliga --
mesmo default do original).

Marker: FIOS-HOST-POP. Idempotente (whole-file marker check).
"""
from pathlib import Path
import re
import sys

MARKER = "FIOS-HOST-POP"
ROOT = (Path(sys.argv[1]) if len(sys.argv) > 1
        else Path(__file__).resolve().parent.parent / "recomp_macos_v2")

# Ancora: o fecho do bloco de trace FIOS-OPEN-PROBE(op_alloc) em
# func_0030D5CC -- "SEM OP LIVRE (F2a)" so' aparece 1x em todo o lift
# (confirmado por varredura), o que torna esta ancora suficientemente
# especifica mesmo sem exigir o nome da funcao explicitamente.
NEEDLE_RE = re.compile(
    r"( *\(\(uint32_t\)ctx->gpr\[3\]==0u\)\?\" *<-- SEM OP LIVRE \(F2a\)\":\"\"\);\n"
    r" *fflush\(stderr\); \} \} \}\n)"
)

# Bloco extraido verbatim de
# recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:116216-116240. `media` e' o
# ponteiro do pool de midia, que func_0030D5CC ja guarda em ctx->gpr[26]
# (ppc_rldicl(ctx->gpr[3], 0, 32) logo a entrada -- confirmado identico
# entre o lift antigo e o actual).
BLOCK = (
    "        /* " + MARKER + ": guest freelist CAS often fails to pop after rebuild\n"
    "         * (F2a with head!=0). Host-pop one op when r3==0 and head!=0. */\n"
    "        { static int _on=-1; if(_on<0){const char* e=getenv(\"PS3_FIOS_HOST_POP\");\n"
    "            _on=(!e||*e!='0')?1:0;}\n"
    "          if(_on && (uint32_t)ctx->gpr[3]==0u){\n"
    "            uint32_t media=(uint32_t)ctx->gpr[26];\n"
    "            if(media>=0x10000u && media<0x4F000000u){\n"
    "              if(vm_read32(media+0x16Cu)==0u) vm_write32(media+0x16Cu,1u);\n"
    "              uint32_t head=vm_read32(media+0x200u);\n"
    "              if(head>=0x10000u && head<0x4F000000u){\n"
    "                uint32_t next=vm_read32(head+0x0u);\n"
    "                vm_write32(media+0x200u, next);\n"
    "                vm_write32(head+0x0u, 0u);\n"
    "                vm_write32(head+0x90u, 0u);\n"
    "                vm_write32(head+0x40u, 0u);\n"
    "                vm_write32(head+0x44u, 0u);\n"
    "                /* drop sticky from prior life of this op */\n"
    "                { extern void ps3_fios_sticky_consume(uint32_t); ps3_fios_sticky_consume(head); }\n"
    "                ctx->gpr[3]=head;\n"
    "                fprintf(stderr,\"[FIOSOPEN] HOST-POP media=0x%08X op=0x%08X next=0x%08X\\n\",\n"
    "                  media, head, next);\n"
    "                fflush(stderr);\n"
    "              }\n"
    "            }\n"
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
        print("SKIP: needle not found (func_0030D5CC op_alloc probe close)")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
