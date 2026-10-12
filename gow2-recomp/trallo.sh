set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompila chunk 024 (trace ALLOC) ==="
g++ -std=c++20 -O0 -c -I . ppu_recomp_024.cpp -o ppu_recomp_024.cpp.o 2>c24.log && echo OK || { echo FAIL; grep -m5 error: c24.log; exit 1; }
echo "=== relink ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va \
  *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o \
  -lm -lbcrypt -lole32 -luser32 -lgdi32 -ld3d12 -ldxgi -ld3dcompiler -ldxguid -luuid -lntdll \
  -o boot_hle.exe 2>lk.log && echo "LINK OK" || { echo LINKFAIL; tail -6 lk.log; exit 1; }
echo "=== run PS3_TRACE_ALLOC=1, watchdog@20s, timeout 30s ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 PS3_CELLSYS_REORDER=1 PS3_TRACE_ALLOC=1 PS3_WATCHDOG_SEC=20 \
  timeout -k 5 30 ./boot_hle.exe ../EBOOT.ELF > al.out 2> al.err
echo "exit=$?"
echo "  total ALLOC calls=$(grep -ac '\[ALLOC' al.err)"
echo "=== ultimos [ALLOC] dumps (o ultimo = a chamada que spina) ==="
grep -a '\[ALLOC' al.err | tail -20
echo "=== watchdog RIPs ==="
nm -n boot_hle.exe 2>/dev/null | awk '$2~/^[tT]$/{print $1,$3}' > fn.nm
for R in $(grep -a 'rip=0x' al.err | grep -oE 'rip=0x[0-9A-Fa-f]+' | sed 's/rip=//' | sort -u); do
  awk -v t="$R" 'BEGIN{tt=strtonum(t);b="";ba=0}{a=strtonum("0x"$1);if(a<=tt&&a>ba){ba=a;b=$2}}END{ if(b!="") printf "  %s -> %s (+0x%X)\n", t, b, tt-ba}' fn.nm
done
echo FIM
