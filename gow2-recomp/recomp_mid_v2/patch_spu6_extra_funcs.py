#!/usr/bin/env python3
"""Lift GoW2's SCREAM policy module (spu6) at its real local-store base, 0xA00.

The module is a raw LS image (11520 B at EBOOT vaddr 0x4FD980, fp
0xCEDB9A67A0C3A305). The SPURS kernel copies a policy module to LS 0xA00 and
enters it there (RPCS3 spursKernelDispatchWorkload: memcpy(0xA00, ...),
pc = 0xA00). Until 2026-09-23 it was lifted and loaded at 0x3000, which only
works for relative branches. Measured with base 0xA00 the image's own data
lines up: 0x12F0 starts at -1, 0x1300 at 2, 0x1540 holds the module's stack
pointer 0x3F990. At 0x3000 the absolute continuations the mixer keeps in r0
(`ila r0, 0x2CF8` ... `ila r2, 0x2D38; bi r2`, a jump into its own code at
0xA00) landed in empty LS below the image, so every mixer pass ended there
and never mixed. The workarounds that base produced are gone with it: the
stack pointer forced into 0x1540, the kernel exit at 0x2D38, and a source
edit that made 0x4D20 load 0x13F0 instead of 0x14E0 (which PUT 0xFF over the
command-queue cursors at 0x8862C0).

Usage: patch_spu6_extra_funcs.py [image.bin] [out_dir]   (defaults: the local
dump spu_miss_cedb9a67a0c3a305.bin -> spu_lifted/spu6_v2). Game bytes stay local.
"""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
INPUT = ROOT / "spu_miss_cedb9a67a0c3a305.bin"
OUTPUT = ROOT / "spu_lifted" / "spu6_v2"
LIFTER = ROOT.parent / "ps3recomp" / "tools" / "spu_lifter.py"
BASE = 0xA00
END = BASE + 0x2D00

# Function boundaries: those of the previous lift (base 0x3000) moved to 0xA00,
# plus the continuations the mixer reaches through `bi` on an absolute address.
STARTS = sorted({
    0x0A00, 0x0A90, 0x1600, 0x16E0, 0x1B78, 0x2320, 0x2330, 0x2360, 0x2418,
    0x25D0, 0x2660, 0x2664, 0x2668, 0x2698, 0x26C8, 0x2720, 0x2808, 0x28C4,
    0x28F0, 0x2930, 0x2998, 0x2A28, 0x2B20, 0x2BF0, 0x2C58, 0x2C60, 0x2D68,
    0x2DB8, 0x2DF8, 0x2E78, 0x2EE0, 0x2F30, 0x3038, 0x31C8, 0x3268, 0x3320,
    0x3568, 0x3578, 0x3588, 0x35A0, 0x35E0, 0x3670,
    # continuations (ila rX, addr ... bi rX)
    0x2C68, 0x2C78, 0x2C88, 0x2CA8, 0x2CF0, 0x2CF8, 0x2D08, 0x2D38, 0x2D40,
    0x2D58, 0x2CE0, 0x2D30,
    # interrupt handler (LS 0 holds `bra 0xA2C`, written by the module)
    0x0A2C,
    # job-command handlers (table at LS 0x15B0, dispatched by `bi r6` at 0x2E74)
    0x1BE0, 0x1CB8, 0x21E8, 0x2288, 0x22D4, 0x2310, 0x1B9C, 0x2318,
    # every other code address the module loads with `ila` and branches to
    # (interrupt windows like 0x2320: `ila r74,0x2328; bie r74`)
    0x1718, 0x1A78, 0x1AE0, 0x1B50, 0x1B60, 0x2328, 0x2410, 0x2928, 0x2EB4,
    0x331C, 0x34F0, 0x3504, 0x3518, 0x352C, 0x3530,
    # services the module offers the job it runs (called through pointers)
    0x1700, 0x1770, 0x17D0, 0x17F0, 0x1AE8, 0x1B08,
})


def lift(image: pathlib.Path, out: pathlib.Path, lifter: pathlib.Path) -> None:
    bounds = [{"start": hex(a), "end": hex(b)}
              for a, b in zip(STARTS, STARTS[1:] + [END])]
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
        json.dump(bounds, f)
        bounds_path = pathlib.Path(f.name)
    try:
        subprocess.run([sys.executable, str(lifter), str(image), "--base", hex(BASE),
                        "--functions", str(bounds_path), "--symbol-prefix", "spu6_",
                        "--output", str(out)], check=True)
    finally:
        bounds_path.unlink(missing_ok=True)

    path = out / "spu_recomp.c"
    source = path.read_text()
    # 0x3FEC0 is a no-op helper outside the image; keep it an empty function
    # instead of an unregistered indirect stub.
    source = source.replace(
        "void spu6_spu_func_0003FEC0(spu_context* ctx) {\n"
        "    ctx->pc = 0x3FEC0u; spu_indirect_branch(ctx);\n"
        "}\n",
        "void spu6_spu_func_0003FEC0(spu_context* ctx) { (void)ctx; }\n",
    )
    # The 0x2330 helper is called with its link in r4 (not r0): its final
    # `bi r4` is a C return to the brsl caller, not a jump to a mid-function PC.
    begin = source.index("void spu6_spu_func_00002330")
    end = source.index("void spu6_spu_func_00002360", begin)
    helper = source[begin:end]
    old_return = "ctx->pc = ctx->gpr[4]._u32[0]; spu_indirect_branch(ctx); return;"
    if helper.count(old_return) != 1:
        raise RuntimeError("unexpected SPU6 0x2330 return shape")
    source = source[:begin] + helper.replace(old_return, "return;") + source[end:]
    path.write_text(source)


def main() -> int:
    image = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else INPUT
    out = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else OUTPUT
    if not image.is_file():
        print(f"missing SPU6 image: {image}", file=sys.stderr)
        return 1
    out.mkdir(parents=True, exist_ok=True)
    lift(image, out, LIFTER)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
