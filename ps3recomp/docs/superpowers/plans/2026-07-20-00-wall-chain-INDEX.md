# Índice — planos de implementação até GoW2 recompilado

> **Ordem de execução obrigatória.** Não abrir o plano N+1 com o aceite do N vermelho.
> Plano mestre de contexto: [`2026-07-20-gow2-full-bringup.md`](2026-07-20-gow2-full-bringup.md).
>
> **Actualização 2026-07-23 (Track M + M3+ R1–R12):** Gate **B GREEN** (StartSeq real +
> thr + R_Perm full, PARK=0). Track M M0–M5 **diagnóstico fechado**; M3+ R1–R12 **fecho**
> (ICALL freelist classificado; HEAP40 H3 DEAD; issuer reclass=`2EFD60`; hot loop
> `CDBA4…CDD88` ~41k post=0; CE0A0 exit guards; menu arm B6150/B94* tot=0; thr oneshot
> 36A598=0; B6150 OPD orphan; POSTTHR only CC9D0+2B2DD0+B951C ratio 2:1:1; B951C ≠ present;
> f4=0 residual M2; TIMEOUT=90/120 after=0).
> Gate **A RED**; menu UI **NO**; playable **NO**. M2 CLOSE-PRESERVE shipped.
> **Wall activo = TYPE15 f4/work** dentro parent `2B2DD0→B951C` (present re-arm bloqueado;
> não OPD B94EC, não CE03C wait cosmético, não bug de gate 2B2DD0/B951C).
> Veredito: `gow2-recomp/notes/2026-07-23-gate-a-verdict.md`. Residual:
> [`2026-07-23-m3plus-postwad-present.md`](2026-07-23-m3plus-postwad-present.md).
> Índice mestre:
> [`2026-07-23-00-gow2-playable-master-INDEX.md`](2026-07-23-00-gow2-playable-master-INDEX.md).
> Plano 4.MENU: [`2026-07-23-menu-type15-frame-loop.md`](2026-07-23-menu-type15-frame-loop.md).
> Tooling paralelo (Xenon): [`2026-07-23-xenonrecomp-lessons-recomp-tooling.md`](2026-07-23-xenonrecomp-lessons-recomp-tooling.md).

## Cadeia

| Ordem | Plano | Walls | Estado | Objetivo de aceite |
|------:|-------|-------|--------|--------------------|
| **1** | [`2026-07-20-01-host-rebaseline-mac.md`](2026-07-20-01-host-rebaseline-mac.md) | W0 | ✅ feito | Mac linka + smoke honesto verde |
| **2** | [`2026-07-20-02-wall-m2-spurs-boot.md`](2026-07-20-02-wall-m2-spurs-boot.md) | W1, W6, W7 | ✅ M2 vencido | Boot sai do spin M2 *ou* wall reclassificado com prova |
| **3** | [`2026-07-20-03-spu-jobs-protocol.md`](2026-07-20-03-spu-jobs-protocol.md) | W8–W12 | 🟡 parcial (wiring OK; jobs deep TBD) | Jobs SPU reais (DMA/LFQueue) com TDD |
| **4.0** | [`2026-07-20-macos-intro-audio-open-wall.md`](2026-07-20-macos-intro-audio-open-wall.md) | macOS st620=1 open/poll | ✅ **destravado** (2026-07-20) | Open FIOS + consumo de `[op+0x90]` → `st620≥3` sem forja |
| **4.EOS** | [`2026-07-20-macos-movie-eos-fsm.md`](2026-07-20-macos-movie-eos-fsm.md) | f744 / arm / EOS | ✅ T0–T5; Task 7 RE | Arm NATURAL; FORCE cold no intro |
| **4.EOS+** | [`2026-07-21-intro-vdec-open-force-wad.md`](2026-07-21-intro-vdec-open-force-wad.md) | vdec Open/FORCE/WAD | ✅ **Gate B** (REPLAY-NOPIC; thr+R_Perm) | 2nd StartSeq real → FORCE → `R_*` |
| **4** | [`2026-07-20-04-content-intro-wad.md`](2026-07-20-04-content-intro-wad.md) | W13–W15 | 🟡 R_Perm full; stream/UI TBD | Assets fluem além do open full |
| **4.MENU** | [`2026-07-23-menu-type15-frame-loop.md`](2026-07-23-menu-type15-frame-loop.md) | TYPE15/CC9D0/flip | 🟡 **parcial / M0–M5+R1–R12 diag** | M2 list ✅; A **RED**; wall = **TYPE15 f4/work** (pós-R12) |
| **5** | [`2026-07-20-05-rsx-shaders-pixels.md`](2026-07-20-05-rsx-shaders-pixels.md) | W17–W23 | ⏳ após present/flip pós-WAD | Frame do jogo via RSX (não só clear/demo) |
| **6** | (futuro) A/V + polish | W24–W28 | ⏳ | Menu/fase jogável |

## Planos satélite (referência — não substituem a cadeia)

| Plano | Estado | Uso |
|-------|--------|-----|
| [`2026-07-20-macos-intro-audio-open-wall.md`](2026-07-20-macos-intro-audio-open-wall.md) | ✅ tasks 0–7 + **Task 8 done-consumption** | Open/poll; sticky + play-already; ver secção “Status 2026-07-20 late” |
| [`2026-07-20-macos-movie-eos-fsm.md`](2026-07-20-macos-movie-eos-fsm.md) | ✅ T0–T5; Task 7 RE (C3644 refutado) | EOS arm / FORCE cold; wall WAD → plano post-st3 |
| [`2026-07-20-macos-post-st3-smpd-wad.md`](2026-07-20-macos-post-st3-smpd-wad.md) | ✅ diag G2/G4 (SMPD no-op) | RE em `notes/2026-07-21-wad-trigger-g4.md` |
| [`2026-07-21-intro-vdec-open-force-wad.md`](2026-07-21-intro-vdec-open-force-wad.md) | ✅ Gate B (2026-07-23) | REPLAY-NOPIC; thr+R_Perm; PARK=0 |
| [`2026-07-23-00-gow2-playable-master-INDEX.md`](2026-07-23-00-gow2-playable-master-INDEX.md) | 📍 **mestre 2026-07-23** | Tracks M/S/G/U/T/P; M+R1–R12 A RED |
| [`2026-07-23-menu-type15-frame-loop.md`](2026-07-23-menu-type15-frame-loop.md) | 🟡 M0–M5 diag done; wall residual | TYPE15 f4/work (não empty-list) |
| [`2026-07-23-m3plus-postwad-present.md`](2026-07-23-m3plus-postwad-present.md) | ✅ R1–R12 diag; fix ⬜ | Gate A still RED; wall = f4 residual M2 |
| [`2026-07-23-xenonrecomp-lessons-recomp-tooling.md`](2026-07-23-xenonrecomp-lessons-recomp-tooling.md) | ⬜ paralelo | TOML/midasm/jtable/SIMDe |
| [`2026-07-21-00-macos-metal-stack-INDEX.md`](2026-07-21-00-macos-metal-stack-INDEX.md) | ⬜ **eixo Mac GPU/media** | M0–M10 (overlay→draw→MSL→tex→state→RT→vdec…) |
| [`2026-07-21-00-macos-metal-pending-backlog.md`](2026-07-21-00-macos-metal-pending-backlog.md) | 📋 inventário | Gap vtable/extras D3D12→Metal |
| [`2026-07-21-macos-videotoolbox-metal-overlay.md`](2026-07-21-macos-videotoolbox-metal-overlay.md) | ✅ M0 GREEN | Logo VT+Metal 330f+EOS (2026-07-21) |
| [`2026-07-21-macos-metal-rsx-draw-path.md`](2026-07-21-macos-metal-rsx-draw-path.md) | ✅ M1 GREEN | draw path + demo triangle |
| [`2026-07-21-macos-metal-shader-msl.md`](2026-07-21-macos-metal-shader-msl.md) | ⬜ M2 | FP/VP → MSL → PSO cache |
| [`2026-07-21-macos-metal-bind-texture.md`](2026-07-21-macos-metal-bind-texture.md) | ⬜ M3 | bind_texture + deswizzle → MTLTexture |
| [`2026-07-21-macos-metal-iosurface-zerocopy.md`](2026-07-21-macos-metal-iosurface-zerocopy.md) | ⬜ M4 | VT→Metal zero-copy (após M0) |
| [`2026-07-21-macos-metal-pipeline-state.md`](2026-07-21-macos-metal-pipeline-state.md) | ⬜ M5 | blend/depth/scissor/masks |
| [`2026-07-21-macos-metal-vertex-attribs.md`](2026-07-21-macos-metal-vertex-attribs.md) | ⬜ M6 | set_vertex_attribs |
| [`2026-07-21-macos-metal-render-targets.md`](2026-07-21-macos-metal-render-targets.md) | ⬜ M7 | RT + depth + blit present |
| [`2026-07-21-macos-metal-content-hold.md`](2026-07-21-macos-metal-content-hold.md) | ⬜ M8 | HOSTRES content hold |
| [`2026-07-21-macos-metal-vdec-present.md`](2026-07-21-macos-metal-vdec-present.md) | ⬜ M9 | cellVdec → Metal present |
| [`2026-07-21-macos-metal-default-ops.md`](2026-07-21-macos-metal-default-ops.md) | ⬜ M10 | metal default + smokes |
| [`2026-07-20-fios-async-read-movie-io-hle.md`](2026-07-20-fios-async-read-movie-io-hle.md) | ⛔ T4–T6 bloqueadas (refutação) | Não reabrir sem nova medição; open usa scheduler nativo |
| [`2026-07-14-gow2-intro-movie-fsm-wads.md`](2026-07-14-gow2-intro-movie-fsm-wads.md) | ref Windows | Detalhe intro FSM → WAD (alimenta plano 4) |
| [`2026-07-14-gow2-rsx-content-vp-tex-spu.md`](2026-07-14-gow2-rsx-content-vp-tex-spu.md) | ref | RSX VP/tex/SPU (alimenta plano 5) |
| [`2026-07-20-gow2-full-bringup.md`](2026-07-20-gow2-full-bringup.md) | roadmap | Phases 0–7 |

## Próximo passo de implementação (ordem)

1. **Wall activo = TYPE15 f4/work (pós-R12):** parent `2B2DD0→B951C→CC9D0` mapeado
   (ratio 2:1:1); f4=0 residual M2 (nodes=12 ≠ UI); B951C ≠ present. R11 OPD DEAD;
   CE03C wait not sole; issuer=`2EFD60` (R6). Veredito:
   `gow2-recomp/notes/2026-07-23-gate-a-verdict.md` + `r12-postthr-spin.md`.
   Residual: [`2026-07-23-m3plus-postwad-present.md`](2026-07-23-m3plus-postwad-present.md).
2. **Não reabrir** 4.EOS+ como wall activo: Gate B está verde (REPLAY-NOPIC + thr + R_Perm).
3. **Não** entrar Track S como “menu done”: Gate A **RED** até SetFlip_after_R_Perm≥1
   (ou prova de present equivalente). Depois: **Track S** + pad re-smoke; **Track G** Metal M2+ paralelo.
4. **Track T** (sessão separada OK): Xenon tooling A1 `recomp_config.py` —
   [`2026-07-23-xenonrecomp-lessons-recomp-tooling.md`](2026-07-23-xenonrecomp-lessons-recomp-tooling.md).
5. Mestre: [`2026-07-23-00-gow2-playable-master-INDEX.md`](2026-07-23-00-gow2-playable-master-INDEX.md).

## Método TDD comum a todos

1. **Vermelho:** teste/smoke que falha pelo wall alvo (comando + expected FAIL).
2. **Verde:** implementação mínima que faz o teste passar.
3. **Refator:** limpar sem mudar o aceite.
4. **Commit** pequeno e nomeado.
5. **Proibido:** force-write de flags do guest sem writer legítimo (constraint M2).
6. **Oráculo RPCS3 (recomendado em walls I/O, SPURS, RSX):** mesmo EBOOT no
   [RPCS3](https://github.com/RPCS3/rpcs3) + log recomp → tabela de gaps.
   Guia completo: [`docs/RPCS3_AS_REFERENCE.md`](../../RPCS3_AS_REFERENCE.md).
   Não forjar FSM/EOS porque o emulador avançou; não copiar GPL sem política.

## Repos

- Motor: `ps3recomp` — https://github.com/andrebrumdev/ps3recomp  
  (branch de trabalho típica: `macos-arm64-port-f0-f2` ou successor)
- Port: `gow2-recomp` — https://github.com/andrebrumdev/gow2-recomp  
  e/ou `ps3recomp/games/gow2` (monorepo)
- Referência (não submodule): https://github.com/RPCS3/rpcs3
