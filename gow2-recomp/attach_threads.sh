set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
sleep 1
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  ./boot_hle.exe ../EBOOT.ELF > at.stdout 2> at.stderr &
sleep 13
WINPID=$(ps -W 2>/dev/null | grep -i "boot_hle" | awk '{print $4}' | head -1)
echo "WINPID=$WINPID  (threads do jogo no momento do loop)"
cat > at.gdb <<'EOF'
set pagination off
set width 0
info threads
echo \n==== todas as threads, topo da pilha ====\n
thread apply all bt 7
detach
quit
EOF
gdb -q --pid="$WINPID" -batch -x at.gdb 2>&1 | grep -E 'Thread [0-9]+|#[0-9]+ |Id   Target' | grep -vE 'KERNELBASE|ntdll|ucrtbase' | head -70
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
