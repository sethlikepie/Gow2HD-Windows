"""oracle_a0c8_branch.py -- does the console's func_0042A0C8 reach the PUSH (0x42A1A0) or take the
early exit (func_0042A1D4=0x42A1D4)? Counts entry(0x42A0C8) vs push-site(0x42A1A0) vs early(0x42A1D4).
If console push-site count is ~0 while ours pushes, the control-flow branch is lifted wrong."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=8.0)
    ap.add_argument("--hits",type=int,default=400); ap.add_argument("--deadline",type=float,default=80); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0); t0=time.time()
    BPS={0x0042A0C8:"entry",0x0042A1A0:"PUSH-site",0x0042A1D4:"early-exit(A1D4)"}
    cnt={v:0 for v in BPS.values()}; k=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        for addr in BPS: rsp.add_breakpoint(addr)
        dl=time.time()+a.deadline
        while k<a.hits and time.time()<dl:
            rsp.cont(); stop=rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S",b"T"): print("[a0c8] no stop/end",flush=True); break
            select_main(rsp); regs=rsp.read_registers(); pc=probe._pc(regs); k+=1
            lbl=BPS.get(pc&0xFFFFFFFF,"?0x%08X"%(pc&0xFFFFFFFF)); cnt[lbl]=cnt.get(lbl,0)+1
        print(f"[a0c8] DONE hits={k} counts={cnt}",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
