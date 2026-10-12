set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
export PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted"
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
sleep 1
./boot_hle.exe ../EBOOT.ELF > hang.stdout 2> hang.stderr &
sleep 12   # let it reach the hang
WINPID=$(ps -W 2>/dev/null | grep -i "boot_hle" | awk '{print $4}' | head -1)
echo "WINPID=$WINPID"
gdb -q --pid="$WINPID" -batch -x ../hang_cmds.gdb > hang_bt.txt 2>&1
echo "gdb done exit=$?"
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
echo "=== threads + backtraces ==="
cat hang_bt.txt
