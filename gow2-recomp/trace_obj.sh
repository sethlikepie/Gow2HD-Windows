set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompila loader + relink no-ASLR $(date +%H:%M:%S) ==="
g++ -std=c++20 -O0 -g -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/runtime/ppu/ppu_loader.cpp -o ppu_loader.o 2>lo.log && echo "loader ok ($(grep -c error: lo.log) err)"
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -Wl,--disable-dynamicbase,--disable-high-entropy-va -o boot_hle.exe 2>lk.log && echo "link ok"
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1
echo "=== run PS3_TRACE_CTRL=1 (15s) $(date +%H:%M:%S) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_TRACE_CTRL=1 timeout -k 3 15 ./boot_hle.exe ../EBOOT.ELF > to.out 2> to.err
echo "=== escritas em 0x6FF480 (quem inicializa o obj ptr) ==="
grep "\[CTRL\].*006FF480\|\[CTRL\].*6FF480" to.err | head -5
echo "total writes 0x6FF480: $(grep -c '6FF480' to.err)"
echo "=== mapear ra (se houve write) ==="
nm --numeric-sort --defined-only boot_hle.exe 2>/dev/null | grep -iE ' [tT] ' > /tmp/syms.txt
for a in $(grep "6FF480" to.err | head -1 | grep -oE "[0-9A-F]{12,16}"); do
  printf "  0x%s -> " "$a"; awk -v t="$a" 'BEGIN{tt=strtonum("0x"t)}{x=strtonum("0x"$1);if(x<=tt){nm=$3}}END{print nm}' /tmp/syms.txt
done
echo "=== FIM $(date +%H:%M:%S) ==="
