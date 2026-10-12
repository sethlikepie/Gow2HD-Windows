"""oracle_lgl_state.py -- what is the LOADER state on the console while it processes R_LglScA?
bp func_002AB1AC (name lookup): read name (r4) + loader type_sys state/rem/body1D4.
Ours during LglScA: WADLD-SM state=1 rem=0 (container never opened) and 0 '_R_LglScA' lookups."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
TS_PTR=0x0053FF4C
def rdstr(rsp, ea, n=28):
    try:
        b=rsp.read_mem(ea,n); o=[]
        for ch in b:
            if ch<0x20 or ch>0x7E: break
            o.append(chr(ch))
        return ''.join(o)
    except Exception: return '?'
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=9.0)
    ap.add_argument("--hits",type=int,default=14); ap.add_argument("--deadline",type=float,default=65); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0); k=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(0x002AB1AC); dl=time.time()+a.deadline
        while k<a.hits and time.time()<dl:
            rsp.cont(); st=rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S",b"T"): print("[lgl] end",flush=True); break
            select_main(rsp); regs=rsp.read_registers(); k+=1
            r4=regs.get("r4",0)&0xFFFFFFFF
            nm = rdstr(rsp,r4) if guest_ok(r4) else '(0x%08X)'%r4
            try:
                ts=rsp.read_u32(TS_PTR)
                if guest_ok(ts):
                    state=rsp.read_u32(ts+0x1CC); rem=rsp.read_u32(ts+0x1AC); b1d4=rsp.read_u32(ts+0x1D4)
                    ring=rsp.read_u32(ts+0x1A8); av=rsp.read_u32(ring+0x10) if guest_ok(ring) else 0
                    print(f"[lgl] #{k} name='{nm}' loader: state={state} rem=0x{rem:08X} body1D4=0x{b1d4:08X} avail=0x{av:X}",flush=True)
                else:
                    print(f"[lgl] #{k} name='{nm}' ts=0x{ts:08X} (bad)",flush=True)
            except Exception as e:
                print(f"[lgl] #{k} name='{nm}' read err {e}",flush=True)
        print("[lgl] DONE",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
