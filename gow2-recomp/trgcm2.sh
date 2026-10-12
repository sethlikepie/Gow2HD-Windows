set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompile chunk 048 (14:23:28) ==="
g++ -std=c++20 -O0 -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ppu_recomp_048.cpp -o ppu_recomp_048.cpp.o 2>c48.log && echo "ok" || { echo FAIL; grep -E "error:" c48.log|head; exit 1; }
echo "=== relink (14:23:28) ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>lk.log && echo "link ok (14:23:28)" || { echo LINKFAIL; tail -3 lk.log; exit 1; }
echo "=== run: PS3_INIT_DISPLAY (roda #7 -> GCM init?) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_VM_LOW_MB=384 PS3_VM_STACK_MB=64 PS3_INIT_DISPLAY=1 PS3_TRACE_SPURS=all timeout -k 3 12 ./boot_hle.exe ../EBOOT.ELF > gc.out 2> gc.err
echo "exit=$?"
echo "=== FIX-c (func_0024A7CC rodou)? ==="; grep -aE "FIX-c" gc.err | head -2
echo "=== GCM INICIALIZOU? (_cellGcmInitBody / Init / ioAddr) ==="; grep -aiE "_cellGcmInitBody|GcmSys\] Init\(|ioAddr=0x[1-9A-F]" gc.out gc.err 2>/dev/null | head -4
echo "=== HLE + GCM + último ==="; echo "  HLE=$(grep -ac '^\[TRACE\]' gc.err) gcm=$(grep -ac 'cellGcm' gc.err) último=$(grep -aE '^\[TRACE\]' gc.err | grep -oaE '(_)?(cell|sce|sys)[A-Za-z_]+' | tail -1)"
echo "=== crash? ==="; grep -aiE "OOB access|unresolved|access viol|FATAL" gc.err gc.out 2>/dev/null | head -3
echo FIM
