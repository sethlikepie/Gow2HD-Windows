#!/usr/bin/env python3
"""main_tree.py SAMPLE_FILE ROOT_FUNC [MIN_PCT] [MAX_REL_DEPTH] -- main thread call tree below the
first ROOT_FUNC frame, guest functions only, as percent of the main thread's samples. Read-only."""
import re
import sys


def main():
    path, root = sys.argv[1], sys.argv[2]
    min_pct = float(sys.argv[3]) if len(sys.argv) > 3 else 2.0
    max_rel = int(sys.argv[4]) if len(sys.argv) > 4 else 8
    lines = open(path, errors="replace").read().splitlines()
    start = next(i for i, l in enumerate(lines) if l.startswith("Call graph:"))
    i = next(j for j in range(start, len(lines)) if re.match(r"^\s{4}\d+\s+Thread_", lines[j])
             and ("main-thread" in lines[j] or "Main Thread" in lines[j]))
    total = int(re.match(r"^\s{4}(\d+)", lines[i]).group(1))
    rows = []
    for l in lines[i + 1:]:
        if re.match(r"^\s{4}\d+\s+Thread_", l):
            break
        m = re.match(r"^([\s+!:|]*)(\d+)\s+(.+?)\s{2,}\(in ", l)
        if m:
            rows.append((len(m.group(1)), int(m.group(2)), m.group(3).split('(')[0]))
    root_depth = None
    guest_depth = {}
    for d, c, n in rows:
        if root_depth is None:
            if n == root:
                root_depth = d
            continue
        if d <= root_depth:
            break
        if not n.startswith(('func_', 'sys_', 'cellGcm', 'ps3_hle_call')):
            continue
        pct = 100.0 * c / total
        if pct < min_pct:
            continue
        # indentation by nesting among printed guest frames
        rel = sum(1 for k in guest_depth if k < d)
        guest_depth = {k: v for k, v in guest_depth.items() if k < d}
        guest_depth[d] = n
        if rel <= max_rel:
            print(f"{'  ' * rel}{pct:5.1f}% {n}")


if __name__ == "__main__":
    main()
