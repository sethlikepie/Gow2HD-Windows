import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import guest_ok, TABLE, s8
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--slot",default="0x00539A38"); ap.add_argument("--at",type=float,default=9.0); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    try:
        time.sleep(a.at); rsp.connect(retries=120,delay=0.5); rsp.handshake()
        root=rsp.read_u32(int(a.slot,0)); mgr=rsp.read_u32(root+0x58) if guest_ok(root) else 0
        cur=s8(rsp.read_mem(mgr+0xC8,1)[0]) if guest_ok(mgr) else None
        st0=rsp.read_u32(mgr+0x48) if guest_ok(mgr) else 0
        ti=[i for i in range(40) if int.from_bytes(rsp.read_mem(TABLE+i*4,4),"big")==mgr]
        print(f"[mgr58] root=*(0x{int(a.slot,0):08X})=0x{root:08X} mgr=*(root+0x58)=0x{mgr:08X} TABLE{ti} cursor={cur} stack0=0x{st0:08X} w0(stack0)=0x{rsp.read_u32(st0):08X}" if guest_ok(st0) else f"[mgr58] root=0x{root:08X} mgr=0x{mgr:08X} TABLE{ti} cursor={cur} stack0=0x{st0:08X}", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
main()
