#!/usr/bin/env python3
"""sample_threads.py SAMPLE_FILE [TOPN] -- per thread: busy vs waiting samples and the
top self-time functions, from macOS `sample` call-graph output. Read-only."""
import re
import sys
from collections import Counter, defaultdict

WAIT = ("__psynch_cvwait", "__semwait_signal", "__workq_kernreturn", "semaphore_wait_trap",
        "__psynch_mutexwait", "mach_msg2_trap", "semaphore_wait_signal_trap", "__select",
        "kevent", "__ulock_wait", "nanosleep", "usleep", "__psynch_rw_", "semaphore_timedwait_trap")

LINE = re.compile(r"^(\s*)(?:[+!:|]\s*)*(\d+)\s+(.*?)\s{2,}\(in ([^)]+)\)")


def main():
    path = sys.argv[1]
    topn = int(sys.argv[2]) if len(sys.argv) > 2 else 12
    lines = open(path, errors="replace").read().splitlines()
    try:
        start = next(i for i, l in enumerate(lines) if l.startswith("Call graph:"))
        end = next(i for i, l in enumerate(lines) if l.startswith("Total number in stack"))
    except StopIteration:
        sys.exit("unexpected sample format")
    threads = []
    cur = None
    stack = []  # (depth, count, name)
    for raw in lines[start + 1:end]:
        m = re.match(r"^\s{4}(\d+)\s+(Thread_\S+.*)$", raw)
        if m:
            cur = {"name": m.group(2).strip(), "total": int(m.group(1)), "self": Counter()}
            threads.append(cur)
            stack = []
            continue
        if cur is None:
            continue
        # depth = position of the first digit
        dm = re.match(r"^([\s+!:|]*)(\d+)\s+(.+)$", raw)
        if not dm:
            continue
        depth = len(dm.group(1))
        count = int(dm.group(2))
        rest = dm.group(3)
        fn = re.split(r"\s{2,}\(in |\s\(in ", rest)[0].strip()
        while stack and stack[-1][0] >= depth:
            d, c, n, child_sum = stack.pop()
            cur["self"][n] += max(c - child_sum, 0)
            if stack:
                stack[-1][3] += c
        stack.append([depth, count, fn, 0])
    # flush happens at thread boundaries only approximately; good enough for ranking
    for t in threads:
        wait = sum(v for k, v in t["self"].items() if any(k.startswith(w) for w in WAIT))
        busy = sum(t["self"].values()) - wait
        t["wait"], t["busy"] = wait, busy
    threads.sort(key=lambda t: -t["busy"])
    for t in threads[:10]:
        tot = t["busy"] + t["wait"]
        print(f"== {t['name'][:60]}  busy={t['busy']} wait={t['wait']} ({100.0 * t['busy'] / max(tot, 1):.0f}% busy)")
        busy_items = [(k, v) for k, v in t["self"].items() if not any(k.startswith(w) for w in WAIT)]
        for k, v in sorted(busy_items, key=lambda kv: -kv[1])[:topn]:
            print(f"     {v:6d}  {k[:90]}")


if __name__ == "__main__":
    main()
