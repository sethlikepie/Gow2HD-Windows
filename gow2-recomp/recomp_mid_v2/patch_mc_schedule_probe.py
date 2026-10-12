#!/usr/bin/env python3
"""
Entry probes for micro-ctor schedule cluster (A1 schedule climb).

Root: func_0032854C — OPD 0x00534CB8, ZERO direct callers in lift; only
reachable via OPD/bctr. Branches to 00328B08 / 00328978 / then 00329490.

Gated PS3_TRACE_A1CHAIN (or PS3_TRACE_MC=1). OFF by default.
Idempotent.
"""
from pathlib import Path
import re
import sys

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"

PROBE = (
    '        {{ static int on=-1; if(on<0){{extern char* getenv(const char*);'
    ' const char* e=getenv("PS3_TRACE_A1CHAIN"); const char* e2=getenv("PS3_TRACE_MC");'
    ' on=((e&&*e&&*e!=\'0\')||(e2&&*e2&&*e2!=\'0\'))?1:0;}}'
    ' if(on){{ static int n=0; if(n++<48)'
    ' fprintf(stderr,"[{tag}] #%d lr=0x%08X r3=0x%08X r4=0x%08X r5=0x%08X\\n",'
    ' n,(uint32_t)ctx->lr,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[4],'
    ' (uint32_t)ctx->gpr[5]); fflush(stderr);}} }}\n'
)

# (file, func_name, tag)
TARGETS = [
    ("ppu_recomp_001.cpp", "func_0032854C", "MC-32854C"),
    ("ppu_recomp_003.cpp", "func_00328B08", "MC-328B08"),
    ("ppu_recomp_003.cpp", "func_00328978", "MC-328978"),
    ("ppu_recomp_001.cpp", "func_00328B18", "MC-328B18"),
    ("ppu_recomp_001.cpp", "func_00329490", "MC-329490"),
    ("ppu_recomp_003.cpp", "func_00328860", "MC-328860"),
    ("ppu_recomp_003.cpp", "func_00328E50", "MC-328E50"),
]


def patch_func(path: Path, fname: str, tag: str) -> str:
    s = path.read_text(encoding="utf-8", errors="replace")
    if f"[{tag}]" in s:
        return "already"
    # Match: void func_XXX(ppu_context* ctx) {\n
    pat = re.compile(
        rf"(void {re.escape(fname)}\(ppu_context\* ctx\) \{{\n)",
        re.M,
    )
    m = pat.search(s)
    if not m:
        return "missing"
    insert = m.group(1) + PROBE.format(tag=tag)
    s = s[: m.start()] + insert + s[m.end() :]
    path.write_text(s, encoding="utf-8")
    return "ok"


def main() -> None:
    if not ROOT.is_dir():
        print(f"lift dir missing: {ROOT}", file=sys.stderr)
        sys.exit(1)
    for fn, name, tag in TARGETS:
        p = ROOT / fn
        if not p.is_file():
            print(f"{name}: no file {fn}")
            continue
        r = patch_func(p, name, tag)
        print(f"{tag}/{name}: {r}")


if __name__ == "__main__":
    main()
