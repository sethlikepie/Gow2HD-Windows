set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
sleep 1
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  ./boot_hle.exe ../EBOOT.ELF > am.stdout 2> am.stderr &
sleep 13
WINPID=$(ps -W 2>/dev/null | grep -i "boot_hle" | awk '{print $4}' | head -1)
echo "WINPID=$WINPID"
cat > am.gdb <<'EOF'
set pagination off
set width 0
set $base = (char*)vm_base
define rdbe
  set $raw = *(unsigned int*)($base + $arg0)
  set $rd = (($raw & 0xff)<<24)|((($raw>>8)&0xff)<<16)|((($raw>>16)&0xff)<<8)|(($raw>>24)&0xff)
end
printf "ppu_vm_size = 0x%X\n", ppu_vm_size
rdbe 0x541AD0
set $p28 = $rd
printf "[0x541AD0] gpr28 = 0x%08X\n", $p28
rdbe 0x541AD4
set $p27 = $rd
printf "[0x541AD4] gpr27 = 0x%08X\n", $p27
printf "--- estrutura em gpr28 (a flag do loop + vizinhos) ---\n"
rdbe $p28
printf "*[%08X +0] = 0x%08X  (flag de saida; loop sai se !=0)\n", $p28, $rd
rdbe ($p28+4)
printf "*[%08X +4] = 0x%08X\n", $p28, $rd
printf "--- cadeia de dispatch via gpr27 (gpr27+8 -> obj -> vtable) ---\n"
rdbe ($p27+8)
set $o = $rd
printf "[gpr27+8] = 0x%08X\n", $o
rdbe $o
printf "*[obj] = 0x%08X (procurando 0xFD86Cxxx)\n", $rd
detach
quit
EOF
gdb -q --pid="$WINPID" -batch -x am.gdb 2>&1 | grep -E 'ppu_vm_size|0x541AD|gpr2|flag|\[gpr27|\[obj|\*\[|FD86'
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
