set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work
PS3=../ps3recomp
echo "=== 1. LIFT --max-mid-passes 1 (cabe na RAM) + fixes $(date +%H:%M:%S) ==="
rm -rf recomp_mid
python $PS3/tools/ppu_lifter.py EBOOT.ELF --functions functions.json -o recomp_mid --max-mid-passes 1 -j 4 > lift_capped.log 2>&1
echo "lift exit=$? | $(grep -E 'functions lifted|dangling gotos total|fallback stubs' lift_capped.log | tail -3)"
tail -2 lift_capped.log | grep -iE "memory|traceback" && echo "(LIFT OOM/FALHOU)" && exit 1
echo "=== 2. COMPILA chunks -P4 $(date +%H:%M:%S) ==="
cd recomp_mid
ls ppu_recomp_*.cpp ppu_stubs.cpp 2>/dev/null | xargs -P 4 -I {} sh -c 'g++ -std=c++20 -O0 -c -I . "$1" -o "$1.o" 2>"$1.cclog"' _ {}
echo "chunks .o=$(ls *.cpp.o 2>/dev/null|wc -l) erros_compile=$(cat *.cclog 2>/dev/null|grep -c error:)"
cat *.cclog 2>/dev/null | grep "error:" | head -3
echo "=== 3. BUILD boot_hle $(date +%H:%M:%S) ==="
cd /c/Users/softlive/Documents/self-projects/gow2_work
bash build_boot_hle.sh > build_boot_capped.log 2>&1
grep -E 'LINK exit|PRODUZIDO|TOTAL compile errors' build_boot_capped.log | tail -3
echo "=== 4. BOOT TEST $(date +%H:%M:%S) ==="
cd recomp_mid
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" timeout -k 3 12 ./boot_hle.exe ../EBOOT.ELF > boot_capped.err 2>&1
echo "boot exit=$? | OOB=$(grep -c 'OOB' boot_capped.err 2>/dev/null) | func_0030600C reached=$(grep -c 'OOB access 0xFD86' boot_capped.err 2>/dev/null)"
echo "--- progressão (não-OOB, fim) ---"
grep -vE "OOB access" boot_capped.err 2>/dev/null | tail -14
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
echo "=== FIM $(date +%H:%M:%S) ==="
