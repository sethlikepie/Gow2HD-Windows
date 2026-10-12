---
phase: 19-weak-overrides-e-as-migracoes-que-fecham-o-marco
plan: 03
subsystem: gow2-lift-migrations
tags: [weak-overrides, migracoes, xen-05, ce03c, setjmp-pad, bounds-check, fios, ledger, contratos, macos-arm64]

requires:
  - phase: 19-weak-overrides-e-as-migracoes-que-fecham-o-marco
    plan: 01
    provides: "ppc_hooks.h + emissao do par __imp_/wrapper fraco atras de flag + MEDICAO de que o override forte ganha o ld da Apple"
  - phase: 19-weak-overrides-e-as-migracoes-que-fecham-o-marco
    plan: 02
    provides: "TU host gow2_func_overrides.{h,cpp}, guard do build_macos.sh que enumera do .cpp, 3 ferramentas de gate ensinadas a ler PPC_FUNC_IMPL"
  - phase: 17-mid-asm-de-ponta-a-ponta
    provides: "[[midasm_hook]] Ce03cWaitIdle (a outra metade do CE03C) + politica A (nao apagar o patch legado)"
  - phase: 18-jump-tables-detectadas-e-declaradas
    provides: "escada A/B/C, regra `redundant nao conta`, licao de localizar por FUNCAO e nao por texto solto"
  - phase: 16-invent-rio-de-patches-e-baseline-medido
    provides: "PATCH_MIGRATION.tsv, CONTRACTS.tsv, baseline de smoke e de verify_lift"
provides:
  - "3 weak overrides REAIS: func_000CE03C (pad setjmp), func_0002F3F0 (bounds-check), func_002C0498 (Play re-entrado)"
  - "4 linhas CORRECTNESS estado=migrated no ledger, com prova de re-lift medida em escada A/B/C"
  - "3 contratos executaveis novos: weak-ce03c-pad, weak-2f3f0, weak-2c0498"
  - "WINDOWS #8 FECHADO (com ressalva escrita: prova estrutural, nao de runtime)"
  - "dominio do ledger alargado: alvo=midasm+weak; alvo do b71_2f3f0 reclassificado midasm->weak"
  - "MEDICAO: os P0 classe=OPD NAO sao migraveis por weak override -- razao escrita no ledger"
affects: [21-fecho-do-marco]

tech-stack:
  added: []
  patterns:
    - "Envolver (__imp_) por omissao; substituir so' quando o corpo natural E' o defeito"
    - "Um [[midasm_hook]] e um [[functions_override]] compoem-se na MESMA funcao sem duplicar trabalho"
    - "Contrato com alternativa regex ancorado DENTRO do corpo -- nunca num comentario acima da assinatura"

key-files:
  created: []
  modified:
    - games/gow2/hooks/gow2_func_overrides.cpp
    - games/gow2/config/gow2_recomp.toml
    - games/gow2/recomp_mid_v2/patch_ce03c_introseq_block.py
    - games/gow2/recomp_mid_v2/patch_ce03c_movie_done_reset.py
    - games/gow2/recomp_mid_v2/patch_b71_2f3f0_guard.py
    - games/gow2/recomp_mid_v2/patch_fios_play_already_active.py
    - games/gow2/lift_baseline/CONTRACTS.tsv
    - games/gow2/lift_baseline/PATCH_MIGRATION.tsv
    - docs/RECOMP_CONFIG.md
    - .planning/WINDOWS.md

key-decisions:
  - "CE03C: ENVOLVER (__imp_ dentro do setjmp) em vez de substituir -- o corpo natural continua a vir do lifter e o wait-idle corre UMA vez"
  - "2C0498: SUBSTITUIR (sem __imp_) -- unico caso em que correr o corpo natural REPORIA o bug"
  - "2F3F0: reclassificado de midasm para weak, porque o guard exige return antecipado E tecto no loop"
  - "Contar as duas leituras (linhas e mecanismos) no ledger, congeladas para a Fase 21 arbitrar"
  - "NAO migrar os P0 OPD: a conversao e cirurgia no interior da funcao; um weak override obrigaria a reescrever 783 instrucoes"

requirements-completed: [XEN-05]

status: complete
---

# Phase 19 Plano 03: As migrações que fecham o marco — Summary

Três `patch_*.py` deixaram de ser texto injectado num lift gitignored e passaram
a ser código host versionado, provado por re-lift. O `XEN-05` sai de **2 linhas
/ 1 mecanismo** para **4 linhas / 3 mecanismos** — e fecha nas três leituras
possíveis da métrica, não só na mais generosa.

**Duração:** ~2h · **Commits:** 3 monorepo + 3 espelho · **14 suites offline verdes**
**Evidência:** `offline-unit` (escada A/B/C sobre 3 re-lifts completos, `nm -m`,
`otool`, `strings`, contratos, `verify_lift`) + `in-boot` (3 smokes de 25 s)

---

## 1. O veredicto, primeiro

| Leitura da métrica | Contagem | `XEN-05` (≥3) |
|---|---|---|
| **Linhas** `classe=CORRECTNESS` + `estado=migrated` | **4** | ✅ |
| **Mecanismos, leitura CONSERVADORA** (uma função = um mecanismo) | **3** | ✅ |
| **Mecanismos por declaração no TOML** | **4** | ✅ |
| `classe=OPD` + `migrated` (tally separado, **não** entra no XEN-05) | 0 | — |
| `estado=redundant` (Fase 18, **não** conta) | 2 | — |

**O marco pode fechar INTEIRO no que toca ao `XEN-05`.** Não é o resultado por
que se entrou: a leitura conservadora que o `19-CONTEXT` mandou usar dava 2/3
com só duas migrações, e o plano previa a hipótese de PARCIAL. A terceira
migração (`0x002C0498`) foi feita precisamente para tirar a métrica da zona
ambígua — não para a inflar. Se ela não existisse, este documento diria
**PARCIAL 2/3** com o número escrito.

As quatro linhas, e o mecanismo de cada uma:

| Patch | EA | `alvo` | Mecanismo |
|---|---|---|---|
| `patch_ce03c_introseq_block.py` | `0x000CE03C` | `midasm+weak` | hook `Ce03cWaitIdle` (F17) **+** pad `setjmp` no override (F19) |
| `patch_ce03c_movie_done_reset.py` | `0x000CE03C` | `midasm` | subsumido pelo mesmo hook (regra da Fase 18) |
| `patch_b71_2f3f0_guard.py` | `0x0002F3F0` | `weak` | override host |
| `patch_fios_play_already_active.py` | `0x002C0498` | `weak` | override host |

A leitura conservadora conta **3**: `func_000CE03C`, `func_0002F3F0`,
`func_002C0498` — uma função, um mecanismo, mesmo quando dois mecanismos
diferentes cooperam na mesma função. Os dois números estão **congelados no
cabeçalho do `PATCH_MIGRATION.tsv`** para a Fase 21 arbitrar com eles à frente,
como o CONTEXT pediu.

---

## 2. As três migrações — e os dois padrões que elas revelam

O 19-02 entregou o mecanismo com um override de **identidade**. Ao aplicá-lo a
casos reais aparece uma distinção que não estava escrita em lado nenhum:

| EA | Padrão | Chama `__imp_`? | Porquê |
|---|---|---|---|
| `0x000CE03C` | **envolver** | sim, dentro do `setjmp` | o corpo do jogo continua a ser o comportamento certo |
| `0x0002F3F0` | **envolver com saída antecipada** | sim, excepto no caminho patológico | idem, salvo quando o loop do guest não pararia |
| `0x002C0498` | **substituir** | **não** | o corpo natural **é** o defeito |

**A regra:** chama-se `__imp_` sempre que o corpo do jogo é o comportamento
correcto — assim um re-lift melhora a tradução sem ninguém tocar no host. Só se
**não** chama quando o fix consiste precisamente em não o correr.

### 2.1 `func_000CE03C` — o pad de `setjmp`/`longjmp` (WINDOWS #8)

A Fase 17 migrou o *wait-idle* para o `[[midasm_hook]] Ce03cWaitIdle` e
**declarou em falta** a outra metade: o bloco injectado também envolvia o corpo
natural num `setjmp`, com um ramo de abort (`cellVdec_stop_all_for_play_abort()`
+ reconstrução da freelist do media object). Um mid-asm corre **ao lado** de uma
instrução; não envolve nada. Num lift `--config` esse pad simplesmente não
existia, e nenhum script o repunha.

**Os dois mecanismos compõem-se na mesma função**, e isso é o detalhe que
decide se a migração está certa:

```
PPC_FUNC_IMPL(func_000CE03C) {          <- corpo liftado, simbolo __imp_
        gow2_midasm_Ce03cWaitIdle(ctx);  <- o hook da Fase 17, emitido pelo lifter
        ...corpo natural...
        ...cauda: contador em r30 + trampolim para func_000CE01C...
}
PPC_FUNC(func_000CE03C) { __imp_func_000CE03C(ctx); }   <- wrapper FRACO
```

O override host **não** chama `gow2_midasm_Ce03cWaitIdle`. Se chamasse, haveria
**dois pumps** por entrada no CE03C — e o wait-idle não é idempotente (cada um
espera até 600 ciclos de 50 ms). O `__imp_` já o traz lá dentro; o override
limita-se a envolvê-lo.

**A cauda é repetida no ramo de abort, e isso não é cosmética.** O corpo liftado
termina com `r9 = *(u32*)r30; r9++; *(u32*)r30 = r9; g_trampoline_fn = func_000CE01C`.
No caminho normal essa cauda corre dentro do `__imp_`. No caminho de abort o
`longjmp` salta-a — e o bloco injectado corria-a na mesma, porque estava a
seguir ao pad e não dentro dele. Sem a repetir, um abort deixaria o contador por
incrementar e o trampolim por armar: divergência silenciosa face ao patch.

**Duas divergências face ao bloco injectado, ambas declaradas no código:**

1. O bloco forçava `r30 = _sv_r30` também no caminho **normal**, antes da cauda.
   Aqui é inalcançável (a cauda vive dentro do `__imp_`). **Não é regressão desta
   fase**: é o comportamento que o lift `--config` já tem desde o 17-03, com
   smoke medido verde nas Fases 17, 18 e 19-02. `r30` é callee-saved na ABI PPC.
   No ramo de **abort** — onde a ABI foi violada por um salto não-local — o
   restauro **é** feito, como o patch fazia.
2. O `_sv_r2` do bloco era capturado **depois** do wait-idle; aqui é capturado à
   entrada. Divergiriam só se o pump sujasse o TOC — e nesse caso o valor certo
   é o da entrada, não o sujo.

### 2.2 `func_0002F3F0` — o bounds-check do hash do registry 393E0

O corpo liftado é um `for(;;)` que só termina ao ler um byte zero a partir de
`r4`. `vm_read8` devolve 0 fora das regiões comitadas, mas o host macOS comita
512 MB contíguos de framebuffer/stack (quase todos não-nulos depois do primeiro
render) e 1,27 GB de heap com payload de WAD. Um ponteiro lixo lá dentro faz
**uma** chamada percorrer centenas de milhões de iterações; o 393E0 chama isto
centenas de vezes.

**O `alvo=midasm` do ledger estava errado, e foi corrigido com razão escrita.**
Duas razões independentes: o guard precisa de um **return antecipado** quando o
ponteiro é inválido, e o tecto de 256 caracteres exige **mudar a condição do
salto de volta ao topo do loop**. Um `[[midasm_hook]]` não faz nem uma coisa nem
a outra.

O override envolve em **todos** os casos menos um:

| Caso | O que faz | Fidelidade |
|---|---|---|
| `r4` fora de `[0x10000, 0x4F000000)` | `r3 = 0`, retorna | igual ao bloco injectado |
| há NUL nos primeiros 256 bytes | chama `__imp_` | **é o corpo do jogo** — bit-a-bit o do console |
| 257 bytes sem NUL (patológico) | replica o hash com tecto de 256 | igual ao bloco injectado |

O terceiro caso é o único em que o corpo liftado não pode ser chamado (não
pararia). Aí o hash é replicado **instrução a instrução** do próprio lift:
`h = h*31 + c` (que é o `rlwinm(r10,5,0,26)` seguido de `subf`), e o módulo por
`0x119` com a **mesma constante mágica `0x0749CB29`** que o lift usa, e não com
o operador `%` — para não depender de o compilador escolher a mesma sequência.
O acumulador só importa nos 32 bits baixos (a magia lê `(uint32_t)r10` e o
resultado sai por `(int32_t)`), portanto a recorrência em `uint32_t` é exacta,
não uma aproximação.

**Sem gate de env var, de propósito** — como no bloco original: é um
bounds-check funcional, não um probe, e deixá-lo OFF por omissão seria repor
exactamente o pendurar que ele existe para evitar. Os dois `fprintf` têm tecto
de vida (16 e 8) e no smoke de 25 s aparecem **0 vezes**.

### 2.3 `func_002C0498` — o Play re-entrado (a única substituição total)

O guest, em `0x002C0498`, faz `bl 0x002BFF88` (teardown do container) e
`b 0x002C0124` (re-entra no Play, re-abrindo o `.m2v`). No recompilador isso
corre sob o giant lock e perde a corrida com o pool de ops do FIOS: o open#1
escreveu `container+8`, o Play#2 faz teardown, o open#2 apanha "SEM OP LIVRE" e
escreve 0, e o poll do estado 1 fica a ver `io=0` para sempre.

**Aqui o corpo natural é o defeito.** O override é o epílogo do próprio Play (o
mesmo de `0x002C03FC`), copiado verbatim do patch legado — migrar não é
oportunidade para reescrever. Não chama `__imp_`, e por isso o modo de falha
contra um lift sem wrappers é `duplicate symbol` (e não `undefined symbol`),
exactamente o caso (2b) que o 19-02 mediu. O guard do `build_macos.sh` apanha os
dois antes de compilar.

---

## 3. A prova, por migração (escada A/B/C sobre re-lifts completos)

Três re-lifts da **mesma** `EBOOT.ELF`, em directórios **novos**.
`recomp_macos_v2` (produção) **não foi tocado**.

| Passo | patch | `--config` | Esperado | Medido |
|---|---|---|---|---|
| **A** | não | não | aceite **falha** → o patch era load-bearing | **falha** nos 4 patches |
| **B** | não | sim | aceite **passa** → o mecanismo carrega o fix | **passa** nos 4 |
| **C** | sim (por cima de B) | sim | `WEAK`/skip, sem dupla injecção | `sha256` dos 7 chunks **idêntico** |

### Passo A — a medição que separa `migrated` de `redundant`

```
=== ESCADA A: lift SEM patch e SEM --config ===
-- CE03C --
  void func_000CE03C(...)      : 1
  PPC_FUNC_IMPL(func_000CE03C) : 0
  gow2_midasm_Ce03cWaitIdle    : 0
  'CE03C Play aborted' (pad)   : 0
  'g_ce03c_play_abort'         : 0
-- 2F3F0 --
  void func_0002F3F0(...)      : 1
  PPC_FUNC_IMPL(func_0002F3F0) : 0
  '[B71] 2F3F0 bad str='       : 0
  '_nch < 256' (tecto do loop) : 0
```

E os contratos, contra esse mesmo lift:

```
[FAIL] midasm-ce03c-waitidle ... esperado 1 encontrado 0
[FAIL] weak-ce03c-pad        ... esperado 1 encontrado 0
[FAIL] weak-2f3f0            ... esperado 1 encontrado 0
[FAIL] weak-2c0498           ... esperado 1 encontrado 0
```

Sem os mecanismos, o fix não está lá. É isto que autoriza `migrated` e proíbe
`redundant` — a distinção que a Fase 18 estabeleceu e que custou duas linhas.

### Passo B — o par emitido, e o texto do fix ausente do lift

```
$ grep -n 'PPC_FUNC_IMPL(func_X)|^PPC_FUNC(func_X)' ppu_recomp_*.cpp
ppu_recomp_002.cpp:19539:PPC_FUNC_IMPL(func_000CE03C) {
ppu_recomp_002.cpp:19567:PPC_FUNC(func_000CE03C) {
ppu_recomp_000.cpp:30162:PPC_FUNC_IMPL(func_0002F3F0) {
ppu_recomp_000.cpp:30185:PPC_FUNC(func_0002F3F0) {
ppu_recomp_003.cpp:426968:PPC_FUNC_IMPL(func_002C0498) {
ppu_recomp_003.cpp:426972:PPC_FUNC(func_002C0498) {

forma FORTE `void func_X(ppu_context* ctx) {` : 0 nos tres
texto injectado no lift ('CE03C Play aborted', 'g_ce03c_play_abort',
  '[B71] 2F3F0 bad str=', '_nch < 256', 'FIOS-PLAY-ALREADY-ACTIVE') : 0
```

Contratos verdes no lift B **e** no lift de produção antigo (a alternativa regex
serve os dois), vermelhos no lift A. É o mesmo padrão que a Fase 17 estabeleceu.

**Localizado por função, não por texto solto** (lição da Fase 18). O
`gow2_midasm_Ce03cWaitIdle(ctx);` aparece **duas** vezes no lift; identificadas
as funções donas:

```
chunk000 linha 167040 -> void func_000CE02C(ppu_context* ctx) {   (WINDOWS #9, codigo morto)
chunk002 linha  19540 -> PPC_FUNC_IMPL(func_000CE03C) {           (o alvo)
```

### Passo C — o patch legado sobre o lift do mecanismo

```
ppu_recomp_002.cpp: WEAK (override host -- nada a injectar)
ppu_recomp_002.cpp: MIDASM (movie_done_timebased_reset() vem do hook host)
ppu_recomp_000.cpp: func_0002F3F0 WEAK (override host -- nada a injectar)
ppu_recomp_003.cpp: WEAK (override host -- nada a injectar)

sha256 antes  = d3e697a5395e02525a1d998b17c538e3a18a663d87027e571571bc9e8902c40f
sha256 depois = d3e697a5395e02525a1d998b17c538e3a18a663d87027e571571bc9e8902c40f
ESCADA C: IDENTICO -- zero escritas, zero dupla injeccao
```

### `nm -m` e `otool` — quem ganhou o link

A armadilha do Mach-O respeitada: `nm` simples imprime `T` para uma definição
**fraca**, e só `nm -m` mostra `weak external`.

```
=== lift (ppu_recomp_00N.cpp.o)
  weak external __Z13func_000CE03CP11ppu_context      <- wrapper
  external      __Z19__imp_func_000CE03CP11ppu_context
  weak external __Z13func_0002F3F0P11ppu_context
  weak external __Z13func_002C0498P11ppu_context

=== binario boot_gow2_f19_mig3
  external __Z13func_00010230P11ppu_context   (piloto do 19-02)
  external __Z13func_0002F3F0P11ppu_context
  external __Z13func_000CE03CP11ppu_context
  external __Z13func_002C0498P11ppu_context
```

`external` (não `weak external`) dentro **deste** binário só pode vir do objecto
host. E a desassemblagem do endereço confirma-o sem depender de tabelas de
símbolos:

```
__Z13func_000CE03CP11ppu_context:
  bl  _setjmp
  bl  __Z19__imp_func_000CE03CP11ppu_context
  ... literal pool for: "[INTROSEQ] CE03C Play aborted (FO residual after StartSeq#2)\n"
  bl  _cellVdec_stop_all_for_play_abort
```

E o texto do fix está no **binário** e não no **lift**:

| marcador | binário | lift |
|---|---|---|
| `2F3F0 bad str=` | 1 | 0 |
| `2F3F0 cap str=` | 1 | 0 |
| `CE03C Play aborted` | 1 | 0 |
| `CE03C post-abort freelist rebuild` | 1 | 0 |

---

## 4. Os gates offline

### `apply_all_patches.sh` — o mesmo total, com as migrações a mudar de estado

| Corrida | APPLIED | ALREADY | NO-MATCH | UNVERIFIED | FAILED |
|---|---|---|---|---|---|
| Fase 18 / 19-02 (baseline) | 116 | 3 | 0 | 13 | 8 |
| lift com o CE03C migrado | 116 | 3 | 0 | 13 | 8 |
| \+ o 2F3F0 | 115 | 4 | 0 | 13 | 8 |
| \+ o 2C0498 (final) | **114** | **5** | **0** | **13** | **8** |

Total 140 em todas. Cada migração move um patch de `APPLIED` (escrevia) para
`ALREADY-APPLIED` (não escreve **e** a pós-condição é verdadeira). Os 8 `FAILED`
são os mesmos nomes de sempre — incluindo `patch_ce03c_pre_play_stop.py` e
`patch_ce03c_wait_idle_f2b_movie.py`, que são **verificadores puros** e já
reportavam MISSING antes desta fase.

`rc=1` global: vem do gate `MANIFEST` e do `UNVERIFIED novo`
(`patch_freelist_guard_262610.py`, WINDOWS #11) — dívida congelada da Fase 18,
não causada aqui.

### `verify_lift.sh` sobre o lift das três migrações — **rc=1**, e a razão

```
lift_parity          PASS      current=76 baseline=77 novos=0 resolvidos=1
MANIFEST             FAIL      current=7  baseline=36 novos=7 resolvidos=36
audit_boundaries     PASS      current=180 baseline=180 novos=0 resolvidos=0
```

`MANIFEST FAIL` tem os números **ao número** iguais aos do 19-02 e da Fase 18.
`rc=1` vem de lá.

### O achado não previsto: **o `lift_parity` melhorou**

```
[lift_parity] RESOLVIDO (melhoria, nao falha o gate):
  RESOLVIDO  0x002C0498/CALL_MISMATCH
```

Um patch que reescreve o corpo **dentro do lift** cria divergência face ao ELF —
e o `lift_parity` via-a, correctamente, como um `CALL_MISMATCH` (o `bl 0x002BFF88`
do ELF não aparecia no lift). Ao migrar para host, o lift voltou a ser uma
**tradução fiel** e a divergência passou a viver em código host **declarado**.
É um argumento a favor do mecanismo que não estava previsto em plano nenhum: a
migração não só torna o fix re-lift-safe, como devolve ao gate de paridade a
capacidade de dizer a verdade sobre a tradução.

### Suites offline: 14/14 PASS

`test_weak_override_symbols`, `test_midasm_emit`, `test_recomp_config`,
`test_lift_parity`, `test_switch_tables`, `test_ppu_jumptables`,
`test_invalid_instructions`, `test_ps3_analyse_jtables`, `test_check_contracts`,
`test_gen_manifest`, `test_gen_catalog`, `test_baseline_delta`,
`test_gen_migration_ledger`, `test_manifest_delta_gate`.

---

## 5. O smoke (in-boot), e o que ele **não** prova

Recipe idêntico ao baseline da Fase 16 e aos smokes do 17-03, 18-03 e 19-02
(`env_gow2.sh` + `PS3_NO_RSX=1 PS3_RSX_BACKEND=trace PS3_PERF_FSM=1
PS3_MOVIE_EOS=0`, `PS3_TRACE_*` limpos), 25 s, **kill por PID** (TERM, 1 s, -9) —
nunca `pkill -f`. Script e logs em `/tmp` (G6). Binário **reconstruído do lift
sob teste**, não um binário antigo.

| Métrica | Baseline F16 | CE03C | \+2F3F0 | **final (3 migrações)** |
|---|---|---|---|---|
| `st620_max` (piso ≥3) | 11 | 11 | 11 | **11** |
| `oob_or_ffff_hits` | 0 | 0 | 0 | **0** |
| `icall_bad_hits` | 0 | 0 | 0 | **0** |
| `orfaos_pos_run` | 0 | 0 | 0 | **0** |
| `sequencia_st620` | `0 1 3 3 3 11 11` | idem | idem | **idem** |
| `lifted_functions` | 52142 | 52142 | 52142 | **52142** |
| CE03C `wait-idle`/`tick`/`arm` | 1/5/1 | 1/5/1 | 1/5/1 | **1/5/1** |
| `ce03c_play_aborted` | — | 0 | 0 | **0** |
| `b71_2f3f0_bad` / `cap` | — | 0/0 | 0/0 | **0/0** |

O log termina em `[CONTENT] boot logo queue DONE — hold cleared`, como o
baseline. Zero `Abort`/`Segmentation`/`assert`.

**Ressalva honesta, e é a mais importante deste documento:**

- O **ramo de abort do pad do CE03C NÃO foi exercitado**. Dispara em
  `ps3_indirect_call`, no ramo `[ICALL-ASCII]`, com `g_vdec_startseq_count >= 2`
  **e** `streak >= 12` — condição a que um smoke de intro de 25 s com
  `PS3_MOVIE_EOS=0` não chega (`ce03c_play_aborted=0` nas três corridas). A prova
  do pad é **estrutural e de link** (`nm -m`, `otool`, `strings` bin=1/lift=0),
  não de runtime. Está escrito assim no WINDOWS #8.
- Os dois `fprintf` do guard do 2F3F0 também não disparam — é o esperado num
  boot saudável (o guard existe para o caminho patológico).
- O que **é** in-boot: o wait-idle do CE03C (7 linhas, perfil 1/5/1 idêntico ao
  17-03) e a não-regressão do caminho FIOS do Play com o `0x002C0498` a vir do
  host — se o override estivesse errado, o `st620` não chegaria a 11.

---

## 6. Os aceites do plano

| # | Aceite | Estado | Prova |
|---|---|---|---|
| T1.1 | WINDOWS #8 fechado **ou** aberto com razão escrita | ✅ **fechado** | secção 2.1 + ressalva escrita na própria entrada |
| T1.2 | Re-lift sem o patch do CE03C mantém wait-idle **e** pad | ✅ | escada A/B, `nm -m`, `otool`, `strings` |
| T1.3 | Sem duplo pump de wait-idle na intro | ✅ **medido** | override não chama o hook; smoke dá 1/5/1, igual ao 17-03 |
| T2.1 | `CORRECTNESS migrated` ≥ 3 **ou** PARCIAL com o número real | ✅ **4** | ledger recontado por script |
| T2.2 | Cada `migrated` novo com prova do passo B, por função | ✅ | secção 3 |
| T2.3 | `redundant` / OPD não contados | ✅ | ledger: OPD migrated=0, redundant=2 fora da conta |
| T2.4 | As duas contagens escritas para a Fase 21 | ✅ | cabeçalho do `PATCH_MIGRATION.tsv` |
| T3.1 | `verify_lift`/contratos com rc real | ✅ | rc=1 por `MANIFEST` (dívida congelada, números iguais) |
| T3.2 | Pisos do smoke registados | ✅ | secção 5 |
| T3.3 | Número final + mecanismos no SUMMARY | ✅ | secção 1 |
| T3.4 | Disposição do WINDOWS #8 escrita | ✅ | `status: fixed` + razão longa |

---

## 7. Desvios do plano

1. **[Regra 2 — funcionalidade crítica em falta] Uma terceira migração
   (`0x002C0498`) que o plano não nomeava.** O plano listava
   `b71_2f3f0_guard`, `factory_block_ty15_install` e `wad_state_machine`. Medidos
   os três: o `factory` arrasta um bloco de **553 linhas** de definições host que
   ainda vive dentro do lift (`host_gow2_factory.cpp` existe versionado e **não
   está ligado ao build**) e o seu *hunk* de saída lê `r29` **antes** do epílogo
   o restaurar — inalcançável a partir de um wrapper; o `wad_state_machine` tem
   como aceite `[WADLD-CALL]`, que é um **probe gated**, não o fix (a conversão
   OPD, que é interior à função). Nenhum dos dois cabia sem trabalho que não é
   desta fase. `patch_fios_play_already_active.py` foi escolhido por medição: uma
   função, corpo inteiro substituído, EA confirmada, e **carregada hoje** (estava
   `APPLIED` no lift de produção). Sem ela a métrica ficava em 2 mecanismos pela
   leitura conservadora — e este documento diria PARCIAL.
2. **[Regra 1 — aceite errado] O contrato `weak-2c0498` não podia ancorar no
   marcador do patch.** Primeira versão usava `FIOS-PLAY-ALREADY-ACTIVE`.
   Medido: em `recomp_macos_v2` esse marcador vive no **comentário acima** da
   assinatura, portanto **fora** da região `escopo=ea` — e o contrato dava
   `[FAIL]` sobre um lift onde o fix **está** aplicado. Reancorado na leitura do
   epílogo (`vm_read64(ctx->gpr[1] + 0x288)`), que existe no corpo substituído e
   não no natural. É a mesma classe de erro que a Fase 18 apanhou com o escopo
   dos `case`, noutra roupagem — e foi apanhada por **correr** o contra-exemplo,
   não por o imaginar.
3. **[Regra 1 — bug] `alvo=midasm` do `patch_b71_2f3f0_guard.py` estava errado.**
   Reclassificado para `weak` com razão escrita no ledger (return antecipado +
   tecto no loop; nenhum dos dois cabe num mid-asm).
4. **Domínio do ledger alargado: `alvo=midasm+weak`.** Um patch pode precisar de
   dois mecanismos — é o CE03C. Segue o precedente das Fases 17 (`alvo=lifter`)
   e 18 (`estado=redundant`): cada fase alarga o vocabulário com a justificação
   por escrito.
5. **`patch_fios_play_already_active.py` e `patch_ce03c_movie_done_reset.py` não
   estavam em `files_modified`.** O primeiro por ser a migração acrescentada; o
   segundo porque também procura `void func_000CE03C(...)` por texto e daria
   vermelho falso.
6. **Os P0 `classe=OPD` não foram migrados** — e isso é um **achado medido**, não
   uma desistência: ver a secção seguinte.

---

## 8. O que NÃO se migrou, e porquê (medido)

O `19-CONTEXT` punha os P0 `classe=OPD` com `alvo=weak` em prioridade 2:
`patch_14b1f0_opd.py`, `patch_32e200_opd.py`, `patch_factory_opd_gate.py`.
Não foram migrados, e a razão está escrita no cabeçalho do ledger:

**A conversão OPD é cirurgia no INTERIOR da função.** Troca a sequência
`ctr`/`r2` + `ps3_indirect_call` por `ps3_call_opd` em **N sítios** (12 em
`0x0014B1F0`, 7 em `0x0032E200`). Um weak override substitui a função **inteira**
e não vê o interior: a única forma de o fazer com este mecanismo seria reescrever
a função toda em código host — e `0x0014B1F0` tem `0xC3C` bytes, **783
instruções**. Isso não é uma migração, é uma reescrita, e congelava o corpo do
jogo contra o próprio re-lift que esta fase existe para tornar seguro.

O `alvo=weak` dessas linhas fica portanto **por rever** no ledger, com o
candidato natural anotado: uma opção do lifter que converta OPD por declaração,
não um hook host. `patch_icg_ctor_opd.py` continua fora por `ea='-'`, como a
Fase 16 decidiu.

---

## 9. Fronteiras respeitadas

- **Árvore principal**, nunca worktree.
- **Espelho (G8):** os ficheiros de `games/gow2/*` foram para o `../gow2-recomp`
  nos **mesmos três commits** (`113f3ac`, `1652ab4`, `14024b4`).
- **Produção intacta:** `recomp_macos_v2` não foi tocado. Os três re-lifts foram
  para directórios **novos** (`recomp_macos_v3_mig19`, `_mig19b`, `_mig19c`) e o
  lift A para `/tmp`. Nenhum binário foi promovido.
- **Zero dados de jogo, logs, binários ou lifts** em qualquer commit.
- **Outra sessão activa:** `.planning/PROJECT.md`, `MILESTONES.md`, `ROADMAP.md`,
  `CLAUDE.md`, `games/gow2/CLAUDE.md`, `config.json`, `milestones/v1.2-*`,
  `scripts/gsd_plan*.sh` continuam modificados na árvore e **fora** dos commits.
  No espelho, `recomp_mid_v2/patch_41f78c_prune_probe.py` ficou igualmente fora.
- **`roadmap.update-plan-progress` e os verbos `state.*` não foram usados.**
- **Técnicas proibidas (CLAUDE.md regra 5):** nenhuma. Não há CRC bypass, stub de
  combination, magic estampado, null-check cosmético nem guard mascarado. As três
  migrações são código host que faz **o mesmo** que o texto injectado fazia.
- **G4 — Windows não regride:** `gow2_func_overrides.cpp` continua sem tokens
  Win32 (o único `#if defined(_MSC_VER)` é o ramo de `thread_local` do
  `g_trampoline_fn`, copiado do `gow2_midasm_hooks.cpp`); o `build_d3d.sh` não
  referencia o ficheiro.

---

## 10. Verificação (reproduzível)

```bash
# offline-unit
.venv/bin/python3 tools/test_weak_override_symbols.py
.venv/bin/python3 tools/test_midasm_emit.py
.venv/bin/python3 tools/test_recomp_config.py
.venv/bin/python3 games/gow2/lift_baseline/test_check_contracts.py

# escada A (sem patch, sem config) -- os aceites TEM de falhar
python3 tools/ppu_lifter.py ../gow2-recomp/EBOOT.ELF \
  --functions ../gow2-recomp/functions.json -o /tmp/m1_f19_lift_A -j 4
for p in patch_ce03c_introseq_block patch_b71_2f3f0_guard patch_fios_play_already_active; do
  .venv/bin/python3 games/gow2/lift_baseline/check_contracts.py \
    games/gow2/lift_baseline/CONTRACTS.tsv /tmp/m1_f19_lift_A --patch $p.py
done

# escada B (sem patch, com config) -- os mesmos aceites TEM de passar
python3 tools/ppu_lifter.py ../gow2-recomp/EBOOT.ELF \
  --functions ../gow2-recomp/functions.json \
  --config ../gow2-recomp/config/gow2_recomp.toml \
  -o ../gow2-recomp/recomp_macos_v3_mig19c -j 4

# cadeia real de build + gates
cd ../gow2-recomp && PS3_ENGINE_ROOT=<raiz> ./apply_all_patches.sh ./recomp_macos_v3_mig19c
OUT=./boot_gow2_f19_mig3 ./build_macos.sh ./recomp_macos_v3_mig19c
PS3_ENGINE_ROOT=$(pwd) ./games/gow2/verify_lift.sh ../gow2-recomp/recomp_macos_v3_mig19c

# quem ganhou o link
nm -m ../gow2-recomp/boot_gow2_f19_mig3 | grep -E '13func_(0002F3F0|000CE03C|002C0498)P11ppu_context$'
strings -a ../gow2-recomp/boot_gow2_f19_mig3 | grep -c 'CE03C Play aborted'

# in-boot
/tmp/m1_f19_mig_smoke.sh /tmp/m1_fase19_smoke_migracoes.log ./boot_gow2_f19_mig3
```

**Artefactos medidos (fora dos repos, G6):**

| | |
|---|---|
| lift final | `../gow2-recomp/recomp_macos_v3_mig19c`, `sha256(cat ppu_recomp_*.cpp)` = `bc0744bd104cb239b27b6923ead489593797b9034e08fd6bba121769daee1af0` |
| binário | `boot_gow2_f19_mig3`, 121 667 408 B, `sha256 2427ea153022cde131f22e12c24275a6c71c776a0d91c68ef0017b1f42166a19` |
| logs | `/tmp/m1_fase19_smoke_migracoes.log`, `/tmp/m1_f19_verify_lift.log`, `/tmp/m1_f19_mig3_apply.log`, `/tmp/m1_f19_ladder_A.txt` |

---

## Known Stubs

Nenhum. Não há stub, `TODO` nem `skip` introduzido por este plano.

O que fica **declarado e não escondido**, já registado no ledger e no WINDOWS:

- o ramo de abort do pad do CE03C não é exercitado pelo smoke de intro
  (WINDOWS #8, na própria razão de fecho);
- os P0 `classe=OPD` continuam `todo` com `alvo=weak` **por rever** (secção 8);
- `MANIFEST FAIL` e `patch_freelist_guard_262610.py` continuam como estavam
  (WINDOWS #11, dívida da Fase 18).

## Self-Check: PASSED

- 10/10 ficheiros de `key-files.modified` existem em disco e estão nos commits.
- 6/6 commits citados existem: `0690668`, `965b8af`, `304610b` (monorepo);
  `113f3ac`, `1652ab4`, `14024b4` (espelho).
- Contagem do ledger recontada **por script** sobre o ficheiro em disco:
  `CORRECTNESS migrated = 4`, `OPD migrated = 0`, `redundant = 2`.
- Os 4 logs citados existem em `/tmp`, e os números das secções 3–5 foram
  extraídos deles.
- `git diff --name-only 0690668^..HEAD` devolve exactamente os 10 ficheiros —
  zero dados de jogo, zero logs, zero lifts, zero ficheiros da outra sessão.
