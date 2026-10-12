set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompila lib_cellGcmSys $(date +%H:%M:%S) ==="
gcc -std=gnu11 -O0 -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/libs/video/cellGcmSys.c -o lib_cellGcmSys.o 2>tg.log && echo "ok" || { echo FAIL; grep error: tg.log|head; exit 1; }
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>tlk.log && echo "link ok" || { echo LINKFAIL; grep -i undefined tlk.log|head; exit 1; }
echo "=== run PS3_BUILD_SINGLETON=1 PS3_TRACE_SPURS=all (10s) $(date +%H:%M:%S) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_BUILD_SINGLETON=1 PS3_TRACE_SPURS=all timeout -k 3 10 ./boot_hle.exe ../EBOOT.ELF > gcm.out 2> gcm.err
echo "=== passou do AddressToOffset? (warning aparece = fix funcionou) ==="
grep -a "AddressToOffset" gcm.err | head -3
echo "=== HLE calls (avançou além do GCM?) — últimas 12 ==="
grep -aE "^\[TRACE\]" gcm.err | tail -12
echo "=== total HLE / crash? ==="
echo "HLE: $(grep -ac '^\[TRACE\]' gcm.err) | unresolved-indirect: $(grep -ac 'unresolved indirect' gcm.err) | FATAL: $(grep -ac FATAL gcm.err)"
echo "FIM $(date +%H:%M:%S)"
