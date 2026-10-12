# Plan 01 — Host rebaseline Mac (W0) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Checkboxes `- [ ]` for tracking.

**Goal:** Após o merge `origin/master` → `macos-arm64-port-f0-f2`, o motor e o `boot_gow2` **compilam, linkam e passam um smoke honesto** no macOS/arm64, sem critérios de log da Phase 2 antiga.

**Architecture:** O master trouxe giant lock Win32, TLS, movie_hle Win-only e STB partilhado. Este plano fecha a portabilidade POSIX e redefine o smoke para o que o boot **realmente** faz pós-merge (SpursInit×3, HLE wired), não o que a suite poll_once logava.

**Tech Stack:** CMake/Ninja, Apple Clang, SDL2/Vulkan Homebrew, `boot_macos.cpp`, `libps3recomp_runtime.a`.

**Depends on:** merge `aab18f4` (já feito).  
**Unblocks:** Planos 02–05.

## Global Constraints

- Não commitar EBOOT/extracted/psarc.
- Não reintroduzir API `spurs_kernel_poll_once` da Phase 2 macOS.
- Smoke não deve exigir strings removidas no master sem provar regressão real.

---

### Task 1: Commitar fixes de portabilidade já no working tree

**Files:**
- Modify: `runtime/ppu/ppu_loader.cpp` (giant lock POSIX, TLS Apple `thread_local` para trampoline, stub `g_ps3_rperma_full`, guards `GetModuleHandleW`)
- Modify: `runtime/ppu/ppu_hle.cpp` (`__thread` em `g_last_hle_caller_*`)
- Modify: `runtime/ppu/ppu_fs.cpp` (`stat` vs `_stat64`)
- Modify: `libs/spurs/lfq_host_inflate.c` (sem 2º `STB_IMAGE_IMPLEMENTATION`)

- [ ] **Step 1: Teste vermelho — provar que sem os fixes o host não linka**

Se os fixes já estão aplicados, documentar o log de falha capturado:

```
undefined: GetCurrentThreadId / g_trampoline_fn ZTW / g_ps3_rperma_full / stbi_* duplicate
```

Expected: FAIL de link (histórico 2026-07-20).

- [ ] **Step 2: Garantir que o tree contém os quatro ficheiros corrigidos**

```bash
cd ps3recomp
git diff --stat runtime/ppu/ppu_loader.cpp runtime/ppu/ppu_hle.cpp runtime/ppu/ppu_fs.cpp libs/spurs/lfq_host_inflate.c
```

- [ ] **Step 3: Rebuild runtime (teste de compilação = verde)**

```bash
cmake -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"
test -f build-macos/libps3recomp_runtime.a
```

Expected: exit 0, `.a` existe.

- [ ] **Step 4: Commit**

```bash
git add runtime/ppu/ppu_loader.cpp runtime/ppu/ppu_hle.cpp runtime/ppu/ppu_fs.cpp libs/spurs/lfq_host_inflate.c
git commit -m "fix(macos): portabilidade pós-merge (TLS, giant lock, stat, STB)"
```

---

### Task 2: Rebuild `boot_gow2` (teste de integração de link)

**Files:**
- `gow2-recomp/build_macos.sh`
- `gow2-recomp/boot_macos.cpp`
- Output: `gow2-recomp/boot_gow2`

- [ ] **Step 1: Vermelho se RUNTIME_LIB apontar para lib velha**

```bash
# build_macos.sh exige:
# RUNTIME_LIB="$PS3/build-macos/libps3recomp_runtime.a"
ls -la ../ps3recomp/build-macos/libps3recomp_runtime.a
```

- [ ] **Step 2: Verde — build completo**

```bash
cd gow2-recomp
./build_macos.sh
file boot_gow2
# expected: Mach-O 64-bit executable arm64
```

- [ ] **Step 3: Se falhar link, classificar**

| Símbolo | Fix |
|---------|-----|
| `g_trampoline_fn` ZTW | `thread_local` no loader (Apple) |
| `g_last_hle_caller_guest` | `__thread` + `extern "C"` no hle |
| `g_ps3_rperma_full` | stub non-Win no loader |
| `stbi_*` | um só `STB_IMAGE_IMPLEMENTATION` |

- [ ] **Step 4: Commit só se `build_macos.sh` mudou** (senão skip)

---

### Task 3: Smoke honesto (TDD no critério)

**Files:**
- Modify: `gow2-recomp/smoke_boot_mac.sh`

**Problema:** o smoke pré-merge exige:

```bash
grep '_SPU_printf_server'
grep 'THREAD 1] host thread started'
grep '\[spurs kernel\] initialize'
```

Pós-merge o boot faz SpursInit×3 mas **não** emite esses logs → FAIL falso.

- [ ] **Step 1: Vermelho — smoke atual**

```bash
cd gow2-recomp && ./smoke_boot_mac.sh 20
# expected today: FAIL: thread do printf server nao criada
```

- [ ] **Step 2: Escrever o novo contrato de PASS (teste)**

Substituir o bloco de greps por:

```bash
# Progresso mínimo (pós-merge master)
grep -qE '5[0-9]{4} lifted functions' "$LOG" || fail "tabela de funcoes"
grep -q  '151 imports'                 "$LOG" || fail "imports"
grep -c  'cellSpursInitializeWithAttribute' "$LOG" | awk '{exit !($1>=3)}' \
  || fail "SpursInit < 3 (precisa PS3_TRACE_SPURS=1)"

# Regressões proibidas
grep -q 'lv2_syscall 141 (stub)' "$LOG" && fail "usleep stub"
nid=$(grep -c 'unresolved NID' "$LOG" || true)
[ "${nid:-0}" -le 5 ] || fail "NID spam ($nid)"

# Opcional: se o kernel logar, aceitar qualquer um destes
# (não falhar se ausente até Task 3 provar regressão)
if grep -qE 'spurs_kernel|spurs kernel|kernel_start' "$LOG"; then
  echo "  note: kernel log present"
fi

# CPU: não busy-spin de stub (amostra já existe no script)
# manter check de %cpu baixo se PID ainda vivo

echo "PASS: baseline post-merge boot (SpursInit wall, not stub-spin)"
```

- [ ] **Step 3: Verde — smoke passa**

```bash
./smoke_boot_mac.sh 30
# expected: PASS: baseline post-merge...
```

- [ ] **Step 4: Investigar regressão real de thread (só se SpursInit < 3)**

Se SpursInit < 3: diff `ppu_sysprx.cpp` EnableSpuPrintf vs `89475a7`.  
Se SpursInit ≥ 3 sem printf server: **não é FAIL** — documentar em note.

- [ ] **Step 5: Commit**

```bash
git add smoke_boot_mac.sh
git commit -m "test(gow2): smoke baseline alinhado ao boot pós-merge master"
```

---

### Task 4: Smoke do motor (suite SPU mínima)

**Files:**
- `ps3recomp/scripts/smoke_macos.sh` (se existir)
- `runtime/spu/tests/`
- `libs/spurs/tests/test_spurs_kernel_main.c`

- [ ] **Step 1: Vermelho — inventário**

```bash
cd ps3recomp/runtime/spu/tests
# regenerar se preciso; contar PASS/FAIL
```

Medido pós-merge: ~7 PASS, 3 FAIL (`adapter` SIGBUS, `shufb`, `workload` path).

- [ ] **Step 2: Verde mínimo para este plano**

Não é preciso 100% da suite SPU genérica aqui. Aceite:

```bash
# pelo menos:
test_sum, test_dma, test_brsl_return  → PASS
# e se possível:
# compilar e correr libs/spurs/tests/test_spurs_kernel_main.c contra spurs_kernel.o
```

- [ ] **Step 3: Documentar FAIL conhecidos como dívida do Plano 03** (ABI adapter/workload)

- [ ] **Step 4: Commit** se `smoke_macos.sh` foi ajustado

---

### Task 5: Note de rebaseline

**Files:**
- Create: `gow2-recomp/notes/2026-07-20-postmerge-rebaseline.md`

Conteúdo mínimo:

```markdown
# Rebaseline pós-merge master

- commit motor: <hash>
- boot_gow2: arm64 link OK
- smoke: PASS com critérios novos
- SpursInit count: 3
- printf server: ausente/presente (com prova)
- próximo plano: 02-wall-m2-spurs-boot
```

- [ ] **Step 1: Escrever note**
- [ ] **Step 2: Commit no gow2-recomp**

---

## Aceite do Plano 01

| Critério | Comando |
|----------|---------|
| Runtime Mac | `test -f ps3recomp/build-macos/libps3recomp_runtime.a` |
| Host GoW2 | `file gow2-recomp/boot_gow2` → arm64 |
| Smoke | `./smoke_boot_mac.sh` → PASS |
| Sem regressão de stub 141 | grep no log do smoke |

**Done → abrir Plano 02.**
