"""oracle_list.py - walk an intrusive list on the oracle: head = *(slot) (next @+0, prev @+4, key @+8)."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import guest_ok
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--title", required=True); ap.add_argument("--slot", default="0x0053EA54"); ap.add_argument("--at", type=float, default=12.0); a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True); rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0)
    try:
        time.sleep(a.at); rsp.connect(); rsp.handshake()
        head = rsp.read_u32(int(a.slot, 0)); print(f"[list] slot={a.slot} head=0x{head:08X} *head=0x{rsp.read_u32(head):08X} head+4=0x{rsp.read_u32(head+4):08X}", flush=True)
        n = rsp.read_u32(head); k = 0
        while guest_ok(n) and n != head and k < 200:
            print(f"[list] #{k} node=0x{n:08X} next=0x{rsp.read_u32(n):08X} prev=0x{rsp.read_u32(n+4):08X} key=0x{rsp.read_u32(n+8):08X}", flush=True); n = rsp.read_u32(n); k += 1
        print(f"[list] fim: {k} nos, terminou em 0x{n:08X} ({'cabeca' if n==head else 'NAO cabeca'})", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__ == "__main__": main()
