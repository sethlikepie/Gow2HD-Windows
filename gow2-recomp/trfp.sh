set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
gcc -std=gnu11 -O0 -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/libs/video/cellGcmSys.c -o lib_cellGcmSys.o 2>cg.log && echo "gcm ok" || { echo FAIL; grep error: cg.log|head; exit 1; }
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>lk.log && echo "link ok" || { echo LINKFAIL; tail -3 lk.log; exit 1; }
echo "=== run: RSX FIFO processor + trace ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_VM_LOW_MB=384 PS3_VM_STACK_MB=64 PS3_FORCE_GCMINIT=1 PS3_BUILD_SINGLETON=1 PS3_SEM_SPIN_BREAK=4096 PS3_RSX_FIFO=1 PS3_RSX_TRACE=1 PS3_TRACE_SPURS=all timeout -k 3 12 ./boot_hle.exe ../EBOOT.ELF > fp.out 2> fp.err
echo "exit=$?"
echo "=== [rsx] métodos processados pelo FIFO processor ==="; grep -aE "^\[rsx\]" fp.err | head -40
echo "=== métodos SYNC encontrados ==="; grep -aE "\*SYNC" fp.err | sort | uniq -c | head
echo "=== HLE + último + avançou? ==="; echo "  HLE=$(grep -ac '^\[TRACE\]' fp.err) último=$(grep -aE '^\[TRACE\]' fp.err | grep -oaE '(_)?(cell|sce|sys)[A-Za-z_]+' | tail -1)"
echo "=== crash? ==="; grep -aiE "OOB access|unresolved indirect|Segmentation" fp.err fp.out 2>/dev/null | head -3
echo FIM
