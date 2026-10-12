set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
echo "=== 1. backup + patch RA=0 nos atomics do chunk 021 (so linhas com reserve_addr) ==="
cp -f ppu_recomp_021.cpp ppu_recomp_021.cpp.bak0
# RA=0 (literal 0) em lwarx/stwcx: 'ea = gpr[0] + gpr[N]' deve virar 'ea = gpr[N]'.
# Restrito a linhas com reserve_addr -> nao toca aritmetica.
sed -i '/reserve_addr/ s/ea = ctx->gpr\[0\] + ctx->gpr\[/ea = ctx->gpr[/g' ppu_recomp_021.cpp
echo "  linhas atomics RA=0 corrigidas: $(diff ppu_recomp_021.cpp.bak0 ppu_recomp_021.cpp | grep -c '^>')"
echo "  func_0014852C agora (loc_00148610):"
grep -n -A1 "loc_00148610:" ppu_recomp_021.cpp | head -3
echo "=== 2. recompila chunk 021 ==="
g++ -std=c++20 -O0 -c -I . ppu_recomp_021.cpp -o ppu_recomp_021.cpp.o 2>c21.log && echo "OK" || { echo FAIL; grep -m3 error: c21.log; exit 1; }
echo "=== 3. relink ==="
taskkill //F //IM boot_hle.exe >/dev/null 2>&1; sleep 1; rm -f boot_hle.exe
g++ -std=c++20 -O0 -Wl,--disable-dynamicbase,--disable-high-entropy-va \
  *.cpp.o ppu_loader.o ppu_imports.o ppu_hle.o ppu_sysprx.o ppu_fs.o ppu_hle_nids.o boot_main.o lib_*.o \
  -lm -lbcrypt -lole32 -luser32 -lgdi32 -ld3d12 -ldxgi -ld3dcompiler -ldxguid -luuid -lntdll \
  -o boot_hle.exe 2>lk.log && echo "LINK OK" || { echo LINKFAIL; tail -6 lk.log; exit 1; }
echo "=== 4. run watchdog@12s + trace, timeout 28s ==="
PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted" \
  PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 PS3_CELLSYS_REORDER=1 PS3_TRACE_SPURS=all PS3_WATCHDOG_SEC=12 \
  timeout -k 5 28 ./boot_hle.exe ../EBOOT.ELF > ra.out 2> ra.err
echo "exit=$? (139=crash 124=timeout 0=saiu)"
echo "  total HLE=$(grep -ac '^\[TRACE\]' ra.err) (era 993) | CRASH=$(grep -ac '\[CRASH\]' ra.err)"
echo "=== 5. watchdog: ainda em func_0014852C? ==="
nm -n boot_hle.exe 2>/dev/null | awk '$2~/^[tT]$/{print $1,$3}' > fn.nm
for R in $(grep -a 'rip=0x' ra.err | grep -oE 'rip=0x[0-9A-Fa-f]+' | sed 's/rip=//' | sort -u); do
  awk -v t="$R" 'BEGIN{tt=strtonum(t);b="";ba=0}{a=strtonum("0x"$1);if(a<=tt&&a>ba){ba=a;b=$2}}END{ if(b!="") printf "  %s -> %s (+0x%X)\n", t, b, tt-ba; else printf "  %s -> (fora .text)\n", t}' fn.nm
done
echo "=== 6. CRASH? ==="
grep -a '\[CRASH\]' ra.err | head -2
RIP=$(grep -a '\[CRASH\]' ra.err | grep -oE 'rip=0x[0-9A-Fa-f]+' | head -1 | sed 's/rip=//'); if [ -n "$RIP" ]; then awk -v t="$RIP" 'BEGIN{tt=strtonum(t);b="";ba=0}{a=strtonum("0x"$1);if(a<=tt&&a>ba){ba=a;b=$2}}END{printf "  CRASH em: %s (+0x%X)\n",b,tt-ba}' fn.nm; fi
echo "=== 7. ultimas 12 HLE (estado novo?) ==="
grep -aoE '^\[TRACE\] (_)?(cell|sce|sys)[A-Za-z_]+' ra.err | tail -12 | sed 's/\[TRACE\] //'
echo FIM
