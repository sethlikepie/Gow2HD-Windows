set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work
PS3=../ps3recomp
echo "=== 1. LIFT --max-mid-passes 3 --chunk-lines 13000 (cabe RAM + chunks pequenos) $(date +%H:%M:%S) ==="
rm -rf recomp_mid
python $PS3/tools/ppu_lifter.py EBOOT.ELF --functions functions.json -o recomp_mid --max-mid-passes 3 --chunk-lines 13000 -j 4 > lift3.log 2>&1
echo "lift exit=$? | $(grep -E 'functions lifted|dangling gotos total|fallback stubs|source chunks' lift3.log | tail -4)"
tail -2 lift3.log | grep -iE "memory|traceback" && echo "(LIFT OOM)" && exit 1
echo "=== 2. COMPILA chunks -P4 $(date +%H:%M:%S) ==="
cd recomp_mid
NCHUNK=$(ls ppu_recomp_*.cpp 2>/dev/null | wc -l)
ls ppu_recomp_*.cpp ppu_stubs.cpp 2>/dev/null | xargs -P 4 -I {} sh -c 'g++ -std=c++20 -O0 -c -I . "$1" -o "$1.o" 2>"$1.cclog"' _ {}
echo "chunks=$NCHUNK .o=$(ls *.cpp.o 2>/dev/null|wc -l) erros=$(cat *.cclog 2>/dev/null|grep -c error:)"
cat *.cclog 2>/dev/null | grep "error:" | head -3
echo "=== 3. BUILD boot_hle $(date +%H:%M:%S) ==="
cd /c/Users/softlive/Documents/self-projects/gow2_work
bash build_boot_hle.sh > build_boot3.log 2>&1
grep -E 'LINK exit|PRODUZIDO|undefined reference' build_boot3.log | head -4
echo "=== 4. BOOT TEST $(date +%H:%M:%S) ==="
cd recomp_mid
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" timeout -k 3 12 ./boot_hle.exe ../EBOOT.ELF > boot3.err 2>&1
echo "boot exit=$? | OOB(wall)=$(grep -c 'OOB access 0xFD86' boot3.err 2>/dev/null) | unlifted=$(grep -c 'unlifted function' boot3.err 2>/dev/null)"
echo "--- progressão (não-OOB) ---"
grep -vE "OOB access" boot3.err 2>/dev/null | tail -16
taskkill //F //IM boot_hle.exe >/dev/null 2>&1
echo "=== FIM $(date +%H:%M:%S) ==="
