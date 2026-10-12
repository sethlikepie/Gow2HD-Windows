"""oracle_ee70.py - read the class->slotindex table *(0x53EE70) and print which class maps to slotindex 9
(the index func_0009EC0C reads at parent+0xB8+idx*4). Also *(0x53EE70) itself + 40 entries."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import guest_ok
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=10.0); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    try:
        time.sleep(a.at); rsp.connect(retries=120,delay=0.5); rsp.handshake()
        tab=rsp.read_u32(0x0053EE70); print(f"[ee70] *(0x53EE70)=0x{tab:08X}")
        if guest_ok(tab):
            for c in range(40):
                v=rsp.read_u32(tab+c*4); mark=" <== slot 9" if v==9 else ""
                if v<64: print(f"[ee70] classe {c:2d} -> slotindex {v}{mark}")
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
main()
