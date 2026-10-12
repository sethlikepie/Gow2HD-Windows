set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
for i in 1 2 3 4; do
  taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1
  PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
    PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 \
    PS3_FORCE_GCMINIT=1 PS3_BUILD_SINGLETON=1 PS3_SEM_SPIN_BREAK=4096 \
    PS3_RSX_FIFO=1 PS3_FIX_DISPLAYLIST=1 PS3_RSX_BACKEND=trace \
    PS3_RSX_TRACE=1 PS3_RSX_DUMP=1 \
    timeout -k 3 18 ./boot_hle.exe ../EBOOT.ELF > b$i.out 2> b$i.err
  ec=$?
  echo "--- run $i exit=$ec | GetCtrl=$(grep -ac cellGcmGetControlRegister b$i.err) flips=$(grep -ac 'SetFlipCommand' b$i.err) GetLabel=$(grep -ac cellGcmGetLabelAddress b$i.err) ---"
  echo "    FIFO dump linhas=$(grep -ac 'fifo\[0x' b$i.err)  walker[rsx]m=$(grep -ac '\[rsx\] m=' b$i.err)  *SYNC=$(grep -ac '\*SYNC' b$i.err)"
  echo "    bridge: CLEAR=$(grep -ac 'rsx-bridge] CLEAR' b$i.err) DRAW_A=$(grep -ac 'DRAW_ARRAYS' b$i.err) DRAW_I=$(grep -ac 'DRAW_INDEXED' b$i.err) SHADER=$(grep -ac 'rsx-bridge] SET_SHADER' b$i.err)"
  echo "    métodos distintos decodificados pelo walker:"
  grep -aoE '\[rsx\] m=0x[0-9A-Fa-f]+' b$i.err | sort -u | awk '{printf "      %s\n",$0}' | head -30
done
echo "=== melhor run (mais [rsx] m=) — amostra do dump FIFO ==="
best=$(for i in 1 2 3 4; do echo "$(grep -ac '\[rsx\] m=' b$i.err) $i"; done | sort -rn | head -1 | awk '{print $2}')
echo "best=run $best"
grep -aE 'fifo\[0x|RSX\] put=|\[rsx\] m=' b$best.err | head -40
echo FIM
