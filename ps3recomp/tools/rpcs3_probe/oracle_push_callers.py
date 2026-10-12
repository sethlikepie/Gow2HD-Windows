"""oracle_push_callers.py -- console PUSH (func_00262C8C=0x00262C8C) caller-lr histogram + MAX scope idx.
Resolves extra-push vs skipped-pop: if console max idx<=126 and a caller dominates our pushes, that's the extra."""
import argparse, os, sys, time, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
STK_PTR=0x0053F0A0
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=8.0)
    ap.add_argument("--hits",type=int,default=800); ap.add_argument("--deadline",type=float,default=70); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    hist=collections.Counter(); k=0; maxidx=-1
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(0x00262C8C); dl=time.time()+a.deadline
        while k<a.hits and time.time()<dl:
            rsp.cont(); stop=rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S",b"T"): print("[push] no stop/end",flush=True); break
            select_main(rsp); regs=rsp.read_registers(); k+=1
            hist[(regs.get("lr",0))&0xFFFFFFFF]+=1
            try:
                stk=rsp.read_u32(STK_PTR)
                if guest_ok(stk):
                    idx=rsp.read_u32(stk+4)
                    if idx>maxidx and idx<1000: maxidx=idx
            except Exception: pass
        print(f"[push] DONE hits={k} MAX-scope-idx={maxidx} distinct={len(hist)}",flush=True)
        for lr,c in hist.most_common(16): print(f"[push]   caller-lr=0x{lr:08X}  x{c}",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
