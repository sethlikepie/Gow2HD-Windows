#!/usr/bin/env bash
# Le a memoria do guest num boot_gow2 vivo (Phase 3 Task 3.1).
#
# Porte do attach_mem.sh (gdb/Windows) para lldb/macOS. Mesmas moradas: a flag
# em que func_0030600C gira e o controlo do allocator, ambas big-endian.
#
# Uso: ./attach_mem_mac.sh [segundos-antes-de-ler]      (default 15)
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

WAIT="${1:-15}"

[ -x ./boot_gow2 ] || { echo "boot_gow2 nao existe -- ./build_macos.sh" >&2; exit 1; }

. "$HERE/env_gow2.sh"
export PS3_NO_RSX=1

./boot_gow2 EBOOT.ELF > /tmp/attach_mem.stdout 2>&1 &
PID=$!
sleep "$WAIT"

if ! kill -0 "$PID" 2>/dev/null; then
    echo "o processo morreu antes da leitura (exit $?) -- ver /tmp/attach_mem.stdout" >&2
    exit 1
fi

# rdbe: le uma palavra big-endian do espaco do guest.
cat > /tmp/attach_mem.lldb <<'EOF'
expr -- (unsigned)__builtin_bswap32(*(unsigned*)((char*)vm_base + 0x541AD0))
expr -- (unsigned)__builtin_bswap32(*(unsigned*)((char*)vm_base + 0x541AD4))
expr -- (unsigned)__builtin_bswap32(*(unsigned*)((char*)vm_base + 0x86E118))
expr -- (unsigned)__builtin_bswap32(*(unsigned*)((char*)vm_base + 0x86E11C))
expr -- (unsigned)__builtin_bswap32(*(unsigned*)((char*)vm_base + 0x86E120))
expr -- (unsigned)__builtin_bswap32(*(unsigned*)((char*)vm_base + 0x881970))
expr -- (unsigned)__builtin_bswap32(*(unsigned*)((char*)vm_base + 0x6FF484))
EOF

LABELS=(
    "[0x541AD0] gpr28 (ponteiro para a struct da flag)"
    "[0x541AD4] gpr27 (base do dispatch vtable)"
    "*[0x86E118 +0] FLAG DE SAIDA (loop sai se != 0)"
    "*[0x86E118 +4]"
    "*[0x86E118 +8]"
    "*[0x881970 +0] alloc-ctrl (0x58585858 = fill nao sobrescrito)"
    "*[0x6FF484]    byte do spin de func_002B3CB0"
)

echo "=== memoria do guest apos ${WAIT}s (pid $PID) ==="
i=0
lldb -b -p "$PID" -s /tmp/attach_mem.lldb 2>/dev/null \
  | grep -oE '\$[0-9]+ = [0-9]+' | awk '{print $3}' \
  | while read -r v; do
        printf "  %-52s = 0x%08X\n" "${LABELS[$i]}" "$v"
        i=$((i + 1))
    done

kill -9 "$PID" 2>/dev/null
