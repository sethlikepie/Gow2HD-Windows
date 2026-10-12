set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
PS3=../../ps3recomp
INC="-I $PS3/include -I $PS3/runtime/ppu -I $PS3/runtime/syscalls -I $PS3/runtime/spu -I $PS3/runtime/prx -I $PS3/runtime/memory -I $PS3/libs/system -I $PS3/libs/spurs -I $PS3/libs/sync -I $PS3/libs/video -I $PS3/libs/audio -I $PS3/libs/network -I $PS3/libs/codec -I $PS3/gow2_gen -I ."
echo "=== recompila boot_main (com watchdog) ==="
g++ -std=c++20 -O0 -g -c $INC $PS3/runtime/ppu/tests/boot_main.cpp -o boot_main.o 2>bm.log && echo OK || { echo FAIL; grep -m3 error: bm.log; exit 1; }
echo "=== relink ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va \
  *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o \
  -lm -lbcrypt -lole32 -luser32 -lgdi32 -ld3d12 -ldxgi -ld3dcompiler -ldxguid -luuid -lntdll \
  -o boot_hle.exe 2>lk.log && echo "LINK OK" || { echo LINKFAIL; tail -6 lk.log; exit 1; }
echo "=== run com watchdog @12s (acha o spin PPU-only) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 PS3_CELLSYS_REORDER=1 PS3_WATCHDOG_SEC=12 \
  timeout -k 5 20 ./boot_hle.exe ../EBOOT.ELF > wd.out 2> wd.err
echo "exit=$?"
echo "=== [watchdog] RIPs capturados ==="
grep -a '\[watchdog\]' wd.err
echo "=== mapeia cada RIP no range recomp (0x141..) -> func ==="
nm -n boot_hle.exe 2>/dev/null | awk '$2 ~ /^[tT]$/{print $1,$3}' > fn.nm
for R in $(grep -a 'rip=0x' wd.err | grep -oE 'rip=0x[0-9A-Fa-f]+' | sed 's/rip=//' | sort -u); do
  awk -v t="$R" 'BEGIN{tt=strtonum(t);b="";ba=0}{a=strtonum("0x"$1);if(a<=tt&&a>ba){ba=a;b=$2}}END{ if(b!="") printf "  %s -> %s (+0x%X)\n", t, b, tt-ba; else printf "  %s -> (fora do .text / runtime)\n", t}' fn.nm
done
echo FIM
