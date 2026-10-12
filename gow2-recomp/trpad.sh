set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompila cellSysutil + ppu_hle_nids $(date +%H:%M:%S) ==="
gcc -std=gnu11 -O0 -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/libs/system/cellSysutil.c -o lib_cellSysutil.o 2>tc.log && echo "cellSysutil ok" || { echo FAIL1; grep -E "error:" tc.log|head; exit 1; }
g++ -std=c++20 -O0 -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/gow2_gen/ppu_hle_nids.cpp -o ppu_hle_nids.o 2>tn.log && echo "nids ok" || { echo FAIL2; grep -E "error:" tn.log|head; exit 1; }
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>tlk.log && echo "link ok" || { echo LINKFAIL; grep -i undefined tlk.log|head; exit 1; }
echo "=== run PS3_SYSUTIL_BOOT=1 (12s) $(date +%H:%M:%S) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_SYSUTIL_BOOT=1 PS3_TRACE_SPURS=all timeout -k 3 12 ./boot_hle.exe ../EBOOT.ELF > pad.out 2> pad.err
echo "=== HLE total (era ~69k; CheckCallback dominava) ==="
echo "HLE: $(grep -ac '^\[TRACE\]' pad.err) | CheckCallback: $(grep -ac cellSysutilCheckCallback pad.err) | cellPad: $(grep -aoEc 'cellPad[A-Za-z]+' pad.err)"
echo "=== loop do cellPad quebrou? avançou? (top HLE calls) ==="
grep -aoE "^\[TRACE\] [A-Za-z_]+" pad.err | sed 's/\[TRACE\] //' | sort | uniq -c | sort -rn | head -12
echo "=== novas chamadas (não vistas antes) — últimas distintas ==="
grep -aoE "^\[TRACE\] [A-Za-z_]+" pad.err | sed 's/\[TRACE\] //' | tail -40 | awk '!s[$0]++' | tail -14
echo "=== crash? ==="
grep -aiE "unresolved indirect|OOB access|FATAL|Segmentation" pad.err | head -4
echo FIM
