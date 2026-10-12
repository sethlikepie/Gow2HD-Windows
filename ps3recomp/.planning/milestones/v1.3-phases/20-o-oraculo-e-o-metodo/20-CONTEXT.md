# Phase 20: O oráculo e o método - Context

**Gathered:** 2026-08-02
**Status:** Ready for planning
**Mode:** Smart discuss (autónomo).

<domain>
## Phase Boundary

A IA/agente passa a ser **navegador do mapa guest** com oráculos determinísticos por trás —
não uma fonte de hipóteses sem verificação. Três entregas: oráculo Ghidra por delta no
`verify_lift`, proveniência do `ghidra_out/`, e o playbook de RE **usado** uma vez.

**Dentro:** `VERIFY_ORACLE=1` opcional no `verify_lift.sh`, `ghidra_out/MANIFEST.json`,
`docs/RE_PLAYBOOK_GOW2.md` + uma sessão real de wall, `docs/GHIDRA_MCP_SETUP.md` (soft).

**Fora:** resolver a wall (o playbook é o método, não a promessa de vitória); mudar o
comportamento default do `verify_lift`; qualquer uso de LLM para emitir lift.

</domain>

<decisions>
## Implementation Decisions

### O oráculo (RE-01)

- **`VERIFY_ORACLE=1` é opt-in. Sem a variável, o `verify_lift.sh` corre exactamente como
  hoje** — mesmos passos, mesmo `rc`. Isto não é preferência: o gate está no caminho de
  aceitação (`accept_relift.sh` perna 2) e mudar o default alteraria o que aceita hoje.
- **Comparação por DELTA contra baseline congelado, nunca limiar absoluto cru.** Um limiar
  absoluto nasceria vermelho e ensinaria toda a gente a ignorá-lo — foi exactamente o que
  aconteceu ao `verify_lift` no v1.0 até ser recalibrado por delta.
- **Prova por mutação, revalidada nesta fase:** truncar uma função numa **cópia** do lift
  tem de dar `rc=1`. Sem essa prova o passo é decorativo. (`audit_boundaries.py` já tem o
  parâmetro `--oracle` referido no cabeçalho do `verify_lift.sh:20` como *não* usado —
  confirmar o que já existe antes de escrever código novo.)
- **`ghidra_out/MANIFEST.json`** com sha256 do ELF analisado, data, versão do Ghidra e
  contagens. Sem isto, "o oráculo diz X" não tem data nem binário — e o `ghidra_out/` actual
  (5 JSONs) não diz de que EBOOT saiu.

### O playbook (RE-02) — e o que o torna real

- `docs/RE_PLAYBOOK_GOW2.md` com as 6 secções mínimas: pergunta de wall (1 frase), âncoras
  (EA / símbolo Ghidra / tag de log), ferramentas, hipótese → **experimento discriminador**,
  aceite medido, anti-padrões.
- **O aceite não é o documento — é tê-lo usado.** Uma página de ledger de uma sessão real
  sobre a wall **D** (typemap/shaders) ou **C2** (texturas), com hipóteses refutadas por
  medição. **"Ainda aberto" é resultado válido**; um fecho forjado não é.
- **Escolher a wall por custo, não por ambição.** As quatro paredes da Fase 11 (v1.1) estão
  documentadas com medição fresca (`func_002545B0`, `func_002182A4`, `func_002547AC`,
  `func_0041F700`) e são candidatos legítimos — mas o objectivo é **exercitar o método**,
  não vencer a parede. Uma sessão que refuta duas hipóteses com medição e escreve porquê
  vale mais que uma que promete uma vitória.
- **Anti-padrões que o playbook tem de nomear** (regra 5 do CLAUDE.md): CRC bypass, stub de
  combination, estampar magic à mão, null-check cosmético, mascarar guard,
  `PS3_SHADER_DEMO` como aceite de conteúdo, e **LLM a reescrever chunk liftado**.

### O MCP (RE-03) — soft por desenho

- `docs/GHIDRA_MCP_SETUP.md` com versão fixada, tools permitidas (decompile, xrefs, rename,
  get_function) e **proibidas** (export/rewrite do programa, patch cego do binário).
- **Política:** MCP = leitura e anotação. Código de produto só com diff revisto e teste.
- **Aceite soft:** 1 sessão de paridade em que o agente devolve os mesmos callers que o
  `tools/ghidra_lookup.py`. **Se o MCP for instável ou exigir instalação interactiva, fica
  `wont` declarado e a trilha fecha na mesma** — o plano de origem diz isto explicitamente
  ("Se MCP falhar/instável: M4.2 sozinho basta para fechar Track R"). Não gastar tempo a
  forçar.

### Herdado da Fase 19 — não é desta fase resolver, mas é desta fase não piorar

O `verify_lift.sh` dá hoje `rc=1` por `MANIFEST FAIL` com **7 itens `NOVO` reais**. O
verifier da Fase 19 corroborou que são pré-existentes (os mesmos 7 aparecem antes das
migrações) mas **não** conseguiu confirmar independentemente a origem. Ao mexer no
`verify_lift.sh`, **não misturar** esse problema com o passo do oráculo: o oráculo é um
passo novo e opcional; a dívida do MANIFEST é da Fase 21. Se descobrires a origem dos 7,
escreve — é informação que a 21 precisa.

### Claude's Discretion

- Se o oráculo é passo próprio ou reutiliza `audit_boundaries.py --oracle`.
- O formato do baseline do oráculo (ficheiro à parte vs secção num existente).
- Qual wall atacar na sessão de RE, e quantas hipóteses levar.
- Se o MCP chega a ser tentado.

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets

- **`games/gow2/verify_lift.sh`** (155 linhas) — orquestra `lift_parity.py`,
  `audit_boundaries.py` (I1/I3) e `verify_lift_baseline.sh`, todos **por delta contra
  baselines congelados**, falhando só por achado NOVO. O cabeçalho (`:20`) diz que corre
  `audit_boundaries.py` **SEM** `--oracle` — logo o parâmetro existe e está por ligar.
- **`games/gow2/accept_relift.sh`** (221) — **já chama** o `verify_lift.sh` na perna 2
  (`:73`). Metade do LEDG-02 já está de pé; a Fase 21 confirma o resto.
- **`../gow2-recomp/ghidra_out/`** — `decompiled.json`, `functions.json`, `names.json`,
  `strings.json`, `symbols.json`. Falta só a proveniência.
- **`tools/ghidra_lookup.py`** (165), `ghidra_analyze.py` (146), `ghidra_names.py` (228),
  `analyze_eboot_ghidra.sh` — o oráculo estático já existe e é o padrão de paridade do MCP.
- **`.planning/WINDOWS.md`** — o ledger de janelas abertas, já usado pelas Fases 17–19 e com
  o vocabulário `open|waived|fixed` (a entrada 7 foi corrigida na 18).

### Established Patterns

- **Opt-in por variável, default inalterado** (G2, e o `--config` das Fases 17–19).
- **Delta contra baseline congelado, nunca limiar cru** — a lição do `verify_lift` no v1.0.
- **Prova por mutação** — injectar o defeito, ver falhar, reverter (16-01, 17-01, 18-01,
  19-01).
- **Aceite executável, nunca "verificar manualmente"** (Fase 16).
- **Diagnóstico honesto é entrega válida** — precedente das Fases 8, 9, 10 e 18.

### Integration Points

- O `verify_lift.sh` está no caminho de aceitação; o passo novo não pode mudar o `rc` do
  caminho default.
- O playbook alimenta a Fase 11 do v1.1 (as quatro paredes) quando ela for retomada.
- A Fase 21 mede o "tempo médio para achar callers de um EA" na tabela antes/depois.

</code_context>

<specifics>
## Specific Ideas

- Comandos que o playbook deve mostrar, já existentes:
  `./analyze_eboot_ghidra.sh`, `python3 tools/ghidra_lookup.py ghidra_out 0xEA`,
  `... --callers`, `... --grep 'SHADER'`.
- Wall D em uma frase, do CLAUDE.md: *"porque é que SHADERSRC tem N=0?"* — o nested walk
  natural do typemap pós-`R_PermA` continua aberto.
- As quatro paredes da Fase 11, medidas a 2026-08-01/02, com sintomas escritos no ROADMAP.

</specifics>

<deferred>
## Deferred Ideas

- Resolver qualquer wall — o playbook é método, não promessa.
- Instrumentar `ps3_fios_sticky_*` (achado da Fase 16: não observável).
- Os 7 itens `NOVO` do `MANIFEST FAIL` — Fase 21.
- `alvo=weak` dos P0 OPD "por rever" (achado da Fase 19: são cirurgia no interior, não
  substituição de função).

</deferred>
