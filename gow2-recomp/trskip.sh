set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompila chunk 048 $(date +%H:%M:%S) ==="
g++ -std=c++20 -O0 -c -I . ppu_recomp_048.cpp -o ppu_recomp_048.cpp.o 2>tc.log && echo "chunk048 ok" || { echo FAIL; grep error: tc.log|head; exit 1; }
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>tlk.log && echo "link ok" || { echo LINKFAIL; grep -i undefined tlk.log|head; exit 1; }
echo "=== run PS3_SKIP_WAIT=1 PS3_TRACE_T=1 PS3_TRACE_SC=1 (10s) $(date +%H:%M:%S) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_SKIP_WAIT=1 PS3_TRACE_T=1 PS3_TRACE_SC=1 timeout -k 3 10 ./boot_hle.exe ../EBOOT.ELF > sk.out 2> sk.err
echo "=== T-traces (func_0025C838 chega ao #7? func_0024A7CC roda?) ==="
grep -aoE "\[T[0-9]+\]" sk.err | uniq -c | head -25
echo "=== 141 ainda gira? (max #) ==="
grep -aoE "num=141 #[0-9]+" sk.err | grep -oE "[0-9]+$" | sort -n | tail -1
echo "=== syscalls NOVOS após o skip (top) ==="
grep -a "^\[SC\]" sk.err | grep -oE "num=[0-9]+" | sort | uniq -c | sort -rn | head -12
echo "=== boot avançou? (stdout tail + stderr não-SC tail) ==="
tail -8 sk.out
grep -av "^\[SC\]\|^\[T" sk.err | tail -10
echo "FIM $(date +%H:%M:%S)"
