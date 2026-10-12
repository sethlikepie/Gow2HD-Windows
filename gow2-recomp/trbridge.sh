set +e
PS3=../../ps3recomp
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
INC="-I $PS3/include -I $PS3/runtime/ppu -I $PS3/runtime/syscalls -I $PS3/runtime/spu -I $PS3/runtime/prx -I $PS3/runtime/memory -I $PS3/libs/system -I $PS3/libs/spurs -I $PS3/libs/sync -I $PS3/libs/video -I $PS3/libs/audio -I $PS3/libs/network -I $PS3/libs/codec -I $PS3/gow2_gen -I ."

echo "=== compile cellGcmSys (com bridge) + rsx_commands + backends ==="
ok=1
gcc -std=gnu11 -O0 -c $INC $PS3/libs/video/cellGcmSys.c       -o lib_cellGcmSys.o       2>cg.log     || { echo "FAIL cellGcmSys";   grep -m3 error: cg.log;     ok=0; }
gcc -std=gnu11 -O0 -c $INC $PS3/libs/video/rsx_commands.c     -o lib_rsx_commands.o     2>rc.log     || { echo "FAIL rsx_commands"; grep -m3 error: rc.log;     ok=0; }
gcc -std=gnu11 -O0 -c $INC $PS3/libs/video/rsx_null_backend.c -o lib_rsx_null_backend.o 2>rn.log     || { echo "FAIL rsx_null";     grep -m3 error: rn.log;     ok=0; }
gcc -std=gnu11 -O0 -c $INC $PS3/libs/video/rsx_d3d12_backend.c -o lib_rsx_d3d12_backend.o 2>rd.log   || { echo "FAIL rsx_d3d12";    grep -m3 error: rd.log;     ok=0; }
[ $ok -eq 1 ] && echo "compile OK" || { echo "COMPILE ABORT"; exit 1; }

echo "=== link boot_hle.exe (+user32 gdi32 d3d12 dxgi d3dcompiler) ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va \
  *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o \
  -lm -lbcrypt -lole32 -luser32 -lgdi32 -ld3d12 -ldxgi -ld3dcompiler -ldxguid -luuid \
  -o boot_hle.exe 2>lk.log && echo "LINK OK ($(ls -la boot_hle.exe|awk '{print $5}') bytes)" || { echo "LINKFAIL"; tail -8 lk.log; exit 1; }

echo "=== run: PS3_RSX_BACKEND=trace (o boot recompilado emite clears/draws?) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 \
  PS3_FORCE_GCMINIT=1 PS3_BUILD_SINGLETON=1 PS3_SEM_SPIN_BREAK=4096 \
  PS3_RSX_FIFO=1 PS3_FIX_DISPLAYLIST=1 PS3_RSX_BACKEND=trace \
  timeout -k 3 20 ./boot_hle.exe ../EBOOT.ELF > br.out 2> br.err
echo "exit=$?"
echo "=== bridge: o que o renderer recebeu ==="
echo "  rsx-bridge ativo: $(grep -ac 'rsx-bridge' br.err)"
echo "  CLEAR=$(grep -ac 'rsx-bridge] CLEAR' br.err)  DRAW_ARRAYS=$(grep -ac 'DRAW_ARRAYS' br.err)  DRAW_INDEXED=$(grep -ac 'DRAW_INDEXED' br.err)  SET_SHADER=$(grep -ac 'SET_SHADER' br.err)"
echo "=== amostras (primeiras linhas do bridge) ==="
grep -a 'rsx-bridge' br.err | head -20
echo "=== contexto: flips + getlabel + última HLE ==="
echo "  flips=$(grep -ac 'SetFlipCommand' br.err) GetLabel=$(grep -ac cellGcmGetLabelAddress br.err)"
grep -aE '^\[TRACE\]' br.err | grep -oaE '(_)?(cell|sce|sys)[A-Za-z_]+' | tail -1 | sed 's/^/  ultima HLE: /'
echo FIM
