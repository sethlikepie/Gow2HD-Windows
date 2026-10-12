set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== relink (todos .o RA=0-fixed) $(date +%H:%M:%S) ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va \
  *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o \
  -lm -lbcrypt -lole32 -luser32 -lgdi32 -ld3d12 -ldxgi -ld3dcompiler -ldxguid -luuid -lntdll \
  -o boot_hle.exe 2>lk.log && echo "LINK OK" || { echo LINKFAIL; tail -8 lk.log; exit 1; }
echo "=== run watchdog@22s + trace, timeout 40s ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 PS3_CELLSYS_REORDER=1 PS3_TRACE_SPURS=all PS3_WATCHDOG_SEC=22 \
  timeout -k 5 40 ./boot_hle.exe ../EBOOT.ELF > rr.out 2> rr.err
echo "exit=$? (139=crash 124=timeout 0=saiu)"
echo "  total HLE=$(grep -ac '^\[TRACE\]' rr.err) (era 993/994) | CRASH=$(grep -ac '\[CRASH\]' rr.err)"
echo "  AVANCOU? GetCfg=$(grep -ac cellGcmGetConfiguration rr.err) VideoOut=$(grep -ac cellVideoOut rr.err) GetCtrl=$(grep -ac cellGcmGetControlRegister rr.err) SetFlip=$(grep -ac cellGcmSetFlip rr.err) Flip=$(grep -ac cellGcmGetFlipStatus rr.err) GetLabel=$(grep -ac cellGcmGetLabelAddress rr.err)"
echo "=== watchdog: onde estao as threads agora? ==="
nm -n boot_hle.exe 2>/dev/null | awk '$2~/^[tT]$/{print $1,$3}' > fn.nm
for R in $(grep -a 'rip=0x' rr.err | grep -oE 'rip=0x[0-9A-Fa-f]+' | sed 's/rip=//' | sort -u); do
  awk -v t="$R" 'BEGIN{tt=strtonum(t);b="";ba=0}{a=strtonum("0x"$1);if(a<=tt&&a>ba){ba=a;b=$2}}END{ if(b!="") printf "  %s -> %s (+0x%X)\n", t, b, tt-ba; else printf "  %s -> (fora .text)\n", t}' fn.nm
done
echo "=== CRASH? ==="
grep -a '\[CRASH\]' rr.err | head -2
RIP=$(grep -a '\[CRASH\]' rr.err | grep -oE 'rip=0x[0-9A-Fa-f]+' | head -1 | sed 's/rip=//'); if [ -n "$RIP" ]; then awk -v t="$RIP" 'BEGIN{tt=strtonum(t);b="";ba=0}{a=strtonum("0x"$1);if(a<=tt&&a>ba){ba=a;b=$2}}END{printf "  CRASH em: %s (+0x%X)\n",b,tt-ba}' fn.nm; fi
echo "=== HLE distintas novas (apos cellGcmGetConfiguration) ==="
grep -aE '^\[TRACE\]' rr.err | grep -oaE '(_)?(cell|sce|sys)[A-Za-z_]+' | awk '!s[$0]++' | tail -25
echo "=== ultimas 14 HLE ==="
grep -aoE '^\[TRACE\] (_)?(cell|sce|sys)[A-Za-z_]+' rr.err | tail -14 | sed 's/\[TRACE\] //'
echo FIM_RUN
