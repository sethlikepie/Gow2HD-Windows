set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== compile boot_main.o ==="
g++ -std=c++20 -O0 -g -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/runtime/ppu/tests/boot_main.cpp -o boot_main.o 2>bm.log && echo "ok" || { echo FAIL; grep -E "error:" bm.log|head; exit 1; }
echo "=== relink (13:58:51) ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>lk.log && echo "link ok (13:58:51)" || { echo LINKFAIL; tail -3 lk.log; exit 1; }
echo "=== run: pegada reduzida 384/64 MB, PS3_TRACE_T, SEM bypass ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_VM_LOW_MB=384 PS3_VM_STACK_MB=64 PS3_TRACE_T=1 PS3_TRACE_SPURS=all timeout -k 3 10 ./boot_hle.exe ../EBOOT.ELF > lv2.out 2> lv2.err
echo "exit=$?"
echo "startup: $(grep -aiE 'vm commit|vm reserve|VFS root' lv2.out | head -1)"
echo "=== probes agregador T51..T59 (até onde foi) ==="; grep -aoE "\[T(5[0-9])\]" lv2.err | sort | uniq -c
echo "=== últimos [T*] ==="; grep -aoE "\[T[0-9]+\] 0x[0-9A-Fa-f]+" lv2.err | tail -4
echo "=== cellSysutil HLE calls ==="; grep -aoE "(_)?cellSysutil[A-Za-z]+" lv2.err | sort | uniq -c | head
echo FIM
