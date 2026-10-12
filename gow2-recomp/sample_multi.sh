set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
sleep 1
PS3_SPURS_STATUS_PTR=0x53FD40 \
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  ./boot_hle.exe ../EBOOT.ELF > samp_multi.stdout 2> samp_multi.stderr &
sleep 12
WINPID=$(ps -W 2>/dev/null | grep -i "boot_hle" | awk '{print $4}' | head -1)
echo "WINPID=$WINPID"
printf 'set pagination off\nset width 0\nthread 1\nbt 12\ndetach\nquit\n' > sm_cmds.gdb
for i in 1 2 3 4 5; do
  echo "######## SNAPSHOT $i (Thread 1 / main) ########"
  gdb -q --pid="$WINPID" -batch -x sm_cmds.gdb 2>&1 | grep -E '#[0-9]+ ' | grep -vE 'KERNELBASE|ntdll|ucrtbase|msvcrt|KERNEL32'
  sleep 2
done
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
