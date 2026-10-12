# Phase 18: Jump tables detectadas e declaradas - Context

**Gathered:** 2026-08-02
**Status:** Ready for planning
**Mode:** Smart discuss (autónomo). Zonas cinzentas propostas com recomendação e decididas
sem pausa — nenhuma é irreversível.

<domain>
## Phase Boundary

Uma jump table deixa de ser descoberta à mão e remendada por patch: o `ps3_analyse.py`
acha-a no ELF, o TOML declara-a, e o lifter emite `switch` real.

**Dentro:** `tools/ps3_analyse.py` (detector v1 com `confidence`), o seu teste sobre
fixture sintética, `games/gow2/config/gow2_switch_tables.toml` populado, reactivação da
chave `switch_table_file`, e migração de ≥1 dos dois `patch_jumptable_*.py` P0.

**Fora:** reescrever `discover_jump_tables` (já existe), mudar o codegen do `bctr`
(já emite `switch`), `default` diferente de `indirect` (não tem representação interna —
exigiria mudança de codegen, fora de escopo por decisão do parser).

</domain>

<decisions>
## Implementation Decisions

### O que já existe e NÃO se reimplementa

- **`merge_switch_tables` (`ppu_lifter.py:3832`) está feito** — Task B2, com a política
  "merge com `discover_jump_tables`; config ganha no conflito" e duas armadilhas já
  documentadas no docstring (alvo declarado que cai fora da função que o contém).
- **A emissão de `switch` já existe** (`:1536-1538`): `case 0x{c:08X}u: goto loc_{c:08X};`,
  com `default: ps3_indirect_call(ctx); return;` (`:1541`). O lifter **não** cai em
  trampolim quando a tabela é conhecida. O plano de origem suspeitava que caísse — a
  medição diz que não. Esta fase alimenta o mecanismo, não o constrói.
- **`SwitchTable` (`recomp_config.py:61`) e `load_switch_tables` (`:271`) estão feitos.**
  Notas do parser que vinculam: `dispatch` é o endereço do **`bctr`**, não do `mtctr`
  anterior; `cases` é achatado a endereços de destino (o `index` é aceite por
  compatibilidade de schema e **descartado**, porque a emissão chaveia por endereço, não
  por posição); `default` só aceita a string literal `"indirect"`.

### O detector

- **`tools/ps3_analyse.py` é ferramenta nova**, irmã do lifter, não uma flag dele. CLI:
  `-o <toml>` e `--include-low`, como o plano de origem fixa.
- **Heurística v1:** `mtctr` seguido, dentro de N instruções, de `bctr`/`bcctr`; backtrack
  de `rlwinm`/`lwzx` para achar a base da tabela; alvos big-endian dentro de `.text` e
  dentro dos bounds das funções. **Os limites da heurística ficam escritos**, não implícitos.
- **`confidence` por tabela, e por default só as `high` são emitidas.** As de baixa
  confiança exigem `--include-low` e **nunca** entram no TOML de produção sem revisão
  humana. Uma jump table errada não é um teste que falha — é o boot a saltar para lixo.
- **O oráculo são os patches existentes.** Corrido contra o EBOOT local, o detector tem de
  reencontrar as tabelas que os `patch_jumptable_2a209c.py` e `patch_jumptable_2b11b8.py`
  já conhecem. A concordância com um patch validado é a prova; sem oráculo, um detector
  novo só produz confiança injustificada.
- **O dump bruto do EBOOT não se commita** (regra de dados de jogo). Só o TOML derivado,
  que contém endereços — não conteúdo do binário.

### A dívida herdada que esta fase fecha

- **A chave `switch_table_file` está COMENTADA no `gow2_recomp.toml`.** O 17-03 mediu
  porquê: com a chave activa e o ficheiro inexistente, o lifter aborta em
  `switch table file not found` e **o `--config` inteiro morre** — o que levaria o mid-asm
  a desaparecer junto. Esta fase cria o ficheiro e **reactiva a chave**, e o smoke tem de
  provar que o mid-asm do CE03C continua a ser emitido depois disso.
  Sem essa prova, esta fase pode partir a Fase 17 em silêncio.
- **Isso fecha o `XEN-06` de vez**: hoje está qualificado como "fecha só para o mid-asm";
  com o `switch_table_file` consumido de verdade, a documentação passa a descrever duas
  secções realmente consumidas.

### A migração

- **≥1 dos dois `patch_jumptable_*.py` fica `migrated` ou `redundant`**, com prova de
  re-lift — o mesmo padrão do CE03C: re-liftar **sem** o patch e o `switch` continua lá.
- Os aceites já estão escritos no ledger e são executáveis:
  `grep:case 0x002A27BCu: goto loc_002A27BC;` (2a209c) e
  `grep:case 0x002B12C4u: goto loc_002B12C4;` (2b11b8).
- **`redundant` é um resultado legítimo e provável.** Se o `discover_jump_tables` já
  encontra a tabela sozinho, o patch nunca foi necessário — e dizê-lo é mais valioso que
  fabricar uma migração. Medir isso primeiro (re-lift sem o patch e sem a declaração)
  distingue `redundant` de `migrated`, e a distinção vai escrita no ledger.
- **A métrica não se infla:** um patch `redundant` **não** conta para os ≥3 fixes
  re-lift-safe do XEN-05 — não foi migrado nada, descobriu-se que não era preciso.
  Contam-se separadamente, e a Fase 21 mede os dois números.

### Não-regressão

- Smoke contra o baseline: `st620_max ≥ 3` (baseline **11**), `OOB/0xFFFF = 0`.
  Kill por PID, logs em `/tmp`. **`sticky` não serve de critério** — não é observável.
- **O lift de teste vai para directório novo**, nunca por cima de `recomp_macos_v2`.
- Uma jump table declarada errada é um salto para endereço inválido. Se o smoke der OOB
  novo, **isso é falha da fase**, não ruído.

### Claude's Discretion

- A janela N entre `mtctr` e `bctr`, e a profundidade do backtrack.
- A escala de `confidence` (booleana high/low, ou numérica com limiar).
- Se o `ps3_analyse.py` partilha código de desassemblagem com o `ppu_lifter.py` por import
  ou se duplica o mínimo necessário.
- Qual dos dois patches jumptable é o piloto.

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets

- `tools/ppu_lifter.py:3641 discover_jump_tables(all_insns, read_u32, toc, text_lo, text_hi)`
  — a análise estática que já existe; o detector novo é a rede para quando esta não chega.
- `tools/ppu_lifter.py:3832 merge_switch_tables(discovered, declared, func_bounds, warn=None)`
  — merge já implementado, com warns para duplicados e alvos fora de bounds.
- `tools/recomp_config.py:61 SwitchTable` + `:271 load_switch_tables` — parser feito.
- `tools/test_switch_tables.py` (511 linhas) — a suite que já cobre o merge; estender, não
  duplicar.
- `tools/test_midasm_emit.py` + `tools/testdata/midasm_golden_noconfig.c` (Fase 17) — o
  padrão de golden por `diff` e de prova por mutação, a repetir aqui.
- `games/gow2/lift_baseline/PATCH_MIGRATION.tsv` — os dois P0 com EA e aceite já escritos.

### Established Patterns

- **Config opcional não muda nada quando ausente** — mas atenção: uma chave *presente* a
  apontar para ficheiro *inexistente* **aborta** (medido no 17-03). Não é o mesmo caso.
- **Teste em RED antes do código** (16-01, 17-01).
- **Golden provado por mutação** — injectar o defeito, ver falhar, reverter (17-01).
- **Duplicados last-wins e WARNED, nunca silenciosos** (`:3633-3634`).
- **Oráculo antes de confiança**: o projecto já usa patches validados como oráculo de
  ferramentas novas.

### Integration Points

- `switch_table_file` reactivado afecta **toda** a cadeia `--config`, incluindo o mid-asm
  da Fase 17. O smoke desta fase é também o teste de não-regressão da anterior.
- O ledger é o numerador da métrica do XEN-05, medida na Fase 21.
- A Fase 19 herda o padrão de prova de re-lift.

</code_context>

<specifics>
## Specific Ideas

- O plano de origem (§6) sugeria que o lifter "ainda cai em trampolim" — **refutado por
  medição**: `:1536-1541` já emite `switch` com `default: ps3_indirect_call`. A Task B2 do
  plano Xenon está mais avançada do que o plano de milestone assumia.
- Os dois EAs P0: `0x002A209C` e `0x002B11B8`. Os alvos de aceite: `0x002A27BC` e
  `0x002B12C4`.
- `--include-low` só offline, nunca no TOML de produção sem revisão (§12, mitigação de
  "jtable false positive").

</specifics>

<deferred>
## Deferred Ideas

- `default` diferente de `indirect` — exige mudança de codegen; o parser já o declara fora
  de escopo.
- Migrar os restantes patches com `subclasse=jumptable` além do piloto — Fase 19 ou P1.
- Auditoria sistemática do fallthrough cross-fragment (`alvo=lifter` do `2550C8`) —
  trabalho do lifter, lista P1 do CLAUDE.md.

</deferred>
