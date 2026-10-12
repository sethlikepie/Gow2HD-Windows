set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompila aggregator chunk (ppu_recomp_042 com reorder) ==="
g++ -std=c++20 -O0 -c -I . ppu_recomp_042.cpp -o ppu_recomp_042.cpp.o 2>c42.log && echo "OK" || { echo FAIL; grep -m3 error: c42.log; exit 1; }
echo "=== relink ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va \
  *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o \
  -lm -lbcrypt -lole32 -luser32 -lgdi32 -ld3d12 -ldxgi -ld3dcompiler -ldxguid -luuid -lntdll \
  -o boot_hle.exe 2>lk.log && echo "LINK OK" || { echo LINKFAIL; tail -6 lk.log; exit 1; }
echo "=== run: PS3_CELLSYS_REORDER=1 (builders #54-#57 antes da espera #53) ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 \
  PS3_CELLSYS_REORDER=1 PS3_TRACE_SPURS=all \
  timeout -k 3 25 ./boot_hle.exe ../EBOOT.ELF > ro.out 2> ro.err
echo "exit=$?"
echo "=== PASSOU do cellSysutil wall? ==="
echo "  CheckCallback=$(grep -ac cellSysutilCheckCallback ro.err) (era ~140k=preso) | OOB=$(grep -ac OOB ro.err) | crash=$(grep -aic 'segmentation\|abort\|terminate' ro.err)"
echo "  GcmGetControlRegister=$(grep -ac cellGcmGetControlRegister ro.err) | GetLabel=$(grep -ac cellGcmGetLabelAddress ro.err) | SetFlip=$(grep -ac SetFlipCommand ro.err) | VideoOut=$(grep -ac cellVideoOut ro.err)"
echo "=== sub-inits #54-#57 reordenadas rodaram? (ppu_tr 540/550/560/570) ==="
grep -aE 'ppu_tr.*5[4-7]0|\[tr\].*5[4-7]0|TR.*5[4-7]0' ro.err | head -5
grep -a '570' ro.err | head -2
echo "=== últimas HLE distintas (até onde chegou) ==="
grep -aE '^\[TRACE\]' ro.err | grep -oaE '(_)?(cell|sce|sys)[A-Za-z_]+' | awk '!s[$0]++' | tail -20
echo "=== últimas 5 linhas não-OOB (onde parou/crashou) ==="
grep -avE "OOB access" ro.err | tail -5
echo FIM
