"""oracle_dump_after.py -- let the console RUN for --run seconds, then pause and dump memory.

oracle_readmem.py reads right after connecting, while the stub still holds the
game at its entry, so every static area reads 0. This one continues first:
connect -> vCont;c -> sleep --run -> 0x03 -> read --len bytes at --addr (hex dump,
one BE u32 per line, same format as oracle_readmem).

Usage: python3 oracle_dump_after.py --title "<full path to EBOOT>" --addr 0x886280 --len 3456 --run 40
"""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--title", required=True)
    ap.add_argument("--addr", default="0x886280")
    ap.add_argument("--len", type=int, default=3456)
    ap.add_argument("--run", type=float, default=40.0)
    a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True)
    rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0)
    try:
        time.sleep(4.0)
        rsp.connect(retries=240, delay=0.5)
        rsp.handshake()
        select_main(rsp)          # Hg before resuming (recipe: the stub ignores a bare c)
        rsp.cont()
        time.sleep(a.run)
        rsp.interrupt()
        rsp.wait_stop(deadline=time.time() + 30)
        ea = int(a.addr, 0)
        data = rsp.read_mem(ea, a.len)
        print(f"[rd] 0x{ea:08X}: LEITURA OK ({len(data)} bytes) apos {a.run:.0f} s a correr")
        for i in range(0, len(data), 4):
            w = int.from_bytes(data[i:i + 4], "big")
            print(f"[rd]   +0x{i:02X} = 0x{w:08X}")
    finally:
        try:
            rsp.close()
        except Exception:
            pass
        proc.kill()


if __name__ == "__main__":
    main()
