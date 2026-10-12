---
phase: 02-c-digo-host-rf-o-versionado-e-compilado
verified: 2026-07-26T18:36:11Z
status: passed
score: 4/4 critérios do ROADMAP verificados (o gap fechou em 2026-07-29 — ver adenda)
behavior_unverified: 0
overrides_applied: 0
gaps:
  - truth: "games/gow2/build_macos.sh compila as fontes extraídas e LIGA host_gow2_factory.cpp (e, por extensão declarada no 02-03-PLAN.md, host_gow2_f2b.c) ao build — critério 3 do ROADMAP, verificável por nm/log de compilação, não por inspeção visual."
    status: failed
    reason: >
      "Liga" foi interpretado com o rigor que o próprio CLAUDE.md do projeto exige
      ("provado in-boot vs offline/unit vs não-exercitado — não escrever 'funciona' sem
      observação"). O mecanismo (guard anti-duplicate-symbol, compilação condicional)
      está correto e bem testado — mas medido agora, de forma independente, contra os
      TRÊS lifts reais existentes nesta máquina (recomp_macos_v2 produção,
      recomp_macos_v2.pre_v4, recomp_macos_v3), o guard dá SKIP para os DOIS ficheiros em
      TODOS os três. Ou seja: hoje, nem host_gow2_factory.o nem host_gow2_f2b.o entram em
      nenhum boot_gow2 real que este build_macos.sh (do monorepo) produziria. A única
      prova de "liga" apresentada nos SUMMARYs de 02-02/02-03 é um harness sintético
      (main.cpp + stubs.c + os .o reais fora do pipeline de build real) — reproduzi essa
      mesma prova de forma independente nesta verificação e ela SUSTÉM-SE (ver secção
      "Prova reproduzida"), mas prova o MECANISMO, não o facto "liga ao build real hoje".
      O 02-03-SUMMARY.md afirma "Critério 3 do ROADMAP fica satisfeito para os DOIS
      ficheiros — não só 'preparado' para o F2B", o que sobrevende face ao estado medido:
      a mesma limitação (nunca observado a entrar num boot_gow2 real desta fase) aplica-se
      IGUALMENTE a host_gow2_factory.cpp, não só ao F2B como a pergunta de verificação
      assumia.
    artifacts:
      - path: "games/gow2/build_macos.sh"
        issue: "Guard correto e testado nos dois sentidos (fixtures), mas o ramo COMPILE nunca disparou contra nenhum lift real desta máquina — a entrada no binário real nunca foi observada por este artefacto."
    missing:
      - "Uma corrida real de games/gow2/build_macos.sh contra um lift genuinamente limpo (sem as definições F2B/factory embutidas) — Fase 3 é o lugar natural — seguida de nm sobre o boot_gow2 PRODUZIDO (não sobre um harness sintético), confirmando que host_gow2_factory.o e host_gow2_f2b.o entram de facto no binário."
      - "Correção da linguagem de 02-03-SUMMARY.md/REQUIREMENTS.md de 'satisfeito... não só preparado' para 'mecanismo correto e testado; ligação real ainda não observada para nenhum dos dois ficheiros' — ou um override explícito do utilizador aceitando a leitura mais permissiva do critério."
deferred: []
---

# Phase 2: Código host órfão versionado e compilado — Verification Report

**Phase Goal:** O código host escrito à mão que hoje só existe dentro dos `.cpp`
gitignored passa a ter casa versionada e a ser compilado como fonte de primeira classe,
para que um lift novo e limpo consiga linkar.

**Verified:** 2026-07-26T18:36:11Z
**Status:** gaps_found
**Re-verification:** Não — verificação inicial.

## Resposta directa à pergunta céptica (i) vs (ii)

**A resposta é (ii), e o achado é mais amplo do que a pergunta assumia.**

Não é só o F2B que "fica preparado e não ligado". Medido agora, de forma independente
(reproduzi os comandos, não confiei nos SUMMARYs), contra os **três** lifts reais
existentes nesta máquina:

| Lift | Guard `host_gow2_factory` | Guard `host_gow2_f2b` |
|---|---|---|
| `../gow2-recomp/recomp_macos_v2` (produção actual) | **SKIP** | **SKIP** |
| `../gow2-recomp/recomp_macos_v2.pre_v4` (preservado, 20 jul) | **SKIP** | **SKIP** |
| `../gow2-recomp/recomp_macos_v3` (candidato a re-lift) | **SKIP** | **SKIP** |

Ou seja: **hoje, contra qualquer lift real disponível nesta máquina, nem
`host_gow2_factory.o` nem `host_gow2_f2b.o` entram em nenhum `boot_gow2` real que
`games/gow2/build_macos.sh` (do monorepo) produza.** O guard está correto — reduz o
risco de "duplicate symbol"/"estado duplicado em silêncio" e tem dentes reais nos três
ramos (testei 6 fixtures sintéticas independentemente, ver abaixo) — mas o guard nunca
disparou o ramo COMPILE contra nada real nesta fase.

A prova de "liga" apresentada em `02-02-SUMMARY.md`/`02-03-SUMMARY.md` é **100%
sintética**: um harness (`main.cpp` + `stubs.c` + os dois `.o` reais), fora do pipeline
real de `build_macos.sh`. Reproduzi essa mesma prova de forma independente nesta
verificação (ver "Prova reproduzida" abaixo) e ela sustém-se — mas prova o **mecanismo**,
não o facto "liga ao build real hoje", que é o que o critério 3 do ROADMAP pede
("verificável por nm/log de compilação, não por inspeção visual").

Existe, sim, uma prova histórica REAL e anterior a esta fase — no checkout **irmão**
(`gow2-recomp`), commit `57b418f` (2026-07-26 03:45:38, a própria madrugada desta sessão),
onde a mesma lógica de guard (adicionada 2h antes, commit `f9ec60f`, 01:29:59) disparou de
facto o ramo COMPILE contra um lift genuinamente regenerado, produziu um `boot_gow2` real,
e esse binário foi promovido a produção com `st620=11` em 6/6. Essa prova é real e não
forjada — mas (a) é de OUTRO script (`../gow2-recomp/build_macos.sh`, não o artefacto
desta fase), (b) predata a existência de `host_gow2_f2b.c` (não cobre o F2B de forma
nenhuma), e (c) o estado "limpo" não persistiu — a nova `recomp_macos_v2` de produção
volta a ter as definições embutidas (confirmado: guard dá SKIP contra ela agora), o que
sugere que algum patch de `apply_all_patches.sh` reinsere o código hand-written depois da
promoção — um facto não investigado por esta fase e relevante para a Fase 3/4.

**Classificação:** o critério 3, no sentido estrito "liga = entra no binário produzido
hoje, verificado por nm sobre esse binário real", **NÃO está satisfeito para nenhum dos
dois ficheiros** — não é uma leitura generosa aceitável dizer que está. O mecanismo
(a parte que É demonstrável e foi demonstrada) está correto. Marco isto como
**gap declarado**, seguindo a instrução explícita de preferir um gap declarado a um
critério dado como cumprido por interpretação generosa.

`02-03-SUMMARY.md` e `REQUIREMENTS.md` (`REQ-RDY0-2`) são parcialmente honestos: ambos
declaram explicitamente a ressalva "a integração em-boot do F2B... ainda não foi
observada... é trabalho da Fase 3" — o que É correcto e não escondido. Mas a mesma
ressalva deveria aplicar-se ao `host_gow2_factory.cpp` dentro do âmbito EXACTO desta
fase (o `build_macos.sh` do monorepo), e a frase "satisfeito para os DOIS ficheiros —
não só 'preparado' para o F2B" convida à leitura mais generosa que a pergunta de
verificação pediu para não aceitar.

## Prova reproduzida (medido nesta verificação, não inferido dos SUMMARYs)

### Critério 1 — gate do preâmbulo (VERIFIED)

```
grep -c PREAMBLE games/gow2/lift_baseline/MANIFEST.tsv          -> 2
gen_manifest.py ... recomp_macos_v2.pre_v4 --verify MANIFEST.tsv -> 246/246, rc=0
gen_manifest.py ... recomp_macos_v2 (produção) --verify         -> 213/246, rc=1
  (os 33 marcadores TAG que faltam são deriva pré-existente, NENHUM dos 2 PREAMBLE
   falha em nenhum dos dois lifts — bate exactamente com os números que o utilizador
   já tinha medido: 246/246 e 213/246)
.venv/bin/python3 games/gow2/lift_baseline/test_gen_manifest.py -> rc=0, 5x [PASS]
  incluindo as 2 mutações (stripar g_trampoline_fn/ps3_indirect_call -> rc=1,
  mensagem "AUSENTE  PREAMBLE <simbolo>" confirmada)
```

O cabeçalho de `MANIFEST.tsv` reconcilia as três cifras (1603/1662/1793) no próprio
artefacto — confirmado por leitura directa, não por citação do plano.

### Critério 2 — F2B com ficheiro versionado próprio (VERIFIED)

```
clang -std=c11 -w -I runtime/ppu -I include -c games/gow2/host_gow2_f2b.c -o /tmp/x.o
  -> rc=0
nm /tmp/x.o | grep " T "   -> exactamente 7: f2b_fo_mfd_put/get, f2b_fo_sz_get,
                              f2b_stream_fill, f2b_stream_ensure,
                              f2b_stream_eof_try_complete, f2b_stream_pre_consume
nm /tmp/x.o | grep " S "   -> exactamente 6 globais externos (g_f2b_fill_*,
                              g_f2b_natural_movie_fo)
nm /tmp/x.o | grep " b "   -> 4 locais (tabela mfd: g_f2b_fo_mfd_fo/fd/sz/n)
```

**Fidelidade da extracção — verificada por diff, não por confiança no SUMMARY:**
normalizei o bloco original (`ppu_recomp_001.cpp:496-713` do `recomp_macos_v2` real de
produção) aplicando programaticamente as 6 transformações mecânicas declaradas (omitir
`g_wadld_eof_ea`/`vm_base`; remover `static` de 5 funções + 6 globais; remover
`extern "C" ` de 2 funções) e comparei contra o corpo de `host_gow2_f2b.c`:

```
diff /tmp/normalized_orig_f2b.txt /tmp/extracted_body.txt   -> vazio, rc=0 (216 linhas)
```

Nenhuma linha de lógica diverge — a extracção é fiel byte a byte, exactamente como
D-2.4 exige (extracção por símbolo, `g_wadld_eof_ea` correctamente excluído apesar de
estar fisicamente dentro do span).

`.venv/bin/python3 games/gow2/recomp_mid_v2/test_patch_zz_host_api_decls.py` → `rc=0`,
6x `[PASS]` (contagem `DECLS=26`/`GLOBAL_DECLS=6`, aplicação, no-op quando já declarado,
idempotência, compilação real, link E2E). Reproduzido.

### Critério 3 — build_macos.sh compila e liga (GAP — ver acima)

Guard testado independentemente com 6 fixtures sintéticas (não as do SUMMARY — escritas
do zero nesta verificação):

| Caso | Fixture | Resultado |
|---|---|---|
| 1. extern puro (âncora) | `extern "C" void f2b_stream_ensure(...) {` | SKIP ✓ |
| 2. static puro (achado crítico) | `static void f2b_fo_mfd_put(...) {` + global static | SKIP ✓ |
| 3. só protótipo | `extern "C" void f2b_stream_ensure(...);` | COMPILE ✓ |
| 4. nada de F2B | símbolo alheio | COMPILE ✓ |
| 5. só 1 das 5 funções static | `static int f2b_stream_fill(...) {` | SKIP ✓ |
| 6. só 1 dos 10 globais static | `static uint32_t g_f2b_fill_fo;` | SKIP ✓ |

Os três ramos do guard têm dentes reais — nenhum ramo morto.

Compilação standalone + `nm` confirmados de novo (`13 T` factory, `7 T` f2b).

**Link E2E sintético reproduzido independentemente** (harness próprio, não o do SUMMARY):

```
clang++ main.o host_gow2_factory.o host_gow2_f2b.o stubs.o -o e2e_bin
  -> LINK rc=0
./e2e_bin -> RUN rc=0
nm e2e_bin | grep -E "ps3_factory_repair_vt|f2b_stream_ensure"
  -> T _ps3_factory_repair_vt
  -> T _f2b_stream_ensure
```

Confirma o mecanismo — mas **não é** nm sobre um `boot_gow2` produzido pelo pipeline
real de `build_macos.sh` contra um lift real, que é o que falta.

`bash -n games/gow2/build_macos.sh` → `syntax rc=0`.

### Critério 4 — citado como evidência, não re-provado (VERIFIED como escopo declarado)

D-2.6 explicitamente descope re-provar isto nesta fase. `grep -c
"counters_pre.tsv\|57b418f" games/gow2/build_macos.sh` → `2`. Aceito como satisfeito
NO SENTIDO ESTRITO em que a fase o definiu (citação, não repetição de medição) — mas
com a mesma ressalva já documentada no `REQUIREMENTS.md`: essa medição é do
`host_gow2_factory.cpp` num checkout diferente, antes do F2B existir.

### Débito D-2.5 e teste-ouro da Fase 1 (VERIFIED, sem regressão)

```
grep -c "D-2.5" runtime/ppu/tests/test_ppu_lifter_emit.py  -> 2
.venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py -> rc=0, todos os PASS da
  Fase 1 preservados; sub-gate de sincronização mostra [AVISO] (3 hunks, WARN-only,
  como D-2.5 determina — nunca falha).
```

### Baseline geral do projecto

```
ctest --test-dir build-tests --output-on-failure -> 100% tests passed out of 21
  (19 passed, 2 skipped esperados: py.lift_selftest, py.spu_lift_selftest)
git status --porcelain -> limpo fora dos untracked pré-existentes de outras sessões
  (build-tests/, runtime/spu/tests/out_spu_channel_blocking/); fixtures SPU regeneradas
  pelo ctest restauradas com git checkout --.
```

## Goal Achievement

### Observable Truths

| # | Truth (critério ROADMAP) | Status | Evidência |
|---|---|---|---|
| 1 | Inventário órfão versionado; 3 cifras reconciliadas; gate cobre o preamble | ✓ VERIFIED | `MANIFEST.tsv` com 2 marcadores PREAMBLE + cabeçalho reconciliado; `--verify` 246/246 (pre_v4) e 213/246 (produção, deriva pré-existente não relacionada); mutação com dentes confirmada |
| 2 | F2B com ficheiro versionado próprio | ✓ VERIFIED | `host_gow2_f2b.c` compila, `nm` exacto (7T+6S+4b), extracção byte-a-byte fiel (diff vazio contra o lift real, normalizado) |
| 3 | `build_macos.sh` compila e **liga** as fontes extraídas ao build, verificável por nm/log de compilação | ✗ FAILED (mecanismo VERIFIED; "liga hoje" FAILED) | Guard correto e testado (6 fixtures, 3 ramos com dentes); mas guard dá SKIP contra os 3 lifts reais disponíveis, para os 2 ficheiros — nenhum `.o` entra em nenhum `boot_gow2` real hoje. Única prova de "liga" é sintética. |
| 4 | Lift novo sem patches + fontes versionadas → linka | ✓ VERIFIED (como citação, D-2.6) | Citado com `counters_pre.tsv`/commit `57b418f`; escopo da fase explicitamente não exige re-prova; ressalva sobre não cobrir F2B correctamente declarada |

**Score:** 3/4 critérios verificados; 1 gap declarado (critério 3, sub-claim "liga").

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `games/gow2/lift_baseline/gen_manifest.py` | tabula PREAMBLE | ✓ VERIFIED | `preamble_syms` presente, `verify()` inalterada |
| `games/gow2/lift_baseline/MANIFEST.tsv` | 246 marcadores + cabeçalho reconciliado | ✓ VERIFIED | 2 PREAMBLE novos, 3 cifras no cabeçalho |
| `games/gow2/lift_baseline/test_gen_manifest.py` | teste-ouro por mutação | ✓ VERIFIED | 5 PASS reproduzidos |
| `games/gow2/host_gow2_f2b.c` | 7 funções + 6 globais externos + 4 estáticos | ✓ VERIFIED | nm exacto; fidelidade confirmada por diff |
| `games/gow2/recomp_mid_v2/patch_zz_host_api_decls.py` | DECLS+7, GLOBAL_DECLS+6 | ✓ VERIFIED | 26/6 confirmado; 6 testes PASS reproduzidos |
| `games/gow2/build_macos.sh` | bloco condicional para os 2 ficheiros | ⚠️ VERIFIED (mecanismo) / gap (efeito real) | guard correto, nunca observado a disparar COMPILE contra lift real |
| `runtime/ppu/tests/test_ppu_lifter_emit.py` | comentário D-2.5, zero regressão | ✓ VERIFIED | rc=0, `git diff` sem remoções |

### Key Link Verification

| From | To | Via | Status |
|---|---|---|---|
| `gen_manifest.py::main()` | `MANIFEST.tsv` (kind=PREAMBLE) | tabulação nova | ✓ WIRED |
| `MANIFEST.tsv` | `verify_lift_baseline.sh` | `verify()` genérica | ✓ WIRED |
| `host_gow2_f2b.c::f2b_stream_ensure` | `ppu_recomp_002.cpp:369-370` (já declarado) | detecção "já declarado" pelo instalador | ✓ WIRED |
| `build_macos.sh` (guard) | `$LIFT/ppu_recomp_*.cpp` | grep de definição vs protótipo | ✓ WIRED (mecanismo) — mas nunca disparou COMPILE contra um lift real nesta fase |
| `host_gow2_factory.o`/`host_gow2_f2b.o` | linha de link (`clang++ ... $LIFT_OBJS`) | `[ -f ... ] && echo` condicional | ✓ WIRED (mecanismo); ⚠️ NUNCA EXERCITADO contra um boot_gow2 real nesta fase |

### Requirements Coverage

| Requirement | Fase | Descrição | Status | Evidência |
|---|---|---|---|---|
| `REQ-RDY0-2` | Fase 2 | Código host órfão versionado e compilado | ⚠️ PARCIAL (marcado `[COMPLETO]` em `REQUIREMENTS.md` com ressalva declarada) | Critérios 1/2/4 sólidos; critério 3 tem a ressalva do "liga hoje" mais ampla do que o texto actual do requisito admite (cobre só o F2B explicitamente, deveria cobrir também o factory dentro do âmbito desta fase) |

Nenhum requisito órfão detectado — só `REQ-RDY0-2` mapeia para a Fase 2.

### Anti-Patterns Found

Nenhum marcador de dívida bloqueante (`TBD`/`FIXME`/`XXX` real) nos ficheiros
modificados desta fase. A única ocorrência de `XXX` (`gen_manifest.py:8`) é parte da
notação `[XXX]` que descreve o formato de tags do próprio domínio (ex. `[FACTORY]`,
`[TYPE15]`), não um marcador de dívida. Nenhum placeholder, `return null`/`{}` vazio, ou
stub cosmético encontrado nos ficheiros desta fase.

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| Gate do preâmbulo com mutação | `test_gen_manifest.py` | 5x PASS, rc=0 | ✓ PASS |
| Instalador F2B com link E2E | `test_patch_zz_host_api_decls.py` | 6x PASS, rc=0 | ✓ PASS |
| Guard do F2B com 6 fixtures (3 ramos) | grep manual reproduzido | SKIP/SKIP/COMPILE/COMPILE/SKIP/SKIP conforme esperado | ✓ PASS |
| Fidelidade da extracção F2B | diff normalizado | vazio | ✓ PASS |
| Link E2E sintético próprio (factory+f2b) | harness dedicado nesta verificação | LINK rc=0, RUN rc=0, nm confirma T | ✓ PASS |
| Guard contra os 3 lifts reais desta máquina | grep directo | SKIP nos 3, para os 2 ficheiros | ✓ PASS (confirma o GAP, não o critério) |
| Teste-ouro completo da Fase 1 (regressão) | `test_ppu_lifter_emit.py` | rc=0 | ✓ PASS |
| `ctest` baseline do projecto | `ctest --test-dir build-tests` | 21/21 (19 pass + 2 skip esperados) | ✓ PASS |

### Probe Execution

Não aplicável — esta fase não usa a convenção `scripts/*/tests/probe-*.sh`; a
verificação equivalente é feita via `ctest`/scripts Python dedicados (cobertos acima).

## Human Verification Required

Nenhum item requer verificação humana no sentido de comportamento visual/UX. **Existe,
porém, uma decisão que pede resolução humana explícita** (não um "não sei", mas uma
escolha de política):

### 1. Aceitar a leitura permissiva do critério 3, ou exigir a observação real?

**O que fazer:** Decidir se "liga ao build" no critério 3 do ROADMAP significa (a) "o
mecanismo de compilação condicional existe, correto, testado" (leitura permissiva — já
satisfeita) ou (b) "o ficheiro entra de facto num `boot_gow2` real produzido por este
script, hoje" (leitura estrita — ainda não satisfeita para nenhum dos dois ficheiros).
**Esperado:** Uma declaração explícita no `VERIFICATION.md`/`REQUIREMENTS.md` (via
`overrides:` se a leitura (a) for aceite) ou uma tarefa explícita na Fase 3 para
observar o ramo COMPILE disparar contra um lift genuinamente limpo, com `nm` sobre o
`boot_gow2` real resultante.
**Porque humano:** É uma decisão de política de aceite (o que "pronto" significa para
este projecto), não algo que grep possa resolver — e o próprio CLAUDE.md do projecto
delega este tipo de julgamento ("aceites distinguem sempre: provado in-boot vs
offline/unit vs não-exercitado") a uma declaração humana explícita, não a uma inferência
do verificador.

## Gaps Summary

Dos quatro critérios do ROADMAP para a Fase 2, três (1, 2, 4) estão solidamente
verificados com prova reproduzida de forma independente — incluindo um diff byte-a-byte
que confirma a fidelidade da extracção do F2B, algo que os SUMMARYs não tinham
demonstrado ao nível de detalhe aqui apresentado. O critério 3 tem o MECANISMO
correcto e bem testado (guard com dentes reais nos três ramos, compilação standalone
correcta, `nm` exacto), mas a afirmação de que ele está "satisfeito... não só
preparado" para host_gow2_factory.cpp E host_gow2_f2b.c não resiste à medição directa:
contra os três lifts reais disponíveis nesta máquina, o guard dá SKIP para os dois
ficheiros, sempre — nenhum dos dois `.o` jamais entrou num `boot_gow2` real produzido
por este `build_macos.sh`. A única prova de "liga" é um harness sintético, válido como
prova de mecanismo mas não como prova do facto "liga ao build hoje".

Isto não é um bug no trabalho desta fase — o guard está correcto e é exactamente o
desenho certo para não arriscar "duplicate symbol"/estado duplicado silencioso contra
lifts que ainda têm as definições escritas à mão. O gap é de **precisão da afirmação**:
o 02-03-SUMMARY.md e (em menor grau) o REQUIREMENTS.md dizem mais do que foi medido.
A correcção é barata — reformular a frase para declarar "mecanismo pronto e testado;
ligação real ainda não observada contra nenhum lift disponível" — ou o utilizador pode
aceitar explicitamente a leitura permissiva via `overrides:` neste `VERIFICATION.md`.

---

_Verified: 2026-07-26T18:36:11Z_
_Verifier: Claude (gsd-verifier)_


---

## Adenda de 2026-07-29 — o gap fechou, e era um gap de PROVA

Este relatório saiu `gaps_found` porque o critério 3 pedia «verificável por `nm`» e nenhum
`host_gow2_*.o` entrava num `boot_gow2` real. A leitura estava incompleta: o comportamento
depende de o lift trazer ou não as definições, e as duas metades estão agora medidas.

| lift | define os símbolos? | guard | os `.o` entram? |
|---|---|---|---|
| **produção** (`recomp_macos_v2`, com a cadeia de patches) | **sim** — `f2b_stream_ensure` no chunk 002, `ps3_factory_repair_vt` no 001 | SKIP | **não** — e é o correcto: compilar daria `duplicate symbol` no factory e estado duplicado em silêncio no F2B |
| **fresco** (antes dos patches) | não | COMPILE | **sim** — `nm` mostra `T _ps3_factory_repair_vt` e `T _f2b_stream_ensure` |

Confirmado por duas partes independentes: pelo verificador da Fase 3, que correu `RELIFT=1`
para um directório scratch e mediu os dois símbolos no binário resultante; e por medição
directa do orquestrador contra o `boot_gow2` de produção e o `boot_gow2_relift_test`.

O critério está satisfeito no único caso em que se aplica. O que este relatório registou
como gap era a **ausência de prova**, não a ausência do comportamento.

### O defeito que o caminho até à prova destapou

O `host_gow2_f2b.c` declarava `g_wadld_eof_ea` como `extern`, assumindo que a definição
vinha do lift. Vinha do bloco `[BA808]` — órfão, desaparecido no relift. O primeiro link
genuíno deu `Undefined symbols`. **O harness sintético que validou a extracção nesta fase
passava porque fornecia um stub desse símbolo.** Corrigido em `6b50c4c`.

Generaliza-se: sempre que uma prova precisa de um stub para passar, o stub é onde o defeito
se esconde.

### Nota de processo

O commit que fechou o gap (`6b50c4c`) não foi citado por nenhum SUMMARY da Fase 3. Foi o
verificador da Fase 3 que o encontrou ao cruzar o `git log` com os relatórios. Sem esse
cruzamento, o requisito ficaria indefinidamente «parcial» por ninguém saber que já estava
resolvido.
