"""oracle_enters.py -- CONSOLE values at the two guards we fail (E392):
  func_0042AF18(r3,r4): r4==0 skips the ENTER          -> print r3, r4, lr
  func_002BBA64(r3=size): size==-1 early-returns; size may come from override *(0x0053FFC8) -> print r3, override, r5, lr"""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=9.0)
    ap.add_argument("--hits",type=int,default=30); ap.add_argument("--deadline",type=float,default=360); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    A=0x0042AF18; B=0x002BBA64; n=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(A); rsp.add_breakpoint(B); dl=time.time()+a.deadline
        while n<a.hits and time.time()<dl:
            rsp.cont(); st=rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S",b"T"): print("[ent] end",flush=True); break
            select_main(rsp); r=rsp.read_registers(); pc=probe._pc(r)&0xFFFFFFFF; n+=1
            r3=r.get('r3',0)&0xFFFFFFFF; r4=r.get('r4',0)&0xFFFFFFFF; r5=r.get('r5',0)&0xFFFFFFFF; lr=r.get('lr',0)&0xFFFFFFFF
            if pc==A: print(f"[ent] 42AF18 #{n} r3=0x{r3:08X} r4=0x{r4:08X} lr=0x{lr:08X}",flush=True)
            elif pc==B: print(f"[ent] 2BBA64 #{n} size(r3)=0x{r3:08X} override=0x{rsp.read_u32(0x0053FFC8):08X} r5=0x{r5:08X} lr=0x{lr:08X}",flush=True)
        print(f"[ent] DONE n={n}",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
