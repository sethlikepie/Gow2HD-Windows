---
name: "source-command-gsd-run-phase"
description: "Hibrido GSD — Grok plan com fallback auto Codex Opus; execute Codex Opus."
---

# source-command-gsd-run-phase

Use this skill when the user asks to run the migrated source command `gsd-run-phase`.

## Command Template

# /gsd-run-phase — Grok plan → (fallback Opus) → execute Opus

| Passo | Quem | Como |
|-------|------|------|
| Plan | **Grok** primeiro | `scripts/gsd_plan.sh N` |
| Plan fallback | **Codex Opus** | **AUTOMATICO** se Grok falhar/timeout/sem PLAN — **sem pedir confirmacao** |
| Execute | **Codex Opus** | `gsd-execute-phase` + `.planning/config.json` |
| Verify | Codex | se a fase exigir |

`cross_ai_execution` permanece **false**.

## Fallback (obrigatorio — nao e opcional)

Ordem:

1. `./scripts/gsd_plan.sh $PHASE`  
   - tenta Grok (`gsd_plan_with_grok.sh`, timeout `GSD_GROK_TIMEOUT` default 900s)  
   - se rc≠0 → Codex CLI Opus  
2. Se o script imprimir `HOST_FALLBACK_REQUIRED=1` ou ainda nao houver `*-PLAN.md`:  
   - **Imediatamente** planear **neste** host Codex (Opus) seguindo o workflow `gsd-plan-phase` / planner GSD  
   - **NAO** uses `AskUserQuestion` para autorizar o fallback  
   - Reporta ao user: `PLAN_ENGINE=host-Codex-opus-fallback`  
3. So falha a fase se **todos** os tres caminhos falharem (Grok + Codex CLI + host inline).

Marcador: `$PHASE_DIR/.last_plan_engine` (`grok` | `Codex-opus-fallback` | `host-Codex-required` | `existing`).

## Argumentos

- `PHASE` (numero)
- `--force-plan` → `--force` no script
- `--skip-plan` → exige PLAN.md existente
- `--skip-execute` / `--no-verify`
- `--no-fallback` → so Grok (repassa ao script; **nao** usar em autonomo)

## Processo

### 0. Pre-flight

- Repo root; `chmod +x scripts/gsd_plan.sh scripts/gsd_plan_with_grok.sh`
- `cross_ai_execution` false
- Ler STATE + ROADMAP; respeitar AGENTS.md

### 1. Plan

Se nao `--skip-plan`:

```bash
./scripts/gsd_plan.sh "$PHASE" ${FORCE} ${NO_FALLBACK_FLAG}
```

Interpretar saida:

| Condicao | Accao |
|----------|--------|
| rc=0 e ha `*-PLAN.md` | continuar; anotar PLAN_ENGINE |
| rc≠0 ou zero PLAN | **fallback host Opus** = corre plan-phase GSD inline **ja** (sem perguntar) |
| `--no-fallback` e rc≠0 | STOP e reportar |

### 2. Execute (Opus)

Se nao `--skip-execute`: workflow `gsd-execute-phase` para `$PHASE`.

### 3. Verify

Default on; skip com `--no-verify`.

### 4. Report

| Item | Valor |
|------|--------|
| Phase | N |
| PLAN_ENGINE | grok / Codex-opus-fallback / host-Codex-opus-fallback / existing |
| Execute | ok/fail |
| Verify | ok/skip/fail |

## Exemplos

```text
/gsd-run-phase 12
/gsd-run-phase 12 --force-plan
```
