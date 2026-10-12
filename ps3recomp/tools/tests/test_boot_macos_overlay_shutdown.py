#!/usr/bin/env python3
"""Source guard: games/gow2/boot_macos.cpp (not built by CMake) shuts the
runtime overlay down on the orderly exit path, before the backend tears down,
and metal_quit_now's _exit path stays free of the (blocking) shutdown."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def strip_comments(t):
    return re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", "", t, flags=re.S))


def main():
    fails = []
    boot = strip_comments((ROOT / "games/gow2/boot_macos.cpp").read_text(encoding="utf-8"))
    run = boot.find("ppu_run(entry")
    tail = boot[run:] if run >= 0 else ""
    sd = tail.find("rsx_overlay_shutdown();")
    be = tail.find("backend_shutdown(g_backend);")
    if run < 0 or be < 0:
        fails.append("orderly exit after ppu_run not found")
    elif sd < 0 or sd > be:
        fails.append("rsx_overlay_shutdown() must run after ppu_run and before backend_shutdown")
    metal = strip_comments((ROOT / "libs/video/rsx_metal_backend.m").read_text(encoding="utf-8"))
    m = re.search(r"static void metal_quit_now\([^)]*\)\s*\{(.*?)\n\}", metal, flags=re.S)
    if not m:
        fails.append("metal_quit_now not found")
    elif "rsx_overlay_shutdown" in m.group(1) or "_exit(" not in m.group(1):
        fails.append("metal_quit_now must stay a plain _exit (no overlay shutdown)")
    for f in fails:
        print("FAIL:", f)
    print("PASS" if not fails else f"FAIL {len(fails)}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
