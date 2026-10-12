set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
sleep 1
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  ./boot_hle.exe ../EBOOT.ELF > ao.stdout 2> ao.stderr &
sleep 13
WINPID=$(ps -W 2>/dev/null | grep -i "boot_hle" | awk '{print $4}' | head -1)
echo "WINPID=$WINPID"
cat > ao.gdb <<'EOF'
set pagination off
set width 0
thread 1
echo \n==== BACKTRACE thread main (no loop OOB) ====\n
bt 24
detach
quit
EOF
gdb -q --pid="$WINPID" -batch -x ao.gdb 2>&1 | grep -E '#[0-9]+ ' | grep -vE 'KERNELBASE|ntdll|ucrtbase|msvcrt' | head -26
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
