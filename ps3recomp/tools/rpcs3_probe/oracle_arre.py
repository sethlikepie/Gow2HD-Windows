"""oracle_arre.py -- what does the CONSOLE do on the 'ARRE_DEFAULT_DC' lookup (key 0x0E611058)?
Ours (E389) spins in func_002ACC30 because the map passed in r3 has root == pool base (index 0).
bp 0x002ACBE8 (lookup entry: r3=map, r4=key); on the key of interest read the map words, then run to
0x002ACD20 (blr) and read r3 (node or 0). Also count how many lookups happen in total."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=9.0)
    ap.add_argument("--key",type=lambda x:int(x,0),default=0x0E611058); ap.add_argument("--deadline",type=float,default=420); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    ENTRY=0x002ACBE8; RET=0x002ACD20; n=0; found=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(ENTRY); dl=time.time()+a.deadline; t0=time.time()
        while time.time()<dl:
            rsp.cont(); st=rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S",b"T"): print("[arre] end",flush=True); break
            select_main(rsp); regs=rsp.read_registers(); pc=probe._pc(regs)&0xFFFFFFFF
            if pc!=ENTRY: continue
            n+=1; r3=regs.get('r3',0)&0xFFFFFFFF; r4=regs.get('r4',0)&0xFFFFFFFF
            if n%500==0: print(f"[arre] {n} lookups t={time.time()-t0:.0f}s",flush=True)
            if r4!=a.key: continue
            found+=1; w=[rsp.read_u32(r3+i) for i in (0,4,8,12)]; lr=regs.get('lr',0)&0xFFFFFFFF
            pool=rsp.read_u32(0x00541178-0x1588); pw=[rsp.read_u32(pool+i) for i in (0,4,8)]
            print(f"[arre] HIT #{found} lookup#{n} map(r3)=0x{r3:08X} words={' '.join(f'{x:08X}' for x in w)} lr=0x{lr:08X} pool=0x{pool:08X} pool[0,4,8]={' '.join(f'{x:08X}' for x in pw)}",flush=True)
            rsp.add_breakpoint(RET); rsp.cont(); st=rsp.wait_stop(deadline=dl); select_main(rsp); r=rsp.read_registers()
            print(f"[arre]   -> returns r3=0x{r.get('r3',0)&0xFFFFFFFF:08X} (node or 0) pc=0x{probe._pc(r)&0xFFFFFFFF:08X}",flush=True)
            rsp.remove_breakpoint(RET)
            if found>=3: break
        print(f"[arre] DONE lookups={n} hits_for_key={found}",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
