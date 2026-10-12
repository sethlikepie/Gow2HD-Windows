"""oracle_type15.py -- how many type-15 (CGOWShader) constructs does the CONSOLE do?
bp func_0039E794 (the type-15 handler, E292). Ours plateaus at 18; console target 27.
Also bp the registry dispatcher func_002B0DB0 to see record types dispatched."""
import argparse, os, sys, time, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=9.0)
    ap.add_argument("--hits",type=int,default=400); ap.add_argument("--deadline",type=float,default=70); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    T15=0x0039E794; DISP=0x002B0DB0
    cnt=collections.Counter(); k=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(T15); rsp.add_breakpoint(DISP); dl=time.time()+a.deadline
        while k<a.hits and time.time()<dl:
            rsp.cont(); st=rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S",b"T"): print("[t15] end",flush=True); break
            select_main(rsp); regs=rsp.read_registers(); pc=probe._pc(regs)&0xFFFFFFFF; k+=1
            if pc==T15:
                cnt['TYPE15']+=1
                if cnt['TYPE15']<=6 or cnt['TYPE15']%5==0:
                    print(f"[t15] TYPE15 construct #{cnt['TYPE15']} (r3=0x{regs.get('r3',0)&0xFFFFFFFF:08X})",flush=True)
            elif pc==DISP:
                cnt['DISPATCH']+=1
        print(f"[t15] DONE TYPE15={cnt['TYPE15']} DISPATCH={cnt['DISPATCH']} (ours: 18 shells)",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
