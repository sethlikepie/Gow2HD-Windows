---
phase: 19-weak-overrides-e-as-migracoes-que-fecham-o-marco
plan: 02
subsystem: build
tags: [weak-overrides, macho, apple-ld, nm, build-macos, guard, ppu-lifter, gates, macos-arm64, gow2, xen-04]

requires:
  - phase: 19-weak-overrides-e-as-migracoes-que-fecham-o-marco
    plan: 01
    provides: "ppc_hooks.h (PPC_FUNC/PPC_FUNC_IMPL), emissao do par __imp_/wrapper fraco atras de flag, e a MEDICAO de que o override forte ganha o ld da Apple"
  - phase: 17-mid-asm-de-ponta-a-ponta
    provides: "padrao da TU host versionada em games/gow2/hooks/, guard anti-duplicate-symbol do build_macos.sh, recipe de re-lift + smoke 25s"
  - phase: 18-jump-tables-detectadas-e-declaradas
    provides: "pisos do smoke (st620_max=11, OOB=0, ICALL-BAD=0) e a armadilha do --config que morre inteiro"
provides:
  - "games/gow2/hooks/gow2_func_overrides.{h,cpp} -- TU host versionada, macro GOW2_FUNC_OVERRIDE"
  - "build_macos.sh: guard anti-duplicate-symbol proprio (enumera do .cpp, procura a FORMA no lift) + link"
  - "config/gow2_recomp.toml: emit_weak_wrappers=true + [[functions_override]] 0x00010230"
  - "PROVA in-boot: o override host corre no boot_gow2 (marcador [FUNCOVR] logo a seguir a '[ppu] run: code 0x00010230')"
  - "PROVA offline: nm -m mostra weak external no lift, external no host, e external (nao weak) no binario ligado"
  - "3 ferramentas de gate ensinadas a ler PPC_FUNC_IMPL (lift_parity, check_contracts, gen_manifest)"
affects: [19-03, 21-fecho-do-marco]

tech-stack:
  added: []
  patterns:
    - "Guard de build que ENUMERA os simbolos a partir do .cpp host, em vez de manter uma lista duplicada no script"
    - "Guard que distingue TRES estados (forte no lift / wrapper fraco / nenhum dos dois), sendo o terceiro o silencioso"
    - "Piloto de override = IDENTIDADE (chama __imp_ e mais nada), para o smoke medir o caminho de LINK e nao uma mudanca de logica"

key-files:
  created:
    - games/gow2/hooks/gow2_func_overrides.h
    - games/gow2/hooks/gow2_func_overrides.cpp
  modified:
    - games/gow2/build_macos.sh
    - games/gow2/config/gow2_recomp.toml
    - tools/lift_parity.py
    - tools/test_lift_parity.py
    - games/gow2/lift_baseline/check_contracts.py
    - games/gow2/lift_baseline/test_check_contracts.py
    - games/gow2/lift_baseline/gen_manifest.py
    - docs/RECOMP_CONFIG.md

key-decisions:
  - "Piloto = func_00010230, o ENTRY POINT do guest: chamado exactamente uma vez e sempre, pela function_table (o cenario de link mais arriscado), com tamanho exacto conhecido e sem nenhum patch_*.py a toca-lo"
  - "Override de IDENTIDADE com marcador gated por PS3_FUNCOVR_TRACE (OFF por omissao) -- o smoke de nao-regressao mede o caminho de link, e uma segunda corrida com o probe mede a execucao"
  - "O guard le a lista de overrides DO .cpp: um override novo em 19-03 nao pode depender de alguem se lembrar de editar tambem o script de build"
  - "Corrigir os gates cegos (lift_parity/check_contracts/gen_manifest) em vez de recalibrar baselines -- o sinal era falso, nao o baseline que estava errado"

patterns-established:
  - "Guard testado contra as QUATRO formas do lift (forte, par fraco, ausente, chaveta na linha seguinte) antes do build caro"
  - "Cada fix de gate nasce do sintoma MEDIDO e leva controlo por mutacao nos dois sentidos (inicio e fim da regiao)"

requirements-completed: [XEN-04]

status: complete
---

# Phase 19 Plano 02: A ponta host do weak override — Summary

O mecanismo que o 19-01 mediu **offline** passou a existir **no `boot_gow2`**:
TU host versionada, guard de build próprio, e um override real que ganha o link
e corre — provado por `nm -m`, por desassemblagem e por marcador in-boot.

**Duração:** ~45 min · **Commits:** 2 monorepo + 2 espelho · **Aceites:** 4/4
**Evidência:** `offline-unit` (nm, otool, greps, 14 suites) + `in-boot` (2 smokes de 25 s)

---

## 1. Os aceites, e o que os prova

| # | Aceite | Estado | Prova |
|---|---|---|---|
| 1 | TU versionada, compilada e ligada, com guard correcto | ✅ | `gow2_func_overrides.o` na linha de link; guard medido contra 4 formas de lift |
| 2 | **Um override real**, por `nm -m` (weak + strong) e comportamento observável | ✅ | `weak external` no lift, `external` no host, **`external` (não `weak`) no binário**; `otool -tV` mostra o corpo host; `[FUNCOVR]` in-boot |
| 3 | Smoke sem regressão: `st620_max ≥ 3`, `OOB/0xFFFF = 0`, `ICALL-BAD = 0` | ✅ | `st620_max=11` (= baseline), `0`, `0`, órfãos `0`, kill por PID |
| 4 | O mid-asm do CE03C continua a funcionar | ✅ | `grep` no lift **e** 7 linhas `[INTROSEQ] CE03C` in-boot — o mesmo perfil 1/5/1 do 17-03 |

**`XEN-04` FECHA.** O aceite escrito é *"`test_weak_override_symbols.py` PASS
(weak + strong override ganha, verificado por `nm`); **1 override trivial liga e
corre**"*. A primeira metade era do 19-01; a segunda é deste plano, e está
medida in-boot, não deduzida.

**`XEN-05` NÃO se move.** O piloto é um override de identidade sobre o entry
point — não é a migração de nenhum `patch_*.py`, e o ledger
`PATCH_MIGRATION.tsv` **não foi tocado**. Reclamar crédito aqui seria
exactamente o tipo de inflação que a Fase 18 recusou.

---

## 2. O piloto: porquê `func_00010230` (escolha medida)

O plano pedia "um piloto trivial, de preferência neutro em comportamento". A
escolha não foi por gosto:

- **É o entry point do guest.** A `EBOOT.ELF` tem `entry OPD 0x00519570`;
  lido do próprio ELF nesta sessão, resolve para `code=0x00010230`
  (`toc=0x00541178`) — o mesmo endereço a que o `runtime/ppu/ppu_loader.cpp` já
  chama *"the literal entry point 0x00010230"*. Se o boot arranca, o override
  correu. Nenhuma outra candidata dá essa garantia sem depender do caminho da
  intro.
- **É chamado pela `function_table`.** `ppu_run` resolve o OPD, procura o code
  addr na tabela e chama `fn`. É o **cenário L4** do 19-01 — a referência está
  na *mesma TU* do wrapper fraco, onde o compilador poderia curto-circuitar. Lá
  era um teste; aqui é o caminho real do jogo.
- **Tamanho exacto conhecido:** `functions.json` diz `start=0x00010230`,
  `end=0x00010254` → `size = 0x24`. O campo é registado e não consumido (19-01),
  mas escreve-se o número verdadeiro.
- **Nenhum `patch_*.py` lhe toca.** Verificado por `grep` sobre os 140 patches:
  a única ocorrência de `00010230` é um **comentário** de cadeia de chamadas em
  `patch_introseq_probe.py`. Isto não é detalhe: com a emissão ligada, a forma
  `void func_X(ppu_context* ctx) {` **deixa de existir** para a função
  declarada, e um patch que a procure por texto não a encontra.
- **Custo zero:** uma chamada por boot. O 19-01 mediu que a `-O0` (o default do
  build) o wrapper é um `bl` real; um piloto chamado por frame seria ruído.

O marcador `[FUNCOVR]` é **probe gated por `PS3_FUNCOVR_TRACE`, OFF por
omissão** (regra 6 do CLAUDE.md). Daí as **duas** corridas de smoke: uma mede
os pisos com o probe desligado, a outra mede a execução.

---

## 3. O guard do `build_macos.sh` — o que se escolheu e a linkagem que o justifica

O brief avisava que o guard do mid-asm tem uma armadilha subtil e que um guard
para os overrides tem "exactamente o mesmo problema". Tem, **e mais um**.

### O que se procura no lift: a FORMA, nunca o nome

Uma TU liftada traz, além da definição, **quatro** coisas que contêm o nome:

```
void func_00010230(ppu_context* ctx);                        <- declaracao (termina em ;)
func_00010230(ctx);                                          <- chamada
{ 0x00010230ULL, func_00010230, "func_00010230" },           <- entrada da function_table
PPC_FUNC(func_00010230) { __imp_func_00010230(ctx); }        <- o proprio wrapper fraco
```

Um `grep` pelo nome casa com todas. **Medido:** no lift sintético com o par
fraco há **6** ocorrências do nome — um guard ingénuo dispararia, saltaria a
compilação, e o override ficaria **por ligar em silêncio** (o wrapper fraco
continua lá e o jogo corre o código liftado; não há erro nenhum a avisar).

As duas expressões exigem `(...)` **sem `;` lá dentro** seguido de `{` (ou de
fim de linha, para a chaveta na linha seguinte). Nenhuma casa com
`PPC_FUNC_IMPL(func_X) {` nem com `PPC_FUNC(func_X) {`, onde a seguir ao nome
vem `)` e não `(`. É exactamente isso que separa o lift **com** wrappers
(compilar) do lift **sem** (não compilar).

### O agravante próprio: a lista de símbolos sai do `.cpp`

O guard do mid-asm ancora num prefixo fixo (`gow2_midasm_`). Aqui os símbolos
são `func_XXXXXXXX` — indistinguíveis das 52 000 funções liftadas. Manter a
lista escrita no script seria garantir que o primeiro override do 19-03 a
deixasse desactualizada. O guard **enumera do próprio `.cpp`**, por duas
expressões (lição do `host_gow2_f2b`: nunca ancorar numa só forma sintáctica):
a macro `GOW2_FUNC_OVERRIDE(func_X)` e, em alternativa, uma definição escrita à
mão `func_X(ppu_context* ctx)` sem a macro.

### O terceiro estado — o silencioso

O aviso do brief sobre o F2B (5 das 7 funções eram `static`, e um guard
só-`extern` teria deixado estado duplicado em silêncio) aplica-se aqui com
outra roupagem. Há **três** estados, não dois:

| Estado do lift | O que acontece se compilarmos | O guard |
|---|---|---|
| definição **forte** `void func_X(...) {` | erro de link | **não compila**, e diz porquê |
| par `PPC_FUNC_IMPL` + wrapper fraco | o override ganha | **compila** |
| **nem um nem outro** | compila, liga, e ninguém chama o símbolo | compila com **AVISO** explícito |

O terceiro é o perigoso: zero erros, override morto. É a mesma classe de falha
que o guard do `host_gow2_f2b` existe para apanhar.

### A linkagem real, medida (é isto que justifica a escolha)

O brief pedia para **verificar a linkagem antes de escolher**. Verificada — e
o resultado é que "duplicate symbol" nem sequer é a única falha possível:

```
(2b) override SEM chamada a __imp_ (substituicao total, o caso do 19-03)
     sobre um lift SEM wrappers:
       duplicate symbol 'func_00010230(ppu_context*)' in:
           m1_f19_dup_host2.o
           m1_f19_dup_lift.o

(2a) override COM chamada a __imp_ (o piloto deste plano)
     sobre um lift SEM wrappers:
       Undefined symbols for architecture arm64:
         "__imp_func_00010230(ppu_context*)", referenced from: m1_f19_dup_host.o
```

Ou seja: sem o guard, um `build_macos.sh` apontado ao lift de produção
(`recomp_macos_v2`, sem `emit_weak_wrappers`) **parte o link** — hoje por
símbolo indefinido, amanhã por duplicado, consoante a forma do override. O
guard evita as duas e declara o estado em vez de o esconder.

### O guard testado ANTES do build caro (4 formas)

O bloco foi extraído do `build_macos.sh` real (linhas 322–354, `sed`) e corrido
sobre quatro lifts sintéticos — o que se testa é o texto do script, não uma
cópia:

```
--- caso: liftA (forma de producao: forte + declaracao + chamada + function_table)
  gow2_func_overrides: DEFINICAO FORTE ja' no lift para: func_00010230 -- nao compilar
    -> objecto: AUSENTE
--- caso: liftB (par __imp_ + wrapper fraco)
  gow2_func_overrides: compilado (overrides: func_00010230 )
    -> objecto: PRESENTE
--- caso: liftC (funcao ausente do lift)
  gow2_func_overrides: AVISO -- o lift nao tem wrapper fraco para: func_00010230
                       (o override compila e liga, mas ninguem o chama: NO-OP SILENCIOSO)
    -> objecto: PRESENTE
--- caso: liftD (chaveta na linha SEGUINTE)
  gow2_func_overrides: DEFINICAO FORTE ja' no lift para: func_00010230 -- nao compilar
    -> objecto: AUSENTE
```

O caso A é o que importa: tinha declaração, chamada **e** entrada da
`function_table`, e nenhuma delas o enganou.

---

## 4. O re-lift e o build (caminho REAL de build, directório NOVO)

```bash
RELIFT=1 OUT=./boot_gow2_f19_ovr ./build_macos.sh ./recomp_macos_v3_ovr19
```

Directório **novo**. `recomp_macos_v2` (produção), `recomp_macos_v3` e
`recomp_macos_v3_jt18`/`_midasm` de outras fases **não foram tocados** (o script
de /tmp tem guarda que aborta se o alvo resolver para um deles, além da guarda
anti-v2 que o próprio `build_macos.sh` já tem).

As três secções coexistem no `--config`, que era o risco herdado do 18-02:

```
Config: 1 midasm_hook(s) loaded
Config: 1 functions_override entry(ies) loaded (weak wrapper emission ON)
Config: 2 switch_table(s) declared in .../gow2_switch_tables.toml
```

| | |
|---|---|
| lift | `../gow2-recomp/recomp_macos_v3_ovr19`, `sha256(cat ppu_recomp_*.cpp)` = `eb7db840ff628c4beec93f9f3cc3f86ea9f6ad15011778ac465bac0591cb2337` |
| binário | `boot_gow2_f19_ovr`, 121 666 768 bytes, `sha256 d863b5f99334ffc3cb72278588a2c52f4234f152864e1d573470b3e368bd7d2d` |
| fases | A (re-lift 20 s + build 1m12) · B (`apply_all_patches`) · C (rebuild + link 35 s) |

`apply_all_patches.sh` deu `APPLIED=116 ALREADY-APPLIED=3 NO-MATCH=0
UNVERIFIED=13 FAILED=8` — **exactamente** os mesmos números e os mesmos 8 nomes
da corrida da Fase 18. `rc_fase_B=1` já era 1 na Fase 18 (vem do gate
`MANIFEST`, dívida congelada).

### As formas emitidas (saída literal)

```
$ grep -n 'PPC_FUNC_IMPL(func_00010230)\|PPC_FUNC(func_00010230)\|__imp_func_00010230' ppu_recomp_*.cpp
ppu_recomp_000.cpp:514:PPC_FUNC_IMPL(func_00010230) {
ppu_recomp_000.cpp:526:PPC_FUNC(func_00010230) {
ppu_recomp_000.cpp:527:    __imp_func_00010230(ctx);

$ grep -c '^void func_00010230(ppu_context\* ctx) {' ppu_recomp_*.cpp
(zero em todos os 7 chunks)
```

### O CE03C da Fase 17 na mesma corrida

```
ppu_recomp_002.cpp:19539:void func_000CE03C(ppu_context* ctx) {
ppu_recomp_002.cpp:19540:        gow2_midasm_Ce03cWaitIdle(ctx);
```

Primeira linha do corpo, como no 17-03. E no binário:
`nm -m boot_gow2_f19_ovr` → `(__TEXT,__text) external _gow2_midasm_Ce03cWaitIdle`.

---

## 5. A prova de `nm -m` (a armadilha do Mach-O respeitada)

O brief avisou: no Mach-O o `nm` simples imprime **`T`** para uma definição
fraca, e só `nm -m` mostra `weak external`. Foi o que se usou.

```
=== (1) OBJECTO HOST (gow2_func_overrides.o)
0000000000000000 (__TEXT,__text) external [no dead strip] __Z13func_00010230P11ppu_context
                 (undefined)     external                 __Z19__imp_func_00010230P11ppu_context
00000000000001e0 (__DATA,__bss)  non-external             __ZZ13func_00010230P11ppu_contextE1n

=== (2) OBJECTO LIFTADO (ppu_recomp_000.cpp.o)
0000000000005004 (__TEXT,__text) weak external __Z13func_00010230P11ppu_context
0000000000003dec (__TEXT,__text) external      __Z19__imp_func_00010230P11ppu_context

=== (3) BINARIO boot_gow2_f19_ovr
00000001068cdae0 (__TEXT,__text) external     __Z13func_00010230P11ppu_context
0000000100004e3c (__TEXT,__text) external     __Z19__imp_func_00010230P11ppu_context
00000001070f090c (__DATA,__bss)  non-external __ZZ13func_00010230P11ppu_contextE1n

=== (3b) o mesmo com `nm` simples -- a armadilha: T para os dois
00000001068cdae0 T __Z13func_00010230P11ppu_context
0000000100004e3c T __Z19__imp_func_00010230P11ppu_context
```

**Como é que isto prova que o host ganhou?** Duas leituras independentes:

1. No `.o` liftado o símbolo é `weak external`; no binário ligado é
   **`external`**. Se o wrapper fraco tivesse ganho, o binário mostraria
   `weak external`. Dentro *deste* binário, `external` só pode vir do objecto
   host.
2. `__ZZ13func_00010230P11ppu_contextE1n` — o `static int n` **local à função**
   do probe host — está no binário. Esse símbolo não existe em lift nenhum.

E, para não depender só de tabelas de símbolos, a **desassemblagem** do endereço:

```
$ otool -tV boot_gow2_f19_ovr | awk '/^__Z13func_00010230P11ppu_context:/,...'
__Z13func_00010230P11ppu_context:
0001068cdaf0  bl  __ZL18gow2_funcovr_tracev
0001068cdb3c  add x1, x1, #0xdd2 ; literal pool for: "[FUNCOVR] func_00010230 (guest entry) -- override HOST activo, chamada #%d\n"
0001068cdb40  bl  0x106c69784 ; symbol stub for: _fprintf
0001068cdb5c  bl  __Z19__imp_func_00010230P11ppu_context
0001068cdb68  ret
```

O corpo naquele endereço **é** o override host — não o `bl __imp_` de uma linha
que o lifter emite no wrapper.

---

## 6. Os smokes — números e nível de evidência

Recipe idêntico ao baseline da Fase 16 e aos smokes do 17-03 e 18-03
(`env_gow2.sh` + `PS3_NO_RSX=1 PS3_RSX_BACKEND=trace PS3_PERF_FSM=1
PS3_MOVIE_EOS=0`; `PS3_TRACE_*` limpos), 25 s, **kill por PID** (`TERM`, 1 s,
`-9`) — nunca `pkill -f`. Script `/tmp/m1_f19_smoke.sh`, logs em `/tmp`, tudo
fora dos dois repositórios (G6). `sticky` **não** é critério (não-observável,
medido na Fase 16 — WINDOWS #4).

| Métrica | Baseline F16/17/18 | **A: probe OFF** | **B: `PS3_FUNCOVR_TRACE=1`** |
|---|---|---|---|
| `st620_max` | 11 (piso ≥3) | **11** | **11** |
| `oob_or_ffff_hits` | 0 | **0** | **0** |
| `icall_bad_hits` | 0 | **0** | **0** |
| `orfaos_pos_run` | 0 | **0** | **0** |
| `sequencia_st620` | `0 1 3 3 3 11 11` | `0 1 3 3 3 11 11` | `0 1 3 3 3 11 11` |
| `lifted_functions` | 52142 | 52142 | 52142 |
| `introseq_ce03c_hits` | 1 | **1** | **1** |
| `funcovr_hits` | — | **0** (como desenhado) | **1** |

Logs: `/tmp/m1_fase19_smoke_weak_trivial.log` (3842 linhas, 248 793 B) e
`/tmp/m1_fase19_smoke_weak_probe.log` (3841 linhas, 248 762 B).

### A evidência in-boot de que o override corre

```
42:[ppu] run: code 0x00010230, toc 0x00541178, sp 0x0FF00000
43:[FUNCOVR] func_00010230 (guest entry) -- override HOST activo, chamada #1
44:[crt] sys_initialize_tls: block 0x0E000000, ...
```

O runtime despacha pela `function_table` para `code 0x00010230` e quem responde
é o **host**. A string `FUNCOVR` tem **zero** ocorrências nos 7 chunks do lift
(`grep -c` = 0 em todos) — só pode vir do `gow2_func_overrides.o`.

### O CE03C não regrediu (aceite 4, in-boot)

```
3801:[INTROSEQ] CE03C wait-idle 1st movie st620=1
3810:[INTROSEQ] CE03C wait tick st620=3 i=0
...
```

| Marcador | 17-03 | A (probe OFF) | B (probe ON) |
|---|---|---|---|
| `CE03C wait-idle 1st movie` | 1 | 1 | 1 |
| `CE03C wait tick st620` | 5 | 5 | 5 |
| `CE03C arm +0x714 st` | 1 | 1 | 1 |
| **total linhas CE03C** | **7** | **7** | **7** |

Perfil idêntico ao medido no 17-03. A Fase 17 **não** foi partida por esta.

> **Ressalva honesta:** um `diff` linha-a-linha dos dois logs dá 1857 linhas de
> diferença. Inspeccionado: é **intercalação de threads** (ordem de
> `sys_lwcond_create`, `THREAD 2`, `spu_workload dispatch`), não comportamento.
> Duas corridas quaisquer do mesmo binário produzem isso. Por isso o que se
> compara são as **métricas**, não os bytes do log — e essas são iguais.

---

## 7. O desvio que a fase descobriu: **três gates cegos** à forma nova

Este é o achado do plano, e não estava previsto em lado nenhum.

Com o primeiro override real ligado, o `verify_lift.sh` acusou:

```
[RED] NO_BODY  func_00010230  existe no ELF, ausente do lift
total: 78 sinais, 28 RED
[lift_parity] current=78 baseline=77 novos=1 resolvidos=0
[lift_parity] NOVO (regressao face ao baseline):
  NOVO  0x00010230/NO_BODY
[lift_parity] FAIL (rc=1)
```

Na Fase 18 este gate dava **PASS**. A função não desapareceu: o
`tools/lift_parity.py` lia o lift com
`^void func_([0-9A-F]{8})\(ppu_context\* ctx\) \{$`, e o corpo agora sai em
`PPC_FUNC_IMPL(func_X) {`. **Cegueira do parser, não código perdido** — e cada
migração do 19-03 traria mais um sinal falso.

A tentação era recalibrar o baseline. Seria mentira: o baseline estava certo.

### Auditado o padrão, não só o sintoma

Procurada a mesma forma no resto das ferramentas. Encontradas **mais duas**:

| Ferramenta | Estado medido | Consequência se ficasse |
|---|---|---|
| `tools/lift_parity.py` | **a falhar agora** (`NO_BODY`, 77→78) | gate vermelho a cada override |
| `games/gow2/lift_baseline/check_contracts.py` | **vermelho falso confirmado** | ver abaixo |
| `games/gow2/lift_baseline/gen_manifest.py` (`preamble_end`) | **não disparou** neste lift | fronteira preâmbulo/corpo errada, dependente da ordem |

O do `check_contracts.py` é o mais grave, e foi medido contra um lift sintético
com a forma nova:

```
[FAIL] opd-32e200 (patch_32e200_opd.py): ancora perdida: func_0032E200 nao existe no lift
[FAIL] opd-32e200-noindirect (...): ancora perdida: func_0032E200 nao existe no lift
```

`0x0032E200` é **um dos P0 `alvo=weak` que o 19-03 vai migrar**. O defeito
estava programado para só aparecer quando já custasse caro: a migração seguinte
faria os seus próprios aceites `contract:` darem vermelho sobre uma função que
lá está inteira.

### Os fixes, e o controlo por mutação de cada um

- **`lift_parity.py`**: passa a reconhecer `PPC_FUNC_IMPL(func_X) {`. O
  **wrapper** deliberadamente **não** conta como início — vem logo a seguir ao
  corpo e, se contasse, sobrescrevia a entrada com um corpo de uma linha,
  trocando `NO_BODY` por um `TRUNCATED` igualmente falso (e mais plausível).
  Teste novo `test_scan_lift_le_o_corpo_de_um_weak_override`, escrito a partir
  do sintoma. **Mutação:** reverter o regex → `KeyError: 66096` (0x10230).
  **Depois do fix:** `current=77 baseline=77 novos=0` → **PASS**.
- **`check_contracts.py`**: o delimitador de **início** conhece as duas formas;
  o de **fim** conhece as três (`void func_`, `PPC_FUNC_IMPL(func_`,
  `PPC_FUNC(func_`), senão a região invadia a função seguinte. Teste novo
  `test_8_weak_override_nao_e_ancora_perdida`, com três asserções
  (`count_ge 7`, `count_eq 7` e `__imp_func_ == 0`). **Mutação A** (esquecer o
  início) → volta a "âncora perdida"; **mutação B** (esquecer o fim) → apanhada
  pelo contrato `__imp_func_ == 0`. Região medida: 4 linhas, sem o wrapper e
  sem a função seguinte.
- **`gen_manifest.py`**: `preamble_end` idem. Não chegou a disparar
  (`MANIFEST_DEBT` deu os mesmos `7/36` do baseline da Fase 18), o que o torna
  precisamente o bug dependente de ordem que só aparece tarde.

### O gate depois dos fixes

```
lift_parity          PASS      current=77 baseline=77 novos=0 resolvidos=0
MANIFEST             FAIL      current=7  baseline=36 novos=7 resolvidos=36
audit_boundaries     PASS      current=180 baseline=180 novos=0 resolvidos=0
```

`MANIFEST FAIL` é **dívida pré-existente**: números idênticos, ao número, aos da
corrida da Fase 18. `rc=1` global vem de lá, não desta fase.

---

## 8. Desvios do plano

1. **[Regra 1 — bug] Três ferramentas de gate cegas à forma nova.** Secção 7.
   Uma medida a falhar, uma medida a dar vermelho falso, uma latente. Corrigidas
   com teste nascido do sintoma e controlo por mutação. Sem isto, o 19-03
   herdava um vermelho falso nos seus próprios aceites.
2. **[Regra 2 — funcionalidade crítica em falta] O guard tem três estados, não
   dois.** O plano falava de "definição vs protótipo". Medido: falta o estado
   "nem definição forte nem wrapper fraco", em que tudo compila, tudo liga, e o
   override é um no-op invisível. Ganhou aviso explícito.
3. **[Regra 2] O guard enumera os símbolos do `.cpp`.** O plano deixava
   implícito que a lista viveria no script. Com símbolos `func_XXXXXXXX`
   (indistinguíveis das 52 000 funções) isso seria garantir que o primeiro
   override do 19-03 a deixasse desactualizada.
4. **Duas corridas de smoke em vez de uma.** A regra 6 do CLAUDE.md exige o
   probe OFF por omissão; medir o piso e observar o marcador na mesma corrida
   seria violá-la. Corrida A mede os pisos, corrida B mede a execução.
5. **`docs/RECOMP_CONFIG.md` acrescentado** (não estava em `files_modified`):
   é o documento canónico do mecanismo e passou a existir uma metade host.

---

## 9. Fronteiras respeitadas

- **Árvore principal**, nunca worktree — `../gow2-recomp` resolve, e é lá que o
  build físico corre.
- **Espelho (G8):** os 4 ficheiros do Task 1 foram para o `../gow2-recomp` no
  **mesmo** commit (`8a0b034`) — a armadilha do 17-03 que a Fase 18 mediu não se
  repete. Verificado por `diff -r` que monorepo e espelho ficaram idênticos.
  O `lift_baseline/` do espelho (duplicado que o `apply_all_patches.sh` **não**
  usa — ele vai buscar a versão do monorepo por `PS3_ENGINE_ROOT`) estava em dia
  antes e foi sincronizado (`a94a231`).
- **G4 — Windows não regride:** zero tokens Win32 nos ficheiros novos
  (`grep` de `_WIN32|__declspec|windows.h|Sleep(|winmm|d3d12|intrin.h` sai
  vazio); `build_d3d.sh` não referencia o ficheiro e não foi tocado.
- **Outra sessão activa:** `.planning/PROJECT.md`, `MILESTONES.md`,
  `ROADMAP.md`, `CLAUDE.md`, `games/gow2/CLAUDE.md`, `config.json`,
  `milestones/v1.2-*`, `scripts/gsd_plan*.sh` continuam modificados na árvore e
  **fora** dos quatro commits (verificado por `git diff --name-only`).
- **Zero dados de jogo, logs, binários ou lifts** em qualquer commit.
- **`roadmap.update-plan-progress` e os verbos `state.*` não foram usados.**

---

## 10. Verificação (reproduzível)

```bash
# offline-unit
.venv/bin/python3 tools/test_weak_override_symbols.py
.venv/bin/python3 tools/test_midasm_emit.py
.venv/bin/python3 tools/test_recomp_config.py
.venv/bin/python3 tools/test_lift_parity.py
.venv/bin/python3 games/gow2/lift_baseline/test_check_contracts.py
rg -n "gow2_func_overrides" games/gow2/build_macos.sh
PS3_ENGINE_ROOT=$(pwd) ./games/gow2/verify_lift.sh ../gow2-recomp/recomp_macos_v3_ovr19
nm -m ../gow2-recomp/boot_gow2_f19_ovr | grep func_00010230

# in-boot
/tmp/m1_f19_smoke.sh /tmp/m1_fase19_smoke_weak_trivial.log
/tmp/m1_f19_smoke.sh /tmp/m1_fase19_smoke_weak_probe.log PS3_FUNCOVR_TRACE=1
```

14 suites corridas, **todas PASS**: `test_weak_override_symbols`,
`test_midasm_emit`, `test_recomp_config`, `test_lift_parity`,
`test_switch_tables`, `test_ppu_jumptables`, `test_invalid_instructions`,
`test_check_contracts` (8/8), `test_gen_manifest`, `test_gen_catalog`,
`test_baseline_delta`, `test_gen_migration_ledger`, `test_manifest_delta_gate`.

---

## 11. Notas para o 19-03

- **O caminho está aberto:** acrescentar uma entrada `[[functions_override]]` +
  um corpo em `gow2_func_overrides.cpp` é tudo o que uma migração precisa. O
  guard descobre o símbolo novo sozinho.
- **Cuidado com os patches de texto.** Para uma função com override, a forma
  `void func_X(ppu_context* ctx) {` deixa de existir no lift. Qualquer
  `patch_*.py` que a procure por texto passa a `NO-MATCH`/`FAILED` — o que para
  uma migração é o desejado (o patch cede ao mecanismo), mas tem de ser
  **verificado por nome**, não assumido. Os alvos P0 (`0x0014B1F0`,
  `0x0032E200`, `0x0039E6B4`) têm patches que lhes tocam, ao contrário do piloto.
- **Os aceites `contract:` já funcionam** com a forma nova (secção 7) — foi
  arranjado neste plano precisamente para o 19-03 não tropeçar nisso.
- **O pad do CE03C** (WINDOWS #8) é substituição total: o override **não** deve
  chamar `__imp_` no ramo de abort. Nesse caso a falha de um lift sem wrappers é
  `duplicate symbol` (medido em 2b), não `undefined symbol`.
- **`boot_gow2_f19_ovr` não foi promovido** a produção. `recomp_macos_v2` está
  intacto e continua sem `emit_weak_wrappers` no seu lift — o TOML de produção
  já a tem ligada, portanto o **próximo** `RELIFT=1` sobre produção passará a
  emitir o par. Promoção é decisão de outra fase.

## Known Stubs

Nenhum. Não há stub, `TODO` nem `skip` introduzido por este plano. O override
piloto é de identidade **por desenho declarado**, não por trabalho por acabar —
o comportamento real é o 19-03.

## Self-Check: PASSED

- 10/10 ficheiros declarados em `key-files` existem em disco.
- 4/4 commits citados existem (`ff55f6e`, `1045ab1` no monorepo; `8a0b034`,
  `a94a231` no espelho).
- `git diff --name-only ff55f6e^..HEAD` devolve exactamente os 10 ficheiros —
  zero dados de jogo, zero logs, zero lifts, zero ficheiros da outra sessão.
- Os dois logs de smoke existem em `/tmp` e os números da secção 6 foram
  extraídos deles pelo mesmo pipeline dos smokes das Fases 16/17/18.
