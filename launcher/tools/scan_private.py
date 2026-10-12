"""Release check: fails if a file (the built exe, the scripts zip) contains a personal path or user name, in UTF-8 or
UTF-16LE. The strings are the repository's list (SECRETS in tools/check_no_game_data.py), so they live in one place.
Usage: python scan_private.py <file> [...]"""
import importlib.util
import os
import sys

sys.dont_write_bytecode = True   # no __pycache__ copy of the checker (it holds the needles)
_CHECKER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "check_no_game_data.py")


def needles():
    spec = importlib.util.spec_from_file_location("check_no_game_data", _CHECKER)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return [s.decode("latin-1") for s in mod.SECRETS]


def main():
    bad = 0
    ns = needles()
    for path in sys.argv[1:]:
        data = open(path, "rb").read().lower()
        for n in ns:
            for enc in ("utf-8", "utf-16-le"):
                if n.lower().encode(enc) in data:
                    print(f"{path}: contains {n!r} ({enc})")
                    bad += 1
    print("clean" if not bad else f"{bad} hit(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
