"""oracle_pop_callers.py -- histogram of POP (func_00262C64=0x00262C64) caller-lr on the console.
Compare to ours (scope2.log). A pop-caller the console has and we lack is the missing pop (E353p)."""
import argparse, os, sys, time, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=8.0)
    ap.add_argument("--hits",type=int,default=600); ap.add_argument("--deadline",type=float,default=75); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    hist=collections.Counter(); k=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(0x00262C64); dl=time.time()+a.deadline
        while k<a.hits and time.time()<dl:
            rsp.cont(); stop=rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S",b"T"): print("[pop] no stop/end",flush=True); break
            select_main(rsp); regs=rsp.read_registers(); k+=1
            lr=(regs.get("lr",0))&0xFFFFFFFF; hist[lr]+=1
        print(f"[pop] DONE hits={k} distinct-callers={len(hist)}",flush=True)
        for lr,c in hist.most_common(20): print(f"[pop]   caller-lr=0x{lr:08X}  x{c}",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
