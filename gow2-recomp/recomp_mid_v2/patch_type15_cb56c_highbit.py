#!/usr/bin/env python3
"""Idempotent TYPE15 CB56C product fixes:
1) type 0x15 -> 0x80000015 (match WAD construct encoding)
2) if construct returns 0, reuse factory+0x48 linked product
"""
from pathlib import Path
import re
import sys
from lift_paths import resolve_lift_paths

# CORRECCAO 2026-07-25 (re-lift). As duas agulhas literais estavam ancoradas em
# blocos de probe "[POSTINTRO] ..." que eram edicao manual de sessao: nenhum
# patch_*.py os escreve (grep POSTINTRO em patch_*.py -> so' aparecem como
# agulha), logo nao existem num lift limpo. Alem disso o lift mudou de forma nos
# mesmos sitios:
#   - shape-LR   : "func_002A49BC(ctx); ..." passou a "ctx->lr = 0x000CB61C; func_002A49BC(ctx); ..."
#   - shape-TOCFIX: apos o icall1, "ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);"
#                   passou a "ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/"
#   - o lifter passou a emitir "/* nop */;" a seguir as chamadas
# Reancora-se em codigo REAL emitido pelo lifter (a chamada a func_002A49BC e o
# par rldicl/or que recebe o produto do icall1), dentro do corpo de
# func_000CB56C -- o comportamento inserido e a sua posicao sao os mesmos.
FUNC_SIG = "void func_000CB56C(ppu_context* ctx) {"

# Chamada a func_002A49BC, com ou sem o prefixo de LR do lifter actual.
CALL_2A49BC_RE = re.compile(
    r"(?:ctx->lr = 0x000CB61C; )?func_002A49BC\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n")
# Primeira leitura do tipo na stack apos essa chamada (existe no lift antigo e
# no novo; no antigo vinha depois do bloco POSTINTRO, que assim fica preservado
# antes da nossa insercao -- mesma posicao semantica).
READ_TYPE_LINE = "        ctx->gpr[9] = vm_read16(ctx->gpr[1] + 0x72);\n"
# Par que recolhe o produto devolvido pelo icall1 (unico no corpo da funcao).
PRODUCT_RE = re.compile(
    r"        ctx->gpr\[29\] = ppc_rldicl\(ctx->gpr\[3\], 0, 32\);\n"
    r"        ctx->gpr\[28\] = ctx->gpr\[3\] \| ctx->gpr\[3\];\n")


def func_span(t: str):
    """(inicio, fim) do corpo de func_000CB56C, ou None se nao estiver aqui."""
    # the current lifter emits "ppu_context* PPU_RESTRICT ctx"; older lifts do not
    sig = FUNC_SIG
    i = t.find(sig)
    if i < 0:
        sig = FUNC_SIG.replace("ppu_context* ctx", "ppu_context* PPU_RESTRICT ctx")
        i = t.find(sig)
    if i < 0:
        return None
    j = t.find("\nvoid func_", i + len(sig))
    return (i, len(t) if j < 0 else j)


def patch_highbit(t: str) -> str:
    marker = "[TYPE15] CB56C type high-bit"
    if "PS3_TYPE15_HIGHBIT" in t:
        return t
    if marker in t:
        # older lift: the block is there, always on -> gate it (OFF by default)
        old_if = "          if (_t == 0x15u) {\n            vm_write32(_d, _t | 0x80000000u);"
        if old_if in t:
            return t.replace(old_if,
                "          static int _hb = -1; if (_hb < 0) { const char* _e = getenv(\"PS3_TYPE15_HIGHBIT\"); _hb = (_e && _e[0] == '1'); }\n"
                "          if (_hb && _t == 0x15u) {\n            vm_write32(_d, _t | 0x80000000u);", 1)
        return t
    span = func_span(t)
    if span is None:
        return t
    b0, b1 = span
    m = CALL_2A49BC_RE.search(t, b0, b1)
    if m is None:
        raise SystemExit("highbit needle missing: chamada a func_002A49BC nao "
                         "encontrada em func_000CB56C")
    at = t.find(READ_TYPE_LINE, m.end(), b1)
    if at < 0:
        raise SystemExit("highbit needle missing: leitura vm_read16(r1+0x72) nao "
                         "encontrada apos a chamada a func_002A49BC")
    insert = '''        /* TYPE15: match WAD path encoding 0x80000015. */
        /* PS3_TYPE15_HIGHBIT=1 only (bring-up aid, OFF by default since
         * 2026-09-23): bit 31 makes func_0039E090 skip the type-21 scope and
         * hand back the root default object, a 0x50-byte lite object with no
         * node pool, so every music request aborted (menu/pause/bg*.vpk never
         * streamed) and the mix clipped. */
        { uint32_t _d = (uint32_t)ctx->gpr[1] + 0x70u;
          uint32_t _t = vm_read32(_d);
          static int _hb = -1; if (_hb < 0) { const char* _e = getenv("PS3_TYPE15_HIGHBIT"); _hb = (_e && _e[0] == '1'); }
          if (_hb && _t == 0x15u) {
            vm_write32(_d, _t | 0x80000000u);
            { static int _n=0; if(_n++<8)
                fprintf(stderr,"[TYPE15] CB56C type high-bit 0x15 -> 0x%08X (match WAD path)\\n",
                  vm_read32(_d)); }
          }
        }
'''
    # Insere ANTES da leitura do tipo (que ja' esta' no texto), i.e. logo apos o
    # retorno de func_002A49BC -- exactamente onde a agulha antiga punha o bloco.
    return t[:at] + insert + t[at:]

def patch_reuse(t: str) -> str:
    marker = "[TYPE15] CB56C reuse product"
    if marker in t:
        return t
    span = func_span(t)
    if span is None:
        return t
    b0, b1 = span
    m = PRODUCT_RE.search(t, b0, b1)
    if m is None:
        raise SystemExit("reuse needle missing: par rldicl/or do produto do "
                         "icall1 nao encontrado em func_000CB56C")
    insert = '''        /* TYPE15: reuse existing product at factory+0x48 when construct returns 0. */
        if ((uint32_t)ctx->gpr[3] == 0u) {
          uint32_t _ent = 0x47D00000u;
          uint32_t _vt = vm_read32(_ent);
          if (_vt == 0x00516D70u) {
            uint32_t _hdr = vm_read32(_ent + 0x48u);
            if (_hdr >= 0x10000u && _hdr < 0x4F000000u) {
              uint32_t _prod = _hdr + 4u;
              if (_prod >= 0x10000u && _prod < 0x4F000000u) {
                ctx->gpr[3] = _prod;
                { static int _n=0; if(_n++<8)
                    fprintf(stderr,"[TYPE15] CB56C reuse product hdr=0x%08X prod=0x%08X\\n",
                      _hdr, _prod); }
              }
            }
          }
        }
'''
    # Insere ANTES do par rldicl/or, i.e. com r3 ainda a ser o valor devolvido
    # pelo icall1 -- mesma posicao que a agulha antiga tinha.
    return t[:m.start()] + insert + t[m.start():]

# CORRECCAO 2026-07-25 (chunk-fixo): com um DIRECTORIO em argv varrem-se os 7
# chunks e func_000CB56C so' vive num deles; antes escrevia-se sempre o ficheiro
# (mesmo sem alteracao) e imprimia-se "ok" para todos. Agora e' SKIP para os
# chunks sem a funcao, APPLIED/ALREADY para o que a tem, e rc=1 se nenhum tiver.
def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2/ppu_recomp_000.cpp")
    hit = False
    for ps in paths:
        p = Path(ps)
        if not p.exists():
            print(f"skip {p}")
            continue
        t = p.read_text()
        if func_span(t) is None:
            continue
        hit = True
        t2 = patch_reuse(patch_highbit(t))
        if t2 != t:
            p.write_text(t2)
            print(f"APPLIED {p}")
        else:
            print(f"ALREADY-APPLIED {p}")
    if not hit:
        print("FAILED: func_000CB56C nao encontrada em nenhum chunk")
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
