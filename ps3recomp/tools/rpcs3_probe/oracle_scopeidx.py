"""oracle_scopeidx.py -- per-lookup: what table (map r3) and scope index does the CONSOLE see?
bp 0x002ACBE8 (lookup entry: r3=map=table+4, r4=key). g=*(0x0053FBDC) (TOC-0x159C); idx=*(g+8); arr=*(g+4);
table=arr[idx] (func_002AAB64). Ours (E389): map=4 (table NULL) from lookup #47 on. Align by key sequence with
our [KEYHASH] lines (key(r4)=...)."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=9.0)
    ap.add_argument("--hits",type=int,default=400); ap.add_argument("--deadline",type=float,default=300); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    ENTRY=0x002ACBE8; G=0x0053FBDC; n=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(ENTRY); dl=time.time()+a.deadline
        while n<a.hits and time.time()<dl:
            rsp.cont(); st=rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S",b"T"): print("[sidx] end",flush=True); break
            select_main(rsp); regs=rsp.read_registers(); pc=probe._pc(regs)&0xFFFFFFFF
            if pc!=ENTRY: continue
            n+=1; r3=regs.get('r3',0)&0xFFFFFFFF; r4=regs.get('r4',0)&0xFFFFFFFF
            g=rsp.read_u32(G); idx=rsp.read_u32(g+8) if g else 0; arr=rsp.read_u32(g+4) if g else 0
            tbl=rsp.read_u32(arr+idx*4) if arr else 0; root=rsp.read_u32(r3) if r3 else 0
            print(f"[sidx] #{n} key=0x{r4:08X} map=0x{r3:08X} root=0x{root:08X} g=0x{g:08X} idx={idx} arr=0x{arr:08X} tbl[idx]=0x{tbl:08X}",flush=True)
        print(f"[sidx] DONE n={n}",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
