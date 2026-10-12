#!/usr/bin/env python3
"""Keep the Metal ring-usage diagnostic valid before the first frame region is set."""

from pathlib import Path

TARGET = Path(__file__).resolve().parents[2] / "ps3recomp" / "libs" / "video" / "rsx_metal_backend.m"
MARKER = "E486: guard VARACE region underflow"
OLD = """        const u32 va_u = s_draw.va_offset - metal_va_base_f4();
        const u32 vb_u = s_draw.vb_offset - metal_vb_base_bytes();
        const u32 ib_u = s_draw.ib_offset - metal_ib_base_idx() * 4u;"""
NEW = """        /* {marker}: the first diagnostic can run before begin_frame has
         * initialized offsets for the active region; do not wrap unsigned usage. */
        const u32 va_base = metal_va_base_f4();
        const u32 vb_base = metal_vb_base_bytes();
        const u32 ib_base = metal_ib_base_idx() * 4u;
        const u32 va_u = s_draw.va_offset >= va_base ? s_draw.va_offset - va_base : 0u;
        const u32 vb_u = s_draw.vb_offset >= vb_base ? s_draw.vb_offset - vb_base : 0u;
        const u32 ib_u = s_draw.ib_offset >= ib_base ? s_draw.ib_offset - ib_base : 0u;""".format(marker=MARKER)

def main() -> int:
    text = TARGET.read_text()
    if MARKER in text:
        print("[metal-varace] already applied")
        return 0
    if text.count(OLD) != 1:
        print(f"[metal-varace] expected one match, got {text.count(OLD)}")
        return 2
    TARGET.write_text(text.replace(OLD, NEW, 1))
    print("[metal-varace] applied")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
