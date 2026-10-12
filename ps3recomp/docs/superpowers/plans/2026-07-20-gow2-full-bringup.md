# GoW2 + ps3recomp Full Bring-up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Execute **one phase at a time**; do not start Phase N+1 until Phase N acceptance is green.

**Goal:** Fazer o God of War II HD (NPUA80491) **avançar de boot nativo (Mac/Windows) até um estado jogável mínimo** — menu/intro com frames reais — sobre o runtime `ps3recomp`, sem depender de emulação dinâmica tipo RPCS3.

**Architecture:** Dois repositórios. `ps3recomp` é o motor (lifter PPU/SPU, runtime lv2, HLE, RSX backends). `gow2-recomp` é o port do título (EBOOT liftado, patches, host de boot, dados locais, provas). O guest PPU já executa código recompilado; o hang atual é **subsistema SPU/SPURS ausente** + **RSX sem shaders**. Abordagem: fechar gaps de host/HLE medidos → kernel SPURS HLE com testes sintéticos → acoplar GoW2 → pipeline gráfico (Vulkan/Metal/D3D12) → assets/intro → polish.

**Tech Stack:** C17/C++20 runtime, Python tools (`ppu_lifter`, `spu_lifter`), CMake/Ninja, SDL2, D3D12 (Win), Metal/Vulkan+MoltenVK (macOS), gdb/lldb, env flags `PS3_*`.

**Estado medido (2026-07-20, pos-merge master):**

| Repo | Branch | Realidade |
|------|--------|-----------|
| `ps3recomp` | `macos-arm64-port-f0-f2` @ `aab18f4` | Merge `origin/master` **concluido**. Lib Mac builda. Fixes portabilidade em working tree (Phase 3.0). |
| `gow2-recomp` | `main` (+8) | `boot_gow2` **relinka** arm64; smoke criterios antigos **FAIL**. SpursInit x3 ainda ocorre. |
| Master / `games/gow2` | monorepo | WAD/pixels + ABI SPU v2 + kernel async |

**Causas do Phase 3 parado:** C1-C10 na Phase 3 replanejada (Win32 loader, TLS Apple, movie_hle, STB dup, smoke desatualizado, wall M2).

Documentos de apoio (não duplicar; estender):
- `FEASIBILITY.md` — veredito e esforço global
- `docs/SPURS_BRINGUP.md` — Marcos 0–5 SPURS
- `docs/GOW2_BOOT_STATE.md` / `gow2-recomp/SPURS_M2_FINDINGS.md` — wall empírico
- `docs/MACOS_PORT_PLAN.md` — F0–F5 host Mac (F0–F4 em grande parte feitos)

## Global Constraints

- **Nunca** commitar `EBOOT.ELF`, `extracted/`, `*.psarc`, `spu_images/`, `movie_cache/`, lift PPU gigante.
- Commits em português no `gow2-recomp`; mensagens claras no `ps3recomp`.
- Toda correção de boot precisa de **critério observável** (log marker, teste sintético, ou “CPU idle bloqueado vs busy-loop”).
- Preferir **HLE SPURS** a LLE do kernel da firmware (sem dependência de SELF de sistema).
- Windows e Mac devem partilhar o **mesmo** runtime; hosts só diferem em backend gráfico e link.
- Não “forçar flags” do guest sem prova (já crashou downstream) — ver M2.
- TDD nos paths sintéticos SPU/SPURS; GoW2 real só no fim de cada marco de runtime.

## File map (unidades de trabalho)

| Unidade | Paths principais | Responsabilidade |
|---------|------------------|------------------|
| Host Mac/Win | `gow2-recomp/boot_macos.cpp`, `build_macos.sh`, `rodar_gow2.sh`; Windows `recomp_mid_v2/*` | Contrato de boot = `runtime/ppu/tests/boot_main.cpp` |
| lv2 | `runtime/syscalls/*`, `lv2_register.c` | Syscalls reais (timer, event, spu group, fs) |
| HLE NIDs | `runtime/ppu/ppu_hle*.cpp`, `tools/gen_hle_nids.py` | Registro de ~977 handlers |
| SPURS HLE | `libs/spurs/cellSpurs.c`, `cellSpurs.h` | Init, workloads, kernel host-side |
| SPU exec | `runtime/spu/*`, `runtime/syscalls/spu_*.c` | LS, DMA, channels, lifted/interp jobs |
| RSX | `libs/video/cellGcmSys.c`, `rsx_commands.c`, `rsx_*_backend.*` | FIFO → GPU host |
| Lift tools | `tools/ppu_lifter.py`, `spu_lifter.py` | Offline recomp |
| GoW2 patches | `gow2-recomp/recomp_mid_v2/patch_*.py` | Correções de lift idempotentes |

---

## Phase 0 — Congelar a baseline (1–2 dias)

**Objetivo:** qualquer regressão futura é mensurável.

### Task 0.1: Smoke script canónico Mac

**Files:**
- Create: `gow2-recomp/smoke_boot_mac.sh`
- Modify: none (usa `boot_gow2` existente)

- [x] **Step 1:** Criar smoke que corre headless 30s e classifica o resultado

```bash
#!/usr/bin/env bash
# gow2-recomp/smoke_boot_mac.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
export PS3_NO_RSX=1 PS3_MOVIE_HLE=1 PS3_NOMOVIES=1 PS3_PAD_AUTOSTART=1
export PS3_RSX_FIFO=1 PS3_CELLSYS_REORDER=1
export PS3_VFS_ROOT="${PS3_VFS_ROOT:-$HERE/extracted/USRDIR}"
export PS3_TRACE_SPURS=1
LOG=$(mktemp)
./boot_gow2 EBOOT.ELF >"$LOG" 2>&1 &
PID=$!
sleep 30
kill -9 "$PID" 2>/dev/null || true
# Classificadores
grep -q '56244 lifted functions\|56[0-9][0-9][0-9] lifted' "$LOG" || { echo FAIL: no function table; exit 1; }
grep -q '151 imports' "$LOG" || { echo FAIL: imports; exit 1; }
grep -q '_SPU_printf_server' "$LOG" || { echo FAIL: no printf server thread; exit 1; }
grep -q 'cellSpursInitializeWithAttribute' "$LOG" || { echo FAIL: no SPURS init; exit 1; }
# Regressões proibidas
if grep -q 'lv2_syscall 141 (stub)' "$LOG"; then echo FAIL: usleep stub; exit 1; fi
if grep -c 'unresolved NID' "$LOG" | awk '{exit !($1>5)}'; then echo FAIL: NID spam; exit 1; fi
echo "PASS: baseline boot reaches SPURS wall (blocked, not stub-spin)"
```

- [x] **Step 2:** Correr e guardar log de referência

```bash
cd gow2-recomp && ./smoke_boot_mac.sh | tee /tmp/gow2_baseline_pass.txt
```

Expected: `PASS: baseline boot reaches SPURS wall`

- [x] **Step 3:** Commit no `gow2-recomp`

```bash
git add smoke_boot_mac.sh
git commit -m "test(gow2): smoke de baseline do boot macOS até o wall SPURS"
```

### Task 0.2: Smoke do motor (SPU + runtime Mac)

**Files:**
- Create: `ps3recomp/scripts/smoke_macos.sh` (ou reutilizar `runtime/spu/tests/run_tests.sh`)

- [x] **Step 1:** Documentar e automatizar

```bash
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake -B "$ROOT/build-macos" -G Ninja -DCMAKE_BUILD_TYPE=Release "$ROOT"
cmake --build "$ROOT/build-macos" -j"$(sysctl -n hw.ncpu)"
export GCC=clang
# python3 se python não existir
(cd "$ROOT/runtime/spu/tests" && ./run_tests.sh clang)
test -f "$ROOT/build-macos/libps3recomp_runtime.a"
echo PASS: runtime + SPU suite
```

- [x] **Step 2:** Correr — Expected: `PASS: runtime + SPU suite`

- [x] **Step 3:** Commit

```bash
git add scripts/smoke_macos.sh
git commit -m "test: smoke macOS do runtime e da suite SPU"
```

**Aceite Phase 0:** dois smokes verdes em máquina limpa com deps Homebrew.

---

## Phase 1 — Correções de host / HLE / lv2 (curto prazo, 3–7 dias)

**Objetivo:** zero “falso hang” de platform; Mac e Windows no **mesmo** contrato de boot.

### Task 1.1: Auditoria de paridade `boot_macos` ↔ `boot_main`

**Files:**
- Modify: `gow2-recomp/boot_macos.cpp` (só se divergir de novo)
- Reference: `ps3recomp/runtime/ppu/tests/boot_main.cpp`

Checklist obrigatório (já parcialmente feito em `a8749bd`):

| Contrato | Função / símbolo |
|----------|------------------|
| `lv2_init_syscalls()` | antes de `ppu_run` |
| `ppu_vm_size` | setado após commit VM |
| `g_ps3_guest_caller` | instalado |
| `g_ppu_thread_entry_trampoline` | instalado |
| `ppu_hle_register_all()` / nids fortes | link de `ppu_hle_nids.cpp` |
| stack top | `0x0FF00000` (não `0xD0xxxxxx`) |
| VM commit | `0..0x51000000` + faixa stacks |

- [x] **Step 1:** Diff mental/automático: extrair sequência de init de `boot_main.cpp` e comparar linha a linha com `boot_macos.cpp`
- [x] **Step 2:** Se faltar algo, portar **só** o gap; re-correr `smoke_boot_mac.sh`
- [x] **Step 3:** Commit se houver diff

### Task 1.2: Inventário de syscalls stub no boot GoW2

**Files:**
- Modify: `ps3recomp/runtime/syscalls/*` conforme achados
- Tool: env `PS3_TRACE_SC=1`

- [x] **Step 1:** Correr boot 15s com `PS3_TRACE_SC=1`, agregar contagens

```bash
PS3_TRACE_SC=1 PS3_NO_RSX=1 TIMEOUT=15 ./rodar_gow2.sh 2>&1 | tee /tmp/sc.log
grep '\[SC\]\|lv2_syscall.*stub' /tmp/sc.log | sed 's/.*num=//;s/ .*//' | sort | uniq -c | sort -rn
```

- [x] **Step 2:** Para cada número no top-10 com stub:
  - Mapear nome real (RPCS3 `lv2.cpp` / evidência de args, como feito no sc 144)
  - Implementar handler mínimo em `sys_*.c` + `lv2_syscall_register`
  - Re-medir: contagem do stub → 0

- [x] **Step 3:** Já feito exemplo: sc **144** = `sys_time_get_timezone` (`d24517d`). Manter o padrão: **medida → assinatura → implementação**.

- [x] **Step 4:** Commit por syscall (não um commit monólito)

**Aceite:** nenhum stub no hot path do boot até SPURS; smoke Phase 0 ainda PASS.

### Task 1.3: NID residual e `g_ctx`

**Files:**
- `tools/gen_hle_nids.py`, `runtime/ppu/ppu_hle.cpp`, libs sob `libs/`

- [x] **Step 1:** Com HLE nids linkados, confirmar `unresolved NID` ≤ 2 no boot (hoje ~0)
- [x] **Step 2:** Se aparecer NID novo no path Windows intro/WAD, adicionar handler real ou stub documentado com `printf` gated
- [x] **Step 3:** Corrigir bug do gerador `sceNpCommerce` duplicado (documentado em `a8749bd`) — excluir um dos `.c` no CMake ou unificar símbolos

### Task 1.4: Paridade Windows host ↔ Mac host

**Files:**
- `gow2-recomp/recomp_mid_v2/rodar_gow2.cmd`, `rodar_gow2.sh`

- [x] **Step 1:** Extrair env canónico partilhado para `gow2-recomp/env_gow2.sh` (sourced por sh e documentado no .cmd)
- [x] **Step 2:** Garantir mesmas flags: `PS3_MOVIE_HLE`, `PS3_NOMOVIES` / intro-skip set, `PS3_VFS_ROOT`, `PS3_RSX_FIFO`, `PS3_CELLSYS_REORDER`, `PS3_VDEC_ASYNC` quando aplicável
- [x] **Step 3:** Smoke Mac + nota no README

**Aceite Phase 1:** boot Mac/Windows classificam o mesmo wall funcional (SPURS / event queue / spin documentado), sem stubs de timer/NID.

---

## Phase 2 — SPURS HLE sintético (crítico, 4–12 semanas)

> **Estado 2026-07-20:** Phases 0 e 1 concluídas e verdes (smokes
> `gow2-recomp/smoke_boot_mac.sh` e `ps3recomp/scripts/smoke_macos.sh`).
> Correções ao mapa desta fase, medidas durante a execução:
> - `libs/spurs/cellSpurs.c` é **código morto** para os NIDs do SPURS. Os
>   handlers activos são os ctx-based de `runtime/ppu/ppu_sysprx.cpp`, que o
>   dispatch (`ps3_hle_call`) consulta antes da tabela genérica. Implementar o
>   kernel em `cellSpurs.c` não teria efeito nenhum.
> - `hle_cellSpursInitializeWithAttribute` já trata `spurs=0` correctamente
>   (a ABI handle-based do GoW2, ver `SPURS_M2_FINDINGS.md`); não é preciso
>   "não depender de r3", já não depende.
> - `spurs_unblock_crt_status()` é opt-in por `PS3_SPURS_STATUS_PTR` e o seu
>   MVP já foi **refutado** empiricamente (o byte já valia 3). Não repetir.

**Objetivo:** destruir o “wait órfão” com kernel HLE **testável sem GoW2**.

Referência: `docs/SPURS_BRINGUP.md` Marcos 2.5–4.  
Nota: nomes `spurs_set_kernel_thread` / `spurs_wake_kernel` aparecem no doc; **implementar** se ainda não existirem em `cellSpurs.c` (hoje `InitializeWithAttribute` só faz `memset`).

### Task 2.1: Modelo de dados do kernel HLE

**Files:**
- Modify: `libs/spurs/cellSpurs.h`, `libs/spurs/cellSpurs.c`
- Create: `libs/spurs/spurs_kernel.h`, `libs/spurs/spurs_kernel.c` (preferível isolar)

**Interfaces a produzir:**

```c
/* spurs_kernel.h */
typedef struct SpursKernelThread {
    int active;
    /* host pthread or parked SPU ctx slot */
    void *host_thread;
} SpursKernelThread;

void spurs_kernel_on_initialize(void *spurs_guest, uint32_t n_spus, uint32_t flags);
void spurs_kernel_on_finalize(void *spurs_guest);
void spurs_kernel_wake(void);   /* after AddWorkload / ready-count */
int  spurs_kernel_poll_once(void); /* run one schedule quantum; for tests */
```

- [x] **Step 1:** Escrever teste C em `runtime/spu/tests/test_spurs_kernel_main.c` que:
  1. Chama `spurs_kernel_on_initialize`
  2. Regista um workload fake
  3. Espera um flag host-side setado pelo “job”
- [x] **Step 2:** Ver o teste **falhar** (API inexistente)
- [x] **Step 3:** Implementar stub do kernel (loop vazio + wake condvar)
      — feito **sem** o wake: nenhum teste o exigia ainda (chega no Task 2.3).
- [x] **Step 4:** Ligar `cellSpursInitializeWithAttribute` → `spurs_kernel_on_initialize`
      — em `runtime/ppu/ppu_sysprx.cpp`, **não** em `libs/spurs/cellSpurs.c`.
- [x] **Step 5:** Commit `feat(spurs): scaffolding do kernel HLE + teste` (`dd3db1f`)

**Estado:** suite SPU 10 → 11. No boot real do GoW2 o guest já conduz o kernel:
`[spurs kernel] initialize(spurs=0x0, nSpus=2, flags=0x0)` ×3, fixado pelo
`smoke_boot_mac.sh`. O wall não mudou — e não devia: o kernel ainda não executa
nada nem escreve `0x86E118`. Isso é Task 2.2 em diante.

### Task 2.2: Materializar e correr um job sintético

**Files:**
- `runtime/spu/spu_workload.c`, `spu_lifted_job.h`, `spurs_kernel.c`
- Reusar: `wrap_spu_elf.py`, lifter, testes `out_sum` / `out_dma`

- [x] **Step 1:** Job sintético: ELF SPU que faz `MFC PUT` de `0xCAFEBABE` para EA conhecida (padrão `test_dma`)
- [x] **Step 2:** `cellSpursAddWorkload` (ou API interna de teste) enfileira entry + EA args
      — API interna `spurs_kernel_add_spu_workload(entry, args_ea)`.
- [x] **Step 3:** Kernel `wake` → `spu_run_lifted_job` **ou** interpretador, conforme image_id
      — via `spu_run_lifted_job`, no `poll_once` (o `wake` continua sem teste que o exija).
- [x] **Step 4:** Assert: `vm_read32(ea) == 0xCAFEBABE` (BE-aware)

**Aceite:** teste `test_spurs_job_dma` PASS sem GoW2. ✅ suite SPU 11 → 12.

### Task 2.3: Event queue completion path

**Files:**
- `runtime/syscalls/sys_event.c` (`sys_event_queue_push_by_id` se existir)
- `spurs_kernel.c`

- [x] **Step 1:** Teste: thread A `sys_event_queue_receive` (ou API C interna); thread B / kernel faz push após job
- [x] **Step 2:** A acorda com `data1/data2/data3` esperados
      — `source` configurável, `data1` = workload id.
- [x] **Step 3:** Commit

**Aceite:** “PPU bloqueado em receive acorda” (Marco 4 do SPURS_BRINGUP). ✅ suite SPU 12 → 13.

> O bloqueio no teste é real (queue vazia, timeout infinito) e ele assere que a
> thread ainda estava parada **antes** do poll — uma completion postada cedo
> demais não passaria por acidente.

### Task 2.4: Atomics SPU necessários ao scheduler

**Files:**
- `runtime/spu/spu_helpers.h` / interpretador ou path lifted
- Ops: `getllar`, `putllc`, `putqlluc` (se o job/kernel HLE não contornar)

- [ ] **Step 1:** Micro-teste SPU que faz lock com getllar/putllc
- [ ] **Step 2:** Implementar modelo mínimo (reserva por SPU + compare-swap na LS/MM)
- [ ] **Step 3:** Se o kernel for **puro HLE** (sem ELF de firmware), documentar contorno e pular interpretador para a fila — **mas** jobs do jogo ainda podem precisar dos atomics

### Task 2.5: Liga EnableSpuPrintf → queue q=1

**Files:**
- `cellSpurs.c` path `AttributeEnableSpuPrintfIfAvailable`
- Event queue id=1 usada por `_SPU_printf_server`

- [x] **Step 1:** No boot GoW2, confirmar q=1 fica em `BLOCKING` (já observado)
      — confirmado: `sys_event_queue_receive q=1 timeout=0 count=0 -> BLOCKING (infinite)`,
      chamado **uma vez**. O título nunca chama `cellSpursAttachLv2EventQueue`.
- [x] **Step 2:** Kernel/HLE, em eventos de printf SPU, faz push na q=1
      — `spurs_kernel_set_printf_queue`; o kernel drena o mailbox de interrupção
      do job e posta uma palavra por evento. Testado por `test_spurs_printf`.
- [x] **Step 3:** ~~Log da thread 1 deve sair do block ocasionalmente (mesmo com payload vazio no início)~~
      — **REFUTADO por experimento. Não fazer.**

> ### Experimento: acordar a q=1 com eventos vazios (2026-07-20)
>
> Hipótese do Step 3: postar eventos, mesmo vazios, destrava a thread do printf
> server e o boot avança.
>
> **Resultado: avança e depois crasha.** Com um pump temporário postando na q=1 a
> cada 50 ms, o boot passou de 36 para 47 linhas e chegou a montar threads de SPU
> (`[SPU] thread_set_argument` ×9, `tid=0xD0004xxx`) — endereços que caem na faixa
> de *pilhas* (`0xD0000000`), não num espaço de thread id plausível. Aos ~15 s o
> processo morre com **SIGBUS**:
>
> ```
> thread #2  EXC_BAD_ACCESS (code=2, address=0x3d0003f70)
>   frame #0  vm_write64
>   frame #1  func_00381C94
>   frame #2..#8  func_00381D24   <- recursão infinita, frames idênticos
> ```
>
> O guest aceita o evento fabricado, segue em frente sobre dados que não existem e
> entra em recursão sem fundo. É a Global Constraint deste plano a acontecer ao
> vivo: *"Não forçar flags do guest sem prova (já crashou downstream)"*.
>
> O pump foi revertido e **não** está no repo. O que ficou é o encanamento: quando
> código SPU real produzir printf, ele chega à queue. Enquanto nenhum job SPU rodar
> no boot do GoW2, a thread ficar bloqueada é o comportamento **fiel**.

**Aceite Phase 2 (pré-merge):** suite SPURS sintética verde na API *poll_once*.
**Pós-merge (`aab18f4`):** essa suite foi **substituída** pelo kernel async do master
(`spurs_kernel_start` / `submit` / `drain` + `libs/spurs/tests/test_spurs_kernel_main.c`).
Revalidar Phase 2 = correr os testes do **master**, não reintroduzir poll_once.
Task 2.4 (atomics) continua em aberto até jobs reais falharem por isso.

---

## Phase 3 — Voltar a correr e acoplar SPURS ao GoW2 (replaneado 2026-07-20)

**Objetivo:** (1) **host Mac verde de novo** sobre o tree mergeado, (2) **wiring SPU**
do título, (3) sair do wall M2 com hipóteses testáveis.

**Ordem obrigatória:** 3.0 → 3.1 → 3.3 → 3.2 → 3.4.  
Não investigar 3.2 em cima de binário que não linka ou smoke desatualizado.

### Task 3.0: Rebaseline pós-merge (Mac host + smokes) — **FAZER PRIMEIRO**

**Porquê:** o merge desbloqueou a ABI SPU correta mas partiu portabilidade Mac e
os critérios de smoke. Phase 3 sem 3.0 = trabalho em cima de areia.

**Files (fixes já rascunhados localmente — commitar):**
- Modify: `runtime/ppu/ppu_loader.cpp` — giant lock POSIX + TLS Apple + `g_ps3_rperma_full` stub + guards `GetModuleHandleW`
- Modify: `runtime/ppu/ppu_hle.cpp` — `g_last_hle_caller_*` como `__thread` (visível em C)
- Modify: `runtime/ppu/ppu_fs.cpp` — `stat` POSIX vs `_stat64`
- Modify: `libs/spurs/lfq_host_inflate.c` — remover 2º `STB_IMAGE_IMPLEMENTATION`
- Modify: `gow2-recomp/smoke_boot_mac.sh` — critérios alinhados ao master
- Modify: `gow2-recomp/build_macos.sh` — garantir `build-macos` = lib pós-merge

**Interfaces (kernel master — não reintroduzir API antiga):**

```c
void spurs_kernel_start(unsigned nSpus, spurs_dispatch_fn fn, void* user);
int  spurs_kernel_submit(const spurs_workload_job* job);
int  spurs_kernel_drain(unsigned timeout_ms);
void spurs_kernel_start_default(u32 nSpus); /* cellSpurs.c — chamado de ppu_sysprx */
```

- [ ] **Step 1:** Commitar os fixes C1–C6 no `ps3recomp` (mensagem: portabilidade Mac pós-merge master)
- [ ] **Step 2:** Rebuild limpo

```bash
cd ps3recomp
cmake -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"
# expected: libps3recomp_runtime.a
```

- [ ] **Step 3:** Rebuild host GoW2

```bash
cd gow2-recomp && ./build_macos.sh
# expected: boot_gow2 Mach-O arm64
```

- [x] **Step 4:** Atualizar `smoke_boot_mac.sh` — **deixar de exigir** strings da Phase 2 antiga:
  - Remover/relaxar: `[spurs kernel] initialize`, `_SPU_printf_server`, `THREAD 1] host thread started` se o master logar diferente
  - Exigir em vez disso (pós-merge medido 2026-07-20):
    - `56244 lifted` / `5[0-9]{4} lifted`
    - `151 imports`
    - `cellSpursInitializeWithAttribute` ≥ 3× (com `PS3_TRACE_SPURS=1`)
    - **ausência** de `lv2_syscall 141 (stub)` e NID spam
    - CPU low (blocked ou wait, não busy-spin de stub)
  - Adicionar: `spurs_kernel_running` ou log que `spurs_kernel_start_default` emitiu (grep no código o prefixo real e fixar no smoke)
- [x] **Step 5:** Correr smoke 30s — deve PASS com critérios novos
- [x] **Step 6:** Se `_SPU_printf_server` / trampolim de thread **sumiram** de verdade (regressão, não só log):
  - Diff `ppu_sysprx` EnableSpuPrintf + `sys_ppu_thread_create` path vs pre-merge `89475a7`
  - Restaurar criação da thread **ou** provar que o master Windows também não a cria neste ponto
- [x] **Step 7:** Commit smoke + notas `gow2-recomp/notes/2026-07-20-postmerge-rebaseline.md`

**Aceite 3.0:** `./build_macos.sh` + `./smoke_boot_mac.sh` verdes; lib e boot do **mesmo** tree mergeado.

### Task 3.1: Instrumentação M2 no Mac (já parcialmente feita)

**Files:**
- `gow2-recomp/attach_mem_mac.sh`, `notes/2026-07-20-baseline-mac-m2.md` ✅
- Revalidar **depois** de 3.0 com o binário novo

- [x] **Step 1:** `attach_mem` lldb (commit `6a4c955`)
- [x] **Step 2:** Baseline pre-merge: `*0x86E118=0`, `*0x881970=0x58585858`
- [x] **Step 3:** Repetir dump com `boot_gow2` pós-3.0; confirmar se o wall é o mesmo ou se o master já avançou
- [x] **Step 4:** Logar se `AddWorkload` aparece no boot (`PS3_TRACE_SPURS=1` / printf em `cellSpursAddWorkload`)

**Aceite 3.1:** nota atualizada com addresses + se houve `AddWorkload` count.

### Task 3.3: Wiring SPU do GoW2 (DESBLOQUEADA pelo merge — fazer antes de 3.2 profundo)

> Pre-merge estava **parqueada** (`notes/2026-07-20-task33-spu-wiring.md`) por ABI.
> Pós-merge: `spu_lifted/spu{0,1}_v2` **compila** contra o runtime novo. Resta **linkar**.

**Files:**
- `gow2-recomp/build_macos.sh` — etapa 3b (patch já documentado na note Task 3.3)
- `gow2-recomp/recomp_mid_v2/gow2_spu_register.c`
- `gow2-recomp/spu_lifted/spu{0..3}_v2/spu_recomp.{c,h}`
- Runtime: `runtime/spu/spu_workload.c` (registry por fingerprint)

- [x] **Step 1:** Aplicar o bloco `3b. imagens SPU` da note ao `build_macos.sh` (clang `-I` runtime/spu + register.o)
- [x] **Step 2:** Linkar `gow2_spu_register.o` + `spu?_v2_spu_recomp.o` no `boot_gow2`
- [x] **Step 3:** Smoke com `PS3_SPU_ALL=1` (ou flags da note); procurar log de register/dispatch
- [ ] **Step 4:** Se SIGBUS em job: validar ABI `spu_run_lifted_job_ex` (u128 em gpr[3]) vs entry lifted

**Aceite 3.3:** binário contém símbolos `spu0_*` / register; boot não regrede smoke 3.0.

### Task 3.2: Destravar o wall M2 (só com 3.0+3.3 verdes)

**Files / evidência:**
- `docs/GOW2_BOOT_STATE.md`, `SPURS_M2_FINDINGS.md`, `notes/2026-07-20-baseline-mac-m2.md`
- `libs/spurs/cellSpurs.c` `AddWorkload` → `spurs_kernel_submit`
- `runtime/ppu/ppu_sysprx.cpp` `spurs_kernel_start_default` no Initialize

**Árvore de hipóteses (testar em ordem):**

```
Boot chega a SpursInit×3?
  N → regressão host/HLE (voltar a 3.0/3.6)
  S → kernel_start_default correu? (spurs_kernel_running)
        N → ppu_sysprx não chama start / r3 spurs EA errado
        S → AddWorkload count > 0 no boot?
              N → jogo ainda não agenda; wall pode ser allocator (0x881970)
                  → trace stores em func_0031415C / imports heap (NID 0x1573DC3F etc.)
              S → submit corre mas image MISS no registry?
                    N → job corre; watch 0x86E118
                    S → 3.3 incompleto / fingerprint
```

- [x] **Step 1:** Contar `AddWorkload` no log de 30s (instrumentar se silencioso)
- [ ] **Step 2:** Hipótese A — zero workloads: não “acordar” event queues à força (já crashou — ver exp. Phase 2.5)
- [ ] **Step 3:** Hipótese B — allocator fill `XXXX`: trace quem deveria escrever `0x881970`; HLE/fs/sync
- [ ] **Step 4:** Hipótese C — workload corre mas não escreve flag: RE do writer de `0x86E118` no lift
- [ ] **Step 5:** Critério: main **sai** do spin (`sample`/`lldb` PC fora de `func_0030600C`) **sem** force-write da flag

**MEDIDO 2026-07-20 (macOS, pós-3.0/3.3) — o wall tem nome e forma exactos:**

A arvore de hipoteses percorrida com medicao: SpursInit x3 **SIM** → kernel
arrancou **SIM** (`[spurs kernel] started: 2 worker(s)`, 2 threads vistas no
`sample` em `pthread_cond_wait`) → `AddWorkload > 0` **NAO** (0, e `CreateTask`
tambem 0). Ou seja: **ramo "o jogo ainda nao agenda"**.

O que o `sample` do processo bloqueado mostra (nao inferido): a main thread esta
em `func_002B3CB0`, com **2419 de 2466 amostras** dentro de `sys_timer_usleep`
→ `nanosleep`. Nao e busy-spin (CPU ~1,5-2%), e polling com sleep. Cadeia:
`func_00010354 → func_0025C838 → func_002B4F04 → func_002B41C0 → func_002B3EA4
→ func_002B3CB0`.

Com `PS3_TRACE_SPURS=all` (traca TODAS as chamadas HLE), o loop e:

```
while (*0x86E118 == 0) { cellSysutilCheckCallback(); sys_timer_usleep(); }
```

**33 347 chamadas a `cellSysutilCheckCallback` em 22 s**, e mais nenhuma HLE
distinta nas ultimas 3000 linhas. A sequencia que precede o loop, uma unica vez
cada:

```
cellSpursInitializeWithAttribute x3   (r3 = 0x40000080 / 0x40001100 / 0x40002180)
cellSysmoduleLoadModule(0x3E)
cellGameBootCheck(0xFEFFC14, 0xFEFFC10, 0xFEFFC18)
cellGameContentPermit(0xFEFFC28, 0xFEFFCA8, 0xFEFFC18)
cellSysmoduleUnloadModule(0x3E)
→ cellSysutilCheckCallback para sempre
```

Refutacoes uteis (poupam rodadas):
- **Nao e caminho de erro do cellGame:** `hle_cellGameBootCheck` e
  `hle_cellGameContentPermit` devolvem ambos `r3 = 0` (OK). Verificado no codigo.
- **Nao esta a espera de um callback:** o titulo **nunca** chama
  `cellSysutilRegisterCallback` (0 ocorrencias no log completo). Nao ha callback
  registado para o `CheckCallback` despachar, logo o pump nao e o que destrava —
  a condicao de saida e a flag `0x86E118`, nada mais.
- **Nao e falta do kernel SPURS:** o kernel esta vivo e vazio. Como
  `AddWorkload = 0`, ligar as imagens SPU (Task 3.3) nao podia mover isto, e nao
  moveu — as imagens estao registadas e nunca sao despachadas.

**Consequencia para o Step 3 (hipotese B):** continua de pe e passa a ser a
principal. `*0x881970` mantem o fill `0x58585858` e o `AddWorkload = 0` e
coerente com "o allocator/scheduler do titulo nunca ficou pronto", nao com
"o SPURS falhou".

**Proximo passo concreto:** Step 4 — achar o writer de `0x86E118` no lift (o
ponteiro vem de `[0x541AD0]`, logo procurar pelo carregamento desse slot, nao
pelo literal).

### Task 3.4: Smoke “avançou além do wall”

```bash
# PASS só com evidência positiva nova, ex.:
# - AddWorkload count >= 1 e spurs_kernel_completed > 0
# - ou PC sample estável fora do loop de 0x86E118
# - e *0x86E118 != 0 sem patch manual
```

**Aceite Phase 3:** boot Mac (e preferencialmente Win) passa do wall M2; próximo hang **nomeado** com endereço + log; smokes 3.0 ainda verdes.

### Task 3.5 (paralelo leve): Suite SPURS master no Mac

**Files:** `libs/spurs/tests/test_spurs_kernel_main.c`, `runtime/spu/tests/test_spurs_e2e_main.c`

- [x] **Step 1:** Ligar estes testes ao `scripts/smoke_macos.sh` / CMake — feito via `runtime/spu/tests/run_tests.sh` (bloco `run_extra`), suite 10 → **12**
- [x] **Step 2:** Corrigir `test_adapter` / `test_workload` / `shufb` se ainda falharem na suite SPU genérica (7 pass / 3 fail medidos pós-merge)
- [x] **Step 3:** Não recriar `gen_test_spurs_job_dma` da API antiga — não foi recriado

---

### Definition of Done Phase 3 (checklist operacional)

| ID | Critério | Como verificar |
|----|----------|----------------|
| D0 ✅ | Tree mergeada + portabilidade Mac commitada | `git status` limpo; lib builda arm64 |
| D1 ✅ | `boot_gow2` linka arm64 | `./build_macos.sh` (114M, Mach-O arm64) |
| D2 ✅ | Smoke baseline pós-merge PASS | `./smoke_boot_mac.sh` PASS |
| D3 ✅ | SPU images no binário | `nm`: 4 imagens, 1940 funções; registo logado |
| D4 ✅ | Kernel HLE running no boot | `[spurs kernel] started: 2 worker(s)` + 2 threads no `sample` |
| D5 ✅ | Wall M2 ultrapassado | `*0x86E118` = `0x43005290` (era 0); AddWorkload 0→3; cellGcmSys 5561× — causa era lift desactualizado (`stwcx.` rA=0), não SPURS |
| D6 ✅ | Hang documentado | `notes/2026-07-20-postmerge-rebaseline.md` + Task 3.2 acima (loop nomeado) |

---

## Phase 4 — RSX: de FIFO a pixels reais (paralelo após Phase 2 início, 3–9 meses)

**Objetivo:** frames do jogo (não só clear color / dumps manuais).

### Task 4.1: Matriz de backends

| Backend | Plataforma | Meta imediata |
|---------|------------|---------------|
| D3D12 | Windows | Manter path de referência `recomp_mid_v2` |
| SDL null | Mac/Linux | Janela + clear + frame dump PNG |
| Metal | macOS | Clear + draw colorido phase-1 |
| Vulkan | Mac (MoltenVK) / Linux | Paridade com roadmap `RSX_GRAPHICS.md` |

- [ ] **Step 1:** Um único `rsx_backend` vtable; hosts só escolhem via `PS3_RSX_BACKEND`
- [ ] **Step 2:** Teste golden: clear color RSX → PNG hash estável (`PS3_FRAME_DUMP=1`)

### Task 4.2: Completar path de draw já semi-pronto

**Files:**
- `rsx_commands.c`, `rsx_d3d12_backend.c`, `rsx_vulkan_backend.c`, `rsx_metal_backend.m`
- `rsx_fp_decompiler.c`, `rsx_vp_decompiler.c`

Ordem interna (FEASIBILITY):

1. Vertex upload BE + DrawInstanced (D3D12 já tem pedaços)
2. `draw_indexed`
3. Texture upload + deswizzle
4. **Tradutor NV40 → HLSL/SPIR-V/MSL** (maior bloco)
5. PSO dinâmico, blend/depth, framebuffer resolve

- [ ] Cada sub-item = teste harness em `libs/video/tests/` + prova PNG no GoW2 quando possível

### Task 4.3: Trazer provas Windows → Mac

**Files:**
- Patches e recipes: `recomp_mid_v2/bt_intro_wads.sh`, `rodar_gow2_intro_skip.cmd`
- Create: `gow2-recomp/rodar_gow2_intro_skip.sh`

- [ ] **Step 1:** Portar env intro-skip (com `PS3_VDEC_ASYNC=1` obrigatório quando movies)
- [ ] **Step 2:** Reproduzir abertura de WAD `R_LglScA` / `R_PermA` no Mac
- [ ] **Step 3:** Frame dump comparado visualmente com `frame_wad_tex_*.png`

**Aceite Phase 4 (mínimo):** no Mac **ou** Windows, frame com conteúdo de WAD/legal screen via backend nativo, de forma reprodutível por script.

---

## Phase 5 — Assets, codecs, heap/free-list (contínuo, 1–4 meses)

**Objetivo:** o path que o Windows já explorou (Tasks 3.x free-list, type-map, OPD) fica estável e partilhado.

### Task 5.1: Catálogo de patches idempotentes

**Files:**
- `gow2-recomp/recomp_mid_v2/patch_*.py`
- Create: `gow2-recomp/apply_all_patches.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail
LIFT=${1:-recomp_macos}
for p in recomp_mid_v2/patch_*.py; do
  python3 "$p" --lift-dir "$LIFT" || python3 "$p"
done
```

- [ ] **Step 1:** Cada patch documenta pré-condição e como reverter
- [ ] **Step 2:** Re-lift PPU → apply_all → rebuild → smoke

### Task 5.2: cellVdec / cellAdec / movie HLE

**Files:**
- `libs/codec/cellVdec.c`, `cellAdec.c`, movie helpers

- [ ] Path intro: ou `PS3_NOMOVIES=1` estável, ou decode real via FFmpeg (opcional, Phase 5b)
- [ ] Não bloquear SPURS/RSX por causa de video

### Task 5.3: VFS / psarc

**Files:**
- `libs/filesystem/cellFs.c`, host path mapping
- `extracted/USRDIR/gow2.psarc`

- [ ] Garantir leituras de WAD no Mac com os mesmos paths do Windows
- [ ] Log gated `PS3_TRACE_FS=1` para opens que falham

**Aceite Phase 5:** recipe intro→WAD verde em Mac e Win com o mesmo script de métricas (`smoke_asset_pipeline.sh` portado).

---

## Phase 6 — Performance e lift SPU integrado (quando jobs reais existirem)

**Objetivo:** jobs SPURS em tempo útil.

### Task 6.1: Ligar `spu_lifter` output ao registry em runtime

**Files:**
- `spu_workload.c`, build system do jogo
- Já existe: `spu_run_lifted_job`

- [ ] Preferir lifted para spu0–3 do GoW2; interpretador só fallback
- [ ] Benchmark: job de compressão / gcm_spu vs meta de frame budget

### Task 6.2: Gaps do PPU lifter (FEASIBILITY §5.1)

**Files:**
- `tools/ppu_lifter.py`, `ppu_disasm.py`

Ops prioritárias se crash/divergência: `lmw/stmw`, `divd/divdu` dot, `lswi/stswi`, `mtfsf`

- [ ] Cada op: teste unitário de lift → C compile → resultado vs interpretador de referência ou RPCS3 log

---

## Phase 7 — “Funciona como deveria” (definição de done escalonada)

Não existe um único “pronto”. Usar **Definition of Done** por nível:

| Nível | Nome | Critério objetivo |
|-------|------|-------------------|
| **L0** | Host paridade | ✅ (2026-07-20) smoke Mac até SPURS wall, blocked, sem stub 141/NID spam |
| **L1** | SPURS sintético | Phase 2 testes verdes |
| **L2** | Boot GoW2 avança | Sai de `func_0030600C` / wall atual; próximo hang nomeado |
| **L3** | Intro/WAD | Recipe intro-skip abre WADs; métricas de asset pipeline |
| **L4** | Frame real | PNG/janela com geometria+textura do jogo via RSX backend |
| **L5** | Menu jogável | Input pad → navega menu; áudio básico |
| **L6** | Gameplay | Fase carrega; 30+ fps em hardware alvo (aspiracional) |

**“Tudo funciona como deveria” no sentido do utilizador final ≈ L5–L6.**  
**“Tudo funciona como deveria” no sentido de engenharia do projeto agora ≈ fechar L0→L2 primeiro.**

---

## Ordem de execução (DAG)

```
Phase 0 baseline ─────────────────────────────────────┐
Phase 1 host/lv2/HLE ─────────────────────────────────┤
         │                                            │
         ▼                                            │
Phase 2 SPURS sintético ──┬──► Phase 3 GoW2 SPURS     │
         │                │         │                 │
         │                │         ▼                 │
         │                │   Phase 5 assets ◄────────┤ (patches mid_v2)
         │                │         │                 │
         ▼                ▼         ▼                 │
Phase 4 RSX (paralelo cedo no D3D12; Mac depois L2)   │
         │                                            │
         ▼                                            │
Phase 6 perf / SPU lifted                             │
         │                                            │
         ▼                                            │
Phase 7 L4→L5→L6                                      │
```

**Regra de ouro:** não gastar semanas em shaders Mac enquanto L2 (boot avança) estiver vermelho — exceto manter D3D12 Windows estável.

---

## Estimativas honestas

| Phase | Calendário (1 dev sénior) | Risco |
|-------|---------------------------|-------|
| 0 Baseline | 1–2 dias | Baixo |
| 1 Host/HLE/lv2 | 3–7 dias | Baixo–médio |
| 2 SPURS sintético | 1–3 meses | **Alto** |
| 3 GoW2 SPURS | 0.5–2 meses | **Alto** |
| 4 RSX+shaders | 3–9 meses | **Alto** |
| 5 Assets/codecs | 1–4 meses (parcialmente paralelo) | Médio |
| 6 Perf | 1–2 meses | Médio |
| **L5 jogável** | **~12–30+ meses** | Confirma FEASIBILITY |

---

## Investigação contínua (não é “task de feature”, é método)

Em **cada** hang novo:

1. **Classificar:** busy-loop (CPU alta) vs block (event/mutex) vs crash (SIGSEGV/SIGBUS)
2. **PC / backtrace** (lldb/gdb) da main + threads guest
3. **Watch** no endereço de saída do loop (como `0x86E118`)
4. **Stub inventory** (`PS3_TRACE_SC`, unresolved NID)
5. **Uma hipótese** → teste que a refuta → só então patch
6. **Proibir** force-write de flags de jogo sem provar writers legítimos

Ferramentas já no tree: `sample_m2.sh`, `attach_mem.sh`, `PS3_TRACE_SPURS`, `PS3_TRACE_SC`, `PS3_TRACE_FS` (se existir).

---

## Riscos e mitigações

| Risco | Mitigação |
|-------|-----------|
| SPURS HLE incompleto vs kernel real | Testes sintéticos + traces do jogo; não LLE firmware |
| Interpretação SPU lenta | Lifted jobs (Phase 6) cedo nos hot images |
| Docs de syscall desatualizados | Confiar em `lv2_syscall_table.h` + medida; corrigir `SYSCALLS.md` |
| Mac ≠ Windows behavior | Smoke partilhado; mesmo runtime `.a`/fontes |
| Scope creep shaders | Congelar L2 antes de investir em MSL full |
| Dados de jogo no git | `.gitignore` + review de `git status` antes de commit |

---

## Self-review (cobertura)

| Requisito do utilizador | Phase |
|-------------------------|-------|
| Correções (host, lv2, NID, timezone, stubs) | 1 |
| Investigações (wall, watch, SC trace) | 0, 3, método contínuo |
| Melhorias (SPURS, RSX, assets, perf) | 2–6 |
| “Funcione como deveria” | DoD L0–L6 Phase 7 |
| Mac + gow2 + motor | Global, smokes 0–1 |
| Não recomeçar do zero | Reusa SPURS_BRINGUP, mid_v2, boot paridade |

---

## Handoff de execução

**Começar sempre por Phase 0 → Phase 1** (rápido, reduz ruído).  
**Trabalho principal de valor:** Phase 2 (SPURS sintético) em `ps3recomp` branch derivada de `macos-arm64-port-f0-f2` ou merge com `spurs-bringup`.

Dois modos de execução:

1. **Subagent-Driven** — um subagente por Task, review entre tasks  
2. **Inline** — esta sessão, com checkpoints no fim de cada Phase  

**Recomendação:** Phase 0–1 inline (horas); Phase 2+ subagent-driven com testes obrigatórios.
