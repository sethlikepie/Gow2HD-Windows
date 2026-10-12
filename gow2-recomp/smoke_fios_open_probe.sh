#!/usr/bin/env bash
# smoke_fios_open_probe.sh -- matriz multi-run do OPEN FIOS da intro (Task 2 do
# plano ps3recomp/docs/superpowers/plans/2026-07-20-macos-intro-audio-open-wall.md).
#
# O boot e' NAO-DETERMINISTICO (contagem de erros de shader varia 40k-55k, o log
# varia 88k-118k linhas). Uma corrida nao e' prova: este smoke corre M vezes e
# classifica por N-de-M.
#
# Dois bracos:
#   A (probe)   PS3_TRACE_FIOSOPEN=1  -> classifica F0..F4 e imprime o caminho
#                                        exacto pedido ao open FIOS
#   B (control) PS3_TRACE_FIOSOPEN por definir -> prova que a probe esta OFF por
#                                        default (zero linhas [FIOSOPEN]) e que
#                                        os handles do [MOVIEOBJ] nao dependem
#                                        dela
#
# Uso: ./smoke_fios_open_probe.sh [M] [SEGUNDOS] [A|B|AB]
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

M="${1:-8}"
SECS="${2:-25}"
ARMS="${3:-AB}"

[ -x ./boot_gow2 ] || { echo "FAIL: boot_gow2 nao existe -- corra ./build_macos.sh" >&2; exit 1; }
[ -f EBOOT.ELF ]   || { echo "FAIL: EBOOT.ELF ausente" >&2; exit 1; }

TMP="$(mktemp -d /tmp/gow2_fiosopen.XXXXXX)"
SUMMARY="$TMP/summary.txt"
echo "M=$M SECS=$SECS arms=$ARMS logs=$TMP" | tee "$SUMMARY"

run_one() {   # $1=braco  $2=indice
    local arm="$1" i="$2"
    # `exec` NAO e' decorativo: sem ele o subshell fica a ser o pai do
    # boot_gow2, `$!` e' o subshell, e o `kill` deixa o boot_gow2 orfao a
    # correr (e a escrever no log) para sempre. Com 16 corridas isso acumula
    # 16 processos a disputar CPU e envenena a propria matriz -- ja aconteceu.
    (
        set -a; . "$HERE/env_gow2.sh"; set +a
        export PS3_NO_RSX=1
        export PS3_TRACE_MOVIEOBJ=1
        export PS3_MOVIE_EOS=0            # baseline: nao armar EOS
        unset PS3_VDEC_FORCE_SEQDONE_MS   # nada de FORCE
        if [ "$arm" = A ]; then export PS3_TRACE_FIOSOPEN=1; else unset PS3_TRACE_FIOSOPEN; fi
        exec ./boot_gow2 EBOOT.ELF >"$TMP/${arm}_$i.log" 2>&1
    ) &
    local pid=$!
    sleep "$SECS"
    kill -9 "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null || true
    # Rede de seguranca: se alguma corrida escapar, nao arrancar a seguinte com
    # o CPU tomado. So apanha o NOSSO padrao de linha de comando.
    pkill -9 -f 'boot_gow2 EBOOT.ELF' 2>/dev/null || true
    sleep 1
}

classify() {   # $1=log -> imprime "F<x> <detalhe>"
    local log="$1"
    local play b4340 d578_in d578_ret0 d578_retN alloc0 file0 path
    play=$(grep -ac '\[FIOSOPEN\] Play 002C00DC'   "$log")
    b4340=$(grep -ac '\[FIOSOPEN\] 002B4340 '      "$log")
    d578_in=$(grep -ac '\[FIOSOPEN\] 0030D578<'    "$log")
    d578_ret0=$(grep -ac 'OPEN DEVOLVEU 0'         "$log")
    d578_retN=$(( $(grep -ac '\[FIOSOPEN\] 0030D578>' "$log") - d578_ret0 ))
    alloc0=$(grep -ac 'SEM OP LIVRE'               "$log")
    file0=$(grep -ac 'MEMBRO RECUSADO'             "$log")
    path=$(grep -a '\[FIOSOPEN\] 0030D578<' "$log" | sed -n "s/.*path='\([^']*\)'.*/\1/p" \
           | grep -v '^/gow2.psarc$' | head -1)
    if   [ "$(grep -ac '\[FIOSOPEN\]' "$log")" -eq 0 ]; then echo "PROBE-OFF (0 linhas [FIOSOPEN])"
    elif [ "$b4340" -eq 0 ]; then echo "F0 (Play nao pede o open: play=$play)"
    elif [ "$d578_in" -eq 0 ]; then echo "F1 (002B4340=$b4340 sem 0030D578)"
    elif [ "$d578_ret0" -gt 0 ]; then echo "F2 (ret=0 alloc0=$alloc0 file0=$file0 path=${path:-?})"
    elif [ "$d578_retN" -gt 0 ]; then echo "OK-OPEN (handles != 0, path=${path:-?})"
    else echo "F1? (0030D578 entrou mas nunca retornou)"
    fi
}

for arm in A B; do
    case "$ARMS" in *"$arm"*) ;; *) continue ;; esac
    for i in $(seq 1 "$M"); do
        run_one "$arm" "$i"
        log="$TMP/${arm}_$i.log"
        fios=$(grep -ac '\[FIOSOPEN\]' "$log")
        maxst=$(grep -a '\[MOVIEFSM\] st620' "$log" | sed -n 's/.*-> \([0-9]*\) .*/\1/p' | sort -n | tail -1)
        # `grep -a ... open=0x` primeiro: as linhas '[MOVIEOBJ] obj=0x... (unset)'
    # nao tem campo open nenhum e contavam como nao-nulas.
    objN=$(grep -a '\[MOVIEOBJ\] .*open=0x' "$log" | grep -vc 'open=0x00000000')
        obj0=$(grep -ac '\[MOVIEOBJ\] .*open=0x00000000' "$log")
        # done=[op+0x90] visto pelo poll do estado 1 (so no braco A)
        donN=$(grep -a '\[FIOSOPEN\] 002B4224 poll' "$log" | grep -vc 'done=0x00000000')
        rec=$(grep -ac 'recursion cap @0x002C07D8' "$log")
        bus=$(grep -acE 'SIGBUS|Bus error' "$log")
        cls=$(classify "$log")
        printf 'arm=%s run=%d fios_lines=%s class=%-44s maxst=%s objNZ=%s obj0=%s pollDone=%s recC07=%s bus=%s\n' \
            "$arm" "$i" "$fios" "$cls" "${maxst:-?}" "$objN" "$obj0" "$donN" "$rec" "$bus" | tee -a "$SUMMARY"
    done
done

echo | tee -a "$SUMMARY"
echo "-- agregados --" | tee -a "$SUMMARY"
for arm in A B; do
    case "$ARMS" in *"$arm"*) ;; *) continue ;; esac
    tot=$(grep -c "^arm=$arm " "$SUMMARY")
    okopen=$(grep -c "^arm=$arm .*class=OK-OPEN" "$SUMMARY")
    off=$(grep -c "^arm=$arm .*class=PROBE-OFF" "$SUMMARY")
    f0=$(grep -c "^arm=$arm .*class=F0" "$SUMMARY")
    f1=$(grep -c "^arm=$arm .*class=F1" "$SUMMARY")
    f2=$(grep -c "^arm=$arm .*class=F2" "$SUMMARY")
    objnz=$(grep "^arm=$arm " "$SUMMARY" | grep -vc 'objNZ=0 ')
    printf 'arm=%s M=%s OK-OPEN=%s PROBE-OFF=%s F0=%s F1=%s F2=%s runs_com_handle_nao_nulo=%s\n' \
        "$arm" "$tot" "$okopen" "$off" "$f0" "$f1" "$f2" "$objnz" | tee -a "$SUMMARY"
done
echo "logs: $TMP"
