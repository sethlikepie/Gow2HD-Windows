set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompila loader $(date +%H:%M:%S) ==="
g++ -std=c++20 -O0 -g -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/runtime/ppu/ppu_loader.cpp -o ppu_loader.o 2>tl.log && echo "loader ok" || { echo FAIL; grep error: tl.log|head; exit 1; }
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
echo "=== link no-ASLR ==="
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>tlk.log && echo "link ok" || { echo LINKFAIL; grep -i undefined tlk.log|head; exit 1; }
echo "=== run PS3_TRACE_SC=1 (8s) $(date +%H:%M:%S) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_TRACE_SC=1 timeout -k 3 8 ./boot_hle.exe ../EBOOT.ELF > tid.out 2> tid.err
echo "=== [SC] por thread (qual T gira no 141?) ==="
grep -a "^\[SC\]" tid.err | sed -E 's/#[0-9]+ //' | sort | uniq -c | sort -rn | head -20
echo "=== ra distintos no 141 ==="
grep -aoE "num=141 .*ra=0x[0-9A-Fa-f]+" tid.err | grep -oE "ra=0x[0-9A-Fa-f]+" | sort -u
echo "=== nm map dos ra (func mais próximo abaixo) ==="
nm --numeric-sort boot_hle.exe 2>/dev/null | grep -E " [Tt] " | awk '{print $1, $3}' > nm.sorted
for ra in $(grep -aoE "ra=0x[0-9A-Fa-f]+" tid.err | sort -u | sed 's/ra=//'); do
  d=$(printf "%d" "$ra" 2>/dev/null)
  awk -v r="$d" 'BEGIN{best="";ba=0} { a=strtonum("0x"$1); if(a<=r && a>ba){ba=a;best=$2} } END{printf "  ra=0x%X -> %s (@0x%X)\n", r, best, ba}' nm.sorted
done
echo "FIM $(date +%H:%M:%S)"
