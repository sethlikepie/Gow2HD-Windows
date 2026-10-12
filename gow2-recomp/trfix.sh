set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompila runtime/syscalls/*.c $(date +%H:%M:%S) ==="
CERR=0
for s in ../../ps3recomp/runtime/syscalls/*.c; do b=$(basename "$s" .c); gcc -std=gnu11 -O0 -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I ../../ps3recomp/runtime/syscalls -I ../../ps3recomp/runtime/spu -I ../../ps3recomp/runtime/prx -I ../../ps3recomp/runtime/memory -I ../../ps3recomp/libs/system -I ../../ps3recomp/libs/spurs -I ../../ps3recomp/libs/sync -I ../../ps3recomp/libs/video -I ../../ps3recomp/libs/audio -I ../../ps3recomp/libs/network -I ../../ps3recomp/libs/codec -I ../../ps3recomp/gow2_gen -I . "$s" -o "lib_$b.o" 2>"lib_$b.cclog"; e=$(grep -c 'error:' "lib_$b.cclog"); CERR=$((CERR+e)); [ $e -gt 0 ] && { echo "  ERR $b: "; grep error: "lib_$b.cclog" | head -3; }; done
echo "syscalls recompilados, erros=$CERR"
[ $CERR -gt 0 ] && exit 1
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -o boot_hle.exe 2>tlk.log && echo "link ok" || { echo "LINK FAIL"; grep -i 'undefined\|error' tlk.log | head; exit 1; }
echo "=== run PS3_TRACE_SC=1 PS3_TRACE_T=1 (12s) $(date +%H:%M:%S) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" PS3_TRACE_SC=1 PS3_TRACE_T=1 timeout -k 3 12 ./boot_hle.exe ../EBOOT.ELF > fx.out 2> fx.err
echo "=== [SC] 141 ainda quente? (contagem máxima) ==="
grep -aoE "num=141 #[0-9]+" fx.err | grep -oE "[0-9]+$" | sort -n | tail -1
echo "=== top syscalls ==="
grep -a "^\[SC\]" fx.err | grep -oE "num=[0-9]+" | sort | uniq -c | sort -rn | head -10
echo "=== T-traces (func_0024A7CC roda agora?) ==="
grep -aoE "\[T[0-9]+\]" fx.err | sort | uniq -c
echo "=== boot avançou? (últimas 12 linhas não-SC) ==="
grep -av "^\[SC\]" fx.err | tail -12
echo "FIM $(date +%H:%M:%S)"
