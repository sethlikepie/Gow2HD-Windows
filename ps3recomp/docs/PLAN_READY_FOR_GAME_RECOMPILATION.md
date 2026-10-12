# Plano-delta: tornar o ps3recomp pronto para recompilar um jogo

> **Para agentes executores:** SUB-SKILL OBRIGATÓRIA — use `superpowers:subagent-driven-development`
> (recomendado) ou `superpowers:executing-plans`. Este documento é um **plano-mestre de tracks**;
> o detalhe TDD passo-a-passo de cada track vive num ficheiro próprio em
> `docs/superpowers/plans/2026-07-24-*-rdy<N>-*.md`, escrito quando o track arranca.

**Objectivo:** fechar o delta real entre o estado medido do repo (2026-07-24) e um pipeline de
recompilação verificável, sem repetir trabalho já mergeado nem competir com os planos activos.

**Arquitectura:** este plano não é auto-suficiente por desenho. Ele **possui** apenas o trabalho
que nenhum plano existente possui, e **delega** o resto por referência explícita
(`IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md`, `2026-07-23-00-gow2-playable-master-INDEX.md`,
`2026-07-21-00-macos-metal-stack-INDEX.md`). Cada track termina num aceite que é um comando
executável com saída esperada, não numa afirmação qualitativa.

**Stack:** C11/C++20, Python ≥3.11, CMake ≥3.20 + Ninja, CTest, GitHub Actions,
Metal/VideoToolbox no Darwin, D3D12 no Windows.

## Restrições globais

Aplicam-se a **todos** os tracks. Copiadas de `CLAUDE.md`.

- **Nunca commitar dados de jogo:** `EBOOT.ELF/BIN`, `*.psarc`, `*.self`, `extracted/`,
  `movie_cache/`, `spu_images/`, `*.m2v`, `*.wav`, `*.wad_ps3`. Nunca `git add -A` / `git add .`
  — sempre ficheiros explícitos.
- **Push só para o fork pessoal** (`personal` neste repo, `origin` no gow2-recomp). Nunca `sp00nznet`.
- **Commits em português, sem `Co-Authored-By`.**
- **Nunca forjar resultados.** Distinguir sempre `in-boot` vs `offline/unit` vs `não-exercitado`.
- **Proibidos como "fix":** CRC bypass, stub de combination, estampar magic à mão, null-check
  cosmético, mascarar guard, `PS3_SHADER_DEMO` como aceite de conteúdo.
- **Probes/experimentos gated por env var, OFF por default** (no-op no baseline).
- **Apple-first no macOS:** Metal é o backend **default** no Darwin (stack M0–M10 todo GREEN), não
  um requisito opcional. VideoToolbox/IOSurface/CoreAudio antes de shims portáveis no hot path.
- **Python ≥3.11 visível ao CMake.** O `/usr/bin/python3` desta máquina é 3.9.6 e o
  `find_package(Python3 3.11 REQUIRED)` (`CMakeLists.txt:42`) falha sem `-DPython3_EXECUTABLE`.
- **Working tree limpa é aceite, não cortesia.** Nenhum track fecha com `git status --short`
  a mostrar ficheiros versionados modificados.

---

## 1. Estado verificado (auditoria de 2026-07-24)

Esta secção substitui o diagnóstico da v1 deste documento. Cada linha foi verificada por leitura
de código, execução, ou `git show` — não por citação de documento anterior.

### 1.1 Correcções ao diagnóstico da v1

| Afirmação da v1 | Veredito | Evidência |
|---|---|---|
| "M0 — base de qualidade a construir" | **Já mergeado** (`0c6a01b`, Fase 0) | 15 testes no CTest (`CMakeLists.txt:274-416`), 3 jobs de CI (`.github/workflows/ci.yml:40,80,124`), convenção SKIP (`CMakeLists.txt:298`) |
| "os blockers medidos são RSX/GCM real e, após isso, o scheduler SPURS" | **Desactualizado** | 2201 `SetFlipCommand` reais do jogo antes do R_PermA (`.superpowers/sdd/progress.md:390-391`); kernel SPURS a correr com workers pthread (`libs/spurs/spurs_kernel.c:165,220`); a estimativa "6–12 pessoa-meses de SPURS" foi refutada em `../gow2-recomp/notes/2026-07-20-wall-m2-vencido.md:1` — era um bug do lifter no `stwcx.` indexado com rA=0 |
| "os 99 `.word` são incógnitas a classificar" | **Classificados** | 97/99 = `heqi` (RI10 op8=0x7F, opcode SPU válido); 2/99 = float4 numa fronteira bogus em `games/gow2/spu_lifted/spu2_v2/spu_recomp.c:13395-13399` (0x10500) |
| "o `spu_lifter.py` é exploratório por desenho" | **Regressão, não desenho** | `7554685` adicionou `heqi/hgti/hlgti` ao `spu_disasm.py`; `11a1c3c` apagou as mesmas 3 linhas. `spu_halt_if()` (`runtime/spu/spu_channels.c:482`) ficou com **zero chamadores** |
| "os 2 falhanços do `test_fp_decompiler` podem ser grafia, não semântica" | **Correcto, e o ledger está errado** | `f2363ce` (2026-07-22, *fix(metal): MSL VP/FP*) introduziu as duas grafias novas; antes dele o emissor produzia literalmente `r[0].xyzw =` e `float4(0.5,0.25,0,1)`. `.superpowers/sdd/progress.md:373` regista-os como "BUGS REAIS DE PRODUTO" — está errado |
| "dois walkers de FIFO, o genérico lê host-endian" | **Confirmado, medido** | `libs/video/rsx_commands.c:613` (`buf[pos++]`, zero byteswap) vs `libs/video/cellGcmSys.c:522,545-553` (`vm_read32`, que faz bswap). `test_rsx_fifo` corrido: `methods processed: 0`, 4 FAIL, exit 1 |
| "não há teste E2E de PPU portátil" | **Confirmado** | Nenhum dos 15 testes do CTest toca `ppu_load_elf`/`ppu_run`; `runtime/ppu/tests/boot_main.cpp:35-37` é `#error` fora do Windows e exige EBOOT (`:318`) |
| "imagens SPU e PPU liftado são deliberadamente privados" | **Parcialmente falso** | O C liftado do SPU **está versionado**: `git ls-files games/gow2/spu_lifted` devolve 8 pastas. É por isso que a suíte suja a árvore |

### 1.2 Factos novos que a v1 não continha

**F1 — o build nunca re-corre o lifter.** `../gow2-recomp/build_macos.sh:5`:
*"# Assumes the lift already ran"*. Consequência directa: **RDY-2, RDY-3 e RDY-4 são melhorias de
ferramenta e não chegam ao GoW2** enquanto o re-lift estiver bloqueado.

**F2 — o re-lift está bloqueado.** ~49 `patch_*.py` presos ao shape antigo do lift, 11 "patches"
que são verificadores puros de edições manuais, e ~700 linhas de subsistema host que existiam
apenas dentro do lift gitignored (`.superpowers/sdd/progress.md:495,593-634`; extraídas para
`../gow2-recomp/host_gow2_factory.cpp`, ainda por ligar ao build). **Hoje um re-lift limpo não
reproduz o `boot_gow2` actual.**

**F3 — o CI nunca correu.** `gh api repos/andrebrumdev/ps3recomp/actions/runs` → `total_count: 2`,
ambos Dependabot. Linux e Windows são `continue-on-error`; o job Windows provavelmente nem compila
(`-ffp-contract=off` sem guarda de MSVC, `CMakeLists.txt:336-340`).

**F4 — o comando de teste da v1 viola o aceite da v1.** `ctest --test-dir build` cru deixa
22 ficheiros versionados modificados + 1 directório novo. Só o wrapper `scripts/test_runtime.sh`
repõe, e mesmo esse nunca **falha** por árvore suja (`:296-305` decide o exit code só por `CTEST_RC`).

**F5 — o universo de imagens SPU é ambíguo.** 99 `TODO spu` é exacto para o corpus **versionado**
(8 pastas: 24+24+16+15+4+2+7+7). Mas `../gow2-recomp/build_macos.sh:160` compila
`spu_lifted/spu?_v2` — que no checkout de trabalho inclui `spu4_v2`/`spu5_v2` **untracked**.
"Todas as imagens SPU usadas pelo build" ≠ os 99.

**F6 — colisão de namespace.** Marcos `M0..M5` colidem com o stack Metal `M0..M10`
(`docs/superpowers/plans/2026-07-21-00-macos-metal-stack-INDEX.md`) **e** com o Track M `M0..M5`
do master INDEX de 2026-07-23, ambos com conteúdo RSX/present/GoW2. Este plano usa o prefixo
**`RDY-`**, sem colisão em todo o repo.

---

## 2. Articulação com os planos existentes

Contrato de não-duplicação. Um item que aparece na coluna "dono" **não é retrabalhado aqui**.

| Trabalho | Dono | Este plano |
|---|---|---|
| Infra de testes, CTest, CI, SKIP explícito | `IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md` Fase 0 — **mergeada** (`0c6a01b`) | só o resíduo aberto → **RDY-1** |
| Orquestrador `tools/ps3recomp_build.py`, `manifest.json`, cache por hash | Fase 1 do reliability (`:76-121`) | delega; contribui só o delta GoW2 → **RDY-5** |
| Política de cobertura declarativa (`[coverage] unknown_spu = "error"`) | Fase 3 do reliability (`:159-168`), parser já aterrado (`tools/recomp_config.py`) | **usa o TOML**, não flags CLI → **RDY-3** |
| Migração dos `games/gow2/*.sh` | Fase 11 do reliability (`:376-406`) | delega |
| RSX/apresentação/multimédia (arquitectura) | Fase 10 do reliability (`:354`) | **RDY-2** entrega o núcleo FIFO como pré-requisito, e reordena-o para antes |
| Wall activo do GoW2 (Gate A, TYPE15 f4, present re-arm) | `2026-07-23-00-gow2-playable-master-INDEX.md:44` | **fora de escopo**; ver §6 |
| Stack Metal M0–M10 (todo GREEN) | `2026-07-21-00-macos-metal-stack-INDEX.md` | consome; não redefine backend |
| Separação dados-vs-código no lifter PPU | Task B3 de `2026-07-23-xenonrecomp-lessons-recomp-tooling.md` (PPU-only) | **RDY-3** faz o equivalente SPU, que ninguém possui |

**Registo obrigatório:** ao arrancar, este plano entra em `.superpowers/sdd/progress.md` e ganha
uma linha no INDEX de tracks, para não voltar a existir um plano-mestre sem cross-reference.

---

## 3. Tracks

### RDY-0 — Desbloquear o re-lift (pré-requisito duro)

**Porque é o passo zero:** por F1 e F2, nenhuma correcção de ferramenta (RDY-2/3/4) chega ao
GoW2 sem isto, e o aceite de reprodutibilidade (RDY-5) é hoje o oposto do estado medido.

**Âncoras:** `../gow2-recomp/build_macos.sh:5`; `.superpowers/sdd/progress.md:495` (triagem dos 49),
`:593` (11 verificadores puros), `:617-634` (`host_gow2_factory.cpp`, *"POR FAZER: ligar ao build"*).

**Trabalho**

1. Triar os ~71 `patch_*.py` de `../gow2-recomp/recomp_mid_v2/` em três classes, com ficheiro de
   catálogo versionado: **funcional** (altera comportamento), **verificador puro** (nunca escreve),
   **obsoleto** (o shape que ele patcheia já não é gerado).
2. Converter cada verificador puro em asserção do build (falha o build se a condição não se
   verificar) ou apagá-lo — não deixar scripts que só confirmam edições manuais.
3. Ligar `../gow2-recomp/host_gow2_factory.cpp` ao `build_macos.sh` e provar que o subsistema host
   deixa de depender do conteúdo de `recomp_macos_v2/`.
4. Reescrever os patches funcionais que falham contra o lift actual (o lifter mudou; o patch é que
   está preso ao shape antigo).
5. Acrescentar ao `build_macos.sh` um passo de re-lift opcional (`RELIFT=1`), mantendo o default
   actual (assume lift feito) até o aceite passar.

**Plano de execução:** [`docs/superpowers/plans/2026-07-25-00-rdy0-desbloquear-relift.md`](superpowers/plans/2026-07-25-00-rdy0-desbloquear-relift.md)
— 10 tarefas com passos TDD, escrito depois de um levantamento que corrigiu quatro premissas
desta secção (ver abaixo).

**Correcções ao esboço acima, medidas em 2026-07-25**

1. **Um re-lift limpo hoje nem linka no Mac.** O commit `11a1c3c` reverteu o `#include <stdlib.h>`,
   o ramo `__APPLE__` do TLS de `g_trampoline_fn` (`tools/ppu_lifter.py:371` tem hoje
   `__declspec(thread)`) e a geração do `ppu_stubs.cpp` — órfão de 653 B com 418 referências ao
   único símbolo que define. É o mesmo commit que apagou o `heqi` (ver RDY-3). Repor isto é a
   Task 2 e vem antes de qualquer trabalho de patches.
2. **São 1603 linhas de host órfão, não ~700.** Medido por diff de preâmbulos: 698 no chunk 000
   (probes, regeneráveis), 897 no 001 (factory 700 + **f2b 197 sem casa versionada**), 8 no 003.
   **21 marcadores de comportamento não têm nenhum script capaz de os escrever.**
3. **O binário de referência não é comparável.** O `boot_gow2` actual foi linkado contra
   `../ps3recomp-bringup` (o `ppu_loader.o` referencia `_ps3_checkpoint_fire` e
   `_ps3_mem_oob_report`, ausentes deste branch). Fixar o engine root e regravar o baseline é o
   passo 6 da Task 1.
4. **O aceite abaixo era um falso gate.** `./apply_all_patches.sh --check` **já sai 0 hoje** — não
   executa nenhum dos 71 patches, só 5 verificações de marcador (`apply_all_patches.sh:251-255`).

**Aceite (executável, corrigido)**

- `RELIFT=1 ./build_macos.sh` escreve num directório novo (`recomp_macos_v3`), corre o catálogo em
  **modo apply** e constrói. `recomp_macos_v2` fica intacto até à promoção.
- `./verify_lift_baseline.sh recomp_macos_v3` sai 0 — nenhum dos 21 comportamentos órfãos se perdeu.
- `./apply_all_patches.sh recomp_macos_v3 --status <tsv>`: **zero** `FAILED` fora da classe `PROBE`
  (baseline pré-plano: `APPLIED 13 / ALREADY 9 / FAILED 49`).
- `./smoke_relift_equiv.sh recomp_macos_v3 6 <tsv>` sai 0: `st620 max ≥ 3` em **≥4 de 6** corridas,
  `function_table_count == 56072` (fonte) e `56223 lifted functions registered` (boot),
  `151 imports / 13 modules`, zero órfãos. `R_PermA` **não** entra: não é observável na recipe M0.
- `PATCH_CATALOG.tsv` fecha em 71 sem "indeterminado".

**Não-regressão:** o baseline é gravado ANTES de tocar em nada (Task 1) e comparado por contadores
nomeados com strings exactas. O `st620` é não-determinístico (1/8 a 6/6 medidos sem mudança de
código, com limiar de swap) — por isso o aceite é N-de-M e grava `vm.swapusage` por corrida.

---

### RDY-1 — Fechar o resíduo da Fase 0

**Escopo:** só o que a Fase 0 deixou aberto. Não re-inventariar, não re-registar, não reescrever CI.

**Âncoras:** `runtime/spu/tests/run_tests.sh:13,57-66` (escreve em `$HERE`, sem override);
`scripts/test_runtime.sh:224-305` (repõe, mas nunca falha); `CMakeLists.txt:266-273` (exclusão
comentada); `.superpowers/sdd/progress.md:373` (classificação errada).

**Trabalho**

1. **Parametrizar o outdir da suíte SPU.** `run_tests.sh` passa a aceitar `SPU_TEST_OUTDIR`
   (default: um `mktemp -d`), e o CTest passa-lhe o build tree. Os fixtures versionados
   `runtime/spu/tests/out_*/` passam a ser **golden de comparação**, não destino de escrita —
   decidir explicitamente: ou se mantêm como golden (e a suíte compara), ou saem do controlo de
   versões. Não deixar o estado actual (versionados **e** sobrescritos).
2. **Gate de working tree.** `scripts/test_runtime.sh` passa a **falhar** (exit ≠ 0) se
   `git status --short` mostrar ficheiros versionados modificados no fim, em vez de avisar.
3. **Tornar os vermelhos visíveis.** Registar `test_rsx_fifo` e `test_fp_decompiler` no CTest com
   `WILL_FAIL` (ou um passo informativo de CI que os liste), para que a exclusão deixe de viver só
   num comentário de CMake. Enquanto forem `WILL_FAIL`, um deles a passar é que falha o CI — o que
   é o sinal correcto para RDY-2.
4. **Corrigir o ledger.** `.superpowers/sdd/progress.md:373` classifica os 2 falhanços do
   `test_fp_decompiler` como "BUGS REAIS DE PRODUTO". A evidência datada (`f2363ce`) diz o
   contrário. Corrigir a entrada e registar a evidência — se alguém "corrigir o produto" para o
   teste passar, reverte o `f2363ce` e re-parte o MSL.
5. **Registar `test_msl_decompiler`** no CTest, depois de o converter à convenção de SKIP
   (`libs/video/tests/test_msl_decompiler.c:60` devolve FAIL quando não há device Metal; tem de
   imprimir `SKIP: <motivo>` e sair 0, senão bloqueia o job macOS, que é o único bloqueante).
6. **Guardar o `-ffp-contract=off`** por compilador (`CMakeLists.txt:336-340`) para o job Windows
   deixar de ser verde-por-`continue-on-error`.
7. **Acabar a correcção de docs iniciada em `2c5fef2`:** `docs/TOOLS.md:611` e
   `docs/GAME_PORTING_GUIDE.md:35` ainda dizem "Python 3.9+" contra o
   `find_package(Python3 3.11 REQUIRED)`. Marcar `docs/gow2-recomp-notes.md` e
   `games/gow2/SPURS_M2_FINDINGS.md` como obsoletos no cabeçalho (ver §7).
8. **`scripts/smoke_macos.sh`** — único runner que exercitava vídeo/backends, continua partido
   (passo 3 não linka: `_cellGcmMainloopNote`, `_movie_vt_*`) e fora do CI e do `test_all.sh`
   (`docs/BASELINE_FASE0.md:186-205`). Ou é reparado, ou entra com `SKIP:` declarado — não pode
   continuar invisível quando RDY-5 depende de smokes.

**Aceite (executável)**

- `ctest --test-dir <build> --output-on-failure` **cru** (sem wrapper) seguido de
  `git status --short` → só ficheiros untracked esperados, zero ` M `.
- `ctest -N` lista `test_rsx_fifo` e `test_fp_decompiler` (como `WILL_FAIL`) e `test_msl_decompiler`.
- Um run de CI real existe e é verde no macOS (hoje `total_count: 2`, ambos Dependabot).

---

### RDY-2 — Núcleo FIFO NV47xx único e BE-correcto

**Custo medido:** `rsx_process_command_buffer` tem **1 chamador em toda a árvore** — o próprio
teste (`libs/video/tests/test_rsx_fifo.c:80`). Mudar a API é quase de graça. O caro e arriscado é
migrar o caminho vivo.

**Âncoras:** `libs/video/rsx_commands.c:613-646` (34 linhas, host-endian);
`libs/video/cellGcmSys.c:522-635` (114 linhas, das quais ~12 são decode: `:545-553`).

**Divergências reais entre os dois decoders — o plano tem de as decidir antes de unificar:**

| Aspecto | `cellGcmSys.c` (vivo) | `rsx_commands.c` (testado) |
|---|---|---|
| Endianness | `vm_read32` → bswap ✅ | `buf[pos++]` cru ❌ |
| Máscara do método | `cmd & 0x3FFCu` (`:552`) | `((h>>2)&0x7FF)<<2` = `& 0x1FFC` (`:625`) |
| Jump | segue (`:546-547`) | `break` (`:636`) |
| CALL | salta **sem empilhar** (`:548`) | `break` |
| RETURN | `get += 4` (`:549`), **sem pop** | `break` |
| Limites | só `guard < 200000` (`:544`); `ioSize` nunca usado | `pos < count` |

**CALL e RETURN não existem correctamente em lado nenhum.** Migrá-los é **adicionar semântica**,
não mover código — e isso tem de ser dito no plano de execução, com testes próprios.

**Trabalho**

1. Núcleo em `rsx_commands` sobre uma fonte abstracta: `read_be32(offset)`, limites/tamanho do
   anel, `get`/`put`, jump/call/return **com pilha real**, callback por método, resultado
   estruturado (métodos processados, posição final, erro, motivo).
2. Decidir e documentar a **máscara canónica** do campo method (nenhuma constante NV do
   `rsx_commands.h` excede `0x1FF0`, mas o caminho vivo usa `0x3FFC` — escolher errado muda o
   comportamento in-boot em silêncio).
3. `cellGcmSys` fornece a fonte (`vm_read32`) e os callbacks de sync (`SET_REFERENCE`, semáforos,
   labels). Preservar o modo **sync-sem-backend**: hoje o sync corre sempre, mas
   `rsx_process_method` só é chamado se `bridge != 0` (`:536,:574`).
4. Shadow-diff gated (`PS3_RSX_SHADOW_DIFF=1`, OFF por default): correr núcleo novo e decoder
   legado lado a lado e registar a primeira divergência de `(method, value, get)`.
5. Backend trace: mantém-se como **rede de segurança de init** (`:481,:494-496,:502-505,:507-510`)
   e ganha um recorder de teste separado. Não remover o fallback — no macOS existe lógica
   específica para não substituir um backend vivo (`:469-472`), que já custou uma janela preta.
6. **Decidir o destino do terceiro modelo de estado RSX.** Existe uma camada de dispatch que
   nenhum dos dois walkers alimenta: `libs/video/rsx_dispatch.c:159` (`rsx_dispatch_method`,
   register file de 16K), consumida por `rsx_live_draw.c:1297` e pelos dois replays. Conhece
   métodos que nenhum walker consegue produzir (`M_GCM_DRIVER_FLIP 0xE944`, que sobrevive a
   nenhuma das duas máscaras). E `rsx_live_draw_method` tem **zero chamadores** fora do próprio
   ficheiro e é no-op fora do Windows (`:26-36`) — código morto in-tree. Ou converge com
   `rsx_process_method`, ou o projecto fica com dois modelos de estado depois do "núcleo único".
7. **Teste de integração** `cellGcmInit → memória guest → FIFO → SetFlipCommand → backend recorder`,
   verificando clear e draw. Não existe hoje. `runtime/host/host_main.c` tem ~180 linhas de fixture
   BE (FP NV40, VP, vertex arrays, textura 2x2, blend, draw) recicláveis, mas **não é um activo
   pronto**: não emite clear e desreferencia o retorno de `cellGcmGetControlRegister()`, que hoje
   devolve um EA guest.
8. Actualizar `docs/RSX_GRAPHICS.md:138` e `README.md:409` — este último descreve
   `gcm_flush_guest_cmdbuf()`, **função que não existe**.

**Aceite (executável, substitui o aceite circular da v1)**

- `test_rsx_fifo` 4/4 no CTest (hoje 0/4) e o `WILL_FAIL` de RDY-1 removido no mesmo commit.
- Testes novos verdes: non-increment; pacote truncado e `count` além do fim (sem acesso fora de
  limites, provado com ASan); jump; **call+return com pilha**; wrap de anel; callbacks de
  reference/semaphore/label.
- **Shadow-diff: 0 divergências** de `(method, value, get)` num run completo da intro com
  `PS3_RSX_SHADOW_DIFF=1`. Este é o aceite que discrimina — o da v1 ("um FIFO guest BE real produz
  métodos") já passa hoje em qualquer leitura.
- Smoke de intro 25s com probes OFF: `st620` max ≥ 3, `sticky` pub/hit, sem órfãos.

---

### RDY-3 — SPU: reverter a regressão de decode e ligar o modo estrito

**O escopo encolheu.** Não são 99 incógnitas: são 97 `heqi` + 2 dados + uma política que não existe.

**Âncoras:** `git show 7554685 -- tools/spu_disasm.py` (adiciona `heqi/hgti/hlgti`) vs
`git show 11a1c3c -- tools/spu_disasm.py` (apaga as mesmas 3 linhas);
`runtime/spu/spu_channels.c:482` (`spu_halt_if`, zero chamadores);
`games/gow2/spu_lifted/spu2_v2/spu_recomp.c:13395-13399` (float4 em 0x10500);
`tools/spu_lifter.py:772-798` (argparse sem qualquer flag de política).

**Trabalho**

1. **Reverter a regressão:** repor `heqi/hgti/hlgti` no `RI10_TABLE` do `spu_disasm.py` e o ramo
   correspondente, com teste de decode que falharia hoje.
2. **Ressuscitar `spu_halt_if`:** o lifter volta a emitir `spu_halt_if(ctx, <cond>, 0x<addr>)` em
   vez de comentário. Teste de semântica: a condição verdadeira aborta o contexto SPU.
3. **Fronteira de função:** declarar `spu2_v2` 0x10500..0x10510 como **dados** no config
   (`[[invalid_instructions]]` do `recomp_config`), não como opcode por implementar. Acrescentar
   detecção de **funções sobrepostas** e de **buracos** no mapa (`spu1_v2`: `spu_func_000030E8`
   cobre 0x30E8..0x3174 mas `spu_func_00003128` começa lá dentro; a mesma `heqi` de 0x3164 é
   liftada duas vezes).
4. **Política no TOML, não em CLI.** Usar `[coverage] unknown_spu = "error"|"warn"|"emit"` da
   Fase 3 do reliability. Em modo `error`: não escrever artefacto e sair ≠ 0.
5. **Relatório JSON** por imagem: endereço, raw word, mnemónico, função, secção, motivo de
   rejeição. Hoje o único diagnóstico é um print por mnemónico, **sem endereço** — a auditoria teve
   de reconstruir os 99 endereços contando linhas a partir dos labels `loc_`.
   Emitir também o endereço LS como comentário por instrução no C gerado.
6. **Alcançabilidade:** `grep 'reach\|cfg\|visited\|worklist' tools/spu_lifter.py` → 0 resultados.
   O critério "instrução alcançável" do plano v1 **não é computável hoje**. **Default recomendado:**
   redefinir o critério como *"dentro de `[start_addr, end_addr)` de uma função do mapa e não
   declarado como dados no config"* — computável já, e suficiente para as 6 imagens (os 2 casos de
   dados são exactamente os que a fronteira bogus de 0x10500 produz). O CFG/worklist real
   (1–2 dias + testes) fica como melhoria posterior, não como pré-requisito do modo estrito.
   O aceite não pode depender de uma análise que não existe.
7. **Definir o universo** (F5): o conjunto de aceite é `spu_lifted/spu?_v2` — o que
   `build_macos.sh:160` realmente compila — e as imagens em falta no monorepo (`spu4_v2`,
   `spu5_v2`) são importadas ou o aceite declara explicitamente que as exclui.

**Aceite (executável, substitui o aceite tautológico da v1)**

- Todas as imagens de `spu_lifted/spu?_v2` liftam com `unknown_spu = "error"` e **0 `.word`**.
- `grep -c 'TODO spu'` nas imagens compiladas → **0**.
- `grep -c 'spu_halt_if' <imagem>` → > 0 (prova que o halt voltou a ser código, não comentário).
- Teste de regressão que falha se `heqi` voltar a sair do `RI10_TABLE`.
- ELF SPU sintético com word desconhecido alcançável: modo `error` falha, aponta o endereço e
  **não escreve** fonte utilizável.

---

### RDY-4 — Fixture PPU end-to-end legal e portátil

**Único track genuinamente novo e sem sobreposição.** Viabilidade já medida: um ELF64 BE PPC64
sintético de 488 bytes atravessou `elf_parser.py → find_functions.py → ppu_lifter.py → clang`
com sucesso, incluindo seed por `.opd` com `shoff=0`.

**Âncoras:** `tools/wrap_spu_elf.py` (234 linhas, ELF32 BE EM_SPU, 1 `PT_LOAD` — serve de **estilo**,
não de base: falta `.opd`, TOC, BSS, ELF64); `runtime/ppu/ppu_loader.cpp:2468` (`ppu_load_elf`,
valida ELF64 MSB em `:2479`), `:1572` (`ppu_recomp_register`); `CMakeLists.txt:149` (runtime/ppu
excluído da lib).

**Trabalho**

1. `tools/wrap_ppu_elf.py` (~300–400 linhas): ELF64 BE, dois `PT_LOAD`, `.text`/`.opd`/BSS, entry
   OPD e TOC válidos, ≥2 funções, chamada directa, retorno, branch, load/store BE.
2. Importação HLE dummy — **o item mais barato, não o mais caro**: `bl` para stub dummy + JSON de
   1 entrada via `ppu_lifter.py --hle-stubs`, gerando `ps3_hle_call(NID, ctx)`. Não é preciso
   forjar `PT_PROC_PRX_PARAM` + `.lib.stub` bem formada para a primeira versão.
3. `runtime/ppu/tests/test_ppu_pipeline_e2e.py` orquestra a cadeia inteira em tmpdir.
4. Runner C++ portátil. **Bloqueador conhecido:** `runtime/ppu/ppu_loader.cpp:1114-1116` chama
   `ps3_type15_block_stomp()`, símbolo que **não existe no monorepo** — o runner precisa de um stub
   fraco. Sem isto o link falha (medido: `Undefined symbols: _g_trampoline_fn, _ps3_type15_block_stomp`).
5. O preâmbulo do lifter emite `__declspec` (`tools/ppu_lifter.py:66,371`) — o teste tem de passar
   `-fms-extensions` em POSIX, ou o preâmbulo passa a ser condicional.
6. Registar no CTest como teste Python que compila o runner no seu tmpdir (padrão já usado pelos
   9 `add_test` existentes), evitando o conflito configure-time vs test-time.

**Aceite (executável, substitui o aceite não-discriminante da v1)**

- Num clone limpo, sem EBOOT nem SDK PS3: `ctest -R py.test_ppu_pipeline_e2e` passa nas 3 plataformas.
- **Mutação prova o valor:** o teste tem de **falhar** quando se inverte o byteswap do parser de
  ELF, quando se corrompe uma fronteira de função, e quando o dispatcher não regista uma função.
  Um E2E que passa com o pipeline partido não é regressão-proof — este é o critério, não "atravessa
  a cadeia".
- `tools/test_ppu_lift.py:946` escreve em `<repo>/scratch/` — mover para tmpdir no mesmo track
  (a regra de artefactos deste plano aplica-se a ele também).

---

### RDY-5 — Reprodutibilidade do build GoW2 (delta sobre a Fase 1)

**Não é um orquestrador novo.** A Fase 1 do reliability já especifica `tools/ps3recomp_build.py` +
`manifest.json` + cache por hash. Este track contribui **o delta GoW2** e depende de RDY-0.

**O que já existe** (`../gow2-recomp/`): `decrypt_self.py`, `extract_pkg.py`,
`apply_all_patches.sh` (319 linhas, catálogo único, com `--check`), `build_macos.sh` (233 linhas),
18+ smokes com gates nomeados (`smoke_boot_mac.sh`, `count_menu_gate.py`, Gate A/B por posição no
log). **O que falta:** o encadeamento num comando, o passo de re-lift (RDY-0), o manifesto de
hashes, e o check de que não há dados proprietários no index.

**Trabalho**

1. Não introduzir `GOW2_ROOT`: o papel já é de `PS3_VFS_ROOT` / `PS3_MOVIE_CACHE` /
   `PS3_ENGINE_ROOT` (`../gow2-recomp/env_gow2.sh:14-16,37`; `build_macos.sh:29`). Um quarto nome
   para a mesma coisa é dívida.
2. Manifesto local de hashes (gitignored) sobre os inputs do jogo, para garantir mesma versão.
3. Check pré-build que falha se houver dados proprietários no index Git.
4. Wrapper que encadeia extract → re-lift → patches → lift SPU estrito → compile → link → smoke,
   como cliente do orquestrador da Fase 1, não como concorrente.

**Aceite (executável, com a ambiguidade da v1 resolvida)**

- A partir de um checkout limpo + cópia privada, **um comando** produz um `boot_gow2` que passa o
  smoke de intro com os mesmos contadores nomeados do baseline.
- "O mesmo executável" significa **funcionalmente equivalente medido pelos gates**, não
  bit-idêntico (paths absolutos, mtimes e build ids tornam bit-identidade inatingível). A v1 não
  distinguia; esta versão distingue.
- `git status` limpo e nenhum ficheiro de jogo em nenhum commit.

---

## 4. Ordem de execução e dependências

| Ordem | Track | Depende de | Bloqueia |
|---:|---|---|---|
| 0 | **RDY-0** re-lift desbloqueado | — | o **efeito no GoW2** de RDY-2/3/4, e RDY-5 |
| 1 | **RDY-1** resíduo da Fase 0 | — | nada (paralelo a tudo) |
| 1 | **RDY-4** fixture PPU E2E | — | nada (paralelo a tudo) |
| 2 | **RDY-2** núcleo FIFO | RDY-1 (gate de árvore limpa) | Fase 10 do reliability |
| 2 | **RDY-3** SPU estrito | RDY-1 | RDY-5 (etapa de lift estrito) |
| 3 | **RDY-5** build reproduzível | RDY-0, RDY-3, Fase 1 do reliability | — |

**Distinção que a v1 não fazia:** RDY-2, RDY-3 e RDY-4 podem ser **feitos** sem RDY-0 — são
melhorias de ferramenta e o seu aceite é offline. Mas o seu efeito **no jogo** é zero até RDY-0
fechar, porque o build não re-corre o lifter (F1). Planear como se RDY-2/3 "avançassem o GoW2"
antes de RDY-0 é o erro de ordenação da v1.

---

## 5. Definition of Done

- Build e testes obrigatórios verdes nas plataformas declaradas, **com pelo menos um run de CI
  real** (não apenas ficheiros de workflow).
- Parser NV47xx único, BE-correcto, usado pelo GCM, com **shadow-diff a zero** contra o decoder
  legado num run completo da intro.
- Código SPU de release sem `.word` nas imagens que o build compila, com `heqi` traduzido para
  `spu_halt_if` e os dados declarados como dados.
- Fixture PPU legal atravessa parser → boundaries → lift → build → loader → execução, **e falha
  sob mutação** de cada uma dessas etapas.
- Um comando reconstrói o `boot_gow2` a partir de uma cópia privada, com o re-lift no caminho.
- Nenhum aceite deste plano é satisfeito por um estado que já existia antes dele.

---

## 6. O que este plano NÃO faz

**Não avança o GoW2 para o menu.** O wall activo é `Gate A RED` — `SetFlip_after_R_Perm = 0`,
`Pad_total = 0`, causa nomeada: trabalho de produto **TYPE15 f4** dentro de `2B2DD0→B951C→CC9D0`
(`docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md:44`). Isso é um projecto à
parte, já em execução, com 12 rondas de diagnóstico fechadas. O aceite da v1 "entrada básica
funciona e o título alcança intro ou menu navegável" era **0/2 medível** (o guest nunca chama
`cellPadGetData` neste boot) e metade dele já estava entregue desde 2026-07-15 (parede A vencida).

**Não redefine o backend gráfico.** Metal é o default no Darwin e o stack M0–M10 está GREEN. Este
plano consome-o; não o trata como requisito opcional passível de SKIP.

**Não reescreve a Fase 0, a Fase 1 nem a Fase 11** do `IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md`.

---

## 7. Referências internas

**Activas — ler antes de executar**

- `CLAUDE.md` — regras absolutas, receita canónica de env, checklist de não-regressão
- `docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md` — Fases 0–12 (Fase 0 mergeada em `0c6a01b`)
- `docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md` — wall activo do GoW2
- `docs/superpowers/plans/2026-07-21-00-macos-metal-stack-INDEX.md` — Metal M0–M10 (GREEN)
- `docs/BASELINE_FASE0.md` — inventário de testes (§2), com órfãos declarados
- `.superpowers/sdd/progress.md` — ledger de execução (atenção: `:373` corrigido por RDY-1)
- `docs/gow2-asset-pipeline-map.md` — cadeia de causa raiz do funil de assets

**Código citado**

- `libs/video/rsx_commands.c:613-646`, `libs/video/cellGcmSys.c:522-635`
- `libs/video/tests/test_rsx_fifo.c`, `test_fp_decompiler.c`, `test_msl_decompiler.c`
- `tools/spu_lifter.py`, `tools/spu_disasm.py`, `tools/wrap_spu_elf.py`, `tools/recomp_config.py`
- `runtime/spu/spu_channels.c:482`, `runtime/spu/tests/run_tests.sh`
- `runtime/ppu/ppu_loader.cpp:1114,1572,2468`, `runtime/ppu/tests/boot_main.cpp`
- `CMakeLists.txt:149,266-273,298,336-340`, `scripts/test_runtime.sh`, `.github/workflows/ci.yml`

**Obsoletas — não usar como diagnóstico**

- `docs/gow2-recomp-notes.md:199-205` ("Remaining blockers … RSX → SPURS") — último commit
  2026-07-14; refutado por medição posterior. O próprio ficheiro auto-contradiz-se (`:96` diz que
  o WAD inflate é a parede actual; está vencido).
- `games/gow2/SPURS_M2_FINDINGS.md` — conteúdo de 2026-06-22; a estimativa de 6–12 pessoa-meses foi
  refutada em `../gow2-recomp/notes/2026-07-20-wall-m2-vencido.md`.

Ambos devem ganhar um cabeçalho de obsolescência (trabalho de RDY-1, item 4).
