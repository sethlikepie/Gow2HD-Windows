"""Creates a FAKE RPCS3/game layout for launcher tests (synthetic PARAM.SFO, dummy EBOOT.BIN, zero .rap).

Nothing here comes from the game. Usage: python make_fake_layout.py <dir>
  <dir>/RPCS3/rpcs3.exe, dev_hdd0/game/NPUA80491 (v01.00), dev_hdd0/home/00000001/exdata/<rap>
  <dir>/wrongver/NPUA80491   (APP_VER 01.01)
  <dir>/othergame/BLUS00000  (another title id)
"""
import os
import struct
import sys


def sfo(entries):
    keys = b""
    data = b""
    index = b""
    for k, v in entries:
        raw = v.encode() + b"\0"
        size = (len(raw) + 3) // 4 * 4
        index += struct.pack("<HHIII", len(keys), 0x0204, len(raw), size, len(data))
        keys += k.encode() + b"\0"
        data += raw.ljust(size, b"\0")
    keys = keys.ljust((len(keys) + 3) // 4 * 4, b"\0")
    key_off = 20 + len(index)
    data_off = key_off + len(keys)
    hdr = b"\0PSF" + struct.pack("<IIII", 0x0101, key_off, data_off, len(entries))
    return hdr + index + keys + data


def game(root, title_id, ver):
    os.makedirs(os.path.join(root, "USRDIR"), exist_ok=True)
    with open(os.path.join(root, "PARAM.SFO"), "wb") as f:
        f.write(sfo([("APP_VER", ver), ("CATEGORY", "HG"), ("TITLE", "Fake Test Title"),
                     ("TITLE_ID", title_id), ("VERSION", ver)]))
    with open(os.path.join(root, "USRDIR", "EBOOT.BIN"), "wb") as f:
        f.write(b"FAKE")


def main():
    base = sys.argv[1]
    r = os.path.join(base, "RPCS3")
    os.makedirs(r, exist_ok=True)
    open(os.path.join(r, "rpcs3.exe"), "wb").close()
    game(os.path.join(r, "dev_hdd0", "game", "NPUA80491"), "NPUA80491", "01.00")
    ex = os.path.join(r, "dev_hdd0", "home", "00000001", "exdata")
    os.makedirs(ex, exist_ok=True)
    with open(os.path.join(ex, "UP9000-NPUA80491_00-GODOFWARIIHDUS00.rap"), "wb") as f:
        f.write(b"\0" * 16)
    game(os.path.join(base, "wrongver", "NPUA80491"), "NPUA80491", "01.01")
    game(os.path.join(base, "othergame", "BLUS00000"), "BLUS00000", "01.00")
    print("fake layout in", base)


if __name__ == "__main__":
    main()
