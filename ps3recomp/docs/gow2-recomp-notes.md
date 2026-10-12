# God of War II HD — static recompilation research notes

Research log of bringing up God of War II HD (BCUS-98164 / EUR equiv) under ps3recomp.
Game-derived material (EBOOT, lifted output, extracted SPU images) lives **outside this
repo** in `gow2_work/`; this file records findings and the reusable fixes only.

## General correctness fixes (committed, benefit every title)

1. **`rlwinm`/`rlwimi`/`slw`/`srw` zero-extension** (`26a80f4`). These PPC word ops
   ZERO-extend their 32-bit result into the 64-bit GPR (bits 0:31 cleared). The lifter
   emitted `(int64_t)(int32_t)` (sign-extend) → silent corruption of the high word when
   the result MSB is 1 (addresses, compares). Verified against XenonRecomp. This broke the
   first GoW2 boot wall (`func_0030600C`, an allocator computing a bad effective address).

2. **lv2 syscall event-flag number collision** (`7a298aa`). `SYS_EVENT_FLAG_*` were numbered
   consecutively 139-146, colliding with the timer/time block: 141=usleep, 142=timer_sleep,
   145=get_current_time. Because `sys_event_init()` registers after `sys_timer_init()`,
   `sys_event_flag_wait` won handler[141] → every guest `usleep()` dispatched to a malformed
   `sys_event_flag_wait` and busy-spun (syscall 141 hit ~10M times in 8s). Corrected to the
   real PS3 numbers (verified vs RPCS3 `lv2.cpp`): create=82 destroy=83 wait=85 trywait=86
   set=87 clear=118 cancel=132 get=139. Diagnostic trap added in `0337b23` (`PS3_TRACE_SC`).

## GoW2 boot wall progression

`func_0030600C` (rlwinm bug) ✓ → **cellSysutil singleton** ✓ → **RSX/GCM** (current).

### Wall 2 — cellSysutil singleton at `[0x6FF480]` (broken, game-specific force-fix)

The main thread spins in `func_002B3CB0` pumping `cellSysutilCheckCallback`, waiting for
the singleton at `[0x6FF480]` to become non-NULL. The singleton's only constructor chain
(`func_0025C838` #7 → `func_002B2E74` → `func_0024A7CC` → `func_001954B0` → `func_00193BBC`)
runs *after* the spin (call #3 of the same init), so it deadlocks — on real hardware the
singleton is produced asynchronously (a system callback during the pump) which ps3recomp
does not deliver. Force-fix (in `gow2_work`, not a general runtime fix): lazy-build the
singleton with the real ctor inside the spin and force the ready flag. Boot then advances
through module loads → `sceNpInit` → `cellGcm*` (RSX/GCM init).

### Wall 3 — RSX/GCM (current frontier)

`func_002F0190` writes GCM commands through a `CellGcmContextData` (begin@0/end@4/current@8/
callback@C) whose callback@C is uninitialized (reads the allocator magic `0xC0DE1111`) and
whose command-buffer pointer targets the RSX region. This is the graphics subsystem; needs
real cellGcm/RSX command-buffer setup. Megaproject.

## SPU subsystem — pipeline validated on real GoW2 code

The checkout's SPU model is **lift-based** (`tools/spu_lifter.py` SPU→C →
`runtime/spu/spu_workload.c` registry → `spu_run_lifted_job`), not an interpreter.

GoW2's EBOOT embeds **4 SPU ELF images** (EM_SPU, ET_EXEC):

| image | offset / entry | libs | role | lift | run |
|-------|----------------|------|------|------|-----|
| spu0  | 0x4C8780 / 0x3070 | spurs, **libgcm_spu**, sync | graphics/EDGE workload | 421 fn, ~100% | (SPURS) |
| spu1  | 0x4D5D00 / 0x3050 | spurs, **libdma**, atomic | EDGE-zlib decompress | 515 fn, ~100% | (SPURS) |
| spu2  | 0x4DF380 / 0x4080 | libm, sputhread | math kernel | 511 fn, ~100% | ✓ clean |
| spu3  | 0x4F0900 / 0x4080 | libm | math kernel | 420 fn, ~100% | ✓ clean |

All four lift and compile with 0 errors (only `.word` data bytes unsupported). spu2/spu3
(no libspurs) execute standalone end-to-end (load → lifted entry → return clean; the
"indirect branch to unknown LS" logs are expected from running with no real task input →
uninitialised function pointers in LS). spu0/spu1 are SPURS workloads and need the SPURS
kernel/dispatch contract; spu1's EDGE-zlib is a SPURS task that pulls work-queue elements
via DMA (not a standalone `inflate(src,dst)`), so invoking it for real is a multi-session
ABI reverse-engineering effort.

## RSX content pipeline + SPU isolation (Fases A-D, commits 95c21f9..3d76af9)

Four increments were built on top of the RSX/SPU infra above. Distinguish
carefully: **provado in-boot** (observed live in a real `boot_v2_new.exe` run
against the GoW2 EBOOT) vs. **provado offline/unit** (a standalone harness,
device-free or headless, exercising the real production code path) vs. **não
exercitado** (the code is live and reachable, but the game's current boot
state never drives it — the content wall below, not a defect).

| Área | O que foi feito | Como foi provado |
|------|------------------|-------------------|
| FP shader cache | Contadores `rsx_d3d12_fp_cache_hits/misses/decompile_ok/decompile_fail` + trace `[RSX-SH]` (`PS3_TRACE_RSX_SHADERS=1`) no pipeline real `d3d12_set_shader → rsx_fp_decompile → D3DCompile → PSO` | **In-boot**: contadores crescendo ao vivo (ex. `hits=7026`) usando `PS3_SHADER_DEMO` (NV40 real, mas sintético — não é asset do jogo) |
| VP → D3D12 | `rsx_vp_decompile` ligado ao cache de PSO (`ShaderPsoSet.vp_hash` + VS compilado `vs_5_0`), com fallback automático para passthrough se decompile/compile falhar ou não houver transform program | **Offline** (`libs/video/tests/test_vp_ps_link.c`: VP→vs_5_0 `S_OK`, D3DReflect confirma 12/12 inputs do PS linkados). **NÃO in-boot** — GoW2 nunca carrega transform program neste ponto (`vp_load=0`), então o path ao vivo sempre cai no passthrough |
| Textura linear | `d3d12_bind_texture` com resolve de EA real (`resolve_texture_ea`) e guard de página committed | **Unit-validado** (bijeção + round-trip em `test_texture_deswizzle.c`). **NÃO exercitado in-boot** — o jogo nunca chama `SET_TEXTURE` neste ponto (0 binds em toda run observada) |
| Textura swizzled | Deswizzle Morton (`rsx_texture_deswizzle.h`: `rsx_morton_offset`/`rsx_deswizzle_texels`) + detecção de swizzle corrigida (bit LN, não dimension) | **Unit-validado** (golden vector PASS + bijeção 8x8/32x8/6x6 não-pow2). **NÃO exercitado in-boot** — mesma razão acima |
| SPU spu1 | Despacha e retorna limpo sob `PS3_SPU_ALL`/env, sem crash host | **In-boot** — observado em runs de 100s (dispatch + return limpo) |
| SPU spu2/spu3 | Gates finos `PS3_SPU2`/`PS3_SPU3` (além de `PS3_SPU_ALL`); isolação SEH de job (thread dedicada + VEH → `ExitThread()` em crash fatal, host sobrevive) | **Provado por teste determinístico** (`test_seh_isolation_main.c`: AV real numa thread de job → `[SPUJOB] aborted by SEH`, host sobrevive, job limpo seguinte roda OK) + smokes standalone spu2/spu3 limpos. **NÃO exercitado in-game** — spu2/3 não são despachados na janela de boot atual (não chegam no intro/movie; "sobem no gameplay") |
| Boot geral | Loop de render não regride | **In-boot** — baseline sem `PS3_SPU_ALL`: 263 flips/100s saudável; com `PS3_SPU_ALL`: host sobrevive ao timeout, sem crash novo |

### A parede de conteúdo, desmembrada: Paredes A-E (Plano 2, Tasks 1-6)

A antiga "parede de conteúdo" monolítica (acima) foi desmembrada e investigada
a fundo pelo Plano 2 (intro FSM → WADs → RSX, subagent-driven, Tasks 1-6). O
resultado é 5 paredes distintas, duas delas **vencidas** desde então:

| Parede | Estado | Evidência |
|--------|--------|-----------|
| [A] SmLogo stuck | **VENCIDA** | FSM progride nos DOIS caminhos: FORCE (`bt_intro_wads.sh`, ~10s) e NATURAL/EOS (intro toca 330 frames → arm st620=11 → 11→0; `bt_visual_combo.sh`, sem FORCE) |
| [B] WADs open | **VENCIDA** | `open 'R_LglScA'` (3KB) + `open 'R_PermA'` (20MB) em ambos os caminhos |
| [C] WAD inflate | **PAREDE ATUAL** | spu1 despacha e RODA (dispatch HIT, DMA real, `spu job returned cleanly`) mas o JOGO nunca pede o dearchive do R_PermA — o pipeline de asset primário não roda no recipe de intro |
| [D] CGOWShader | **desacoplada de C; investigação multi-sessão** | CRC-miss com string válida; registry de variantes nunca populado (`ICGLdrShader` 0 callers, fonte vazia N=-1 18/18); os shaders que falham são os `$/enginesupport/` residentes, 1º `Invalid shader combination` já no frame ~1 (antes do WAD) — raiz de fundo comum com [C]: pipeline de asset primário ausente |
| [E] Pixels de jogo | **BLOQUEADA por [C]+[D]** | sem consumo de asset + sem microcódigo de shader real → sem draws próprios |

Ou seja: o jogo **sai do logo** (parede A vencida) e **abre os WADs certos**
(parede B vencida) — algo que não era verdade antes do Plano 2 — mas o
consumo real desses WADs (inflate via spu1 disparado pelo jogo, não pelo
smoke) e o carregamento de shaders próprios (`CGOWShader`) continuam sem
disparar, e as duas ficam presas na mesma raiz de fundo: o subsistema de
asset/streaming primário do jogo não roda dentro da janela de boot exercitada
(intro). É um diagnóstico, não um fix — nenhuma correção pontual segura foi
encontrada; requer localizar/forçar o disparo desse subsistema, fora do
escopo do Plano 2.

O pipeline RSX (FP/VP/textura) e a isolação de SPU seguem implementados e
**ligados ao caminho real** (não são mocks), e provados até onde o jogo
consegue chegar (via `PS3_SHADER_DEMO`, um programa NV40 sintético injetado no
mesmo `rsx_state`, ou via harnesses offline/testes determinísticos para os
trechos que o jogo ainda não alcança). **Não** renderiza pixels do jogo, **não**
carrega texturas do jogo — isso continua bloqueado pelas paredes [C]/[D]/[E].

## Matriz de aceite global (honesta)

| # | Critério (do plano) | Estado REAL verificado | Marca |
|---|---|---|---|
| 1 | WAD carrega + ICGLdrShader registra | Infra pré-existente (`[SHREG]` existe), mas no boot atual o jogo trava na parede `Invalid shader combination: Default.ps3fx` antes de progredir. Não confirmado nesta janela. | PARCIAL/pré-existente — não é item novo desta leva |
| 2 | ≥1 FP de jogo no cache D3D12 | FP do `PS3_SHADER_DEMO` (NV40 real) passa pelo pipeline real; contadores `[RSX-SH]` crescem ao vivo. NÃO é shader nativo do jogo (o jogo não emite draw próprio ainda). | SIM (pipeline); ressalva: demo, não asset do jogo |
| 3 | ≥1 VP decompilado em PSO (ou dump+compile ok) | Provado OFFLINE (`test_vp_ps_link`: VP→vs_5_0 `S_OK`, 12/12 linka PS). In-boot cai no fallback (`vp_load=0`). | SIM offline; NÃO in-boot |
| 4 | Fallback passthrough se VP falhar | SIM, garantido no código e observado in-boot. | SIM |
| 5 | Textura linear upload ok | Path implementado + guard; NÃO exercitado in-boot (0 `SET_TEXTURE`). Deswizzle/detector provados por unit test. | Implementado+unit; NÃO in-boot |
| 6 | Textura swizzled upload ok | idem #5 — deswizzle Morton + golden vector PASS unit; detector de swizzle corrigido (bit LN). NÃO in-boot. | Implementado+unit; NÃO in-boot |
| 7 | spu1 default ou env, sem crash host | SIM — spu1 despacha e retorna limpo sob env; sem crash. | SIM (env-gated) |
| 8 | spu2/3 registráveis; crash isolado | SIM — gates `PS3_SPU2`/`PS3_SPU3`; isolação SEH provada por teste determinístico + smokes standalone. NÃO exercitado in-game (spu2/3 não despachados no boot atual). | SIM (mecanismo); repro in-game pendente |
| 9 | Loop 40fps não regride sem `PS3_SPU_ALL` | SIM — baseline sem env: loop saudável (263 flips/100s), spu2/3 não registrados. | SIM |

Fonte: verificação de campo das Fases A-D (commits `95c21f9`, `320cbc6`,
`425d8d0`+`cfb57eb`, `3d76af9` no ps3recomp; `e01fbee` no gow2_work).

**Nota:** o plano RSX acima (FP/VP/textura) está **pronto e ligado ao caminho
real**, apenas aguardando o jogo alcançá-lo — os itens marcados "NÃO in-boot"
(#1, #3, #5, #6) ficam bloqueados pelas paredes [C]/[D]/[E] do Plano 2
(intro→WADs→RSX, abaixo), não por trabalho pendente no pipeline RSX em si.

### Matriz de aceite — Plano 2 (intro FSM → WADs → RSX)

Critérios do plano de intro FSM/WADs/RSX (Tasks 1-6, subagent-driven), com o
resultado REAL medido pelo smoke E2E (`gow2_work/smoke_intro_to_rsx.sh`):

| # | Critério | Resultado |
|---|----------|-----------|
| 1 | Recipe documentado + script verde (WAD open) | ✅ T1 (`c061dab`; `PS3_VDEC_ASYNC=1` obrigatório pós-`f811ee3`) |
| 2 | Log st620 transitions legível | ✅ T2 (`[MOVIEFSM]` edge-triggered, `2dabeeb`) |
| 3 | (Stretch) natural EOS sem FORCE | ✅ T3 — ATINGIDO (intro toca e progride naturalmente) |
| 4 | spu1 HIT no path pós-logo/dearch | ✅ T4 (`e70ef8d`; +fix stack `6610361`) |
| 5 | Queda de `Invalid shader` OU bypass controlado | ❌ — diagnóstico entregue: nenhum fix honesto disponível (paredes [C]/[D] desacopladas; bypass não gera pixels) |
| 6 | ≥1 FP de conteúdo de jogo OU bind_texture real | ❌ — bloqueado pela mesma raiz (pipeline de asset primário) |
| 7 | Host sem crash 150s; flips>0 | ✅ (todas as runs; SEH nunca precisou agir; 0 crash) |

Dois critérios seguem **❌** de propósito: nem o Plano 2 nem esta rodada de
docs alegam pixels ou textura do jogo — ver seção "Paredes A-E" acima para a
raiz e o porquê de não haver fix pontual seguro.

### Smoke de integração

Dois smokes complementares:

- `gow2_work/smoke_rsx_spu.sh` — foco RSX+SPU isolado, com `PS3_SHADER_DEMO`
  (NV40 sintético, não é o boot real de intro). VFS root + VM low/stack +
  reorder/tblsize fixes + `PS3_RSX_FIFO` + `PS3_RSX_BACKEND=d3d12` +
  `PS3_TRACE_RSX_SHADERS` + `PS3_DUMP_SHADERS` + `PS3_SPU_ALL`, timeout
  configurável (default 60s).
- `gow2_work/smoke_intro_to_rsx.sh` — smoke E2E único do Plano 2: recipe
  completo de intro (movie FSM FORCE+NATURAL/EOS, `PS3_VDEC_ASYNC=1`
  obrigatório — ver nota abaixo) + `PS3_SPU1=1` (dearch) + backend RSX d3d12
  com trace de shader, timeout default 150s. Greps de sanidade cobrem as 5
  paredes A-E (`FORCE SEQDONE`/`state-3 gate`, `open 'R_LglScA'`/`'R_PermA'`,
  `dispatch HIT fp=0x2A5C4E67A14505B8`, `RSX-SH`/`set_shader`/`Invalid shader
  combination`, `SetFlipCommand`). Run real (150s, `boot_v2_new.exe` via
  `EBOOT.ELF`, 14/07):
  ```
  force_seqdone=1 state3_gate=1 r_lgl=1 r_perm=1
  spu1_miss=0 spu1_hit=4 spujob_clean=4
  rsx_sh=6 set_shader=1 invalid_shader_combination=293
  flips=285 crash_markers=0
  ```
  Confirma ao vivo: FSM sai do logo e chega no gate ([A] vencida), os dois
  WADs abrem ([B] vencida), spu1 despacha e volta limpo 4x ([C] roda mas sem
  ser disparado pelo jogo para o R_PermA), e a parede `Invalid shader
  combination` continua batendo 293x em 150s ([D], sem queda). Exit 124 do
  processo do jogo é o comportamento **normal** (o boot roda em loop de
  render indefinidamente até ser cortado pelo timeout) — não é falha do
  smoke.
  **Nota `PS3_VDEC_ASYNC=1`**: o runtime mudou o default de `StartSeq` para
  sync no commit `f811ee3` ("reverter default p/ sync (async crashava o
  backend D3D12)"); o loop que dispara `FORCE SEQDONE`
  (`libs/codec/cellVdec.c`) só roda na thread assíncrona — sem esta var o
  FORCE nunca dispara e os WADs nunca abrem. Como o recipe do
  `smoke_intro_to_rsx.sh` também liga `PS3_RSX_BACKEND=d3d12`, o motivo
  original do revert (crash no backend D3D12 com async) foi reavaliado nesta
  rodada: a run de 150s acima não crashou (`crash_markers=0`), então a
  combinação async+d3d12 está estável neste recipe/momento — mas o default
  do runtime permanece sync; isto é opt-in explícito via env.

## Remaining blockers (megaprojects, intertwined, order matters)

1. **RSX/GCM** — 0% rendering; the GCM command buffer / context must be set up so command
   writes land in mapped memory and are consumed. This is the current boot wall.
2. **SPURS kernel** — host-side scheduler to dispatch spu0/spu1 (the firmware kernel binary
   is absent). Not exercised until after the RSX wall; spu0 generates RSX commands, so it
   depends on RSX anyway. Order is **RSX → SPURS**.

Honest verdict: the path to a *playable* GoW2 is RPCS3. ps3recomp here is research; the two
general fixes above benefit the whole project.
