#!/usr/bin/env bash
# smoke_intro_open_wall.sh -- Task 0/1 do plano
# ../ps3recomp/docs/superpowers/plans/2026-07-20-macos-intro-audio-open-wall.md
#
# Correlacao MULTI-RUN do wall de open da intro. O boot do GoW2 no macOS e'
# NAO-DETERMINISTA (contagem de erros de shader 40k..55k, log 88k..118k linhas,
# st620 chega a 3 em ~1 de 8 runs e fica em 1 nos outros), portanto NENHUM
# criterio deste ficheiro e' de uma corrida so': todos sao N de M.
#
# Por corrida regista-se:
#   wav      linhas "snd_stream: couldn't open file .../_movies/...wav" do proprio jogo
#   maxst    maior st620 visto ([MOVIEFSM] transicoes + [MOVIEOBJ] amostras)
#   openN    amostras [MOVIEOBJ] com handle de open != 0  (FIOS obj+0x630)
#   recC07   "recursion cap @0x002C07D8" (gate de EOS -- consequencia do park)
#   recCB9   "recursion cap @0x0045CB90" (helper snd_stream -- canal de SIGBUS)
#   movieio  "[movieio] open" de qualquer membro
#   smlogo   QUALQUER [fs]/[movieio] que mencione SmLogo/.wav  (esperado: 0)
#   ORDER    ordem de PRIMEIRA aparicao dos eventos (por numero de linha)
#
# MODE:
#   base       (default) baseline sem probes -- Task 0
#   probe_snd  PS3_TRACE_SNDOPEN=1 -- Task 1 (probe de patch_snd_open_probe.py)
#
# Uso: ./smoke_intro_open_wall.sh [M] [SEGUNDOS] [MODE]
#      ./smoke_intro_open_wall.sh 8 25 base
#      ./smoke_intro_open_wall.sh 8 25 probe_snd
#
# ---------------------------------------------------------------------------
# RESULTADO Task 0 (M=8, 25 s, baseline, 2026-07-20)
#
#   parked_at_le1=7/8   left_ge3=1/8   (a nao-determinacao do plano confirma-se)
#   smlogo_io=0/8       zero [fs]/[movieio] para SmLogo/.wav/.m2v/.vpk
#   movieio=8/8         so' o gow2.psarc inteiro
#   openN=8/8           obj+0x630/+0x634 NAO ficam a zero (ver nota abaixo)
#   wav=0/8             a linha "couldn't open file" NAO aparece no baseline
#   recC07=6/8  recCB9=1/8  bus=0/8   lines=22909..127608
#
#   ORDER (6/8): MOVIEOBJ open=0 -> st620 0->1 -> recursion cap @0x002C07D8
#   ORDER (1/8): ... -> recursion cap @0x0045CB90   (o run que chega a st620=3)
#   ORDER (1/8): ... -> (nem cap)
#
#   DUAS CORRECCOES a factos herdados do brief da task:
#   1. Os handles FIOS obj+0x630/+0x634 NAO sao 0 o run inteiro. Sobem para
#      open=0x43018528 read=0x430094C0/0x430095A0 e ficam la a maioria das
#      amostras (5..108 linhas [MOVIEOBJ] por run). O que fica a zero e' o
#      f744 (EOS).
#   2. A mensagem "snd_stream: couldn't open file" nao e' fiavel como sinal:
#      func_0045CF50 tem porta de verbosidade ([[TOC+0x604]] < nivel => cala),
#      e no baseline ela sai 0/8. Medir a ENTRADA do open, nao a mensagem.
#
# ---------------------------------------------------------------------------
# RESULTADO Task 1 -- HOST_ENTRY_AUDIO: **NENHUM** (o open nunca sai do guest)
#
#   Medido in-boot com PS3_TRACE_SNDOPEN=1 (patch_snd_open_probe.py):
#
#     [SNDOPEN] 0045E230 enter params=0x0FEFF9F0 p_CC=0x0FEFF8F0
#                        path='/_movies/SmLogo_v2.wav'
#     [SNDOPEN] 00461658 OPEN enter stream=0x4309EF00 sub=0x4309F120
#                        f110=0x00000000 ramo=A-004618A0(TOC+0x750)
#     [SNDOPEN]   004618A0 open() drv=0x009B95BC opd=0x0052F650
#                        code=0x002B47D4 toc=0x00541178  class=GUEST-code-EA
#     [SNDOPEN] 004618EC FAIL rc=-2 sub=0x430A26C8
#
#   O open aterra em codigo do PROPRIO GUEST -- func_002B47D4 -- e dai em
#   func_0030D578, o mesmo choke point FIOS que a Task 2 persegue no lado do
#   video. Nao ha cellFsOpen, nao ha movie_io, nao ha import de firmware no
#   caminho. Audio e video partilham o MESMO open de membro do psarc.
#
#   EVIDENCIA (duas suites M=8 sobre o MESMO binario, probe ON e probe OFF):
#     path='/_movies/SmLogo_v2.wav'       8/8 com PS3_TRACE_SNDOPEN=1
#     code=0x002B47D4 (GUEST-code-EA)     8/8
#     [SNDOPEN] sem a variavel definida   0/8   (OFF por default, no-op)
#     [fs] open                           1 linha/run, sempre so' o gow2.psarc
#     smlogo_io                           0/8 nas duas suites
#     ./smoke_boot_mac.sh 25 com probe OFF -> PASS (sem regressao)
#
#   CORRELACAO (input para a Task 3, NAO e' decisao desta task): nas duas
#   suites deu left_with_wav = 0.
#     probe ON : parked=6/8 left_ge3=2/8 wav=6/8 wav_and_park=6 left_with_wav=0
#     probe OFF: parked=7/8 left_ge3=1/8 wav=7/8 wav_and_park=7 left_with_wav=0
#   Ou seja, em 16 corridas, as 13 que ficam presas em st620=1 imprimem TODAS a
#   falha do wav e as 3 que chegam a st620>=3 nao imprimem NENHUMA. Pela regra
#   do Step 1 da Task 3 isto e' WAV_CAUSAL_OR_CORRELATED -- confirmar com o par
#   D0/D1 antes de agir.
#
#   AVISO DE MEDICAO: a suite Task 0 (14:18, binario anterior) correu com load
#   ~11 e deu wav=0/8; as suites da Task 1 (14:31/14:34) correram com load ~40
#   (outra sessao com 8 boots concorrentes a fugir) e deram wav=6/8 e 7/8. A
#   taxa de aparecimento da mensagem e' sensivel a carga da maquina e ao
#   binario; comparacoes ENTRE suites nao valem -- so' valem os N-de-M DENTRO
#   de cada suite, que e' o que os numeros acima sao.
# ---------------------------------------------------------------------------
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

M="${1:-8}"
SECS="${2:-25}"
MODE="${3:-base}"

if [ ! -x ./boot_gow2 ] || [ ! -f EBOOT.ELF ]; then
    echo "FAIL: falta boot_gow2 ou EBOOT.ELF" >&2
    exit 1
fi

TMP=$(mktemp -d /tmp/gow2_openwall.XXXXXX)
SUMMARY="$TMP/summary.txt"

# O binario pode ser reconstruido por outra sessao a meio da suite -- isso
# invalidaria a comparacao. Fixa-se a identidade no inicio e verifica-se no fim.
BIN_MD5_BEFORE="$( (md5 -q ./boot_gow2 2>/dev/null || md5sum ./boot_gow2 | cut -d' ' -f1) )"

run_one() {
    local i=$1
    (
        set -a; . "$HERE/env_gow2.sh"; set +a
        export PS3_NO_RSX=1
        export PS3_TRACE_MOVIEOBJ=1
        # Baseline honesto: NAO armar EOS, NAO forcar SEQDONE.
        unset PS3_VDEC_FORCE_SEQDONE_MS
        export PS3_MOVIE_EOS=0
        case "$MODE" in
            probe_snd) export PS3_TRACE_SNDOPEN=1 ;;
        esac
        exec ./boot_gow2 EBOOT.ELF >"$TMP/run_$i.log" 2>&1
    ) &
    local pid=$!
    sleep "$SECS"
    kill -9 "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null || true
}

# Ordem de PRIMEIRA aparicao: numero de linha do 1o hit de cada evento.
first_order() {
    local log=$1
    "$PYBIN" - "$log" <<'PY'
import re, sys
pats = [
    ("vdecQAE",  re.compile(rb"cellVdecQueryAttrEx")),
    ("wavfail",  re.compile(rb"couldn't open file")),
    ("st620->1", re.compile(rb"\[MOVIEFSM\] st620 \d+ -> 1")),
    ("obj0",     re.compile(rb"\[MOVIEOBJ\].*open=0x00000000")),
    ("sndopen",  re.compile(rb"\[SNDOPEN\]")),
    ("recC07",   re.compile(rb"recursion cap @0x002C07D8")),
    ("recCB9",   re.compile(rb"recursion cap @0x0045CB90")),
]
seen = {}
with open(sys.argv[1], "rb") as fh:
    for n, line in enumerate(fh, 1):
        for name, rx in pats:
            if name not in seen and rx.search(line):
                seen[name] = n
print("<".join(k for k, _ in sorted(seen.items(), key=lambda kv: kv[1])) or "-")
PY
}

pick_python() {
    local c
    for c in python3.14 python3.13 python3.12 python3.11 python3.10 python3 \
             /opt/homebrew/bin/python3 /usr/local/bin/python3; do
        command -v "$c" >/dev/null 2>&1 || continue
        if "$c" -c 'import sys; sys.exit(0 if sys.version_info >= (3,8) else 1)' 2>/dev/null; then
            command -v "$c"; return 0
        fi
    done
    return 1
}
PYBIN="$(pick_python)"

{
    echo "M=$M SECS=$SECS MODE=$MODE"
    echo "bin=boot_gow2 md5=$BIN_MD5_BEFORE date=$(date '+%F %T')"
} | tee "$SUMMARY"

for i in $(seq 1 "$M"); do
    run_one "$i"
    log="$TMP/run_$i.log"
    lines=$(wc -l <"$log" | tr -d ' ')
    wav=$(grep -ac "couldn't open file" "$log" || true)
    st_fsm=$(grep -aE '\[MOVIEFSM\] st620' "$log" | sed -n 's/.*-> \([0-9]*\).*/\1/p' | sort -n | tail -1)
    st_obj=$(grep -a '\[MOVIEOBJ\]' "$log" | sed -n 's/.*st620=\([0-9]*\).*/\1/p' | sort -n | tail -1)
    maxst=$(printf '%s\n%s\n' "${st_fsm:-0}" "${st_obj:-0}" | sort -n | tail -1)
    openN=$(grep -a '\[MOVIEOBJ\]' "$log" | grep -vc 'open=0x00000000' || true)
    sndopen=$(grep -ac '\[SNDOPEN\]' "$log" || true)
    recC07=$(grep -ac 'recursion cap @0x002C07D8' "$log" || true)
    recCB9=$(grep -ac 'recursion cap @0x0045CB90' "$log" || true)
    bus=$(grep -acE 'SIGBUS|Bus error' "$log" || true)
    mio=$(grep -ac '\[movieio\] open' "$log" || true)
    # A pergunta central da Task 0: o pedido do SmLogo chega a ALGUM caminho de
    # I/O do host? Conta linhas [fs]/[movieio] que mencionem o filme ou .wav.
    smlogo=$(grep -aiE '\[(fs|movieio)\][^\n]*(smlogo|\.wav|\.m2v|\.vpk)' "$log" | grep -c . || true)
    order=$(first_order "$log")
    printf 'run=%d wav=%s maxst=%s openN=%s sndopen=%s recC07=%s recCB9=%s bus=%s movieio=%s smlogo_io=%s lines=%s ORDER=%s\n' \
        "$i" "$wav" "$maxst" "$openN" "$sndopen" "$recC07" "$recCB9" "$bus" "$mio" "$smlogo" "$lines" "$order" \
        | tee -a "$SUMMARY"
done

BIN_MD5_AFTER="$( (md5 -q ./boot_gow2 2>/dev/null || md5sum ./boot_gow2 | cut -d' ' -f1) )"
if [ "$BIN_MD5_BEFORE" != "$BIN_MD5_AFTER" ]; then
    echo "AVISO: boot_gow2 mudou a meio da suite ($BIN_MD5_BEFORE -> $BIN_MD5_AFTER)" | tee -a "$SUMMARY"
fi

"$PYBIN" - "$SUMMARY" <<'PY' | tee -a "$SUMMARY"
import sys, re
from collections import Counter
rx = re.compile(
    r'run=(\d+) wav=(\d+) maxst=(\d+) openN=(\d+) sndopen=(\d+) recC07=(\d+) '
    r'recCB9=(\d+) bus=(\d+) movieio=(\d+) smlogo_io=(\d+) lines=(\d+) ORDER=(\S+)')
rows = []
for line in open(sys.argv[1]):
    m = rx.search(line)
    if m:
        g = m.groups()
        rows.append(dict(run=int(g[0]), wav=int(g[1]), maxst=int(g[2]), openN=int(g[3]),
                         sndopen=int(g[4]), recC07=int(g[5]), recCB9=int(g[6]),
                         bus=int(g[7]), movieio=int(g[8]), smlogo=int(g[9]),
                         lines=int(g[10]), order=g[11]))
if not rows:
    print('FAIL: no rows'); sys.exit(1)
M = len(rows)
parked = sum(1 for r in rows if r['maxst'] <= 1)
left   = sum(1 for r in rows if r['maxst'] >= 3)
wav_and_park  = sum(1 for r in rows if r['wav'] > 0 and r['maxst'] <= 1)
left_with_wav = sum(1 for r in rows if r['maxst'] >= 3 and r['wav'] > 0)
print()
print('AGG M=%d parked_at_le1=%d left_ge3=%d wav_any=%d wav_and_park=%d left_with_wav=%d'
      % (M, parked, left, sum(1 for r in rows if r['wav'] > 0), wav_and_park, left_with_wav))
print('AGG smlogo_io_nonzero=%d/%d  movieio_nonzero=%d/%d  openN_nonzero=%d/%d  sndopen_nonzero=%d/%d'
      % (sum(1 for r in rows if r['smlogo']), M,
         sum(1 for r in rows if r['movieio']), M,
         sum(1 for r in rows if r['openN']), M,
         sum(1 for r in rows if r['sndopen']), M))
print('AGG recC07_nonzero=%d/%d recCB9_nonzero=%d/%d bus_nonzero=%d/%d lines_min=%d lines_max=%d'
      % (sum(1 for r in rows if r['recC07']), M, sum(1 for r in rows if r['recCB9']), M,
         sum(1 for r in rows if r['bus']), M,
         min(r['lines'] for r in rows), max(r['lines'] for r in rows)))
for order, n in Counter(r['order'] for r in rows).most_common():
    print('ORDER %d/%d: %s' % (n, M, order))
print('DECISION_HINT: se left_with_wav ~= left_ge3 e left_ge3>0 -> wav SOBREVIVIVEL; '
      'se left_with_wav==0 e left_ge3>0 -> wav CORRELACIONADO com o park; '
      'se left_ge3==0 -> INCONCLUSIVO (Task 3)')
PY

echo
echo "logs: $TMP"
