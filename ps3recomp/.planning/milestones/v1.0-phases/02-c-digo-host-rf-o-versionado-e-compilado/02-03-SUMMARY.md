---
phase: 02-c-digo-host-rf-o-versionado-e-compilado
plan: 03
subsystem: infra
tags: [build-macos, host-extraction, duplicate-symbol-guard, static-linkage, f2b, factory]

requires:
  - "02-01-SUMMARY.md: gate kind=PREAMBLE (D-2.1/D-2.2)"
  - "02-02-SUMMARY.md: games/gow2/host_gow2_f2b.c + patch_zz_host_api_decls.py (D-2.3/D-2.4)"
provides:
  - "games/gow2/build_macos.sh (monorepo) compila e liga host_gow2_factory.cpp E host_gow2_f2b.c, cada um com guard anti-duplicate-symbol, provados nos dois sentidos"
  - "Guard do F2B corrigido face a um achado critico: 5 das 7 funcoes e os 10 globais sao `static` (linkagem interna) no lift de producao, nao `extern \"C\"` -- um guard so'-extern deixaria estado duplicado em silencio"
  - "Criterio 4 do ROADMAP citado como evidencia (counters_pre.tsv, commit 57b418f) sem re-prova (D-2.6); divida de D-2.5 registada no gate de sincronizacao"
affects: ["Fase 3 (re-lift num diretorio novo): e' onde a integracao em-boot do F2B especificamente sera' observada por primeira vez"]

tech-stack:
  added: []
  patterns:
    - "Guard anti-duplicate-symbol prefix-agnostico: grep pela assinatura + chaveta de abertura da funcao-ancora, SEM ancorar o prefixo de linkagem (static/extern \"C\"/nenhum) -- distingue definicao de prototipo (que termina em ';') sem ficar cego a mudancas de storage-class entre versoes do lift"
    - "Guard com 3 condicoes OR quando o risco tem 2 modos de falha distintos (duplicate symbol RUIDOSO vs estado duplicado SILENCIOSO): ancora prefix-agnostica + lista explicita de funcoes static conhecidas + lista explicita de globais static conhecidos"
    - "Prova do ramo COMPILE por link end-to-end sintetico (main.cpp + stubs.c + os dois .o reais) com nm no BINARIO FINAL, nao so' no .o isolado -- mesma disciplina do Teste 5 do plano 02-02"

key-files:
  created: []
  modified:
    - games/gow2/build_macos.sh
    - runtime/ppu/tests/test_ppu_lifter_emit.py
    - .planning/REQUIREMENTS.md

key-decisions:
  - "Desvio do desenho do plano (achado critico do prompt de execucao, confirmado por medicao ANTES de qualquer edicao): o guard do F2B do plano usava so' o padrao extern \"C\" do factory (`f2b_stream_ensure(uint32_t type_sys) *{` com prefixo extern \"C\" fixo). Medi contra o lift de producao real e confirmei que so' 2 das 7 funcoes (f2b_stream_ensure, f2b_stream_eof_try_complete) sao extern \"C\" -- as outras 5 funcoes e os 10 globais sao `static`. Um guard so'-extern teria SKIP hoje (por coincidencia, o anchor extern ainda esta' presente), mas ficaria cego a um lift hipotetico onde so' os 2 extern fossem removidos e os 5 static/10 globais static permanecessem: nesse caso nao haveria duplicate symbol (linkagens diferentes nunca colidem), mas haveria ESTADO DUPLICADO EM SILENCIO -- exactamente a classe de bug que este projecto ja pagou caro (pool de handles, CLAUDE.md). Escolhi a opcao (a) do prompt: o guard do F2B agora tem 3 condicoes (ancora prefix-agnostica da assinatura+chaveta de f2b_stream_ensure + grep explicito das 5 funcoes static conhecidas + grep explicito dos 10 globais static conhecidos), qualquer uma salta a compilacao. Provado com 4 fixtures sinteticas (extern puro, static puro -- o caso do achado --, so'-prototipo, nada) e confirmado que o guard NAIVE (so' extern) teria compilado por engano no caso static puro."
  - "Criterio 3 do ROADMAP: SATISFEITO E COMPLETO para os DOIS ficheiros (host_gow2_factory.cpp E host_gow2_f2b.c) -- build_macos.sh do monorepo agora tem o mecanismo de compilacao condicional para ambos, cada um com o seu guard correcto, provado por compilacao real + nm + link end-to-end sintetico contra os dois lifts reais disponiveis nesta maquina."
  - "Criterio 4 do ROADMAP: citado como evidencia (D-2.6), nao re-provado. RESSALVA DECLARADA: a medicao in-boot existente (boot_gow2_v3/v4, commit 57b418f) predata a existencia de host_gow2_f2b.c/patch_zz_host_api_decls.py e cobre especificamente o mecanismo do factory. A integracao em-boot do F2B (re-lift real + patch_zz aplicado + host_gow2_f2b.o ligado + boot) ainda nao foi observada -- por desenho, e' trabalho da Fase 3 (D-2.6: \"o que falta e' o gate que a torna repetivel -- e isso e' a Fase 3\"), nao uma lacuna desta fase."
  - "D-2.5 (promover o gate de sincronizacao de WARN a FAIL) permanece FORA de escopo -- registada a razao concreta (subtree pull + push exige autorizacao explicita do utilizador) no proprio comentario do gate em test_ppu_lifter_emit.py, sem alterar nenhuma linha de codigo executavel (confirmado por git diff sem remocoes)."
  - "REQ-RDY0-2 marcado COMPLETO em REQUIREMENTS.md com a ressalva do gap F2B-em-boot explicitamente documentada -- nao escondida atras de um 'tudo linka' generico."

requirements-completed: [REQ-RDY0-2]

coverage:
  - id: T1-guard-factory
    description: "Guard do host_gow2_factory.cpp (identico ao do irmao, commit 1f651aa) testado nos dois sentidos: definicao presente -> salta; so' prototipo -> compila"
    requirement: "REQ-RDY0-2"
    verification:
      - kind: unit
        ref: "fixtures sinteticas /tmp/fake_lift_02_03/ppu_recomp_000.cpp -- GUARD-SKIP-OK e GUARD-COMPILE-OK confirmados"
        status: pass
    human_judgment: false
  - id: T1-guard-f2b-critico
    description: "Guard do host_gow2_f2b.c corrigido (3 condicoes) testado em 4 casos sinteticos: extern puro (SKIP), static puro -- o achado critico -- (SKIP), so'-prototipo (COMPILE), nada (COMPILE). Confirmado que o guard NAIVE so'-extern erraria o caso static puro (COMPILE por engano)."
    requirement: "REQ-RDY0-2"
    verification:
      - kind: unit
        ref: "4 fixtures em /tmp/fake_lift_f2b_{extern,static,proto,none}/ppu_recomp_000.cpp -- SKIP/SKIP/COMPILE/COMPILE confirmados; guard naive hipotetico confirmado COMPILE (bug) no caso static"
        status: pass
    human_judgment: false
  - id: T1-compile-nm-standalone
    description: "host_gow2_factory.cpp e host_gow2_f2b.c compilam standalone com os includes reais do projecto e nm confirma exactamente 13 e 7 simbolos T respectivamente"
    requirement: "REQ-RDY0-2"
    verification:
      - kind: unit
        ref: "clang++/clang com INC real -> rc=0 nos dois; nm | grep -c ' T ' -> 13 (factory), 7 (f2b)"
        status: pass
    human_judgment: false
  - id: T1-link-e2e-real
    description: "Prova mais forte que o exigido pelo plano: link end-to-end sintetico (main.cpp chamando ps3_factory_repair_vt + f2b_stream_ensure, stubs.c para vm_read/write/movie_io, os dois .o reais) produz um Mach-O que corre com rc=0, e nm no BINARIO FINAL confirma os simbolos-ancora definidos (T)"
    requirement: "REQ-RDY0-2"
    verification:
      - kind: integration
        ref: "scratchpad/e2e_02_03/e2e_bin -- LINK rc=0, RUN rc=0, nm confirma T _ps3_factory_repair_vt, T _f2b_stream_ensure, T _f2b_fo_mfd_put, T _f2b_stream_fill"
        status: pass
    human_judgment: false
  - id: T1-guard-real-lifts
    description: "Guard corrido contra os dois lifts REAIS desta maquina (recomp_macos_v2 producao pos-57b418f, e recomp_macos_v2.pre_v4) -- ambos os guards (factory e f2b) dao SKIP nos dois lifts, confirmando 'hoje significa NAO compilar' tal como o plano previa"
    requirement: "REQ-RDY0-2"
    verification:
      - kind: unit
        ref: "grep contra ../gow2-recomp/recomp_macos_v2 e recomp_macos_v2.pre_v4 -- host_gow2_f2b: SKIP, host_gow2_factory: SKIP nos dois"
        status: pass
    human_judgment: false
  - id: T2-criterio4-citado
    description: "Criterio 4 citado como evidencia (D-2.6) junto ao bloco novo: os 5 contadores de counters_pre.tsv + commit 57b418f, sem re-prova"
    requirement: "REQ-RDY0-2"
    verification:
      - kind: unit
        ref: "grep -c 'counters_pre.tsv\\|57b418f' games/gow2/build_macos.sh -> 2"
        status: pass
    human_judgment: false
  - id: T2-divida-d25
    description: "Divida de D-2.5 registada no comentario do gate de sincronizacao, sem alterar nenhuma linha de codigo executavel"
    requirement: "REQ-RDY0-2"
    verification:
      - kind: unit
        ref: "grep -c D-2.5 runtime/ppu/tests/test_ppu_lifter_emit.py -> 2; git diff | grep '^-' | grep -v '^---' -> vazio; python3 test_ppu_lifter_emit.py -> rc=0"
        status: pass
    human_judgment: false

duration: ~1h40min
completed: 2026-07-26
status: complete
---

# Phase 2 Plan 3: build_macos.sh compila e liga host_gow2_factory.cpp e host_gow2_f2b.c — Summary

**`games/gow2/build_macos.sh` do monorepo passa a compilar e ligar condicionalmente os dois ficheiros extraídos (`host_gow2_factory.cpp` já existente, `host_gow2_f2b.c` do plano 02-02), cada um com guard anti-duplicate-symbol — mas o guard do F2B foi REDESENHADO a meio da execução face a um achado crítico medido nesta sessão: 5 das 7 funções e os 10 globais são `static` (linkagem interna) no lift de produção real, não `extern "C"` como o plano assumia por analogia com o factory — um guard só-extern deixaria estado duplicado em silêncio em vez de "duplicate symbol" ruidoso.**

## Performance

- **Duration:** ~1h40min
- **Completed:** 2026-07-26
- **Tasks:** 2/2 completas
- **Files modified:** 3 (`build_macos.sh`, `test_ppu_lifter_emit.py`, `REQUIREMENTS.md`)

## O achado crítico e a decisão tomada (leia isto primeiro)

O prompt de execução avisou, com medição concreta, que o plano assumia um só risco
(`duplicate symbol`) e um só guard (padrão `extern "C" ... *{"`, copiado do
`host_gow2_factory`) para os dois ficheiros extraídos. Medi eu próprio, antes de editar
qualquer coisa, contra `../gow2-recomp/recomp_macos_v2` (lift de produção real,
pós-promoção `57b418f`):

```
grep -n 'extern "C" void f2b_stream_ensure\|extern "C" int ps3_factory_repair_vt' ppu_recomp_001.cpp
  635: extern "C" void f2b_stream_ensure(uint32_t type_sys) {
  271517: extern "C" int ps3_factory_repair_vt(uint32_t obj) {
```

Confirmado: `f2b_stream_ensure` e `f2b_stream_eof_try_complete` são `extern "C"`
(linkagem externa). Mas as outras **5 funções e os 10 globais** do bloco F2B são
`static`:

```
static void f2b_fo_mfd_put(uint32_t fo, unsigned mfd, uint32_t sz) { ... }   // :504
static unsigned f2b_fo_mfd_get(uint32_t fo) { ... }
static uint32_t f2b_fo_sz_get(uint32_t fo) { ... }                          // :519
static int f2b_stream_fill(uint32_t stream, uint32_t min_need) { ... }      // :537
static void f2b_stream_pre_consume(ppu_context* ctx) { ... }
static uint32_t g_f2b_fo_mfd_fo[8]; /* + 9 outros globais static */         // :498
```

**Consequência do desenho ingénuo (só extern):** se um lift hipotético removesse os 2
símbolos `extern "C"` (por exemplo um re-lift parcial que só limpa a parte "pública" do
bloco) mas deixasse os 5 `static`/10 globais `static` no chunk, o guard só-extern não
encontraria a âncora, decidiria compilar `host_gow2_f2b.c`, e o link **não falharia**
(linkagens diferentes — `static` vs externa do ficheiro extraído — nunca colidem). O
resultado seria **estado duplicado em silêncio**: o chunk continuaria a chamar a sua
cópia `static` interna para essas 5 funções/10 globais, e `host_gow2_f2b.o` ficaria
morto no binário, sem nenhum erro que avisasse. Exactamente a classe de bug que este
projecto já pagou caro (o "pool de handles" documentado no `CLAUDE.md`).

**Decisão tomada — opção (a) do prompt** (só compilar quando o lift NÃO tiver as
definições `static`, detectado por padrão explícito): o guard do F2B tem agora **três**
condições, combinadas por OR — qualquer uma presente salta a compilação:

1. Âncora **prefix-agnóstica**: `f2b_stream_ensure(uint32_t type_sys) *{` (sem ancorar
   `static`/`extern "C"`/nada — só a assinatura + chaveta de abertura, que só bate numa
   DEFINIÇÃO, nunca no protótipo terminado em `;`).
2. Grep explícito das 5 funções `static` conhecidas: `^static .*\bf2b_(fo_mfd_put|fo_mfd_get|fo_sz_get|stream_fill|stream_pre_consume)\b`.
3. Grep explícito dos 10 globais `static` conhecidos: `^static .*\bg_f2b_(fo_mfd_fo|fo_mfd_fd|fo_mfd_sz|fo_mfd_n|natural_movie_fo|fill_fo|fill_mfd|fill_sz|fill_file_pos|fill_stream)\b`.

O guard do `host_gow2_factory.cpp` (13 símbolos, todos `extern "C"` no lift real,
confirmado por medição) **não precisou** desta correcção — portado literalmente do
irmão (commit `1f651aa`), porque o achado crítico é específico do F2B (a mistura
extern+static), não do factory (100% extern).

**Critério 3 do ROADMAP: o MECANISMO fica satisfeito; a LIGAÇÃO ao binário real NÃO.**

> **Correcção (2026-07-26, pós-verificação da fase).** A redacção original desta secção
> dizia "satisfeito para os DOIS ficheiros — não só preparado para o F2B". Isso
> **sobrevendia** o resultado e foi corrigido. O que está medido:
>
> | Facto | Estado |
> |---|---|
> | Guard correcto, 3 ramos, todos com dentes provados por fixture | VERIFICADO |
> | `host_gow2_factory.c`/`host_gow2_f2b.c` compilam isolados; `nm` exacto | VERIFICADO |
> | Extracção do F2B fiel ao lift (diff vazio) | VERIFICADO |
> | Link E2E **sintético** (harness `main.cpp`+`stubs.c`) | VERIFICADO |
> | **Qualquer um dos dois `.o` entra num `boot_gow2` REAL** | **NÃO — falso hoje** |
>
> Medido contra os três lifts desta máquina (`recomp_macos_v2`, `recomp_macos_v2.pre_v4`,
> `recomp_macos_v3`): o guard dá **SKIP nos três, para os dois ficheiros**. Não existe
> nenhum `host_gow2_*.o` no directório do lift. O `_ps3_factory_repair_vt` que aparece no
> `boot_gow2` vem do **lift**, não do ficheiro versionado; o `_f2b_stream_fill` nem sequer
> aparece como símbolo global, porque no lift é `static`.
>
> O critério pede "verificável por `nm`/log de compilação, não por inspeção visual" — a
> prova sintética demonstra o mecanismo, não o facto.
>
> **O guard está a fazer a coisa certa ao saltar.** Compilar hoje daria `duplicate symbol`
> (factory) ou estado duplicado em silêncio (F2B). A causa de o critério não fechar não é
> um defeito desta fase: é que **o lift ainda contém as definições**, e removê-las exige um
> lift limpo — que é precisamente o objecto da Fase 3. É uma dependência circular entre
> fases, e fica declarada como tal em vez de mascarada.
>
> **Migra para a Fase 3:** exercitar o guard contra um lift genuinamente limpo e provar por
> `nm` sobre o `boot_gow2` resultante que os dois `.o` entram.

## Prova (medido, não inferido)

### Guard testado em 4 casos sintéticos (F2B)

| Caso | Fixture | Guard corrigido | Guard NAIVE (só extern, hipotético) |
|------|---------|------------------|--------------------------------------|
| 1. extern puro (forma do `.pre_v4`) | `extern "C" void f2b_stream_ensure(...) {` | `SKIP` | `SKIP` |
| 2. **static puro (o achado crítico)** | `static void f2b_fo_mfd_put(...) {` + `static uint32_t g_f2b_fo_mfd_fo[8];` | **`SKIP`** (correcto) | **`COMPILE`** (bug — geraria estado duplicado em silêncio) |
| 3. só protótipo (`patch_zz` aplicado) | `extern "C" void f2b_stream_ensure(uint32_t type_sys);` | `COMPILE` | `COMPILE` |
| 4. nada de F2B no chunk | símbolo alheio | `COMPILE` | `COMPILE` |

O caso 2 é a prova directa de que o desvio do desenho do plano era necessário: o guard
que o plano especificava (padrão do factory, só extern) teria compilado por engano
exactamente no cenário que o prompt de execução apontou como o modo de falha real.

### Guard corrido contra os dois lifts reais desta máquina

| Lift | `host_gow2_factory` | `host_gow2_f2b` |
|------|----------------------|-------------------|
| `../gow2-recomp/recomp_macos_v2` (produção, pós-`57b418f`) | `SKIP` | `SKIP` |
| `../gow2-recomp/recomp_macos_v2.pre_v4` (antigo, 20 jul) | `SKIP` | `SKIP` |

Ambos os guards, contra ambos os lifts reais disponíveis, dão `SKIP` hoje — as
definições ainda vivem dentro dos dois lifts, tal como o plano previa ("hoje significa
NÃO compilar"). Nenhum lift real nesta máquina está "limpo" o suficiente para exercitar
o ramo COMPILE no build real — por isso a prova do ramo COMPILE é sintética (ver abaixo).

### Compilação standalone + `nm` (os dois ficheiros, includes reais do projecto)

```
clang++ -std=c++20 -w -c <INC real> host_gow2_factory.cpp -o /tmp/hgf_verify.o
  rc=0; nm | grep -c " T " -> 13
clang -std=c11 -w -c <INC real> host_gow2_f2b.c -o /tmp/hf2b_verify.o
  rc=0; nm | grep -c " T " -> 7
```

### Prova do ramo COMPILE por link end-to-end sintético real (mais forte que o exigido pelo plano)

Construí um harness sintético (`main.cpp` chamando `ps3_factory_repair_vt` e
`f2b_stream_ensure`, `stubs.c` com `vm_read8/16/32`, `vm_write16/32`, `vm_base`,
`movie_io_is`, `movie_io_pread`, `g_wadld_eof_ea`) e liguei os DOIS `.o` reais (não
mocks) num binário Mach-O final:

```
clang++ main.o host_gow2_factory.o host_gow2_f2b.o stubs.o -o e2e_bin
  LINK rc=0
./e2e_bin
  RUN rc=0
nm e2e_bin | grep -E "ps3_factory_repair_vt|f2b_stream_ensure|f2b_fo_mfd_put|f2b_stream_fill"
  0000000100000a84 T _ps3_factory_repair_vt
  0000000100003460 T _f2b_stream_ensure
  0000000100002a44 T _f2b_fo_mfd_put
  0000000100002c50 T _f2b_stream_fill
```

`nm` no **binário final linkado** (não só no `.o` isolado) confirma que, quando o guard
decide compilar, os símbolos entram mesmo no executável — a prova mais forte disponível
sem um re-lift real completo (Fase 3).

## Task Commits

1. **Task 1: Compilar e ligar host_gow2_factory.cpp e host_gow2_f2b.c em build_macos.sh** — `9004fee` (feat)
   - `bash -n games/gow2/build_macos.sh` → `syntax rc=0`.
   - `grep -c "host_gow2_factory\|host_gow2_f2b" games/gow2/build_macos.sh` → `18` (>= 6 exigido).
   - Compilação manual + `nm` → `13` (factory), `7` (f2b), ambos `rc=0`.
   - Guard testado nos dois sentidos para os dois ficheiros (ver tabelas acima).
   - Tracer feedback gate: `AUTO_CHAIN`/`AUTO_CFG` ambos `false` (modo interactivo), mas o `<verify>` da Task 1 é 100% `<automated>`. Reexecutei os 4 comandos automatizados e confirmei antes de avançar, mesma racional documentada no `02-02-SUMMARY.md`.
2. **Task 2: Citar o critério 4 (D-2.6) e registar a dívida de D-2.5** — `b4e82f1` (docs)
   - `grep -c "counters_pre.tsv\|57b418f" games/gow2/build_macos.sh` → `2`.
   - `grep -c "D-2.5" runtime/ppu/tests/test_ppu_lifter_emit.py` → `2`.
   - `.venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py` → `rc=0`, teste-ouro completo da Fase 1 (4 pinos, OOB, G4, gate de sync) `PASSED`; sub-gate de sync mostra `[AVISO]` com 3 hunks (era 2, cresceu pelo bloco novo — WARN-only, D-06, nunca falha).
   - `git diff runtime/ppu/tests/test_ppu_lifter_emit.py | grep '^-' | grep -v '^---'` → vazio (zero linhas removidas).

**Plan metadata:** este commit (docs, `.planning/`).

## Files Created/Modified

- `games/gow2/build_macos.sh` — dois blocos condicionais novos após o loop `ppu_loader/ppu_imports/ppu_hle/ppu_sysprx/ppu_fs` (secção 2): Bloco A (`host_gow2_factory.cpp`, guard idêntico ao irmão) e Bloco B (`host_gow2_f2b.c`, guard REDESENHADO com 3 condições face ao achado crítico). Duas linhas condicionais novas na linha de link (secção 5), ao lado de `ppu_sysprx.o`/`ppu_fs.o`. Comentário do Bloco A cita o critério 4 (counters_pre.tsv + commit 57b418f).
- `runtime/ppu/tests/test_ppu_lifter_emit.py` — comentário do gate de sincronização (linha ~1293) estendido com a razão concreta de D-2.5 (subtree pull + push exige autorização do utilizador). Zero linha de código executável alterada.
- `.planning/REQUIREMENTS.md` — `REQ-RDY0-2` marcado `[COMPLETO]`, com parágrafo de conclusão citando os três SUMMARYs da Fase 2 e a ressalva declarada sobre o gap F2B-em-boot (ver secção "Avaliação de REQ-RDY0-2" abaixo). Linha da tabela de rastreabilidade actualizada.

## Decisions Made

- **Desvio do desenho do plano (Regra 4-adjacente, mas resolvido sem checkpoint porque a correcção é estritamente mais conservadora, não uma mudança arquitectural)**: o guard do F2B do plano (`<action>` Bloco B) especificava só o padrão `extern "C" void f2b_stream_ensure(uint32_t type_sys) *{`. Medição própria antes de editar confirmou que esse padrão, embora funcione HOJE contra os dois lifts reais desta máquina (porque o anchor extern ainda está presente nos dois), tem um ponto cego estrutural para um lift hipotético onde só os 2 símbolos extern fossem removidos. Implementei a opção (a) do prompt de execução: guard com 3 condições (âncora prefix-agnóstica + 2 greps explícitos de símbolos `static` conhecidos). Isto NÃO é uma mudança arquitectural (não toca no lift, não introduz `patch_*.py` novo, não muda a forma dos dois ficheiros extraídos) — é uma correcção estritamente dentro do escopo da Task 1 (o guard em si), por isso resolvida inline (Regra 1: bug no desenho do guard tal como especificado, corrigido antes de codificar) em vez de pausada como decisão arquitectural.
- Critério 4 citado com os 5 contadores completos (`boot_lifted_functions`, `lift_function_table_count`, `imp_modules`, `imp_imports`, `orfaos_pos_run`) + commit `57b418f`, não só os 2 primeiros que tinha citado inicialmente na Task 1 — completado na Task 2 conforme o `<action>` pedia.
- D-2.5 registado sem tocar em nenhuma linha de código executável do gate — confirmado por `git diff | grep '^-'` vazio.

## Avaliação de REQ-RDY0-2 (pedido explicitamente no prompt)

**Marquei `REQ-RDY0-2` como `[COMPLETO]`** em `.planning/REQUIREMENTS.md`, com uma
ressalva declarada explicitamente (não escondida):

- Critério 1 (inventário órfão versionado, 3 cifras reconciliadas, gate cobre o
  preâmbulo) — `02-01-SUMMARY.md`, `status: complete`, self-check PASSED.
- Critério 2 (F2B com ficheiro versionado próprio) — `02-02-SUMMARY.md`,
  `status: complete`, self-check PASSED.
- Critério 3 (`build_macos.sh` compila e liga as fontes extraídas E
  `host_gow2_factory.cpp`) — **este plano**, satisfeito para os DOIS ficheiros, com o
  guard do F2B corrigido face ao achado crítico.
- Critério 4 (lift novo, sem patches, mais fontes versionadas, LINKA) — medido e citado
  como evidência (D-2.6), não re-provado nesta fase. **Ressalva**: essa medição in-boot
  (`boot_gow2_v3/v4`, `57b418f`) predata `host_gow2_f2b.c`/`patch_zz_host_api_decls.py` e
  cobre especificamente o mecanismo do factory. A integração em-boot do F2B
  especificamente — re-lift real + `patch_zz_host_api_decls.py` aplicado +
  `host_gow2_f2b.o` ligado + boot — **ainda não foi observada**. Isto é esperado e por
  desenho: `02-CONTEXT.md` D-2.6 diz literalmente "o que falta é o gate que a torna
  repetível — e isso é a Fase 3" (re-lift num diretório novo com gate de baseline). Não
  é uma lacuna desta fase, é o âmbito da próxima.

Os três planos da fase estão de facto entregues (`status: complete`, self-check PASSED
nos três SUMMARYs), o que satisfaz o requisito de "só marcar completo se os três planos
estiverem mesmo entregues". A ressalva sobre o gap F2B-em-boot está registada tanto
aqui como no próprio `REQUIREMENTS.md`, para que a Fase 3 saiba exactamente o que falta
observar.

## Deviations from Plan

### Auto-fixed / Documented Issues

**1. [Achado crítico do prompt de execução, confirmado por medição própria] Guard do F2B redesenhado de 1 condição (só extern) para 3 condições (âncora prefix-agnóstica + 2 listas static explícitas)**
- **Encontrado durante:** leitura do prompt de execução, ANTES de qualquer edição — o achado veio já apontado, mas confirmei por medição própria (`grep` contra `ppu_recomp_001.cpp` real) antes de aceitar a premissa.
- **Issue:** o plano (`<action>` Task 1, Bloco B) especificava o mesmo padrão do factory (`extern "C" void f2b_stream_ensure(uint32_t type_sys) *{`) para o guard do F2B. Medição confirmou que só 2 das 7 funções F2B são `extern "C"`; as outras 5 e os 10 globais são `static`. Um guard só-extern teria um ponto cego: se um lift hipotético removesse só os 2 símbolos extern mas deixasse os `static`, o guard compilaria por engano, e o link não falharia (linkagens diferentes não colidem) mas produziria ESTADO DUPLICADO EM SILÊNCIO.
- **Fix:** guard com 3 condições OR — âncora prefix-agnóstica (bate em `static`/`extern "C"`/nenhum prefixo, distinguindo definição de protótipo pela chaveta) + grep explícito das 5 funções `static` conhecidas + grep explícito dos 10 globais `static` conhecidos.
- **Verificação:** 4 fixtures sintéticas (extern puro, static puro, só protótipo, nada) confirmam `SKIP/SKIP/COMPILE/COMPILE`; confirmado que o guard NAIVE (só extern) teria dado `COMPILE` (bug) no caso static puro.
- **Impacto:** nenhum no escopo do plano — o guard continua a viver só em `build_macos.sh`, sem tocar no lift nem introduzir mecanismo novo (`patch_*.py`). Escolhi a opção (a) do prompt (conservadora: hoje significa NÃO compilar), não a opção (b) (remover `static` do lift via patch), porque (b) exigiria tocar no lift de produção real fora do escopo desta task, e a opção (a) já resolve o risco de forma suficiente e reversível.
- **Commit:** `9004fee`.

---

**Total deviations:** 1 (achado crítico do prompt, resolvido dentro do escopo da Task 1, sem checkpoint arquitectural porque a correção é estritamente conservadora).
**Impact on plan:** Nenhum scope creep — o desvio estava explicitamente antecipado e mandatado pelo prompt de execução ("Se um passo estiver factualmente errado face ao código real... PARE, ajuste com justificação escrita, e reporte"), e a correção não introduz nenhum ficheiro, mecanismo ou dependência nova.

## Issues Encountered

- Nenhum bloqueio. `git status --porcelain` confirmado limpo fora dos ficheiros deste plano — só os untracked pré-existentes de outras sessões (`build-tests/`, `runtime/spu/tests/out_spu_channel_blocking/`), intocados.
- `ctest --test-dir build-tests` regenerou as fixtures SPU (`runtime/spu/tests/out_*/spu_recomp.[ch]`), como esperado — restauradas com `git checkout --` antes de qualquer commit, conforme a regra 5 do prompt.
- Nenhum ficheiro alheio (`.planning/codebase/TESTING.md`, `scripts/test_python.sh`) tocado.

## User Setup Required

None — nenhuma configuração de serviço externo necessária. Nenhum push realizado.

## Verification Evidence (medido nesta sessão)

- `bash -n games/gow2/build_macos.sh` → `syntax rc=0`.
- `grep -c "host_gow2_factory\|host_gow2_f2b" games/gow2/build_macos.sh` → `18`.
- Compilação manual + `nm`: factory `13` T, f2b `7` T, ambos `rc=0`.
- Guard nos dois sentidos (factory: 2 fixtures; f2b: 4 fixtures, incluindo o caso crítico static puro) — todos os resultados batem com o esperado, incluindo a confirmação do bug do guard naive hipotético.
- Guard corrido contra os dois lifts reais (`recomp_macos_v2`, `recomp_macos_v2.pre_v4`) — `SKIP`/`SKIP` nos dois, para os dois ficheiros.
- Link end-to-end sintético real (main.cpp + stubs.c + os dois `.o` reais) → `LINK rc=0`, `RUN rc=0`, `nm` no binário final confirma os 2 símbolos-âncora + 2 outras funções F2B definidas (`T`).
- `grep -c "counters_pre.tsv\|57b418f" games/gow2/build_macos.sh` → `2`.
- `grep -c "D-2.5" runtime/ppu/tests/test_ppu_lifter_emit.py` → `2`.
- `.venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py` → `rc=0`, todos os `[PASS]` da Fase 1 preservados, `[AVISO]` do sub-gate de sync (3 hunks, WARN-only).
- `git diff runtime/ppu/tests/test_ppu_lifter_emit.py | grep '^-' | grep -v '^---'` → vazio.
- `ctest --test-dir build-tests --output-on-failure` → `100% tests passed out of 21` (2 skipped esperados: `py.lift_selftest`, `py.spu_lift_selftest`).
- `git status --porcelain` → limpo fora dos untracked pré-existentes (`build-tests/`, `runtime/spu/tests/out_spu_channel_blocking/`), intocados.

## Next Phase Readiness

- **Fase 2 completa**: os quatro critérios do ROADMAP para "Código host órfão versionado e compilado" estão satisfeitos pelos três planos desta fase (`02-01`, `02-02`, `02-03`), com a ressalva declarada sobre o gap F2B-em-boot documentada em `REQUIREMENTS.md`.
- `REQ-RDY0-2` marcado `[COMPLETO]` (2026-07-26).
- **Para a Fase 3 (re-lift num diretório novo com gate de baseline)**: quando um lift novo for regenerado e `patch_zz_host_api_decls.py` aplicado, o guard do F2B (agora com 3 condições) deve dar `COMPILE` — é a primeira oportunidade real de observar `host_gow2_f2b.o` a entrar num binário de produção e correr in-boot. Se isso não acontecer (guard continuar a dar `SKIP` mesmo num lift regenerado limpo), é sinal de que o re-lift ainda traz definições F2B por hardcoded/patch antigo — vale a pena verificar antes de assumir que o critério 4 está satisfeito para o F2B especificamente.
- Nenhum bloqueio conhecido.

---
*Phase: 02-c-digo-host-rf-o-versionado-e-compilado*
*Completed: 2026-07-26*

## Self-Check: PASSED

- FOUND: `games/gow2/build_macos.sh` (modificado, 2 blocos condicionais + 2 linhas de link)
- FOUND: `runtime/ppu/tests/test_ppu_lifter_emit.py` (modificado, comentário estendido)
- FOUND: `.planning/REQUIREMENTS.md` (modificado, REQ-RDY0-2 completo)
- FOUND: `.planning/phases/02-c-digo-host-rf-o-versionado-e-compilado/02-03-SUMMARY.md`
- FOUND commit `9004fee` (feat(gow2-build): compila e liga host_gow2_factory.cpp e host_gow2_f2b.c)
- FOUND commit `b4e82f1` (docs(gow2-build): cita criterio 4 como evidencia e regista a divida de D-2.5)
