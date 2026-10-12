set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
sleep 1
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  ./boot_hle.exe ../EBOOT.ELF > ob.stdout 2> ob.stderr &
sleep 13
WINPID=$(ps -W 2>/dev/null | grep -i "boot_hle" | awk '{print $4}' | head -1)
echo "WINPID=$WINPID"
cat > ob.gdb <<'EOF'
set pagination off
set width 0
break vm_oob if a > 0xE0000000
continue
echo \n==== capturado no OOB ====\n
bt 8
python
import gdb
f = gdb.newest_frame(); ctx=None
while f is not None:
    if 'ps3_indirect_call' in (f.name() or ''):
        try: ctx=int(f.read_var('ctx')); break
        except: pass
    f=f.older()
if ctx:
    print("ctx=0x%X"%ctx)
    for r in (2,3,24,25,26,27,28,30,31):
        v=int(gdb.parse_and_eval("((ppu_context*)%d)->gpr[%d]"%(ctx,r)))&0xFFFFFFFF
        print("  gpr[%d]=0x%08X"%(r,v))
    # gpr[26] deve ser ~0xFD86Cxxx aqui; ler [gpr2+0x18C0] (global usado por func_00379D00)
    g2=int(gdb.parse_and_eval("((ppu_context*)%d)->gpr[2]"%ctx))&0xFFFFFFFF
    raw=int(gdb.parse_and_eval("*(unsigned int*)((char*)vm_base + %d + 0x18C0)"%g2))&0xFFFFFFFF
    be=((raw&0xff)<<24)|((raw>>8&0xff)<<16)|((raw>>16&0xff)<<8)|(raw>>24&0xff)
    print("  [TOC+0x18C0]=0x%08X (global da iteração)"%be)
end
detach
quit
EOF
timeout -k 3 12 gdb -q --pid="$WINPID" -batch -x ob.gdb 2>&1 | grep -E 'capturado|#[0-9]+ |ctx=|gpr\[|TOC\+|Breakpoint' | grep -vE 'KERNELBASE|ntdll'
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
