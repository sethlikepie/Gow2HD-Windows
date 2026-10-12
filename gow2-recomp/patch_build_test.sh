set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work
echo "=== 1. copiar recomp_full -> recomp_mid + patch rlwinm/rlwimi (zero-extend) $(date +%H:%M:%S) ==="
rm -rf recomp_mid && cp -r recomp_full recomp_mid
cd recomp_mid
sed -i 's/(int64_t)(int32_t)ppc_rlwinm(/(uint32_t)ppc_rlwinm(/g; s/(int64_t)(int32_t)ppc_rlwimi(/(uint32_t)ppc_rlwimi(/g' ppu_recomp_*.cpp
echo "patch ok; rlwinm sign-extend restantes=$(grep -rhc '(int64_t)(int32_t)ppc_rlwinm' ppu_recomp_*.cpp 2>/dev/null | awk '{s+=$1}END{print s}')"
echo "=== 2. compilar chunks (-P 4) $(date +%H:%M:%S) ==="
ls ppu_recomp_*.cpp ppu_stubs.cpp 2>/dev/null | xargs -P 4 -I {} sh -c 'g++ -std=c++20 -O0 -c -I . "$1" -o "$1.o" 2>"$1.cclog"' _ {}
echo "chunks .o=$(ls *.cpp.o 2>/dev/null|wc -l) erros=$(cat *.cclog 2>/dev/null|grep -c error:)"
cat *.cclog 2>/dev/null | grep error: | head -3
echo "=== 3. build boot_hle $(date +%H:%M:%S) ==="
cd /c/Users/softlive/Documents/self-projects/gow2_work
bash build_boot_hle.sh > build_boot_patched.log 2>&1
grep -E 'LINK exit|PRODUZIDO|TOTAL compile errors' build_boot_patched.log | tail -3
echo "=== 4. boot test $(date +%H:%M:%S) ==="
cd recomp_mid
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" timeout -k 3 12 ./boot_hle.exe ../EBOOT.ELF > boot_patched.err 2>&1
echo "boot exit=$? | OOB=$(grep -c 'OOB' boot_patched.err 2>/dev/null)"
echo "--- progressão (não-OOB, fim) ---"
grep -vE "OOB access" boot_patched.err 2>/dev/null | tail -12
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
echo "=== FIM $(date +%H:%M:%S) ==="
