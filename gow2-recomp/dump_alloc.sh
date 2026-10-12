set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
sleep 1
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  ./boot_hle.exe ../EBOOT.ELF > da.stdout 2> da.stderr &
sleep 13
WINPID=$(ps -W 2>/dev/null | grep -i "boot_hle" | awk '{print $4}' | head -1)
echo "WINPID=$WINPID"
cat > da.gdb <<'EOF'
set pagination off
set width 0
set $base = (char*)vm_base
define rdbe
  set $raw = *(unsigned int*)($base + $arg0)
  set $rd = (($raw&0xff)<<24)|((($raw>>8)&0xff)<<16)|((($raw>>16)&0xff)<<8)|(($raw>>24)&0xff)
end
# [TOC+0x18C0] = ponteiro de controle do allocator usado por func_00379D00
rdbe 0x542A38
set $ctrl = $rd
printf "[TOC+0x18C0=0x542A38] alloc-ctrl ptr = 0x%08X\n", $ctrl
if $ctrl != 0 && $ctrl < 0xE0000000
  printf "--- estrutura do alloc-ctrl (0x%08X) ---\n", $ctrl
  rdbe $ctrl
  printf "  [+0x00] = 0x%08X\n", $rd
  rdbe ($ctrl+0x1C)
  printf "  [+0x1C] = 0x%08X\n", $rd
  rdbe ($ctrl+0x20)
  printf "  [+0x20] (limite usado por func_00379D00) = 0x%08X\n", $rd
  rdbe ($ctrl+0x24)
  printf "  [+0x24] = 0x%08X\n", $rd
end
# objeto da flag (0x86E118) e o ponteiro global em 0x53FD40 (status do Marco 2)
rdbe 0x53FD40
printf "[0x53FD40] = 0x%08X\n", $rd
detach
quit
EOF
gdb -q --pid="$WINPID" -batch -x da.gdb 2>&1 | grep -E 'alloc-ctrl|\[\+0x|\[0x53FD40|estrutura|limite'
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
