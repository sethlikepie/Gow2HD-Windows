set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
PS3=../../ps3recomp
INC="-I $PS3/include -I $PS3/runtime/ppu -I $PS3/runtime/syscalls -I $PS3/runtime/spu -I $PS3/runtime/prx -I $PS3/runtime/memory -I $PS3/libs/system -I $PS3/libs/spurs -I $PS3/libs/sync -I $PS3/libs/video -I $PS3/libs/audio -I $PS3/libs/network -I $PS3/libs/codec -I $PS3/gow2_gen -I ."
echo "=== recompila ppu_loader.cpp (trace HEAP) ==="
g++ -std=c++20 -O0 -g -c $INC $PS3/runtime/ppu/ppu_loader.cpp -o ppu_loader.o 2>pl.log && echo "loader OK" || { echo FAIL; grep -m5 error: pl.log; exit 1; }
echo "=== relink ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va \
  *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o \
  -lm -lbcrypt -lole32 -luser32 -lgdi32 -ld3d12 -ldxgi -ld3dcompiler -ldxguid -luuid -lntdll \
  -o boot_hle.exe 2>lk.log && echo "LINK OK" || { echo LINKFAIL; tail -6 lk.log; exit 1; }
echo "=== run PS3_TRACE_HEAP+ALLOC, timeout 22s ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 PS3_CELLSYS_REORDER=1 PS3_TRACE_HEAP=1 PS3_TRACE_ALLOC=1 \
  timeout -k 5 22 ./boot_hle.exe ../EBOOT.ELF > hp.out 2> hp.err
echo "exit=$?"
echo "  [HEAP] writes=$(grep -ac '\[HEAP\]' hp.err) | [ALLOC] calls=$(grep -ac '\[ALLOC' hp.err)"
echo "=== [ALLOC] (control block visto pelo alocador) ==="
grep -a '\[ALLOC' hp.err | head -3
echo "=== [HEAP] writes ao control block (quem inicializa) — com func do escritor ==="
nm -n boot_hle.exe 2>/dev/null | awk '$2~/^[tT]$/{print $1,$3}' > fn.nm
mapra(){ awk -v t="$1" 'BEGIN{tt=strtonum(t);b="";ba=0}{a=strtonum("0x"$1);if(a<=tt&&a>ba){ba=a;b=$2}}END{ if(b!="") printf "%s(+0x%X)", b, tt-ba; else printf "?"}' fn.nm; }
grep -a '\[HEAP\]' hp.err | head -60 | while read line; do
  ra=$(echo "$line" | grep -oE 'ra=0x[0-9A-Fa-f]+' | head -1 | sed 's/ra=//')
  addr=$(echo "$line" | grep -oE '\[0x[0-9A-Fa-f]+\]' | head -1)
  val=$(echo "$line" | grep -oE '=0x[0-9A-Fa-f]+ ra' | sed 's/ ra//;s/=//')
  printf "  %s %s  por %s\n" "$addr" "$val" "$(mapra $ra)"
done
echo "=== bin-heads (0x3005AB2..) foram escritos com 0xFFFF? ==="
grep -aE '\[HEAP\] w16 \[0x3005A[B-F]' hp.err | head -10
echo FIM
