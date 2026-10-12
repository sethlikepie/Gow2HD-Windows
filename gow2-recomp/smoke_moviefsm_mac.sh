#!/usr/bin/env bash
# Smoke da Task 1 do plano
# ../ps3recomp/docs/superpowers/plans/2026-07-20-macos-movie-eos-fsm.md:
# amostrador portatil do objecto do movie player no host macOS.
#
# O que se exige, por ordem:
#
#   unit    test_movie_eos_policy: leitura BE com verificacao de commit ANTES de
#           tocar na memoria (a pagina "nao commitada" e' PROT_NONE a serio, por
#           isso uma leitura indevida mata o processo em vez de passar).
#
#   ON      com PS3_TRACE_MOVIEOBJ=1 aparecem linhas [MOVIEFSM].
#
#   OFF     sem essa variavel NAO aparece nenhuma -- o amostrador nem cria a
#           thread, portanto o baseline nao muda.
#
#   NO-ARM  em NENHUM dos dois casos pode aparecer [MOVIEEOS]. Esta e a
#           aceitacao de que a Task 1 nao invadiu a Task 3: armar o read-hook
#           sem um produtor real de "done" (movie_hle_overlay_done, que no
#           POSIX devolve 0 sempre) seria forjar progresso do guest.
#
#   MAPA    o ppu_guest_range_committed passa a responder segundo as regioes
#           que o host commitou, em vez do "sim" universal de guard aberto.
#
# Uso: ./smoke_moviefsm_mac.sh [segundos]        (default 25)
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

SECS="${1:-25}"
TMP=$(mktemp -d /tmp/gow2_moviefsm.XXXXXX)
FAILED=0

fail() { echo "FAIL: $1"; FAILED=1; }

# --- 1. unit offline -----------------------------------------------------
echo "== unit: test_movie_eos_policy =="
if clang -std=c11 -O0 -Wall -I "$HERE" \
        tests/test_movie_eos_policy.c movie_eos_arm.c -o "$TMP/test_policy" 2>"$TMP/cc.log"; then
    if "$TMP/test_policy" > "$TMP/unit.log" 2>&1; then
        echo "  PASS ($(grep -c '  ok:' "$TMP/unit.log") assercoes)"
    else
        fail "unit reprovou"; grep FAIL "$TMP/unit.log" | sed 's/^/    /'
    fi
else
    fail "unit nao compila"; sed 's/^/    /' "$TMP/cc.log" | head -10
fi

# --- 2. in-boot ----------------------------------------------------------
if [ ! -x ./boot_gow2 ] || [ ! -f EBOOT.ELF ]; then
    echo "SKIP in-boot: falta boot_gow2 ou EBOOT.ELF"
    rm -rf "$TMP"; exit $FAILED
fi

run_boot() {   # $1 = nome, resto = atribuicoes de env
    local name=$1; shift
    ( set -a; . "$HERE/env_gow2.sh"; set +a
      export PS3_NO_RSX=1
      # Task 3 acrescentou PS3_MOVIE_EOS como 2o gate do amostrador (o arm do
      # canal NATURAL). env_gow2.sh liga PS3_MOVIE_EOS=1 por default, portanto o
      # caso OFF tem de desligar OS DOIS gates para o amostrador nem se criar --
      # senao [MOVIEFSM] aparecia no OFF e este smoke reprovava. O caso ON
      # re-liga o que precisa via os args.
      unset PS3_TRACE_MOVIEOBJ PS3_MOVIE_EOS
      for kv in "$@"; do export "$kv"; done
      # exec: a subshell VIRA o boot_gow2, portanto $pid e' o proprio processo e
      # o kill abaixo mata-o directamente (sem deixar orfao a ~290% CPU, que era
      # o que acontecia quando o kill atingia so a subshell wrapper).
      exec ./boot_gow2 EBOOT.ELF > "$TMP/$name.log" 2>&1 ) &
    local pid=$!
    sleep "$SECS"
    kill -9 $pid 2>/dev/null
    wait $pid 2>/dev/null
}

echo "== in-boot ON: PS3_TRACE_MOVIEOBJ=1 (${SECS}s) =="
run_boot on PS3_TRACE_MOVIEOBJ=1 PS3_TRACE_COMMITMAP=1
FSM_ON=$(grep -c '\[MOVIEFSM\]' "$TMP/on.log" || true)
EOS_ON=$(grep -c '\[MOVIEEOS\]' "$TMP/on.log" || true)
RANGES=$(grep -c '\[vm\] committed range' "$TMP/on.log" || true)
DIVERG=$(grep -oE '\[commitmap\] divergencias=[0-9]+' "$TMP/on.log" | tail -1 | grep -oE '[0-9]+$' || echo "?")
echo "  [MOVIEFSM]=$FSM_ON  [MOVIEEOS]=$EOS_ON  ranges=$RANGES  commitmap divergencias=$DIVERG"
grep '\[MOVIEFSM\]' "$TMP/on.log" | head -3 | sed 's/^/    /'

[ "$FSM_ON" -ge 1 ]  || fail "sem linhas [MOVIEFSM] com o amostrador ligado"
[ "$EOS_ON" -eq 0 ]  || fail "[MOVIEEOS] apareceu: a Task 1 armou o read-hook (proibido)"
[ "$RANGES" -eq 3 ]  || fail "esperadas 3 regioes commitadas registadas, vi $RANGES"
[ "$DIVERG" = "0" ]  || fail "o guard nao concorda com as regioes commitadas (divergencias=$DIVERG)"

echo "== in-boot OFF: sem PS3_TRACE_MOVIEOBJ (${SECS}s) =="
run_boot off
FSM_OFF=$(grep -c '\[MOVIEFSM\]' "$TMP/off.log" || true)
EOS_OFF=$(grep -c '\[MOVIEEOS\]' "$TMP/off.log" || true)
OBJ_OFF=$(grep -c '\[MOVIEOBJ\]' "$TMP/off.log" || true)
echo "  [MOVIEFSM]=$FSM_OFF  [MOVIEOBJ]=$OBJ_OFF  [MOVIEEOS]=$EOS_OFF"

[ "$FSM_OFF" -eq 0 ] || fail "[MOVIEFSM] apareceu sem a env var (devia estar OFF por default)"
[ "$OBJ_OFF" -eq 0 ] || fail "[MOVIEOBJ] apareceu sem a env var (devia estar OFF por default)"
[ "$EOS_OFF" -eq 0 ] || fail "[MOVIEEOS] apareceu no caminho por default (proibido)"

if [ "$FAILED" -eq 0 ]; then
    echo "PASS: amostrador ligado observa, desligado nao existe, e nada arma EOS"
    rm -rf "$TMP"
else
    echo "  logs: $TMP"
fi
exit $FAILED
