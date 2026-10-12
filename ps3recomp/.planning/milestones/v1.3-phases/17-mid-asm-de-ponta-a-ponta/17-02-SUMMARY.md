---
phase: 17-mid-asm-de-ponta-a-ponta
plan: 02
subsystem: build-macos
tags: [midasm, hooks, host, build_macos, link, nm, smoke, macos-arm64, gow2]

requires:
  - phase: 17-mid-asm-de-ponta-a-ponta
    plan: 01
    provides: "contrato do simbolo fixado e verificado: void gow2_midasm_<Name>(ppu_context*), extern \"C\", sem --symbol-prefix, com a declaracao ja' emitida no preambulo da TU gerada"
  - phase: 16-invent-rio-de-patches-e-baseline-medido
    provides: "baseline smoke intro (st620_max=11, oob=0) e a ressalva medida de que o binario era mais velho que o lift"
provides:
  - "games/gow2/hooks/gow2_midasm_hooks.{h,cpp} -- stubs no-op versionados, um por hook"
  - "build_macos.sh compila e liga $LIFT/gow2_midasm_hooks.o com guard anti-duplicate-symbol de duas expressoes, sem ancora de linkagem"
  - "boot_gow2 reconstruido A PARTIR DO LIFT CORRENTE -- a ressalva de idade da Fase 16 fica resolvida para esta medicao"
  - "nm boot_gow2 -> T _gow2_midasm_Ce03cWaitIdle (definido, nao U)"
  - "espelho em ../gow2-recomp (hooks/ + build_macos.sh), commit 109f322"
affects: [17-03 corpo do CE03C + entrada no TOML, 19 weak overrides, 21 metrica re-lift-safe]

tech-stack:
  added: []
  patterns:
    - "Guard anti-duplicate-symbol que casa com a DEFINICAO (assinatura + chaveta) e nunca com o prototipo que o proprio lifter emite no preambulo"
    - "Bloco do script extraido por awk e corrido contra um $LIFT falso para exercitar os DOIS ramos do guard -- teste do codigo real, nao de uma parafrase"

key-files:
  created:
    - games/gow2/hooks/gow2_midasm_hooks.h
    - games/gow2/hooks/gow2_midasm_hooks.cpp
  modified:
    - games/gow2/build_macos.sh
    - games/gow2/config/gow2_recomp.toml

key-decisions:
  - "Guard com DUAS expressoes e ZERO ancora de linkagem: o lifter emite `void gow2_midasm_X(ppu_context* ctx);` no preambulo, logo um guard pelo NOME do simbolo (grep -q gow2_midasm) passaria a saltar a compilacao assim que existisse o primeiro lift com --config -- e o link morreria em undefined symbol"
  - "A linkagem real foi MEDIDA antes de escolher o guard (zero ocorrencias de gow2_midasm_ no lift de producao), nao inferida por analogia com o host_gow2_f2b"
  - "Um stub por hook, mesmo nao implementado (G2): sem stub, declarar um hook da' erro de link em vez de no-op"
  - "GOW2_MIDASM_USED guardado por __GNUC__/__clang__ -- __attribute__((used)) partiria o MSVC e o G4 exige que o Windows nao regrida"
  - "hooks.cpp inclui o ppu_context.h canonico do motor, nao o ppu_recomp.h do lift: dois typedefs distintos ao nivel da linguagem, layout identico campo-a-campo (verificado), e como sao extern \"C\" o simbolo nao leva os tipos"

requirements-completed: []

coverage:
  - id: E1
    description: "games/gow2/hooks/gow2_midasm_hooks.{h,cpp} existem, versionados, com stub extern \"C\" no-op a receber ppu_context*"
    requirement: XEN-02
    verification:
      - kind: unit
        ref: "test -f dos dois ficheiros + grep do simbolo e de ppu_context no .h (ver ## Provas)"
        status: pass
      - kind: other
        ref: "clang++ -std=c++20 -Wall -Wextra sobre o .cpp isolado -> rc=0, nm do .o -> T _gow2_midasm_Ce03cWaitIdle"
        status: pass
    human_judgment: false
  - id: E2
    description: "build_macos.sh compila e liga o objecto com guard anti-duplicate-symbol cuja linkagem foi verificada, nao assumida"
    requirement: XEN-02
    verification:
      - kind: unit
        ref: "8 casos discriminadores das duas expressoes (decl / chamada / comentario / corpo do lift vs definicao plain, static, extern \"C\", chaveta na linha seguinte) -> 8/8"
        status: pass
      - kind: integration
        ref: "bloco EXACTO extraido do build_macos.sh por awk, corrido com $LIFT falso: CASO A (sem definicao) compila, CASO B (definicao static) salta e apaga o .o"
        status: pass
      - kind: integration
        ref: "build real: /tmp/m1_fase17_build.log -> 'gow2_midasm_hooks: compilado', BUILD_RC=0"
        status: pass
    human_judgment: false
  - id: E3
    description: "nm no boot_gow2 reconstruido resolve gow2_midasm_* (definido, nao U)"
    requirement: XEN-02
    verification:
      - kind: integration
        ref: "nm ../gow2-recomp/boot_gow2 | grep gow2_midasm -> '0000000106805c20 T _gow2_midasm_Ce03cWaitIdle'; nm -u | grep midasm -> vazio"
        status: pass
    human_judgment: false
  - id: E4
    description: "Smoke intro apos rebuild nao regride os pisos da Fase 16 (st620_max >= 3, OOB/0xFFFF = 0)"
    requirement: XEN-02
    verification:
      - kind: integration
        ref: "/tmp/m1_fase17_smoke_stubs.log -- st620_max=11 (baseline 11), oob_or_ffff_hits=0 (baseline 0), orfaos=0, kill por PID [in-boot]"
        status: pass
    human_judgment: false

duration: ~35min
completed: 2026-08-02
status: complete
---

# Fase 17 Plano 02: hooks host mid-asm ligados ao `boot_gow2` — Summary

**O símbolo `gow2_midasm_Ce03cWaitIdle` passou a existir de verdade no binário do macOS —
`nm` dá `T`, não `U` — e o smoke da intro sobre um `boot_gow2` **reconstruído do lift
corrente** repete o baseline da Fase 16 (`st620_max=11`, `OOB=0`), que é o que prova que
ligar o objecto não mudou comportamento nenhum.**

## Performance

- **Duração:** ~35 min
- **Tasks:** 2/2
- **Ficheiros:** 2 criados, 2 modificados (+ espelho de 3 no `../gow2-recomp`)
- **Níveis de evidência:** `offline-unit` (guard, compilação, `nm`), `in-boot` (smoke de 25 s)

## Os quatro aceites — estado medido

| # | Aceite | Estado |
|---|---|---|
| 1 | `hooks/gow2_midasm_hooks.{h,cpp}` versionados, stub `extern "C"` no-op por hook | **Cumprido** — `.h:51` declara, `.cpp:49` define `(void)ctx;`. Um stub por hook declarado; o TOML declara **zero** hoje, logo o conjunto de stubs é um superconjunto |
| 2 | `build_macos.sh` compila e liga, com guard anti-duplicate-symbol | **Cumprido** — e o guard tem os **dois** ramos exercitados, com o bloco real do script, não com uma paráfrase |
| 3 | `nm boot_gow2 \| grep gow2_midasm` resolve | **Cumprido** — `0000000106805c20 T _gow2_midasm_Ce03cWaitIdle`; `nm -u` sai vazio |
| 4 | Smoke não regride os pisos da Fase 16 | **Cumprido** — `st620_max=11` contra piso ≥3 e baseline 11; `OOB/0xFFFF = 0` contra baseline 0 |

## Ficheiros

### Criados

- **`games/gow2/hooks/gow2_midasm_hooks.h`** (57 linhas) — declaração `extern "C"` de
  `gow2_midasm_Ce03cWaitIdle(ppu_context*)`, a bater com a que o `ppu_lifter.py` emite no
  preâmbulo da TU gerada. O cabeçalho documenta o contrato **como foi fixado no 17-01**
  (assinatura, `extern "C"`, sem `--symbol-prefix`), não como suposição.
- **`games/gow2/hooks/gow2_midasm_hooks.cpp`** (54 linhas) — corpo `(void)ctx;`, com
  `GOW2_MIDASM_USED` guardado por `__GNUC__`/`__clang__`.

### Modificados

- **`games/gow2/build_macos.sh`** (+46 linhas) — bloco de compilação com guard, a seguir ao
  `host_gow2_f2b`, e `$LIFT/gow2_midasm_hooks.o` na linha de link (condicional `[ -f ]`,
  igual aos vizinhos).
- **`games/gow2/config/gow2_recomp.toml`** (+21/−3) — o comentário de cabeçalho **estava
  errado**: ainda dizia que o TOML não estava ligado ao `ppu_lifter.py` ("isso é a Task A2"),
  o que deixou de ser verdade no 17-01. Reescrito, mais a regra do stub obrigatório no mesmo
  commit e a nota de que `jump_address` é recusado com `rc=2`. **Nenhuma entrada
  `[[midasm_hook]]` foi acrescentada** — isso é o 17-03 — e `elf_path` não foi tocado.

## O guard escolhido, e a linkagem real que o justifica

**Duas expressões, ambas sem âncora de linkagem nenhuma:**

```bash
_midasm_def_same_line='(^|[^A-Za-z0-9_])gow2_midasm_[A-Za-z0-9_]+[[:space:]]*\([^;]*\)[[:space:]]*\{'
_midasm_def_next_line='(^|[^A-Za-z0-9_])gow2_midasm_[A-Za-z0-9_]+[[:space:]]*\([^;]*\)[[:space:]]*$'
```

**A linkagem foi medida, não inferida por analogia** (era o pedido explícito, vindo da lição
do F2B em que 5 das 7 funções eram `static` no lift real):

```
$ grep -c "gow2_midasm" ../gow2-recomp/recomp_macos_v2/ppu_recomp_*.cpp | grep -v ':0'
(vazio -- ZERO ocorrencias nos 7 chunks)
```

Ou seja: hoje o lift de produção não tem **nenhuma** forma do símbolo — nem definição, nem
declaração, nem chamada. Não havia linkagem a copiar; havia é que decidir o que fazer quando
ela aparecer. Daí as duas propriedades do guard:

1. **Não ancora em `static` / `extern "C"` / nada.** É a lição do F2B aplicada: um guard
   só-`extern` não apanharia uma definição `static` colada à mão, e nesse caso **não haveria
   erro de link nenhum** — o chunk usaria a sua cópia interna e o `gow2_midasm_hooks.o`
   ficaria morto no binário, em silêncio. As três linkagens disparam o guard.
2. **Casa com a DEFINIÇÃO, nunca com o protótipo.** Esta é a diferença que o `host_gow2_f2b`
   não tinha de enfrentar e que aqui é a armadilha principal: **o próprio lifter emite
   `void gow2_midasm_X(ppu_context* ctx);` no preâmbulo de toda a TU gerada com hooks**
   (17-01, `_preamble_lines`). Um guard pelo nome do símbolo — o óbvio `grep -q gow2_midasm` —
   passaria a saltar a compilação **no instante em que o 17-03 corresse o primeiro lift com
   `--config`**, e o link morreria em `undefined symbol` sem que o culpado fosse óbvio. As
   duas expressões exigem `{` (ou fim-de-linha, para a chaveta na linha seguinte) depois do
   `)`, e `[^;]*` dentro dos parênteses impede que a chamada `gow2_midasm_X(ctx);` case.

### Prova do guard: 8/8 discriminante, e os dois ramos corridos

Casos de classificação (`/tmp/midasm_guard_t`):

| Entrada | Veredicto |
|---|---|
| `void gow2_midasm_Ce03cWaitIdle(ppu_context* ctx);` (o que o lifter emite) | NÃO-definição |
| `    gow2_midasm_Ce03cWaitIdle(ctx);` (a chamada) | NÃO-definição |
| corpo de função liftada que **chama** o hook | NÃO-definição |
| comentário que menciona o nome | NÃO-definição |
| `void ...(ppu_context* ctx) {` | **definição** |
| `static void ...(ppu_context* ctx) {` | **definição** |
| `extern "C" void ...(ppu_context* ctx) {` | **definição** |
| assinatura numa linha, `{` na seguinte | **definição** |

E, mais forte do que testar as expressões soltas: o **bloco exacto** do `build_macos.sh` foi
extraído por `awk` (14 linhas, do `if` ao `fi`) e corrido contra um `$LIFT` falso, com o
`clang++` substituído por uma função de shell:

```
### CASO A: lift SEM definicao (so' declaracao + chamada) -- estado real de hoje
    [fake clang++ chamado]
    gow2_midasm_hooks: compilado
  -> .o presente no fim: SIM
### CASO B: lift COM definicao static (o cenario da licao do F2B)
    gow2_midasm_hooks: DEFINICAO (static ou extern) ja' no lift -- nao compilar
                       (evita duplicate symbol OU estado duplicado em silencio)
  -> .o presente no fim: NAO
```

O ramo que salta também apaga o `.o` obsoleto (`rm -f`), como os dois blocos vizinhos.

## Provas (saídas literais)

### Build (script que reconstruiu o binário)

O `boot_gow2` foi reconstruído por **`../gow2-recomp/build_macos.sh`** — o espelho do
`games/gow2/build_macos.sh`, que é o que o operador corre porque resolve as fontes host por
`$HERE`. Comando, com `PS3_ENGINE_ROOT` explícito (G6):

```
$ cd ../gow2-recomp && PS3_ENGINE_ROOT=/Users/.../ps3recomp ./build_macos.sh
=== 1. lifted chunks -> .o  LIFT_OPT=-O0 HOST_OPT=-O0 ... (-P 6) ===
  dur=10s objs=7 errors=0
=== 2. runtime PPU sources -> .o (HOST_OPT=-O0) ===
  host_gow2_factory: definicoes ja' no lift -- nao compilar (evita duplicate symbol)
  host_gow2_f2b: definicao (static ou extern) ja' no lift -- nao compilar (...)
  gow2_midasm_hooks: compilado
...
=== 5. link ===
*** .../boot_gow2 built (LIFT_OPT=-O0 HOST_OPT=-O0) ***
BUILD_RC=0
```

Log completo: `/tmp/m1_fase17_build.log` (fora dos repos).

**O rebuild foi real, e resolve a ressalva herdada da Fase 16.** Três dos sete chunks estavam
com `.cpp` mais recente que o `.o` (`000`, `001`, `003`) e foram recompilados — `mtime` passou
a `02/08 17:37` e dois deles mudaram de tamanho (`000`: 23357848 → 23362136; `001`: 21473920 →
21478232). O binário passou de `29/07 15:07` para `02/08 17:37`. Portanto este smoke exercita
o **mesmo** lift que o `verify_lift` da Fase 16 verificou — ao contrário do baseline, que
comparava um binário de 29/jul com um lift de 31/jul.

- `boot_gow2`: 120675480 bytes, `sha256 5988372046255859abeb399b2a1fa9a949cf1e4f173503d04a32a32af898d592`

### `nm` (aceite 3) — saída literal

```
$ nm ../gow2-recomp/boot_gow2 | grep gow2_midasm
0000000106805c20 T _gow2_midasm_Ce03cWaitIdle

$ nm -u ../gow2-recomp/boot_gow2 | grep midasm
(vazio -- nenhum simbolo indefinido)

$ nm ../gow2-recomp/recomp_macos_v2/gow2_midasm_hooks.o | grep midasm
0000000000000000 T _gow2_midasm_Ce03cWaitIdle
```

`T` = definido na secção de texto, com linkagem externa. O `_` inicial é o prefixo do Mach-O.

### Smoke (aceite 4) — `in-boot`, uma corrida, protocolo G6

Recipe idêntico ao baseline da Fase 16 (`env_gow2.sh` + `PS3_NO_RSX=1`
`PS3_RSX_BACKEND=trace` `PS3_PERF_FSM=1` `PS3_MOVIE_EOS=0`; `PS3_TRACE_*` limpos; probes OFF),
25 s, **kill por PID** (`TERM`, 1 s, `-9`) — nunca `pkill -f`. Script em
`/tmp/m1_fase17_smoke_stubs.sh`, log em `/tmp/m1_fase17_smoke_stubs.log`, ambos fora dos dois
repositórios.

```
st620_max=11            (piso >=3; baseline Fase 16 = 11)
oob_or_ffff_hits=0      (baseline Fase 16 = 0)
lifted_functions=52068
sequencia_st620=0 1 3 3 3 11 11
linhas_log=3863  bytes=251535
orfaos_pos_run=0
```

A contagem do `st620_max` é a **mesma** do `smoke_m0_baseline.sh` da Fase 16 (lado direito do
`st620 <de> -> <para>`, com o sentinela `4294967295` = `0xFFFFFFFF` fora do máximo) — copiada,
não reinventada, senão os dois números não seriam comparáveis. `lifted_functions=52068` bate
o baseline exactamente.

**Diferença observada:** a sequência tem um `3` a mais (`0 1 3 3 3 11 11` contra
`0 1 3 3 11 11`). É a cadência da amostragem do sampler, não um estado novo nem um estado
perdido: o conjunto de estados visitados e o máximo são idênticos. Não é tratado como
regressão nem escondido.

**`sticky` NÃO foi usado como critério**, por decisão herdada da Fase 16: os
`ps3_fios_sticky_*` não têm um único `fprintf` em `runtime/` nem `libs/`, portanto o `grep`
daria sempre 0 e 0 não significaria "sticky morto". Métrica sem emissor não é métrica a zero.

### Não-regressão do 17-01

```
$ .venv/bin/python3 tools/test_midasm_emit.py   -> rc=0 (50 asserções)
$ .venv/bin/python3 tools/test_recomp_config.py -> rc=0
$ python -c "load_recomp_config('games/gow2/config/gow2_recomp.toml')" -> midasm_hooks = 0
```

O último confirma que a reescrita do comentário do TOML não partiu o parse e que o ficheiro
continua a declarar zero hooks (o 17-02 não podia acrescentar nenhum).

### Compilação isolada do TU (antes de tocar no build)

```
$ clang++ -std=c++20 -O0 -Wall -Wextra -c -I games/gow2/hooks -I include -I runtime/ppu \
      games/gow2/hooks/gow2_midasm_hooks.cpp -o /tmp/...o     -> rc=0, zero avisos
```

### G4 — Windows não regride

```
$ grep -nE 'Sleep\(|intrin\.h|__declspec|GetModuleHandleA|windows\.h' games/gow2/hooks/*
(vazio)
```

Vale a pena registar o detalhe: à primeira, este `grep` **apanhou o próprio comentário** do
`.cpp`, que listava os tokens proibidos pelo nome. Reescrito para descrever a lista sem a
citar — uma auditoria G4 automática sobre este ficheiro sai limpa em vez de sair a apontar
para prosa.

## Decisões Tomadas

1. **O `.h` inclui `ppu_context.h` do motor, não o `ppu_recomp.h` do lift.** Os dois trazem um
   `typedef struct ppu_context` distinto ao nível da linguagem. Comparados campo a campo nesta
   corrida: `gpr[32]`, `fpr[32]`, `vr[32]` alinhado a 16, `cr`, `lr`, `ctr`, `xer`, `fpscr`,
   `vscr`, `cia`, `thread_id`, `reserve_addr`, `reserve_value`, `reserve_valid` — mesma ordem,
   mesmos tipos. Como o símbolo é `extern "C"`, o nome não leva os tipos e o link é o mesmo.
   É o arranjo que o `ppu_loader` do motor já usa. Verificado, não assumido — e o `-I` do
   `build_macos.sh` resolve bem porque o `$LIFT` **não** tem `ppu_context.h` (só `ppu_recomp.h`).
2. **Opção (A) do plano: o `.o` está sempre na linha de link.** A linha de link do
   `build_macos.sh` **não passa `-dead_strip`** (lido, não suposto), por isso o objecto não é
   descartado. O `__attribute__((used))` foi na mesma, guardado por compilador — mas o
   contrafactual (linkar sem ele) **não foi corrido**, portanto quem prova o aceite é o `nm`
   acima e não o atributo. Está escrito assim no próprio ficheiro.
3. **Um stub por hook, e a regra escrita onde ela é violada.** O TOML declara zero hooks hoje,
   por isso `{Ce03cWaitIdle}` é um superconjunto trivial. Como um hook declarado sem stub dá
   erro de link (falha alta, desejável) mas atrasaria o 17-03, a regra "stub no mesmo commit
   que a entrada `[[midasm_hook]]`" ficou escrita **no comentário do TOML**, que é o ficheiro
   que se edita ao criar o problema, e não só neste SUMMARY.

## Deviations from Plan

### Rule 2 — funcionalidade crítica em falta, adicionada

**1. Guard que distingue definição de protótipo (e não só de linkagem)**
- **Onde:** Task 2, `games/gow2/build_macos.sh`.
- **Problema:** o plano pedia "o padrão do `host_gow2_factory` / `host_gow2_f2b`" e avisava
  para verificar a linkagem. Mas há aqui uma armadilha que nenhum dos dois vizinhos tem: **o
  lifter emite ele próprio a declaração do símbolo** em toda a TU gerada com hooks. Um guard
  copiado por analogia (ou o `grep -q gow2_midasm` óbvio) começaria a saltar a compilação
  exactamente quando o 17-03 fizesse o primeiro lift com `--config`, produzindo um
  `undefined symbol` cuja causa é invisível.
- **Fix:** duas expressões que exigem a chaveta de abertura (ou fim-de-linha) e proíbem `;`
  dentro dos parênteses, sem âncora de linkagem.
- **Verificação:** 8/8 casos discriminadores + os dois ramos do bloco real corridos contra um
  `$LIFT` falso.
- **Commit:** `13301f4`.

**2. Comentário de cabeçalho do `gow2_recomp.toml` corrigido**
- **Onde:** Task 2, ponto 8 (o plano marcava-o "opcional").
- **Problema:** não é cosmético — o comentário **afirmava algo falso desde o 17-01** ("Ainda
  NAO ligado ao ppu_lifter.py"). Quem chegasse ao ficheiro para o 17-03 leria que a ligação
  não existe e procuraria trabalho já feito.
- **Fix:** reescrito para o estado real, mais a regra do stub obrigatório e a nota de que
  `jump_address` é recusado com `rc=2` (para ninguém o tentar usar e levar com um lift abortado).
- **Verificação:** `load_recomp_config` continua a parsear, `midasm_hooks = 0`.
- **Commit:** `13301f4`.

**Total:** 2 auto-fixes (Rule 2 ×2). Nenhum alarga o âmbito: o primeiro é condição de
correcção do que o plano pediu, o segundo corrige documentação que induziria o plano seguinte
em erro.

## O que NÃO foi feito, e não é dado por feito

- **Nenhuma entrada `[[midasm_hook]]` foi declarada** e **nenhum re-lift com `--config` foi
  corrido.** O `boot_gow2` reconstruído é do lift de produção **sem** hooks: a chamada
  `gow2_midasm_Ce03cWaitIdle(ctx)` **não existe** no binário — só a definição existe. O
  aceite 3 é sobre o símbolo RESOLVER, e é isso que está provado; não está provado que o hook
  **corre**. Isso é o 17-03.
- **O CE03C continua a depender do `patch_ce03c_introseq_block.py`.** O stub é no-op de
  propósito. Registado como `stub` aberto em `.planning/WINDOWS.md`.
- **O ramo do guard que salta a compilação nunca disparou num build real** — só no harness com
  `$LIFT` falso. No lift de produção actual não há definição nenhuma para o fazer disparar, e
  injectar uma num chunk de 36 MB gitignored só para o ver disparar seria pior do que o teste
  que foi feito.
- **Uma corrida de smoke, não seis.** O plano diz explicitamente que uma chega para o piso de
  não-regressão (REG-03 multi-corrida não é desta fase). Não se afirma estabilidade estatística.
- **`ROADMAP.md` continua por actualizar** para a Fase 17, pela mesma razão do 17-01: está
  modificado no working tree por outra sessão activa. Já registado em `WINDOWS.md` (entrada do
  17-01); não foi duplicado.

## Fronteira monorepo ↔ checkout de build (G8)

O monorepo é a fonte de verdade. Como o `build_macos.sh` resolve as fontes host por `$HERE`, o
checkout de build precisa das mesmas fontes ao lado dele:

| Ficheiro | Monorepo | `../gow2-recomp` |
|---|---|---|
| `gow2_midasm_hooks.h` | `games/gow2/hooks/` | `hooks/` |
| `gow2_midasm_hooks.cpp` | `games/gow2/hooks/` | `hooks/` |
| `build_macos.sh` | `games/gow2/` | raiz |

Cópia verificada por `diff -rq` (idêntica nos dois lados) e commitada no espelho como
`109f322`, com paths explícitos. O `git subtree pull` reconcilia quando for corrido. **Zero
dados de jogo tocados**: `EBOOT.ELF`, `extracted/`, `movie_cache/`, o `boot_gow2` e os
`recomp_macos_v2/*` continuam fora de qualquer commit (o `boot_gow2` foi reconstruído, mas é
gitignored e não entrou em nada).

## Restrições do projecto respeitadas

- Commits em português, sem `Co-Authored-By`, `git add` com paths explícitos (nunca `-A`/`.`).
- Árvore principal, sem worktree; `../gow2-recomp` resolveu.
- Ficheiros da outra sessão intactos e fora dos commits: `games/gow2/CLAUDE.md` (aparece `M`
  no `git status` e ficou por commitar), `.planning/PROJECT.md`, `MILESTONES.md`, `ROADMAP.md`,
  `CLAUDE.md` da raiz, `.planning/milestones/v1.2-*`, `config.json`, `GSD-MODEL-ROUTING.md`,
  `scripts/gsd_plan*.sh`. No espelho, `recomp_mid_v2/patch_41f78c_prune_probe.py` ficou por
  commitar.
- Nenhum verbo `state.*` nem `roadmap.update-plan-progress` foi corrido.
- Logs todos em `/tmp`; só números entraram no git.
- G4: `grep` de tokens Win32 sobre os ficheiros novos sai vazio.

## Next Phase Readiness

O 17-03 tem o caminho de link fechado e não precisa de inventar nada de build. Passos, por ordem:

1. Pôr o corpo real em `gow2_midasm_hooks.cpp` (substituir o `(void)ctx;`), mantendo
   `extern "C"` e a assinatura.
2. Declarar `[[midasm_hook]]` com `address = 0x000CE03C` em `games/gow2/config/gow2_recomp.toml`
   — **no mesmo commit que o stub/corpo**, senão o link parte.
3. Re-liftar com `--config` para um directório **novo** (`RELIFT=1` do `build_macos.sh` já tem
   a guarda que aborta se o alvo resolver para `recomp_macos_v2`).
4. Espelhar as duas fontes para `../gow2-recomp/hooks/` antes de reconstruir.

**Duas armadilhas a lembrar:** (a) quando o lift passar a trazer a declaração no preâmbulo, o
guard **não** deve disparar — se disparar, a expressão regrediu para casar com protótipos, e o
sintoma será `undefined symbol` no link; (b) o `nm` do 17-03 tem de mostrar, além do `T`, a
**chamada** vinda do lift — o `T` sozinho é o que este plano já prova e não distingue "ligado"
de "a correr".

---
*Fase: 17-mid-asm-de-ponta-a-ponta*
*Concluído: 2026-08-02*

## Self-Check: PASSED

Verificado a 2026-08-02, contra o disco e o `git log` (números recontados, não copiados):

- **10/10 ficheiros** declarados existem, incluindo os três do espelho e os dois logs em `/tmp`.
- **3/3 commits** existem: `5ed4aba`, `13301f4` (monorepo), `109f322` (`../gow2-recomp`).
- `sha256` e tamanho do `boot_gow2` recontados e iguais aos citados
  (`5988372…d592`, 120675480 bytes).
- Contagem de linhas dos ficheiros novos recontada: `.h` 57, `.cpp` 54.
- `st620_max=11` e `oob_or_ffff_hits=0` **re-extraídos do log** com o mesmo pipeline do
  `smoke_m0_baseline.sh`, não relidos do stdout do smoke.
- `diff -q` / `diff -rq`: espelho idêntico ao monorepo nos três ficheiros.
- `nm ../gow2-recomp/boot_gow2 | grep gow2_midasm` reconfirmado após todos os commits.
