"""oracle_tick_caller.py -- WHO ticks the loader step func_002BA76C on the console?
Ours runs it ONCE (WADLD-SM #1); console runs it continuously. bp 0x002BA76C, walk the stack
to name the driver, and report the caller histogram."""
import argparse, os, sys, time, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=9.0)
    ap.add_argument("--hits",type=int,default=25); ap.add_argument("--deadline",type=float,default=65); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    hist=collections.Counter(); k=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(0x002BA76C); dl=time.time()+a.deadline
        while k<a.hits and time.time()<dl:
            rsp.cont(); st=rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S",b"T"): print("[tick] end",flush=True); break
            select_main(rsp); regs=rsp.read_registers(); k+=1
            lr=(regs.get("lr",0))&0xFFFFFFFF
            frames=[f"0x{f['pc']:08X}" for f in probe.walk_stack(rsp,regs,8) if f["pc"]][1:5]
            hist[lr]+=1
            if k<=8: print(f"[tick] #{k} lr=0x{lr:08X} stack={frames}",flush=True)
        print(f"[tick] DONE hits={k}",flush=True)
        for lr,c in hist.most_common(8): print(f"[tick]   caller-lr=0x{lr:08X} x{c}",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
