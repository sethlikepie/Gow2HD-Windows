#!/usr/bin/env python3
r"""E96 -- fix de raiz da parede [G] (parte 3): o icallA do B71 volta a ser natural.

Medido (E94/E95, 2026-09-02, com o oraculo RPCS3): no B71 #058 o bloco de Julho
(patch_b71_cb56c_reuse_block.py) chama ps3_factory_freelist_replenish(T4) --
planta uma freelist FALSA com um 'shell' host em 0x47AB4400 na fabrica da
classe 4 -- e, se o construct devolve 0, 'reutiliza' o produto existente e
salta o attach. Mais tarde, o registo SCRX (classe 4) do R_Hero01 saca esse
shell da freelist (create -> OPD em 0x90090130 -> lixo), o descritor da
classe 4 da raiz do hero fica em 0x47AB4400, o lookup vt[0x50] devolve-o e
func_0039D51C entra em ciclo infinito (frame preso). No console: freelist
vazia -> crescimento natural (func_002550C8, fixado) -> descritor valido.

Este patch poe os tres hacks (repair_vt, replenish, reuse+skip-attach) sob
PS3_B71_LEGACY_ICALL=1 (OFF por default): o construct e' chamado como no
jogo e um resultado nulo segue o ramo natural (`beq loc_000B7514`).
Idempotente (marcador E96-B71-NATURAL). Depende de patch_b71_cb56c_reuse_block.py.
"""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
A_OLD = '''          if (_ent) {
            ps3_factory_repair_vt(_ent);
            /* type 4 factory (0x400D6808 family) freelist often empty after first
             * WAD product — replenish before construct. */
            ps3_factory_freelist_replenish(_ent, 0x0004u);
          }'''
A_NEW = '''          /* E96-B71-NATURAL: repair/replenish/reuse only under PS3_B71_LEGACY_ICALL=1 */
          if (_ent && b71_legacy_icall()) {
            ps3_factory_repair_vt(_ent);
            ps3_factory_freelist_replenish(_ent, 0x0004u);
          }'''
B_OLD = '''          if ((uint32_t)ctx->gpr[3] == 0u && _ent) {
            uint32_t rp = ps3_factory_reuse_product(_ent);'''
B_NEW = '''          if (b71_legacy_icall() && (uint32_t)ctx->gpr[3] == 0u && _ent) {
            uint32_t rp = ps3_factory_reuse_product(_ent);'''
HELPER = '''static int b71_legacy_icall(void) {
    /* E96 (2026-09-02): default natural; PS3_B71_LEGACY_ICALL=1 restores the July hacks for A/B. */
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_B71_LEGACY_ICALL"); on = (e && *e && *e != '0') ? 1 : 0; }
    return on;
}
'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s = f.read_text(errors='replace')
        if A_OLD not in s and "E96-B71-NATURAL" not in s: continue
        if "E96-B71-NATURAL" in s: print(f"E96: ALREADY em {f.name}"); return 0
        if s.count(A_OLD) != 1 or s.count(B_OLD) != 1: print(f"E96: agulhas A x{s.count(A_OLD)} B x{s.count(B_OLD)} -> recusa"); return 2
        anchor = "void func_000B71B8(ppu_context* ctx) {"
        if s.count(anchor) != 1: print("E96: func_000B71B8 ambigua"); return 2
        s = s.replace(A_OLD, A_NEW, 1).replace(B_OLD, B_NEW, 1).replace(anchor, HELPER + anchor, 1)
        f.write_text(s); print(f"E96: aplicado em {f.name}"); return 0
    print("E96: bloco icallA ausente (correr patch_b71_cb56c_reuse_block.py antes)"); return 2
if __name__ == "__main__": raise SystemExit(main())
