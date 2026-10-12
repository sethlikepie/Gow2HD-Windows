set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
sleep 1
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  ./boot_hle.exe ../EBOOT.ELF > wc.out 2> wc.err &
sleep 1   # attach cedo, antes da init do allocator
WINPID=$(ps -W 2>/dev/null | grep -i "boot_hle" | awk '{print $4}' | head -1)
echo "WINPID=$WINPID"
cat > wc.gdb <<'EOF'
set pagination off
set width 0
set $base = (char*)vm_base
printf "=== no attach: [ctrl+0]=0x%08X  [ctrl+0x20]=0x%08X ===\n", *(unsigned int*)($base+0x881970), *(unsigned int*)($base+0x881990)
# watchpoint de hardware no campo limite (+0x20) e no campo count (+0)
watch *(unsigned int*)($base+0x881990)
watch *(unsigned int*)($base+0x881970)
commands
  printf ">>> WRITE: [ctrl+0]=0x%08X [ctrl+0x20]=0x%08X\n", *(unsigned int*)($base+0x881970), *(unsigned int*)($base+0x881990)
  bt 10
  continue
end
continue
EOF
timeout -k 3 16 gdb -q --pid="$WINPID" -batch -x wc.gdb 2>&1 \
  | grep -E 'no attach|WRITE|func_[0-9A-F]|Hardware watch|#[0-9]+ ' | grep -vE 'KERNELBASE|ntdll|msvcrt' | head -60
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; true