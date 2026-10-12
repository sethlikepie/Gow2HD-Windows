set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompila chunk 042 $(date +%H:%M:%S) ==="
g++ -std=c++20 -O0 -c -I . ppu_recomp_042.cpp -o ppu_recomp_042.cpp.o 2>tc.log && echo "chunk042 ok" || { echo FAIL; grep error: tc.log|head; exit 1; }
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>tlk.log && echo "link ok" || { echo LINKFAIL; grep -i undefined tlk.log|head; exit 1; }
echo "=== run PS3_TRACE_T=1 (8s) $(date +%H:%M:%S) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_TRACE_T=1 timeout -k 3 8 ./boot_hle.exe ../EBOOT.ELF > t42.out 2> t42.err
echo "=== [T5x] sequência (até onde func_0025C838 chega?) ==="
grep -aoE "\[T5[0-9]\]" t42.err | uniq -c
echo "=== detalhe (todas as linhas T) ==="
grep -aE "^\[T[0-9]+\]" t42.err | head -20
echo "FIM"
