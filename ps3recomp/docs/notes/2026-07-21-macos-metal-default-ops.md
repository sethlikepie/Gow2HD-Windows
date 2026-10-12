# M10 — Metal default + matriz smoke (2026-07-21)

## Defaults

| Ficheiro | Comportamento |
|----------|----------------|
| `gow2-recomp/env_gow2.sh` | Darwin → `PS3_RSX_BACKEND=metal` se unset e sem `PS3_NO_RSX` |
| `gow2-recomp/rodar_gow2.sh` | usa env; log `[boot] RSX backend=…` |
| `boot_macos.cpp` `pick_backend` | default `metal`; log `[boot] RSX backend=metal` |

Headless: `PS3_NO_RSX=1` → backend none (sem janela).

## Matriz

```bash
cd gow2-recomp
./smoke_metal_matrix_mac.sh          # SECS=8 default
SECS=6 ./smoke_metal_matrix_mac.sh
```

| Smoke | Assert |
|-------|--------|
| headless | NO_RSX + progresso guest |
| metal_boot | `[boot] RSX backend=metal` / Window created |
| overlay | iosurface / decode start |
| draw | draw path PSO / M6 float3 |
| default_env | env sozinho → metal |

## Aceite

| ID | Resultado |
|----|-----------|
| O1 | sem env → metal |
| O2 | NO_RSX → none |
| O3 | matriz runnable |
