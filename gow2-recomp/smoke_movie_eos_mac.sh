#!/usr/bin/env bash
# smoke_movie_eos_mac.sh -- aceite in-boot da Task 3 (canal NATURAL de EOS) do
# plano ../ps3recomp/docs/superpowers/plans/2026-07-20-macos-movie-eos-fsm.md.
#
# O boot e' NAO-DETERMINISTICO (st620 chega a 3 numa fraccao variavel dos runs,
# sem mudar codigo). Uma corrida NAO e' prova: mede-se N-of-M, M>=8, e compara-se
# a TAXA de cada metrica COM o arm vs SEM o arm, no MESMO binario.
#
# Dois bracos, intercalados por iteracao (para nao enviesar por drift da maquina):
#   NO-ARM (M3): PS3_MOVIE_EOS=1, SEM produtor (PS3_MOVIE_DONE_MS ausente).
#                Nada pode armar; TEM de armar 0/M. Se armar, e' forja.
#   WITH-ARM (M2): PS3_MOVIE_EOS=1 + PS3_MOVIE_DONE_MS=auto (duracao REAL do .wav).
#                O produtor time-based dispara ~duracao do filme depois do
#                playback comecar, arma o read-hook, e mede-se o efeito na FSM.
#
# Metricas por run: reached1 (st620>=1), reached3 (st620==3), past3 (st620>=5),
# reset30 (transicao 3->0), armed, done, hit, WAD (R_LglScA/R_PermA).
#
# BOOT: binario a exercitar (default ./boot_gow2). O pkill usa o basename, por
# isso um binario com nome unico (ex. ./boot_eosarm) fica isolado de outra sessao
# a correr ./boot_gow2 em paralelo.
#
# Uso: ./smoke_movie_eos_mac.sh [M] [secs]     (default M=8 secs=48)
#      BOOT=./boot_eosarm ./smoke_movie_eos_mac.sh 8 48
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

M="${1:-8}"
SECS="${2:-48}"
BOOT="${BOOT:-./boot_gow2}"
PAT="$(basename "$BOOT")"
TMP=$(mktemp -d /tmp/gow2_eos.XXXXXX)
echo "BOOT=$BOOT (pkill pat='$PAT')  M=$M secs=$SECS logs=$TMP"

if [ ! -x "$BOOT" ] || [ ! -f EBOOT.ELF ]; then
    echo "SKIP: falta $BOOT ou EBOOT.ELF"; exit 2
fi

run_one() {
    local log=$1; shift
    ( set -a; . "$HERE/env_gow2.sh"; set +a
      export PS3_NO_RSX=1 PS3_VDEC_ASYNC=1
      unset PS3_TRACE_MOVIEOBJ    # sem spam [MOVIEOBJ]; [MOVIEFSM] sai na mesma sob PS3_MOVIE_EOS
      for kv in "$@"; do export "$kv"; done
      exec "$BOOT" EBOOT.ELF ) > "$log" 2>&1 &
    sleep "$SECS"
    pkill -f "$PAT" 2>/dev/null
    sleep 1
    pkill -9 -f "$PAT" 2>/dev/null
    sleep 1
}

maxst() { grep -oE 'st620 [0-9]+ -> [0-9]+' "$1" | awk '{print $4}' | awk '$1<1000{print}' | sort -n | tail -1; }
cnt()   { grep -c "$2" "$1" 2>/dev/null || true; }
has()   { grep -qE "$2" "$1" 2>/dev/null && echo 1 || echo 0; }

# acumuladores: [0]=noarm [1]=arm
r1=(0 0); r3=(0 0); p3=(0 0); rs=(0 0); ar=(0 0); dn=(0 0); ht=(0 0); wl=(0 0); wp=(0 0)

g_ms=0
score() {  # $1=idx(0/1) $2=log ; roda no shell-pai (nao em $()) para actualizar arrays
    local k=$1 L=$2 ms; ms=$(maxst "$L"); ms=${ms:-0}; g_ms=$ms
    [ "$ms" -ge 1 ] 2>/dev/null && r1[$k]=$(( ${r1[$k]} + 1 ))
    grep -qE 'st620 [0-9]+ -> 3( |$)' "$L" && r3[$k]=$(( ${r3[$k]} + 1 ))
    [ "$ms" -ge 5 ] 2>/dev/null && p3[$k]=$(( ${p3[$k]} + 1 ))
    grep -qE 'st620 3 -> 0( |$)' "$L" && rs[$k]=$(( ${rs[$k]} + 1 ))
    [ "$(cnt "$L" 'arming EOS read-hook')" -gt 0 ] && ar[$k]=$(( ${ar[$k]} + 1 ))
    [ "$(cnt "$L" '\[MOVIEDONE\] done')"   -gt 0 ] && dn[$k]=$(( ${dn[$k]} + 1 ))
    [ "$(cnt "$L" 'read-hook HIT')"        -gt 0 ] && ht[$k]=$(( ${ht[$k]} + 1 ))
    [ "$(cnt "$L" 'R_LglScA')"             -gt 0 ] && wl[$k]=$(( ${wl[$k]} + 1 ))
    [ "$(cnt "$L" 'R_PermA')"              -gt 0 ] && wp[$k]=$(( ${wp[$k]} + 1 ))
}

printf '%-4s | %-22s | %-34s\n' "it" "NO-ARM (M3)" "WITH-ARM (M2 DONE_MS=auto)"
for i in $(seq 1 "$M"); do
    run_one "$TMP/noarm_$i.log"
    score 0 "$TMP/noarm_$i.log"; msn=$g_ms
    run_one "$TMP/arm_$i.log" PS3_MOVIE_DONE_MS=auto
    score 1 "$TMP/arm_$i.log"; msa=$g_ms
    printf '%-4s | maxst=%-3s arm=%-3s        | maxst=%-3s done=%s arm=%s hit=%s lgl=%s perm=%s\n' \
        "$i" "$msn" "$(cnt "$TMP/noarm_$i.log" 'arming EOS')" "$msa" \
        "$(cnt "$TMP/arm_$i.log" 'MOVIEDONE] done')" "$(cnt "$TMP/arm_$i.log" 'arming EOS')" \
        "$(cnt "$TMP/arm_$i.log" 'read-hook HIT')" "$(cnt "$TMP/arm_$i.log" R_LglScA)" "$(cnt "$TMP/arm_$i.log" R_PermA)"
done

echo "======================================================================"
printf '%-24s %8s %8s\n' "metrica (de $M runs)" "NO-ARM" "WITH-ARM"
printf '%-24s %8s %8s\n' "st620>=1 (playback)"   "${r1[0]}" "${r1[1]}"
printf '%-24s %8s %8s\n' "st620==3 (parou em 3)" "${r3[0]}" "${r3[1]}"
printf '%-24s %8s %8s\n' "st620>=5 (avancou>3)"  "${p3[0]}" "${p3[1]}"
printf '%-24s %8s %8s\n' "st620 3->0 (reset)"    "${rs[0]}" "${rs[1]}"
printf '%-24s %8s %8s\n' "produtor done disparou" "${dn[0]}" "${dn[1]}"
printf '%-24s %8s %8s\n' "[MOVIEEOS] armou"      "${ar[0]}" "${ar[1]}"
printf '%-24s %8s %8s\n' "read-hook HIT"         "${ht[0]}" "${ht[1]}"
printf '%-24s %8s %8s\n' "R_LglScA abriu"        "${wl[0]}" "${wl[1]}"
printf '%-24s %8s %8s\n' "R_PermA abriu"         "${wp[0]}" "${wp[1]}"
echo "orfaos '$PAT' vivos: $(pgrep -f "$PAT" | wc -l | tr -d ' ')  logs: $TMP"

rc=0
[ "${ar[0]}" -eq 0 ] || { echo "FALHA: M3 armou sem produtor em ${ar[0]}/$M (forja)"; rc=1; }
[ "${dn[1]}" -ge 1 ] || { echo "FALHA: produtor de done nunca disparou no braco WITH-ARM"; rc=1; }
[ "${ar[1]}" -ge 1 ] || { echo "FALHA: WITH-ARM nunca armou apesar do done"; rc=1; }
echo "RESULT rc=$rc"
exit $rc
