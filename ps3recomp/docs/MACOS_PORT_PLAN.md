# Plano de implementação — rodar ps3recomp / GoW2 no macOS

> **Data do diagnóstico:** 2026-07-19  
> **Máquina:** Apple Silicon (arm64), macOS 26.5, Apple Clang 21  
> **Branch:** `spurs-bringup`  
> **Repos do “rodar”:** ⚠️ **parcial** — testes SPU isolados passam; runtime library **não linka/compila** sem portabilidade; o port GoW2 **não roda** (Windows/D3D12 + dados do jogo ausentes).

---

## 1. O que tentamos e o que aconteceu

### 1.1 Ambiente

| Dep | Antes | Depois (instalado no diagnóstico) |
|-----|-------|-------------------------------------|
| CMake | ausente | 4.4.0 (Homebrew) |
| Ninja | ausente | 1.13.2 |
| SDL2 | ausente | `sdl2-compat` + `sdl3` |
| Python | system 3.x | ok (lifter tools) |
| capstone (pip) | ausente | necessário p/ regenerar SPU tests via lifter |

### 1.2 Runtime (`ps3recomp` CMake)

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Resultado:** configure OK; compile **falha** em vários arquivos POSIX que assumem Linux/glibc.

Erros **reproduzidos** neste Mac:

| Arquivo | Problema macOS |
|---------|----------------|
| `libs/filesystem/cellFs.c` | `st_atim`/`st_mtim`/`st_ctim` (Linux) — macOS usa `st_*timespec` (**fix local aplicado**) |
| `libs/spurs/cellFiber.c` | `ucontext` exige `_XOPEN_SOURCE`; API deprecada no Darwin |
| `libs/sync/cellSync2.c` | `pthread_mutex_timedlock` **não existe**; `sem_timedwait` **não existe**; `sem_init` deprecado (semáforos anônimos não suportados) |
| `runtime/syscalls/sys_mutex.c` | `pthread_mutex_timedlock` |
| `runtime/memory/vm.h` | `MAP_NORESERVE` não existe no Darwin |
| `CMakeLists.txt` | não acha/linka SDL2 no non-Windows (include path manual com `-I/opt/homebrew/include`) |
| `libs/video/rsx_d3d12_backend.c` | só compila sob `_WIN32` (stub vazio no Mac) |
| `libs/video/rsx_null_backend.c` | stub non-Windows retorna `-1` (sem janela) |

### 1.3 Testes SPU (trabalho SPURS bring-up)

Com outputs pré-gerados em `runtime/spu/tests/out_*`:

| Teste | Resultado no Mac |
|-------|------------------|
| `test_sum` | ✅ PASS |
| `test_dma` | ✅ PASS |
| `test_shufb` | ✅ PASS |
| `test_brsl_return` | ✅ PASS |
| adapter / bringup / event / lfqueue / taskset / workload | ⛔ sem `out_*` pré-gerado (precisa `spu_lifter.py` + deps) |

**Conclusão:** o núcleo do lift/runtime SPU **já é nativo e correto em arm64 macOS**. O bring-up SPURS *em isolamento* é a parte mais portável do projeto.

### 1.4 Port GoW2 (`gow2-recomp`)

| Item | Estado no Mac |
|------|----------------|
| `EBOOT.ELF` / `extracted/` | **ausentes** (gitignored, copyright) |
| Lift PPU (`ppu_recomp_*.cpp`) | **ausente** (~1.3 GB regenerável no Windows) |
| `boot_v2_new.exe` | binário Windows; scripts `.cmd` / link com `-ld3d12 -lxinput` |
| `PS3_RSX_BACKEND=d3d12` | impossível no Mac |
| Janela / flip | depende de D3D12 ou null Win32 |

**Não é possível “rodar o jogo” neste checkout sem:** (1) dados do jogo, (2) re-lift PPU, (3) backend gráfico não-Windows, (4) scripts de build POSIX.

---

## 2. Objetivo e critérios de aceite

### Objetivo em camadas

| Camada | Meta | Aceite |
|--------|------|--------|
| **A — Runtime library** | `libps3recomp_runtime.a` builda no macOS arm64/x86_64 | `cmake --build build` verde |
| **B — SPU suite completa** | `runtime/spu/tests/run_tests.sh` 100% | todos os gen_test_* PASS |
| **C — Host boot headless** | linkar um `boot_gow` mínimo com null/SDL backend, sem GPU | processa CRT + syscalls, loga hang point |
| **D — Janela + input/audio** | SDL2 window + pad + audio | flip/clear color ou frame dump |
| **E — Render real** | RSX → Metal (ou Vulkan via MoltenVK) | pelo menos clear + draw colorido como no D3D12 phase-1 |
| **F — GoW2 jogável no Mac** | paridade com Windows bring-up atual | intro/WAD path com env canônico |

Este plano prioriza **A → B → C → D → E**. F depende do progresso de SPURS/RSX no branch atual (independente de OS).

---

## 3. Plano por fases

### Fase 0 — Toolchain e CI macOS (0.5–1 dia)

**Tarefas**

1. Documentar deps no `docs/BUILDING.md`:
   ```bash
   brew install cmake ninja sdl2 pkg-config
   python3 -m venv .venv && source .venv/bin/activate
   pip install -r requirements.txt tools/requirements.txt
   ```
2. Atualizar `CMakeLists.txt`:
   - `find_package(SDL2 REQUIRED)` em non-Windows (ou `pkg_check_modules`)
   - `target_include_directories` + `target_link_libraries(... SDL2::SDL2)`
   - `target_compile_definitions` para Apple se necessário (`_DARWIN_C_SOURCE`)
3. (Opcional) workflow GitHub Actions `macos-latest` com build library + SPU tests.

**Aceite:** configure + build da lib sem flags manuais `-I/opt/homebrew/include`.

---

### Fase 1 — Portabilidade POSIX/Darwin no runtime (2–4 dias) ← **bloqueio atual do build**

Criar um header de compatibilidade, p.ex. `include/ps3emu/host_compat.h`, e usá-lo nas syscalls/HLE:

#### 1.1 Timestamps de `stat` ✅ (já iniciado)

- Linux: `st_atim.tv_sec`
- macOS: `st_atimespec.tv_sec`
- Fallback: `st_atime`

Arquivo: `libs/filesystem/cellFs.c` (patch já no tree).

#### 1.2 `pthread_mutex_timedlock` (macOS não tem)

Polyfill:

```c
#if defined(__APPLE__)
static int ps3_pthread_mutex_timedlock(pthread_mutex_t *m, const struct timespec *abs) {
    for (;;) {
        if (pthread_mutex_trylock(m) == 0) return 0;
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        if (now.tv_sec > abs->tv_sec ||
            (now.tv_sec == abs->tv_sec && now.tv_nsec >= abs->tv_nsec))
            return ETIMEDOUT;
        struct timespec sl = {0, 1000000}; /* 1ms */
        nanosleep(&sl, NULL);
    }
}
#define pthread_mutex_timedlock ps3_pthread_mutex_timedlock
#endif
```

Arquivos: `runtime/syscalls/sys_mutex.c`, `libs/sync/cellSync2.c`.

#### 1.3 Semáforos em `cellSync2` (macOS sem unnamed sem)

Substituir `sem_t` + `sem_init`/`sem_timedwait` por:

- `pthread_mutex_t` + `pthread_cond_t` + contador, **ou**
- `dispatch_semaphore_t` (GCD) no branch `__APPLE__`.

Arquivo: `libs/sync/cellSync2.c`.

#### 1.4 `MAP_NORESERVE` em `vm.h`

```c
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif
```

Arquivo: `runtime/memory/vm.h`.

#### 1.5 `cellFiber` / ucontext

Opções (em ordem de preferência):

1. Compilar com `-D_XOPEN_SOURCE=600` **só** em `cellFiber.c` e validar em arm64 (ucontext em Apple Silicon é frágil).
2. Reimplementar fibers com `setjmp`/`longjmp` + pilhas manuais (recomendado no `PLATFORM_ABSTRACTION.md`).
3. Stub HLE: fibers cooperativas single-thread com queue (suficiente se GoW2 não depender de cellFiber real).

**Recomendação:** (3) para destravar build rápido + (2) se algum título usar cellFiber de verdade.

#### 1.6 SDL no CMake

Ver Fase 0.2.

**Aceite Fase 1:** `cmake --build build` produz `build/libps3recomp_runtime.a` em arm64.

---

### Fase 2 — Suite SPU completa no Mac (0.5–1 dia)

1. venv + `pip install capstone` (e deps do `tools/requirements.txt`).
2. Ajustar `run_tests.sh`:
   - `python` → `python3`
   - `GCC` default → `clang` no Darwin
3. Rodar `./run_tests.sh`.
4. Commitar (ou regenerar em CI) `out_*` se quiser offline.

**Aceite:** `Results: N passed, 0 failed` no Mac.

---

### Fase 3 — Backend gráfico “null” / SDL no Mac (3–5 dias) ← **bloqueio de janela**

Hoje:

```c
/* rsx_null_backend.c non-Windows */
return -1; /* Window backend not available */
```

**Implementar `rsx_sdl_backend` (ou estender null):**

| API | Comportamento |
|-----|----------------|
| `init(w,h,title)` | `SDL_CreateWindow` + renderer/texture |
| `pump_messages` | `SDL_PollEvent` (quit → -1) |
| `present` / clear | preencher texture com clear color do RSX |
| opcional | dump PNG por frame (`PS3_FRAME_DUMP=1`) — útil headless |

Pontos de integração:

- `cellGcmSys` flip path (já tem timestamps POSIX)
- env `PS3_RSX_BACKEND=sdl` (espelhar `d3d12` do launcher Windows)

**Aceite:** host de teste cria janela, limpa com a cor RSX, fecha limpo.

---

### Fase 4 — Build POSIX do GoW2 (3–7 dias, *após dados do jogo*)

Scripts Windows atuais (`build_d3d.sh`, `rodar_gow2.cmd`) linkam:

```
-ld3d12 -ldxgi -ld3dcompiler -lxinput -luser32 -lgdi32 -lbcrypt -lole32 ...
```

**Novo `recomp_mid_v2/build_macos.sh` (esboço):**

```bash
#!/usr/bin/env bash
set -euo pipefail
PS3="${PS3:-../../ps3recomp}"
# 1) garantir libps3recomp_runtime.a
# 2) compilar ppu_recomp_*.cpp + ppu_*.cpp + boot_main + gow2_spu_register
# 3) link: -lps3recomp_runtime -lSDL2 -framework Metal? (fase 5) -pthread
# 4) stack: -Wl,-stack_size,0x2000000
```

**Pré-requisitos de dados (local, nunca no git):**

1. Decrypt/extract PKG → `EBOOT.ELF`, `extracted/USRDIR`
2. Lift PPU: `ppu_lifter.py EBOOT.ELF --functions functions.json`
3. Reaplicar `patch_*.py` idempotentes
4. SPU lift já versionado em `spu_lifted/spu{0..3}_v2/`

> **⚠️ Os passos 2–3 acima estão desactualizados desde o marco v1.3.** O lift de
> produção passou a precisar de `--config config/gow2_recomp.toml` (mid-asm
> hooks, switch tables, weak overrides) e as correcções já migradas **não** se
> reaplicam à mão. A ordem completa — lift → `apply_all_patches.sh` →
> `verify_lift.sh` → build → smoke da intro → `accept_relift.sh`, e só depois
> promoção — está em **[`RELIFT_CANONICAL.md`](RELIFT_CANONICAL.md)**. Seguir
> essa sequência, não este esboço de 2026-07.

**Launcher `rodar_gow2.sh`:**

```bash
export PS3_MOVIE_HLE=1 PS3_NOMOVIES=1 PS3_PAD_AUTOSTART=1
export PS3_RSX_BACKEND=sdl   # não d3d12
export PS3_RSX_FIFO=1
export PS3_VFS_ROOT=../extracted/USRDIR
export PS3_CELLSYS_REORDER=1
./boot_gow ../EBOOT.ELF
```

**Aceite:** binário nativo arm64 sobe, loga o mesmo hang point que no Windows (hoje: cadeia SPURS / spin em estruturas SPU — ver `SPURS_BRINGUP.md` / `SPURS_M2_FINDINGS.md`), **sem crash de platform**.

---

### Fase 5 — RSX real no Mac: Metal ou Vulkan/MoltenVK (4–12 semanas)

Espelhar o que o D3D12 phase-1 já faz (`rsx_d3d12_backend.c`):

1. Device + swapchain (Metal layer / CAMetalLayer, ou Vulkan)
2. Clear color do RSX
3. Upload de vértices com BE byte-swap (reusar lógica D3D12)
4. PSO / pipeline vertex-color
5. Shader path: HLSL atual → **MSL** (Metal) ou SPIR-V (Vulkan); decompilers FP/VP já existem parcialmente

**Decisão de API**

| Opção | Prós | Contras |
|-------|------|---------|
| **Metal** | nativo, performance, Apple support | segundo backend além de D3D12; shaders MSL |
| **Vulkan + MoltenVK** | um backend “cross” Linux/Mac; alinhado ao roadmap (`RSX_GRAPHICS.md` “Vulkan planned”) | MoltenVK quirks; validação extra |

**Recomendação:** **Vulkan first** se o roadmap já aponta Vulkan; **Metal** se o único alvo Apple e quiser menos camadas.

**Aceite:** mesma geometry de teste do D3D12 (clear + DrawInstanced colorido) na janela macOS.

---

### Fase 6 — SPURS bring-up no Mac (paralelo ao Windows)

O trabalho da branch **não é bloqueado por OS** depois da Fase 1–2:

- Marcos M2.5–M5 em `docs/SPURS_BRINGUP.md` (kernel HLE, workload, event queue)
- Testes sintéticos em `runtime/spu/tests/`
- GoW2 real só depois de dados + boot host (Fase 4)

**Vantagem Mac:** Apple Silicon é rápido para iterar o interpretador/lift SPU; o path crítico SPURS pode ser desenvolvido **sem GPU**.

---

## 4. Ordem de ataque recomendada (checklist)

```
[ ] F0  CMake + SDL2 + docs deps
[ ] F1  host_compat: stat, timedlock, sem, MAP_NORESERVE, cellFiber
[ ] F1  libps3recomp_runtime.a builds on macOS
[ ] F2  run_tests.sh 100% green
[ ] F3  rsx_sdl_backend (window + clear + pump)
[ ] F4  restore game data offline; re-lift PPU; build_macos.sh
[ ] F4  boot headless/SDL reaches known hang (not platform crash)
[ ] F5  Metal or Vulkan present path
[ ] F6  continue SPURS M2.5+ (shared with Windows)
```

---

## 5. Estimativas (ordem de grandeza)

| Fase | Esforço | Risco |
|------|---------|-------|
| F0 Toolchain | 0.5–1 d | baixo |
| F1 Portabilidade Darwin | 2–4 d | médio (cellFiber, sem) |
| F2 SPU tests | 0.5–1 d | baixo |
| F3 SDL null/window | 3–5 d | baixo–médio |
| F4 GoW2 link POSIX | 3–7 d | médio (dados, patches, tamanho do lift) |
| F5 RSX Metal/Vulkan | 4–12 sem | **alto** (mesmo esforço do D3D12) |
| F6 SPURS kernel | 6–12 pessoa-meses | **alto** (já no FEASIBILITY) |

**Mac “útil para dev SPURS”:** F0–F2 (~1 semana).  
**Mac “vê janela GoW2”:** F0–F4 (~2–3 semanas + dados).  
**Mac “paridade visual Windows”:** +F5 (meses).

---

## 6. O que *não* fazer

- Não commitar `EBOOT.ELF`, `extracted/`, `.psarc`, SPU images brutas.
- Não forçar D3D12 via wine/proton como solução principal (frágil no Apple Silicon).
- Não bloquear SPURS HLE esperando Metal — SPURS é CPU/runtime e testa-se sem GPU.
- Não assumir glibc (`st_atim`, `pthread_mutex_timedlock`, `sem_init`) em código “POSIX genérico”.

---

## 7. Snapshot do estado após este diagnóstico

| Componente | macOS hoje |
|------------|------------|
| Tools Python (lifter) | ✅ usáveis |
| SPU recomp unit tests (pré-gerados) | ✅ 4/4 PASS |
| Runtime static lib | ❌ compile errors Darwin |
| Audio/Pad SDL | ⚠️ código existe; CMake não linka |
| RSX window | ❌ stub |
| RSX GPU | ❌ só D3D12 |
| GoW2 binary | ❌ Windows-only pipeline + sem dados locais |

**Resposta prática:**  
- **Sim**, o *trabalho atual de SPURS/SPU* pode (e já quase) roda no Mac.  
- **Não**, o *port GoW2 com janela D3D12* não roda sem as fases acima.
