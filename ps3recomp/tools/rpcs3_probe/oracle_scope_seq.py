"""oracle_scope_seq.py -- CONSOLE sequence of arena-scope ENTER (func_00262C64, idx--) / LEAVE (func_00262C8C, idx++)
and registry create+push (func_002AAC84) with caller lr and the registry idx (*(0x00868C00+8)). Ours (E392): a LEAVE
from lr=0x002BB8B8 right before the loader's first lookup, registry idx 15->16; console goes 15->14."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--title",required=True); ap.add_argument("--at",type=float,default=9.0)
    ap.add_argument("--hits",type=int,default=300); ap.add_argument("--deadline",type=float,default=300); a=ap.parse_args()
    proc=probe.launch_rpcs3(None,a.title,headless=True); rsp=RSPClient(port=rpcs3_host.stub_port(),timeout=60.0)
    ENTER=0x00262C64; LEAVE=0x00262C8C; NEWREG=0x002AAC84; LK=0x002ACBE8; names={ENTER:"ENTER",LEAVE:"LEAVE",NEWREG:"NEWREG"}; n=0; nl=0; FRAME={0x002C0C34,0x002C0C9C,0x002C09CC,0x002C0BB0}; nf=0
    try:
        time.sleep(a.at); rsp.connect(retries=240,delay=0.5); rsp.handshake(); select_main(rsp)
        for b in (ENTER,LEAVE,NEWREG,LK): rsp.add_breakpoint(b)
        dl=time.time()+a.deadline
        while n<a.hits and time.time()<dl:
            rsp.cont(); st=rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S",b"T"): print("[seq] end",flush=True); break
            select_main(rsp); regs=rsp.read_registers(); pc=probe._pc(regs)&0xFFFFFFFF
            if pc==LK:
                nl+=1
                if nl in (1,10,40,45,46,47,48,50,60,100): print(f"[seq]   lookup#{nl} key=0x{regs.get('r4',0)&0xFFFFFFFF:08X} map=0x{regs.get('r3',0)&0xFFFFFFFF:08X}",flush=True)
                continue
            lr=regs.get('lr',0)&0xFFFFFFFF
            if lr in FRAME: nf+=1; continue
            n+=1; g=rsp.read_u32(0x0053FBDC); ridx=rsp.read_u32(g+8) if g else -1
            print(f"[seq] #{n} {names.get(pc,hex(pc))} lr=0x{lr:08X} reg_idx(before)={ridx} after_lookup#{nl}",flush=True)
        print(f"[seq] DONE events={n} frame_pairs_skipped={nf} lookups={nl}",flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__=="__main__": main()
