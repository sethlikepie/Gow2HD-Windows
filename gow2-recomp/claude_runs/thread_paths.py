#!/usr/bin/env python3
"""thread_paths.py SAMPLE_FILE -- per thread, the dominant call path (frames holding >= 60% of the
thread's samples), keeping guest (func_*), syscall (sys_*) and SPU frames. Read-only."""
import re
import sys


def main():
    lines = open(sys.argv[1], errors="replace").read().splitlines()
    try:
        start = next(i for i, l in enumerate(lines) if l.startswith("Call graph:"))
        end = next(i for i, l in enumerate(lines) if l.startswith("Total number in stack"))
    except StopIteration:
        sys.exit("unexpected sample format")
    threads = []
    cur = None
    for l in lines[start + 1:end]:
        m = re.match(r"^\s{4}(\d+)\s+(Thread_\S+.*)$", l)
        if m:
            cur = {"name": m.group(2)[:40], "frames": []}
            threads.append(cur)
            continue
        if cur is None:
            continue
        m = re.match(r"^([\s+!:|]*)(\d+)\s+(.+?)\s{2,}\(in ", l)
        if not m:
            continue
        cur["frames"].append((len(m.group(1)), int(m.group(2)), m.group(3)))
    for t in threads:
        fr = t["frames"]
        if not fr:
            continue
        total = fr[0][1]
        path = []
        last = -1
        for d, c, n in fr:
            if d > last and c >= total * 0.6:
                path.append(n.split('(')[0])
                last = d
        keep = [p for p in path if p.startswith(('func_', 'sys_', 'spu'))]
        if keep:
            print(t["name"], "|", " > ".join(keep[-6:]))


if __name__ == "__main__":
    main()
