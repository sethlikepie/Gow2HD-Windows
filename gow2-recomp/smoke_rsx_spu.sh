#!/usr/bin/env bash
# smoke_rsx_spu.sh — smoke unico de integracao: pipeline RSX (shader D3D12) + SPU (spu1
# default; spu2/3 sob PS3_SPU_ALL). Roda o boot_v2_new.exe com o conjunto de env vars REAL
# usado nas Fases A-D (ver docs/gow2-recomp-notes.md e RSX_FRAGMENT_PROGRAM.md no repo
# ps3recomp), por um tempo limitado (timeout), salva o log e roda greps de sanidade.
#
# NAO prova "o jogo renderiza". Prova que o pipeline sobe (device D3D12, cache de shader,
# SPU) sem crashar, ate a parede de conteudo conhecida (o jogo ainda nao emite draws/
# texturas proprios neste ponto do boot — ver ressalvas honestas em gow2-recomp-notes.md).
#
# Uso: ./smoke_rsx_spu.sh [timeout_segundos]   (default 60)

set +e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$SCRIPT_DIR/recomp_mid_v2"
LOG="$RUN_DIR/smoke_e2e.log"
TIMEOUT_S="${1:-60}"

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

echo "=== smoke_rsx_spu.sh: timeout=${TIMEOUT_S}s ==="

# Env vars reais confirmadas nas Fases A-D (Tasks 1-4):
#  - PS3_VFS_ROOT/PS3_VM_*/PS3_CELLSYS_REORDER/PS3_FIX_TBLSIZE: minimo pra o boot sair do
#    "vm commit (RSX local) failed" cedo (ver task-1-report.md).
#  - PS3_SHADER_DEMO+PS3_RSX_FIFO+PS3_RSX_BACKEND=d3d12+PS3_TRACE_RSX_SHADERS+PS3_DUMP_SHADERS:
#    liga o backend D3D12 real e os contadores [RSX-SH] (Fase A/B). Sem PS3_RSX_BACKEND=d3d12
#    o bridge fica em modo sync-only e nada do D3D12/RSX-SH aparece (ver RSX_GRAPHICS.md).
#  - PS3_SPU_ALL: registra spu1/2/3 (Fase D); spu1 e default, spu2/3 sao gate-only.
PS3_VFS_ROOT="$SCRIPT_DIR/extracted/USRDIR" \
PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 PS3_CELLSYS_REORDER=1 PS3_FIX_TBLSIZE=1 \
PS3_SHADER_DEMO=1 PS3_RSX_FIFO=1 PS3_RSX_BACKEND=d3d12 PS3_TRACE_RSX_SHADERS=1 PS3_DUMP_SHADERS=1 \
PS3_SPU_ALL=1 \
timeout "$TIMEOUT_S" ./boot_v2_new.exe ../EBOOT.ELF > "$LOG" 2>&1
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
echo "=== sanidade RSX: pipeline de shader (novo [RSX-SH] Fase A/B, OU [SHREG]/ICGLdrShader pre-existente) ==="
RSXSH=$(grep -ac "RSX-SH" "$LOG")
ICGL=$(grep -ac "ICGLdrShader" "$LOG")
D3DREADY=$(grep -ac "Backend ready" "$LOG")
echo "RSX-SH=$RSXSH ICGLdrShader=$ICGL d3d12_backend_ready=$D3DREADY"
if [ "$RSXSH" -eq 0 ] && [ "$ICGL" -eq 0 ]; then
  echo "AVISO: nenhum marcador de shader encontrado nesta run (RSX-SH nem ICGLdrShader)."
fi

echo ""
echo "=== sanidade SPU: isolacao SEH (Fase D) — crash isolado esperado 0 nesta janela de boot ==="
SPUCRASH=$(grep -ac "SPUCRASH" "$LOG")
# "aborted by SEH" (Windows) ou "aborted by signal" (POSIX): o mesmo evento.
SPUABORT=$(grep -ac "\[SPUJOB\] aborted by" "$LOG")
echo "SPUCRASH=$SPUCRASH aborted_by_SEH=$SPUABORT"

echo ""
echo "=== parede de conteudo conhecida (esperado aparecer — NAO e falha do smoke) ==="
INVALIDSHADER=$(grep -ac "Invalid shader combination" "$LOG")
echo "invalid_shader_combination=$INVALIDSHADER"

echo ""
echo "=== resumo geral ==="
echo "flips=$(grep -ac SetFlipCommand "$LOG")"
echo "crash_markers(access violation/abort)=$(grep -aciE "access violation|0xC0000005" "$LOG")"
echo "log completo em: $LOG"
echo "=== FIM smoke_rsx_spu.sh ==="
