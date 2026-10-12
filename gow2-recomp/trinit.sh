set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
gcc -std=gnu11 -O0 -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/libs/video/cellGcmSys.c -o lib_cellGcmSys.o 2>cg.log && echo "gcm ok" || { echo FAIL; grep error: cg.log|head; exit 1; }
g++ -std=c++20 -O0 -g -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/gow2_gen/ppu_hle_nids.cpp -o ppu_hle_nids.o 2>cn.log && echo "nids ok" || { echo FAIL; grep error: cn.log|head; exit 1; }
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>lk.log && echo "link ok" || { echo LINKFAIL; grep -i undefined lk.log|head; exit 1; }
echo "=== run: _cellGcmInitBody chamado? GCM inicializa? ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_SEM_SPIN_BREAK=4096 PS3_SYSUTIL_BOOT=1 PS3_SYSUTIL_CB_OPD=0x524288 PS3_SYSUTIL_SINGLETON=0x6FF480 PS3_TRACE_SPURS=all timeout -k 3 12 ./boot_hle.exe ../EBOOT.ELF > ri.out 2> ri.err
echo "exit=$?"
echo "=== _cellGcmInitBody / cellGcmInit logs (stdout+stderr) ==="
grep -aE "_cellGcmInitBody|GcmSys\] Init\(|cellGcmInit" ri.out ri.err 2>/dev/null | head
echo "=== é chamado via HLE? (unresolved 15BAE46B? ou TRACE?) ==="
grep -aE "15BAE46B|_cellGcmInitBody|cellGcmGetControlRegister|AddressToOffset" ri.err | head
echo FIM
