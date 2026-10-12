"""Small helpers shared by the lift patches (were in engine_fixes_v20.py)."""
import sys


def _rw(path):
    return path.read_text(encoding="utf-8", errors="surrogateescape")


def _wr(path, s):
    path.write_text(s, encoding="utf-8", errors="surrogateescape", newline="\n")


def rep1(s, old, new, what):
    if s.count(old) != 1:
        sys.exit(f"anchor for '{what}' found {s.count(old)}x (want 1)")
    return s.replace(old, new, 1)
