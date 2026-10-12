# Plano de adopção — o que roubar ao `auto-re-agent` e o que recusar

Alvo: `ps3recomp` (motor) + `gow2-recomp` (port GoW2).
Investigação read-only. Nada foi alterado nos dois repos; medições novas em
`/private/tmp/.../scratchpad/{frag,frag2,chain,mut}.py` e `/tmp/audit_oracle_check.json`.

---

## 0. Verificações próprias — correcções ao dossier

Antes de aceitar as 5 listas de recomendações, refiz as medições load-bearing.
Quatro delas mudaram conclusões.

| # | Afirmação do dossier | O que medi | Efeito no plano |
|---|---|---|---|
| V1 | `audit_boundaries --oracle` "aponta nominalmente a regressão" | **Confirmado**: I4=137, I5=5154 (2422 `extende-seguro`, 778 288 bytes); `0x0037C384` sai com `nosso_end 0x0037C38C / oraculo_end 0x0037C3B4 / bytes 40`; `0x00379C70` sai com 6912 bytes (`extende-funde`) | Mantém-se — mas ver V2 |
| V2 | (implícito) o incidente `func_0037C384` está resolvido | **Falso e verdadeiro em metades diferentes**: `functions.json` AINDA diz `end=0x0037C38C` (2 instruções), mas o lift emite as 12 instruções e a `func_00379C70(ctx)` nos dois lifts. A fronteira continua errada; a emissão está certa (recuperada por continuação de fragmento) | O oráculo I5 é um sinal de **fronteira**, não de emissão. Não substitui a paridade; complementa-a |
| V3 | "apertar `TRUNCATE_RATIO` 0.5→0.9 custa ZERO achados" | **Medido dentro da zona cega**. `elf_spans()` corta o span do ELF no *próximo start*, e `starts = functions.json ∪ símbolos do lift`. Há **31 167 fragmentos que só existem no lift**, e **4787 dos 20 750 spans reais são encurtados por fragmentos do lift**. Com starts só de `functions.json`: TRUNCATED passa de **1 → 13** (@0.5) e **15** (@0.9) | O TRUNCATED é **circular pela mesma razão que o NO_BODY**. Corrigir a autoridade dos starts vale mais do que mexer no limiar |
| V4 | (novo) as 15 detecções seriam achados reais | **Não**: 13 das 15 explicam-se pela cadeia de trampolins (ex.: `0x00346C6C` = 12L no símbolo, mas cadeia de 4 fragmentos = 76L/3C contra ELF 72 insns/3 calls). A unidade certa de comparação é a **CADEIA**, não o símbolo | Desenho final: cadeia + starts autoritativos → **TRUNCATED = 1 @0.5, 2 @0.9, 6 @1.0** e **CALL_MISMATCH = 8** (1 com défice ≥4). Baseline pequeno e congelável |
| V5 | catálogo de patches ~30 no-op | **Confirmado por artefacto, não por rc**: produção `recomp_macos_v2` tem **40** `ps3_call_opd`; `recomp_macos_v3` tem **52**. A produção está hoje a **12 conversões OPD de distância** do que os patches produzem numa cópia | O defeito 2 não é histórico — está vivo no lift de produção |
| V6 | `PS3_CELLSYS_REORDER` é botão morto | **Confirmado**: 0 ficheiros `.c/.cpp/.h/.m/.mm` nos dois repos; 62 scripts | Mantém-se |
| V7 | `apply_all_patches.sh` classifica em 3 estados | **4**: `FAILED`, `FAILED-PARTIAL`, `APPLIED`, `ALREADY-APPLIED` (linhas 277-292). E confirmei que **nunca invoca** `verify_lift_baseline.sh`/`MANIFEST` (grep = 0) | O plano acrescenta 2 estados, não 1 |
| V8 | o caminho absoluto do lift é rejeitado | **Confirmado**: `LIFT="$REPO/${LIFT_REL#./}"` (linha 68) | Mantém-se |
| V9 | (novo) o CI corre os gates | **Não corre nenhum**. `.github/workflows/ci.yml` + `CMakeLists.txt` só têm testes unitários (`test_lift_parity`, `test_audit_boundaries`, `test_ghidra_lookup`). Nenhum `.sh` do repo invoca `audit_boundaries.py`, e `--oracle` nunca é passado em lado nenhum | **Este é o achado central do plano**: não nos faltam sinais, falta-nos **invocação** |
| V10 | (novo) já existe um segundo mecanismo de oráculo | `tools/harness/ps3_recomp_harness.py:815` tem um estágio `crosscheck` tier-6 com `--oracle ghidra\|ida\|both` e métrica de *recall* | Não construir um terceiro. Ver corte C9 |

**Prova por mutação, feita hoje** (`scratchpad/mut.py`), com o desenho de cadeia:

```
baseline 0x0037C384 : []                       elf=(12 insns, 1 call)  lift=12L/1C
mutado (incidente)  : RED TRUNCATED (ELF 12 insns, cadeia 2 linhas)
                      YELLOW CALL_MISMATCH (ELF 1, cadeia 0)
mutado só-chamada   : YELLOW CALL_MISMATCH (ELF 1, cadeia 0)
```

O sinal existe e apanha o incidente. Ninguém o corre contra o lift real.

---

## 1. Triagem — o que foi CORTADO das recomendações dos 5 eixos

Critério: (a) pressupõe LLM a gerar código; (b) duplica coisa que já temos;
(c) não ataca defeito medido.

| # | Recomendação cortada | Origem | Motivo do corte |
|---|---|---|---|
| C1 | Loop reversor→verificador com LLM | loop | (a) `ppu_lifter.py` traduz 51 917 funções deterministicamente. Introduzir geração por modelo destrói o valor central declarado (reprodutível a partir do binário) |
| C2 | Checker LLM | loop, parity | (a)+(b) A pergunta dele é respondida melhor e de graça por `lift_parity.py` + `ppu_disasm.py`. Trocar determinismo por opinião no único sítio onde já temos determinismo |
| C3 | `knowledge-graph.json` persistente | kg-picker | (b)+(c) 87 linhas, 3 tipos de nó, um consumidor (prompt de LLM). Consulta equivalente nossa: `ghidra_lookup --callers` em **0,07 s**. Nenhum defeito medido é de latência |
| C4 | `ParityCache` | parity | (c) Código morto no repo deles (0 importadores). `lift_parity` inteiro corre em 6,6 s |
| C5 | `abs()` bilateral no CALL_MISMATCH | parity | (c) Medido: daria **14 954 YELLOW** (28,8 %) porque o lift emite legitimamente mais chamadas (`ps3_call_opd`, helpers). O nosso unilateral está certo |
| C6 | Sinais 3 (`trivial_stub`), 5 (`plugin_call_heavy`), 10 (`nan_logic`), 6 (`short_body`) | parity | (a)+(c) 3 e 5 assentam em `plugin::Call*` (não existe numa tradução determinística); 10 precisa de C decompilado no fluxo; 6 é o nosso THIN, **medido como estruturalmente impossível** (0/51 917) |
| C7 | `manual_checks` (`- [x] 0xADDR nota` → GREEN) | parity | (c) **Anti-padrão**. Força verde no endereço inteiro antes de qualquer sinal correr, mata sinais futuros, sem razão obrigatória. O nosso `OBSOLETO` do MANIFEST já é melhor |
| C8 | `REBackend` Protocol completo (9 métodos) + probe de capacidades por 24 subprocessos | ghidra-config | (a)+(c) `decompile`/`get_struct`/`search`/`unimplemented`/`remaining` existem para um LLM escrever C++ à mão. "Funções que faltam escrever" não é conceito quando 51 917 são emitidas. E o nosso oráculo é um **directório estático**, não um CLI vivo |
| C9 | Construir um novo cross-check com oráculo | ghidra-config | (b) `audit_boundaries.py --oracle` (I4/I5) **já existe e funciona** (medido hoje) e `ps3_recomp_harness.py` tier-6 já tem recall Ghidra/IDA. Falta invocação, não ferramenta |
| C10 | `config/loader.py` (YAML, 4 níveis, coerção por substring) | ghidra-config | (b) `tools/recomp_config.py` já faz isto e é **mais estrito** (rejeita com explicação em vez de avisar e ignorar) |
| C11 | Índice de marcadores por performance | kg-picker | (c) parcial. `gen_manifest --verify` custa 14,3 s; não é um defeito medido que doa. Fica como sub-item barato do item 6, não como projecto |
| C12 | `project_profile` para a divergência subtree/checkout | ghidra-config | (c) O profile modela "1 ferramenta, N projectos"; o nosso caso é "N cópias de 1 projecto a derivar". Substituído pelo item 5 (checksums) |

**Duplicações que NÃO devem gerar código novo**: `audit_boundaries` (oráculo),
`lift_parity` (sinais), `MANIFEST.tsv` (baseline com `OBSOLETO`/`GRUPO`),
`ghidra_lookup` (consulta), `recomp_config.py` (config validada),
`test_ghidra_lookup.py` (o `StubBackend` deles, que já temos sem nome).

---

## 2. Contradições entre eixos e resolução

| # | Contradição | Resolução |
|---|---|---|
| X1 | **Quatro nomes para o mesmo artefacto**: `POSTCOND.tsv` (loop), `PATCHES.tsv` (verification), `OWNERS.tsv` (kg-picker), "regras semânticas por EA" (parity) | **Um só ficheiro autorado**: `lift_baseline/CONTRACTS.tsv` (afirmações sobre o conteúdo do lift, com dono opcional). `OWNERS.tsv` passa a ser **derivado**, nunca escrito à mão |
| X2 | Medições em árvores diferentes: parity/kg mediram `recomp_macos_v3`, verification mediu `recomp_macos_v2` | Produção é **v2** (`build_macos.sh:37`, default do `apply_all_patches.sh`). Todo o baseline congela contra **v2**; v3 é candidato de relift. Confirmado que divergem: 40 vs 52 `ps3_call_opd` |
| X3 | "25 marcadores A-MENOS" (enunciado) vs "0 A-MENOS, 1 GRUPO em falta de 36" (kg) | O enunciado está desactualizado. O défice vivo é o **GRUPO opd-dispatch, −36** — e é a mesma família dos 12 que faltam à produção (V5) |
| X4 | "apertar o ratio custa zero" (parity) vs a minha medição V3/V4 | Vence a medição com starts autoritativos + cadeia. **Primeiro corrigir a autoridade e a unidade, depois calibrar o limiar** — pela ordem inversa afina-se ruído |
| X5 | In-degree do lift (kg, 3,8 s) vs do ELF | **Do ELF**. Se o lift perdeu a chamada, o alvo perde um chamador exactamente no caso a detectar |
| X6 | NO_BODY precisa de oráculo (parity) vs o oráculo é 13 006 descritores `.opd` (kg/ghidra) | Não é defeito, é a resposta: o conjunto OPD **é** a fonte independente. NO_BODY sobre o conjunto OPD, não sobre `functions.json` |
| X7 | Contagens de env: 199/22 (ghidra-config) vs 197/101/125/29 (loop) | Diferença metodológica (helpers como `movie_env_on` não são `getenv` literal). Não resolver por debate: o item 9 **produz a medição canónica** e falha o CI a partir dela |
| X8 | "o oráculo apanha a regressão" (ghidra) vs "o oráculo tem 5154 I5" | Ambos verdade. Por isso o oráculo **só** entra como gate **por delta contra baseline congelado**, nunca por limiar absoluto |
| X9 | `verify_lift_baseline.sh` é o gate (verification) vs `apply_all_patches` é o gate (prática) | São dois gates desligados. Item 1 junta-os num só ponto de entrada com um rc |

---

## 3. Plano ordenado — valor / esforço, ancorado em defeito medido

Nenhum item entra sem número medido.

| Ord | Item | Defeito nosso (medido) | Valor | Esforço | Transfere |
|-----|------|------------------------|-------|---------|-----------|
| **1** | **`verify_lift.sh`: gate único por DELTA sobre baselines congelados** (paridade com cadeia + starts autoritativos, MANIFEST, oráculo) | D1: duas perdas passaram todos os gates. Medido: **0 invocações** de `audit_boundaries` em `.sh`/CI; `--oracle` nunca usado; `apply_all_patches` nunca chama `verify_lift_baseline` (grep=0). E TRUNCATED tem zona cega em **4787/20 750** spans | alto | baixo | adaptado (o delta é nosso, do MANIFEST) |
| **2** | **`CONTRACTS.tsv` + estados `NO-MATCH`/`UNVERIFIED` no runner de patches** | D2: **82–84 de 87** patches em `ALREADY-APPLIED` num dia normal; **83/87** sem um único `sys.exit(1)`; `patch_14a01c_opd.py` é incapaz de falhar; produção tem **40** `ps3_call_opd` contra **52** na cópia patched | alto | médio | adaptado (`rules.py` deles, dados em vez de código) |
| **3** | **Oráculo com manifesto de proveniência + gate I5/I4 por delta** | D1: `0x0037C384` continua com fronteira errada em `functions.json` (**40 bytes**, 10 instruções); I5=**5154** (2422 seguros, **778 288 bytes**); `ghidra_out` **sem hash do ELF nem data**, enquanto o lift já é carimbado com `lifter-rev` | alto | baixo | adaptado (`BackendCapabilities` = probe, não declaração) |
| 4 | `--lift` absoluto + modo `--stage` (validar em cópia, promover por rsync) | D6: `LIFT="$REPO/${LIFT_REL#./}"` transforma `/tmp/L` em `/repo//tmp/L`, rc=2. A prática correcta é hoje feita **contra** a ferramenta | médio | baixo | directo (`copy_project`) |
| 5 | `sync-check` por sha256 entre `games/gow2/` e `../gow2-recomp` | **59 ficheiros divergentes**, incluindo ~20 `patch_*.py` e o `gen_manifest.py` do próprio baseline, com diferenças semânticas (um lado conhece 2 shapes de restauro do TOC) | médio | baixo | não-aplicável (é problema nosso) |
| 6 | In-degree do ELF + `lift_rank`: ordenar achados por raio de explosão | D5: escolha ad-hoc. `func_0037C384` tinha **35 chamadores**; `func_001856A8` tem **690** | médio | baixo | adaptado (melhor que o `function_picker` deles: eles não têm eixo de risco) |
| 7 | `OWNERS.tsv` **derivado** (marcador→patch) e `UNVERIFIABLE` para patches sem marcador | D3/D2: 246/246 marcadores resolvidos em 0,05 s; **6 patches sem nenhum marcador** — regressão invisível ao gate | médio | baixo | adaptado (`hook_address_index`) |
| 8 | `NO_BODY` sobre o conjunto OPD + `NO_EVIDENCE` contado | D1: NO_BODY é circular (0/20 750 starts em falta, por construção). Conjunto OPD independente disponível: **13 006** descritores | médio | médio | adaptado (`check_missing_source` lê artefacto diferente) |
| 9 | `ENV_REGISTRY.tsv` + linter no ctest | D4: **0 leitores / 62 scripts** para `PS3_CELLSYS_REORDER`, que está na receita canónica do `CLAUDE.md` e em 2 baselines congelados | médio | baixo | adaptado (só a ideia de medir, não o loader) |
| 10 | Sinal FP/VMX (ELF faz matemática, lift não emite token float) | Buraco **não medido por nenhuma ferramenta nossa**; `fmadd`/VMX BE estão listados no `CLAUDE.md` como lacunas face ao upstream. **Medir a taxa antes de commitar** | médio | médio | adaptado (o único dos 11 que cobre buraco real) |
| 11 | `LEDGER.json` por artefacto com `attempt_count` e tecto | D3/D5: `grep -c attempt_count` no nosso ledger = **0**. Um patch obsoleto pode ser re-tentado por sessões sucessivas | baixo | médio | adaptado |

Itens 10 e 11 só entram depois de 1–3 estarem verdes; o 10 exige medição prévia
da taxa de disparo (regra nova: **nenhum sinal entra sem taxa medida no corpus inteiro**).

---

## 4. Desenho concreto dos três primeiros

### Item 1 — `verify_lift.sh`: um gate, três verificadores, delta contra baseline

**Porquê primeiro**: as duas perdas do enunciado seriam apanhadas por ferramentas
que **já existiam** (prova por mutação na secção 0). O que falhou foi a invocação
e a inexistência de um critério de falha utilizável num corpus de 51 917 funções.

#### Ficheiros

| Ficheiro | Acção |
|---|---|
| `ps3recomp/tools/lift_parity.py` | alterar |
| `ps3recomp/games/gow2/verify_lift.sh` | **criar** |
| `ps3recomp/games/gow2/lift_baseline/PARITY_BASELINE.json` | **criar** (congelado) |
| `ps3recomp/games/gow2/lift_baseline/SUPPRESS.tsv` | **criar** (vazio) |
| `ps3recomp/tools/test_lift_parity.py` | estender |
| `ps3recomp/CMakeLists.txt` | +1 teste |

#### Alterações em `lift_parity.py`

1. **Autoridade dos starts** — `elf_spans(elf, starts, cut_at=None)`: o conjunto que
   corta o span passa a ser `cut_at` (os starts de `functions.json`), não a união com
   os símbolos do lift. Novo flag `--authority functions.json`. Motivo medido: 31 167
   fragmentos só-do-lift encurtam 4787 spans reais.
2. **Unidade = cadeia** — `chain_metrics(ea, lift) -> (lines, calls, n_frags)`,
   seguindo `tail_to` com guarda de ciclo. `classify()` compara ELF contra a **cadeia**.
   Motivo medido: 13 dos 15 TRUNCATED do símbolo são fragmentação legítima.
3. **THIN sai** (0/51 917, estruturalmente impossível: piso de 1 linha por instrução).
   Entra `INFO NO_EVIDENCE` quando `elf_spans` não produziu span para um EA —
   *falta de evidência nunca é silêncio verde* (regra 4 do `CLAUDE.md`).
4. **`callers_elf`** por finding, contado no laço de desassemblagem que já existe
   (`CALL_MNEMONICS`, linha 63) — custo marginal ~0.
5. **`--baseline FICH.json`** e **`--suppress SUPPRESS.tsv`**.
6. **CALL_MISMATCH continua unilateral**, com o número medido no docstring
   (o `abs()` deles daria 14 954 YELLOW).

#### Interface

```
lift_parity.py ELF LIFT_DIR --authority functions.json \
               --baseline lift_baseline/PARITY_BASELINE.json \
               --suppress lift_baseline/SUPPRESS.tsv [--json out.json] [--freeze]

rc=0  nenhum par (ea,sinal) novo e nenhuma contagem (nivel,sinal) subiu
rc=1  achado novo, contagem subiu, ou SUPRESSAO ORFA
rc=2  erro de uso / lift ou ELF ausente
```

```
verify_lift.sh [LIFT_DIR]     # default recomp_macos_v2 (produção)
  1) lift_parity  --baseline PARITY_BASELINE.json
  2) gen_manifest --verify MANIFEST.tsv          (já existe; hoje ninguém o encadeia)
  3) audit_boundaries --oracle --baseline ORACLE_BASELINE.tsv   (item 3)
  cabeçalho: caminho ABSOLUTO do lift + sha256 do conjunto ppu_recomp_*.cpp
  rodapé: tabela PASS/FAIL/UNVERIFIED por verificador; rc = OR das falhas
```

O carimbo do sha256 no cabeçalho é a peça anti-fraude: o veredicto passa a pertencer
a um artefacto identificado e deixa de poder ser citado sobre outro lift.

#### Baseline inicial (medido hoje contra `recomp_macos_v2`, cadeia + autoridade)

| Sinal | @0.5 | @0.9 | @1.0 |
|---|---|---|---|
| TRUNCATED | 1 | 2 | 6 |
| CALL_MISMATCH (unilateral) | 8 (1 com défice ≥4) | — | — |

Recomendação de limiar: **`TRUNCATE_RATIO = 0.9`**, baseline de 2 TRUNCATED
(`0x004CBD28` 959→147; `0x002B0E78` 57→45) + 8 CALL_MISMATCH, com o percentil e a
data registados no docstring, como já se fez para o TAIL_JUMP.

#### Testes (e a mutação que os faz falhar)

| Teste | Fixture | Mutação que DEVE fazer falhar |
|---|---|---|
| `test_truncated_apanha_perda_de_corpo` | ELF sintético: função de 12 insns com 1 `bl`; lift com corpo completo → 0 achados | cortar o corpo para 2 linhas e remover a chamada → **RED TRUNCATED + CALL_MISMATCH** (reproduz `func_0037C384`; provado hoje em memória) |
| `test_cadeia_de_fragmentos_nao_e_falso_positivo` | função de 72 insns partida em 4 fragmentos ligados por `g_trampoline_fn` → 0 achados | pôr `tail_to=None` no 1.º fragmento (cadeia quebrada) → **RED TRUNCATED** |
| `test_autoridade_dos_starts` | fragmento do lift no meio de uma função de `functions.json` | usar a união como corte → o span encolhe e o achado **desaparece**: o teste falha se a regressão para o comportamento antigo for reintroduzida |
| `test_baseline_delta` | baseline com os N achados actuais → rc=0 | acrescentar 1 par (ea,sinal) → rc=1; subir a contagem de um (nível,sinal) → rc=1 |
| `test_supressao_orfa_falha` | `SUPPRESS.tsv` com (ea,sinal) que já não dispara | rc=1 com `SUPRESSAO ORFA` — supressão apodrecida tem de doer |
| `test_no_evidence_conta` | EA sem span no ELF | achado `INFO NO_EVIDENCE` contado no sumário; falha se voltar a devolver lista vazia |

> **Nota de honestidade sobre CI**: o `EBOOT.ELF` e o lift são dados de jogo e não estão
> versionados. O CI só pode correr os testes com fixtures sintéticas. O `verify_lift.sh`
> corre **localmente em `gow2-recomp`**, e o item 2 torna-o obrigatório ao fim de cada
> leva de patches. Fingir que isto entra no GitHub Actions seria exactamente o tipo de
> plano que o brief proíbe.

---

### Item 2 — `CONTRACTS.tsv` e o fim do `ALREADY-APPLIED` como verde por omissão

**Porquê**: a lição real do repo A não é o tri-estado — é a **polaridade** e o **local**
do veredicto. Lá, "não consegui verificar" é um valor de primeira classe (`UNKNOWN`) que
**bloqueia** por omissão (`require_verified=True`), e o veredicto é calculado por quem
consome, a partir de evidência externa ao produtor. Nós temos o inverso: o veredicto é
auto-relato do patch e a omissão é verde.

#### Ficheiros

| Ficheiro | Acção |
|---|---|
| `games/gow2/lift_baseline/CONTRACTS.tsv` | **criar** |
| `games/gow2/lift_baseline/check_contracts.py` | **criar** (~120 linhas) |
| `games/gow2/lift_baseline/PATCHES_KNOWN_FAILING.tsv` | **criar** |
| `games/gow2/apply_all_patches.sh` | alterar (classificação + encadear `verify_lift.sh`); **apagar** o heredoc com os 13 `report()` |
| `tools/test_check_contracts.py` | **criar** |

#### Formato de `CONTRACTS.tsv`

```
id          patch                 escopo  alvo           predicado  agulha                      valor  razao                              data
opd-14a01c  patch_14a01c_opd.py   ea      0x0014A01C     count_ge   ps3_call_opd(               2      OPD do SHADERSRC; regressao 2026-07  2026-07-26
opd-14a01c-n patch_14a01c_opd.py  ea      0x0014A01C     count_eq   ps3_indirect_call(          0      nao pode sobrar indirecto           2026-07-26
mc-32854c   patch_mc_schedule...  ea      0x0032854C     all_of     MC-32854C                   1      tag de probe                        2026-07-26
opd-total   -                     global  -              count_ge   ps3_call_opd(               52     total pos-catalogo (v3 mediu 52)    2026-07-26
```

- `escopo=ea`: região = de `void func_XXXXXXXX(` até ao próximo `^void func_` — o mesmo
  delimitador que os patches já usam.
- `agulha` aceita prefixo `re:` para regex (ideia directa do `rules.py:70-73`).
- `escopo=global`: contagem em todos os `ppu_recomp_*.cpp`, mesma primitiva do
  `gen_manifest.py::verify`.
- **Regra dura**: para marcadores TAG procurar **sem** parênteses rectos (medido: com
  match literal aparecem 11 órfãos falsos, porque os patches guardam `"ICG-VCALL"` e
  o lift emite `[ICG-VCALL]`).

#### Nova classificação no runner

| Estado | Condição | Cor |
|---|---|---|
| `APPLIED` | rc=0 e o conteúdo mudou | verde |
| `VERIFIED-ALREADY` | rc=0, conteúdo inalterado, **e os contratos do patch passam antes e depois** | verde |
| `NO-MATCH` | rc=0, conteúdo inalterado, **e os contratos do patch falham** | **vermelho** (é o `patch_14a01c_opd` de hoje) |
| `UNVERIFIED` | rc=0, conteúdo inalterado, **e o patch não declara nenhum contrato** | **amarelo contado, nunca somado aos verdes** |
| `FAILED` / `FAILED-PARTIAL` | rc≠0 (mantêm-se) | vermelho |

Duas mudanças de comportamento adicionais:
- **imprimir sempre o stdout** nos estados `NO-MATCH` e `UNVERIFIED` (hoje é suprimido
  em `:295-298` — e 69 dos 84 verdes imprimem uma frase de "already" que ninguém lê);
- **encadear `verify_lift.sh`** no fim (hoje grep confirma que nunca acontece).

#### Allowlist, para o rc voltar a significar algo

O cabeçalho do próprio script admite (`:24-27`) que o rc está **permanentemente em 1**.
Introduzir `UNVERIFIED` dentro de um gate cronicamente vermelho é nascer morto.
`PATCHES_KNOWN_FAILING.tsv` com `patch | motivo | data_limite`: rc=0 se todos os FAILED
estiverem lá e dentro da validade; rc=1 no primeiro FAILED novo ou entrada expirada.
`UNVERIFIED` migra pelo mesmo mecanismo (aviso contado + data-limite), o que força a
encher a `CONTRACTS.tsv` por levas em vez de deixar o gate vermelho para sempre.

#### Ordem de povoamento (pelo custo/benefício já medido)

1. os ~12 patches da família OPD (a classe do incidente das 36 conversões, e a produção
   está hoje a 12 conversões de distância — V5);
2. os 9 que imprimem contagem sem a gatear;
3. os 13 `report()` embebidos, migrados 1:1 e depois apagados do shell;
4. os 6 patches sem marcador nenhum → nascem `UNVERIFIABLE`, nunca verdes.

#### Testes

| Teste | Fixture | Mutação que DEVE fazer falhar |
|---|---|---|
| `test_contrato_ea_passa` | lift sintético com `ps3_call_opd(` 2× na região de `func_0014A01C` | trocar por `ps3_indirect_call(` → **FAIL**, não `UNVERIFIED` |
| `test_ancora_ausente_nao_e_unverified` | remover `void func_0014A01C(` do lift | **FAIL** com `ancora ausente`, mensagem exacta — não "fixed 0 / OK" |
| `test_patch_no_op_da_NO_MATCH` | patch falso que lê e reescreve idêntico, com contrato declarado ausente | runner devolve `NO-MATCH` e rc≠0 (hoje devolve `ALREADY-APPLIED` e rc=0) |
| `test_patch_sem_contrato_e_UNVERIFIED` | patch sem linha na tabela | contado à parte; **falha se for somado aos verdes** |
| `test_tag_sem_parenteses_rectos` | contrato `all_of ICG-VCALL` contra lift que emite `[ICG-VCALL]` | passa; muda para match literal com `[]` → falha (protege contra os 11 órfãos falsos) |
| `test_allowlist_expira` | entrada com data-limite passada | rc=1 |

---

### Item 3 — Oráculo com proveniência e gate I5/I4 por delta

**Porquê**: `audit_boundaries --oracle` funciona hoje e nomeia `0x0037C384` com os
40 bytes exactos. Mas tem 5154 achados I5 — inutilizável como limiar absoluto — e o
`ghidra_out` não tem hash do ELF nem data, por isso um export obsoleto passaria por
verdade. A ideia transferida é o `BackendCapabilities`: **medir** o que o oráculo é,
e transformar ausência/obsolescência em **facto reportado**, não em silêncio.

#### Ficheiros

| Ficheiro | Acção |
|---|---|
| `tools/oracle_manifest.py` | **criar** (~50 linhas) |
| `tools/ghidra_analyze.py` | alterar (escrever `ghidra_out/manifest.json` no fim) |
| `tools/audit_boundaries.py` | alterar (`--oracle-manifest`, `--baseline`, linha `ORACLE:`) |
| `games/gow2/lift_baseline/ORACLE_BASELINE.tsv` | **criar** (congelado) |
| `tools/test_oracle_manifest.py` | **criar** |

#### Interface

```json
// ghidra_out/manifest.json
{"elf_sha256":"…","elf_path":"EBOOT.ELF","ghidra_version":"11.x",
 "exported_at":"2026-07-25T15:26:00Z",
 "counts":{"functions":13143,"decompiled":13096,"symbols":114065,"strings":1962,"names":266},
 "opd_descriptors":13006}
```

```
audit_boundaries.py functions.json --elf EBOOT.ELF \
    --oracle ghidra_out/functions.json --oracle-manifest ghidra_out/manifest.json \
    --baseline lift_baseline/ORACLE_BASELINE.tsv

ORACLE: ok (13143 fn, 13006 opd, exportado 2026-07-25)   -> continua
ORACLE: absent                                            -> rc=1 (nunca pass silencioso)
ORACLE: stale (sha do ELF difere)                         -> rc=2
```

**Baseline (medido hoje)**: congelar o conjunto de endereços I5 do tipo
`extende-seguro` (**2422**, **778 288 bytes**) e os I4 (**137**). Falha se aparecer um
endereço **novo** em `extende-seguro` ou se os bytes em falta **aumentarem**.
`extende-funde` fica fora nesta fase (a lacuna já é reclamada por outra função nossa e
a discordância pode ser legítima — `0x00379C70` com 6912 bytes é desse tipo).
Relatório ordenado por bytes perdidos — dá de graça a fila de trabalho por impacto.

#### Testes

| Teste | Fixture | Mutação que DEVE fazer falhar |
|---|---|---|
| `test_oraculo_stale_falha` | manifesto com `elf_sha256` diferente do ELF | rc=2; **falha se o gate passar em silêncio** |
| `test_oraculo_ausente_falha` | sem `manifest.json` | rc=1 com `ORACLE: absent` |
| `test_baseline_i5_delta` | `functions.json` sintético igual ao baseline → rc=0 | encurtar uma função nossa em 8 bytes → novo `extende-seguro` → rc=1 (reproduz a classe `0x0037C384`) |
| `test_extende_funde_nao_gateia` | criar um `extende-funde` novo | rc=0 nesta fase, mas **contado e impresso** |

---

## 5. O que NÃO devemos adoptar (e porquê)

| Não adoptar | Origem | Motivo |
|---|---|---|
| **Reverser LLM** a gerar corpos de função | `agents/reverser.py` | Destrói o valor central: reprodutível a partir do binário, não de edições no código gerado. 51 917 funções são traduzidas deterministicamente |
| **Checker LLM** | `agents/checker.py` | A pergunta dele já é respondida melhor por `lift_parity` + `ppu_disasm`. Trocar determinismo por opinião |
| **`knowledge-graph.json`** | `core/knowledge_graph.py` | Existe para comprimir contexto num prompt (`neighborhood` truncado a 6000 chars, 1 consumidor). Consulta equivalente nossa: 0,07 s. E o deles nunca apaga, não tem timestamp, `schema_version` é escrito e nunca lido, corrupção vira grafo vazio em silêncio |
| **`ParityCache`** | `parity/cache.py` | Código morto lá (0 importadores). Cache que não existe é pior que nenhuma. 6,6 s não justificam |
| **`abs()` bilateral** no mismatch de chamadas | `signals.py:122` | Medido: **14 954 YELLOW** (28,8 %) no nosso lift |
| **`manual_checks`** (GREEN por endereço) | `parity/rules.py:12` | Martelo: força verde antes de qualquer sinal, mata os 11 e todos os futuros, sem razão nem revalidação. O nosso `OBSOLETO` já é superior. Supressão tem de ser por **(ea, sinal)** com razão e falhar se ficar órfã |
| **Overrides que só actuam no comando standalone** | `single.py:131` vs `engine.py:210` | Lá, o gate que bloqueia não lê os overrides. Regra nossa: **um só caminho de avaliação** — CI e relatório interactivo chamam a mesma função com a mesma configuração |
| **Sinais `trivial_stub`, `plugin_call_heavy`, `nan_logic`, `short_body`** | `signals.py:27,53,134,73` | Os dois primeiros assentam em `plugin::Call*` (inexistente numa tradução determinística); `nan_logic` precisa de C decompilado no fluxo; `short_body` = o nosso THIN, medido como impossível (0/51 917) |
| **`REBackend` completo + probe por 24 subprocessos** | `backend/protocol.py`, `ghidra_bridge.py:127` | Metade da superfície serve um LLM a escrever C++. E o nosso oráculo é um directório estático: um `manifest.json` substitui 24 spawns |
| **Parsing heurístico de texto livre do bridge** | `ghidra_bridge.py:157,193` | Mudança de formato vira lista vazia em vez de erro — a mesma classe de silêncio que nos passou o `func_0037C384`. Ler JSON exportado, e falhar alto |
| **`config/loader.py` (YAML 4 níveis)** | `config/loader.py` | Já temos `recomp_config.py`, mais estrito. E o deles engole `selection_strategy` com typo em silêncio |
| **Substituir env vars por YAML de uma vez** | ghidra-config | O valor está na **verificabilidade**, não no formato. O linter (item 9) resolve; a migração, se acontecer, faz `setenv()` para não partir os ~117 scripts |
| **Tolerâncias difusas** (`call_count_tolerance=3`) | `objective.py` | A nossa pós-condição é exacta e contável ("7 `ps3_call_opd` em `func_0032E200`, 0 indirectos"). Adoptar tolerância onde temos igualdade seria regressão de rigor |
| **Um terceiro mecanismo de oráculo** | ghidra-config | `audit_boundaries --oracle` + `ps3_recomp_harness` tier-6 já existem. Falta invocação |
| **Não gravar o veredicto determinista** | `core/session.py:36-48` | Defeito deles: o único sinal não-LLM do loop não sobrevive à sessão. O nosso `LEDGER` (item 11) grava-o desde o início |

---

## 6. Se só se fizer UMA coisa

**Item 1 — `verify_lift.sh`: um gate único, por delta, sobre baselines congelados,
com o `lift_parity` corrigido para cadeia + starts autoritativos.**

Três razões, todas medidas:

1. **As duas perdas seriam apanhadas por ferramentas que já tínhamos.** Provei-o por
   mutação: com o desenho de cadeia, reproduzir o incidente `func_0037C384` (2 linhas,
   sem a `bl`) dá `RED TRUNCATED` + `YELLOW CALL_MISMATCH`. E `audit_boundaries --oracle`,
   corrido hoje, nomeia `0x0037C384` com os 40 bytes exactos. O que falhou não foi a
   detecção — foi que **nada corre**: 0 invocações de `audit_boundaries` em scripts,
   `--oracle` nunca passado, `apply_all_patches` nunca chama `verify_lift_baseline`.
2. **Sem o mecanismo de delta, nada mais funciona.** Um corpus de 51 917 funções e um
   oráculo com 5154 divergências nunca dão zero. Um gate absoluto ou fica sempre vermelho
   (e é ignorado — o `apply_all_patches.sh` já admite isso no cabeçalho) ou é afrouxado
   até não servir (foi assim que o `TRUNCATE_RATIO` chegou a 0.5). O delta contra
   baseline congelado é a peça que **o repo A não tem** — `score()` é stateless e não há
   ficheiro de baseline em lado nenhum — e que o nosso `MANIFEST.tsv` já provou.
3. **É o pré-requisito do item 2.** Os contratos por patch precisam exactamente do mesmo
   mecanismo (afirmação sobre o artefacto + comparação com um estado congelado) e do
   mesmo ponto de entrada carimbado com o sha256 do lift avaliado.

Esforço: baixo. Duas alterações cirúrgicas no `lift_parity.py` (autoridade dos starts e
agregação por cadeia), um wrapper de shell, dois ficheiros de baseline que se geram com
os números já medidos hoje, e seis testes com fixtures sintéticas que entram no ctest ao
lado dos 22 existentes.
