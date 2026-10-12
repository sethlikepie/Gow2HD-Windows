# GSD — routing de modelos (ps3recomp)

**Actualizado:** 2026-08-02

## Idioma

- **Interno (plan/research/reasoning):** inglês  
- **Output ao usuário (chat, checkpoints, commits):** pt-BR  
- `response_language` em config = `Portuguese (Brazil)` só afeta user-facing

## Politica deste monorepo

| Etapa GSD | Motor | Como |
|-----------|--------|------|
| Discuss / research (default) | Claude (Opus/Sonnet) | subagents no Claude Code |
| **Plan (preferido)** | **Grok** | `scripts/gsd_plan.sh` → `gsd_plan_with_grok.sh` |
| **Plan fallback (auto)** | **Claude Opus** | se Grok timeout/auth/crash/zero PLAN — **sem pedir confirmacao** |
| **Execute** | **Claude Opus** | `.planning/config.json` → `model_profile: quality`, `models.execution: opus` |
| Verify | Claude Sonnet/Opus | verifier GSD |
| Execucao em Grok | **PROIBIDO por default** | `workflow.cross_ai_execution: false` |

## Cadeia de fallback do plan (automatica)

```text
1. Grok (timeout GSD_GROK_TIMEOUT=900s)
      │ falha / nao responde / sem PLAN.md
      ▼
2. Claude CLI -p --model opus  (se `claude` no PATH)
      │ falha / sem PLAN.md
      ▼
3. Host Claude Code (sessao actual): gsd-plan-phase INLINE
      (agente NAO pergunta ao user — so reporta PLAN_ENGINE=host-claude-opus-fallback)
```

Marcador por fase: `.planning/phases/NN-*/.last_plan_engine`

## Comandos

| Pedido do usuário | Comando |
|----------------------|---------|
| Uma fase hibrida | `/gsd-run-phase N` |
| Varias fases / "gsd autonomo" | `/gsd-run-autonomous` |
| Tudo so em Claude (stock) | `/gsd-autonomous` |
| So planear (Grok+fallback) | `./scripts/gsd_plan.sh N` |
| So Grok (sem fallback) | `./scripts/gsd_plan.sh N --no-fallback` |

## O que e auto-gerido vs manual

### Auto (`/gsd-run-autonomous` ou "gsd autonomo")

1. Fases incompletas no ROADMAP  
2. Por fase: `gsd_plan.sh` (Grok→Opus) → execute Opus → verify  
3. STATE/ROADMAP  
4. Parar so em blocker real / destructive / falha total  

### Stock `/gsd-autonomous`

Nao chama Grok. Preferir hibrido neste monorepo.

### Env uteis

| Var | Default | Significado |
|-----|---------|-------------|
| `GSD_GROK_TIMEOUT` | 900 | segundos ate considerar Grok morto |
| `GSD_CLAUDE_TIMEOUT` | 900 | timeout do CLI Claude no fallback |
| `GSD_CLAUDE_MODEL` | `opus` | modelo do fallback CLI |
| `GSD_PLAN_NO_FALLBACK` | 0 | 1 = so Grok |

## Config chave (`.planning/config.json`)

```json
"model_profile": "quality",
"models": { "planning": "opus", "execution": "opus", "verification": "sonnet" },
"workflow": { "cross_ai_execution": false }
```

`models.planning: opus` = fallback / plan stock no Claude. Plan preferido continua Grok.
