#!/usr/bin/env bash
# Build + run do unit test de ps3_mask_trace (Fase 1, Task 1).
#
# Standalone: compila runtime/trace/ps3_trace.c (Fase 0) + ps3_mask_trace.c +
# o teste. NAO liga ppu_loader.cpp (inclui o ppu_recomp.h gerado por jogo).
#
# Cada cenario e' um processo separado: os gates cacheiam o getenv.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
CC="${CC:-clang}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
EXE="$WORK/test_ps3_mask_trace"

"$CC" -std=c11 -O1 -Wall -Wextra -Wno-unused-parameter \
    -I "$ROOT/include" -I "$ROOT/runtime/memory" \
    "$ROOT/runtime/trace/tests/test_ps3_mask_trace.c" \
    "$ROOT/runtime/trace/ps3_trace.c" \
    "$ROOT/runtime/trace/ps3_mask_trace.c" \
    -o "$EXE"

pass=0; fail=0
run() {  # run <nome> <modo> [VAR=val ...]
    local name="$1" mode="$2"; shift 2
    local out
    if out=$(env -u PS3_TRACE_MEMORY -u PS3_MASK_OFF_UNCOMMITTED \
                 -u PS3_MASK_OFF_TBLSIZE "$@" "$EXE" "$mode" 2>&1); then
        echo "  [PASS] $name"; pass=$((pass+1))
    else
        echo "  [FAIL] $name"; echo "$out" | sed 's/^/      /'; fail=$((fail+1))
    fi
}

run "off"      off                                        # sem PS3_TRACE_MEMORY
run "format"   format   PS3_TRACE_MEMORY=1
run "names"    names    PS3_TRACE_MEMORY=1
run "flood"    flood    PS3_TRACE_MEMORY=1
run "disabled" disabled PS3_MASK_OFF_UNCOMMITTED=1

echo "Results: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
