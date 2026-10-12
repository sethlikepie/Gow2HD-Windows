---
name: "source-command-gsd-run-autonomous"
description: "GSD autonomo hibrido — Grok plan + fallback auto Opus; execute Opus. Nao e /gsd-autonomous stock."
---

# source-command-gsd-run-autonomous

Use this skill when the user asks to run the migrated source command `gsd-run-autonomous`.

## Command Template

# /gsd-run-autonomous — multi-fase com fallback automatico

```text
para cada fase incompleta:
  plan  = ./scripts/gsd_plan.sh N     # Grok → Codex CLI Opus se Grok cair
        | host /gsd-plan-phase N      # se ainda falhar — AUTOMATICO, sem AskUser
  exec  = gsd-execute-phase N         # Codex Opus (config quality)
  verify se necessario
```

## Fallback (critico)

Se Grok **nao responder**, timeout, auth, crash, ou zero `*-PLAN.md`:

1. `gsd_plan.sh` tenta Codex CLI Opus sozinho.  
2. Se ainda falhar (`HOST_FALLBACK_REQUIRED=1`): **tu (agente Codex Opus)** corres o planner GSD **inline** nesta sessao.  
3. **Proibido** bloquear em "queres fallback?" — o fallback e a politica do monorepo.  
4. So para e pede humano se **plan e execute** falharem de verdade (ou safety destructive).

Registar sempre `PLAN_ENGINE` no report da fase.

## vs stock `/gsd-autonomous`

| | Plan | Fallback | Execute |
|--|------|----------|---------|
| **Este comando** | Grok | auto → Opus | Opus |
| stock | Codex | n/a | Codex |

Linguagem natural "gsd autonomo" / "corre o gsd sozinho" → **este** comando.

## Argumentos

`--from N` `--to N` `--only N` `--force-plan` `--interactive` `--no-fallback` (desliga fallback; so para debug).

## Descobrir fases

ROADMAP + STATE; default **v1.1** se incompleto; v1.2 (12–15) so com pedido ou `--from 12`.

## Loop por fase

1. (opcional) discuss se `--interactive`  
2. `./scripts/gsd_plan.sh $PHASE` (+ force)  
3. Se PLAN em falta → **host plan-phase Opus ja**  
4. `gsd-execute-phase`  
5. verify; STATE; proxima fase  

Parar: blocker real, destructive gate, falha total de plan+exec.

## Pre-flight

```bash
chmod +x scripts/gsd_plan.sh scripts/gsd_plan_with_grok.sh
# grok e/ou Codex no PATH preferivel; se so houver host Codex, fallback host ainda funciona
```
