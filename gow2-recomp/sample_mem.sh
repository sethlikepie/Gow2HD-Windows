set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
sleep 1
PS3_SPURS_STATUS_PTR=0x53FD40 \
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  ./boot_hle.exe ../EBOOT.ELF > sm_mem.stdout 2> sm_mem.stderr &
sleep 13
WINPID=$(ps -W 2>/dev/null | grep -i "boot_hle" | awk '{print $4}' | head -1)
echo "WINPID=$WINPID"
cat > mem_cmds.gdb <<'EOF'
set pagination off
set width 0
set $base = (char*)vm_base
printf "vm_base = %p\n", $base
# helper: read guest u32 (big-endian) at guest addr -> $rd
define rdbe
  set $raw = *(unsigned int*)($base + $arg0)
  set $rd = (($raw & 0xff)<<24)|((($raw>>8)&0xff)<<16)|((($raw>>16)&0xff)<<8)|(($raw>>24)&0xff)
end
rdbe 0x541AD0
set $p28 = $rd
printf "[0x541AD0] gpr28_src = 0x%08X\n", $p28
rdbe 0x541AD4
set $p27 = $rd
printf "[0x541AD4] gpr27_src = 0x%08X\n", $p27
rdbe $p28
printf "*gpr28 +0 (loop-exit flag, must be 0) = 0x%08X\n", $rd
rdbe ($p28+4)
printf "*gpr28 +4 = 0x%08X\n", $rd
rdbe ($p28+8)
printf "*gpr28 +8 = 0x%08X\n", $rd
rdbe ($p27+8)
printf "[gpr27+8] = 0x%08X\n", $rd
detach
quit
EOF
gdb -q --pid="$WINPID" -batch -x mem_cmds.gdb 2>&1 | grep -E 'vm_base|0x541|gpr|\*gpr|\[gpr'
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
