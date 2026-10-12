# Plan 03 — SPU jobs & SPURS protocol (W8–W12) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: subagent-driven-development or executing-plans.

**Goal:** Jobs SPU do GoW2 (e sintéticos) **executam com ABI correta**, fazem DMA, respeitam atomics/filas quando necessário, e falhas de job **não derrubam o host**.

**Architecture:** `spurs_kernel_submit` → callback `spurs_pm_dispatch` / `spu_workload_dispatch` → `spu_run_lifted_job_ex` (gpr[3] = CellSpursTaskArgument u128). Imagens em `spu_lifted/*_v2`. Filas LFQueue + `lfq_host_inflate` cobrem o atalho EDGE-zlib até o pop protocol estar fiel.

**Tech Stack:** `spu_lifted_job.h`, `spu_workload.c`, `spurs_kernel.c`, `cellSpursLFQueue.c`, `lfq_host_inflate.c`, `runtime/spu/tests/*`, gen scripts Python.

**Depends on:** Plano 02 (registry + kernel start).  
**Unblocks:** Plano 04 (conteúdo que depende de dearchive/SPU).

## Global Constraints

- ABI: **não** voltar a `r3 = args_ea` scalar (refutado no master).
- Isolar crashes de job (SEH/thread — `test_seh_isolation` no master).
- Preferir lifted; interpretador só fallback documentado.

---

### Task 1: Teste de ABI u128 (TDD core)

**Files:**
- `runtime/spu/spu_lifted_job.h` — `spu_run_lifted_job_ex`
- Create: `runtime/spu/tests/test_spu_task_abi_main.c`
- Create: `runtime/spu/tests/gen_test_spu_task_abi.py` (ELF mínimo)

**Contrato a testar:**

```c
/* spu_run_lifted_job_ex carrega 16 bytes BE de args_ea em ctx.gpr[3]._u32[0..3]
 * (ou fallback documentado). O job lifted faz:
 *   preferred slot = gpr[3].word0
 *   DMA PUT de word0 para EA fixa de teste
 */
```

- [ ] **Step 1: Vermelho — teste que espera ABI nova**

```c
// test_spu_task_abi_main.c (esqueleto)
// 1) materializa args em guest mem: 16 bytes BE
// 2) spu_run_lifted_job_ex(entry, ls, args_ea, 16, 0)
// 3) assert vm_read32(result_ea) == expected_from_word0
// FAIL se entry assumir r3==args_ea (valor ponteiro escrito em vez do payload)
```

- [ ] **Step 2: Gerar ELF SPU com lifter atual do tree**

```bash
python3 gen_test_spu_task_abi.py
python3 ../../../tools/spu_lifter.py --auto-functions test_spu_task_abi.elf -o out_spu_task_abi
```

- [ ] **Step 3: Verde — ajustar só se o lifter/runtime divergir**

Corrigir **um** dos lados (runtime load vs lift emit), nunca os dois ao acaso.

- [ ] **Step 4: Commit**

```bash
git commit -m "test(spu): ABI CellSpursTaskArgument u128 (TDD)"
```

---

### Task 2: Consertar suite SPU quebrada pós-merge (adapter/workload/shufb)

**Files:**
- `runtime/spu/tests/test_adapter_main.c`, `test_workload_main.c`, `test_shufb_main.c`
- `test_workload.deps` (path `/spu_workload.c` inválido medido)

- [ ] **Step 1: Vermelho**

```bash
cd runtime/spu/tests
# adapter → Bus error
# workload → clang: no such file /spu_workload.c
# shufb → wrong out
```

- [ ] **Step 2: Verde workload deps**

```bash
# test_workload.deps
EXTRA="-I $REPO/runtime/spu $REPO/runtime/spu/spu_workload.c ..."
# paths relativos corretos a REPO
```

- [ ] **Step 3: Verde adapter**

Atualizar harness para passar args no formato u128 / EA de struct, alinhado a Task 1.

- [ ] **Step 4: Verde shufb**

Re-lift `out_shufb` com lifter atual; se ainda falhar, bug no helper `spu_shufb` → teste unitário do helper em C puro.

- [ ] **Step 5: Commit por teste**

---

### Task 3: Kernel submit → lifted job → DMA (integração)

**Files:**
- `libs/spurs/tests/test_spurs_kernel_main.c` (expandir) **ou**
- `runtime/spu/tests/test_spurs_e2e_main.c`

- [ ] **Step 1: Vermelho — e2e sem dispatch real**

```c
// start kernel with dispatch that calls spu_run_lifted_job_ex
// submit job with pm=NULL / image from sum-elf
// drain
// assert completed==1 && memory written
```

- [ ] **Step 2: Verde — `spurs_pm_dispatch` de produção**

Garantir que o callback registado em `spurs_kernel_start_default` é o mesmo path do e2e.

- [ ] **Step 3: Commit**

```bash
git commit -m "test(spurs): e2e kernel submit → lifted DMA job"
```

---

### Task 4: LFQueue + host inflate (W9/W15 precursor)

**Files:**
- `libs/spurs/cellSpursLFQueue.c`
- `libs/spurs/lfq_host_inflate.c`
- Create: `libs/spurs/tests/test_lfq_inflate_main.c`

**Element layout (já documentado em lfq_host_inflate.c):**

```
+0x00 src EA, +0x08 csize, +0x0C kind, +0x14 dst EA, +0x18 max out, ...
```

- [ ] **Step 1: Vermelho — inflate de fixture**

```c
// monta elem BE em buffer host
// lfq_host_inflate_try(elem)
// assert dst[0..] == zlib raw deflate esperado
// FAIL se STB/header errado
```

- [ ] **Step 2: Verde — só declarações stb (já fix Plano 01)**

```c
// sem STB_IMAGE_IMPLEMENTATION neste TU
#include "../codec/stb_image.h"
// stbi_zlib_decode_noheader_buffer from cellPngDec.o
```

- [ ] **Step 3: Integração com push LFQueue**

Teste: push elem na fila; fulfiller host drena; ready count.

- [ ] **Step 4: Commit**

---

### Task 5: Atomics SPU (W10) — só se teste falhar

**Files:**
- `runtime/spu/spu_helpers.h` / channels
- Create: `runtime/spu/tests/gen_test_getllar.py` + `test_getllar_main.c`

- [ ] **Step 1: Vermelho**

```
// dois "SPUs" lógicos: getllar/putllc race
// expected FAIL se não implementado
```

- [ ] **Step 2: Verde mínimo**

```c
// reserva por contexto: addr + value; putllc succeeds only if unchanged
```

- [ ] **Step 3: Commit**

Se nenhum job do GoW2 falhar por atomics nesta fase: **skip** com note "adiado".

---

### Task 6: Isolação de crash de job (W12 safety)

**Files:**
- `runtime/spu/tests/test_seh_isolation_main.c` (master)
- Runtime job runner

- [ ] **Step 1: Vermelho em Mac se SEH Win-only**

Portar isolamento para POSIX: `pthread` + `sigsetjmp` / process-fork **ou** documentar que Mac aborta o job thread com signal handler.

- [ ] **Step 2: Verde**

```
// job proposital NULL deref → host sobrevive → próximo job OK
```

- [ ] **Step 3: Commit**

---

### Task 7: Smoke GoW2 com SPU_ALL

```bash
# gow2-recomp
export PS3_SPU_ALL=1   # ou PS3_SPU1=1 conforme gow2_spu_register
PS3_TRACE_SPURS=1 ./smoke_boot_mac.sh 45
# expected: sem SIGBUS novo; preferir log [SPU] dispatch
```

- [ ] **Step 1: Vermelho se crash**
- [ ] **Step 2: Verde — host sobrevive timeout**
- [ ] **Step 3: Note** com contagens dispatch/miss/complete
- [ ] **Step 4: Commit note**

---

## Aceite do Plano 03

| Critério | Evidência |
|----------|-----------|
| ABI u128 test PASS | test_spu_task_abi |
| adapter/workload/shufb não bloqueiam CI mínimo | suite documentada |
| e2e kernel→DMA PASS | test |
| LFQueue inflate unit PASS | test |
| GoW2 + SPU_ALL não derruba host | smoke 45s |

**Done → Plano 04.**
