#!/usr/bin/env bash
# smoke_bctr_tail.sh -- matriz A/B do fix "bctr e' um SALTO, nao uma chamada".
#
# O boot e' NAO-DETERMINISTICO (erros de shader 40k-55k, log 88k-118k linhas,
# e no baseline a intro saia do estado 1 em ~5/16 corridas SEM mudanca nenhuma
# de codigo). Uma corrida nunca e' prova: aqui correm-se M corridas por braco e
# classifica-se por N-de-M. Os dois bracos usam o MESMO binario -- so muda a
# variavel de ambiente -- para que a unica diferenca medida seja o fix.
#
#   F (fix, default) : PS3_BCTR_HOSTCALL por definir
#                      -> `bctr` despacha em cauda (g_trampoline_fn), stack O(1)
#   L (legacy)       : PS3_BCTR_HOSTCALL=1
#                      -> `bctr` volta a ser chamada host, stack O(iteracoes)
#
# O QUE SE MEDE
#   recCAP    numero de "[ppu] recursion cap @0x002C0(7D8|5F8)" -- o pump da FSM
#             da intro a rebentar o orcamento de 4000 frames host
#   maxst     maior st620 visto pelo amostrador [MOVIEOBJ] (1 = parado no open,
#             >=3 = o poll de [op+0x90] devolveu "done")
#   DONE      linhas "[FIOSOPEN] 002B4274 DONE" -- a observacao DIRECTA (e sem
#             corrida) de func_002B4224 a ver [op+0x90] != 0: func_002B4274 e o
#             ramo que so existe depois desse teste passar
#
# Uso: ./smoke_bctr_tail.sh [M] [SEGUNDOS] [F|L|FL]
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

M="${1:-8}"
SECS="${2:-25}"
ARMS="${3:-FL}"

[ -x ./boot_gow2 ] || { echo "FAIL: boot_gow2 nao existe -- corra ./build_macos.sh" >&2; exit 1; }
[ -f EBOOT.ELF ]   || { echo "FAIL: EBOOT.ELF ausente" >&2; exit 1; }

TMP="$(mktemp -d /tmp/gow2_bctr.XXXXXX)"
SUMMARY="$TMP/summary.txt"
echo "M=$M SECS=$SECS arms=$ARMS logs=$TMP" | tee "$SUMMARY"

run_one() {   # $1=braco  $2=indice
    local arm="$1" i="$2"
    # `exec` NAO e' decorativo: sem ele o subshell e' o pai do boot_gow2, `$!` e'
    # o subshell, e o `kill` deixa o boot_gow2 orfao a queimar CPU e a escrever
    # no log para sempre -- ja envenenou uma matriz inteira nesta sessao.
    (
        set -a; . "$HERE/env_gow2.sh"; set +a
        export PS3_NO_RSX=1
        export PS3_TRACE_MOVIEOBJ=1
        export PS3_TRACE_FIOSOPEN=1
        export PS3_TRACE_FIOSSCHED=1
        export PS3_MOVIE_EOS=0            # baseline: nao armar EOS
        unset PS3_VDEC_FORCE_SEQDONE_MS   # nada de FORCE
        if [ "$arm" = L ]; then export PS3_BCTR_HOSTCALL=1; else unset PS3_BCTR_HOSTCALL; fi
        exec ./boot_gow2 EBOOT.ELF >"$TMP/${arm}_$i.log" 2>&1
    ) &
    local pid=$!
    sleep "$SECS"
    kill -9 "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null || true
    pkill -9 -f 'boot_gow2 EBOOT.ELF' 2>/dev/null || true
    sleep 1
}

for arm in F L; do
    case "$ARMS" in *"$arm"*) ;; *) continue ;; esac
    for i in $(seq 1 "$M"); do
        run_one "$arm" "$i"
        log="$TMP/${arm}_$i.log"
        cap=$(grep -acE 'recursion cap @0x002C0(7D8|5F8)' "$log")
        capany=$(grep -ac 'recursion cap' "$log")
        maxst=$(grep -a '\[MOVIEOBJ\]' "$log" | sed -n 's/.*st620=\([0-9]*\).*/\1/p' | sort -n | tail -1)
        done_n=$(grep -ac "002B4274 DONE" "$log")
        prod=$(grep -ac '\[FIOSSCHED\] done90' "$log")
        noop=$(grep -ac 'SEM OP LIVRE' "$log")
        objN=$(grep -a '\[MOVIEOBJ\] .*open=0x' "$log" | grep -vc 'open=0x00000000')
        f744=$(grep -a '\[MOVIEOBJ\]' "$log" | grep -vc 'f744=0 ')
        bus=$(grep -acE 'SIGBUS|Bus error' "$log")
        lines=$(wc -l < "$log" | tr -d ' ')
        printf 'arm=%s run=%d recCAP_fsm=%s recCAP_any=%s maxst=%s DONE=%s prod90=%s semop=%s objNZ=%s f744NZ=%s bus=%s lines=%s\n' \
            "$arm" "$i" "$cap" "$capany" "${maxst:-?}" "$done_n" "$prod" "$noop" "$objN" "$f744" "$bus" "$lines" | tee -a "$SUMMARY"
    done
done

echo | tee -a "$SUMMARY"
echo "-- agregados (N de M) --" | tee -a "$SUMMARY"
for arm in F L; do
    case "$ARMS" in *"$arm"*) ;; *) continue ;; esac
    tot=$(grep -c "^arm=$arm " "$SUMMARY")
    nocap=$(grep "^arm=$arm " "$SUMMARY" | grep -c 'recCAP_fsm=0 ')
    st3=$(grep "^arm=$arm " "$SUMMARY" | awk '{for(i=1;i<=NF;i++) if($i ~ /^maxst=/){split($i,a,"=" ); if(a[2]+0>=3) c++}} END{print c+0}')
    dn=$(grep "^arm=$arm " "$SUMMARY" | grep -vc 'DONE=0 ')
    bus=$(grep "^arm=$arm " "$SUMMARY" | grep -vc 'bus=0 ')
    printf 'arm=%s M=%s sem_recursion_cap_na_FSM=%s/%s maxst>=3=%s/%s poll_viu_DONE=%s/%s runs_com_SIGBUS=%s/%s\n' \
        "$arm" "$tot" "$nocap" "$tot" "$st3" "$tot" "$dn" "$tot" "$bus" "$tot" | tee -a "$SUMMARY"
done
echo "logs: $TMP"
echo "orfaos deixados: $(pgrep -c -f boot_gow2 2>/dev/null || echo 0)"
