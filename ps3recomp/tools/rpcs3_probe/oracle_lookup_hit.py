"""oracle_lookup_hit.py -- do the console's name lookups HIT?
bp func_002AB1AC entry (0x002AB1AC): r4 = name ptr.
bp the return of func_002ACBE8 inside it (0x002AB1F4): r3 = node found (0 = MISS).
Ours: 80/80 MISS. If console HITS, the names are registered there and we lack the creation phase."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
def rdstr(rsp, ea, n=32):
    try:
        b=rsp.read_mem(ea,n); o=[]
        for ch in b:
            if ch<0x20 or ch>0x7E: break
            o.append(chr(ch))
        return ''.join(o)
    except Exception: return '?'
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=9.0)
    ap.add_argument("--pairs",type=int,default=20); ap.add_argument("--deadline",type=float,default=68); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    ENTRY=0x002AB1AC; RET=0x002AB1F4; pend=None; k=0; hits=0; miss=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(ENTRY); rsp.add_breakpoint(RET); dl=time.time()+a.deadline
        while k<a.pairs and time.time()<dl:
            rsp.cont(); st=rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S",b"T"): print("[hit] end",flush=True); break
            select_main(rsp); regs=rsp.read_registers(); pc=probe._pc(regs)&0xFFFFFFFF
            if pc==ENTRY:
                r4=regs.get("r4",0)&0xFFFFFFFF
                pend = rdstr(rsp,r4) if guest_ok(r4) else '(0x%08X)'%r4
            elif pc==RET:
                r3=regs.get("r3",0)&0xFFFFFFFF; k+=1
                if r3: hits+=1
                else: miss+=1
                print(f"[hit] #{k} name='{pend}' node=0x{r3:08X} {'HIT' if r3 else 'MISS'}",flush=True); pend=None
        print(f"[hit] DONE hits={hits} misses={miss}",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
