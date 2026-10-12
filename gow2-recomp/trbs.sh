cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
for r in 2 3 4; do
  taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1
  PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_VM_LOW_MB=384 PS3_VM_STACK_MB=64 PS3_BUILD_SINGLETON=1 PS3_TRACE_SPURS=all timeout -k 2 9 ./boot_hle.exe ../EBOOT.ELF > br$r.out 2> br$r.err
  built=$(grep -ac "FIX-b" br$r.err); gcm=$(grep -ac "cellGcm" br$r.err); np=$(grep -ac "sceNpInit" br$r.err)
  last=$(grep -aE "^\[TRACE\]" br$r.err | grep -oaE "(_)?(cell|sce|sys)[A-Za-z_]+" | tail -1)
  echo "run$r: built=$built sceNp=$np gcm=$gcm last=$last"
done
echo DONE
