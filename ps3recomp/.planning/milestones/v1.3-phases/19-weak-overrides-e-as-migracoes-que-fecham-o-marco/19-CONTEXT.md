# Phase 19: Weak overrides e as migrações que fecham o marco - Context

**Gathered:** 2026-08-02
**Status:** Ready for planning
**Mode:** Smart discuss (autónomo).

<domain>
## Phase Boundary

Duas coisas: (1) o mecanismo de **weak override** — substituir uma função guest inteira sem
editar o `.cpp` gerado; (2) as **migrações reais** que levam o `XEN-05` de 2 a ≥3.

**Esta é a fase que decide se o marco fecha.** As Fases 17 e 18 entregaram mecanismos; o
`XEN-05` continua em **2 linhas `migrated` / 1 mecanismo** (verificado na Fase 18).

**Dentro:** `runtime/ppu/ppc_hooks.h`, emissão de wrappers fracos atrás de flag,
`tools/test_weak_override_symbols.py`, `games/gow2/hooks/gow2_func_overrides.cpp`, e ≥2
migrações novas com prova de re-lift.

**Fora:** opts ABI, `jump_address`, SIMDe, auditoria do fallthrough cross-fragment
(`alvo=lifter`, trabalho do lifter).

</domain>

<decisions>
## Implementation Decisions

### O mecanismo

- **`runtime/ppu/ppc_hooks.h`** com `PPC_FUNC_IMPL` / `PPC_FUNC`, **Clang-first**
  (`__attribute__((weak))`), com ramo não-Clang que degrada para símbolo forte. macOS/arm64
  é a plataforma primária (G4).
- **Emissão atrás de flag** (`--emit-weak-wrappers` ou chave no TOML), **nunca por default**.
  O corpo liftado vira `__imp_func_XXXXXXXX`; o wrapper fraco `func_XXXXXXXX` chama-o.
  Sem a flag, o output do lifter não muda — golden de regressão, como no 17-01.
- **O risco a medir, não a assumir:** weak symbols no `ld` da Apple. `tools/test_weak_override_symbols.py`
  tem de provar por `nm` num objecto real que o símbolo fraco existe **e que um override
  forte ganha o link** no toolchain arm64 local. O plano de origem lista isto como risco
  (§12) com mitigação "documentar Clang; testar `nm`/`ld` no Mac; fallback link-order".
  Se o override **não** ganhar, isso é o resultado — e o mecanismo fica declarado inviável
  em vez de fingido.

### O que migrar — e a ordem

**Prioridade 1: o pad de `setjmp`/`longjmp` do CE03C** (`WINDOWS.md` #8, `status: open`).
É o caso que só o weak resolve: um mid-asm corre **ao lado** de uma instrução e não
**envolve** o corpo da função — e num lift com `--config` esse pad nem sequer é reposto
hoje. Migrá-lo:
- fecha a lacuna aberta pela Fase 17;
- **regulariza a contagem do XEN-05**, porque hoje o CE03C está migrado com a
  qualificação "o que ficou re-lift-safe foi o wait-idle, não as 143 linhas".

**Prioridade 2: os P0 `OPD` com `alvo=weak` e EA confirmada:**
`patch_14b1f0_opd.py` (`0x0014B1F0`, `contract:opd-14b1f0`),
`patch_32e200_opd.py` (`0x0032E200`, `contract:opd-32e200`),
`patch_factory_opd_gate.py` (`0x0039E6B4`, `grep:[WADLD-FACT] #A`).
Os aceites já são executáveis e já foram corridos verdes na Fase 16.

**Reserva P1** (se algum P0 se revelar inadequado): `patch_14a01c_opd.py`,
`patch_1856a8_stream_opd.py`, `patch_39e5d8_opd.py`, `patch_groupend_opd.py`,
`patch_opdisp_entry.py`, `patch_wad_state_machine.py`.
**Nota:** `patch_icg_ctor_opd.py` tem `ea='-'` — a Fase 16 recusou promovê-lo por isso, e
essa recusa mantém-se. Sem EA confirmada não há weak.

### A regra que protege a métrica (herdada e reforçada)

- **`redundant` não conta.** A Fase 18 estabeleceu isto e custou-lhe o crédito de duas
  linhas — mantém-se.
- **Contar mecanismos, não linhas de ficheiro.** A Fase 18 mediu que as 2 linhas `migrated`
  actuais são **1 mecanismo** (o `CONTRACTS.tsv` diz que o hook `Ce03cWaitIdle` subsume o
  `movie_done_reset`). Esta fase usa a contagem **por mecanismo** — a mais conservadora —
  e escreve as duas no ledger para a Fase 21 arbitrar com os números à frente.
- **Se o marco acabar com 2 de 3, fecha PARCIAL** com o número escrito. Não se promove uma
  linha sem prova de re-lift para chegar ao 3.

### A prova, por migração

O mesmo padrão que o CE03C estabeleceu e que a Fase 18 refinou:
1. Re-lift **sem** o patch, com a flag/TOML — o override está lá.
2. O aceite do ledger corre verde (`contract:` via `check_contracts.py`, ou `grep:`).
3. **Localizar por função, não por texto solto** — a Fase 18 aprendeu que o texto do aceite
   pode aparecer em mais de uma função; a prova identifica a função que contém.
4. Smoke: `st620_max ≥ 3` (baseline **11**), `OOB/0xFFFF = 0`, `ICALL-BAD = 0`.
   Kill por PID, logs em `/tmp`, nunca `pkill`.
5. Lift de teste em directório **novo**, nunca por cima de `recomp_macos_v2`.

### Claude's Discretion

- Se a flag é CLI (`--emit-weak-wrappers`) ou chave TOML — desde que off por default.
- Se os wrappers fracos são emitidos para todas as funções ou só para as declaradas.
  (Emitir para todas dobra o número de símbolos do lift — medir o custo antes de decidir.)
- Quantas migrações tentar além do mínimo, e por que ordem.
- Como o `gow2_func_overrides.cpp` se organiza face ao `gow2_midasm_hooks.cpp` já existente.

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets

- **`games/gow2/hooks/gow2_midasm_hooks.{h,cpp}`** (Fase 17) — o padrão de hook host
  versionado, já compilado e ligado pelo `build_macos.sh` com guard anti-duplicate-symbol.
  O `gow2_func_overrides.cpp` segue o mesmo caminho de build.
- **O guard do `build_macos.sh` é subtil e já tem uma armadilha resolvida:** exige `{` (ou
  fim-de-linha) depois do `)` e proíbe `;` dentro dos parênteses, porque o lifter emite
  *declarações* no preâmbulo que um `grep` ingénuo confundiria com *definições*. Um guard
  para os overrides tem o mesmo problema — não o reinventes mal.
- **`tools/test_midasm_emit.py`** — o padrão de teste de emissão com golden por `diff` e
  prova por mutação.
- **`games/gow2/lift_baseline/CONTRACTS.tsv`** + `check_contracts.py` — os aceites
  `contract:` dos candidatos OPD já existem e já correram verdes.
- **`ppu_lifter.py`** — `--config` já é o caminho de opções; a flag nova junta-se a ele.

### Established Patterns

- **Flag off por default + golden byte-a-byte** (G2, 17-01).
- **Teste em RED antes do código**; **prova por mutação** obrigatória.
- **Medir antes de classificar** — foi assim que a Fase 18 evitou inflar a métrica.
- **Falhar alto, não em silêncio** — o lifter recusa (`rc=2`) em vez de emitir algo
  subtilmente errado (17-01, `jump_address`).
- **Ficheiros da outra sessão activa** ficam intactos e fora dos commits.

### Integration Points

- `WINDOWS.md` #8 fecha aqui (ou fica declarado com razão).
- O ledger é o numerador do XEN-05, medido na Fase 21.
- A Fase 21 arbitra a contagem final linhas-vs-mecanismos.

</code_context>

<specifics>
## Specific Ideas

- O pad do CE03C, conforme `WINDOWS.md` #8: `g_ce03c_play_abort` +
  `cellVdec_stop_all_for_play_abort` + reconstrução da freelist do media object.
- Alternativa declarada na própria entrada #8: "ou com um patch residual mínimo só para o
  pad". Se o weak não servir, essa é a saída honesta — e vai escrita.
- Aceites já verdes na Fase 16 para os P0 OPD: `contract:opd-14b1f0`, `contract:opd-32e200`,
  `grep:[WADLD-FACT] #A`.

</specifics>

<deferred>
## Deferred Ideas

- Migrar os 6 P1 `weak` restantes — depois do marco.
- `patch_icg_ctor_opd.py` — precisa de EA confirmada primeiro.
- Auditoria sistemática do fallthrough cross-fragment (`patch_fallthrough_2550c8.py`,
  `alvo=lifter`) — trabalho do lifter, lista P1 do CLAUDE.md.
- Reconciliar o espelho subtree — dívida do v1.0.

</deferred>
