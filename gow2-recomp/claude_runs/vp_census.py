#!/usr/bin/env python3
"""vp_census.py -- static census of the dumped vertex programs (msl_fp*_vp*.metal).

For each distinct VP hash: whether it skins (relative constant reads through a0),
which address-register lanes it uses (bone count), the vertex inputs it reads,
the statement that writes TEXCOORD1 (o[8]) and the FPs it is paired with.
Run from the gow2-recomp directory. Read-only."""
import glob
import os
import re
import sys


def main():
    files = sorted(glob.glob("msl_fp*_vp*.metal"))
    if not files:
        sys.exit("no msl_fp*_vp*.metal files in the current directory")
    vps = {}
    for f in files:
        m = re.match(r"msl_fp([0-9A-F]{8})_vp([0-9A-F]{8})\.metal$", os.path.basename(f))
        if not m:
            continue
        fph, vph = m.group(1), m.group(2)
        try:
            with open(f, encoding="utf-8", errors="replace") as fh:
                src = fh.read()
        except OSError as exc:
            print(f"skip {f}: {exc}", file=sys.stderr)
            continue
        _, _, vp = src.partition("// ---- vertex ----")
        body = vp.split("for (int _j=0;_j<32;_j++) o[_j]=float4(0);", 1)[-1]
        e = vps.setdefault(vph, {"fps": set(), "body": body})
        e["fps"].add(fph)
    rows = []
    for vph, e in vps.items():
        body = e["body"]
        lanes = sorted(set(re.findall(r"uint\(a0\.([xyzw])\)", body)))
        inputs = sorted(set(int(x) for x in re.findall(r"\bv\[(\d+)\]", body)))
        tc1 = [ln.strip() for ln in body.splitlines() if re.search(r"\bo\[8\]", ln) and "Out." not in ln]
        rows.append((len(lanes), vph, lanes, inputs, tc1, sorted(e["fps"])))
    rows.sort(key=lambda r: (-r[0], r[1]))
    for nl, vph, lanes, inputs, tc1, fps in rows:
        if nl == 0:
            continue
        print(f"VP {vph} bones={nl} lanes={''.join(lanes)} inputs={inputs} fps={','.join(fps)}")
        for t in tc1[:3]:
            print(f"    tc1: {t[:150]}")
    print(f"--- {sum(1 for r in rows if r[0])} skinned of {len(rows)} VPs")


if __name__ == "__main__":
    main()
