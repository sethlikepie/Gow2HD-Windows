"""oracle_attach.py - FUN_0024c3ec(parent=r3, child=r4): child class = u16 at child+2; count/print attaches during a window."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import guest_ok
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=4.0); ap.add_argument("--hits",type=int,default=200); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0); t0=time.time(); k=0; cls={}
    try:
        time.sleep(a.at); rsp.connect(retries=120,delay=0.5); rsp.handshake(); rsp.add_breakpoint(0x0024C3EC); rsp.add_breakpoint(0x002B4340); dl=time.time()+180
        while k<a.hits and time.time()<dl:
            rsp.cont(); stop=rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S",b"T"): print("[att] no stop",flush=True); break
            regs=rsp.read_registers(); pc=probe._pc(regs); k+=1
            if pc==0x002B4340:
                nm=None
                for rr in (4,6):
                    ea=regs.get(f"r{rr}",0)&0xFFFFFFFF
                    if guest_ok(ea):
                        s=rsp.read_mem(ea,18).split(b"\0")[0]
                        if 2<=len(s)<18 and all(32<=c<127 for c in s): nm=s.decode(); break
                print(f"[att] --- WAD open {nm} t={time.time()-t0:.1f} (attaches ate' aqui: {dict(sorted(cls.items()))})",flush=True); continue
            parent=regs.get("r3",0)&0xFFFFFFFF; child=regs.get("r4",0)&0xFFFFFFFF
            c=int.from_bytes(rsp.read_mem(child+2,2),"big") if guest_ok(child) else -1
            pw0=rsp.read_u32(parent) if guest_ok(parent) else 0; psub=(pw0>>16)&0xFFF
            key=(psub,c); cls[key]=cls.get(key,0)+1
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
        print("[att] TOTAL (parent_subtag -> child_class : count):",flush=True)
    for (ps,cc),n in sorted(cls.items()):
        if ps==3: print(f"[att]   parent classe 3 (ANMX) -> filho classe {cc} : {n}",flush=True)
    print("[att]   (todos):",dict(sorted(cls.items())),flush=True)
main()
