set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
export PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted"
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
sleep 1
./boot_hle.exe ../EBOOT.ELF > samp.stdout 2> samp.stderr &
sleep 12
WINPID=$(ps -W 2>/dev/null | grep -i "boot_hle" | awk '{print $4}' | head -1)
echo "WINPID=$WINPID"
printf 'set pagination off\nset width 0\nthread apply all bt 6\ndetach\nquit\n' > /tmp_sampcmds.gdb 2>/dev/null || printf 'set pagination off\nset width 0\nthread apply all bt 6\ndetach\nquit\n' > sampcmds.gdb
for i in 1 2 3 4 5; do
  echo "######## SAMPLE $i ########"
  gdb -q --pid="$WINPID" -batch -x sampcmds.gdb 2>&1 | grep -E "^#|Thread [0-9]|func_[0-9A-F]+|cell|spu_|sys_|ps3_indirect|ZwWait|SleepCond" | head -30
  sleep 1
done
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
echo "=== [cellSysutil] RegisterCallback no stdout? ==="
grep -iE "RegisterCallback|CheckCallback|cellSysutil|cellGcm|GcmSys|flip" samp.stdout samp.stderr 2>/dev/null | sort | uniq -c | head
