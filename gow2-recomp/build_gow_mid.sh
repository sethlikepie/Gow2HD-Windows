set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work
echo "=== 1. regen mid-passes lift (lifter atualizado p/ Codex; emite ppu_stubs) $(date +%H:%M:%S) ==="
rm -rf recomp_mid
python ../ps3recomp/tools/ppu_lifter.py EBOOT.ELF --functions functions.json -o recomp_mid -j 4 > lift_mid_final.log 2>&1
echo "lift exit=$?"
grep -E "functions lifted|fallback stubs|mid-function tail-entry wrappers total|Wrote [0-9]" lift_mid_final.log | tail -6
echo "chunks: $(ls recomp_mid/ppu_recomp_*.cpp 2>/dev/null | wc -l); stubs file: $(ls -lh recomp_mid/ppu_stubs.cpp 2>/dev/null | awk '{print $5}')"
echo "=== 2. compila chunks + stubs -> .o (paralelo -P2) $(date +%H:%M:%S) ==="
cd recomp_mid
t0=$(date +%s)
ls ppu_recomp_*.cpp ppu_stubs.cpp 2>/dev/null | xargs -P 2 -I {} sh -c 'g++ -std=c++20 -O0 -c -I . "$1" -o "$1.o" 2> "$1.cclog"' _ {}
echo "compile dur=$(( $(date +%s)-t0 ))s; .o gerados=$(ls *.cpp.o 2>/dev/null | wc -l); erros de compile=$(cat *.cclog 2>/dev/null | grep -c 'error:')"
echo "=== 3. ppu_loader.o + LINK $(date +%H:%M:%S) ==="
g++ -std=c++20 -O0 -c -I ../../ps3recomp/include -I ../../ps3recomp/runtime/ppu -I . ../../ps3recomp/runtime/ppu/ppu_loader.cpp -o ppu_loader.o 2> ldr.log
echo "ppu_loader exit=$? erros=$(grep -c error: ldr.log)"
g++ -std=c++20 -O0 *.cpp.o ppu_loader.o -x c ../recomp_compile/gow_main.c -x none -lm -o gow_mid.exe 2> link_mid.log
echo "LINK exit=$? $(date +%H:%M:%S)"
echo "undefined refs restantes: $(grep -c 'undefined reference' link_mid.log 2>/dev/null)"
ls -lh gow_mid.exe 2>/dev/null && echo "*** gow_mid.exe PRODUZIDO ***"
