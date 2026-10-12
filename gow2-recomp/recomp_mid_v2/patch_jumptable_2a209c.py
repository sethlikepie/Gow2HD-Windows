#!/usr/bin/env python3
"""Materialize PPC switch jump-table in func_002A209C (chunk 001).

Same class of lifter bug as Task4 func_002B11B8: table left as TODO .word,
dispatch via TOC-0x1694 + bctr. Byte opcode switch 0..0x39 (58 cases).
Guest table base 0x2A2134 (matches TOC-0x1694 and ELF).

This is on the stream byte-walker path (reads u8, advances cursor) — likely
the WAD/group content dispatcher that never reaches ICGLdr.
"""
from __future__ import annotations
from pathlib import Path
import re
import sys
from lift_paths import resolve_lift_paths

# CORRECCAO 2026-07-25 (re-lift):
#  a) chunk-fixo: abria sempre ROOT/"ppu_recomp_001.cpp"; o lift passou de 31
#     para 7 chunks e a funcao pode migrar. Passa por resolve_lift_paths (aceita
#     directorio) e procura o chunk que TEM func_002A209C.
#  b) o lifter actual JA' materializa esta jump table sozinho: em vez de
#     "ctx->ctr = ...; ps3_indirect_call(ctx); return;" emite um
#     "switch ((uint32_t)ctx->ctr) { case 0x002A221Cu: goto loc_002A221C; ... }"
#     com os 54 alvos distintos (as 58 entradas tem 4 repetidas). A agulha
#     literal antiga deixou de existir.
#     NAO se enfraquece nada: em vez de assumir, VERIFICA-SE que o switch do
#     lifter cobre exactamente os mesmos 58 alvos (0x2A2134 + offset) que este
#     patch instalaria. So' se a verificacao falhar e' que se injecta o
#     dispatch a' mao (caminho legado, agora por regex tolerante ao cast
#     (uint32_t)/(uint64_t) do ppc_rlwinm).
MARKER = "Task5 FIX: PPC switch jump-table 2A209C"
FUNC_SIG = "void func_002A209C(ppu_context* ctx) {"
JT_BASE = 0x002A2134

OLD = """        if (((ctx->cr >> 0) & 4)) goto loc_002A2238;
        ctx->gpr[11] = vm_read32(ctx->gpr[2] + -0x1694);
        ctx->gpr[9] = (uint32_t)ppc_rlwinm((uint32_t)ctx->gpr[9], 2, 22, 29);
        ctx->gpr[0] = vm_read32((ctx->gpr[9] + ctx->gpr[11]));
        ctx->gpr[0] = (int64_t)(int32_t)ctx->gpr[0];
        ctx->gpr[0] = ctx->gpr[0] + ctx->gpr[11];
        ctx->ctr = (uint32_t)ctx->gpr[0];
        ps3_indirect_call(ctx); return;
        /* TODO: .word 0x00000688 */;
        /* TODO: .word 0x00000524 */;
        /* TODO: .word 0x000000E8 */;
        /* TODO: .word 0x000012C8 */;
        /* TODO: .word 0x000012E8 */;
        /* TODO: .word 0x000011B8 */;
        /* TODO: .word 0x000011D8 */;
        /* TODO: .word 0x000011D8 */;
        /* TODO: .word 0x000011D8 */;
        /* TODO: .word 0x000011D8 */;
        /* TODO: .word 0x00001214 */;
        /* TODO: .word 0x0000126C */;
        /* TODO: .word 0x000008DC */;
        /* TODO: .word 0x00000938 */;
        /* TODO: .word 0x00000994 */;
        /* TODO: .word 0x000009F4 */;
        /* TODO: .word 0x00000A4C */;
        /* TODO: .word 0x00000A74 */;
        /* TODO: .word 0x0000112C */;
        /* TODO: .word 0x00001168 */;
        /* TODO: .word 0x00000AB0 */;
        /* TODO: .word 0x00000B08 */;
        /* TODO: .word 0x00000320 */;
        /* TODO: .word 0x00000394 */;
        /* TODO: .word 0x00000400 */;
        /* TODO: .word 0x0000045C */;
        /* TODO: .word 0x000004C8 */;
        /* TODO: .word 0x00001514 */;
        /* TODO: .word 0x00000DFC */;
        /* TODO: .word 0x00000E74 */;
        /* TODO: .word 0x00000EE4 */;
        /* TODO: .word 0x00000F48 */;
        /* TODO: .word 0x00000FA4 */;
        /* TODO: .word 0x0000100C */;
        /* TODO: .word 0x00001068 */;
        /* TODO: .word 0x000010C4 */;
        /* TODO: .word 0x00000C10 */;
        /* TODO: .word 0x00000C70 */;
        /* TODO: .word 0x00000CF8 */;
        /* TODO: .word 0x00000D74 */;
        /* TODO: .word 0x00000B74 */;
        /* TODO: .word 0x00000BC4 */;
        /* TODO: .word 0x000002A8 */;
        /* TODO: .word 0x00000818 */;
        /* TODO: .word 0x0000088C */;
        /* TODO: .word 0x00001308 */;
        /* TODO: .word 0x00001374 */;
        /* TODO: .word 0x000013C0 */;
        /* TODO: .word 0x0000140C */;
        /* TODO: .word 0x00001478 */;
        /* TODO: .word 0x000014C4 */;
        /* TODO: .word 0x0000060C */;
        /* TODO: .word 0x00000228 */;
        /* TODO: .word 0x00000594 */;
        /* TODO: .word 0x00000228 */;
        /* TODO: .word 0x000006F8 */;
        /* TODO: .word 0x00000780 */;
        /* TODO: .word 0x000007B4 */;
        ctx->gpr[31] = (int64_t)(int32_t)(ctx->gpr[1] + 0x70);"""

NEW = """        if (((ctx->cr >> 0) & 4)) goto loc_002A2238;
        /* Task5 FIX: PPC switch jump-table 2A209C was left as TODO .word by the
         * lifter; TOC-0x1694 + bctr never hits real case labels. Materialize the
         * original 58 offsets (base guest 0x2A2134 = first .word = TOC-0x1694).
         * Byte opcodes 0..0x39 inclusive. */
        {
          static const uint32_t k_jt[0x3A] = {
            0x00000688, 0x00000524, 0x000000E8, 0x000012C8,
            0x000012E8, 0x000011B8, 0x000011D8, 0x000011D8,
            0x000011D8, 0x000011D8, 0x00001214, 0x0000126C,
            0x000008DC, 0x00000938, 0x00000994, 0x000009F4,
            0x00000A4C, 0x00000A74, 0x0000112C, 0x00001168,
            0x00000AB0, 0x00000B08, 0x00000320, 0x00000394,
            0x00000400, 0x0000045C, 0x000004C8, 0x00001514,
            0x00000DFC, 0x00000E74, 0x00000EE4, 0x00000F48,
            0x00000FA4, 0x0000100C, 0x00001068, 0x000010C4,
            0x00000C10, 0x00000C70, 0x00000CF8, 0x00000D74,
            0x00000B74, 0x00000BC4, 0x000002A8, 0x00000818,
            0x0000088C, 0x00001308, 0x00001374, 0x000013C0,
            0x0000140C, 0x00001478, 0x000014C4, 0x0000060C,
            0x00000228, 0x00000594, 0x00000228, 0x000006F8,
            0x00000780, 0x000007B4
          };
          const uint32_t op = (uint32_t)ctx->gpr[9];
          const uint32_t base = 0x002A2134u;
          uint32_t tgt = base + k_jt[op < 0x3Au ? op : 0];
          { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
            if(on){ static int n=0; if(n++<80)
              fprintf(stderr,"[OPDISP] op=%u -> 0x%08X\\n", op, tgt); } }
          ctx->ctr = tgt;
          ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
          return;
        }
        ctx->gpr[31] = (int64_t)(int32_t)(ctx->gpr[1] + 0x70);"""

# Os 58 offsets vem das proprias linhas ".word" da agulha antiga -- uma unica
# fonte de verdade, tanto para injectar como para verificar o switch do lifter.
JT_OFFSETS = [int(w, 16) for w in re.findall(r"\.word (0x[0-9A-Fa-f]{8})", OLD)]

# Caminho legado: mesma extensao exacta que a agulha literal OLD cobria (ate' a'
# linha do gpr[31] inclusive, que NEW volta a emitir), so' que tolerante ao cast
# do ppc_rlwinm -- (uint32_t) no lift antigo, (uint64_t) no actual.
LEGACY_RE = re.compile(
    r"        if \(\(\(ctx->cr >> 0\) & 4\)\) goto loc_002A2238;\n"
    r"        ctx->gpr\[11\] = vm_read32\(ctx->gpr\[2\] \+ -0x1694\);\n"
    r"        ctx->gpr\[9\] = \((?:uint32_t|uint64_t)\)ppc_rlwinm\(\(uint32_t\)ctx->gpr\[9\], 2, 22, 29\);\n"
    r"        ctx->gpr\[0\] = vm_read32\(\(ctx->gpr\[9\] \+ ctx->gpr\[11\]\)\);\n"
    r"        ctx->gpr\[0\] = \(int64_t\)\(int32_t\)ctx->gpr\[0\];\n"
    r"        ctx->gpr\[0\] = ctx->gpr\[0\] \+ ctx->gpr\[11\];\n"
    r"        ctx->ctr = \(uint32_t\)ctx->gpr\[0\];\n"
    r"        ps3_indirect_call\(ctx\); return;\n"
    r"(?:        /\* TODO: \.word 0x[0-9A-Fa-f]{8} \*/;\n)+"
    r"        ctx->gpr\[31\] = \(int64_t\)\(int32_t\)\(ctx->gpr\[1\] \+ 0x70\);"
)


def func_body(s: str) -> str | None:
    """Corpo de func_002A209C (ate' a' funcao seguinte), ou None."""
    i = s.find(FUNC_SIG)
    if i < 0:
        return None
    j = s.find("\nvoid func_", i + len(FUNC_SIG))
    return s[i:] if j < 0 else s[i:j]


def lifter_already_resolved(body: str) -> tuple[bool, int, int]:
    """O switch emitido pelo lifter cobre os mesmos alvos que este patch poria?"""
    targets = sorted({JT_BASE + off for off in JT_OFFSETS})
    hit = sum(1 for t in targets
              if f"case 0x{t:08X}u: goto loc_{t:08X};" in body)
    return hit == len(targets), hit, len(targets)


def main() -> None:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2/ppu_recomp_001.cpp")
    for p in paths:
        if not p.exists():
            continue
        s = p.read_text(encoding="utf-8", errors="replace")
        body = func_body(s)
        if body is None:
            continue
        if MARKER in body:
            print(f"OK: already patched ({p.name})")
            return
        ok, hit, tot = lifter_already_resolved(body)
        if ok:
            print(f"OK: jump table ja' materializada pelo proprio lifter em "
                  f"{p.name} -- switch cobre {hit}/{tot} alvos 0x2A2134+offset; "
                  f"nada a injectar")
            return
        m = LEGACY_RE.search(body)
        if not m:
            print(f"FAILED {p.name}: func_002A209C presente mas nem o switch do "
                  f"lifter cobre os alvos ({hit}/{tot}) nem a forma legada "
                  f"(ctr + ps3_indirect_call + .word) foi encontrada")
            raise SystemExit(1)
        new_body = body[:m.start()] + NEW + body[m.end():]
        p.write_text(s.replace(body, new_body, 1), encoding="utf-8", newline="\n")
        print(f"OK: patched func_002A209C jump table ({p.name})")
        return
    print("FAILED: func_002A209C nao encontrada em nenhum chunk")
    raise SystemExit(1)

if __name__ == "__main__":
    main()
