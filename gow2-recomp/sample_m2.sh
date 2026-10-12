set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
sleep 1
PS3_SPURS_STATUS_PTR=0x53FD40 \
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  ./boot_hle.exe ../EBOOT.ELF > samp_m2.stdout 2> samp_m2.stderr &
sleep 14
WINPID=$(ps -W 2>/dev/null | grep -i "boot_hle" | awk '{print $4}' | head -1)
echo "WINPID=$WINPID"
printf 'set pagination off\nset width 0\nthread apply all bt 14\ndetach\nquit\n' > sampcmds.gdb
gdb -q --pid="$WINPID" -batch -x sampcmds.gdb > bt_m2.txt 2>&1
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
echo "===== THREADS + FUNÇÕES NO TOPO (filtrado) ====="
grep -E 'Thread [0-9]+|#[0-9]+ ' bt_m2.txt | grep -vE 'KERNELBASE|ntdll|ucrtbase|msvcrt|KERNEL32' | head -80
echo "===== quantas threads? ====="
grep -c 'Thread [0-9]' bt_m2.txt
