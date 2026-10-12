# Phase 17: Mid-asm de ponta a ponta - Context

**Gathered:** 2026-08-02
**Status:** Ready for planning
**Mode:** Smart discuss (autónomo) — zonas cinzentas propostas com recomendação e decididas
sem pausa. Uma delas (o piloto) tem consequência real e está justificada com medição.

<domain>
## Phase Boundary

Esta fase liga a cadeia inteira do mid-asm: um `[[midasm_hook]]` declarado no TOML faz o
lifter emitir uma chamada C, o hook host compila, o `boot_gow2` liga o símbolo, e um fix
real deixa de precisar de `patch_*.py` para sobreviver a um re-lift.

**Dentro:** emissão no `ppu_lifter.py` atrás de `--config`; testes unitários da emissão;
golden de regressão (sem `--config` o output não muda); `games/gow2/hooks/` com os stubs e o
corpo do piloto; wire no build do macOS; migração do piloto **CE03C** com prova de re-lift;
`docs/RECOMP_CONFIG.md` a descrever o que é realmente consumido.

**Fora:** weak overrides de função inteira (Fase 19), switch tables (Fase 18), opts ABI
(fora do marco), qualquer alteração ao `spu_lifter`.

</domain>

<decisions>
## Implementation Decisions

### O piloto: CE03C, e porquê não o 2550C8

- **O piloto é `patch_ce03c_introseq_block.py`** (EA `0x000CE03C`, P0 no ledger,
  `alvo=midasm`, aceite `grep:[INTROSEQ] CE03C wait-idle 1st movie`). É o caso de uso
  canónico: `func_000CE03C` tem **143 linhas no lift de produção e o lifter gera 27** — as
  outras **116 são uma edição manual** que, até 2026-07-25, *nenhum script repunha*. Um
  re-lift limpo deixava a função nas 27 linhas geradas. É exactamente o defeito que o
  mid-asm existe para eliminar.
- **O `patch_fallthrough_2550c8.py` NÃO é o piloto**, apesar de estar como `alvo=midasm` no
  ledger e de ser P0. A causa é um **fallthrough cross-fragment errado do lifter**
  (`func_002550C8` caía em `func_002550E8` em vez de `func_00255178`) — um defeito de
  *tradução*, não uma lacuna de comportamento. O plano de origem é explícito: *"Preferir
  fix no lifter se sistemático; senão midasm/switch"*. Envolvê-lo num hook esconderia um
  bug do lifter atrás de código host.
  **Acção:** esta fase corrige o `alvo` dessa linha do ledger de `midasm` para o mecanismo
  certo, com a razão escrita. Não o migra — isso é trabalho do lifter, e a auditoria
  sistemática do padrão continua na lista P1.
- **O contrato de RECUSA do patch actual é o modelo a preservar.** O
  `patch_ce03c_introseq_block.py` recusa (`rc=2`) quando o corpo gerado não é o esperado,
  em vez de adivinhar. O hook mid-asm tem de manter essa propriedade: se a instrução-âncora
  no EA declarado não for a esperada, o **lifter** falha ruidosamente — nunca emite o hook
  no sítio errado em silêncio.

### Contrato do hook

- **Assinatura:** `void gow2_midasm_<Name>(PPUContext* ctx)` — um único parâmetro, o
  contexto guest. O nome exacto do tipo segue o que o lift já usa; confirmar no código
  gerado, não assumir.
- **Emissão:** antes da instrução por default; **depois** quando `after_instruction = true`.
  Com `returns = true` (a chave TOML é `return`), emitir `return;` logo a seguir ao hook.
  O `recomp_config.py` já parseia os três campos e já rejeita `return` + `jump_address`
  em simultâneo — **não reimplementar validação que o parser já faz**.
- **Sem `--config`, zero mudança.** É o golden de regressão e é inegociável: um diff
  byte-a-byte numa fixture pequena tem de ser vazio. Se não for, a emissão está errada.
- **O hook não substitui a instrução** — corre ao lado dela. Substituir a função inteira é
  weak override, Fase 19.

### Onde vivem os hooks e como ligam

- **`games/gow2/hooks/gow2_midasm_hooks.h` + `.cpp`**, versionados no monorepo. É a
  fronteira: o `.cpp` gerado passa a chamar código **versionado e compilado**, em vez de
  conter 116 linhas injectadas por um script de texto.
- **Todos os hooks declarados no TOML têm stub no-op**, mesmo os não implementados. Sem
  isso, um hook declarado e não implementado dá erro de link em vez de no-op — e o
  princípio G2 (probes OFF por default, zero mudança de comportamento) exige o contrário.
- **Wire no `games/gow2/build_macos.sh`**, seguindo o padrão já usado para
  `host_gow2_factory.cpp` e `host_gow2_f2b.c` — incluindo o **guard anti-duplicate-symbol**.
  Lição registada no marco v1.0: 5 das 7 funções do F2B eram `static` no lift real, não
  `extern "C"`, e um guard só-extern teria deixado estado duplicado em silêncio. Verificar
  a linkagem real antes de escolher o guard, não por analogia.
- **Windows não regride** (G4): nada de `Sleep(`, `<intrin.h>`, `__declspec` ou
  `GetModuleHandleA` fora de `#ifdef _WIN32`.

### A prova, e o que a torna válida

- **A prova que define a fase:** re-liftar `func_000CE03C` com `--config` e **sem** correr
  o `patch_ce03c_introseq_block.py`, e o hook está no `.cpp` gerado. Um `grep` do símbolo
  no output é o aceite — o mesmo tipo de aceite executável que o ledger já usa.
- **Não promover o lift de teste a produção.** O re-lift vai para um directório novo
  (padrão `recomp_macos_v3` do marco v1.0, com a guarda que aborta se o alvo for a
  produção). A promoção tem gate próprio (`accept_relift.sh`) e não é desta fase.
- **Smoke de não-regressão contra o baseline da Fase 16:** `st620_max ≥ 3` (o baseline
  mediu **11**), `OOB/0xFFFF = 0` (baseline **0**), kill por PID, log fora dos repos.
- **Ressalva herdada, e é importante:** o baseline da Fase 16 mediu um binário de 29/jul
  contra um lift de 31/jul. Para o smoke desta fase comparar alguma coisa, **tem de
  reconstruir o binário a partir do lift corrente** — senão compara duas coisas diferentes
  e o resultado não significa nada. Se o rebuild não for viável, isso é o resultado e vai
  escrito, não contornado.
- **`sticky` continua NÃO-OBSERVÁVEL** (medido na Fase 16: os `ps3_fios_sticky_*` não têm
  um único `fprintf`). Não usar `sticky_pub>=1` como critério de não-regressão nesta fase —
  mediria o nada. Se alguém quiser esse critério, primeiro instrumenta.

### Claude's Discretion

- O ponto exacto de inserção no `ppu_lifter.py` (dentro de `_translate`, de `lift_function`,
  ou no ponto de junção) — desde que o golden sem `--config` fique byte-a-byte igual.
- A forma do dicionário `ea -> list[MidasmHook]` e como sobrevive ao lift paralelo
  (`_worker_init` / `_worker_lift` propagam estado por `_WORKER_STATE` — o mesmo caminho
  que `invalid_instructions` já usa é o precedente óbvio).
- Se os 116 linhas do CE03C vão para uma função host única ou se decompõem.
- O nome do hook no TOML.

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets

- **`tools/recomp_config.py:27` `MidasmHook`** — já tem `name`, `address`, `jump_address`,
  `after_instruction`, `returns`. `_parse_midasm_hook` (`:135`) já valida chaves obrigatórias
  e já rejeita `return` + `jump_address` juntos (`:146`). `load_recomp_config` (`:162`) já
  devolve `midasm_hooks` (`:107`). **O parser está feito — falta só a emissão.**
- **`tools/test_recomp_config.py`** (276 linhas) — o padrão de teste a seguir.
- **`tools/ppu_lifter.py:3862` `--config`** — a flag já existe e já consome
  `[[invalid_instructions]]` (`:3892-3899`) e `[[switch_table]]` via `merge_switch_tables`
  (`:3595`). O caminho de "config opcional, lifter corre exactamente como antes quando
  omitida" (`:3893`) é o precedente exacto a replicar para o mid-asm.
- **`_worker_init` (`:3707`) / `_worker_lift` (`:3733`)** — propagação de estado para o lift
  paralelo; `invalid_instructions` viaja por aqui (`:3730`, `:3755`). Os hooks têm de fazer
  o mesmo ou o mid-asm desaparece sob `-j`.
- **`games/gow2/config/gow2_recomp.toml`** — já tem `[main]` e `[optimizations]`; o próprio
  comentário do ficheiro diz que `midasm_hook` entra "na Task A3 (migração do CE03C
  wait-idle)". É esta fase.
- **`games/gow2/build_macos.sh`** (342 linhas) — já compila e liga `host_gow2_factory.cpp` e
  `host_gow2_f2b.c` condicionalmente, cada um com guard anti-duplicate-symbol.
- **`games/gow2/lift_baseline/CONTRACTS.tsv`** + `check_contracts.py` — o marcador
  `[INTROSEQ]` ganha contrato aqui.

### Established Patterns

- **Config opcional que não muda nada quando ausente** — `invalid_instructions` e
  `switch_table` já se comportam assim; o mid-asm segue.
- **Duplicados são last-wins e WARNED, não silenciosos** (`:3633-3634`). Hooks duplicados no
  mesmo EA devem seguir a mesma regra.
- **Teste antes do código** — o 16-01 commitou o teste em RED antes do gerador. Mesmo padrão.
- **Guard anti-duplicate-symbol com a linkagem verificada**, não assumida (lição do F2B).
- **Marcador + contrato**: cada bloco instalado tem um marcador grepável e uma linha em
  `CONTRACTS.tsv` que `check_contracts.py` valida independentemente do que o patch imprime.

### Integration Points

- O ledger `PATCH_MIGRATION.tsv` é onde o CE03C passa a `estado=migrated` — e é o
  numerador da métrica "fixes re-lift-safe" que a Fase 21 mede.
- `docs/RECOMP_CONFIG.md` (76 linhas, stub nas secções mid-asm) fecha o XEN-06.
- A Fase 19 herda o padrão de hooks host desta fase para os weak overrides.

</code_context>

<specifics>
## Specific Ideas

- O corpo do CE03C, pelo comentário do próprio patch: *"Intro idx-2 natural 2nd Play (guest
  already calls Play here). Wait-idle only: CE03C often races while st620!=0 -> SAI CEDO.
  Pump until natural MovieStop (st=0); arm +0x714 at st>=10 for EOS. Clear sticky EOS hook
  + done timer so re-Play is not poisoned."*
- O patch inclui a chamada a `movie_done_timebased_reset()`, o que torna o
  `patch_ce03c_movie_done_reset.py` redundante quando corre primeiro. Ao migrar, decidir o
  destino desse segundo patch — e escrevê-lo no ledger, não deixá-lo órfão.
- O patch **não** inclui as linhas de probe (`SCHEDARM-PROBE`, `MENUPRESENT-PROBE`): essas
  têm escritores próprios e continuam patches. A fronteira entre "fix permanente" e "probe"
  já está desenhada aqui — respeitá-la.

</specifics>

<deferred>
## Deferred Ideas

- **Auditoria sistemática do fallthrough cross-fragment** (o padrão do 2550C8: alvo com EA
  guest MENOR que o site). É trabalho do lifter, está na lista P1 do CLAUDE.md, e não cabe
  nesta fase.
- **Instrumentar `ps3_fios_sticky_*`** para tornar o `sticky` observável. Achado da Fase 16,
  registado em `WINDOWS.md`; é uma fase própria ou dívida.
- **`jump_address`** do `MidasmHook` — o parser suporta, mas o plano de origem só pede
  `returns` na v1. Deixar declarado como não-implementado em vez de meio-implementado.
- Migrar os restantes patches `alvo=midasm` do ledger além do piloto — Fase 19.

</deferred>
