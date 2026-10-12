set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
g++ -std=c++20 -O0 -g -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/runtime/ppu/ppu_loader.cpp -o ppu_loader.o 2>tl.log && echo "loader ok" || { echo FAIL; grep error: tl.log|head; exit 1; }
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>tlk.log && echo "link ok" || { echo LINKFAIL; grep -i undefined tlk.log|head; exit 1; }
echo "=== run (6s) $(date +%H:%M:%S) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_TRACE_SC=1 timeout -k 3 6 ./boot_hle.exe ../EBOOT.ELF > sp.out 2> sp.err
echo "=== [SPIN] obj-chain ==="
grep -a "^\[SPIN\]" sp.err | head -3
echo "=== valor real em 0x53FD40 (o S) e ao redor ==="
grep -a "num=147\|num=169\|num=128\|num=134\|num=136" sp.err | head
echo "FIM"
