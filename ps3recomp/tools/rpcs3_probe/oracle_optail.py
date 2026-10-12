"""oracle_optail.py -- CONSOLE ordering: the sync-open tail (0x2B4434: lwz r0,4(r31) -> r3=(op+4!=0)) vs the FIOS
free that clears the user's op slot (0x30CD40: stw 0,0(r9)). Ours (E399): the FIOS thread clears op+4 before the tail."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=9.0)
    ap.add_argument("--hits",type=int,default=12); ap.add_argument("--deadline",type=float,default=360); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    TAIL=0x002B4434; CLR=0x0030CD40; OP=0x008695D4; n=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(TAIL); rsp.add_breakpoint(CLR); dl=time.time()+a.deadline
        while n<a.hits and time.time()<dl:
            rsp.cont(); st=rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S",b"T"): print("[optail] end",flush=True); break
            r=rsp.read_registers(); pc=probe._pc(r)&0xFFFFFFFF; n+=1; g=lambda k:r.get(k,0)&0xFFFFFFFF
            if pc==TAIL: print(f"[optail] TAIL  r31(op)=0x{g('r31'):08X} *(op+4)=0x{rsp.read_u32(g('r31')+4):08X} loader-op+4=0x{rsp.read_u32(OP+4):08X} t={time.time():.2f}",flush=True)
            elif pc==CLR: print(f"[optail] CLEAR r9(slot)=0x{g('r9'):08X} (loader op+4 is 0x{OP+4:08X}) loader-op+4=0x{rsp.read_u32(OP+4):08X} stop={st[:12]} t={time.time():.2f}",flush=True)
            else: print(f"[optail] other pc=0x{pc:08X}",flush=True)
        print(f"[optail] DONE n={n}",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
