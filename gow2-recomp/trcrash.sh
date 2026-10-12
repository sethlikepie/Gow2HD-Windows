set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
PS3=../../ps3recomp
INC="-I $PS3/include -I $PS3/runtime/ppu -I $PS3/runtime/syscalls -I $PS3/runtime/spu -I $PS3/runtime/prx -I $PS3/runtime/memory -I $PS3/libs/system -I $PS3/libs/spurs -I $PS3/libs/sync -I $PS3/libs/video -I $PS3/libs/audio -I $PS3/libs/network -I $PS3/libs/codec -I $PS3/gow2_gen -I ."
echo "=== recompila boot_main (com SEH crash handler) ==="
g++ -std=c++20 -O0 -g -c $INC $PS3/runtime/ppu/tests/boot_main.cpp -o boot_main.o 2>bm.log && echo OK || { echo FAIL; grep -m3 error: bm.log; exit 1; }
echo "=== relink ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va \
  *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o \
  -lm -lbcrypt -lole32 -luser32 -lgdi32 -ld3d12 -ldxgi -ld3dcompiler -ldxguid -luuid -lntdll \
  -o boot_hle.exe 2>lk.log && echo "LINK OK" || { echo LINKFAIL; tail -6 lk.log; exit 1; }
echo "=== run reorder (crasha no trophy) + captura RIP ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 PS3_CELLSYS_REORDER=1 PS3_TRACE_SPURS=all \
  timeout -k 3 25 ./boot_hle.exe ../EBOOT.ELF > cr.out 2> cr.err
echo "exit=$?"
echo "=== [CRASH] capturado? ==="
grep -a '\[CRASH\]' cr.err
echo "=== mapeia RIP -> func via nm ==="
RIP=$(grep -a '\[CRASH\]' cr.err | grep -oE 'rip=0x[0-9A-Fa-f]+' | head -1 | sed 's/rip=//')
echo "RIP=$RIP"
if [ -n "$RIP" ]; then
  nm -n boot_hle.exe 2>/dev/null | awk '$2 ~ /^[tT]$/ {print $1, $3}' > funcs.nm
  awk -v t="$RIP" 'BEGIN{tt=strtonum(t);best="";ba=0} {a=strtonum("0x"$1); if(a<=tt && a>ba){ba=a;best=$2}} END{printf "  CRASH em: %s (+0x%X)\n", best, tt-ba}' funcs.nm
fi
echo "=== última HLE antes do crash ==="
grep -aoE '^\[TRACE\] (_)?(cell|sce|sys)[A-Za-z_]+' cr.err | tail -4 | sed 's/\[TRACE\] //'
echo FIM
