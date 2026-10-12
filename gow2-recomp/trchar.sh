set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
PS3=../../ps3recomp
INC="-I $PS3/include -I $PS3/runtime/ppu -I $PS3/runtime/syscalls -I $PS3/runtime/spu -I $PS3/runtime/prx -I $PS3/runtime/memory -I $PS3/libs/system -I $PS3/libs/spurs -I $PS3/libs/sync -I $PS3/libs/video -I $PS3/libs/audio -I $PS3/libs/network -I $PS3/libs/codec -I $PS3/gow2_gen -I ."
echo "=== recompila lib_cellVideoOut (fix raw-ptr) ==="
gcc -std=gnu11 -O0 -c $INC $PS3/libs/video/cellVideoOut.c -o lib_cellVideoOut.o 2>vo.log && echo OK || { echo FAIL; grep -m3 error: vo.log; exit 1; }
echo "=== relink ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va \
  *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o \
  -lm -lbcrypt -lole32 -luser32 -lgdi32 -ld3d12 -ldxgi -ld3dcompiler -ldxguid -luuid -lntdll \
  -o boot_hle.exe 2>lk.log && echo "LINK OK" || { echo LINKFAIL; tail -6 lk.log; exit 1; }
echo "=== run LONGO (50s) pra caracterizar o loop pós-config ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 PS3_CELLSYS_REORDER=1 PS3_TRACE_SPURS=all \
  timeout -k 5 50 ./boot_hle.exe ../EBOOT.ELF > ch.out 2> ch.err
echo "exit=$? (139=crash, 124=timeout, 1=vm-commit, 0=saiu)"
echo "  total HLE=$(grep -ac '^\[TRACE\]' ch.err) | CRASH=$(grep -ac '\[CRASH\]' ch.err) | lwmutex=$(grep -ac sys_lwmutex ch.err)"
echo "  AVANÇOU? GetConfig=$(grep -ac cellGcmGetConfiguration ch.err) VideoOut=$(grep -ac cellVideoOut ch.err) GetCtrl=$(grep -ac cellGcmGetControlRegister ch.err) SetFlip=$(grep -ac SetFlipCommand ch.err) GetLabel=$(grep -ac cellGcmGetLabelAddress ch.err)"
grep -a '\[CRASH\]' ch.err
RIP=$(grep -a '\[CRASH\]' ch.err | grep -oE 'rip=0x[0-9A-Fa-f]+' | head -1 | sed 's/rip=//'); if [ -n "$RIP" ]; then nm -n boot_hle.exe 2>/dev/null | awk '$2~/^[tT]$/{print $1,$3}' > fn.nm; awk -v t="$RIP" 'BEGIN{tt=strtonum(t);b="";ba=0}{a=strtonum("0x"$1);if(a<=tt&&a>ba){ba=a;b=$2}}END{printf "  CRASH em: %s (+0x%X)\n",b,tt-ba}' fn.nm; fi
echo "=== TODAS as HLE distintas (ordem de 1a aparição) ==="
grep -aE '^\[TRACE\]' ch.err | grep -oaE '(_)?(cell|sce|sys)[A-Za-z_]+' | awk '!s[$0]++' | tail -22
echo "=== últimas 8 HLE (o que faz no fim) ==="
grep -aoE '^\[TRACE\] (_)?(cell|sce|sys)[A-Za-z_]+' ch.err | tail -8 | sed 's/\[TRACE\] //'
echo FIM
