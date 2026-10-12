#!/usr/bin/env python3
"""msl_census.py STAMP -- census of MSL pairs dumped after STAMP (PS3_METAL_DUMP_MSL=1).
Character shaders = FPs paired with a skinned VP (relative constant addressing a0/a1).
Also reports how each FP sets its output alpha (translucency with SRC_ALPHA blending)."""
import glob
import os
import re
import sys


def main():
    if len(sys.argv) != 2 or not os.path.isfile(sys.argv[1]):
        sys.exit("usage: msl_census.py STAMP_FILE")
    stamp = os.path.getmtime(sys.argv[1])
    fps = {}
    for f in sorted(glob.glob("msl_fp*_vp*.metal")):
        try:
            if os.path.getmtime(f) < stamp:
                continue
            with open(f, encoding="utf-8", errors="replace") as fh:
                src = fh.read()
        except OSError as exc:
            print(f"skip {f}: {exc}", file=sys.stderr)
            continue
        m = re.search(r"msl_fp([0-9A-F]+)_vp", f)
        if not m:
            continue
        fp_part, _, vp_part = src.partition("// ---- vertex ----")
        skinned = bool(re.search(r"vp_c\[\(\d+u \+ uint\(a[01]\.", vp_part))
        e = fps.setdefault(m.group(1), {"pairs": 0, "skinned": 0, "file": f})
        e["pairs"] += 1
        if skinned:
            e["skinned"] += 1
            e["file"] = f
        body = fp_part.split("fs_main", 1)[-1]
        e["tex"] = sorted(set(re.findall(r"rsx_tex\[(\d+)\]", body)))
        e["fc"] = len(re.findall(r"rsx_fc\[", body))
        rets = re.findall(r"return ([a-z_]+\[?\d*\]?)", body)
        e["ret"] = rets[-1] if rets else "?"
        e["alpha_w"] = len(re.findall(r"\[0\]\.w\s*=|\[0\]\.\w*w\w*\s*=", body))
        e["inputs"] = sorted(set(re.findall(r"input\.(\w+)", body)))
    print("fp        pairs skin tex     fc ret      a0w inputs")
    for fph, e in sorted(fps.items(), key=lambda kv: (-kv[1]["skinned"], -kv[1]["pairs"])):
        print(f"{fph:9} {e['pairs']:5} {e['skinned']:4} {','.join(e['tex']) or '-':7} "
              f"{e['fc']:2} {e['ret']:8} {e['alpha_w']:3} {','.join(e['inputs'])}")


if __name__ == "__main__":
    main()
