set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompila chunk 042 $(date +%H:%M:%S) ==="
g++ -std=c++20 -O0 -c -I . ppu_recomp_042.cpp -o ppu_recomp_042.cpp.o 2>tc.log && echo "chunk042 ok" || { echo FAIL; grep error: tc.log|head; exit 1; }
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>tlk.log && echo "link ok" || { echo LINKFAIL; grep -i undefined tlk.log|head; exit 1; }
echo "=== run PS3_FORCE_SINGLETON=1 PS3_TRACE_T=1 PS3_TRACE_SC=1 (10s) $(date +%H:%M:%S) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_FORCE_SINGLETON=1 PS3_TRACE_T=1 PS3_TRACE_SC=1 timeout -k 3 10 ./boot_hle.exe ../EBOOT.ELF > fc.out 2> fc.err
echo "=== T-traces (gates de func_0024A7CC + construção + [0x6FF480]) ==="
grep -aoE "\[T[0-9]+\]( 0x[0-9A-Fa-f]+)?" fc.err | sort | uniq -c | head -30
echo "=== 141 ainda gira? (max #) ==="
grep -aoE "num=141 #[0-9]+" fc.err | grep -oE "[0-9]+$" | sort -n | tail -1
echo "=== [SPIN] (obj ainda NULL?) ==="
grep -a "^\[SPIN\]" fc.err | head -2
echo "=== boot avançou / crashou? (stdout + stderr não-SC, tail) ==="
tail -6 fc.out
grep -av "^\[SC\]\|^\[T" fc.err | tail -12
echo "FIM $(date +%H:%M:%S)"
