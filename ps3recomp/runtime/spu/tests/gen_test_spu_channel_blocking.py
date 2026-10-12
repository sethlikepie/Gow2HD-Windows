#!/usr/bin/env python3
"""Isolated unit test scaffold for spu_channel_read_blocking()
(runtime/spu/spu_context.h).

Unlike the other tests in this directory, this one does not exercise any
lifted SPU instruction semantics: spu_channel_read_blocking() is a plain host
C function (pthread producer + bounded poll consumer), tested directly by
test_spu_channel_blocking_main.c. The lifter pipeline is still run here (a
single `stop` instruction is all the "program" needed) purely so this test
produces the same out_<name>/spu_recomp.{c,h} pair every other test links
against, keeping run_tests.sh's build step uniform across the suite. The
generated spu_func_00000000 is never called by the harness.
"""
import struct, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools"))
from wrap_spu_elf import wrap

def w(v): return struct.pack(">I", v & 0xFFFFFFFF)
def rr(op11, rb, ra, rt): return w(((op11 & 0x7FF) << 21) | ((rb & 0x7F) << 14) | ((ra & 0x7F) << 7) | (rt & 0x7F))

STOP = 0x000

b = rr(STOP, 0, 0, 0)   # stop

elf = wrap(b, base=0, entry=0, symbols=[{"name": "main", "addr": 0, "size": len(b)}])
out = os.path.join(HERE, "test_spu_channel_blocking.elf")
open(out, "wb").write(elf)
print(f"Wrote test_spu_channel_blocking.elf ({len(b)} bytes code)")
