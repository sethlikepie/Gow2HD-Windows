"""oracle_handler.py -- on the console, is *(obj+0xC) (the handler OPD) filled at func_002F0AAC?
Ours: NULL (or garbage) -> ICALL-BAD. bp 0x002F0AAC, read r31 and its fields."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=9.0)
    ap.add_argument("--hits",type=int,default=14); ap.add_argument("--deadline",type=float,default=62); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0); k=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(0x002F0AAC); dl=time.time()+a.deadline
        while k<a.hits and time.time()<dl:
            rsp.cont(); st=rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S",b"T"): print("[hnd] end",flush=True); break
            select_main(rsp); regs=rsp.read_registers(); k+=1
            obj=regs.get("r31",0)&0xFFFFFFFF
            if guest_ok(obj):
                f0=rsp.read_u32(obj); f4=rsp.read_u32(obj+4); f8=rsp.read_u32(obj+8); fc=rsp.read_u32(obj+0xC)
                code=rsp.read_u32(fc) if guest_ok(fc) else 0xFFFFFFFF
                print(f"[hnd] #{k} obj=0x{obj:08X} +0=0x{f0:08X} +4=0x{f4:08X} +8=0x{f8:08X} +C(OPD)=0x{fc:08X} code=0x{code:08X}",flush=True)
            else:
                print(f"[hnd] #{k} obj=0x{obj:08X} (not guest)",flush=True)
        print("[hnd] DONE",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
