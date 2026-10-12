# Mac Metal — default backend + matriz de smokes (M10)

> **For agentic workers:** `subagent-driven-development` ou `executing-plans`.  
> **Depende de:** M0 e/ou M1 minimamente buildáveis. Ops/ergonomia, não features GPU novas.

**Goal:** Fazer **Metal o default** no Mac para corridas com janela; documentar e automatizar smokes que cubram o stack M0–M9 sem `PS3_NO_RSX` por engano; alinhar `rodar_gow2.sh` / `env_gow2.sh` / `boot_macos.cpp` pick_backend.

**Architecture:** `pick_backend()` já escolhe por `PS3_RSX_BACKEND`. Defaults actuais tendem a `sdl`. Mudar default Apple para `metal`; `sdl`/`null` só headless CI.

## Global Constraints

- Não partir smokes headless (`PS3_NO_RSX=1` continua válido).
- Política Apple: metal default = nativo.

---

### Task 1–2: Implementado 2026-07-21

- [x] Default `metal` em `env_gow2.sh` / `rodar_gow2.sh` / `boot_macos.cpp`
- [x] Log `[boot] RSX backend=metal`
- [x] `smoke_metal_matrix_mac.sh` (headless, metal boot, overlay, draw, default_env)

---

## Aceite M10

| ID | Critério |
|----|----------|
| O1 | Sem env, `rodar_gow2.sh` usa metal (log) |
| O2 | `PS3_NO_RSX=1` não abre janela e não falha pick |
| O3 | Matriz documentada e runnable |

## Fora de escopo

CI cloud Apple obrigatório; performance A/B LIFT_OPT.
