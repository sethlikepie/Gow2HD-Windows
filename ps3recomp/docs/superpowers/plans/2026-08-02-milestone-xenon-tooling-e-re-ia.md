# Milestone: Tooling Xenon-style + RE assistida (IA/Ghidra) → lift regenerável e walls mais rápidas

> **Para agentes:** preferir `subagent-driven-development` (1 task por subagent, review entre tasks).  
> Checkboxes `- [ ]` = trabalho ainda por fazer. Itens já no repo marcados **JÁ EXISTE** (não reimplementar).

**Data:** 2026-08-02  
**Branch típica:** `macos-arm64-port-f0-f2`  
**Repos:** monorepo `ps3recomp` + checkout de build `../gow2-recomp` (ou `games/gow2/` no subtree)

---

## 0. Objectivo do milestone

### Meta de produto (o que “melhoramos”)

| Eixo | Hoje (dor) | Depois do milestone |
|------|------------|---------------------|
| **Re-lift** | Fixes permanentes vivem em ~80+ `patch_*.py` (agulhas de texto) | Fixes de correctness em **TOML midasm / weak override / switch tables**; patches só para probes experimentais |
| **Correctness do lift** | Jump tables / fallthrough / bounds às vezes manuais e frágeis | `ps3_analyse` + switch TOML + `verify_lift` por **delta** após cada re-lift |
| **Velocidade de wall** | Hipóteses ad-hoc; Ghidra usado à mão | Playbook RE + Ghidra (CLI + opcional MCP) com **EA + smoke medido**; IA **não** gera o C liftado |
| **Confiança** | “OK: already patched” sem contrato | `CONTRACTS.tsv` + gate único; regressão de OPD/fallthrough falha CI/local |

### Meta explícita de **não**-fazer

- **Não** reescrever `ppu_lifter` / `spu_lifter` em Go neste milestone.  
- **Não** usar LLM para emitir `ppu_recomp_*.cpp` nem “consertar” guest sem prova in-boot.  
- **Não** activar opts ABI (`*_as_local`, `skip_lr`) por default.  
- **Não** commitar `EBOOT.ELF`, `extracted/`, `movie_cache/`, chunks gigantes de recomp sem pedido humano.  
- **Não** contar overlay/logo como pixels de jogo (regra CLAUDE.md).

### Critério de “milestone fechado”

Todos os gates M0–M4 verdes **e** pelo menos **3** fixes GoW2 de correctness migrados de `patch_*.py` → midasm/weak/TOML, com:

1. `verify_lift.sh` limpo no lift de produção.  
2. Smoke intro 25–40s: `st620` 0→1→≥3 (ou path FORCE documentado), sem OOB pós-TLS novo.  
3. Re-lift de **uma** função/chunk de teste **sem** reaplicar o patch migrado (só config+hooks).  
4. Playbook RE documentado usado numa wall real (D ou C2) com ledger de hipóteses.

---

## 1. Estado actual (baseline medido no repo — 2026-08)

### 1.1 Já existe (reutilizar)

| Artefacto | Estado | Notas |
|-----------|--------|-------|
| `tools/recomp_config.py` + `test_recomp_config.py` | **JÁ EXISTE** | Schema Xenon: midasm, overrides, invalid, switch_table path |
| `games/gow2/config/gow2_recomp.toml` | **JÁ EXISTE** | Só `[main]` + opts off; midasm ainda não ligado ao emit |
| `ppu_lifter.py --config` | **Parcial** | Consome `[[invalid_instructions]]` + merge `switch_table`; **não** emite midasm |
| `tools/lift_parity.py` + baselines | **JÁ EXISTE** | Delta-friendly |
| `games/gow2/verify_lift.sh` | **JÁ EXISTE** | parity + MANIFEST delta + audit_boundaries (**sem** `--oracle` ainda) |
| `lift_baseline/CONTRACTS.tsv`, `check_contracts.py`, MANIFEST | **JÁ EXISTE** | Contratos por patch/EA |
| Ghidra: `ghidra_analyze.py`, `ghidra_lookup.py`, `ghidra_names.py`, `analyze_eboot_ghidra.sh` | **JÁ EXISTE** | Oráculo estático em `ghidra_out/` |
| Plano Xenon detalhado | `2026-07-23-xenonrecomp-lessons-recomp-tooling.md` | Tasks A–G; este milestone **orquestra** o que falta |
| Plano anti-LLM no lift | `docs/PLANO_ADOPCAO_AUTO_RE_AGENT.md` | Cortes C1–C2: não gerar C com LLM |

### 1.2 Falta (escopo deste milestone)

| Lacuna | Track |
|--------|-------|
| Emissão mid-asm no lifter + stubs linkados no `boot_gow2` | **X** Xenon |
| Weak `PPC_FUNC` / overrides host | **X** |
| `tools/ps3_analyse.py` + `gow2_switch_tables.toml` revisto | **X** |
| Migração de patches P0 de correctness | **X** |
| Oracle Ghidra no `verify_lift` (opcional/gated) + manifesto de hash ELF | **R** RE |
| Playbook + skill/docs para sessão RE com Ghidra (MCP opcional) | **R** |
| CI/local: `verify_lift` invocado no fluxo de aceitação de re-lift | **R** |
| Ledger de migração patch → mecanismo Xenon | **X+R** |

### 1.3 Planos irmãos (não duplicar trabalho)

| Plano | Relação |
|-------|---------|
| `2026-07-23-xenonrecomp-lessons-recomp-tooling.md` | Spec fina das Tasks A1–G; **executar** a partir daí quando a task for “implement midasm” |
| `docs/PLANO_ADOPCAO_AUTO_RE_AGENT.md` | Gates/oráculo; items 1–3 em grande parte aterrados — completar residual |
| Metal INDEX M0–M10 | **Paralelo**; este milestone **não** bloqueia M*, mas midasm ajuda probes de shader wall |
| Wall chain INDEX | Consome o resultado (re-lift limpo + RE mais rápida) |

---

## 2. Arquitectura alvo

```
                    ┌─────────────────────────────────────┐
                    │  gow2_recomp.toml                   │
                    │  + gow2_switch_tables.toml          │
                    │  + hooks/*.cpp (midasm + weak)      │
                    └──────────────┬──────────────────────┘
                                   │
   EBOOT.ELF ──► find_functions ──► ppu_lifter --config ──► ppu_recomp_*.cpp
                    │                     │
                    │                     ▼
                    │              /* regenerável */
                    │
   Ghidra ──► ghidra_out/ ──► audit / seed-json / (MCP só leitura+annotate)
                    │
                    ▼
            verify_lift.sh ──► delta vs lift_baseline/*
                    │
                    ▼
            apply_all_patches.sh  (só PROBE / legado residual)
                    │
                    ▼
            build_macos.sh + smoke intro ──► aceite wall
```

**Regra de ouro pós-M2:**  
novo fix **permanente** de jogo **não** pode existir *só* como `patch_*.py`.  
Tem de ter caminho TOML/hook **ou** justificação `LEGACY-PATCH` no ledger com data-limite.

---

## 3. Tracks e fases

```
M0  Foundation & inventory          (1–2 dias)
M1  Mid-asm end-to-end              (3–5 dias)     Track X
M2  Jump tables + analyse           (3–5 dias)     Track X
M3  Weak overrides + migração P0    (3–5 dias)     Track X
M4  RE stack: oracle gate + playbook (+ MCP opcional)  Track R
M5  Hardening: CI, ledger, 3 migrações, smoke matrix  (fecho)
```

Dependências:

```
M0 ──► M1 ──► M3 ──► M5
 └────► M2 ──────────┘
 └────► M4 ──────────┘   (M4 pode em paralelo com M1–M2)
```

---

## 4. M0 — Foundation & inventory

**Objectivo:** saber exacto o que migrar e como medir melhoria.

### Task M0.1 — Inventário de patches (classificação)

**Ficheiros:**
- Criar: `games/gow2/notes/patch-migration-ledger.md` (ou `lift_baseline/PATCH_MIGRATION.tsv`)

**Colunas obrigatórias:**

| Campo | Valores |
|-------|---------|
| `patch` | nome do ficheiro |
| `classe` | `CORRECTNESS` \| `PROBE` \| `OPD` \| `BUILD` \| `OBSOLETE` |
| `alvo` | midasm \| weak \| switch_table \| invalid_insn \| runtime \| keep-probe \| delete |
| `ea` | 0x… ou `-` |
| `prioridade` | P0 \| P1 \| P2 |
| `aceite` | string de grep/smoke ou contrato CONTRACTS |
| `estado` | `todo` \| `migrated` \| `wont` |

**Passos:**
- [ ] Partir de `PATCH_CATALOG.tsv` / lista `recomp_mid_v2/patch_*.py` (contar N).
- [ ] Classificar **no mínimo** os 15 de maior impacto (fallthrough, jumptable, CE03C, OPD factory, host inflate).
- [ ] Marcar P0 ≥ 5 itens com EA e aceite.

**Teste de melhoria:**
```bash
# Contagem baseline (congelar no ledger header)
find games/gow2/recomp_mid_v2 -name 'patch_*.py' | wc -l
# Após milestone: N_correctness_only_patch deve baixar; N_migrated sobe
```

### Task M0.2 — Smoke e verify canónicos documentados

**Ficheiros:**
- Actualizar este plano §10 com comandos medidos no teu checkout
- Confirmar paths: `../gow2-recomp` vs `games/gow2`

**Passos:**
- [ ] Correr **uma vez** `verify_lift.sh` no lift de produção; guardar log em `/tmp/m0_verify_lift.log` (não commitar se tiver paths locais sensíveis).
- [ ] Correr smoke 25s intro (PID kill, nunca `pkill -f boot_gow2`).
- [ ] Anotar no ledger: `st620 max`, sticky, OOB pós-TLS.

**Aceite M0:** ledger com ≥15 linhas classificadas + 1 smoke + 1 verify baseline registados.

---

## 5. M1 — Mid-asm end-to-end (Track X)

**Objectivo:** TOML `[[midasm_hook]]` → chamada C no C gerado → link no boot → smoke.

### Task M1.1 — Emissão no lifter (unit-tested)

**Referência fina:** plano 2026-07-23 Task A2.

**Ficheiros:**
- Modificar: `tools/ppu_lifter.py` (loop por instrução)
- Criar: `tools/test_midasm_emit.py`
- Modificar: `tools/recomp_config.py` se faltar serialização de `returns` / `after_instruction`
- Actualizar: `docs/RECOMP_CONFIG.md` (deixar de ser stub nas secções midasm)

**Comportamento:**
1. Com `--config TOML`, construir `dict[ea] -> list[MidasmHook]`.
2. Antes/depois da insn (flag `after_instruction`), emitir:
   ```c
   gow2_midasm_<Name>(ctx);
   ```
3. Se `returns=true`: emitir `return;` após o hook (v1).
4. Sem `--config`: **zero** mudança de output (golden de regressão).

**Testes:**
```bash
python3 tools/test_midasm_emit.py          # sintético: buffer 4–8 words
python3 tools/test_recomp_config.py
# opcional: diff de lift de 1 função com e sem hook
```

**Aceite:**
- [ ] test_midasm_emit PASS  
- [ ] Lifter sem config: output idêntico em fixture pequena  
- [ ] Commit: `feat(lifter): emissão mid-asm hooks via TOML`

### Task M1.2 — Host hooks + link Mac

**Ficheiros:**
- Criar: `games/gow2/hooks/gow2_midasm_hooks.h`  
- Criar: `games/gow2/hooks/gow2_midasm_hooks.cpp` (stubs no-op)  
- Modificar: `build_macos.sh` / CMake do gow2-recomp para compilar e linkar hooks  
- Espelho monorepo: paths sob `games/gow2/` se o build for daqui

**Testes:**
```bash
# no checkout de build
./build_macos.sh   # ou comando canónico actual
nm boot_gow2 | grep gow2_midasm   # símbolos resolvidos (não U undefined se stubs no binário)
```

**Aceite:**
- [ ] `boot_gow2` linka com hooks  
- [ ] Smoke intro **sem regressão** (st620 ≥3 path baseline)

### Task M1.3 — Migração piloto CE03C (ou 1º P0 do ledger)

**Ficheiros:**
- Modificar: `gow2_recomp.toml` → `[[midasm_hook]]`  
- Implementar corpo em `gow2_midasm_hooks.cpp`  
- Patch legado: no-op se `PS3_MIDASM_*=1` **ou** remover injecção após prove  
- Contrato em `CONTRACTS.tsv` para o marcador midasm

**Aceite (medido):**
- [ ] Smoke 30–40s: mesmo (ou melhor) thr/wait-idle que o patch  
- [ ] Re-lift da função **sem** `patch_ce03c_*.py` → hook ainda presente  
- [ ] `check_contracts` / `verify_lift` não regredem  

**Melhoria quantificável:**  
`patches_correctness_active` −1; `re_lift_safe_fixes` +1.

---

## 6. M2 — Jump tables + analyse (Track X)

**Objectivo:** detectar e declarar jtables; lifter emite `switch` fiável; menos patches jumptable.

### Task M2.1 — `tools/ps3_analyse.py`

**Referência:** plano 2026-07-23 Task B1.

**CLI:**
```bash
python3 tools/ps3_analyse.py path/EBOOT.ELF -o games/gow2/config/gow2_switch_tables.toml
python3 tools/ps3_analyse.py path/EBOOT.ELF --include-low -o /tmp/jtables_low.toml
```

**Heurística v1 (documentar limites):**
1. Scan `mtctr` seguido em N insns de `bctr`/`bcctr`.  
2. Backtrack `rlwinm` / `lwzx` / base de tabela.  
3. Alvos BE dentro de `.text` / bounds de funções.  
4. Campo `confidence`; default só emite high.

**Testes:**
- Criar: `tools/test_ps3_analyse_jtables.py` + fixture sintética  
```bash
python3 tools/test_ps3_analyse_jtables.py
```

**Aceite:**
- [ ] Fixture sintética PASS  
- [ ] Run real no EBOOT **local** (não commitar dump bruto); humano revê ≥1 tabela GoW2 conhecida (ex. sites de `patch_jumptable_*.py`)

### Task M2.2 — Lifter: switch tables como `switch` real

**Referência:** Task B2 do plano Xenon.

**Já parcial:** `merge_switch_tables` existe — completar emissão se ainda cair em trampoline.

**Testes:**
- Unit: função sintética com dispatch declarado → contém `switch` / cases com EAs  
- Integração: re-lift de chunk que contém jtable GoW2 P0  

**Aceite:**
- [ ] Pelo menos **1** `patch_jumptable_*.py` marcado `migrated` ou `redundant`  
- [ ] Smoke sem FATAL/OOB novo  

### Task M2.3 — `invalid_instructions` GoW2 (se medido)

- [ ] Se o EBOOT tiver padding/EH entre funcs, popular TOML (já suportado no lifter).  
- [ ] Teste: `tools/test_invalid_instructions.py` continua PASS.

---

## 7. M3 — Weak overrides + migração P0 (Track X)

**Objectivo:** substituir funções guest inteiras sem editar o C gerado.

### Task M3.1 — `runtime/ppu/ppc_hooks.h` + emissão

**Macros (Clang first):**
```cpp
// runtime/ppu/ppc_hooks.h
#if defined(__clang__)
#define PPC_FUNC_IMPL(name) __attribute__((weak)) void name(ppu_context* ctx)
#define PPC_FUNC(name) void name(ppu_context* ctx)
#else
#define PPC_FUNC_IMPL(name) void name(ppu_context* ctx)
#define PPC_FUNC(name) void name(ppu_context* ctx)
#endif
```

**Emissão (atrás de flag no TOML ou `--emit-weak-wrappers`):**
- Corpo liftado → `__imp_func_XXXXXXXX`  
- Wrapper fraco `func_XXXXXXXX` chama `__imp_...`  
- Override host em `games/gow2/hooks/gow2_func_overrides.cpp`

**Testes:**
```bash
# object file pequeno + nm: weak + strong override wins
python3 tools/test_weak_override_symbols.py  # criar
```

**Aceite:**
- [ ] 1 override trivial (no-op probe) linka e corre  
- [ ] Smoke intro sem regressão  

### Task M3.2 — Migrar ≥2 P0 adicionais do ledger

Exemplos candidatos (validar EA no ledger; âncoras **verificadas** no código actual):

| Candidato | Mecanismo provável |
|-----------|-------------------|
| Fallthrough 2550C8 (já patch) | Preferir fix no **lifter** se sistemático; senão midasm/switch |
| Jumptable 2a209c / 2b11b8 | switch_table TOML |
| Factory OPD / ICG ctor | weak ou midasm |
| Host inflate / WAD path | runtime HLE (já parcial) — documentar, não forçar midasm |

**Aceite M3:**
- [ ] Total **≥3** fixes `CORRECTNESS` re-lift-safe (contando CE03C se M1.3 contou)  
- [ ] Ledger actualizado  
- [ ] `verify_lift.sh` verde  

---

## 8. M4 — RE stack: gates + playbook + MCP opcional (Track R)

**Objectivo:** IA/agente como **navegador do mapa guest**, com oráculos determinísticos.

### Task M4.1 — Fechar residual do plano auto-re

**Já existe `verify_lift.sh`.** Completar:

| Item | Acção |
|------|--------|
| Oráculo I4/I5 | Adicionar passo **opcional** `VERIFY_ORACLE=1` com `--oracle` + `ORACLE_BASELINE` por **delta** (nunca limiar absoluto cru) |
| Proveniência Ghidra | `ghidra_out/MANIFEST.json`: sha256 ELF, data, ghidra version, counts |
| Wire em aceitação | `accept_relift.sh` / docs: “sem verify_lift verde, não promover lift” |

**Testes:**
```bash
# sem oracle (default CI local)
./verify_lift.sh "$LIFT_DIR" ; echo rc=$?

# com oracle (dev machine com ghidra_out fresco)
VERIFY_ORACLE=1 ./verify_lift.sh "$LIFT_DIR"
```

**Prova de valor (mutação):**  
truncar uma função no lift de cópia → parity ou bounds **rc=1**. Já documentado no plano auto-re; revalidar uma vez.

### Task M4.2 — Playbook RE para walls (sem MCP)

**Criar:** `docs/RE_PLAYBOOK_GOW2.md`

Secções mínimas:

1. **Pergunta de wall** (1 frase: “porque SHADERSRC N=0?”)  
2. **Âncoras:** EA, símbolo Ghidra, log tag (`[LDRSH]`, etc.)  
3. **Ferramentas:**
   ```bash
   ./analyze_eboot_ghidra.sh          # se out stale
   python3 tools/ghidra_lookup.py ghidra_out 0xEA
   python3 tools/ghidra_lookup.py ghidra_out 0xEA --callers
   python3 tools/ghidra_lookup.py ghidra_out --grep 'SHADER'
   python3 tools/show_func.py ...     # se aplicável
   ```
4. **Hipótese → experimento discriminador** (env probe OFF default)  
5. **Aceite medido** (grep log / contrato / PNG hash)  
6. **Anti-padrões:** CRC bypass, aceitar demo shader, LLM a reescrever chunk  

**Teste de melhoria (processo):**  
- [ ] Usar o playbook numa sessão real de wall D **ou** C2  
- [ ] Preencher 1 página de ledger: hipóteses refutadas + fix ou “ainda aberto”

### Task M4.3 — MCP Ghidra (opcional, não bloqueante)

**Objectivo:** agente (Claude/Grok/etc.) fala com Ghidra **em live** sem substituir `ghidra_out` estático.

**Passos:**
- [ ] Escolher servidor MCP estável (ex. LaurieWired/GhidraMCP ou fork activo); documentar versão.  
- [ ] Instalar extensão Ghidra + registar MCP no cliente do utilizador (`~/.grok` / Claude config) — **local, não secretos no git**.  
- [ ] Criar: `docs/GHIDRA_MCP_SETUP.md` com:
  - requisitos (Ghidra 11+/12, JDK)
  - tools permitidas: decompile, xrefs, rename, get_function  
  - tools **proibidas** no workflow: “export whole program rewrite”, “patch binary blindly”  
- [ ] Policy no playbook:
  - MCP = **leitura + anotações**  
  - Código de produto = lifter/hooks/runtime só com diff revisto e teste  

**Aceite M4.3 (soft):**
- [ ] Doc de setup  
- [ ] 1 sessão demo: agente devolve callers de um EA GoW2 **igual** a `ghidra_lookup` (paridade)

**Se MCP falhar/instável:** M4.2 sozinho basta para fechar Track R.

### Task M4.4 — Skill / nota para agentes no monorepo

**Criar (opcional mas útil):**  
`.claude/commands/` ou secção em `CLAUDE.md`:

> Em trabalho de wall guest: (1) ghidra_lookup, (2) não editar ppu_recomp em massa sem patch/midasm, (3) smoke PID, (4) sem forjar aceite.

---

## 9. M5 — Hardening e fecho do milestone

### Task M5.1 — Integração no fluxo de re-lift

**Sequência canónica a documentar e scriptar se ainda faltar elos:**

```text
1. find_functions / functions.json (se bounds mudaram)
2. ppu_lifter --config gow2_recomp.toml --functions ... -o LIFT_NEW
3. (não) apply correctness patches migrados
4. apply_all_patches.sh  # só PROBE / LEGACY
5. verify_lift.sh LIFT_NEW
6. build_macos.sh
7. smoke intro 25–40s
8. accept_relift / rsync promote só se 5–7 verdes
```

- [ ] Actualizar `docs/MACOS_PORT_PLAN.md` ou `games/gow2/README` com esta sequência  
- [ ] `accept_relift.sh` chama `verify_lift` se ainda não chamar  

### Task M5.2 — Suite de testes do milestone (checklist único)

Correr e registar rc em `games/gow2/notes/milestone-2026-08-xenon-re-results.md`:

| # | Comando | rc esperado |
|---|---------|-------------|
| T1 | `python3 tools/test_recomp_config.py` | 0 |
| T2 | `python3 tools/test_midasm_emit.py` | 0 |
| T3 | `python3 tools/test_invalid_instructions.py` | 0 |
| T4 | `python3 tools/test_ps3_analyse_jtables.py` | 0 |
| T5 | `python3 tools/test_weak_override_symbols.py` | 0 (após M3) |
| T6 | `python3 tools/test_lift_parity.py` (ou subset CI) | 0 |
| T7 | `games/gow2/lift_baseline` testes (`test_check_contracts`, etc.) | 0 |
| T8 | `./verify_lift.sh $LIFT_PROD` | 0 |
| T9 | Smoke intro 25–40s (secção 10) | st620≥3, sem OOB novo |
| T10 | Re-lift pilot function: midasm present without patch | grep hook |

### Task M5.3 — Métricas de melhoria (antes/depois)

Preencher tabela no results doc:

| Métrica | Antes (M0) | Depois (M5) | Alvo |
|---------|------------|-------------|------|
| `patch_*.py` classe CORRECTNESS activos | _N0_ | _N1_ | N1 ≤ N0 − 3 |
| Fixes re-lift-safe (midasm/weak/toml) | 0 | ≥3 | ≥3 |
| `verify_lift` no accept path | ? | sim | sim |
| Tempo médio “achar callers de EA” | manual | lookup/MCP | &lt; 1 min |
| Regressões intro no smoke pós-midasm | — | 0 | 0 |
| Oracle step opcional documentado | não | sim | sim |

### Task M5.4 — Retro e handoff walls

- [ ] 1 página: o que desbloqueia wall D (typemap natural) com o novo stack  
- [ ] Lista P1 seguinte (mais midasm, não ABI opts)  
- [ ] Explicitar: **opts D / SIMDe E / shader F** ficam **fora** deste milestone (planos próprios)

---

## 10. Receitas de teste (copiar/colar)

### 10.1 Unit tools (motor)

```bash
cd /path/to/ps3recomp
# venv se existir
[ -x .venv/bin/python3 ] && PY=.venv/bin/python3 || PY=python3

$PY tools/test_recomp_config.py
$PY tools/test_midasm_emit.py
$PY tools/test_invalid_instructions.py
$PY tools/test_ps3_analyse_jtables.py
$PY tools/test_switch_tables.py
$PY scripts/test_python.sh   # se agregar suites
```

### 10.2 Verify lift (produção)

```bash
cd /path/to/gow2-recomp   # ou games/gow2 com dados
# LIFT típico: recomp_macos_v2 ou recomp_mid_v2 — o que build_macos usa
./verify_lift.sh ./recomp_macos_v2
echo rc=$?
```

### 10.3 Smoke intro (não-regressão)

```bash
cd /path/to/gow2-recomp
set -a; . ./env_gow2.sh; set +a
unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
export PS3_NO_RSX=1 PS3_PERF_FSM=1 PS3_MOVIE_EOS=0
unset PS3_MOVIE_DONE_MS PS3_VDEC_FORCE_SEQDONE_MS
# Ajustar FORCE se o path de aceite local usar:
# export PS3_VDEC_ASYNC=1 PS3_VDEC_FORCE_SEQDONE_MS=8000

./boot_gow2 EBOOT.ELF > /tmp/m_milestone_smoke.log 2>&1 &
BPID=$!
sleep 25
kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null

grep -E '\[MOVIEFSM\] st620|sticky|FATAL|OOB|002B4274' /tmp/m_milestone_smoke.log | head -40
# FAIL se st620 max < 3 no path baseline, ou OOB 0xFFFF* logo após TLS
```

### 10.4 Prova re-lift-safe (midasm)

```bash
# 1) Lift com --config para um out de teste
# 2) NÃO correr o patch migrado
# 3) grep o símbolo midasm no cpp gerado
grep -n 'gow2_midasm_' recomp_test_out/ppu_recomp_*.cpp | head
# 4) build + smoke
```

### 10.5 Ghidra lookup (RE)

```bash
cd /path/to/gow2-recomp
./analyze_eboot_ghidra.sh   # se necessário
python3 ../ps3recomp/tools/ghidra_lookup.py ghidra_out 0x002550C8 --callers
```

---

## 11. Integração com walls (como isto chega a pixels)

| Wall | Como o milestone ajuda |
|------|------------------------|
| **C / C2** | Jtables + fallthrough sistemático; menos re-break no re-lift |
| **D** shaders | RE playbook + midasm em typemap/ICG sem patch frágil; Ghidra callers |
| **E** pixels | Indirectamente (lift estável); pixels ainda dependem Metal/RSX (M*) |
| **Intro A/B** | Smoke gates impedem regressão ao tocar lifter |

**Não promete:** menu jogável ou first draw só com este milestone.

---

## 12. Riscos e mitigações

| Risco | Mitigação |
|-------|-----------|
| Mid-asm quebra codegen / CR | Flag opt-in via `--config`; testes sintéticos; smoke obrigatório |
| Weak symbols no Apple ld | Documentar Clang; testar `nm`/`ld` no Mac; fallback link-order |
| Jtable false positive | confidence + review humano; `--include-low` só offline |
| MCP instável / permissões | M4.3 soft; playbook CLI é o hard requirement |
| LLM a “fixar” chunks | CLAUDE.md + playbook + rejeição em review |
| Divergência monorepo vs `gow2-recomp` | `sync-check` sha (item 5 plano auto-re) se ainda doer |
| Scope creep ABI opts | Explicit out of scope até smoke thr multi-sessão |

---

## 13. Ordem de execução recomendada (checklist mestre)

- [ ] **M0.1** Ledger patches  
- [ ] **M0.2** Baseline smoke + verify  
- [ ] **M1.1** Emit midasm + unit tests  
- [ ] **M1.2** Hooks link Mac  
- [ ] **M1.3** Piloto CE03C (ou 1º P0)  
- [ ] **M2.1** `ps3_analyse` + testes  
- [ ] **M2.2** Switch emission + 1 jtable real  
- [ ] **M4.2** Playbook RE (em paralelo após M0)  
- [ ] **M4.1** Oracle opcional no verify  
- [ ] **M3.1** Weak wrappers  
- [ ] **M3.2** +2 migrações P0  
- [ ] **M4.3** MCP doc (opcional)  
- [ ] **M5.*** Suite T1–T10 + metrics + results doc  

**Commits:** português, paths explícitos, sem `git add -A`, sem Co-Authored-By, sem dados de jogo.

---

## 14. Definition of Done (DoD) — checklist final

- [ ] ≥3 correctness fixes re-lift-safe (midasm e/ou weak e/ou switch TOML)  
- [ ] `test_midasm_emit` + `test_ps3_analyse_jtables` (e weak se M3) PASS  
- [ ] `verify_lift.sh` no path de aceitação de re-lift  
- [ ] Smoke intro não regrediu  
- [ ] `docs/RE_PLAYBOOK_GOW2.md` existe e foi usado 1×  
- [ ] `patch-migration-ledger` actualizado  
- [ ] `docs/RECOMP_CONFIG.md` descreve midasm/switch consumidos de verdade  
- [ ] Results doc com métricas antes/depois  
- [ ] Nenhuma activação de opts ABI por default  
- [ ] Nenhum uso de LLM como emissor do lift  

---

## 15. Handoff de execução

1. **Começar por M0.1** (ledger) — 1–2 h, desbloqueia priorização.  
2. Em paralelo possível: **M4.2** playbook (só docs).  
3. Implementação pesada: **M1 → M2 → M3**.  
4. Fechar com **M5** e só então declarar milestone.

**Opções:**
- Subagent por task (recomendado)  
- Inline com review a cada M*  

**Entrada default:** M0.1 inventário de patches.

---

## 16. Referências

- [XenonRecomp README](https://github.com/hedge-dev/XenonRecomp) — TOML, midasm, weak, analyse  
- `docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md`  
- `docs/PLANO_ADOPCAO_AUTO_RE_AGENT.md`  
- `docs/RECOMP_CONFIG.md`  
- `docs/RPCS3_AS_REFERENCE.md`  
- `docs/TOOLS.md` (Ghidra, oracle)  
- `CLAUDE.md` — regras de aceite e Mac  

---

*Fim do plano de milestone. Próximo passo humano/agente: executar M0.1 e abrir o ledger.*
