set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
sleep 1
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  ./boot_hle.exe ../EBOOT.ELF > dcb.out 2> dcb.err &
sleep 8
WINPID=$(ps -W 2>/dev/null | grep -i "boot_hle" | awk '{print $4}' | head -1)
echo "WINPID=$WINPID"
cat > dcb.gdb <<'EOF'
set pagination off
set width 0
set $b = (char*)vm_base
define rdbe
  set $r = *(unsigned int*)($b+$arg0)
  set $v = (($r&0xff)<<24)|((($r>>8)&0xff)<<16)|((($r>>16)&0xff)<<8)|(($r>>24)&0xff)
  printf "  [0x%X] = 0x%08X\n", $arg0, $v
end
printf "=== ctrl ptr [0x542A38],[0x542A3C] (BE) ===\n"
rdbe 0x542A38
rdbe 0x542A3C
printf "=== control @0x881970 (BE, +0..+0x2C) ===\n"
rdbe 0x881970
rdbe 0x881974
rdbe 0x881978
rdbe 0x88197C
rdbe 0x881980
rdbe 0x881984
rdbe 0x881988
rdbe 0x88198C
rdbe 0x881990
rdbe 0x881994
rdbe 0x881998
rdbe 0x88199C
rdbe 0x8819A0
detach
quit
EOF
timeout -k 3 12 gdb -q --pid="$WINPID" -batch -x dcb.gdb 2>&1 | grep -E '\[0x|control|ctrl ptr'
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; true