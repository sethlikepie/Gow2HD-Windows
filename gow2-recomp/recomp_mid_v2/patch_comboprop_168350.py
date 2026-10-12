#!/usr/bin/env python3
"""PS3_TRACE_COMBOPROP -- dump material props at CGOWShader combination build.

WHY
---
Shader-bringup Task 2 (.superpowers/sdd/shader-task-2-brief.md). The guest
spams "Invalid shader combination: .../Default.ps3fx (TEXTURE=0;CONSTCOLOR=1;
HASCOLOR=0;TRANSFORM=0)" via its own CGOWShader. The combination string is
built at a shared epilogue (labelled `loc_00168440` in the original guest
disassembly) that loads r3 = format pointer (TOC-0x3ACC), r4/r5 = two derived
0/1 flags, r6 = vm_read8(mat+0x90), r7 = vm_read8(mat+0x99), then calls
func_001F2AD4 (the guest sprintf-alike). This probe dumps, read-only, exactly
what the guest itself reads/computes at that call site.

MULTI-SITE, NOT A SINGLE FUNCTION
----------------------------------
First cut of this probe targeted only func_00168350 (ppu_recomp_000.cpp,
where the *top-level* combination builder lives; its own `mode = mat+0x94`
switch has an inline mode>=4 fallthrough that reaches loc_00168440 directly).
Boot A measurement with THAT single site produced 0 [COMBOPROP] hits while
"Invalid shader combination" fired 23425x in the same run -- i.e. the single
site was not the live one for this material's mode.

Reading func_00168350's body shows mode 1/2/3 (and the mode<1 case) instead
trampoline to func_00168538 / func_0016872C / func_001685E0 / func_00168670.
Those, in turn (confirmed by reading ppu_recomp_002.cpp), land in one of FIVE
sibling functions -- func_001683C4, func_001683D8, func_001683DC,
func_001683F8, func_00168418 -- which are byte-for-byte the same tail code as
func_00168350's own mode>=4 continuation (same mat+0x98/+0x7C/+0x78 reads,
same TOC-0x3AE0 global, same `loc_00168440:` label, same call into
func_001F2AD4), just entered with different subsets of registers already
live. This is the lifter's standard function-boundary-per-branch-target
behaviour applied to what is, in the original PS3 binary, a single routine
with a shared epilogue reached from multiple internal entry points -- the
same structural class of issue flagged in ps3recomp/CLAUDE.md as "auditoria
sistemática do fallthrough cross-fragment" (the `func_002550C8` precedent).
It is NOT a lifter bug to fix here -- diagnosis only: all 6 sites are
legitimate copies of the same guest logic and are all instrumented so the
measurement lands on whichever one is actually live.

Anchor confirmed unique to real combination-builder tails file-wide: the
5-line "-0x3ACC format load + rlwinm bit-extract + func_001F2AD4 call" block
occurs in exactly 6 places across all 31 ppu_recomp_*.cpp chunks (grep-swept
000..030): 1x in ppu_recomp_000.cpp (func_00168350) and 5x in
ppu_recomp_002.cpp (the siblings above). No collisions elsewhere.

Each inserted probe is tagged with its own enclosing function name
(`site=func_XXXXXXXX`) so the log shows exactly which entry point is live for
the "Default.ps3fx" spam, discriminating:
  - fmt==0 everywhere                    -> H0 (format pointer dead)
  - fmt==0x4C61B0 + coherent props        -> real reads, not resolve garbage;
                                             H3 weakened
  - props don't match the digits printed  -> H3 (sprintf/arg-order/corruption)

Gated by PS3_TRACE_COMBOPROP, OFF by default. Read-only (fprintf of vm_read*
values only -- no vm_write, no forged CRC/registry, no early-arm re-enable).
Idempotent (marker COMBOPROP-PROBE, checked per file).
"""
from pathlib import Path
import re
import sys

MARKER = "COMBOPROP-PROBE"

ROOT = (Path(sys.argv[1]) if len(sys.argv) > 1
        else Path(__file__).resolve().parent.parent / "recomp_macos_v2")

# The tail of the TEXTURE-slot bit computation immediately followed by the
# sprintf-alike call. Verified unique to genuine loc_00168440 combination
# epilogues (6 occurrences file-wide across all chunks; see module docstring).
#
# CORRECCAO 2026-07-25 (shape-LR + shape-outro): a agulha era literal e o
# lifter mudou DUAS coisas nestas 5 linhas --
#   shape-outro: o cast do rlwinm passou de "(uint32_t)ppc_rlwinm(" para
#                "(uint64_t)ppc_rlwinm(" (o valor de 32 bits passa a ser
#                promovido em 64 bits antes de ir para o gpr);
#   shape-LR   : as chamadas passaram a ter prefixo de link register --
#                "ctx->lr = 0x00168468; func_001F2AD4(ctx); DRAIN..." em vez de
#                "func_001F2AD4(ctx); DRAIN...", e ganharam um "/* nop */;" a
#                seguir (delay-slot do bl).
# Passa a regex tolerante: aceita os dois casts e o prefixo ctx->lr opcional,
# por isso casa com o lift antigo E com o novo. O ponto de insercao continua a
# ser exactamente entre o bloco de 4 linhas e a linha de chamada, e a chamada e'
# re-emitida tal e qual foi encontrada (o ctx->lr do lift novo e' preservado).
PREFIX_RE = (
    r"        ctx->gpr\[3\] = vm_read32\(ctx->gpr\[2\] \+ -0x3ACC\);\n"
    r"        ctx->gpr\[4\] = ctx->gpr\[9\] - ctx->gpr\[4\];\n"
    r"        ctx->gpr\[4\] = \((?:uint32_t|uint64_t)\)ppc_rlwinm"
    r"\(\(uint32_t\)ctx->gpr\[4\], 1, 31, 31\);\n"
    r"        ctx->gpr\[4\] = \(int64_t\)\(int32_t\)ctx->gpr\[4\];\n"
)
CALL_RE = (
    r"(        (?:ctx->lr = 0x[0-9A-Fa-f]+; )?"
    r"func_001F2AD4\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n)"
)
NEEDLE_RE = re.compile(PREFIX_RE + CALL_RE)

FUNC_RE = re.compile(r"^void (func_[0-9A-Fa-f]+)\(ppu_context\* ctx\) \{", re.MULTILINE)


def probe_for(site: str) -> str:
    """Read-only probe block, site name baked in as a compile-time literal
    (no extra runtime arg needed)."""
    return (
        "        /* COMBOPROP-PROBE: read-only dump of material props feeding the\n"
        "           CGOWShader \"TEXTURE=..;CONSTCOLOR=..;HASCOLOR=..;TRANSFORM=..\"\n"
        "           combination string, right before the guest sprintf-alike call.\n"
        "           No vm_write. Gated PS3_TRACE_COMBOPROP, OFF by default, cap 32. */\n"
        "        { static int _on=-1; if(_on<0){extern char* getenv(const char*);\n"
        "            const char* _e=getenv(\"PS3_TRACE_COMBOPROP\"); _on=(_e&&*_e&&*_e!='0')?1:0;}\n"
        "          if(_on){ static int _n=0; if(_n++<32){\n"
        "            uint64_t mat=ctx->gpr[29];\n"
        "            uint32_t fmt=vm_read32(ctx->gpr[2] + -0x3ACC);\n"
        "            uint32_t g3AE0=vm_read32(ctx->gpr[2] + -0x3AE0);\n"
        "            uint8_t  t90=mat?vm_read8(mat+0x90):0xFF;\n"
        "            uint8_t  h99=mat?vm_read8(mat+0x99):0xFF;\n"
        "            uint32_t w78=mat?vm_read32(mat+0x78):0;\n"
        "            uint32_t mode94=mat?vm_read32(mat+0x94):0;\n"
        "            uint8_t  gate7c=mat?vm_read8(mat+0x7C):0xFF;\n"
        "            fprintf(stderr,\n"
        "              \"[COMBOPROP] #%d site=" + site + " mat=0x%08X fmt=0x%08X t90(+0x90)=%u h99(+0x99)=%u w78(+0x78)=0x%08X mode94(+0x94)=0x%08X gate7c(+0x7C)=%u g3AE0=0x%08X args(a1,a2,a3,a4)=(%d,%d,%d,%d)\\n\",\n"
        "              _n, (uint32_t)mat, fmt, (unsigned)t90, (unsigned)h99, w78, mode94, (unsigned)gate7c, g3AE0,\n"
        "              (int32_t)ctx->gpr[4], (int32_t)ctx->gpr[5], (int32_t)ctx->gpr[6], (int32_t)ctx->gpr[7]);\n"
        "            fflush(stderr);\n"
        "          } }\n"
        "        }\n"
    )


def enclosing_func(text: str, idx: int) -> str:
    last = None
    for m in FUNC_RE.finditer(text, 0, idx):
        last = m.group(1)
    return last or "UNKNOWN"


def patch_file(p: Path):
    """Returns (status, n_sites)."""
    t = p.read_text(encoding="utf-8", errors="replace")
    if MARKER in t:
        return "ALREADY", 0
    if not NEEDLE_RE.search(t):
        return "SKIP", 0
    out = []
    pos = 0
    n_sites = 0
    for m in NEEDLE_RE.finditer(t):
        site = enclosing_func(t, m.start())
        # tudo ate' ao fim do bloco de 4 linhas (= inicio da linha de chamada)
        out.append(t[pos:m.start(1)])
        out.append(probe_for(site))
        out.append(m.group(1))  # linha de chamada como o lift a escreveu
        pos = m.end()
        n_sites += 1
    out.append(t[pos:])
    p.write_text("".join(out), encoding="utf-8")
    return "APPLIED", n_sites


def main() -> int:
    files = sorted(ROOT.glob("ppu_recomp_*.cpp"))
    if not files:
        print("nenhum ppu_recomp_*.cpp em %s" % ROOT)
        return 1
    any_hit = False
    total_sites = 0
    for p in files:
        status, n_sites = patch_file(p)
        if status != "SKIP":
            print("%s: %s (%d site(s))" % (p.name, status, n_sites))
        if status in ("APPLIED", "ALREADY"):
            any_hit = True
        total_sites += n_sites
    if not any_hit:
        print("SKIP: needle not found anywhere under %s" % ROOT)
        return 1
    print("total sites patched this run: %d" % total_sites)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
