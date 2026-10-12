set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompila loader + chunk040 $(date +%H:%M:%S) ==="
g++ -std=c++20 -O0 -g -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/runtime/ppu/ppu_loader.cpp -o ppu_loader.o 2>tl.log && echo "loader ok($(grep -c error: tl.log))"
g++ -std=c++20 -O0 -c -I . ppu_recomp_040.cpp -o ppu_recomp_040.cpp.o 2>tc.log && echo "chunk040 ok($(grep -c error: tc.log))"
grep error: tl.log tc.log | head -3
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>tlk.log && echo "link ok"
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1
echo "=== run PS3_TRACE_T=1 (12s) $(date +%H:%M:%S) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_TRACE_T=1 timeout -k 3 12 ./boot_hle.exe ../EBOOT.ELF > tr.out 2> tr.err
echo "=== traces [T*] (func_0024A7CC: roda? qual gate pula?) ==="
grep -E "^\[T[0-9]+\]" tr.err | sort | uniq -c | head -20
echo "=== sequência (primeiras 12) ==="
grep -E "^\[T[0-9]+\]" tr.err | head -12
echo "=== OOB ainda?  ==="
echo "FIM $(date +%H:%M:%S)"
