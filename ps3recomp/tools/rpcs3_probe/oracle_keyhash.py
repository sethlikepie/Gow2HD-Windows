"""oracle_keyhash.py -- console name->key for the loader's name lookups.
bp func_002AB1AC entry (0x002AB1AC): r4 = name ptr -> read string.
bp the func_002ACBE8 call inside it (0x002AB1F0): r4 = computed key.
Pair them in sequence and compare with ours (Master_Server=0xD5122DE0, WadServer=0xF25A08DD...)."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
def rdstr(rsp, ea, n=32):
    try:
        b = rsp.read_mem(ea, n); out=[]
        for ch in b:
            if ch < 0x20 or ch > 0x7E: break
            out.append(chr(ch))
        return ''.join(out)
    except Exception: return '?'
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=9.0)
    ap.add_argument("--pairs",type=int,default=16); ap.add_argument("--deadline",type=float,default=70); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    ENTRY=0x002AB1AC; CALL=0x002AB1F0; pending=None; k=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(ENTRY); rsp.add_breakpoint(CALL); dl=time.time()+a.deadline
        while k<a.pairs and time.time()<dl:
            rsp.cont(); st=rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S",b"T"): print("[keyhash] end",flush=True); break
            select_main(rsp); regs=rsp.read_registers(); pc=probe._pc(regs)&0xFFFFFFFF
            r4=regs.get("r4",0)&0xFFFFFFFF
            if pc==ENTRY:
                pending = rdstr(rsp,r4) if guest_ok(r4) else '(bad ptr 0x%08X)'%r4
            elif pc==CALL:
                k+=1; print(f"[keyhash] #{k} name='{pending}' key=0x{r4:08X}",flush=True); pending=None
        print("[keyhash] DONE",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
