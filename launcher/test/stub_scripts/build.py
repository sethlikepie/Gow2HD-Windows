"""STUB build.py for testing the launcher. It is NOT the real builder.

Implements the launcher <-> build.py contract:
  build.py --print-tools                      -> JSON list: name, url, sha256, size, extract-to
  build.py --game D --rap F [--elf F] --tools D --out D [--jobs N]
      stdout: "PROGRESS <0-100> <msg>", "ERROR <msg>", optional "EXE <path relative to --out>"
      exit code 0 = success
Test switches (environment):
  STUB_FAIL=<pct>     fail with an ERROR line when this percentage is reached
  STUB_BADHASH=1      --print-tools lists a wrong sha256 for one tool (the launcher must refuse it)
  STUB_SECONDS=<n>    total run time (default 12)
  STUB_HANG=1         never finish (to test Cancel / job kill); starts a child python that also hangs
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)                     # the embeddable Python does not add the script folder itself
import stub_helper  # noqa: E402  (proves sibling imports work)

TOOLS = [
    {"name": "ninja", "url": "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip",
     "sha256": "f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a", "size": 275425,
     "extract-to": "ninja"},
    {"name": "7zr", "url": "https://www.7-zip.org/a/7zr.exe",
     "sha256": "256feca8e274e5da655e2a284fabafd9f554365eb164862089dacd4e8276d282", "size": 602624,
     "extract-to": "7zip"},
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--print-tools", action="store_true")
    ap.add_argument("--game"); ap.add_argument("--rap"); ap.add_argument("--elf")
    ap.add_argument("--tools"); ap.add_argument("--out"); ap.add_argument("--jobs", type=int, default=0)
    a = ap.parse_args()
    if a.print_tools:
        tools = [dict(t) for t in TOOLS]
        if os.environ.get("STUB_BADHASH") == "1":
            tools[1]["sha256"] = "0" * 64
        print(json.dumps(tools, indent=1))
        return 0
    for need in ("game", "rap", "tools", "out"):
        if not getattr(a, need):
            print(f"ERROR missing --{need}")
            return 2
    if not os.path.isfile(os.path.join(a.game, "PARAM.SFO")):
        print(f"ERROR no PARAM.SFO in {a.game}")
        return 3
    if os.path.getsize(a.rap) != 16:
        print("ERROR rap is not 16 bytes")
        return 3
    if not os.path.isfile(os.path.join(a.tools, "ninja", "ninja.exe")):
        print("ERROR ninja.exe missing from the tools folder")
        return 4
    if not os.path.isfile(os.path.join(a.tools, "7zip", "7zr.exe")):
        print("ERROR 7zr.exe missing from the tools folder")
        return 4
    print(f"stub: python {sys.version.split()[0]} at {sys.executable}")
    print(f"stub: game={a.game} rap={a.rap} elf={a.elf} tools={a.tools} out={a.out} jobs={a.jobs}")
    print(stub_helper.hello())
    fail_at = int(os.environ.get("STUB_FAIL", "-1"))
    total = float(os.environ.get("STUB_SECONDS", "12"))
    if os.environ.get("STUB_HANG") == "1":
        print("PROGRESS 5 Hanging on purpose (STUB_HANG)")
        child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(3600)"])
        print(f"stub: child pid {child.pid}", flush=True)
        time.sleep(3600)
    steps = [(3, "Checking the game files"), (10, "Decrypting EBOOT.BIN"), (25, "Lifting PPU code"),
             (45, "Compiling recompiled code (stub)"), (70, "Compiling SPU programs (stub)"),
             (90, "Linking"), (98, "Writing settings")]
    for pct, msg in steps:
        if 0 <= fail_at <= pct:
            print(f"ERROR Stub failure at {pct}% (STUB_FAIL={fail_at})")
            return 1
        print(f"PROGRESS {pct} {msg}")
        for i in range(3):
            time.sleep(total / len(steps) / 3)
            print(f"  [{pct}] working... {i + 1}/3")
    os.makedirs(a.out, exist_ok=True)
    shutil.copy2(os.path.join(HERE, "stub_game.exe"), os.path.join(a.out, "gow2.exe"))
    os.makedirs(os.path.join(a.out, "movie_cache"), exist_ok=True)
    os.makedirs(os.path.join(a.out, ".build"), exist_ok=True)
    with open(os.path.join(a.out, "movie_cache", "stub.txt"), "w") as f:
        f.write("stub movie cache\n")
    with open(os.path.join(a.out, "gow2_port.ini"), "w") as f:
        f.write("[paths]\nvfs_root=%s\nmovie_cache=movie_cache\n" % os.path.join(a.game, "USRDIR"))
    with open(os.path.join(a.out, "build_info.json"), "w") as f:
        json.dump({"exe": "gow2.exe", "installed": ["gow2.exe", "gow2_port.ini", "build_info.json"],
                   "installed_dirs": [".build", "movie_cache"]}, f)
    print("EXE gow2.exe")
    print("PROGRESS 100 Done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
