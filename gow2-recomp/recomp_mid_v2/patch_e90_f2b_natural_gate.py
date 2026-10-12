#!/usr/bin/env python3
r"""E90 -- fix de raiz da parede [F]: desliga (por DEFAULT) o preenchimento host do
ring do WAD (F2B) e deixa o caminho natural do jogo (FUN_002b9f00 -> chunks aread de 128KB ->
AREAD-HLE em func_002B3D1C) alimentar o leitor FUN_002ba76c.

Porque existe (E88/E89 + oraculo RPCS3, 2026-09-01)
----------------------------------------------------
O oraculo (RPCS3, interpretador, breakpoints no despachante de registos
FUN_002b9c80) processa o R_PermA como UM pedido de 20169344 bytes servido em
chunks de 0x20000 pelo proprio jogo; a sequencia de registos e' identica a'
nossa durante 3458 registos e termina em `06 0B 0C 0D 0E 0F 10` com
avail=0 rem=0 -> o leitor devolve 0 e o #038 segue. No nosso lado, depois do
`10` o bloco F2B-STREAM-ALIGN (patch_f2ba9bc_stream_align_install.py)
REBOBINA o ficheiro (20054688 -> 20038352), o leitor le' lixo (`F46F3520`),
re-processa registos antigos e um SEGUNDO `06` despeja o R_PermA
(pop-all -> cursores do conjunto A a -1) -- a parede [F].

Medido (E90, 3 corridas ON + 1 OFF): com o caminho natural, a sequencia de
registos e' IDENTICA a' do oraculo ate ao fim, o segundo `06` desaparece,
VAZIO/ICALL-BAD=0, e o frame 1 vira frame 10000. Com o host fill (OFF/legado):
ALIGN=1, dois `06`, VAZIO=14, frame=1.

Default: caminho NATURAL. PS3_F2B_HOST_FILL=1 (ou PS3_F2B_NATURAL=0) repoe o
legado so' para comparacao A/B. O gate faz no-op de:
  f2b_stream_fill / f2b_stream_ensure / f2b_stream_pre_consume /
  f2b_stream_eof_try_complete e do bloco F2B-BODY-CLAMP/ALIGN/RESYNC em
  func_002BA9BC. O F2B-STREAM-PUMP passa tambem a default OFF (PS3_FIOS_STREAM_PUMP=1 repoe),
editado no bloco do open (patch_fios_f2b_open_block_install.py) e no lift.
O open do FO falso (F2B-MOVIEIO/DONEFORCE/RESTATUS) fica como esta'.

Idempotente: marcador `f2b_natural_on(` presente -> ALREADY.
Uso: python3 recomp_mid_v2/patch_e90_f2b_natural_gate.py [LIFT_DIR]
"""
import sys
from pathlib import Path

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"

HELPER = '''/* E90: PS3_F2B_NATURAL=1 -> host fill/ensure/pre_consume/eof/align OFF; the
 * guest's own chunked aread path (FUN_002b9f00 -> 002B3D1C AREAD-HLE) feeds
 * the ring, exactly like the console/oracle. Latched once; OFF by default. */
static int f2b_natural_on(void) {
    /* Default ON since E90 (2026-09-01): the host fill is the root cause of
     * wall [F]. PS3_F2B_HOST_FILL=1 (or PS3_F2B_NATURAL=0) restores the legacy
     * pump/fill/align path for A/B comparison only. */
    static int on = -1;
    if (on < 0) {
        const char* h = getenv("PS3_F2B_HOST_FILL");
        const char* n = getenv("PS3_F2B_NATURAL");
        on = 1;
        if (h && *h && *h != '0') on = 0;
        if (n && *n && *n == '0') on = 0;
    }
    return on;
}
'''

OPTIONAL = [
    # pump default OFF: needle present only if the open-block installer predates E90
    ("            _pump=(!e||*e!='0')?1:0;}\n",
     "            _pump=(e&&*e&&*e!='0')?1:0;} /* E90: default OFF */\n"),
]
EDITS = [
    # (needle, replacement) -- each needle must be unique in the chunk
    ('static int f2b_stream_fill(uint32_t stream, uint32_t min_need) {\n',
     'static int f2b_stream_fill(uint32_t stream, uint32_t min_need) {\n    if (f2b_natural_on()) return 0; /* E90 */\n'),
    ('extern "C" void f2b_stream_ensure(uint32_t type_sys) {\n',
     'extern "C" void f2b_stream_ensure(uint32_t type_sys) {\n    if (f2b_natural_on()) return; /* E90 */\n'),
    ('extern "C" void f2b_stream_eof_try_complete(uint32_t type_sys) {\n',
     'extern "C" void f2b_stream_eof_try_complete(uint32_t type_sys) {\n    if (f2b_natural_on()) return; /* E90 */\n'),
    ('static void f2b_stream_pre_consume(ppu_context* ctx) {\n',
     'static void f2b_stream_pre_consume(ppu_context* ctx) {\n    if (f2b_natural_on()) return; /* E90 */\n'),
    ('        if (g_f2b_fill_mfd && g_f2b_fill_sz) {\n            uint32_t _bst = vm_read32((uint32_t)ctx->gpr[31] + 0x1A8u);\n',
     '        if (g_f2b_fill_mfd && g_f2b_fill_sz && !f2b_natural_on()) { /* E90 */\n            uint32_t _bst = vm_read32((uint32_t)ctx->gpr[31] + 0x1A8u);\n'),
]

def main():
    files = sorted(ROOT.glob("ppu_recomp_*.cpp"))
    tgt = [f for f in files if 'static int f2b_stream_fill(' in f.read_text(errors='replace')]
    if len(tgt) != 1:
        print(f"E90: f2b_stream_fill em {len(tgt)} chunks (esperado 1) -> recusa"); return 2
    f = tgt[0]; s = f.read_text(errors='replace')
    if 'f2b_natural_on(' in s:
        print(f"E90: ALREADY em {f.name}"); return 0
    anchor = 'static int f2b_stream_fill(uint32_t stream, uint32_t min_need) {\n'
    if s.count(anchor) != 1:
        print(f"E90: ancora f2b_stream_fill x{s.count(anchor)} (esperado 1) -> recusa"); return 2
    for needle, _ in EDITS:
        if s.count(needle) != 1:
            print(f"E90: agulha x{s.count(needle)} (esperado 1): {needle[:60]!r} -> recusa"); return 2
    s = s.replace(anchor, HELPER + anchor, 1)
    for needle, rep in EDITS:
        s = s.replace(needle, rep, 1)
    for needle, rep in OPTIONAL:
        if s.count(needle) == 1:
            s = s.replace(needle, rep, 1)
    f.write_text(s)
    print(f"E90: aplicado em {f.name} (1 helper + {len(EDITS)} gates)"); return 0

if __name__ == "__main__":
    raise SystemExit(main())
