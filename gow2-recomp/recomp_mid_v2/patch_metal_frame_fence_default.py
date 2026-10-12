#!/usr/bin/env python3
"""Make the Metal frame fence safe by default while preserving the A/B override."""

from pathlib import Path

TARGET = Path(__file__).resolve().parents[2] / "ps3recomp" / "libs" / "video" / "rsx_metal_backend.m"
MARKER = "E486: frame fence defaults safe"
OLD = 'if (on < 0) on = metal_env_on("PS3_METAL_FRAME_FENCE");'
NEW = f'/* {MARKER}: PS3_METAL_FRAME_FENCE=0 keeps the unsafe A/B path. */\n    if (on < 0) {{ const char* e = getenv("PS3_METAL_FRAME_FENCE"); on = !(e && e[0] == \'0\'); }}'

def main() -> int:
    text = TARGET.read_text()
    if MARKER in text:
        print("[metal-frame-fence] already applied")
        return 0
    if text.count(OLD) != 1:
        print(f"[metal-frame-fence] expected one match, got {text.count(OLD)}")
        return 2
    TARGET.write_text(text.replace(OLD, NEW, 1))
    print("[metal-frame-fence] applied")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
