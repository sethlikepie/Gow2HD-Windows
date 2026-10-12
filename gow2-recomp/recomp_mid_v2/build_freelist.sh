set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid_v2
PS3=../../ps3recomp
INC="-I $PS3/include -I $PS3/runtime/ppu -I $PS3/runtime/syscalls -I $PS3/runtime/spu -I $PS3/runtime/prx -I $PS3/runtime/memory -I $PS3/libs/system -I $PS3/libs/spurs -I $PS3/libs/sync -I $PS3/libs/video -I $PS3/libs/audio -I $PS3/libs/network -I $PS3/libs/codec -I $PS3/gow2_gen -I ."
echo "=== compila ppu_loader.cpp $(date +%H:%M:%S) ==="
g++ -std=c++20 -O0 -c $INC $PS3/runtime/ppu/ppu_loader.cpp -o ppu_loader.o 2>ldr.cclog
echo "loader err=$(grep -c 'error:' ldr.cclog)"; grep 'error:' ldr.cclog | head
echo "=== relink $(date +%H:%M:%S) ==="
taskkill //F //IM boot_v2_new.exe >/dev/null 2>&1; sleep 1; rm -f boot_v2_new.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va -Wl,--stack,33554432 \
  *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o \
  -lm -lbcrypt -lole32 -luser32 -lgdi32 -ld3d12 -ldxgi -ld3dcompiler -ldxguid -luuid -lntdll -lwinmm -lxinput \
  -o boot_v2_new.exe 2>tlkfl.log && echo "LINK OK" || { echo LINKFAIL; grep -i 'undefined' tlkfl.log | sort -u | head; exit 1; }
ls -la boot_v2_new.exe | awk '{print "exe:",$5,$6,$7,$8}'
echo FIMBUILD $(date +%H:%M:%S)
