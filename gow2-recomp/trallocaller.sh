set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompila chunk 024 $(date +%H:%M:%S) ==="
g++ -std=c++20 -O0 -c -I . ppu_recomp_024.cpp -o ppu_recomp_024.cpp.o 2>tc24.log && echo "chunk024 ok" || { echo FAIL; grep -E "error:" tc24.log|head; exit 1; }
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>tlk24.log && echo "link ok" || { echo LINKFAIL; grep -i undefined tlk24.log|head; exit 1; }
echo "=== run PS3_TRACE_ALLOC=1 $(date +%H:%M:%S) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 PS3_CELLSYS_REORDER=1 PS3_FIX_TBLSIZE=1 PS3_TRACE_ALLOC=1 timeout -k 5 25 ./boot_hle.exe ../EBOOT.ELF > ra.err 2>&1
echo "exit=$?"
echo "=== [ALLOC] com lr (caller) — foco nos sz garbage (>256MB) ==="
grep -a '\[ALLOC' ra.err | head -40
echo "FIM $(date +%H:%M:%S)"
