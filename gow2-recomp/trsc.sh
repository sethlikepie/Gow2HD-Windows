set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompila loader $(date +%H:%M:%S) ==="
g++ -std=c++20 -O0 -g -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/runtime/ppu/ppu_loader.cpp -o ppu_loader.o 2>tl.log && echo "loader ok" || { echo "LOADER FAIL"; grep error: tl.log | head; exit 1; }
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>tlk.log && echo "link ok" || { echo "LINK FAIL"; grep -i 'undefined\|error' tlk.log | head; exit 1; }
echo "=== run PS3_TRACE_SC=1 (8s) $(date +%H:%M:%S) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_TRACE_SC=1 timeout -k 3 8 ./boot_hle.exe ../EBOOT.ELF > sc.out 2> sc.err
echo "=== [SC] syscalls (num/contagem/ra) ==="
grep -a "^\[SC\]" sc.err | sort | uniq -c | sort -rn | head -25
echo "=== últimas 8 linhas do stderr (onde travou) ==="
grep -av "^\[SC\]" sc.err | tail -8
echo "FIM $(date +%H:%M:%S)"
