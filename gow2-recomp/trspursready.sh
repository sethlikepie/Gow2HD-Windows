set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== recompila chunk com o shim (ppu_recomp_054) ==="
g++ -std=c++20 -O0 -c -I . ppu_recomp_054.cpp -o ppu_recomp_054.cpp.o 2>c54.log && echo "chunk OK" || { echo FAIL; grep -m3 error: c54.log; exit 1; }
echo "=== relink boot_hle.exe ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va \
  *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o \
  -lm -lbcrypt -lole32 -luser32 -lgdi32 -ld3d12 -ldxgi -ld3dcompiler -ldxguid -luuid \
  -o boot_hle.exe 2>lk.log && echo "LINK OK" || { echo LINKFAIL; tail -6 lk.log; exit 1; }
echo "=== run: boot REAL (sem force-hacks) + PS3_SPURS_READY=1 ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 \
  PS3_SPURS_READY=1 PS3_TRACE_SPURS=all \
  timeout -k 3 25 ./boot_hle.exe ../EBOOT.ELF > sr.out 2> sr.err
echo "exit=$?"
echo "=== shim disparou? (deixou func_0030600C?) ==="
echo "  SPURS_READY writes=$(grep -ac 'PS3_SPURS_READY] wrote' sr.err)"
echo "=== progressão HLE: total + últimas distintas ==="
echo "  HLE calls=$(grep -ac '^\[TRACE\]' sr.err)"
echo "  últimas HLE distintas (avanço além de func_0030600C?):"
grep -aE '^\[TRACE\]' sr.err | grep -oaE '(_)?(cell|sce|sys)[A-Za-z_]+' | awk '!s[$0]++' | tail -25
echo "=== últimas linhas não-OOB (onde parou) ==="
grep -avE "OOB access" sr.err | tail -8
echo FIM
