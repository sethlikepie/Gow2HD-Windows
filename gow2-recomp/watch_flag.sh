set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
sleep 1
PS3_SPURS_STATUS_PTR=0x53FD40 \
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  ./boot_hle.exe ../EBOOT.ELF > wf.stdout 2> wf.stderr &
sleep 13
WINPID=$(ps -W 2>/dev/null | grep -i "boot_hle" | awk '{print $4}' | head -1)
echo "WINPID=$WINPID"
cat > wf_cmds.gdb <<'EOF'
set pagination off
set width 0
set $addr = (char*)vm_base + 0x86E118
printf "watching guest 0x86E118 at host %p (current value 0x%08X)\n", $addr, *(unsigned int*)$addr
watch *(unsigned int*)$addr
commands
  printf "*** WRITE to 0x86E118 detected ***\n"
  bt 6
  continue
end
continue
EOF
# Run gdb with a hard wall-clock cap; if the watchpoint never fires, gdb runs
# until killed -> "no write observed".
timeout -k 3 10 gdb -q --pid="$WINPID" -batch -x wf_cmds.gdb > wf_gdb.txt 2>&1
echo "=== resultado do watchpoint (8s de spin) ==="
grep -E 'watching|WRITE|#[0-9]+ |Watchpoint|Hardware' wf_gdb.txt | head -30
echo "=== (se vazio acima de 'watching', nenhuma escrita ocorreu) ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
