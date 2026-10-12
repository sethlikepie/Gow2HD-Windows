"""oracle_t21.py -- CONSOLE: type-21 handler func_002BA3F0 entry: r3, r4=record payload, its first words, lr.
Ours reads size=bswap(*(r4))=0xFFFFFFFF on the first call; console gets 0x0007A120 (E393)."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=9.0)
    ap.add_argument("--hits",type=int,default=4); ap.add_argument("--deadline",type=float,default=360); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    BP=0x002BA3F0; n=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(BP); dl=time.time()+a.deadline
        while n<a.hits and time.time()<dl:
            rsp.cont(); st=rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S",b"T"): print("[t21] end",flush=True); break
            select_main(rsp); r=rsp.read_registers(); pc=probe._pc(r)&0xFFFFFFFF
            if pc!=BP: continue
            n+=1; r3=r.get('r3',0)&0xFFFFFFFF; b=r.get('r4',0)&0xFFFFFFFF; lr=r.get('lr',0)&0xFFFFFFFF
            w=[rsp.read_u32(b+i*4) for i in range(6)]; ts=rsp.read_u32(0x0053FF4C); f=rsp.read_u32(ts+0x1B0) if ts else -1
            print(f"[t21] #{n} r3=0x{r3:08X} buf(r4)=0x{b:08X} words: {' '.join(f'{x:08X}' for x in w)} lr=0x{lr:08X} ts=0x{ts:08X} ts1B0={f}",flush=True)
            st_=rsp.read_u32(ts+0x1CC); rem=rsp.read_u32(ts+0x1AC); f64=rsp.read_u32(ts+0x64); f74=rsp.read_u32(ts+0x74)
            print(f"[t21]    loader: state={st_} rem=0x{rem:08X} ts+0x64=0x{f64:08X} ts+0x74=0x{f74:08X}",flush=True)
            try:
                fr=probe.walk_stack(rsp,r,max_frames=12); print("[t21]    stack: "+" <- ".join(f"0x{x['pc']:08X}" for x in fr),flush=True)
            except Exception as e: print(f"[t21]    stack: err {e}",flush=True)
        print(f"[t21] DONE n={n}",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
