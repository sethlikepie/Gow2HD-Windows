#!/usr/bin/env bash
# smoke_intro_to_rsx.sh — smoke E2E unico do Plano 2 (intro FSM -> WADs -> RSX).
# Junta num so recipe tudo que as Tasks 1-5 provaram, EM VIGOR (nao o esqueleto
# original do plano): movie FSM (FORCE + NATURAL/EOS), WAD open (R_LglScA/
# R_PermA), spu1 (EDGE-zlib dispatch), backend RSX d3d12 + trace de shader.
#
# NAO prova "o jogo renderiza" nem "textura funciona". Prova o quanto do
# pipeline sobe e roda ate a parede REAL (ver docs/gow2-recomp-notes.md,
# secao "Paredes A-E"):
#   [A] SmLogo stuck            -> VENCIDA (FSM progride, FORCE e NATURAL/EOS)
#   [B] WADs open                -> VENCIDA (R_LglScA + R_PermA abrem)
#   [C] WAD inflate (spu1)       -> PAREDE: spu1 roda mas o jogo nunca pede
#       o dearchive do R_PermA (pipeline de asset primario nao roda)
#   [D] CGOWShader                -> desacoplada de C, mesma raiz de fundo
#   [E] Pixels de jogo            -> BLOQUEADA por C+D
#
# Uso: ./smoke_intro_to_rsx.sh [timeout_segundos]   (default 150)
set +e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$SCRIPT_DIR/recomp_mid_v2"
LOG="$RUN_DIR/smoke_intro_to_rsx.log"
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

echo "=== smoke_intro_to_rsx.sh: timeout=${TIMEOUT_S}s ==="

# Recipe REAL validado nas Tasks 1-5 (nao o esqueleto original do plano):
#  - VFS/VM/reorder/tblsize: minimo pro boot sair do "vm commit failed" cedo.
#  - MOVIE_IO/MOVIE_CACHE/MOVIE_EOS + VDEC_FORCE_SEQDONE_MS: movie FSM (state
#    3 gate) + fallback FORCE se o EOS natural nao chegar a tempo.
#  - VDEC_ASYNC=1: OBRIGATORIO hoje. O default do runtime virou sync no
#    commit f811ee3 ("reverter default p/ sync (async crashava o backend
#    D3D12)"); o loop que dispara FORCE SEQDONE so roda na thread assincrona
#    -- sem esta var o FORCE nunca dispara e os WADs nunca abrem.
#  - SPU1=1: liga o dispatch do EDGE-zlib (dearch) no caminho intro->WAD.
#  - RSX_BACKEND=d3d12 + RSX_FIFO + TRACE_RSX_SHADERS: liga o backend D3D12
#    real e os contadores/trace de shader ([RSX-SH], set_shader).
#  - PAD_AUTOSTART=1: evita ficar preso esperando input de pad.
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
echo "=== [A/B] movie FSM + WAD open ==="
FORCE=$(grep -ac 'FORCE SEQDONE' "$LOG")
GATE=$(grep -ac 'state-3 gate' "$LOG")
RLGL=$(grep -ac "open 'R_LglScA'" "$LOG")
RPERM=$(grep -ac "open 'R_PermA'" "$LOG")
echo "force_seqdone=$FORCE state3_gate=$GATE r_lgl=$RLGL r_perm=$RPERM"

echo ""
echo "=== [C] spu1 (EDGE-zlib dispatch) ==="
SPU1_MISS=$(grep -ac 'dispatch MISS fp=0x2A5C4E67A14505B8' "$LOG")
SPU1_HIT=$(grep -ac 'dispatch HIT fp=0x2A5C4E67A14505B8' "$LOG")
SPUJOB_CLEAN=$(grep -ac 'spu job returned cleanly' "$LOG")
echo "spu1_miss=$SPU1_MISS spu1_hit=$SPU1_HIT spujob_clean=$SPUJOB_CLEAN"

echo ""
echo "=== [D] RSX shader pipeline / parede CGOWShader ==="
RSXSH=$(grep -ac "RSX-SH" "$LOG")
SETSHADER=$(grep -ac 'set_shader' "$LOG")
INVALID=$(grep -ac 'Invalid shader combination' "$LOG")
echo "rsx_sh=$RSXSH set_shader=$SETSHADER invalid_shader_combination=$INVALID"

echo ""
echo "=== resumo geral ==="
FLIPS=$(grep -ac SetFlipCommand "$LOG")
CRASH=$(grep -aciE "access violation|0xC0000005" "$LOG")
echo "flips=$FLIPS crash_markers=$CRASH"
echo "log completo em: $LOG"
echo "=== FIM smoke_intro_to_rsx.sh ==="
