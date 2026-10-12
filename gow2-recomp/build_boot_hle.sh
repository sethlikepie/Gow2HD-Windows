set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
PS3=../../ps3recomp
INC="-I $PS3/include -I $PS3/runtime/ppu -I $PS3/runtime/syscalls -I $PS3/runtime/spu -I $PS3/runtime/prx -I $PS3/runtime/memory -I $PS3/libs/system -I $PS3/libs/spurs -I $PS3/libs/sync -I $PS3/libs/video -I $PS3/libs/audio -I $PS3/libs/network -I $PS3/libs/codec -I $PS3/gow2_gen -I ."

CPPSRC="$PS3/runtime/ppu/ppu_loader.cpp $PS3/runtime/ppu/ppu_imports.cpp $PS3/runtime/ppu/ppu_hle.cpp $PS3/runtime/ppu/ppu_sysprx.cpp $PS3/runtime/ppu/ppu_fs.cpp $PS3/gow2_gen/ppu_hle_nids.cpp $PS3/runtime/ppu/tests/boot_main.cpp"
CSRC="$PS3/libs/system/sysPrxForUser.c $PS3/libs/system/cellSysmodule.c $PS3/libs/system/cellSysutil.c $PS3/libs/system/cellGame.c $PS3/libs/spurs/cellSpurs.c $PS3/libs/sync/cellSync.c $PS3/libs/video/cellGcmSys.c $PS3/libs/audio/cellAudio.c $PS3/libs/network/sceNp.c $PS3/libs/network/sceNpTrophy.c $PS3/libs/codec/cellVdec.c $PS3/runtime/syscalls/*.c $PS3/runtime/spu/*.c $PS3/runtime/prx/*.c"

CERR=0
echo "=== compile C++ pieces ($(date +%H:%M:%S)) ==="
for s in $CPPSRC; do b=$(basename "$s" .cpp); rm -f "$b.o"; g++ -std=c++20 -O0 -g -c $INC "$s" -o "$b.o" 2>"$b.cclog"; e=$(grep -c 'error:' "$b.cclog"); CERR=$((CERR+e)); echo "  $b.cpp exit=$? err=$e"; done
echo "=== compile C libs ==="
for s in $CSRC; do b=$(basename "$s" .c); rm -f "lib_$b.o"; gcc -std=gnu11 -O0 -c $INC "$s" -o "lib_$b.o" 2>"lib_$b.cclog"; e=$(grep -c 'error:' "lib_$b.cclog"); CERR=$((CERR+e)); echo "  $b.c exit=$? err=$e"; done
echo ">>> TOTAL compile errors = $CERR"
if [ "$CERR" -ne 0 ]; then echo "ABORTANDO link (ha erros de compile)"; grep -h 'error:' *.cclog | head -20; exit 9; fi

echo "=== LINK boot_hle.exe ($(date +%H:%M:%S)) ==="
# A prior run that segfaulted can leave a zombie holding the exe -> link fails
# with "Permission denied" and the STALE binary runs. Kill + remove first.
taskkill //F //IM boot_hle.exe >/dev/null 2>&1 || true
rm -f boot_hle.exe 2>/dev/null || true
g++ -std=c++20 -O0 *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o -lm -lbcrypt -lole32 -lgdi32 -ld3d12 -ldxgi -ld3dcompiler -luuid -o boot_hle.exe 2> link_hle.log
echo "LINK exit=$?"
echo "--- multiple definition (distintos) ---"
grep -oE "multiple definition of [\`'][A-Za-z0-9_]+" link_hle.log | sed -E "s/.*[\`']//" | sort -u | head -40
echo "--- undefined reference (distintos) ---"
grep -oE "undefined reference to [\`'][A-Za-z0-9_]+" link_hle.log | sed -E "s/.*[\`']//" | sort -u | head -80
echo "--- counts: mult=$(grep -c 'multiple definition' link_hle.log) undef=$(grep -c 'undefined reference' link_hle.log) ---"
ls -lh boot_hle.exe 2>/dev/null && echo "*** boot_hle.exe PRODUZIDO ***"
