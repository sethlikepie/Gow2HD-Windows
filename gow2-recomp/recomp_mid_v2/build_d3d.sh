#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
PS3=../../ps3recomp
INC="-I $PS3/include -I $PS3/runtime/ppu -I $PS3/runtime/syscalls -I $PS3/runtime/spu -I $PS3/runtime/prx -I $PS3/runtime/memory -I $PS3/libs/system -I $PS3/libs/spurs -I $PS3/libs/sync -I $PS3/libs/video -I $PS3/libs/audio -I $PS3/libs/network -I $PS3/libs/codec -I $PS3/gow2_gen -I ."

echo "=== compile rsx_d3d12_backend $(date +%H:%M:%S) ==="
gcc -std=gnu11 -O0 -c $INC $PS3/libs/video/rsx_d3d12_backend.c -o lib_rsx_d3d12_backend.o 2>d3d.cclog
echo "errors=$(grep -c 'error:' d3d.cclog || true)"
grep 'error:' d3d.cclog | head -15 || true
ls -la lib_rsx_d3d12_backend.o

echo "=== relink $(date +%H:%M:%S) ==="
taskkill //F //IM boot_v2_new.exe >/dev/null 2>&1 || true
sleep 1
rm -f boot_v2_new.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va -Wl,--stack,33554432 \
  *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o \
  -lm -lbcrypt -lole32 -luser32 -lgdi32 -ld3d12 -ldxgi -ld3dcompiler -ldxguid -luuid -lntdll -lwinmm -lxinput \
  -o boot_v2_new.exe 2>tlkfl_bc.log
echo LINK_OK
ls -la boot_v2_new.exe
strings boot_v2_new.exe | grep -E 'PIXELS|frame_movie' | head
echo FIM $(date +%H:%M:%S)
