#!/usr/bin/env bash
# smoke_intro_vdec_wad.sh — aceite Mac intro: Open/StartSeq/FORCE (e WAD se vier)
#
# Gates do plano 2026-07-21-intro-vdec-open-force-wad.md:
#   M0       PS3_MOVIE_EOS=0          — st620 max >= 3 (baseline honesto)
#   M_open   EOS+DONE auto, sem FORCE — open>=1 start>=1 (A3b + arm st>=5)
#   M_force  EOS+DONE auto+FORCE      — open>=1 start>=1 force>=1 (Task 4)
#   M_wad    mesmo M_force            — target wad>=1 (Task 4b se force sem wad)
#   M3       EOS=1, SEM done producer — arm==0 (forge-trap)
#
# Kill SEMPRE por PID (TERM → -9). Nunca pkill -f boot_gow2.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
EXE=./boot_gow2
LOGDIR="${TMPDIR:-/tmp}/gow2_vdec_wad_$$"
mkdir -p "$LOGDIR"
FAILED=0

fail() { echo "FAIL: $1"; FAILED=1; }
pass() { echo "  PASS: $1"; }

if [ ! -x "$EXE" ] || [ ! -f EBOOT.ELF ]; then
    echo "SKIP: falta $EXE ou EBOOT.ELF"
    exit 1
fi

# unit offline (policy Task 2/4)
echo "== unit: test_movie_eos_policy =="
if clang -std=c11 -O0 -Wall -I "$HERE" \
        tests/test_movie_eos_policy.c movie_eos_arm.c \
        -o "$LOGDIR/test_policy" 2>"$LOGDIR/cc.log"; then
    if "$LOGDIR/test_policy" >"$LOGDIR/unit.log" 2>&1; then
        pass "unit ($(grep -c '  ok:' "$LOGDIR/unit.log") checks)"
    else
        fail "unit"; grep FAIL "$LOGDIR/unit.log" | head -10
    fi
else
    fail "unit compile"; head -10 "$LOGDIR/cc.log"
fi

run_boot() {
    # $1=name $2=secs; remaining: env KEY=VAL
    local name=$1 secs=$2; shift 2
    (
        set -a; . "$HERE/env_gow2.sh"; set +a
        unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
        export PS3_NO_RSX=1 PS3_PERF_FSM=1
        # baseline clean for movie gates; callers re-export what they need
        unset PS3_MOVIE_DONE_MS PS3_VDEC_FORCE_SEQDONE_MS
        export PS3_MOVIE_EOS=0
        for kv in "$@"; do export "$kv"; done
        exec "$EXE" EBOOT.ELF
    ) >"$LOGDIR/$name.log" 2>&1 &
    local pid=$!
    sleep "$secs"
    kill -TERM "$pid" 2>/dev/null
    sleep 1
    kill -9 "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    echo "$LOGDIR/$name.log"
}

metrics() {
    local log=$1
    echo "  open=$(grep -c '\[cellVdec\] Open' "$log" 2>/dev/null || echo 0)"
    echo "  start=$(grep -c 'StartSeq' "$log" 2>/dev/null || echo 0)"
    echo "  force=$(grep -c 'FORCE SEQDONE' "$log" 2>/dev/null || echo 0)"
    echo "  arm=$(grep -c 'arming EOS' "$log" 2>/dev/null || echo 0)"
    echo "  wad=$(grep -ciE 'R_LglScA|R_PermA' "$log" 2>/dev/null || echo 0)"
    local stmax
    stmax=$(grep -oE 'st620 [0-9]+ -> [0-9]+' "$log" 2>/dev/null \
            | awk '{print $4}' | sort -n | tail -1)
    echo "  st620_max=${stmax:-0}"
}

count() { grep -c "$2" "$1" 2>/dev/null || echo 0; }

echo "== M0: baseline EOS=0 (25s) =="
L=$(run_boot m0 25 PS3_MOVIE_EOS=0)
metrics "$L"
stmax=$(grep -oE 'st620 [0-9]+ -> [0-9]+' "$L" | awk '{print $4}' | sort -n | tail -1)
if [ "${stmax:-0}" -ge 3 ]; then pass "M0 st620_max=$stmax >=3"; else fail "M0 st620_max=$stmax <3"; fi

echo "== M_force: EOS+auto+FORCE=4000 (70s) — Task 4 primary =="
L=$(run_boot m_force 70 \
    PS3_MOVIE_EOS=1 PS3_MOVIE_DONE_MS=auto \
    PS3_VDEC_ASYNC=1 PS3_VDEC_FORCE_SEQDONE_MS=4000)
metrics "$L"
o=$(grep -c '\[cellVdec\] Open' "$L" || true)
s=$(grep -c 'StartSeq' "$L" || true)
f=$(grep -c 'FORCE SEQDONE' "$L" || true)
w=$(grep -ciE 'R_LglScA|R_PermA' "$L" || true)
[ "${o:-0}" -ge 1 ] && pass "M_force open=$o" || fail "M_force open=$o"
[ "${s:-0}" -ge 1 ] && pass "M_force start=$s" || fail "M_force start=$s"
[ "${f:-0}" -ge 1 ] && pass "M_force force=$f" || fail "M_force force=$f"
if [ "${w:-0}" -ge 1 ]; then
    pass "M_wad wad=$w"
else
    echo "  NOTE: force GREEN but wad=$w — Task 4b (post-SEQDONE WAD path)"
fi

echo "== M3 forge-trap: EOS=1 sem DONE_MS (25s) =="
L=$(run_boot m3 25 PS3_MOVIE_EOS=1)
# ensure DONE unset (run_boot already unsets then exports only args)
a=$(grep -c 'arming EOS' "$L" || true)
[ "${a:-0}" -eq 0 ] && pass "M3 arm=$a (forge-trap)" || fail "M3 arm=$a expected 0"

echo ""
echo "logs: $LOGDIR"
if [ "$FAILED" -eq 0 ]; then
    echo "ALL GREEN (WAD may be NOTE-only)"
    exit 0
fi
echo "FAILED=$FAILED"
exit 1
