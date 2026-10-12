set +e
PS3=../../ps3recomp
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
INC="-I $PS3/include -I $PS3/runtime/ppu -I $PS3/runtime/syscalls -I $PS3/runtime/spu -I $PS3/runtime/prx -I $PS3/runtime/memory -I $PS3/libs/system -I $PS3/libs/spurs -I $PS3/libs/sync -I $PS3/libs/video -I $PS3/libs/audio -I $PS3/libs/network -I $PS3/libs/codec -I ."
echo "=== recompila lib_cellSysutil (com cbcaller) ==="
gcc -std=gnu11 -O0 -c $INC $PS3/libs/system/cellSysutil.c -o lib_cellSysutil.o 2>cs.log && echo "OK" || { echo FAIL; grep -m3 error: cs.log; exit 1; }
echo "=== relink (+ntdll) ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va \
  *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o \
  -lm -lbcrypt -lole32 -luser32 -lgdi32 -ld3d12 -ldxgi -ld3dcompiler -ldxguid -luuid -lntdll \
  -o boot_hle.exe 2>lk.log && echo "LINK OK" || { echo LINKFAIL; tail -6 lk.log; exit 1; }
echo "=== run com PS3_TRACE_CBCALLER ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 PS3_TRACE_CBCALLER=1 \
  timeout -k 3 15 ./boot_hle.exe ../EBOOT.ELF > cbc.out 2> cbc.err
echo "exit=$?"
echo "=== frames cbcaller capturados ==="
grep -a '\[cbcaller\]' cbc.err | head -4
echo "=== mapeia host-addr -> func_<addr> (nm, exe no-ASLR) ==="
nm -n boot_hle.exe 2>/dev/null | awk '$2 ~ /^[tT]$/ {print $1, $3}' > funcs.nm
echo "  funcs no nm=$(wc -l < funcs.nm)"
# pega a 2a linha de frames (r3=0x0, loop estável), mapeia cada addr
LINE=$(grep -a '\[cbcaller\]' cbc.err | sed -n '2p' | sed 's/\[cbcaller\]//')
echo "  frames: $LINE"
for a in $LINE; do
  ad=$(printf "%d" "$a" 2>/dev/null)
  [ -z "$ad" ] && continue
  awk -v t="$ad" 'BEGIN{best="";ba=0} {a=strtonum("0x"$1); if(a<=t && a>ba){ba=a;best=$2}} END{printf "    %s -> %s (+0x%X)\n", t, best, t-ba}' funcs.nm
done
echo FIM
