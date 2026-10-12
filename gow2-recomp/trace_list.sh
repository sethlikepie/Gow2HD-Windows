set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
sleep 1
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  ./boot_hle.exe ../EBOOT.ELF > tl.stdout 2> tl.stderr &
sleep 13
WINPID=$(ps -W 2>/dev/null | grep -i "boot_hle" | awk '{print $4}' | head -1)
echo "WINPID=$WINPID"
cat > tl.gdb <<'EOF'
set pagination off
set width 0
python
import gdb
gdb.execute("thread 1")
f = gdb.newest_frame()
ctx = None
while f is not None:
    nm = f.name() or ""
    if 'ps3_indirect_call' in nm:
        try:
            ctx = int(f.read_var('ctx'))
            break
        except Exception as e:
            pass
    f = f.older()
if ctx is None:
    print("ctx não encontrado")
else:
    print("ctx = 0x%X" % ctx)
    base = int(gdb.parse_and_eval("(unsigned long long)vm_base"))
    for r in (2,22,23,26,27,28,30):
        v = int(gdb.parse_and_eval("((ppu_context*)%d)->gpr[%d]" % (ctx, r))) & 0xFFFFFFFF
        print("gpr[%d] = 0x%08X" % (r, v))
    # seguir a lista a partir de gpr[26], lendo next em +0xC (big-endian)
    g26 = int(gdb.parse_and_eval("((ppu_context*)%d)->gpr[26]" % ctx)) & 0xFFFFFFFF
    print("--- seguindo lista de gpr[26]=0x%08X via [+0xC] ---" % g26)
    cur = g26
    for step in range(6):
        if cur == 0 or cur >= 0xE0000000:
            print("  nó 0x%08X -> OOB/NULL, parando" % cur); break
        raw = int(gdb.parse_and_eval("*(unsigned int*)((char*)vm_base + %d + 0xC)" % cur)) & 0xFFFFFFFF
        be = ((raw&0xff)<<24)|((raw>>8&0xff)<<16)|((raw>>16&0xff)<<8)|(raw>>24&0xff)
        print("  [0x%08X + 0xC] next = 0x%08X" % (cur, be))
        cur = be
end
detach
quit
EOF
gdb -q --pid="$WINPID" -batch -x tl.gdb 2>&1 | grep -E 'ctx =|gpr\[|nó|next|lista|não'
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
