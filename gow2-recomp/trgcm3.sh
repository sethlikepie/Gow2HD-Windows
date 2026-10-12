set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
g++ -std=c++20 -O0 -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ppu_recomp_048.cpp -o ppu_recomp_048.cpp.o 2>c48.log && echo "compile ok" || { echo FAIL; grep -E "error:" c48.log|head; exit 1; }
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>lk.log && echo "link ok" || { echo LINKFAIL; tail -3 lk.log; exit 1; }
echo "=== run: FORCE_GCMINIT + singleton + spin-break ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_VM_LOW_MB=384 PS3_VM_STACK_MB=64 PS3_FORCE_GCMINIT=1 PS3_BUILD_SINGLETON=1 PS3_SEM_SPIN_BREAK=4096 PS3_TRACE_SPURS=all timeout -k 3 12 ./boot_hle.exe ../EBOOT.ELF > g3.out 2> g3.err
echo "exit=$?"
echo "=== FIX-gcm (ctx_out sane?) + _cellGcmInitBody (io set?) ==="
grep -aE "FIX-gcm|_cellGcmInitBody\(ctxOut|GcmSys\] Init\(" g3.out g3.err 2>/dev/null | head -4
echo "=== ainda crasha em 0xC0DE1111? ==="; grep -aiE "C0DE1111|unresolved indirect|OOB access" g3.err 2>/dev/null | sort | uniq -c | head -5
echo "=== HLE + GCM + último (avançou?) ==="; echo "  HLE=$(grep -ac '^\[TRACE\]' g3.err) gcm=$(grep -ac 'cellGcm' g3.err) último=$(grep -aE '^\[TRACE\]' g3.err | grep -oaE '(_)?(cell|sce|sys)[A-Za-z_]+' | tail -1)"
echo "=== FIFO em 0x0B000000? (primeiras words não-zero) ==="; echo "(checado via dump separado se avançar)"
echo FIM
