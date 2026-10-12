set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== compile 2 libs alteradas ==="
gcc -std=gnu11 -O0 -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/libs/video/cellGcmSys.c -o lib_cellGcmSys.o 2>cg.log && echo "  cellGcmSys ok" || { echo "  cellGcmSys FAIL"; grep -E "error:" cg.log|head; exit 1; }
gcc -std=gnu11 -O0 -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/runtime/syscalls/sys_semaphore.c -o lib_sys_semaphore.o 2>cs.log && echo "  sys_semaphore ok" || { echo "  sys_semaphore FAIL"; grep -E "error:" cs.log|head; exit 1; }
echo "=== relink (10:30:09) ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>lk.log && echo "  link ok (10:30:09)" || { echo "  LINK FAIL"; grep -i "undefined\|multiple" lk.log|head; exit 1; }
echo "=== run 15s: spin-break=4096 + bypass cellSysutil ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_SEM_SPIN_BREAK=4096 PS3_SYSUTIL_BOOT=1 PS3_SYSUTIL_CB_OPD=0x524288 PS3_SYSUTIL_SINGLETON=0x6FF480 PS3_TRACE_SC=1 PS3_TRACE_SPURS=all timeout -k 3 15 ./boot_hle.exe ../EBOOT.ELF > sp2.out 2> sp2.err
echo "exit=$?"
echo "OOB=$(grep -ac 'OOB access' sp2.err) | HLE=$(grep -ac '^\[TRACE\]' sp2.err)"
echo "=== top syscalls (depois do spin-break) ==="; grep -a "^\[SC\]" sp2.err | grep -oE "num=[0-9]+" | sort | uniq -c | sort -rn | head -8
echo "=== syscall 92: distintos (ra, sem_id) — apareceu NOVO wait? ==="; grep -aE "^\[SC\] T[0-9]+ num=92 " sp2.err | grep -oaE "r3=0x[0-9A-Fa-f]+ ra=[0-9A-Fa-f]+" | sort -u | head
echo "=== últimas 24 HLE distintas (avançou?) ==="; grep -aE "^\[TRACE\]" sp2.err | grep -oaE "(_)?(cell|sce|sys)[A-Za-z_]+" | tail -50 | awk '!s[$0]++' | tail -24
echo "=== crash/fatal? ==="; grep -aiE "OOB access|unresolved|FATAL|Segmentation|abort" sp2.err | tail -4
echo FIM
