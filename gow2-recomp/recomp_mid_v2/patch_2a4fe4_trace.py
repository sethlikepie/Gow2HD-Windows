#!/usr/bin/env python3
"""Idempotent diagnostic-only trace + loop-cap for func_002A4FE4 (2026-07-23).

func_002A4FE4 walks an intrusive circular list rooted at `this+0x70`, OR-ing
0x8000 into each node's `+0x8` field, terminating when the walk returns to the
sentinel address `this+0x70` itself. It is called unconditionally (no special
case) by its sibling factory-attach function at ppu_recomp_000.cpp:162252, so
the routine is not broken in general.

Evidence (2026-07-22-type15-cb56c-attach-diagnostic.md): under
PS3_TYPE15_FORCE_ICALL2=1 the call from func_000CB56C (ppu_recomp_000.cpp
line ~161730) on the TYPE15 REUSE product (0x42F85AE4, a freelist-REPLENISH
shell, not a naturally-constructed product) never returns and no further boot
output appears -- i.e. this is the hang site the old "002A4FE4 hangs on
pin-shell products" comment predicted, now localized precisely.

This patch does NOT change default behaviour (PS3_TRACE_2A4FE4 unset ->
byte-identical to upstream). Gated ON it adds:
  1) entry trace: product ptr + raw `this+0x70` head word (to see whether the
     reused shell's list head looks like a valid self-referential empty list).
  2) a capped node-visit trace (first ~16 nodes).
  3) a loop-cap bailout (diagnostic escape valve only, NOT a claimed fix): if
     the walk exceeds a bound, log LOOP-CAP and return, so a forced icall2 run
     can be observed past this point instead of hanging the whole process.
"""
from pathlib import Path
import re
import sys
from lift_paths import resolve_lift_paths

MARKER = "[2A4FE4] enter"

# Correccao 2026-07-25 (relift): a agulha era um literal exacto e o script
# corria contra TODOS os chunks do lift (o lifter passou de 31 para 7
# ppu_recomp_*.cpp, por isso ja' nao se sabe em qual vive a funcao). Nos 6
# chunks onde func_002A4FE4 nao existe o literal nunca casava e o script
# imprimia "FAILED ... needle missing" + rc=1, mascarando o unico chunk onde
# aplicou de facto. Passa a haver:
#   - HEADER_RE: deteccao "a funcao vive neste chunk?" -> senao, skip silencioso
#   - NEEDLE_RE: a mesma agulha, mas tolerante a whitespace/indentacao e com
#     slot opcional para os prologos callee-save novos do lifter
#     ("uint64_t _cs_NN = ctx->gpr[NN];"), que sao repostos no texto injectado.
# Assim casa com o lift antigo E com o novo, e so' e' FAILED quando a funcao
# existe mas a forma do corpo mudou de verdade.
HEADER_RE = re.compile(r"void\s+func_002A4FE4\s*\(\s*ppu_context\s*\*\s*ctx\s*\)\s*\{")

_CS_SLOT = r"(?P<cs>(?:uint64_t\s+_cs_\d+\s*=\s*ctx->gpr\[\d+\]\s*;\s*)*)"


def _flex(literal: str) -> str:
    """Literal -> regex tolerante a variacoes de espacos/indentacao/quebras."""
    return r"\s*".join(re.escape(tok) for tok in literal.split())


def patch(t: str) -> str:
    if MARKER in t:
        return t
    needle = '''void func_002A4FE4(ppu_context* ctx) {
        ctx->gpr[0] = vm_read16(ctx->gpr[3] + 0xBA);
        ctx->gpr[9] = (int64_t)(int32_t)(-32768);
        ctx->gpr[0] = ctx->gpr[0] | ctx->gpr[9];
        vm_write16(ctx->gpr[3] + 0xBA, ctx->gpr[0]);
        ctx->gpr[11] = vm_read32(ctx->gpr[3] + 0x70); ctx->gpr[3] += 0x70;
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int32_t)ctx->gpr[11]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }
        if (((ctx->cr >> 0) & 2)) return;
        ctx->gpr[10] = (int64_t)(int32_t)(-32768);
loc_002A5004:
        ctx->gpr[9] = ppc_rldicl(ctx->gpr[11], 0, 32);
        ctx->gpr[11] = vm_read32(ctx->gpr[9] + 0x0);
        ctx->gpr[0] = vm_read16(ctx->gpr[9] + 0x8);
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int32_t)ctx->gpr[11]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }
        ctx->gpr[0] = ctx->gpr[0] | ctx->gpr[10];
        vm_write16(ctx->gpr[9] + 0x8, ctx->gpr[0]);
        if ((!((ctx->cr >> 0) & 2))) goto loc_002A5004;
        return;
}'''
    insert = '''void func_002A4FE4(ppu_context* ctx) {
@@CS@@        { static int _on=-1; if(_on<0){extern char* getenv(const char*);
            const char* e=getenv("PS3_TRACE_2A4FE4"); _on=(e&&*e&&*e!='0')?1:0;}
          if(_on){ static int _n=0; if(_n++<16)
            fprintf(stderr,"[2A4FE4] enter product=0x%08X head_word=0x%08X (sentinel=0x%08X)\\n",
              (unsigned)(uint32_t)ctx->gpr[3],
              vm_read32((uint32_t)ctx->gpr[3] + 0x70),
              (unsigned)((uint32_t)ctx->gpr[3] + 0x70));
            fflush(stderr);} }
        ctx->gpr[0] = vm_read16(ctx->gpr[3] + 0xBA);
        ctx->gpr[9] = (int64_t)(int32_t)(-32768);
        ctx->gpr[0] = ctx->gpr[0] | ctx->gpr[9];
        vm_write16(ctx->gpr[3] + 0xBA, ctx->gpr[0]);
        ctx->gpr[11] = vm_read32(ctx->gpr[3] + 0x70); ctx->gpr[3] += 0x70;
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int32_t)ctx->gpr[11]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }
        if (((ctx->cr >> 0) & 2)) return;
        ctx->gpr[10] = (int64_t)(int32_t)(-32768);
        { static int _on=-1; if(_on<0){extern char* getenv(const char*);
            const char* e=getenv("PS3_TRACE_2A4FE4"); _on=(e&&*e&&*e!='0')?1:0;}
          if(_on){ uint64_t _iters=0;
loc_002A5004:
        ctx->gpr[9] = ppc_rldicl(ctx->gpr[11], 0, 32);
        ctx->gpr[11] = vm_read32(ctx->gpr[9] + 0x0);
        ctx->gpr[0] = vm_read16(ctx->gpr[9] + 0x8);
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int32_t)ctx->gpr[11]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }
        ctx->gpr[0] = ctx->gpr[0] | ctx->gpr[10];
        vm_write16(ctx->gpr[9] + 0x8, ctx->gpr[0]);
            if (_on) { if (_iters<16) fprintf(stderr,"[2A4FE4] node[%llu]=0x%08X next=0x%08X\\n",
                (unsigned long long)_iters, (unsigned)(uint32_t)ctx->gpr[9], (unsigned)(uint32_t)ctx->gpr[11]);
              if (++_iters >= 200000ull) {
                fprintf(stderr,"[2A4FE4] LOOP-CAP hit at %llu nodes (diagnostic bailout, NOT a fix) product=0x%08X\\n",
                  (unsigned long long)_iters, (unsigned)((uint32_t)ctx->gpr[3]-0x70u));
                fflush(stderr);
                return;
              } }
        if ((!((ctx->cr >> 0) & 2))) goto loc_002A5004;
          } else {
loc_002A5004_off:
        ctx->gpr[9] = ppc_rldicl(ctx->gpr[11], 0, 32);
        ctx->gpr[11] = vm_read32(ctx->gpr[9] + 0x0);
        ctx->gpr[0] = vm_read16(ctx->gpr[9] + 0x8);
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int32_t)ctx->gpr[11]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }
        ctx->gpr[0] = ctx->gpr[0] | ctx->gpr[10];
        vm_write16(ctx->gpr[9] + 0x8, ctx->gpr[0]);
        if ((!((ctx->cr >> 0) & 2))) goto loc_002A5004_off;
          }
        }
        return;
}'''
    # Agulha literal -> regex tolerante (ver nota no topo): mesmo corpo, mas
    # aceita reindentacao e o prologo callee-save novo, que e' recolocado.
    head_lit, body_lit = needle.split("{", 1)
    del head_lit
    pat = re.compile(
        r"void\s+func_002A4FE4\s*\(\s*ppu_context\s*\*\s*ctx\s*\)\s*\{\s*"
        + _CS_SLOT
        + _flex(body_lit)
    )
    hm = HEADER_RE.search(t)
    m = pat.search(t, hm.start()) if hm else None
    if m is None:
        raise SystemExit("2a4fe4 needle missing (lift shape changed?)")
    cs_decls = re.findall(
        r"uint64_t\s+_cs_(\d+)\s*=\s*ctx->gpr\[(\d+)\]\s*;", m.group("cs") or ""
    )
    cs_block = "".join(f"        uint64_t _cs_{a} = ctx->gpr[{b}];\n" for a, b in cs_decls)
    return t[: m.start()] + insert.replace("@@CS@@", cs_block) + t[m.end() :]


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2/ppu_recomp_001.cpp")
    rc = 0
    found = False
    for p in paths:
        if not p.exists():
            print(f"skip {p}")
            continue
        t = p.read_text()
        # A funcao vive num so' chunk: nos restantes isto e' skip, nao falha.
        if MARKER not in t and not HEADER_RE.search(t):
            print(f"skip {p} (func_002A4FE4 nao esta' neste chunk)")
            continue
        found = True
        try:
            t2 = patch(t)
        except SystemExit as e:
            print(f"FAILED {p}: {e}")
            rc = 1
            continue
        if t2 != t:
            p.write_text(t2, newline="\n")
            print(f"APPLIED {p}")
        else:
            print(f"ALREADY-APPLIED {p}")
    if not found:
        print("FAILED: func_002A4FE4 ausente de todos os chunks")
        rc = 1
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
