set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompile ppu_loader.o ==="
g++ -std=c++20 -O0 -g -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/runtime/ppu/ppu_loader.cpp -o ppu_loader.o 2>pl.log && echo "  ok" || { echo "  FAIL"; grep -E "error:" pl.log|head; exit 1; }
echo "=== relink (10:55:50) ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>lk.log && echo "  link ok (10:55:50)" || { echo "  LINK FAIL"; grep -i "undefined\|multiple" lk.log|head; exit 1; }
echo "=== run 12s: OOBRA + spin-break ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_SEM_SPIN_BREAK=4096 PS3_SYSUTIL_BOOT=1 PS3_SYSUTIL_CB_OPD=0x524288 PS3_SYSUTIL_SINGLETON=0x6FF480 PS3_TRACE_OOBRA=1 timeout -k 3 12 ./boot_hle.exe ../EBOOT.ELF > ob.out 2> ob.err
echo "exit=$?"
echo "=== [OOBRA] capturados ==="
grep -a "^\[OOBRA\]" ob.err | head -12
echo "=== mapeando ra1/ra0 -> func (nm) ==="
