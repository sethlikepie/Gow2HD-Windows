set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
sleep 1
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  ./boot_hle.exe ../EBOOT.ELF > pf.stdout 2> pf.stderr &
sleep 13
WINPID=$(ps -W 2>/dev/null | grep -i "boot_hle" | awk '{print $4}' | head -1)
echo "WINPID=$WINPID  -- forçando flag [0x86E118]=0xFFFFFFFF e soltando"
cat > pf.gdb <<'EOF'
set pagination off
set {unsigned int}((char*)vm_base + 0x86E118) = 0xFFFFFFFF
printf "flag escrita; valor agora = 0x%08X\n", *(unsigned int*)((char*)vm_base+0x86E118)
detach
quit
EOF
gdb -q --pid="$WINPID" -batch -x pf.gdb 2>&1 | grep -E 'flag escrita'
echo "--- boot continua por 6s com a flag setada ---"
sleep 6
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
echo "=== avançou além do loop? linhas NOVAS no stderr (sem OOB) ==="
grep -vE '\[vm\] OOB' pf.stderr | tail -12
echo "=== stdout (TTY) ==="
tail -6 pf.stdout