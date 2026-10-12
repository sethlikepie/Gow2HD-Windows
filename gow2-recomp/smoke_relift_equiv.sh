#!/usr/bin/env bash
# smoke_relift_equiv.sh -- entrega do criterio 1 da Fase 5 (REQ-RDY0-6):
# ./smoke_relift_equiv.sh recomp_macos_v3 6 <tsv> tem de sair 0 quando
# st620 max >= 3 em >= 4 de 6 execucoes.
#
# Base literal: smoke_m0_baseline.sh (6 runs, protocolo G6, grep do st620 ja
# validado -- formato real "[MOVIEFSM] st620 <de> -> <para>", NAO "st620=N";
# 4294967295 (0xFFFFFFFF) e o sem-estado inicial do sampler, nunca um valor).
# Este script nao copia smoke_m0_baseline.sh tal-e-qual -- acrescenta:
#   - build automatico do binario a partir do lift candidato
#     (FORCE_REBUILD_LIFT=1, para nunca testar um binario desactualizado
#     face a um patch recem-aplicado ao lift, T-05-01);
#   - saida em TSV maquina-legivel, uma linha por corrida;
#   - um portao explicito de >=4-de-6 (THRESHOLD = (RUNS*2+2)/3);
#   - a distincao MEDIDA entre flakiness de timing e regressao real
#     (T-05-03): uma corrida presa em st620<3 e classificada, nao
#     descartada -- FLAKE_SUSPEITA quando o log tem >=1000 linhas E contem
#     "allocate(0x7D00000)" (log saudavel-mas-lento historico tem
#     ~3180-3236 linhas e faz esse allocate; log de regressao real medido
#     tem ~55 linhas e nao faz), senao REGRESSAO_SUSPEITA. O portao final
#     continua a contar SO' st620>=3 literal -- nunca reclassifica uma
#     falha como sucesso.
#
# Nota de reformulacao (D-5.2, 05-CONTEXT.md): o ROADMAP.md/REQUIREMENTS.md
# ainda citam function_table_count==56072 e "56223 lifted functions
# registered" -- medido na sessao de planeamento desta fase: esses numeros
# sao do lift ANTIGO escrito a mao. O lift regenerado (recomp_macos_v2 de
# producao e o candidato recomp_macos_v3) tem function_table_count=51917
# nos dois. Este script NAO gateia por 56072/56223 -- isso reintroduziria o
# erro de limiar absoluto ja medido duas vezes nas Fases 3/4. O Plano 05-03
# reformula esse criterio por delta contra um baseline congelado, consumindo
# o TSV e o modo --bin desta entrega.
#
# Uso:
#   ./smoke_relift_equiv.sh [LIFT_DIR] [RUNS] [TSV]   (contrato literal do
#                                                       ROADMAP; default
#                                                       recomp_macos_v3/6)
#   ./smoke_relift_equiv.sh --bin BIN_PATH [RUNS] [TSV]  (sem rebuild --
#                                                       mede um binario ja
#                                                       compilado, ex. de
#                                                       producao)
#
# Kill SEMPRE por PID (TERM depois -9). NUNCA pkill -f boot_gow2 (D-5.5,
# ja derrubou a maquina do operador com load 75). Logs em /tmp, fora dos
# dois repositorios.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE" || exit 1

# ---- parsing de argumentos (duas formas) ------------------------------------
BIN_OVERRIDE=""
LIFT_REL=""
if [ "${1:-}" = "--bin" ]; then
  BIN_OVERRIDE="${2:-}"
  if [ -z "$BIN_OVERRIDE" ]; then
    echo "ERRO: --bin precisa de um caminho de binario" >&2
    exit 2
  fi
  shift 2
else
  LIFT_REL="${1:-recomp_macos_v3}"
  [ -n "${1:-}" ] && shift
fi
RUNS="${1:-6}"
TSV="${2:-/tmp/smoke_relift_$(basename "${LIFT_REL:-$BIN_OVERRIDE}")_$(date +%Y%m%d_%H%M%S).tsv}"

# ---- resolucao do binario ----------------------------------------------------
if [ -n "$BIN_OVERRIDE" ]; then
  BIN="$BIN_OVERRIDE"
  if [ ! -x "$BIN" ]; then
    echo "ERRO: binario inexistente ou nao executavel: $BIN" >&2
    exit 2
  fi
  TAG="$(basename "$BIN")"
  LIFT=""
else
  LIFT="$HERE/${LIFT_REL#./}"
  if [ ! -d "$LIFT" ]; then
    echo "ERRO: dir de lift nao existe: $LIFT" >&2
    exit 2
  fi
  TAG="$(basename "$LIFT")"
  BIN="$HERE/boot_gow2_${TAG#recomp_macos_}"
fi

# ---- build (so em modo lift; FORCE_REBUILD_LIFT=1 sempre, T-05-01) ---------
if [ -n "$LIFT" ]; then
  echo "=== a construir $BIN a partir de $LIFT (FORCE_REBUILD_LIFT=1) ==="
  OUT="$BIN" FORCE_REBUILD_LIFT=1 "$HERE/build_macos.sh" "$LIFT"
  build_rc=$?
  if [ "$build_rc" -ne 0 ]; then
    echo "ERRO: build_macos.sh falhou (rc=$build_rc) para $LIFT -- nao vou correr um binario desactualizado" >&2
    exit "$build_rc"
  fi
fi

# ---- TSV: cabecalho (trunca se ja existir, mesmo padrao de
# apply_all_patches.sh:91-93 / games/gow2/apply_all_patches.sh:91-93) --------
if [ -n "$TSV" ]; then
  printf 'run\tst620_max\tlifted_functions\tmodules\timports\tlog_lines\thas_alloc_7d00000\tclassification\n' > "$TSV"
fi

# ---- loop de N corridas (Task 2) --------------------------------------------
OK_COUNT=0
FLAKE_COUNT=0
REGR_COUNT=0
for r in $(seq 1 "$RUNS"); do
  LOG="/tmp/smoke_relift_${TAG}_run${r}.log"
  (
    set -a; . "$HERE/env_gow2.sh"; set +a
    unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
    # Protocolo G6 (D-5.5, criterio 3): PS3_NO_RSX=1, PS3_RSX_BACKEND=trace
    # (inerte quando PS3_NO_RSX=1 -- pick_backend() em boot_macos.cpp:211-212
    # retorna antes de ler esta variavel -- mas o ROADMAP exige-a literalmente),
    # PS3_ENGINE_ROOT explicito.
    export PS3_NO_RSX=1 PS3_RSX_BACKEND=trace PS3_PERF_FSM=1 PS3_MOVIE_EOS=0
    export PS3_ENGINE_ROOT="${PS3_ENGINE_ROOT:-$HERE/../ps3recomp}"
    unset PS3_MOVIE_DONE_MS PS3_VDEC_FORCE_SEQDONE_MS
    exec "$BIN" EBOOT.ELF
  ) > "$LOG" 2>&1 &
  BPID=$!
  sleep 25
  kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null; wait $BPID 2>/dev/null

  # ---- extracao do log -------------------------------------------------
  # Mesmo grep de smoke_m0_baseline.sh:27-28. Formato real:
  # "[MOVIEFSM] st620 <de> -> <para>". O estado e o lado DIREITO.
  # 4294967295 (0xFFFFFFFF) e o "sem estado" inicial do sampler, nao um valor.
  ST=$(grep -oE '\[MOVIEFSM\] st620 [0-9]+ -> [0-9]+' "$LOG" 2>/dev/null \
        | awk '{print $NF}' | grep -v '^4294967295$' | sort -n | tail -1)
  ST=${ST:-0}
  LOGLINES=$(wc -l < "$LOG" | tr -d ' ')
  if grep -q 'allocate(0x7D00000)' "$LOG" 2>/dev/null; then HASALLOC=1; else HASALLOC=0; fi
  LIFTED=$(grep -oE '\[boot\] [0-9]+ lifted functions' "$LOG" 2>/dev/null | grep -oE '[0-9]+' | head -1)
  IMPLINE=$(grep -oE '\[imp\] [0-9]+ modules, [0-9]+ imports routed to HLE' "$LOG" 2>/dev/null | head -1)
  MODULES=$(printf '%s\n' "$IMPLINE" | grep -oE '[0-9]+' | sed -n '1p')
  IMPORTS=$(printf '%s\n' "$IMPLINE" | grep -oE '[0-9]+' | sed -n '2p')

  # ---- classificacao (T-05-03: distinguir, nao descartar) ---------------
  # OK: st620_max>=3. Senao, FLAKE_SUSPEITA quando o log e "grande" (>=1000
  # linhas -- historico saudavel-mas-lento fica em ~3180-3236) E contem o
  # allocate(0x7D00000) (assinatura do heap de 125MB, sinal de que o boot
  # progrediu ate ao ponto de alocar mas ficou preso por timing). Caso
  # contrario REGRESSAO_SUSPEITA (assinatura medida do v3 original preso no
  # sampler, log com ~55 linhas -- morte estrutural precoce).
  if [ "$ST" -ge 3 ] 2>/dev/null; then
    CLASS=OK
    OK_COUNT=$((OK_COUNT + 1))
  elif [ "$LOGLINES" -ge 1000 ] && [ "$HASALLOC" = 1 ]; then
    CLASS=FLAKE_SUSPEITA
    FLAKE_COUNT=$((FLAKE_COUNT + 1))
  else
    CLASS=REGRESSAO_SUSPEITA
    REGR_COUNT=$((REGR_COUNT + 1))
  fi

  printf "run%d  st620_max=%s lifted=%s modules=%s imports=%s log_lines=%s alloc=%s class=%s\n" \
    "$r" "$ST" "${LIFTED:-?}" "${MODULES:-?}" "${IMPORTS:-?}" "$LOGLINES" "$HASALLOC" "$CLASS"

  if [ -n "$TSV" ]; then
    printf '%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$r" "$ST" "${LIFTED:-0}" "${MODULES:-0}" "${IMPORTS:-0}" "$LOGLINES" "$HASALLOC" "$CLASS" >> "$TSV"
  fi
done

# THRESHOLD generaliza ">=4 de 6" para outros N ((RUNS*2+2)/3 aritmetica
# inteira -- RUNS=6 -> (12+2)/3=4). O uso canonico desta fase e' sempre
# RUNS=6/THRESHOLD=4; outros N sao suportados mas nao o caminho principal.
THRESHOLD=$(( (RUNS * 2 + 2) / 3 ))

echo "-----"
printf "st620>=3 em %d de %d (limiar: %d)\n" "$OK_COUNT" "$RUNS" "$THRESHOLD"
if [ "$FLAKE_COUNT" -gt 0 ] || [ "$REGR_COUNT" -gt 0 ]; then
  printf "flake_suspeita=%d regressao_suspeita=%d\n" "$FLAKE_COUNT" "$REGR_COUNT"
fi
echo "orfaos_pos_run: $(pgrep -f "$TAG" | wc -l | tr -d ' ')"

# rc final: 0 se OK_COUNT >= THRESHOLD. NUNCA reclassifica uma corrida
# FLAKE_SUSPEITA/REGRESSAO_SUSPEITA como OK para este calculo -- a distincao
# e' so' informativa/diagnostica (T-05-03); o portao continua literal sobre
# st620>=3.
if [ "$OK_COUNT" -ge "$THRESHOLD" ]; then
  exit 0
else
  exit 1
fi
