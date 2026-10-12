"""oracle_readmem.py - read a few u32 at ADDR (default 0x0A050000) on the console (headless), decode ASCII."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--addr",default="0x0A050000"); ap.add_argument("--n",type=int,default=16); ap.add_argument("--at",type=float,default=10.0); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    try:
        time.sleep(a.at); rsp.connect(retries=120,delay=0.5); rsp.handshake()
        ea=int(a.addr,0)
        try:
            data=rsp.read_mem(ea,a.n*4)
            print(f"[rd] 0x{ea:08X}: LEITURA OK ({len(data)} bytes)")
            for i in range(0,len(data),4):
                w=int.from_bytes(data[i:i+4],'big'); asc=''.join(chr(b) if 32<=b<127 else '.' for b in data[i:i+4])
                print(f"[rd]   +0x{i:02X} = 0x{w:08X}  '{asc}'")
        except Exception as e:
            print(f"[rd] 0x{ea:08X}: NAO LEGIVEL ({e}) -> nao e' memoria commitada no console")
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
main()
