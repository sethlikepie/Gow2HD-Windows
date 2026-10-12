# Plan 02 — Wall M2 + SPURS boot (W1, W6, W7) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: subagent-driven-development or executing-plans.

**Goal:** Sair do hang de boot do GoW2 caracterizado em M2 (`func_0030600C` / flag `0x86E118` / alloc `0x881970='XXXX'`), com kernel SPURS HLE **running**, imagens SPU **registadas**, e sem force-write da flag.

**Architecture:** O PPU já corre código liftado. O guest chama `cellSpursInitializeWithAttribute` (handlers em `ppu_sysprx.cpp` → `spurs_kernel_start_default`). Workloads só avançam se (1) o jogo chamar `AddWorkload`/`task` path e (2) o `spu_workload` registry conhecer spu0–3. O wall M2 pode ser **falta de sinal SPURS** ou **init de allocator incompleta** — o plano separa as hipóteses com testes.

**Tech Stack:** `spurs_kernel.h` (start/submit/drain), `cellSpurs.c`, `ppu_sysprx.cpp`, `gow2_spu_register.c`, `spu_lifted/*_v2`, lldb/`attach_mem_mac.sh`.

**Depends on:** Plano 01 verde.  
**Unblocks:** Plano 03 (jobs reais).

## Global Constraints

- **Proibido** `*0x86E118 = nonzero` manual ou pump de event queue vazia (já SIGBUS — full-bringup exp. 2.5).
- Kernel API = master async only.
- Cada hipótese = um experimento com log guardado em `gow2-recomp/notes/`.

---

### Task 1: Teste de instrumentação — kernel running? (TDD de diagnóstico)

**Files:**
- Modify: `libs/spurs/spurs_kernel.c` (log único se faltar)
- Modify: `libs/spurs/cellSpurs.c` — `spurs_kernel_start_default`
- Read: `runtime/ppu/ppu_sysprx.cpp` (~lines com `spurs_kernel_start_default`)

- [ ] **Step 1: Vermelho — smoke/log sem prova de kernel**

```bash
cd gow2-recomp
PS3_TRACE_SPURS=1 PS3_NO_RSX=1 ./boot_gow2 EBOOT.ELF > /tmp/m2_k.log 2>&1 &
sleep 15; kill -9 $!
grep -E 'spurs_kernel|kernel_start|start_default|submitted' /tmp/m2_k.log || true
# expected if missing: empty → FAIL diagnostic
```

- [ ] **Step 2: Teste unitário do kernel (master)**

```bash
# Compilar e correr libs/spurs/tests/test_spurs_kernel_main.c
# (liga spurs_kernel.c + stubs de dispatch)
# expected: PASS submit/drain
```

Se o teste unitário falhar: corrigir kernel **antes** de culpar o GoW2.

- [ ] **Step 3: Verde — boot loga start**

Em `spurs_kernel_start` (ou `start_default`):

```c
fprintf(stderr, "[spurs_kernel] start nSpus=%u running=%d\n",
        nSpus, spurs_kernel_running());
```

Re-boot 15s:

```bash
grep -q '\[spurs_kernel\] start' /tmp/m2_k2.log
```

Expected: PASS.

- [ ] **Step 4: Contador AddWorkload**

Em `cellSpursAddWorkload` (já tem printf) e no path **ctx** de `ppu_sysprx` se o jogo não passar pelo C de `cellSpurs.c`:

```c
/* ppu_sysprx: se existir hle_cellSpursAddWorkload, logar igual */
fprintf(stderr, "[spurs] AddWorkload wid=... pm=... size=...\n");
```

```bash
grep -c 'AddWorkload' /tmp/m2_k2.log
# registar N=0 ou N>0 na note
```

- [ ] **Step 5: Commit**

```bash
git commit -m "diag(spurs): log kernel start + garantir trace AddWorkload no path HLE"
```

---

### Task 2: Teste de memória M2 (baseline empírica)

**Files:**
- `gow2-recomp/attach_mem_mac.sh`
- Create/update: `gow2-recomp/notes/2026-07-20-m2-post01.md`

- [ ] **Step 1: Vermelho se addresses divergirem do M2**

```bash
# attach_mem_mac.sh @ ~12s
# expected M2:
# *0x86E118 +0 == 0
# *0x881970 +0 == 0x58585858
# [0x541AD0] == 0x0086E118
```

- [ ] **Step 2: Guardar tabela na note (PASS = reproduziu M2)**

Se **não** reproduzir (flag já ≠ 0): o master **já passou** W1 — documentar e saltar Task 5 para o novo PC.

- [ ] **Step 3: Commit note**

---

### Task 3: Wiring SPU images (W7) — TDD de link + register

**Files:**
- Modify: `gow2-recomp/build_macos.sh`
- `gow2-recomp/recomp_mid_v2/gow2_spu_register.c`
- `gow2-recomp/spu_lifted/spu{0..3}_v2/spu_recomp.c`

- [ ] **Step 1: Vermelho — símbolos ausentes**

```bash
nm boot_gow2 | grep -E 'spu0_|gow2_spu' || echo "FAIL: no SPU symbols"
# expected pre-task: FAIL
```

- [ ] **Step 2: Vermelho — object compile isolado (já deve passar pós-merge)**

```bash
PS3=../ps3recomp
clang -std=c11 -O1 -c -I spu_lifted/spu0_v2 -I $PS3/runtime/spu -I $PS3/include \
  spu_lifted/spu0_v2/spu_recomp.c -o /tmp/spu0.o
# expected: OK (provado 2026-07-20)
```

- [ ] **Step 3: Implementar etapa 3b no `build_macos.sh`**

```bash
echo "=== 3b. SPU lifted images ==="
SPU_OBJS=()
for d in "$HERE"/spu_lifted/spu?_v2; do
  [ -f "$d/spu_recomp.c" ] || continue
  n=$(basename "$d")
  o="$LIFT/${n}_spu_recomp.o"
  if [ ! -f "$o" ] || [ "$d/spu_recomp.c" -nt "$o" ]; then
    clang -std=c11 -O1 -w -c -I "$d" -I "$PS3/runtime/spu" -I "$PS3/include" \
      "$d/spu_recomp.c" -o "$o"
  fi
  SPU_OBJS+=("$o")
done
clang -std=c11 -O1 -w -c -I "$PS3/runtime/spu" -I "$PS3/include" \
  "$HERE/recomp_mid_v2/gow2_spu_register.c" -o "$LIFT/gow2_spu_register.o"
# incluir ${SPU_OBJS[@]} e gow2_spu_register.o no link final
```

- [ ] **Step 4: Verde — nm**

```bash
./build_macos.sh
nm boot_gow2 | grep 'spu0_' | head
# expected: vários símbolos
```

- [ ] **Step 5: Verde — register no boot**

Garantir que `gow2_spu_register` é chamado de `boot_macos.cpp` (ou constructor):

```cpp
extern "C" void gow2_spu_register(void); // nome real no .c
// em main, após ppu_hle_init:
gow2_spu_register();
```

Log esperado (adicionar se faltar):

```c
fprintf(stderr, "[gow2_spu] registered images 0..3\n");
```

```bash
grep -q 'gow2_spu' /tmp/boot.log
```

- [ ] **Step 6: Smoke 01 ainda PASS**

```bash
./smoke_boot_mac.sh 30
```

- [ ] **Step 7: Commit**

```bash
git commit -m "feat(gow2): link spu0-3 v2 + register no boot macOS"
```

---

### Task 4: Hipótese A — zero AddWorkload (experimento)

**Pré-condição:** Task 1 mostrou `AddWorkload` count = 0.

- [ ] **Step 1: Vermelho — jogo não agenda**

```bash
grep -c AddWorkload /tmp/m2_k2.log   # 0
```

- [ ] **Step 2: Não “inventar” workloads**

Proibido: `spurs_kernel_submit` sintético no boot do GoW2 sem contrato do título.

- [ ] **Step 3: Trace quem deveria chamar AddWorkload**

```bash
# Com PS3_TRACE_SPURS + sample/lldb na main após SpursInit
# Mapear PC: ainda em func_0030600C?
lldb -b -o 'process launch' -o 'process status' ./boot_gow2 -- EBOOT.ELF
# ou sample $PID 3
```

- [ ] **Step 4: Note**

```markdown
## Hipótese A
AddWorkload count = 0 após SpursInit×3.
Conclusão: wall não é "job falha"; é "job nunca submetido".
Seguir Task 5 (allocator) em paralelo com RE do caller de AddWorkload no lift.
```

- [ ] **Step 5: Commit note only**

---

### Task 5: Hipótese B — allocator `0x881970 = XXXX` (TDD por watch)

**Files:**
- Guest: `func_0031415C` / `func_00315EAC` (lift em `recomp_macos/ppu_recomp_*.cpp`)
- Runtime: HLE heap (`sysPrxForUser` NIDs de heap-init no log pós-merge)

Log pós-merge mostrou:

```
[imp] *** heap-init import slot=... NID=0x1573DC3F ...
```

- [ ] **Step 1: Vermelho — controle inválido**

```bash
# attach_mem: *0x881970 == 0x58585858
```

- [ ] **Step 2: Teste de store**

Adicionar (gated `PS3_TRACE_ALLOC_CTRL=1`) em `vm_write32` / path já usado para CTRL:

```c
if (getenv("PS3_TRACE_ALLOC_CTRL") && addr >= 0x881970 && addr < 0x8819A0)
  fprintf(stderr, "[ALLOC] w32 [0x%08X]=0x%08X ra=%p\n", addr, val, __builtin_return_address(0));
```

- [ ] **Step 3: Boot 20s com trace**

```bash
PS3_TRACE_ALLOC_CTRL=1 ./boot_gow2 EBOOT.ELF 2>&1 | grep ALLOC | head
# expected vermelho: zero writes → init nunca escreve
# ou writes parciais → ver valor final
```

- [ ] **Step 4: Verde — se NID de heap for stub**

Implementar/corrigir o handler do NID que a init chama (só com assinatura medida).  
Re-correr: `*0x881970 != 0x58585858` **sem** write manual.

- [ ] **Step 5: Commit** por NID/fix, não monólito

---

### Task 6: Hipótese C — workload corre, flag não sobe

**Pré-condição:** AddWorkload ≥ 1 e `spurs_kernel_completed` > 0.

- [ ] **Step 1: Vermelho**

```bash
# completed > 0 mas *0x86E118 ainda 0
```

- [ ] **Step 2: Teste sintético isolado (Plano 03 reutiliza)**

Job SPU de teste: `MFC PUT 0x1` em EA `0x86E118` → prova que o pipeline **pode** escrever a flag.  
Isto **não** substitui o writer real do jogo; só prova o plumbing.

- [ ] **Step 3: RE do writer real**

No lift, procurar stores a TOC-relative que alimentam `0x541AD0` / flag.  
Instrumentar só esse path.

- [ ] **Step 4: Verde**

```bash
# attach_mem: *0x86E118 != 0  (sem patch)
# sample: PC fora do loop de func_0030600C
```

---

### Task 7: Smoke “além do M2”

**Files:**
- Create: `gow2-recomp/smoke_past_m2.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail
# reusa env_gow2.sh, 45s
./boot_gow2 EBOOT.ELF >"$LOG" 2>&1 &
# PASS se UMA das evidências:
# 1) grep AddWorkload count >= 1 && spurs_kernel completed
# 2) attach_mem flag != 0 (scripted lldb one-shot)
# 3) marker explícito [boot] left_m2_wall (só se instrumentado honestamente)
```

- [ ] **Step 1: Vermelho** (ainda no M2)
- [ ] **Step 2: Verde** só quando Task 4–6 destravar de verdade
- [ ] **Step 3: Commit**

---

## Aceite do Plano 02

| Critério | Evidência |
|----------|-----------|
| Kernel start no boot | log `[spurs_kernel] start` |
| SPU images no binário | `nm boot_gow2 \| grep spu0_` |
| M2 classificado | note com A/B/C e contagens |
| Ideal: left wall | flag ≠ 0 ou PC novo **sem** force |
| Smoke 01 ainda PASS | regredir proibido |

**Done → Plano 03** (mesmo se M2 só “classificado”, desde que A/B tenham próximos passos claros; preferível left wall).
