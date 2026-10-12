"""oracle_a0c8_pop.py -- after func_0042A0C8 pushes (0x42A1A0), how soon does the pop (0x262C64) fire,
and from what caller? Tight pair = lift bug (we skip it). Deferred across load = load-conditional."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
STK_PTR=0x0053F0A0
def idx(rsp):
    try:
        s=rsp.read_u32(STK_PTR)
        return rsp.read_u32(s+4) if guest_ok(s) else -1
    except Exception: return -1
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=9.0)
    ap.add_argument("--events",type=int,default=60); ap.add_argument("--deadline",type=float,default=68); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0); t0=time.time()
    PUSH=0x0042A1A0; POP=0x00262C64; k=0; seen_a0c8=False; since=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(PUSH); rsp.add_breakpoint(POP); dl=time.time()+a.deadline
        while k<a.events and time.time()<dl:
            rsp.cont(); stop=rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S",b"T"): print("[a0c8pop] end",flush=True); break
            select_main(rsp); regs=rsp.read_registers(); pc=probe._pc(regs)&0xFFFFFFFF; lr=regs.get("lr",0)&0xFFFFFFFF; k+=1
            if pc==PUSH:
                print(f"[a0c8pop] #{k} func_0042A0C8-PUSH idx={idx(rsp)}",flush=True); seen_a0c8=True; since=0
            elif pc==POP:
                since+=1
                tag=" <== pops func_0042A0C8 scope?" if (seen_a0c8 and since==1) else ""
                print(f"[a0c8pop] #{k} POP idx={idx(rsp)} caller-lr=0x{lr:08X}{tag}",flush=True)
        print("[a0c8pop] DONE",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
