#!/usr/bin/env python3
"""fetch_tools.py -- download and unpack the tools build.py needs (the list from `build.py --print-tools`).

    python tools/fetch_tools.py <tools dir>

Each download is checked against its pinned size and SHA-256 before it is unpacked into <tools dir>/<extract-to>.
A tool whose folder already exists is skipped. (A launcher can do the same itself from the JSON.)
"""
import hashlib
import json
import os
import subprocess
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    tools = os.path.abspath(sys.argv[1])
    os.makedirs(tools, exist_ok=True)
    spec = json.loads(subprocess.run([sys.executable, os.path.join(HERE, "..", "build.py"), "--print-tools"],
                                     check=True, capture_output=True, text=True).stdout)
    for t in spec:
        dst = os.path.join(tools, t["extract-to"])
        if os.path.isdir(dst) and os.listdir(dst):
            print("have", t["name"])
            continue
        z = os.path.join(tools, os.path.basename(t["url"]))
        if not (os.path.isfile(z) and os.path.getsize(z) == t["size"]):
            print("downloading", t["url"])
            urllib.request.urlretrieve(t["url"], z + ".part")
            os.replace(z + ".part", z)
        h = hashlib.sha256()
        with open(z, "rb") as f:
            for b in iter(lambda: f.read(1 << 22), b""):
                h.update(b)
        if os.path.getsize(z) != t["size"] or h.hexdigest() != t["sha256"]:
            sys.exit("%s: size/sha256 mismatch, download damaged" % z)
        print("unpacking", os.path.basename(z))
        with zipfile.ZipFile(z) as zf:
            zf.extractall(dst + ".part")
        os.replace(dst + ".part", dst)
        os.remove(z)
    print("tools ready in", tools)


if __name__ == "__main__":
    main()
