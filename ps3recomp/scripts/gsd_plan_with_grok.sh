#!/usr/bin/env bash
# gsd_plan_with_grok.sh — planeia uma fase GSD com o CLI Grok (headless).
#
# Uso:
#   ./scripts/gsd_plan_with_grok.sh <PHASE_NUM> [--force]
#
# Exit codes:
#   0  — PLAN.md ok (ja existia ou Grok criou)
#   1  — Grok correu mas nao produziu PLAN / falhou semanticamente
#   2  — uso incorrecto
#   3  — grok CLI ausente
#   4  — timeout / Grok nao respondeu a tempo
#   5  — falha de processo Grok (crash, auth, rede)
#
# Preferir o orquestrador com fallback Opus:
#   ./scripts/gsd_plan.sh <PHASE_NUM> [--force]
set -euo pipefail

PHASE="${1:-}"
FORCE=0
if [[ "${2:-}" == "--force" ]] || [[ "${1:-}" == "--force" ]]; then
  FORCE=1
  [[ "${1:-}" == "--force" ]] && PHASE="${2:-}"
fi

if [[ -z "$PHASE" ]] || ! [[ "$PHASE" =~ ^[0-9]+$ ]]; then
  echo "uso: $0 <PHASE_NUM> [--force]" >&2
  exit 2
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# shellcheck source=gsd_plan_common.sh
# Resolve PHASE_DIR + helpers
# inline minimal (common sourced by gsd_plan.sh)
GROK_BIN="${GROK_BIN:-}"
if [[ -z "$GROK_BIN" ]]; then
  if command -v grok >/dev/null 2>&1; then
    GROK_BIN="$(command -v grok)"
  elif [[ -x "$HOME/.grok/bin/grok" ]]; then
    GROK_BIN="$HOME/.grok/bin/grok"
  else
    echo "ERRO: grok CLI nao encontrado (PATH ou ~/.grok/bin/grok)" >&2
    echo "FALLBACK_HINT: claude-opus" >&2
    exit 3
  fi
fi

resolve_phase_dir() {
  local phase="$1" d PHASE_DIR="" PAD SLUG
  if [[ -d ".planning/phases" ]]; then
    for d in .planning/phases/"${phase}"-*/ .planning/phases/"$(printf '%02d' "$phase" 2>/dev/null || echo "$phase")"-*/; do
      [[ -d "$d" ]] || continue
      echo "${d%/}"
      return 0
    done
  fi
  if [[ -d ".planning/milestones" ]]; then
    for d in .planning/milestones/*/phases/"${phase}"-*/ .planning/milestones/*/"${phase}"-*/; do
      [[ -d "$d" ]] || continue
      echo "${d%/}"
      return 0
    done
  fi
  case "$phase" in
    12) SLUG="scaffold-go-elfinfo" ;;
    13) SLUG="ppu-disasm-go-parity" ;;
    14) SLUG="find-functions-ppu-lift-poc" ;;
    15) SLUG="go-optin-verdict" ;;
    *)  SLUG="phase-${phase}" ;;
  esac
  PAD="$(printf '%02d' "$phase" 2>/dev/null || echo "$phase")"
  PHASE_DIR=".planning/phases/${PAD}-${SLUG}"
  mkdir -p "$PHASE_DIR"
  echo "AVISO: criei esqueleto $PHASE_DIR" >&2
  echo "$PHASE_DIR"
}

PHASE_DIR="$(resolve_phase_dir "$PHASE")"
MARKER="$PHASE_DIR/.last_plan_engine"

count_plans() {
  find "$1" -maxdepth 1 -name '*-PLAN.md' 2>/dev/null | wc -l | tr -d ' '
}

EXISTING_PLANS="$(count_plans "$PHASE_DIR")"
if [[ "$EXISTING_PLANS" -gt 0 && "$FORCE" -eq 0 ]]; then
  echo "OK: ja existem $EXISTING_PLANS PLAN.md em $PHASE_DIR (use --force para replanear)"
  ls -1 "$PHASE_DIR"/*-PLAN.md 2>/dev/null || true
  echo "existing $(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$MARKER"
  exit 0
fi

PROMPT_FILE="$(mktemp)"
LOG_FILE="$(mktemp)"
trap 'rm -f "$PROMPT_FILE" "$LOG_FILE"' EXIT

cat > "$PROMPT_FILE" <<EOF
You are the GSD planner (gsd-planner) for the ps3recomp monorepo.

TASK: plan GSD phase number ${PHASE}.
Phase directory: ${PHASE_DIR}

Read and follow:
- CLAUDE.md (absolute rules: no game data, pt-BR commit messages, kill smokes by PID, no forged acceptance; language: internal work ENGLISH, user-facing output pt-BR)
- .planning/PROJECT.md
- .planning/STATE.md
- .planning/ROADMAP.md
- .planning/REQUIREMENTS.md (if present and relevant)
- .planning/milestones/v1.2-REQUIREMENTS.md and v1.2-ROADMAP.md if PHASE is 12-15
- Any ${PHASE_DIR}/*-CONTEXT.md, *-RESEARCH.md, *-DISCUSSION*

PRODUCE:
1. One or more files ${PHASE_DIR}/*-PLAN.md (GSD pattern: NN-0M-PLAN.md)
2. Each PLAN in ENGLISH: goal, task checkboxes, files to touch, measurable acceptance criteria, tests to run
3. Do NOT implement product code (runtime/libs) — planning only
4. Do NOT commit game data; do NOT use git add -A
5. Internal reasoning and PLAN/CONTEXT/RESEARCH content: ENGLISH (model efficiency)
6. Optional short end summary for the user may be Brazilian Portuguese (pt-BR); technical body stays English

If CONTEXT is missing, create a minimal CONTEXT.md with assumptions in the same directory (English).
When done, list the paths of PLAN.md files created/updated.
EOF

TIMEOUT_SEC="${GSD_GROK_TIMEOUT:-900}"
MAX_TURNS="${GSD_GROK_MAX_TURNS:-100}"

echo "== gsd_plan_with_grok.sh =="
echo "GROK:    $GROK_BIN"
echo "PHASE:   $PHASE"
echo "DIR:     $PHASE_DIR"
echo "TIMEOUT: ${TIMEOUT_SEC}s"
echo

set +e
if command -v timeout >/dev/null 2>&1; then
  timeout --signal=TERM "$TIMEOUT_SEC" "$GROK_BIN" \
    --cwd "$ROOT" \
    --always-approve \
    --permission-mode auto \
    --max-turns "$MAX_TURNS" \
    --prompt-file "$PROMPT_FILE" \
    --output-format plain > >(tee -a "$LOG_FILE") 2> >(tee -a "$LOG_FILE" >&2)
  RC=$?
elif command -v gtimeout >/dev/null 2>&1; then
  gtimeout --signal=TERM "$TIMEOUT_SEC" "$GROK_BIN" \
    --cwd "$ROOT" \
    --always-approve \
    --permission-mode auto \
    --max-turns "$MAX_TURNS" \
    --prompt-file "$PROMPT_FILE" \
    --output-format plain > >(tee -a "$LOG_FILE") 2> >(tee -a "$LOG_FILE" >&2)
  RC=$?
else
  # Fallback portatil: background + kill
  "$GROK_BIN" \
    --cwd "$ROOT" \
    --always-approve \
    --permission-mode auto \
    --max-turns "$MAX_TURNS" \
    --prompt-file "$PROMPT_FILE" \
    --output-format plain > "$LOG_FILE" 2>&1 &
  GPID=$!
  ELAPSED=0
  while kill -0 "$GPID" 2>/dev/null; do
    if [[ "$ELAPSED" -ge "$TIMEOUT_SEC" ]]; then
      kill -TERM "$GPID" 2>/dev/null
      sleep 2
      kill -KILL "$GPID" 2>/dev/null
      wait "$GPID" 2>/dev/null
      echo "ERRO: Grok timeout apos ${TIMEOUT_SEC}s (PID $GPID)" >&2
      echo "FALLBACK_HINT: claude-opus" >&2
      exit 4
    fi
    sleep 5
    ELAPSED=$((ELAPSED + 5))
  done
  wait "$GPID"
  RC=$?
  cat "$LOG_FILE"
fi
set -e

echo
echo "grok exit=$RC"

# timeout(1) devolve 124 em GNU coreutils
if [[ "$RC" -eq 124 ]]; then
  echo "ERRO: Grok timeout (exit 124)" >&2
  echo "FALLBACK_HINT: claude-opus" >&2
  exit 4
fi

if [[ "$RC" -ne 0 ]]; then
  echo "ERRO: processo Grok falhou rc=$RC (auth/rede/crash?)" >&2
  echo "FALLBACK_HINT: claude-opus" >&2
  exit 5
fi

PLANS_AFTER="$(count_plans "$PHASE_DIR")"
if [[ "$PLANS_AFTER" -eq 0 ]]; then
  echo "ERRO: nenhum *-PLAN.md em $PHASE_DIR apos o Grok" >&2
  echo "FALLBACK_HINT: claude-opus" >&2
  exit 1
fi

echo "grok $(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$MARKER"
echo "PLAN.md encontrados ($PLANS_AFTER):"
ls -1 "$PHASE_DIR"/*-PLAN.md 2>/dev/null || true
exit 0
