set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
gcc -std=gnu11 -O0 -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . ../../ps3recomp/libs/video/cellGcmSys.c -o lib_cellGcmSys.o 2>tg.log && echo "gcm ok" || { echo FAIL; grep -E "error:" tg.log|head; exit 1; }
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>tlk.log && echo "link ok" || { echo LINKFAIL; grep -i undefined tlk.log|head; exit 1; }
echo "=== run (10s) GPU mínimo ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_SYSUTIL_BOOT=1 PS3_SYSUTIL_CB_OPD=0x524288 PS3_TRACE_SC=1 PS3_TRACE_SPURS=all timeout -k 3 10 ./boot_hle.exe ../EBOOT.ELF > gp.out 2> gp.err
echo "OOB: $(grep -ac 'OOB access' gp.err) | HLE: $(grep -ac '^\[TRACE\]' gp.err) | 141: $(grep -aoE 'num=141 #[0-9]+' gp.err|grep -oE '[0-9]+$'|sort -n|tail -1) | 92(sem): $(grep -aoE 'num=92 #[0-9]+' gp.err|grep -oE '[0-9]+$'|sort -n|tail -1)"
echo "=== top syscalls ==="; grep -a "^\[SC\]" gp.err | grep -oE "num=[0-9]+" | sort | uniq -c | sort -rn | head -6
echo "=== últimas 16 HLE (onde está agora) ==="; grep -aE "^\[TRACE\]" gp.err | grep -oaE "(_)?(cell|sce|sys)[A-Za-z_]+" | tail -30 | awk '!s[$0]++' | tail -16
echo "=== crash? ==="; grep -aiE "OOB access|unresolved indirect|FATAL|Segmentation" gp.err | head -4
echo FIM
