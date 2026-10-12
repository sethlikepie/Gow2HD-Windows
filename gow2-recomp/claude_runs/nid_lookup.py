#!/usr/bin/env python3
"""nid_lookup.py -- which cellGcmSys function names (from the RPCS3 checkout's
REG_FUNC list) hash to the given PS3 NIDs. Read-only."""
import hashlib
import os
import re
import struct
import sys

SUFFIX = bytes.fromhex("6759659904250490566427499489741A")
SRC = os.path.expanduser("~/Documents/PESSOAL/_ref_rpcs3/rpcs3/Emu/Cell/Modules/cellGcmSys.cpp")


def nid(name):
    return struct.unpack("<I", hashlib.sha1(name.encode() + SUFFIX).digest()[:4])[0]


def main():
    want = set()
    for a in sys.argv[1:]:
        if not re.fullmatch(r"0x[0-9A-Fa-f]{8}", a):
            sys.exit("bad nid " + a)
        want.add(int(a, 16))
    with open(SRC, errors="replace") as fh:
        names = re.findall(r"REG_FUNC\(cellGcmSys, ([A-Za-z0-9_]+)\)", fh.read())
    print(len(names), "names")
    for n in names:
        v = nid(n)
        if v in want:
            print("0x%08X %s" % (v, n))


if __name__ == "__main__":
    main()
