set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
PS3=../../ps3recomp
INC="-I $PS3/include -I $PS3/runtime/ppu -I ."
echo "=== compila pecas de runtime + boot_main ($(date +%H:%M:%S)) ==="
for src in $PS3/runtime/ppu/ppu_loader.cpp $PS3/runtime/ppu/ppu_imports.cpp $PS3/runtime/ppu/ppu_hle.cpp $PS3/runtime/ppu/ppu_sysprx.cpp $PS3/runtime/ppu/ppu_fs.cpp $PS3/runtime/ppu/tests/boot_main.cpp; do
  b=$(basename "$src" .cpp)
  g++ -std=c++20 -O0 -c $INC "$src" -o "$b.o" 2> "$b.cclog"
  echo "$b -> exit=$? erros=$(grep -c 'error:' "$b.cclog")"
done
echo "=== conta objetos da lift ==="
echo "lift .cpp.o = $(ls *.cpp.o 2>/dev/null | wc -l)"
echo "=== LINK boot_gow.exe ($(date +%H:%M:%S)) ==="
g++ -std=c++20 -O0 *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o boot_main.o -lm -o boot_gow.exe 2> link_boot.log
echo "LINK exit=$? ($(date +%H:%M:%S)) undefined=$(grep -c 'undefined reference' link_boot.log 2>/dev/null)"
grep 'undefined reference' link_boot.log 2>/dev/null | head -20
ls -lh boot_gow.exe 2>/dev/null && echo "*** boot_gow.exe PRODUZIDO ***"
