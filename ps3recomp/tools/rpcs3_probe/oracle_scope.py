"""oracle_scope.py -- does the console's scope stack overflow (idx>lim) like ours? (E353m)
bp func_0042A0C8 (0x0042A0C8, the orphan-push function) + func_00262C8C (0x00262C8C push) + func_00262C64
(0x00262C64 pop); read scope idx/lim from *(0x0053F0A0)->+4/+8. If console idx stays <=lim-1, the overflow
is OURS (a lift bug in func_0042A0C8 / its caller)."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
STK_PTR = 0x0053F0A0  # *(TOC 0x00541178 - 0x20D8) -> scope stack struct
def rd_idx(rsp):
    try:
        stk = rsp.read_u32(STK_PTR)
        if not guest_ok(stk): return (stk,-1,-1)
        return (stk, rsp.read_u32(stk+4), rsp.read_u32(stk+8))
    except Exception: return (0,-1,-1)
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--title", required=True)
    ap.add_argument("--at", type=float, default=8.0); ap.add_argument("--hits", type=int, default=200); ap.add_argument("--deadline", type=float, default=85)
    a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True)
    rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0); t0=time.time(); k=0
    try:
        time.sleep(a.at); rsp.connect(retries=240, delay=0.5); rsp.handshake(); select_main(rsp)
        BP_A0C8=0x0042A0C8
        rsp.add_breakpoint(BP_A0C8)
        dl=time.time()+a.deadline; maxidx=-1
        while k<a.hits and time.time()<dl:
            rsp.cont(); stop=rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S",b"T"): print("[scope] no stop/end",flush=True); break
            select_main(rsp); k+=1
            stk,idx,lim=rd_idx(rsp)
            if idx>maxidx: maxidx=idx
            if k<=8 or idx>=118 or (k%40)==0:
                print(f"[scope] #{k} t={time.time()-t0:.1f} func_0042A0C8 hit: scope idx={idx} lim={lim} (max={maxidx})",flush=True)
        print(f"[scope] DONE hits={k} MAX idx={maxidx} lim(last)={lim}",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
