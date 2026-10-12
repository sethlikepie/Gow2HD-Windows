set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
PS3=../../ps3recomp
INC="-I $PS3/include -I $PS3/runtime/ppu -I $PS3/runtime/syscalls -I $PS3/runtime/spu -I $PS3/runtime/prx -I $PS3/runtime/memory -I $PS3/libs/system -I $PS3/libs/spurs -I $PS3/libs/sync -I $PS3/libs/video -I $PS3/libs/audio -I $PS3/libs/network -I $PS3/libs/codec -I $PS3/gow2_gen -I ."
echo "=== 1. recompila ppu_loader.cpp (helpers value-CAS) $(date +%H:%M:%S) ==="
g++ -std=c++20 -O0 -g -c $INC $PS3/runtime/ppu/ppu_loader.cpp -o ppu_loader.o 2>pl.log && echo "loader OK" || { echo "LOADER FAIL"; grep -m5 error: pl.log; exit 1; }
echo "=== 2. transforma chunks com atomics (reserve_addr) -> ppu_lwarx/stwcx/ldarx/stdcx ==="
> cas_changed.txt
for f in $(grep -l "reserve_addr" ppu_recomp_*.cpp); do
  [ -f "$f.bakcas" ] || cp "$f" "$f.bakcas"
  perl -i -p ../cas.pl "$f"
  echo "$f" >> cas_changed.txt
done
echo "transformados=$(wc -l < cas_changed.txt) | residual reserve_addr=$(grep -l reserve_addr ppu_recomp_*.cpp 2>/dev/null | wc -l)"
echo "=== 3. recompila transformados (-P 3) $(date +%H:%M:%S) ==="
cat cas_changed.txt | xargs -P 3 -I {} sh -c 'g++ -std=c++20 -O0 -c -I . "$1" -o "$1.o" 2>"$1.cascc"' _ {}
ERR=$(cat $(sed 's/$/.cascc/' cas_changed.txt) 2>/dev/null | grep -c 'error:')
echo "erros de compile=$ERR $(date +%H:%M:%S)"
if [ "$ERR" -gt 0 ]; then echo "ERROS (primeiros):"; grep -h 'error:' *.cascc 2>/dev/null | head -10; echo "ABORT"; exit 1; fi
echo "=== 4. relink ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -luser32 -lgdi32 -ld3d12 -ldxgi -ld3dcompiler -ldxguid -luuid -lntdll -o boot_hle.exe 2>lk.log && echo "LINK OK" || { echo "LINK FAIL"; grep -i 'undefined\|error' lk.log | head; exit 1; }
echo "=== 5. run watchdog@20s timeout 30s (regressao? boot ainda passa func_0014852C?) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 PS3_CELLSYS_REORDER=1 PS3_TRACE_SPURS=all PS3_WATCHDOG_SEC=20 timeout -k 5 30 ./boot_hle.exe ../EBOOT.ELF > cas.out 2> cas.err
echo "exit=$? | HLE=$(grep -ac '^\[TRACE\]' cas.err) (era 994) | CRASH=$(grep -ac '\[CRASH\]' cas.err)"
nm -n boot_hle.exe 2>/dev/null | awk '$2~/^[tT]$/{print $1,$3}' > fn.nm
for R in $(grep -a 'rip=0x' cas.err | grep -oE 'rip=0x[0-9A-Fa-f]+' | sed 's/rip=//' | sort -u); do
  awk -v t="$R" 'BEGIN{tt=strtonum(t);b="";ba=0}{a=strtonum("0x"$1);if(a<=tt&&a>ba){ba=a;b=$2}}END{ if(b!="") printf "  %s -> %s (+0x%X)\n", t, b, tt-ba}' fn.nm
done
echo "DONE_CAS"
