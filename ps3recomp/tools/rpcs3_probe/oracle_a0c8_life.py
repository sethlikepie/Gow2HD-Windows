"""oracle_a0c8_life.py -- scope lifetime of func_0042A0C8's push on the console.
After func_0042A0C8 pushes (idx N->N+1), log every push(0x262C8C)/pop(0x262C64) until idx<=N (its scope
popped). Few events, no load = tight pair (lift bug if we skip). Many events / load in between = deferred."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
STK=0x0053F0A0
def gi(rsp):
    try:
        s=rsp.read_u32(STK); return rsp.read_u32(s+4) if guest_ok(s) else -1
    except Exception: return -1
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=7.0)
    ap.add_argument("--tracked",type=int,default=3); ap.add_argument("--deadline",type=float,default=62); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    A0C8=0x0042A1A0; PUSH=0x00262C8C; POP=0x00262C64
    done=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(A0C8); dl=time.time()+a.deadline
        while done<a.tracked and time.time()<dl:
            rsp.cont(); st=rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S",b"T"): print("[life] end",flush=True); break
            select_main(rsp); base=gi(rsp)
            print(f"[life] --- func_0042A0C8 PUSH at idx={base} (scope will live until idx<={base}) ---",flush=True)
            # now watch push/pop until idx back to base
            rsp.add_breakpoint(PUSH); rsp.add_breakpoint(POP); ev=0
            while time.time()<dl and ev<60:
                rsp.cont(); st=rsp.wait_stop(deadline=dl)
                if not st or st[:1] not in (b"S",b"T"): break
                select_main(rsp); regs=rsp.read_registers(); pc=probe._pc(regs)&0xFFFFFFFF; lr=regs.get("lr",0)&0xFFFFFFFF; ev+=1
                idx=gi(rsp); kind="PUSH" if pc==PUSH else ("POP" if pc==POP else "A0C8")
                print(f"[life]    ev{ev} {kind} idx={idx} lr=0x{lr:08X}",flush=True)
                if kind=="POP" and idx is not None and idx<=base+1 and idx<base+1+1 and (idx<=base):
                    print(f"[life]    >>> scope popped after {ev} events (lr=0x{lr:08X})",flush=True); break
            rsp.remove_breakpoint(PUSH); rsp.remove_breakpoint(POP); done+=1
        print("[life] DONE",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
