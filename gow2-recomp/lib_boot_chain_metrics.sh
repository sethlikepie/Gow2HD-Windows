#!/usr/bin/env bash
# lib_boot_chain_metrics.sh -- nucleo partilhado da medicao da cadeia de boot
# (Fase 7, GATE-01/GATE-02, marco v1.1). Extraido de bisect_regression.sh
# (Fase 6) SEM duplicar greps: bisect_regression.sh e smoke_chain_gate.sh
# fazem `source` deste ficheiro em vez de definirem as suas proprias copias.
#
# Contem, verbatim do bisect_regression.sh original:
#   arm_menu_fast_recipe()  -- o bloco de env da recipe menu-fast Metal
#   measure_one()           -- lanca em fundo, poll por segundo, mata SEMPRE
#                              por PID (TERM -> espera 1s -> -9, NUNCA pkill -f)
#   extract_counts()        -- 6 numeros via grep -c (log_lines/startseq/
#                              thr_end/r_perma/nopic)
#   classify()               -- STRUCTURAL_FAIL/OK/REGRESSAO por log_lines/thr_end
#
# E acrescenta (Fase 7, Plano 07-01):
#   extract_st620()          -- o mesmo grep/awk de smoke_relift_equiv.sh para
#                               o st620 maximo (primeiro elo da cadeia)
#
# NUNCA reimplementar estes greps noutro sitio -- e o ponto desta fase.

# ---- arm_menu_fast_recipe: exporta o bloco de env da recipe menu-fast Metal --
# Chamar SO depois de "set -a; . env_gow2.sh; set +a" (o mesmo pre-requisito
# de bisect_regression.sh original).
arm_menu_fast_recipe() {
  export PS3_VDEC_ASYNC=1
  export PS3_VDEC_FORCE_SEQDONE_MS=1500
  export PS3_MOVIE_EOS=1 PS3_MOVIE_HLE=1 PS3_MOVIE_IO=1
  export PS3_BOOT_LOGO_MS=300
  export PS3_AUTO_LOAD_RUN=1
  export PS3_PAD_AUTOSTART=1
  export PS3_TYPE15_UNSTICK=1
  export PS3_SPU1=1 PS3_SPU4=1 PS3_SPU5=1
  export PS3_RSX_BACKEND=metal
  export PS3_RSX_FIFO=1
  export PS3_FULLSCREEN=0
  export PS3_PERF_FSM=1
}

# ---- measure_one BIN_PATH LOG_PATH TIMEOUT --------------------------------
# Lanca BIN_PATH EBOOT.ELF em fundo, guarda o PID, poll a cada segundo com
# kill -0 ate TIMEOUT (default 90s), e no fim manda kill -TERM, espera 1s,
# kill -9, e wait. Nunca pkill -f.
measure_one() {
  local bin_path="$1" log_path="$2" timeout="${3:-90}"

  "$bin_path" EBOOT.ELF > "$log_path" 2>&1 &
  local bpid=$!

  local i
  for ((i = 0; i < timeout; i++)); do
    kill -0 "$bpid" 2>/dev/null || break
    sleep 1
  done

  if kill -0 "$bpid" 2>/dev/null; then
    kill -TERM "$bpid" 2>/dev/null
    sleep 1
    kill -0 "$bpid" 2>/dev/null && kill -9 "$bpid" 2>/dev/null
  fi
  wait "$bpid" 2>/dev/null
}

# ---- extract_counts LOG_PATH -> imprime 6 numeros separados por espaco ----
# grep -c ja imprime 0 quando nao ha match (exit 1, stdout "0"); capturar
# so' com VAR=$(grep -c ...), sem encadear "|| echo 0" a seguir (isso
# duplicaria o 0).
#
# CORRECCAO 2026-08-01 -- o elo AUTO_LOAD media uma string inexistente
# ----------------------------------------------------------------------
# thr_end procurava 'thr_auto_load() end'. Essa string NAO EXISTE em nenhum
# dos 26 binarios guardados (`strings -a boot_gow2* | grep -c thr_auto_load`
# = 0 em todos, incluindo os de referencia pre_v3/rdy0_ref que este gate foi
# escrito para medir), nem em nenhum ficheiro fonte. `thr_auto_load` e' o
# nome de uma FUNCAO HOST citada num comentario sobre um SIGSEGV
# (sys_ppu_thread.c) -- nunca foi um marcador impresso.
#
# Consequencia: thr_end era sempre 0, classify() devolvia sempre REGRESSAO e
# elo_stopped era sempre "AUTO_LOAD", medisse o binario o que medisse. Um
# instrumento que nao pode passar nao distingue progresso de regressao.
#
# E o elo colapsava dois estados MUITO diferentes num so' numero:
#   - a thread nunca ser criada          (o que de facto acontece hoje)
#   - a thread ser criada e nao terminar
# Medido: `sys_ppu_thread_create name="AUTO_LOAD"` nao aparece uma unica vez
# -- as threads criadas sao BPETrophyInitThread, fios mediathread, fios
# scheduler, snd_stream_service_thread e syn_tick_timer_thread. A pergunta
# certa nao e' "porque nao termina", e' "porque nunca e' criada".
#
# Agora thr_end mede o marcador REAL de fim de thread (acrescentado em
# ps3recomp `[SYS] sys_ppu_thread end name="AUTO_LOAD"`, simetrico ao de
# create) e thr_created e' devolvido a' parte para os dois estados serem
# distinguiveis no relatorio.
#
# AVISO MAIOR (medido no mesmo dia, depois do acima)
# --------------------------------------------------
# **O elo AUTO_LOAD nao pertence a esta cadeia.** A thread so' pode ser criada
# por `func_00146CB8`, chamada de dois sitios (0x000BB1E4 e 0x000BBD70). Ambos
# ficam a jusante de:
#
#     0x002B2E14   bl 0x00242C94        <- o LOOP PRINCIPAL do jogo
#     0x002B2E18   (codigo pos-loop, onde a cadeia do AUTO_LOAD comeca)
#
# e `func_00242C94` so' retorna em REQUEST_EXITGAME. Medido numa corrida
# saudavel (`PS3_TRACE_2B2E04` + `PS3_TRACE_ALCHAIN`, com CONTROLO):
#
#     SetFlipCommand ................ 2619   (o loop corre e desenha)
#     linhas apos o loop principal ..    0   (nunca retorna)
#     degraus da cadeia AUTO_LOAD ...    0
#     sonda de CONTROLO .............    2   (o instrumento funciona)
#
# Ou seja: **"AUTO_LOAD nunca criada" e' o comportamento CORRECTO de um jogo
# que ainda esta a correr.** Nao e' uma parede; e' a prova de que o loop nao
# terminou. Um binario que "passasse" este elo teria saido do jogo.
#
# Nao mexo aqui na ordem dos elos porque isso e' uma decisao de desenho do
# marco (v1.1 chama-se "o boot volta a chegar ao AUTO_LOAD"), mas quem vier a
# seguir NAO deve perseguir este elo: o caminho para o menu passa pelo despacho
# indirecto do loop principal (`*(r30+0x460C)` em func_00242C94), nao por aqui.
extract_counts() {
  local log_path="$1"
  local log_lines startseq thr_end thr_created r_perma nopic

  log_lines=$(wc -l < "$log_path" 2>/dev/null | tr -d ' ')
  log_lines=${log_lines:-0}
  startseq=$(grep -c 'StartSeq(handle=' "$log_path" 2>/dev/null)
  thr_end=$(grep -c 'sys_ppu_thread end name="AUTO_LOAD"' "$log_path" 2>/dev/null)
  thr_created=$(grep -c 'sys_ppu_thread_create name="AUTO_LOAD"' "$log_path" 2>/dev/null)
  r_perma=$(grep -c 'R_PermA full flagged' "$log_path" 2>/dev/null)
  nopic=$(grep -c 'REPLAY-NOPIC' "$log_path" 2>/dev/null)

  echo "$log_lines $startseq $thr_end $r_perma $nopic $thr_created"
}

# ---- extract_st620 LOG_PATH -> imprime o st620 maximo (0 se ausente) ------
# Mesmo grep/awk de smoke_relift_equiv.sh (que por sua vez copia
# smoke_m0_baseline.sh): formato real "[MOVIEFSM] st620 <de> -> <para>", NAO
# "st620=N"; 4294967295 (0xFFFFFFFF) e o sem-estado inicial do sampler,
# nunca um valor -- filtrado fora antes do sort.
extract_st620() {
  local log_path="$1"
  local st
  st=$(grep -oE '\[MOVIEFSM\] st620 [0-9]+ -> [0-9]+' "$log_path" 2>/dev/null \
        | awk '{print $NF}' | grep -v '^4294967295$' | sort -n | tail -1)
  echo "${st:-0}"
}

# ---- classify LOG_LINES THR_END -> imprime a classe -----------------------
# log_lines < 200 -> STRUCTURAL_FAIL (a familia dos ~55-101 linhas que o
# CLAUDE.md documenta como falha estrutural -- nunca um binario que correu
# a recipe ate ao fim; todas as corridas saudaveis desta sessao tem
# >=3161 linhas). Senao thr_end>=1 -> OK; senao REGRESSAO.
classify() {
  local log_lines="$1" thr_end="$2"
  if [ "$log_lines" -lt 200 ]; then
    echo "STRUCTURAL_FAIL"
  elif [ "$thr_end" -ge 1 ]; then
    echo "OK"
  else
    echo "REGRESSAO"
  fi
}
