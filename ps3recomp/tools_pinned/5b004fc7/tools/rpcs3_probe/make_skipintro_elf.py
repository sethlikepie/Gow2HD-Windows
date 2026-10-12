"""make_skipintro_elf.py - derive an intro-skipping copy of a PS3 ELF for the RPCS3 oracle.

GoW2 HD (NPUA80491): the intro wait loop `func_000CE0A0` spins until
`*0x00592668 == 4` (movie player done) or the game's own skip byte is set:

    000CE0F4  lwz   r0, 0(r31)      ; r31 = *(TOC-0x5E70) = 0x00592668
    000CE0F8  cmpwi cr7, r0, 4
    000CE0FC  bne   cr7, 0xCE0D4    ; != 4 -> keep waiting   <- patched to nop

RPCS3 (macOS arm64, loose decrypted ELF) never moves the counter past 2 (E81), so
the oracle cannot reach the B71 boot sequence at all. Replacing that `bne` with a
`nop` lets the game fall through exactly as it does when the movie finishes,
which is what our runtime's FORCE/EOS paths also do. DIAGNOSTIC ONLY - the copy
is written next to nothing that gets committed (game data is never versioned).

Idempotent: refuses to touch a file whose word at the patch site is neither the
original `bne` nor the `nop` already applied.

    python tools/rpcs3_probe/make_skipintro_elf.py <EBOOT.ELF> <out.ELF>
"""
import struct
import sys

PATCH_EA = 0x000CE0FC
ORIG = 0x409EFFD8   # bne cr7, -0x28
NOP = 0x60000000


def segments(b):
    e_phoff, = struct.unpack_from('>Q', b, 0x20)
    n, = struct.unpack_from('>H', b, 0x38)
    out = []
    for i in range(n):
        o = e_phoff + i * 56
        t, = struct.unpack_from('>I', b, o)
        off, va = struct.unpack_from('>QQ', b, o + 8)
        fsz, = struct.unpack_from('>Q', b, o + 32)
        if t == 1 and fsz:
            out.append((va, off, fsz))
    return out


def file_off(segs, ea):
    for va, off, fsz in segs:
        if va <= ea < va + fsz:
            return off + ea - va
    raise SystemExit(f"EA 0x{ea:08X} not in a PT_LOAD file range")


def main():
    src, dst = sys.argv[1], sys.argv[2]
    b = bytearray(open(src, 'rb').read())
    fo = file_off(segments(b), PATCH_EA)
    w, = struct.unpack_from('>I', b, fo)
    if w == NOP:
        print(f"[skipintro] already patched at 0x{PATCH_EA:08X} (foff 0x{fo:X})")
    elif w != ORIG:
        raise SystemExit(f"[skipintro] unexpected word 0x{w:08X} at 0x{PATCH_EA:08X}; refusing")
    else:
        struct.pack_into('>I', b, fo, NOP)
        print(f"[skipintro] 0x{PATCH_EA:08X} (foff 0x{fo:X}): bne -> nop")
    open(dst, 'wb').write(b)
    print(f"[skipintro] wrote {dst} ({len(b)} bytes)")


if __name__ == '__main__':
    main()
