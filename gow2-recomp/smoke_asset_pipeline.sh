#!/usr/bin/env bash
# smoke_asset_pipeline.sh — smoke de METRICAS DE RAIZ do pipeline de asset
# primario (Plano 3, Task 0). NAO altera smoke_intro_to_rsx.sh.
#
# Objetivo: nao apenas confirmar que R_PermA ABRE (isso ja esta provado pelo
# smoke_intro_to_rsx.sh), mas medir se o jogo CONSOME o conteudo do slot —
# via contadores de pread/bytes_read por slot recem-adicionados em
# ps3recomp/libs/video/movie_hle.c (stats no close + trace periodico gated
# por PS3_TRACE_ASSET=1, pois a hipotese e que o jogo NUNCA fecha o slot).
#
# Greps de RAIZ (nao de sintoma):
#   - r_perm_open      : slot do R_PermA abriu (sintoma ja conhecido, so p/ contexto)
#   - wad_pread_stats   : linhas "[movieio] ... stats ... R_PermA" (close OU trace
#                          periodico) — aqui vivem os numeros REAIS de preads/bytes_read
#   - shadersrc_n_neg   : [SHADERSRC] ... N=-1 (probe emite "nenhum shader" —
#                          sinal de que o jogo pediu lookup de shader mas nao achou nada)
#   - shadersrc_n_ok    : [SHADERSRC] ... N=<0..> (N>=0, contagem real de shaders
#                          encontrados — regex exclui N=-1 de proposito)
#   - shreg_register    : [SHREG] register (shaders efetivamente registrados)
#   - shreg_call        : [SHREG] call (uso de shader registrado — esperado 0 por ora)
#   - hostinfl          : [HOSTINFL] (dearchive/inflate do WAD via SPU host-side)
#   - invalid           : "Invalid shader combination" (parede D conhecida)
#   - flips             : SetFlipCommand (frames apresentados)
#   - crash             : access violation / 0xC0000005
#
# BASELINE ESPERADO (hipotese "open sem consumo", documentar apos 1a rodada real):
#   r_perm_open   >= 1   (R_PermA abre, ja provado no Plano 2)
#   wad_pread_stats: bytes_read do R_PermA ESPERADO proximo de 0 ou so cabecalho
#                     (muito menor que o size~20MB do arquivo) -- se isso se
#                     confirmar, classifica como "open sem stream" (linha 1 da
#                     tabela abaixo).
#   shadersrc_n_neg = 18  (contagem conhecida de N=-1 do probe SHADERSRC)
#   shadersrc_n_ok  = 0   (nenhum shader real encontrado ainda)
#   shreg_register  = 0..poucos (nao e o foco desta task)
#   shreg_call      = 0   (esperado — nada consome shader registrado ainda)
#   hostinfl        ~ 0 para R_PermA especificamente (spu1 dispara p/ outros
#                     WADs no smoke_intro_to_rsx.sh, mas nao necessariamente p/
#                     R_PermA — ver Plano 2 nota [C])
#   invalid         alto (parede D conhecida, "Invalid shader combination")
#
# TABELA DE CLASSIFICACAO (Plano 3, Task 1 Step 2):
#   bytes_read ~ 0                    -> "open sem stream": o jogo abre o
#                                         handle mas nunca chama pread/read_cur.
#   bytes_read ~ size, sem HOSTINFL    -> "stream raw sem dearchive": o jogo le
#                                         o arquivo cru mas nao aciona o
#                                         pipeline SPU-zlib de descompressao.
#   bytes_read grande + HOSTINFL        -> "bug no loader": o dado e lido E
#                                         descomprimido mas algo na cadeia
#                                         depois (shader/textura) descarta o
#                                         resultado.
#
# Uso: ./smoke_asset_pipeline.sh [timeout_segundos]   (default 150)
set +e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$SCRIPT_DIR/recomp_mid_v2"
LOG="$RUN_DIR/smoke_asset_pipeline.log"
TIMEOUT_S="${1:-150}"

if [ ! -x "$RUN_DIR/boot_v2_new.exe" ]; then
  echo "FALHA: nao encontrei $RUN_DIR/boot_v2_new.exe"
  exit 1
fi
if [ ! -f "$SCRIPT_DIR/EBOOT.ELF" ]; then
  echo "FALHA: nao encontrei $SCRIPT_DIR/EBOOT.ELF"
  exit 1
fi

cd "$RUN_DIR" || exit 1
taskkill //F //IM boot_v2_new.exe >/dev/null 2>&1
sleep 1
rm -f "$LOG"

echo "=== smoke_asset_pipeline.sh: timeout=${TIMEOUT_S}s ==="

# Recipe validado (identico ao smoke_intro_to_rsx.sh) + PS3_TRACE_ASSET=1
# (novo: liga o dump periodico de slots abertos no movie_io, ~5s).
export PS3_VFS_ROOT="$SCRIPT_DIR/extracted/USRDIR"
export PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64
export PS3_CELLSYS_REORDER=1 PS3_FIX_TBLSIZE=1
export PS3_MOVIE_IO=1 PS3_MOVIE_CACHE="../movie_cache" PS3_MOVIE_EOS=1
export PS3_VDEC_FORCE_SEQDONE_MS=8000
export PS3_VDEC_ASYNC=1
export PS3_SPU1=1
export PS3_RSX_BACKEND=d3d12 PS3_RSX_FIFO=1
export PS3_TRACE_RSX_SHADERS=1
export PS3_PAD_AUTOSTART=1
export PS3_TRACE_ASSET=1
unset PS3_SPU_ALL PS3_NOMOVIES

timeout -k 5 "$TIMEOUT_S" ./boot_v2_new.exe ../EBOOT.ELF > "$LOG" 2>&1
BOOT_EXIT=$?

taskkill //F //IM boot_v2_new.exe >/dev/null 2>&1

echo "=== exit do boot=$BOOT_EXIT (124 = timeout, NORMAL — o boot roda em loop de render ate ser cortado) ==="

LINES=$(wc -l < "$LOG" 2>/dev/null || echo 0)
echo "=== log: $LOG (linhas=$LINES) ==="
if [ "$LINES" -eq 0 ]; then
  echo "FALHA: log vazio — o processo nao produziu saida (checar EBOOT.ELF/PS3_VFS_ROOT/permissoes)."
  exit 1
fi

echo ""
echo "=== [raiz] R_PermA: abertura vs consumo real ==="
RPERM_OPEN=$(grep -ac "open 'R_PermA'" "$LOG")
echo "r_perm_open=$RPERM_OPEN"
echo "-- linhas de stats (close ou trace periodico) para R_PermA --"
grep -a 'stats.*R_PermA' "$LOG"
WAD_PREAD_STATS=$(grep -ac 'stats.*R_PermA' "$LOG")
echo "wad_pread_stats_lines=$WAD_PREAD_STATS"

echo ""
echo "=== [raiz] SHADERSRC probe (N=-1 = nenhum shader encontrado) ==="
SHADERSRC_N_NEG=$(grep -acE '\[SHADERSRC\].*N=-1' "$LOG")
SHADERSRC_N_OK=$(grep -acE '\[SHADERSRC\].*N=[0-9]' "$LOG")
echo "shadersrc_n_neg=$SHADERSRC_N_NEG shadersrc_n_ok=$SHADERSRC_N_OK"
if [ "$SHADERSRC_N_NEG" -eq 0 ] && [ "$SHADERSRC_N_OK" -eq 0 ]; then
  echo "shadersrc: n/a (probe nao presente neste build — ver nota no brief; provavelmente build antigo/chunk local gitignored)"
fi

echo ""
echo "=== [raiz] SHREG (registro/uso de shader) ==="
SHREG_REGISTER=$(grep -ac '\[SHREG\].*register' "$LOG")
SHREG_CALL=$(grep -ac '\[SHREG\].*call' "$LOG")
echo "shreg_register=$SHREG_REGISTER shreg_call=$SHREG_CALL"

echo ""
echo "=== [raiz] HOSTINFL (dearchive/inflate SPU host-side) ==="
HOSTINFL=$(grep -ac '\[HOSTINFL\]' "$LOG")
echo "hostinfl=$HOSTINFL"

echo ""
echo "=== resumo geral (sintomas conhecidos, para contexto) ==="
INVALID=$(grep -ac 'Invalid shader combination' "$LOG")
FLIPS=$(grep -ac SetFlipCommand "$LOG")
CRASH=$(grep -aciE "access violation|0xC0000005" "$LOG")
echo "invalid_shader_combination=$INVALID flips=$FLIPS crash_markers=$CRASH"
echo "log completo em: $LOG"
echo "=== FIM smoke_asset_pipeline.sh ==="
