# M0 — Baseline menu-fast (honest freeze)

**Data:** 2026-07-23  
**Repo:** gow2-recomp  
**Binary:** `boot_gow2` (local build present)  
**Aceite:** Gate **B green** / Gate **A red** (explícito) — sem correção; só contagens.

## Comando

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp
TIMEOUT=90 LOG=/tmp/m0_menu_fast.log PS3_NO_RSX=1 ./rodar_gow2_menu_fast.sh
```

Recipe efectiva (script `rodar_gow2_menu_fast.sh` + `env_gow2.sh`):

| Var | Valor |
|-----|-------|
| `PS3_NO_RSX` | **1** (headless agent; sem janela Metal) |
| `PS3_RSX_BACKEND` | **none** (script: `backend=none` com NO_RSX) |
| `PS3_VDEC_ASYNC` | 1 |
| `PS3_VDEC_FORCE_SEQDONE_MS` | 1500 |
| `PS3_MOVIE_EOS` / `HLE` / `IO` | 1 |
| `PS3_BOOT_LOGO_MS` | 300 |
| `PS3_AUTO_LOAD_RUN` | 1 |
| `PS3_PAD_AUTOSTART` | 1 |
| `PS3_TYPE15_UNSTICK` | 1 |
| `PS3_SPU1` / `SPU4` / `SPU5` | 1 |
| `PS3_PERF_FSM` | 1 |
| `TIMEOUT` | 90s |
| kill | por PID do `boot_gow2` (TERM → -9); script nativo |

Wrapper observou:

```
[menu-fast] FORCE_SEQDONE=1500ms logos=300ms
[menu-fast] UNSTICK=1 SPU1/4/5 PAD AUTO_LOAD backend=none
[menu-fast] thr_auto_load end at 32s — +25s for menu/pad
[menu-fast] timeout 90s — kill
```

## Log

| Campo | Valor |
|-------|-------|
| Path | `/tmp/m0_menu_fast.log` (não commitado) |
| Linhas | 6310 |
| Tamanho | 376814 bytes |
| **MD5** | `34ccebd3e9b852abe2242d4027b5e6fa` |

## Contagens (regex do brief M0)

| Signal | Measured | Expected M0 | Notas |
|--------|----------|-------------|-------|
| StartSeq | **6** | (livre) | regex `cellVdecStartSeq\|StartSeq` também pega `StartSeq_count=` em REPLAY-NOPIC; wrapper: **2** StartSeq reais + FORCE×2 |
| PARK | **0** | **0** | sem reintrodução de PARK |
| REPLAY-NOPIC | **4** | — | skip PICOUT em replay |
| thr_end | **1** | ≥1 | `thr_auto_load() end` @ ~32s |
| R_Perm_full | **1** | ≥1 | `[GATE-FORCE] R_PermA full … bytes_read=20169344` |
| TYPE15_RESET | **2** | — | product list RESET (2A4FE4-safe) |
| attach_full | **2** | ≥1 | `attach=full` pós-sanitize / reuse CB56C |
| CC9D0 | **0** | — | `PS3_TRACE_CC9D0=0` (default script) — contagem de tag `[CC9D0]` off |
| SetFlip (total) | **3733** | — | flips no intro/path pré-WAD; com `PS3_NO_RSX=1` ainda há logs de flip guest se emitidos |
| **SetFlip_after_R_Perm** | **0** | **0** | Gate A: sem flip pós `R_Perm` full |
| Pad (`cellPadGetData`) | **0** | **0** | |
| **Pad_after_R_Perm** | **0** | **0** | Gate A |
| FATAL | **0** | **0** | |

### Ordem relevante no log

1. Intro/FORCE + logos DONE (2×).
2. Linha `R_Perm` full @ idx **5886**.
3. TYPE15 CB56C attach=full ×2; B71 HLE-lite path.
4. `thr_auto_load() start` / `end` **depois** de R_Perm (thr_end_after_R_Perm=1).
5. Até timeout 90s: **zero** `SetFlip` e **zero** `cellPadGetData` após R_Perm.

## Veredito de gates

| Gate | Critério | Status |
|------|----------|--------|
| **B** | thr_end ≥1 **e** R_Perm full ≥1; PARK=0; FATAL=0; attach_full ≥1 | **GREEN** |
| **A** | SetFlip e/ou Pad **depois** do último R_Perm full (menu/frame loop) | **RED** (0 / 0) |

**Não se reclama menu jogável.** FlashUI/shader noise no log **não** conta como menu (script e brief).

## Expected vs measured (resumo)

- Baseline honesto de **sucesso parcial**: pipeline intro→FORCE→WAD R_Perm→AUTO_LOAD thr end está vivo.
- Baseline honesto de **falha Gate A**: sem regresso a flip/pad pós-R_Perm (parede TYPE15/CC9D0 / frame loop — ver notas irmãs `2026-07-23-menu-fast-attempt.md`, `postthr-cc9d0-type15-wall`).
- Backend usado neste freeze: **`PS3_NO_RSX=1`** (metrics only). Contagens Gate B/A de thr/R_Perm/Pad independem de Metal; SetFlip total pode diferir com Metal window — o sinal de aceite Gate A é **SetFlip_after_R_Perm**, medido **0**.

## Pass M0

- [x] Uma corrida menu-fast ~90s, kill por PID  
- [x] Tabela + md5 + command line  
- [x] Gate B green / Gate A red explícitos  
- [x] Commit **só** desta note (sem code / sem dados de jogo)
