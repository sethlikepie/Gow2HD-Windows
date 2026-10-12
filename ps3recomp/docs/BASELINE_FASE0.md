# BASELINE_FASE0.md — Estado do repositório antes da Fase 0

> Relatório de auditoria produzido pela Task **F0-1** (`docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md`,
> Fase 0, primeiro bullet + "Entrega 1"). **Só documentação** — nada aqui foi corrigido; a
> remoção/correcção das violações listadas é trabalho das fases posteriores (nomeadamente a
> Fase 12). Todas as afirmações abaixo vêm de comandos correridos neste checkout
> (`git rev-parse HEAD` = `6d6dc6b`, branch `infra/fase0-recomp-reliability`, macOS/arm64,
> Darwin) e citam `ficheiro:linha` ou o comando exacto + saída. Onde algo só é verificável
> com boot do jogo (não há ELF nem o checkout irmão `../gow2_work` neste ambiente), está
> marcado explicitamente como **"requer boot — não verificado"**.

---

## 1. Estrutura do repositório

Camadas e o que cada uma é suposta conter:

| Camada | Suposto conter |
|---|---|
| `runtime/` | Runtime genérico do jogo: `ppu/` (loader+ISA+HLE bridge PPU), `spu/` (SPU runtime+testes), `memory/` (API de memória guest), `syscalls/` (lv2 syscalls), `prx/` (loader de PRX), `host/` (scaffolding do host) |
| `libs/` | Módulos HLE (`audio`, `codec`, `video`, `spurs`, `system`, `network`, `misc`, `filesystem`, `font`, `hardware`, `input`, `sync`) |
| `tools/` | Toolchain Python (lifters PPU/SPU, parser ELF/SELF/PRX, NID DB, config) |
| `games/gow2/` | Código/dados específicos do jogo (scripts de trace, config, artefactos SPU liftados) |

### Contagem de ficheiros (comando: `git ls-files <dir> | wc -l` vs `find <dir> -type f | wc -l`,
corrido em 2026-07-24 na raiz do repo)

| Camada | Ficheiros *tracked* (git) | Ficheiros em disco (`find`) | Diferença |
|---|---:|---:|---|
| `runtime/` | 117 | 172 | build logs/`.exe`/`.elf` de correr os testes (gitignored) |
| `libs/` | 271 | 272 | 1 binário de teste (`libs/video/tests/replay_mac`, gitignored) |
| `tools/` | 43 | 51 | `tools/__pycache__/*.pyc` (gitignored) |
| `games/gow2/` | 170 | 170 | nenhuma — tudo o que está em disco está tracked |

A diferença *tracked* vs *disco* em `runtime/`/`tools/` é só ruído de build local
(`.log`, `.exe`, `.elf`, `.pyc`) já coberto pelo `.gitignore`; não há dados de jogo soltos.

### Ficheiros tracked por linguagem (extensão), por camada

```
runtime/  : 49 .c, 35 .h, 14 .py, 14 .cpp, 4 .deps, 1 .sh   (total 117)
libs/     : 127 .c, 119 .h, 9 .m (Objective-C, backend Metal/GameController),
            8 .sh, 7 .py, 1 .cmd                             (total 271)
tools/    : 36 .py, 3 .md, 2 .c, 1 .txt, 1 .java             (total 43)
games/gow2/: 89 .sh, 33 .py, 12 .json, 10 .c, 8 .h, 7 .png,
             4 .md, 2 .gdb, 2 .cmd, 1 .stdout, 1 .pl, 1 .gitignore (total 170)
```
(comando: `git ls-files <dir> | sed -E 's/.*\.([A-Za-z0-9_]+)$/\1/' | sort | uniq -c | sort -rn`)

### Linhas totais tracked por camada (`git ls-files <dir> -z | xargs -0 wc -l`, linha "total")

| Camada | Linhas totais |
|---|---:|
| `runtime/` | 24.600 |
| `libs/` | 79.338 |
| `tools/` | 17.843 |
| `games/gow2/` | 204.988 (⚠ ver nota) |

⚠ Nota sobre `games/gow2/`: a maioria destas linhas não é código "de jogo" escrito à mão —
são artefactos **gerados**: `games/gow2/functions.json` sozinho tem 83.001 linhas, e os 8
ficheiros `games/gow2/spu_lifted/spu{0,1,2,3}{,_v2}/spu_recomp.c` somam ~101.166 linhas de
C emitido pelo `spu_lifter.py`. O código "de jogo" escrito à mão (scripts `tr*.sh` de trace,
`recomp_mid_v2/*.py`, `config/*.toml`) é uma fracção pequena disto.

### Sub-repartição por submódulo (contagem `git ls-files <sub> | wc -l`)

`libs/`: audio 9, codec 31, video 81, spurs 17, system 21, network 38, misc 42,
filesystem 4, font 9, hardware 6, input 9, sync 4.

`runtime/`: ppu 19, spu 68, memory 1, syscalls 24, prx 4, host 1.

---

## 2. Inventário de testes existentes

### 2.1 Self-tests Python (`tools/test_*.py`) — confirmados 5, como o brief antecipa

Corridos com `python3 tools/test_<nome>.py` a partir da raiz do repo, `python3` = versão do
sistema (ver §5, **3.9.6**):

| Ficheiro | Como corre hoje | Resultado observado (2026-07-24) | Ligado a runner? |
|---|---|---|---|
| `tools/test_audit_fallthrough.py` | `python3 tools/test_audit_fallthrough.py` | **PASS** (exit 0) | Não — nenhum script/CI o invoca |
| `tools/test_find_functions_prologue_corroboration.py` | idem | **PASS** (exit 0) | Não |
| `tools/test_objdump_audit.py` | idem | **PASS** (exit 0) | Não |
| `tools/test_ppu_lift.py` | idem | **FAIL** — `tools/test_ppu_lift.py:905`: `subprocess.run(["cmd", "/c", bat], ...)` → `FileNotFoundError: [Errno 2] No such file or directory: 'cmd'` em macOS/Linux (chama MSVC via `cmd /c` sem nenhum gate de plataforma — `grep -n "sys.platform\|platform.system\|os.name" tools/test_ppu_lift.py` não devolve nada) | Não |
| `tools/test_recomp_config.py` | idem | **FAIL** sob Python 3.9.6 — `tools/recomp_config.py:19`: `import tomli as tomllib` → `ModuleNotFoundError: No module named 'tomli'` (o `tomli` não está instalado nem no Python de sistema nem no `.venv`; sob `.venv/bin/python` (**3.14.6**, tem `tomllib` nativo) o mesmo teste dá **PASS**) | Não |

Nenhum destes 5 tem `pytest.ini`/`setup.cfg`/`tox.ini`/CI a agregá-los — confirmado por
`find . -iname pytest.ini -o -iname tox.ini -o -iname setup.cfg -o -iname pyproject.toml`
(sem resultados fora de `build-macos/`) e por não existir `.github/` no repo. Rodar os 5
hoje é 100% manual, um a um.

Nota: `tools/test_ppu_lift.py` também escreve um ficheiro fora do controlo do teste antes de
falhar — `wrote /Users/.../ps3recomp/scratch/ppu_conformance.cpp: 1000 cases` — usando um
caminho absoluto calculado a partir de `ROOT` (não relativo/temp). Isto foi limpo depois de
observado (ver nota de reprodutibilidade no fim desta secção).

### 2.2 `tools/lift_selftest.py` e `tools/spu_lift_selftest.py`

Ambos exigem o cross-compiler proprietário da SDK PS3 (`ppu-lv2-gcc.exe` / `spu-lv2-gcc.exe`,
via `--ppu-gcc`/`--spu-gcc` ou env `PPU_GCC`/`SPU_GCC`), **ausente** neste ambiente
(`which ppu-lv2-gcc ppu-lv2-gcc.exe spu-lv2-gcc spu-lv2-gcc.exe` → nenhum encontrado) —
não corridos além do `--help`/import.

- `tools/spu_lift_selftest.py --help` **funciona** (imports resolvem, argparse ok).
- `tools/lift_selftest.py --help` **falha já no import**, e de um jeito mais grave do que
  "path relativo à raiz": `tools/lift_selftest.py:29` faz `import elf_symbols`, mas
  **`elf_symbols.py` não existe em lado nenhum deste checkout**
  (`find . -iname "elf_symbols*"` só encontra referências dentro do próprio
  `lift_selftest.py`, nenhum ficheiro `elf_symbols.py`). Isto não é um bug de
  `sys.path`/cwd (correr de dentro de `tools/` dá o mesmo erro) — é uma **dependência em
  falta**. Rastreei a causa: `elf_symbols.py` foi introduzido no upstream
  `sp00nznet/ps3recomp` no commit `f369159` ("protos: unwrap debug fSELF EBOOTs for symbols
  + ground-truth function detection"), que **não é ancestral do nosso HEAD**
  (`git merge-base --is-ancestor f369159 HEAD` → não). O nosso `tools/lift_selftest.py` foi
  trazido por um merge upstream posterior (`git log --oneline -- tools/lift_selftest.py`
  aponta para `11a1c3c "fix(macos): recupera boot apos merge v0.7 — tools upstream sem
  runtime partido"`) e é **byte-idêntico** ao `tools/lift_selftest.py` do
  `upstream/master` (`diff` vazio) — mas esse merge não trouxe o `elf_symbols.py` que o
  ficheiro depende. É um gap de integração parcial de upstream, não um bug de path.

### 2.3 Harness SPU (`runtime/spu/tests/run_tests.sh`)

Corrido com `./runtime/spu/tests/run_tests.sh` (escolhe `clang` em Darwin, `.venv/bin/python`
se existir): **21 testes, 21 passaram, 0 falharam** (saída real: `Results: 21 passed, 0
failed`). Lista dos 21: `adapter`, `bringup`, `brsl_return`, `dma`, `event`, `lfqueue`,
`shufb`, `spu_channel_blocking`, `spu_task_abi`, `sum`, `taskset`, `workload` (12 do loop
gerador `gen_test_*.py`+`test_*_main.c`), mais `spurs_kernel`, `spurs_kick_async`,
`seh_isolation`, `lfq_inflate`, `spu_fma_double`, `spurs_e2e` (6 via `run_extra`, linkam
`libs/spurs/tests/*.c` directamente), mais `spu_fma_precision_off`/`spu_fma_precision_on`
(2 fases do mesmo binário) e `spu_channel_tsan` (ThreadSanitizer, condicional a
`-fsanitize=thread` estar disponível). **Não está ligado a nenhum CTest/CI** — é chamado
manualmente ou por `scripts/smoke_macos.sh` (ver §2.5).

⚠ **Efeito colateral observado ao correr esta suite**: regenerar os testes reescreve, no
lugar, 11 pares tracked `runtime/spu/tests/out_<nome>/spu_recomp.{c,h}` para bater com a
saída **actual** de `tools/spu_lifter.py` — `git diff --stat` acusou **22 ficheiros
alterados, 261 inserções(+), 74 remoções(-)** antes de eu reverter (`git checkout --`) para
não deixar o working tree sujo. Ou seja: os fixtures liftados que estão *commitados* no
repo estão **desactualizados** em relação ao lifter actual — rodar a suite hoje já não é
"no-op determinístico" no sentido de deixar a árvore limpa; ela corrige silenciosamente o
drift dos fixtures a cada execução. Também cria `runtime/spu/tests/out_spu_channel_blocking/`
(dir tracked ausente até correr a suite). Revertido antes de terminar esta task (ver nota
final).

### 2.4 Testes PPU (`runtime/ppu/tests/`)

| Ficheiro | Estado |
|---|---|
| `runtime/ppu/tests/test_fs_movie_io.cpp` | **Ligado**: compilado/corrido por `scripts/smoke_macos.sh:65-69` |
| `runtime/ppu/tests/test_ppu_lifter_emit.py` | **Órfão de runner**, mas standalone e corrível: `python3 runtime/ppu/tests/test_ppu_lifter_emit.py` → **PASS** ("Results: OK (0 failure(s))"), auto-contido (compila um harness próprio, não precisa de ELF de jogo). Não é chamado por nenhum script. |
| `runtime/ppu/tests/test_hle.cpp` | **Órfão** — 0 referências fora do próprio ficheiro (`grep -rl`); build manual documentado no cabeçalho (`g++ ... test_hle.cpp ../ppu_hle.cpp`), não corrido (requer `<lift_out>/ppu_recomp.h` de um jogo liftado) |
| `runtime/ppu/tests/test_hle_integration.cpp` | **Órfão** — 0 referências em lado nenhum, nem no próprio grep de nome |
| `runtime/ppu/tests/test_loader.cpp` | **Órfão** — o próprio cabeçalho diz que carrega "the real Uncharted EBOOT image" (outro título, não GoW2); `docs/PPU_RECOMP.md:153` documenta-o como "loader smoke test (real EBOOT)" mas nenhum script o corre. Impossível de correr aqui (nem GoW2 nem Uncharted ELF presentes). |
| `runtime/ppu/tests/test_ppu_blrl_call.cpp` | **Órfão** — 0 referências fora do ficheiro |
| `runtime/ppu/tests/test_ppu_fpu_lmw.cpp` | **Órfão** — 0 referências fora do ficheiro |
| `runtime/ppu/tests/boot_main.cpp` | Não é bem um "teste": é o scaffold de boot real ligado por `games/gow2/build_boot_gow.sh:6,14` e `games/gow2/build_boot_hle.sh:6,22` (Windows) — mistura teste/harness com build de produto, vive em `tests/` mas está no caminho de build do GoW2 |

### 2.5 `libs/spurs/tests/`, `libs/system/tests/`, `runtime/prx/tests/`, mais `libs/video`/`libs/audio`/`libs/codec`

| Ficheiro/dir | Estado |
|---|---|
| `libs/spurs/tests/test_spurs_kernel_main.c` | **Ligado** — `run_tests.sh:120-121` (`run_extra spurs_kernel ...`) |
| `libs/spurs/tests/test_spurs_kick_async_main.c` | **Ligado** — `run_tests.sh:127-129` |
| `libs/spurs/tests/test_lfq_inflate_main.c` | **Ligado** — `run_tests.sh:150-154` |
| `libs/system/tests/test_autoload_map.c` | **Órfão** — 0 referências em qualquer script/doc. Tentei o build manual do próprio cabeçalho (`cc ... test_autoload_map.c build-macos/.../cellSaveData.c.o -o ...`): **falha a linkar hoje** — `ld: symbol(s) not found for architecture arm64` (faltam `_vm_base`, `_dispatch_func_stat` e outros símbolos do runtime que o `.o` isolado não resolve). O procedimento documentado no próprio ficheiro não é suficiente tal como está. |
| `runtime/prx/tests/test_prx_loader.cpp` | **Órfão** — 0 referências em qualquer script/doc; o próprio cabeçalho exige um checkout externo `$FLOW` ("flOw checkout") + um binário `libsre.linked.bin` relocado — nenhum dos dois existe neste ambiente. Nem é sobre GoW2. |
| `runtime/prx/tests/libsre_externs_stub.cpp` | **Órfão**, companheiro do acima |
| `libs/video/tests/test_movie_io.c` | **Ligado** — `scripts/smoke_macos.sh:47-52` |
| `libs/video/tests/test_fios_aread_hle.c` | **Ligado** — `scripts/smoke_macos.sh:86-92` |
| `libs/video/tests/run_backend_smoke.sh` | **Ligado** (gated `--no-gui`) — `scripts/smoke_macos.sh:119` |
| `libs/video/tests/build_gpu_deswizzle.sh`, `build_mesh_spin.sh`, `build_mesh_spin_tex.sh`, `build_metalfx_upscale.sh`, `build_native_loader.sh`, `build_replay_mac.sh`, `build_replay.cmd` | **Órfãos** — cada um só é referenciado por si mesmo/pelo seu par `test_*`/`gen_*`; nenhum script os agrega |
| `libs/codec/tests/test_cellvdec_force_gate.c` | **Ligado** — `scripts/smoke_macos.sh:110-115` |
| `libs/audio/tests/build_coreaudio_ring.sh` (+ `test_coreaudio_ring.c`) | **Órfão** — 0 referências fora do próprio par |

### 2.6 Rodar `scripts/smoke_macos.sh` (único runner agregador que existe hoje)

Corrido: `./scripts/smoke_macos.sh --no-gui` (2026-07-24, neste HEAD limpo, macOS/arm64):

```
=== 1. configure + build ===
  ok: libps3recomp_runtime.a (arm64)
=== 2. suite SPU ===
  ok: Results: 21 passed, 0 failed
=== 3. movie io (caminho de dados do WAD) ===
Undefined symbols for architecture arm64:
  "_cellGcmMainloopNote", referenced from: _movie_io_open in movie_hle-....o
  "_movie_vt_autostart_from_cache", ...
  "_movie_vt_overlay_done", ...
  "_movie_vt_start", ...
  "_movie_vt_stop", ...
ld: symbol(s) not found for architecture arm64
FAIL: test_movie_io nao compilou
```
O script falha e sai (`set -euo pipefail`) no passo 3. Causa: `scripts/smoke_macos.sh:47-50`
compila `test_movie_io.c` + `libs/video/movie_hle.c` sozinhos, mas `movie_hle.c` (neste HEAD)
já chama `cellGcmMainloopNote` (definido em `libs/video/cellGcmSys.c`) e as funções
`movie_vt_*` (definidas em `libs/video/movie_vt_metal.m`) — nenhum dos dois está na lista de
fontes do passo 3. **Confirmado que isto é o estado do HEAD desta branch, não um resíduo de
working tree**: `git status -sb` estava limpo antes de eu correr o script, e
`git diff --stat -- libs/video/movie_hle.c` está vazio. Ou seja: o único script "runner
agregado" que o repo tem hoje **não passa** neste checkout — os passos 4+ (backends RSX)
nunca chegaram a correr. Isto por si só é evidência forte do problema que a Fase 0 quer
atacar (scripts de teste que ficam quebrados silenciosamente por drift entre módulos).

### 2.7 Resumo de órfãos (não corridos por nenhum runner automático)

`tools/test_ppu_lift.py`*, `tools/test_recomp_config.py`* (*ambos correm manualmente, mas
falham — ver §2.1), `tools/lift_selftest.py`, `tools/spu_lift_selftest.py`,
`runtime/ppu/tests/test_ppu_lifter_emit.py` (corre e passa, mas sem runner),
`runtime/ppu/tests/test_hle.cpp`, `test_hle_integration.cpp`, `test_loader.cpp`,
`test_ppu_blrl_call.cpp`, `test_ppu_fpu_lmw.cpp`, `libs/system/tests/test_autoload_map.c`,
`runtime/prx/tests/*`, a maioria de `libs/video/tests/build_*.sh`,
`libs/audio/tests/build_coreaudio_ring.sh`.

---

## 3. Violações de fronteira genérico ↔ específico do jogo

Cada linha abaixo foi confirmada com `grep -n` neste HEAD. Classificações:
**EA** = endereço-hardcoded, **FSM** = FSM/hook específico do jogo, **PROBE** = probe de
diagnóstico gated (env var, OFF por default), **FB** = fallback de módulo.

| Ficheiro:linha | O que é | Classificação |
|---|---|---|
| `runtime/ppu/ppu_loader.cpp:652-655` | `cc9d0_trace_read`: `switch` literal em `0x4066D79C/0x4066D7EC/0x4066D808/0x4066D858` (campos `f4`/`f54` dos dois objectos-shell TYPE15 do GoW2) | EA + PROBE (`PS3_TRACE_CC9D0`) |
| `runtime/ppu/ppu_loader.cpp:1021-1024`, `1131-1137` | Mesmos 4 endereços, watch de escrita (`PS3_TRACE_TYPE15_WRITES`) | EA + PROBE |
| `runtime/ppu/ppu_loader.cpp:683,741` | Watch de leitura em `[0x4C5D70,0x4C5D84)` (config de heap CRT) | EA + PROBE (`PS3_TRACE_XLEN`) |
| `runtime/ppu/ppu_loader.cpp:698-713,1308-1313` | Histograma/dump de `0x884F00` (região de controlo de fila) | EA + PROBE (`spinread_on()`) |
| `runtime/ppu/ppu_loader.cpp:1016-1019` | Watch de escrita em `[0x86E0E0,0x86E120)` ("ready byte" + "historical orphan-wait flag") | EA + PROBE (`PS3_TRACE_STG`) |
| `runtime/ppu/ppu_loader.cpp:726-736` | Watch de leitura em `0x00539C1C/0x00510AB4/0x00510B3C` (tabela estática de nomes WAD: `R_PermA`/`r_perma`/`r_shella`) | EA + PROBE (`PS3_WATCH_SHELLA`) |
| `runtime/ppu/ppu_loader.cpp:747-751` | Watch de leitura em `0x572BB0/0x572BB4/0x572BB8/0x881974/0x881994/0x881998` (config de heap) | EA + PROBE (`PS3_TRACE_HEAPCFG`) |
| `runtime/ppu/ppu_loader.cpp:617-624,678-681` | `g_movie_eos_ea`: variável global que, quando != 0, faz `vm_read8` devolver `1` no endereço apontado — replay do sinal de fim-de-vídeo do movie player para o FSM do jogo avançar (estado 1→3→5→11→done→carrega WADs) | FSM (hook sempre activo, não gated por env var — é o mecanismo real de Parede A) |
| `runtime/ppu/tests/boot_main.cpp:369` | `g_movie_eos_ea = obj + 0x744;` — arma o hook acima com offset `+0x744` de um objecto do jogo | FSM (fica em `tests/` mas é o ponto de armamento real, ligado ao build de produto do GoW2) |
| `runtime/syscalls/sys_ppu_thread.c:251-263` | Stub de `sys_ppu_thread_create` para thread chamada `"AUTO_LOAD"` **ou** `entry == 0x00521768` — evita SIGSEGV conhecido pós-B71; gated por `PS3_AUTO_LOAD_RUN` | EA + FSM (o próprio comentário chama-lhe "stub", e é o comportamento *default*, não um probe passivo) |
| `libs/spurs/cellSpurs.c:265-275` | Comentário + lógica: "GoW2 post-AUTO_LOAD waits in func_00454C60 until ... readyCount==0"; decremento de `readyCount` glosado ao comportamento esperado do GoW2 | FSM |
| `libs/spurs/cellSpurs.c:606-625` | "GoW2: cellSpursInitialize(WithAttribute) for this instance is ... lazy-init workaround applied" | FSM |
| `libs/spurs/cellSpurs.c:1125-1132` | `cellSpursReadyCountStore`: zera `readyCount` explicitamente citando "GoW2's post-AUTO_LOAD wait (00454C60) livelocks" se não o fizer | FSM |
| `libs/spurs/cellSpurs.c:394` | Comentário: "salta o spu_task_ls_setup e a task-lib do GoW2 (spu0 0xE2B0)" | EA + FSM |
| `libs/video/movie_hle.c:242-247,330` | Mapeamento de paths `"/_movies/NAME.m2v"` específico de como o GoW2 embrulha vídeos em `gow2.psarc`; comentário cita "the intro .m2v often never hits movie_io_open (only gow2.psarc does)" | FSM + FB (fallback de path do módulo de vídeo) |
| `libs/video/movie_hle.c:113` | "GoW2 ships each movie as .m2v + sibling .wav" — decisão de formato de áudio companheiro hardcoded ao jogo | FSM |
| `libs/codec/cellVdec.c:156,174,211` | Comentários "GoW2's player retrieve only 1 of ~300 offered frames", "GoW2 opens the decoder and StartSeq's it", "GoW2 smlogo is 1280x720" (fallback de geometria) | FSM + EA (geometria fixa 1280x720) |
| `libs/codec/cellVdec.c:573-593` | "Re-Play wall": após `StartSeq_count>=2` salta o PICOUT real do guest citando o comportamento específico do GoW2; gated por `PS3_VDEC_REPLAY_PIC` | FSM + PROBE |
| `libs/codec/cellVdec.c:450-486` | `vdec_force_seqdone` / `PS3_VDEC_FORCE_SEQDONE_MS`: força fim-de-sequência após N ms para destravar o FSM do GoW2 | FSM + PROBE (default OFF, mas descrito como necessário para o jogo avançar) |
| `libs/system/sysPrxForUser.c:506,511` | "GoW2's heap malloc uses destroy(EBUSY) as a liveness probe"; "TEST: GoW2 heap-alive probe" | FSM |

### Totais por classificação (das 20 linhas/clusters acima)

| Classificação | Ocorrências nesta tabela |
|---|---:|
| `endereço-hardcoded` (EA) | 8 |
| `FSM/hook específico do jogo` | 13 |
| `probe de diagnóstico gated` | 9 (sobrepõe-se com EA nalgumas linhas) |
| `fallback de módulo` | 2 |

Nota honesta sobre completude: esta tabela **confirma e expande** as pistas do brief, mas
**não é exaustiva**. Uma busca ampla por literais hex de 6-8 dígitos em
`runtime/ppu/ppu_loader.cpp` (`grep -oE '0x[0-9A-Fa-f]{6,8}[uU]?' runtime/ppu/ppu_loader.cpp
| sort -u | wc -l`) devolve **200 literais distintos**; a tabela acima cobre os clusters mais
documentados (com comentário explícito citando GoW2) — não tentei classificar os 200 um a
um (fora do orçamento desta task; fica para a auditoria sistemática já listada no
`CLAUDE.md` como pendente). Também há 89 ocorrências de `PS3_TRACE_` no mesmo ficheiro,
cobrindo pelo menos 41 nomes de env var distintos (`grep -oE 'PS3_TRACE_[A-Z0-9_]+'
runtime/ppu/ppu_loader.cpp | sort -u`) — todas as que inspeccionei estão gated
(`getenv(...)`, default OFF), consistente com a regra 6 do `CLAUDE.md`.

---

## 4. Realidade de plataformas

### Macros de plataforma (contagem em `runtime/*`, `libs/*`, `tools/*` tracked, `git grep -c`)

| Macro | Ficheiros que a usam | Ocorrências totais |
|---|---:|---:|
| `_WIN32` | 67 | 408 |
| `__APPLE__` | 14 | 30 |
| `__linux__` | 2 | 3 |

Os únicos 2 sítios com `__linux__` são `libs/video/cellGcmSys.c:25,966` e
`runtime/spu/spu_workload.c:170` — cobertura mínima e nunca exercitada por CI (não há CI).

### CI

**Não existe.** Confirmado: sem `.github/` no repo
(`find . -iname "*.yml" -o -iname "*.yaml"` não encontra nada fora de `build*/`; `ls -la
.github` → "no .github dir"). Nenhum build Linux/Windows verificado neste checkout — os
únicos directórios de build presentes (`build/`, `build-macos/`) são ambos builds locais
**macOS/arm64** neste mesmo host Darwin (`grep CMAKE_SYSTEM_NAME build*/CMakeCache.txt` não
devolve nada — sem cross-compile, ambos nativos). Há histórico de commits mencionando
fixes de compilação Linux/GCC (ex.: `963fd44 libs: fix remaining GCC/Linux compile errors`),
mas nenhuma evidência de um build Linux **verificado neste momento** — não medido, porque
não há toolchain Linux neste ambiente.

### `CMakeLists.txt`

- `PS3RECOMP_BUILD_TESTS` é declarado em `CMakeLists.txt:24` (`option(PS3RECOMP_BUILD_TESTS
  "Build unit tests" OFF)`) mas **nunca usado** no resto do ficheiro
  (`grep -n PS3RECOMP_BUILD_TESTS CMakeLists.txt` só devolve essa linha). Não há
  `enable_testing()`, `add_test(`, nem qualquer `if(PS3RECOMP_BUILD_TESTS)` — a flag existe,
  mas não liga nada.
- `CMakeLists.txt:114-115` exclui explicitamente `HLE_SOURCES` que casem `/tests/` (harnesses
  têm o seu próprio `main()`).
- `CMakeLists.txt:122-129` exclui `RUNTIME_SOURCES` que casem `/tests/|/runtime/ppu/|/runtime/host/`.
  O comentário no próprio ficheiro (linhas 123-128) confirma e explica: `runtime/ppu/` e
  `runtime/host/` são "compiled per-game against the lifter-generated ppu_recomp.h (and the
  game's RSX/D3D12 backend), not as part of the game-agnostic runtime library" — ou seja,
  `runtime/ppu/*.o` **não entra** em `libps3recomp_runtime.a`; é ligado à parte por
  `games/gow2/build_boot_gow.sh`/`build_boot_hle.sh` (consistente com a nota do `CLAUDE.md`
  sobre isto). Confirmado.

---

## 5. Realidade de Python

- **`python3` do sistema**: `Python 3.9.6` (`python3 --version`, Xcode Command Line Tools,
  `/usr/bin/python3`).
- **`.venv/bin/python`** (existente no repo, não gitignored o link mas o conteúdo sim):
  `Python 3.14.6` (`/opt/homebrew/opt/python@3.14/...`).
- **Sintaxe PEP 604 (`X | Y` em anotações)**: confirmada em pelo menos 10 ficheiros de
  `tools/` — `extract_spu_images.py:29`, `generate_stubs.py:207`, `audit_fallthrough.py`
  (6 ocorrências, ex. linha 513 `def main(argv: list[str] | None = None) -> int`),
  `elf_parser.py:614,629,631,634`, `find_functions.py:88,144-145`, `nid_database.py` (6
  ocorrências), `recomp_config.py` (4 ocorrências), `prx_analyzer.py:52,59`,
  `wrap_spu_elf.py:92`, `ppu_lifter.py:509,528,3219`.
- **`tomllib`**: `tools/recomp_config.py:17-19` — `try: import tomllib` (3.11+),
  `except ModuleNotFoundError: import tomli as tomllib`.
- **`tools/requirements.txt`** (idêntico ao `requirements.txt` da raiz): `pycryptodome>=3.19`,
  `capstone>=5.0`, `construct>=2.10`, `tabulate>=0.9`, `tomli>=2.0; python_version < "3.11"`,
  `tqdm>=4.65`. Nem `pip3 show tomli` nem `.venv/bin/pip show tomli` encontram o pacote
  instalado — mesmo o `.venv` (Python 3.14, que não precisaria dele por causa do marker)
  não o tem, e o Python de sistema (3.9.6, que precisaria) também não.

### Conflito concreto pedido pelo brief: `tools/wrap_spu_elf.py` falha em Python 3.9

Reproduzido: `python3 -c "import sys; sys.path.insert(0,'tools'); import wrap_spu_elf"`
sob o Python de sistema (3.9.6) →

```
File ".../tools/wrap_spu_elf.py", line 92, in <module>
    symbols: list[dict] | None = None) -> bytes:
TypeError: unsupported operand type(s) for |: 'types.GenericAlias' and 'NoneType'
```

Confirmado: `tools/wrap_spu_elf.py` **não** tem `from __future__ import annotations` (ao
contrário de `recomp_config.py`, `audit_fallthrough.py` etc., que têm essa linha e por isso
só falham no *uso* de `tomllib`/outras features, não na sintaxe `X | Y` per se — a anotação
fica como string não avaliada). `wrap_spu_elf.py` avalia a anotação no momento da definição
da função (assinatura de `wrap(...)` na linha 92), o que só funciona em Python ≥ 3.10 sem o
`__future__` import.

---

## 6. Estado dos gates de qualidade do plano

Os 7 "Critérios globais de qualidade" (`docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md:32-41`):

| # | Critério | Verificável automaticamente hoje? | Porquê |
|---|---|---|---|
| 1 | Não houver instrução crítica desconhecida | **Não** | `tools/ppu_lifter.py:2847`: `return f"/* TODO: {mn} {insn.operands} */;"` — catch-all silencioso para instrução não tratada; não há taxonomia de cobertura nem gate de erro (linhas 1546, 2186, 2415 mostram histórico de casos que caíam neste TODO até serem tratados um a um) |
| 2 | Não houver import obrigatório sem resolução explícita | **Não** | `runtime/ppu/ppu_hle.cpp:170-180`: NID não resolvido → log rate-limited (`[hle] unresolved NID ...`) e **`ctx->gpr[3] = 0` (finge CELL_OK) e continua** — o oposto de falhar o build; `docs/MODULE_STATUS.md` é uma tabela mantida à mão (Not Started/Stubbed/Partial/Complete), não gerada nem imposta por build |
| 3 | Todos os chunks passarem por compilação e link | **Não** | Não existe orquestrador (`tools/ps3recomp_build.py` é da Fase 1, ainda não existe — `ls tools/ps3recomp_build.py` não encontra nada); compilação dos chunks liftados é feita por scripts ad-hoc por jogo (`games/gow2/build_boot_*.sh`), sem gate uniforme |
| 4 | Loader validar segmentos, BSS, OPD, TOC e relocations | **Parcial** | `tools/elf_parser.py` valida magia/tamanho mínimo (`:162,237,490,655` — `raise ValueError` em casos pontuais), mas não há validação de sobreposição de segmentos, alinhamento de OPD, limites de TOC, ou tipos de relocation não suportados como erro explícito (tarefas da Fase 2, ainda não feitas) |
| 5 | Testes de estado e concorrência passarem | **Parcial** | A suite SPU (`runtime/spu/tests/run_tests.sh`) cobre concorrência com ThreadSanitizer (`spu_channel_tsan`, PASS) e passa 21/21 hoje — mas é só SPU/SPURS; não há testes de estado PPU (contexto, TOC, CR, XER) nem de scheduler/threads (Fases 5/6, ainda não feitas) |
| 6 | Boot atingir o checkpoint definido pelo jogo sem acesso guest inválido não autorizado | **requer boot — não verificado** | Não há ELF do jogo nem o checkout irmão `../gow2_work` neste ambiente; impossível arrancar o GoW2 aqui |
| 7 | Resultado reproduzível a partir do manifesto | **Não** | Não existe conceito de manifesto (`manifest.json` é da Fase 1, ainda não existe); e mesmo a reprodutibilidade dos fixtures de teste já falha hoje na prática — ver o efeito colateral do `run_tests.sh` sobre `out_*/spu_recomp.{c,h}` documentado em §2.3 |

Honestamente: 5 dos 7 critérios são "não" hoje, 1 é "parcial" (item 4) e um outro "parcial"
por cobertura estreita (item 5); o item 6 não é sequer testável neste ambiente. Isto é o
ponto de partida esperado pela Fase 0 — nada disto é surpresa, é a baseline que a task pediu
para registar.

---

## Notas finais

- **Python-alvo decidido**: uma task posterior da Fase 0 vai elevar a versão mínima
  oficialmente para **Python 3.11** (decisão já tomada, fora do escopo desta task — aqui só
  se regista a realidade actual: sistema em 3.9.6, `.venv` em 3.14.6, ambos sem `tomli`
  instalado apesar do `requirements.txt` o declarar condicionalmente).
- **CI**: uma task posterior vai configurar CI macOS-bloqueante com Linux/Windows
  não-bloqueantes — aqui só se regista que **não existe CI nenhum** hoje.
- **Reprodutibilidade do próprio processo de auditoria**: correr `runtime/spu/tests/
  run_tests.sh` e `scripts/smoke_macos.sh` como parte desta auditoria alterou, como efeito
  colateral, 22 ficheiros tracked (`out_*/spu_recomp.{c,h}`) e escreveu para
  `scratch/ppu_conformance.cpp`/`.bat`/`.log` (via `tools/test_ppu_lift.py`). Todos estes
  efeitos foram **revertidos** (`git checkout --` nos fixtures, `rm` nos ficheiros de
  `scratch/`) antes de terminar esta task, precisamente para ilustrar o problema descrito
  em §2.3/§6-item-7: hoje, simplesmente *correr a suite de testes existente* não é uma
  operação idempotente sobre a árvore git.
