set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompila chunk 048 $(date +%H:%M:%S) ==="
g++ -std=c++20 -O0 -c -I . ppu_recomp_048.cpp -o ppu_recomp_048.cpp.o 2>tc.log && echo "chunk048 ok" || { echo FAIL; grep -E "error:" tc.log|head; exit 1; }
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>tlk.log && echo "link ok" || { echo LINKFAIL; grep -i undefined tlk.log|head; exit 1; }
echo "=== run PS3_BUILD_SINGLETON=1 PS3_TRACE_SPURS=all (10s) $(date +%H:%M:%S) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_BUILD_SINGLETON=1 PS3_TRACE_SPURS=all timeout -k 3 10 ./boot_hle.exe ../EBOOT.ELF > bs.out 2> bs.err
echo "=== [FIX-b] construiu? ==="
grep -a "FIX-b" bs.err | head
echo "=== spin sumiu? (cellSysutilCheckCallback count) ==="
grep -ac "cellSysutilCheckCallback" bs.err
echo "=== progressão APÓS o spin (novas chamadas HLE / threads) ==="
grep -aE "^\[TRACE\]" bs.err | grep -v cellSysutilCheckCallback | tail -20
echo "=== crash/OOB/erro? ==="
grep -aiE "OOB|unresolved indirect|Segmentation|FATAL|unhandled" bs.err | head -6
echo "=== stdout tail ==="
tail -5 bs.out
echo "FIM $(date +%H:%M:%S)"
