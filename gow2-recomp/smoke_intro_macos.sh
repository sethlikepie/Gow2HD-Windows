#!/usr/bin/env bash
# Smoke multi-run do arranque da intro no macOS.
#
# Fecha a Task 4 do plano macos-movie-eos-fsm (paridade de recipe) e a Task 5 do
# plano macos-intro-audio-open-wall (sair do st620=1 sem forjar). Os dois pediam
# o mesmo: um aceite de N-de-M corridas, com etiquetas HONESTAS que distinguem o
# que esta provado do que esta bloqueado.
#
# Uso: ./smoke_intro_macos.sh [M]        (default 8 corridas)
#
# NAO usa `set -e`: os boots correm ate serem mortos por timeout (rc != 0 e
# normal), e um `set -e` mataria o loop -- o mesmo trap que ja custou uma suite.
set +e

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

M="${1:-8}"
SECS="${SMOKE_SECS:-40}"

[ -x ./boot_gow2 ] || { echo "FAIL: boot_gow2 nao existe -- ./build_macos.sh" >&2; exit 1; }
[ -f EBOOT.ELF ]   || { echo "FAIL: EBOOT.ELF ausente" >&2; exit 1; }

. "$HERE/env_gow2.sh"
export PS3_NO_RSX=1 PS3_SPU_ALL=1 PS3_TRACE_MOVIEOBJ=1 PS3_TRACE_FIOSOPEN=1

# --- aviso de ambiente: o boot faulta para swap e o throughput colapsa --------
# O st620 so sobe alem de 1 se o boot tiver folga de RAM para progredir na janela.
# Com a maquina em swap pesado o boot fica ~3x mais lento e a FSM nao avanca --
# nao e regressao do codigo. Medido nesta sessao: 8,5 GB em swap => st620 preso
# em 1; sem pressao => st620=3 em 6/6. O smoke reporta o swap para o leitor saber
# em que regime as barras N-de-M foram medidas.
# sysctl imprime com o separador decimal do locale (8573,88M ou 8573.88M); corta
# em qualquer um dos dois para ficar so com a parte inteira dos MB.
SWAP_USED=$(sysctl -n vm.swapusage 2>/dev/null | grep -oE 'used = [0-9]+' | grep -oE '[0-9]+$')
echo "=== ambiente: swap usado = ${SWAP_USED:-?} MB (acima de ~4000 MB o st620 tende a ficar preso em 1) ==="

# run_case <nome> <env...>: corre um boot, mata-o, imprime as metricas.
run_case() {
    local name="$1"; shift
    local log
    log=$(mktemp "/tmp/smoke_intro_${name}.XXXXXX")
    # `exec` faz o boot herdar o PID da subshell, portanto $! E o PID do boot.
    # Mata-se ESSE PID com -9 (sob swap o SIGTERM nao chega a tempo e o boot
    # sobrevive ao pkill -- foi assim que 10 orfaos a 90% CPU se acumularam em
    # horas, load 75). O pkill -9 fica so como rede, nunca como mecanismo.
    ( exec env "$@" ./boot_gow2 EBOOT.ELF > "$log" 2>&1 ) &
    local bpid=$!
    sleep "$SECS"
    kill -9 "$bpid" 2>/dev/null
    pkill -9 -f 'boot_gow2 EBOOT.ELF' 2>/dev/null
    wait "$bpid" 2>/dev/null
    sleep 1
    local maxst arm hit lgl perm
    maxst=$(grep -oE 'st620 [0-9]+ -> [0-9]+' "$log" | grep -oE '> [0-9]+' | tr -d '> ' \
            | grep -v 4294967295 | sort -n | tail -1)
    arm=$(grep -c 'arming EOS read-hook\|MOVIEEOS' "$log")
    hit=$(grep -c 'read-hook HIT' "$log")
    lgl=$(grep -c 'R_LglScA' "$log")
    perm=$(grep -c 'R_PermA' "$log")
    # 002B4274 DONE: prova de que o poll do estado 1 OBSERVOU a palavra de
    # conclusao [op+0x90] (so alcancavel com done != 0). E o sinal que o sticky
    # + prioridade destravaram; conta-lo aqui e o aceite da Task 8.
    done4274=$(grep -c '002B4274 DONE' "$log")
    echo "${maxst:-0} ${arm} ${hit} ${lgl} ${perm} ${done4274}"
    rm -f "$log"
}

# --- braco M3 (forja-segura): ambiente-INDEPENDENTE -------------------------
# EOS ligado, produtor DESLIGADO. Se algo armar aqui, e forja. Esta barra nao
# depende de RAM: mede que NADA arma sem produtor, o que vale em qualquer regime.
echo; echo "=== M3 (forja-segura): PS3_MOVIE_EOS=1 sem produtor, ${M} corridas ==="
m3_armed=0
for i in $(seq 1 "$M"); do
    read -r st arm hit lgl perm done4274 < <(run_case m3 PS3_MOVIE_EOS=1 PS3_MOVIE_DONE_MS=)
    [ "${arm:-0}" -gt 0 ] && m3_armed=$((m3_armed+1))
done
echo "  armes indevidos: ${m3_armed}/${M}  (tem de ser 0 -- se >0, ha forja)"

# --- braco NATURAL: produtor por tempo real do stream -----------------------
echo; echo "=== NATURAL: PS3_MOVIE_EOS=1 PS3_MOVIE_DONE_MS=auto, ${M} corridas ==="
nat_st3=0; nat_arm=0; nat_wad=0; nat_done=0
for i in $(seq 1 "$M"); do
    read -r st arm hit lgl perm done4274 < <(run_case natural PS3_MOVIE_EOS=1 PS3_MOVIE_DONE_MS=auto)
    [ "${st:-0}" -ge 3 ] && nat_st3=$((nat_st3+1))
    [ "${arm:-0}" -gt 0 ] && nat_arm=$((nat_arm+1))
    [ "${done4274:-0}" -gt 0 ] && nat_done=$((nat_done+1))
    { [ "${lgl:-0}" -gt 0 ] || [ "${perm:-0}" -gt 0 ]; } && nat_wad=$((nat_wad+1))
done
echo "  002B4274 DONE:   ${nat_done}/${M}  (poll viu [op+0x90] != 0 -- aceite da Task 8)"
echo "  st620>=3:        ${nat_st3}/${M}"
echo "  EOS armado:      ${nat_arm}/${M}"
echo "  WAD aberto:      ${nat_wad}/${M}  (stretch -- so conta se realmente visto)"

echo; echo "=== veredicto (etiquetas honestas) ==="
FAIL=0
# forja-segura: barra dura, ambiente-independente. Reprova de verdade.
if [ "$m3_armed" -eq 0 ]; then echo "  [PROVADO]   forja-segura: nada arma sem produtor"; else
    echo "  [FALHA]     forja-segura: ${m3_armed} armes indevidos"; FAIL=1; fi
# progresso da FSM: gated por ambiente. Nao reprova sob swap -- reporta o regime.
if [ "$nat_st3" -ge 4 ]; then echo "  [PROVADO]   FSM progride: st620>=3 em ${nat_st3}/${M}"
elif [ "${SWAP_USED:-0}" -ge 4000 ]; then
    echo "  [BLOQ-AMBIENTE] FSM presa em 1 (swap ${SWAP_USED} MB) -- alivie a memoria e repita"
else
    echo "  [FALHA]     FSM nao progride e o host nao esta em swap: ${nat_st3}/${M}"; FAIL=1; fi
[ "$nat_wad" -ge 1 ] && echo "  [PROVADO]   WAD aberto em ${nat_wad}/${M}" \
                      || echo "  [PENDENTE]  WAD nao abre -- wall a jusante (st620 3->0 idle, nao 3->5->11)"

exit "$FAIL"
