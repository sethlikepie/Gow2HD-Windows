# Phase 16: Inventário de patches e baseline medido - Context

**Gathered:** 2026-08-02
**Status:** Planned (16-01 / 16-02 / 16-03) — ready for execute
**Mode:** Smart discuss (autónomo) — as zonas cinzentas foram propostas com recomendação e
decididas sem pausa por serem todas reversíveis e nenhuma tocar código de produção.

<domain>
## Phase Boundary

Esta fase produz **um ledger** e **uma linha de base medida**. Não migra nenhum patch, não
toca no lifter, não escreve hooks. O que entrega é a informação sem a qual as Fases 17–21
não sabem o que atacar primeiro nem contra que número medir a melhoria.

**Dentro:** regenerar o catálogo de patches contra o corpus real, derivar o ledger de
migração, preencher à mão as colunas de decisão de ≥15 patches (≥5 P0 com EA e aceite),
correr uma vez o `verify_lift.sh` e um smoke de intro contra a produção e registar os
números.

**Fora:** migrar qualquer patch (Fases 17–19), apagar patches órfãos, reconciliar o
espelho subtree `games/gow2/`, corrigir um `verify_lift` que nasça vermelho.

</domain>

<decisions>
## Implementation Decisions

### Forma e lugar do ledger

- **Formato TSV**, não Markdown narrativo. É o formato padrão do projecto
  (`PATCH_CATALOG.tsv`, `CONTRACTS.tsv`, `MANIFEST.tsv`) e o único que um gate consegue
  ler. O plano de origem oferecia as duas; a coerência com o que já existe decide.
- **A coluna `classe` é derivada**, por join com `PATCH_CATALOG.tsv` pelo nome do ficheiro
  — nunca copiada à mão. Copiada, diverge na primeira regeneração do catálogo.
- **Só 4 colunas são editadas por humano**: `alvo`, `prioridade`, `aceite`, `estado`. As
  restantes (`patch`, `classe`, `ea`) são derivadas. Isto respeita o contrato já escrito no
  cabeçalho do catálogo: *"catalogo DERIVADO por gen_catalog.py, NUNCA editado a mao"*.
- **Vive em `games/gow2/lift_baseline/PATCH_MIGRATION.tsv`** (monorepo, ao lado do
  catálogo e dos contratos), não em `notes/*.md` — uma nota não é consumível por gate.

### O universo real a classificar

- **A fonte de verdade da contagem é `../gow2-recomp/recomp_mid_v2` = 139 patches**, porque
  é esse o corpus que o `build_macos.sh` aplica de facto. O monorepo tem 88 e é espelho
  desactualizado (dívida já declarada no ROADMAP: *"`games/gow2/` é espelho subtree
  desactualizado"*).
- **Regenerar `PATCH_CATALOG.tsv` é o primeiro passo, obrigatório.** Medido nesta sessão:
  o catálogo classifica **89** patches e o corpus real tem **139** — **50 patches são
  invisíveis ao catálogo**, quase todos probes das Fases 9–11 (`patch_2545b0_entry_probe`,
  `patch_254610_empty_list_gate`, `patch_263040_freelist_tag_guard`, …). Classificar para
  migração sem regenerar seria classificar um universo que não existe.
- **Os patches que só existem no checkout de build entram no ledger**, com a divergência
  declarada numa coluna ou nota de cabeçalho. Reconciliar o subtree exige `push`
  autorizado e **não** é escopo desta fase.
- **Os 139 são classificados por script** (a classe vem do catálogo); as **4 colunas de
  decisão são preenchidas à mão em ≥15**, priorizando os `CORRECTNESS`. O "≥15" do plano é
  o mínimo de *decisão humana*, não de cobertura do inventário.

### Critério de prioridade e de aceite

- **P0 = `classe=CORRECTNESS` E toca o caminho de boot medido E tem EA identificável.** Os
  três, não um. Um patch de correctness sem EA não é migrável para midasm nem para weak, e
  por isso não pode ser P0 — vai para P1 com a razão escrita.
- **A coluna `aceite` é uma string executável**, num de três formatos:
  `grep:<padrão>` | `contract:<id do CONTRACTS.tsv>` | `smoke:<métrica>`.
  "Verificar manualmente" é proibido — um aceite que ninguém consegue correr não é aceite
  (G5: toda afirmação declara o nível de evidência).
- **Os PROBE entram no ledger** com `alvo=keep-probe` e sem prioridade. Ficam de fora da
  contagem de "correctness activos", mas dentro do denominador — senão a métrica de
  melhoria da Fase 21 mede um universo escolhido a dedo.
- **Os 2 patches `orfao-sem-fonte` recebem `alvo=delete` como proposta e `estado=todo`.**
  Ninguém apaga nada nesta fase; a proposta fica registada para decisão posterior.

### O baseline medido

- **Mede-se o lift de produção** — o que o `build_macos.sh` consome hoje, não um lift de
  teste.
- **Uma corrida de smoke**, não seis. O critério desta fase é "correr uma vez e registar";
  o portão de ≥4-de-6 pertence às fases que provam uma correcção. (Contraste deliberado
  com a decisão da Fase 10, onde 3 corridas era o mínimo para um *veredicto* — aqui não há
  veredicto, há registo.)
- **Os logs ficam fora dos dois repositórios** (G6); só os números entram no ledger:
  `rc` do `verify_lift`, `st620` máximo, `sticky`, e presença/ausência de OOB pós-TLS.
- **Se o `verify_lift.sh` ou o smoke nascerem vermelhos, isso fica escrito como estado
  inicial.** Não se corrige nesta fase — corrigir aqui falsificaria a linha de base contra
  a qual a Fase 21 vai medir a melhoria.

### Claude's Discretion

- O nome exacto do script que deriva o ledger e se ele estende o `gen_catalog.py` ou é
  irmão dele.
- A ordem das colunas no TSV, desde que as 7 obrigatórias do plano existam.
- Como representar a divergência monorepo/build (coluna dedicada vs nota de cabeçalho).
- Quais dos 139 patches entram nos ≥15 classificados à mão, desde que os ≥5 P0 saiam dos
  `CORRECTNESS` e o critério de escolha fique escrito.

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets

- **`games/gow2/lift_baseline/gen_catalog.py`** — gera o `PATCH_CATALOG.tsv` a partir de um
  directório de patches + `MANIFEST.tsv`. Comando registado no próprio cabeçalho do TSV:
  `gen_catalog.py ../gow2-recomp/recomp_mid_v2 games/gow2/lift_baseline/MANIFEST.tsv`.
- **`PATCH_CATALOG.tsv`** — 8 colunas (`patch`, `escreve`, `classe`, `subclasse`,
  `no_gate`, `razao`, `marcador`, `chunk_alvo`). Já dá `classe` (64 FUNCIONAL / 25 PROBE)
  e `subclasse` (23 diagnostico, 18 misc, 12 fios, 10 opd, 7 type15, 3 tymap, 2 shader,
  2 orfao-sem-fonte, 2 jumptable, 2 host, 2 guard, 2 factory, 2 dispatch-fix, 1 wadld,
  1 dispatch). A subclasse já é quase o mapa de migração: `jumptable` → switch_table,
  `opd`/`factory` → weak, `fios`/`type15` → midasm ou runtime.
- **`games/gow2/lift_baseline/CONTRACTS.tsv`** (88 linhas) + `check_contracts.py` (198) —
  a coluna `aceite` no formato `contract:<id>` liga-se directamente a isto.
- **`games/gow2/verify_lift.sh`** (155) e **`accept_relift.sh`** (221) — o baseline do
  critério 4 sai de os correr, não de os reescrever.
- **`games/gow2/apply_all_patches.sh`** (465) — tem `--check` e `--status <tsv>`; a
  classificação em seis estados que ele já produz é entrada útil para a coluna `estado`.

### Established Patterns

- **TSV com cabeçalho `#` comentado** que documenta como regenerar o ficheiro e congela a
  linha de base histórica — padrão visível no `PATCH_CATALOG.tsv` e no `CONTRACTS.tsv`.
  O ledger deve seguir o mesmo.
- **Derivado vs editado é uma fronteira explícita** no projecto, escrita no próprio
  ficheiro. Respeitar essa fronteira é o que impede o ledger de apodrecer.
- **Números medidos e reconciliados, não copiados**: o cabeçalho do catálogo já regista
  uma reconciliação anterior ("corrigiu os 73/71 desactualizados do ROADMAP"). O mesmo
  rigor aplica-se à divergência 88/139/89 desta fase.
- **Medição com kill por PID, nunca `pkill -f boot_gow2`** (G6, CLAUDE.md).

### Integration Points

- O ledger é consumido pelas Fases 17–19 (quem escolhe o piloto de migração) e pela
  Fase 21 (que mede o antes/depois). A coluna `estado` é o que muda ao longo do marco.
- A contagem baseline congelada no cabeçalho é a referência da métrica
  "`patch_*.py` de classe CORRECTNESS activos" da tabela antes/depois da Fase 21.

</code_context>

<specifics>
## Specific Ideas

- O plano de origem (§4, Task M0.1) fixa as 7 colunas obrigatórias e os seus domínios:
  `classe` ∈ {CORRECTNESS, PROBE, OPD, BUILD, OBSOLETE};
  `alvo` ∈ {midasm, weak, switch_table, invalid_insn, runtime, keep-probe, delete};
  `prioridade` ∈ {P0, P1, P2}; `estado` ∈ {todo, migrated, wont}.
  Nota: o vocabulário de `classe` do plano **não** é o do `PATCH_CATALOG.tsv`
  (FUNCIONAL/PROBE) — o mapeamento entre os dois tem de ficar escrito, não implícito.
- Os candidatos nomeados pelo plano como de maior impacto: fallthrough (`2550C8`),
  jumptable (`2a209c`, `2b11b8`), `CE03C`, factory OPD / ICG ctor, host inflate / WAD path.
- O plano avisa que o *host inflate / WAD path* já é runtime HLE parcial — deve ser
  **documentado, não forçado a midasm**.

</specifics>

<deferred>
## Deferred Ideas

- **Reconciliar o espelho subtree `games/gow2/`** (88 vs 139 patches). Exige `push`
  autorizado; é dívida declarada do v1.0 e não entra neste marco.
- **Apagar os patches `orfao-sem-fonte`.** O ledger propõe; a decisão é de outra sessão.
- **Mapa marcador→escritor** para os 246 marcadores do MANIFEST (a ressalva por fechar do
  `REQ-RDY0-4`). Adjacente, mas é outro trabalho.
- **Reduzir o tempo do `apply_all_patches.sh`** (~2m14s porque o `check_contracts.py` relê
  o lift a cada chamada) — dívida declarada, não desta fase.

</deferred>
