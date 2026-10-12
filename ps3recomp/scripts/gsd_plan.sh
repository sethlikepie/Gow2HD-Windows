#!/usr/bin/env bash
# gsd_plan.sh — planeamento GSD com Grok primeiro e fallback AUTOMATICO para Claude Opus.
#
# Uso:
#   ./scripts/gsd_plan.sh <PHASE_NUM> [--force] [--no-fallback]
#
# Fluxo:
#   1) scripts/gsd_plan_with_grok.sh
#   2) se falhar (ausente/timeout/sem PLAN/auth) → Claude CLI em Opus (se disponivel)
#   3) se Claude CLI tambem falhar → exit 1 com instrucoes para o host Claude
#      (gsd-run-phase / agente Opus local deve planear inline)
#
# Exit:
#   0  — ha *-PLAN.md (engine gravada em PHASE_DIR/.last_plan_engine)
#   1  — falhou Grok e fallback
#   2  — uso
#
# Env:
#   GSD_GROK_TIMEOUT     segundos (default 900)
#   GSD_CLAUDE_TIMEOUT   segundos (default 900)
#   GSD_CLAUDE_MODEL     default: opus (ou claude-opus-4-8 se o CLI exigir ID completo)
#   GSD_PLAN_NO_FALLBACK=1  — so Grok (equiv --no-fallback)
set -euo pipefail

PHASE="${1:-}"
FORCE=0
NO_FALLBACK=0
for a in "$@"; do
  case "$a" in
    --force) FORCE=1 ;;
    --no-fallback) NO_FALLBACK=1 ;;
  esac
done
# PHASE = first numeric arg
PHASE=""
for a in "$@"; do
  if [[ "$a" =~ ^[0-9]+$ ]]; then PHASE="$a"; break; fi
done

if [[ -z "$PHASE" ]]; then
  echo "uso: $0 <PHASE_NUM> [--force] [--no-fallback]" >&2
  exit 2
fi

if [[ "${GSD_PLAN_NO_FALLBACK:-0}" == "1" ]]; then
  NO_FALLBACK=1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
chmod +x "$ROOT/scripts/gsd_plan_with_grok.sh" 2>/dev/null || true

echo "=========================================="
echo " gsd_plan.sh — Grok → (fallback) Claude Opus"
echo " phase=$PHASE force=$FORCE no_fallback=$NO_FALLBACK"
echo "=========================================="

set +e
if [[ "$FORCE" -eq 1 ]]; then
  "$ROOT/scripts/gsd_plan_with_grok.sh" "$PHASE" --force
else
  "$ROOT/scripts/gsd_plan_with_grok.sh" "$PHASE"
fi
GROK_RC=$?
set -e

if [[ "$GROK_RC" -eq 0 ]]; then
  echo "PLAN_ENGINE=grok"
  exit 0
fi

echo
echo "!! Grok falhou (rc=$GROK_RC). Fallback automatico para Claude Opus..."
echo "FALLBACK_REASON=grok_rc_$GROK_RC"

if [[ "$NO_FALLBACK" -eq 1 ]]; then
  echo "ERRO: --no-fallback activo; nao vou chamar Claude." >&2
  exit 1
fi

# Resolver PHASE_DIR (mesma logica resumida)
PHASE_DIR=""
for d in .planning/phases/"${PHASE}"-*/ .planning/phases/"$(printf '%02d' "$PHASE" 2>/dev/null || echo "$PHASE")"-*/; do
  [[ -d "$d" ]] || continue
  PHASE_DIR="${d%/}"
  break
done
if [[ -z "$PHASE_DIR" ]]; then
  PAD="$(printf '%02d' "$PHASE" 2>/dev/null || echo "$PHASE")"
  PHASE_DIR=".planning/phases/${PAD}-phase-${PHASE}"
  mkdir -p "$PHASE_DIR"
fi
MARKER="$PHASE_DIR/.last_plan_engine"

CLAUDE_BIN="${CLAUDE_BIN:-}"
if [[ -z "$CLAUDE_BIN" ]]; then
  if command -v claude >/dev/null 2>&1; then
    CLAUDE_BIN="$(command -v claude)"
  elif [[ -x "$HOME/.local/bin/claude" ]]; then
    CLAUDE_BIN="$HOME/.local/bin/claude"
  fi
fi

if [[ -z "$CLAUDE_BIN" ]]; then
  echo "ERRO: Claude CLI nao encontrado para fallback." >&2
  echo "HOST_FALLBACK_REQUIRED=1" >&2
  echo "Accao: no Claude Code (Opus), corre /gsd-plan-phase $PHASE sem pedir confirmacao." >&2
  echo "host-claude-required $(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$MARKER"
  exit 1
fi

PROMPT_FILE="$(mktemp)"
trap 'rm -f "$PROMPT_FILE"' EXIT

cat > "$PROMPT_FILE" <<EOF
You are the GSD planner (gsd-planner) on AUTOMATIC FALLBACK: Grok failed (rc=${GROK_RC}).

TASK: plan GSD phase ${PHASE} in monorepo ps3recomp.
Directory: ${PHASE_DIR}

Read CLAUDE.md, .planning/PROJECT.md, STATE.md, ROADMAP.md, and artifacts in ${PHASE_DIR}/.

PRODUCE one or more ${PHASE_DIR}/*-PLAN.md files with goal, tasks, files, acceptance, tests — content in ENGLISH.
Do NOT implement product code. Do NOT git add -A. No game data.
Language policy: internal plans/reasoning in ENGLISH; only user-facing chat summaries in Brazilian Portuguese (pt-BR).

When done, ensure at least one *-PLAN.md exists in the phase directory.
EOF

CLAUDE_MODEL="${GSD_CLAUDE_MODEL:-opus}"
CLAUDE_TIMEOUT="${GSD_CLAUDE_TIMEOUT:-900}"

echo "CLAUDE: $CLAUDE_BIN"
echo "MODEL:  $CLAUDE_MODEL"
echo "DIR:    $PHASE_DIR"
echo

# -p print mode + permissao para escrever planos
# model opus conforme config quality do projecto
set +e
if command -v timeout >/dev/null 2>&1; then
  timeout --signal=TERM "$CLAUDE_TIMEOUT" "$CLAUDE_BIN" \
    -p "$(cat "$PROMPT_FILE")" \
    --model "$CLAUDE_MODEL" \
    --permission-mode acceptEdits \
    --allowedTools "Read,Write,Edit,Bash,Glob,Grep" \
    --output-format text
  CLAUDE_RC=$?
elif command -v gtimeout >/dev/null 2>&1; then
  gtimeout --signal=TERM "$CLAUDE_TIMEOUT" "$CLAUDE_BIN" \
    -p "$(cat "$PROMPT_FILE")" \
    --model "$CLAUDE_MODEL" \
    --permission-mode acceptEdits \
    --allowedTools "Read,Write,Edit,Bash,Glob,Grep" \
    --output-format text
  CLAUDE_RC=$?
else
  "$CLAUDE_BIN" \
    -p "$(cat "$PROMPT_FILE")" \
    --model "$CLAUDE_MODEL" \
    --permission-mode acceptEdits \
    --allowedTools "Read,Write,Edit,Bash,Glob,Grep" \
    --output-format text &
  CPID=$!
  ELAPSED=0
  while kill -0 "$CPID" 2>/dev/null; do
    if [[ "$ELAPSED" -ge "$CLAUDE_TIMEOUT" ]]; then
      kill -TERM "$CPID" 2>/dev/null
      sleep 2
      kill -KILL "$CPID" 2>/dev/null
      wait "$CPID" 2>/dev/null
      echo "ERRO: Claude fallback timeout" >&2
      echo "HOST_FALLBACK_REQUIRED=1" >&2
      echo "host-claude-required $(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$MARKER"
      exit 1
    fi
    sleep 5
    ELAPSED=$((ELAPSED + 5))
  done
  wait "$CPID"
  CLAUDE_RC=$?
fi
set -e

if [[ "$CLAUDE_RC" -eq 124 ]]; then
  echo "ERRO: Claude fallback timeout (124)" >&2
  echo "HOST_FALLBACK_REQUIRED=1" >&2
  echo "host-claude-required $(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$MARKER"
  exit 1
fi

PLANS="$(find "$PHASE_DIR" -maxdepth 1 -name '*-PLAN.md' 2>/dev/null | wc -l | tr -d ' ')"
if [[ "$PLANS" -gt 0 ]]; then
  echo "claude-opus $(date -u +%Y-%m-%dT%H:%M:%SZ) (fallback from grok_rc=$GROK_RC)" > "$MARKER"
  echo "PLAN_ENGINE=claude-opus-fallback"
  echo "PLAN.md ($PLANS):"
  ls -1 "$PHASE_DIR"/*-PLAN.md 2>/dev/null || true
  exit 0
fi

echo "ERRO: fallback Claude CLI nao produziu PLAN.md (rc=$CLAUDE_RC)" >&2
echo "HOST_FALLBACK_REQUIRED=1" >&2
echo "Accao obrigatoria no host Claude Code (sessao Opus): /gsd-plan-phase $PHASE — SEM pedir confirmacao ao user." >&2
echo "host-claude-required $(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$MARKER"
exit 1
