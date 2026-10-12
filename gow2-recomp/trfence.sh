set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
gcc -std=gnu11 -O0 -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/libs/video/cellGcmSys.c -o lib_cellGcmSys.o 2>cg.log && echo "gcm ok" || { echo FAIL; grep error: cg.log|head; exit 1; }
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>lk.log && echo "link ok" || { echo LINKFAIL; tail -3 lk.log; exit 1; }
echo "=== run: FIFO drenado no poll do label ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 PS3_FORCE_GCMINIT=1 PS3_BUILD_SINGLETON=1 PS3_SEM_SPIN_BREAK=4096 PS3_RSX_FIFO=1 PS3_RSX_TRACE=1 PS3_FIX_DISPLAYLIST=1 PS3_TRACE_SPURS=all timeout -k 3 14 ./boot_hle.exe ../EBOOT.ELF > fn.out 2> fn.err
echo "exit=$?"
echo "=== métodos do FIFO no poll (o fence/label-write) ==="; grep -aE "^\[rsx\]" fn.err | awk '!s[$0]++' | head -30
echo "=== avançou além do spin? GetLabelAddress count + flips + último ==="; echo "  GetLabel=$(grep -ac cellGcmGetLabelAddress fn.err) flips=$(grep -ac _cellGcmSetFlipCommand fn.err) HLE=$(grep -ac '^\[TRACE\]' fn.err) último=$(grep -aE '^\[TRACE\]' fn.err | grep -oaE '(_)?(cell|sce|sys)[A-Za-z_]+' | tail -1)"
echo "=== novas HLE (avanço)? ==="; grep -aE "^\[TRACE\]" fn.err | grep -oaE "(_)?(cell|sce|sys)[A-Za-z_]+" | sort | uniq -c | sort -rn | head -10
echo FIM
