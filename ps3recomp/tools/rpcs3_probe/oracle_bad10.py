"""oracle_bad10.py -- CONSOLE: func_002BAD10 (container load) right after the open call (pc=0x002BADA4):
r3 = open result, r28 = flags (0x10000210 = sync), ts+0x74 = file size, ts+0x64 = op, state. Ours (E394) never runs
the sync tick loop for the LglScA, i.e. r3==0 or ts+0x74==0 there."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=9.0)
    ap.add_argument("--hits",type=int,default=4); ap.add_argument("--deadline",type=float,default=360); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    ENT=0x002BAD10; POST=0x002BADA4; n=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(ENT); rsp.add_breakpoint(POST); dl=time.time()+a.deadline
        while n<a.hits and time.time()<dl:
            rsp.cont(); st=rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S",b"T"): print("[bad10] end",flush=True); break
            select_main(rsp); r=rsp.read_registers(); pc=probe._pc(r)&0xFFFFFFFF
            g=lambda k:r.get(k,0)&0xFFFFFFFF; ts=0x00869570
            if pc==ENT: print(f"[bad10] entry desc(r3)=0x{g('r3'):08X} flags(r4)=0x{g('r4'):08X} lr=0x{g('lr'):08X}",flush=True)
            elif pc==POST:
                n+=1; print(f"[bad10] after open: r3=0x{g('r3'):08X} flags(r28)=0x{g('r28'):08X} ts+0x74=0x{rsp.read_u32(ts+0x74):08X} ts+0x64=0x{rsp.read_u32(ts+0x64):08X} state={rsp.read_u32(ts+0x1CC)} t={time.time():.0f}",flush=True)
        print(f"[bad10] DONE n={n}",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
