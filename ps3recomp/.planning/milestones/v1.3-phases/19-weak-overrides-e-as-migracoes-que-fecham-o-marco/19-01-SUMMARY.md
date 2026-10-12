---
phase: 19-weak-overrides-e-as-migracoes-que-fecham-o-marco
plan: 01
subsystem: tooling
tags: [ppu-lifter, recomp-config, weak-symbols, macho, clang, apple-ld, toml, tdd]

requires:
  - phase: 16-invent-rio-de-patches-e-baseline-medido
    provides: "ledger PATCH_MIGRATION.tsv com os candidatos alvo=weak e EA confirmada"
  - phase: 17-mid-asm-de-ponta-a-ponta
    provides: "padrao de emissao atras de flag + golden por diff; tools/testdata/midasm_golden_noconfig.c; hooks host versionados"
  - phase: 18-jump-tables-detectadas-e-declaradas
    provides: "precedente de medir antes de classificar (redundant nao conta)"
provides:
  - "runtime/ppu/ppc_hooks.h -- PPC_FUNC / PPC_FUNC_IMPL / PPC_FUNC_IMPL_DECL, Clang-first"
  - "ppu_lifter.py --emit-weak-wrappers + [main].emit_weak_wrappers, OFF por default"
  - "emissao do par __imp_func_X (forte) + func_X (fraco) so' para [[functions_override]]"
  - "check_weak_override_coverage: rc=2 para EA que nao e' inicio de funcao liftada"
  - "tools/test_weak_override_symbols.py -- 56 assercoes, com link+nm+execucao reais"
  - "MEDICAO: weak override VIAVEL no ld da Apple (arm64), nos 3 caminhos de chamada e a -O0/-O1/-O2"
affects: [19-02, 19-03, 21-fecho-do-marco]

tech-stack:
  added: []
  patterns:
    - "Macros do header como fonte unica do atributo fraco (o lifter usa-as, nao carimba o attribute)"
    - "Medicao de linkagem por nm -m + execucao real, nao por inspeccao de texto gerado"

key-files:
  created:
    - runtime/ppu/ppc_hooks.h
    - tools/test_weak_override_symbols.py
  modified:
    - tools/ppu_lifter.py
    - tools/recomp_config.py
    - tools/test_recomp_config.py
    - docs/RECOMP_CONFIG.md

key-decisions:
  - "Emitir wrappers SO' para as funcoes declaradas, nunca para todas -- decidido por medicao (ver seccao propria), e o argumento decisivo foi o tempo de execucao a -O0, nao o tamanho"
  - "Flag dupla (CLI --emit-weak-wrappers OU [main].emit_weak_wrappers), ambas default false"
  - "Sem plumbing de weak_override_eas para os workers: a emissao e' do processo principal; provado por -j1 == -j2 byte a byte em vez de copiar o padrao do midasm sem pensar"
  - "FunctionOverride.size fica REGISTADO e nao consumido, documentado como tal"
  - "Recusa (rc=2) so' com a emissao ligada, para nao partir lifts que ja correm com a seccao declarada e ignorada"

patterns-established:
  - "Controlo por mutacao DENTRO da suite (L6): o mesmo par sem o weak tem de falhar o link -- a prova de que o mecanismo e' quem carrega o resultado"
  - "Medir o custo de uma alternativa (--measure-cost) antes de escolher, e escrever o numero"

status: complete
---

# Phase 19 Plano 01: Mecanismo de weak override — Summary

Weak function override do lifter PPU: macros em `runtime/ppu/ppc_hooks.h`, emissão
do par `__imp_func_X` (forte) + `func_X` (fraco) atrás de flag desligada por
omissão, e a **medição** — não a suposição — de que um override host forte ganha
o link no `ld` da Apple em arm64.

**Duração:** ~2h · **Commits:** 4 · **Aceites:** 56 asserções verdes ·
**Evidência:** `offline-unit` (sem in-boot neste plano, por desenho)

---

## O risco central: MEDIDO, e o mecanismo é VIÁVEL

O 19-CONTEXT mandou medir, não assumir: *"se o override não ganhar, isso é o
resultado, e o mecanismo fica declarado inviável em vez de fingido"*. Foi medido
com `clang++` real, `nm` real e **execução** real do binário — sobre o lift
**realmente emitido** pelo lifter, não sobre um toy escrito à mão.

**Veredicto: VIÁVEL.** Apple clang 21.0.0, `arm64-apple-darwin25.5.0`.

### A saída literal do `nm`

```
[weak] nm    (lift.o): wrapper=[('func_00001000(ppu_context*)', 'T')] impl=[('__imp_func_00001000(ppu_context*)', 'T')]
[weak] nm -m (lift.o): wrapper=[('__Z13func_00001000P11ppu_context', '(__TEXT,__text) weak external')] impl=[('__Z19__imp_func_00001000P11ppu_context', '(__TEXT,__text) external')]
[weak] nm(exe com override): [('func_00001000(ppu_context*)', 'T')]
[weak] run(exe com override): 'direct=4242\ntable=4242'
[weak] run(L5): 'via_caller=4242'
[weak] run(L7 -O1): 'via_caller=4242'
[weak] run(L7 -O2): 'via_caller=4242'
```

`4242` é o valor que **só** o override host escreve; o corpo liftado escreveria
`1` (ou `7` no caso L5). Ou seja o override ganhou em **todos** os caminhos.

### A armadilha de leitura que quase deu falso vermelho

**No Mach-O o `nm` simples imprime `T` para uma definição FRACA**, exactamente
como para uma forte — a letra `W` do ELF **nunca aparece**. A fraqueza só se lê
com `nm -m` (`weak external`).

O plano pedia literalmente *"o símbolo fraco existe (`W`/`w`, não `T`)"*. Escrito
a pensar em ELF, esse critério **reprovaria um mecanismo que funciona**. A
primeira execução verde do mecanismo falhou nessa asserção com
`[('func_00001000(ppu_context*)', 'T')]` — e a resolução correcta foi medir o que
o Mach-O realmente regista, não relaxar a asserção. Provado num probe isolado:

```
$ nm -C a.o
000000000000001c T f(C*)                                    <-- FRACO, indistinguível
$ nm -m -C a.o
... (__TEXT,__text) weak external __Z1fP1C
```

Fica documentado em `docs/RECOMP_CONFIG.md` e no docstring de `_nm_macho`, porque
é a diferença entre um aceite honesto e um falso negativo.

### Os seis cenários de link medidos

| # | Cenário | Resultado |
|---|---|---|
| L1 | `nm -m` do `.o` do lift | wrapper `weak external`, corpo `external` (forte), ambos em `__TEXT` |
| L2 | Sem override | corre o corpo liftado (`direct=1`, `table=1`) |
| L3 | Override forte **noutra TU**, chamada directa | **override ganha** (`direct=4242`) |
| L4 | Chamada indirecta via `function_table` (referência na **mesma TU** do wrapper) | **override ganha** (`table=4242`) |
| L5 | Chamada directa a partir de **outra função liftada da mesma TU** | **override ganha** (`via_caller=4242`) |
| L6 | **Controlo:** o mesmo par **sem** `weak` | link **falha** com `duplicate symbol` — como tinha de ser |
| L7 | L5 com o lift compilado a `-O1` e `-O2` | **override continua a ganhar** |

L4 e L5 não estavam no plano e foram acrescentados por análise de risco: são as
referências **da mesma TU**, onde o compilador poderia curto-circuitar o símbolo
fraco para a definição local e produzir um mecanismo meio-funcional — que passaria
nos testes directos e falharia no jogo (a `function_table` é a origem de **toda**
a chamada indirecta do guest). Ambos verdes.

**L7 é a guarda que faltava mais:** o `build_macos.sh` compila os chunks com
`LIFT_OPT` (default `-O0`, mas suporta `-O1`, e o repo tem um `boot_gow2_O1`). Um
mecanismo que funcionasse a `-O0` e falhasse a `-O1` seria a pior armadilha
possível. Medido: não falha.

---

## A medição que o CONTEXT deixou à escolha: todas vs só as declaradas

**Decisão: só as declaradas.** Números primeiro (`--measure-cost`, 2000 funções
sintéticas, mais os totais do lift real do GoW2):

| Métrica | Só declaradas (0) | Todas (2000) | Δ |
|---|---|---|---|
| Fonte | 460 603 B | 560 907 B | **+21,8 %** (+50,2 B/wrapper) |
| Compilação | 0,24 s | 0,39 s | +61,9 % (+75 µs/wrapper) |
| Objecto | 373 736 B | 595 736 B | +59,4 % (+111 B/wrapper) |
| Símbolos (`nm`) | 4 007 | 6 007 | +49,9 % (+1/função) |

As percentagens estão infladas porque as funções sintéticas têm 4 instruções.
Extrapolando o custo **por wrapper** ao lift real — **51 917 funções**, **252,7 MB**
de fonte, **133,8 MB** de objectos (medidos em `../gow2-recomp/recomp_macos_v2`):

| | Extrapolado ao GoW2 real |
|---|---|
| Fonte | +2,6 MB → **+1,0 %** |
| Objecto | +5,8 MB → **+4,3 %** |
| Compilação | ~+3,9 s no total dos 7 chunks |
| Símbolos | **+51 917** (o dobro dos `func_*`) |

**O tamanho não foi o argumento decisivo — o tempo de execução foi.** Os chunks
compilam com `LIFT_OPT=-O0` por omissão, e a `-O0` o wrapper é uma chamada
**real**, verificada no *disassembly*:

```
-O0:  30: 94000000  bl  0x30 <__Z1fP1C+0x14>     <-- frame + bl + epílogo
-O1:  (o wrapper desaparece — inlined)
```

Envolver as 51 917 funções faria **toda** chamada guest pagar um frame host extra
no perfil de build por omissão, para benefício nenhum nas funções que ninguém
substitui. Daí: **só as declaradas**, e lista vazia com a flag ligada emite
**zero** wrappers (com aviso), nunca "todas".

---

## O que foi construído

### `runtime/ppu/ppc_hooks.h`

`PPC_FUNC_IMPL(name)` → `void __imp_##name(ppu_context* ctx)` (forte).
`PPC_FUNC(name)` → `PPC_WEAK void name(ppu_context* ctx)` (fraco no Clang).
`PPC_FUNC_IMPL_DECL(name)` → declaração, para o override chamar o corpo original
(padrão "envolver, não substituir").

- **Clang-first:** `#if defined(__clang__)` → `__attribute__((weak))`,
  `PPC_HOOKS_WEAK_SUPPORTED 1`. Fora do Clang, `PPC_WEAK` fica **vazio**, o
  wrapper sai forte, e um override host dá `duplicate symbol` no link —
  deliberado: falhar alto é muito melhor que um override que não pega. Nenhum
  outro toolchain foi medido e nenhum se assume (GCC *tem* `__attribute__((weak))`,
  mas afirmá-lo sem medir seria a mesma dívida que esta fase existe para não fazer).
- **Não inclui `ppu_context.h`** de propósito: o `ppu_recomp.h` gerado define o
  seu próprio `typedef struct ppu_context`, e incluir os dois seria redefinição.
  O header é só macros; o tipo só é preciso no ponto de expansão.
- **Sem tokens Win32** fora de `#ifdef` (G4 — verificado por `grep`).

### Emissão no `ppu_lifter.py`

```c
PPC_FUNC_IMPL(func_00001000) {
        ctx->gpr[3] = (int64_t)(int32_t)(1);
        ...
}
PPC_FUNC(func_00001000) {
    __imp_func_00001000(ctx);
}
```

O lifter **usa as macros do header** em vez de carimbar `__attribute__((weak))`
no output: o atributo fraco e o prefixo `__imp_` vivem num só sítio, e se header
e lifter divergirem o build parte alto em vez de emitir um wrapper que *parece*
fraco e não é.

- `#include "ppc_hooks.h"` só é emitido quando há wrappers (o caminho de include
  já existe: `-I $PS3/runtime/ppu` no `build_macos.sh`).
- `function_table` e as chamadas directas entre funções liftadas continuam a
  nomear `func_XXXXXXXX` — nunca `__imp_*`.
- **Falha alto:** `check_weak_override_coverage` recusa com **rc=2**, antes de
  escrever ficheiro nenhum, um `address` que não seja o **início** de uma função
  liftada (com mensagem própria para o caso "EA no interior", que é o engano
  provável — confundir com um mid-asm). A recusa só corre com a emissão ligada.
- Três estados impressos, nenhum silencioso: contagem carregada + `ON`/`OFF`;
  aviso se a flag está ligada e a lista vazia; aviso se há overrides declarados
  com a emissão desligada ("declarei e não fez nada" é a surpresa que este repo
  recusa noutros sítios).

### O golden aguenta

Sem a flag, o `.c` gerado é **byte-a-byte** igual a
`tools/testdata/midasm_golden_noconfig.c` — o golden **da Fase 17**, reutilizado
de propósito em vez de criar um segundo ficheiro de referência: prova de uma só
vez que nem a Fase 17 nem a 19 mexeram no caminho por omissão. Três variantes
cobertas: sem `--config`; com `[[functions_override]]` declarado e flag **off**;
flag **on** com lista vazia.

Os TOMLs de produção (`games/gow2/config/gow2_recomp.toml` e o do
`../gow2-recomp`) foram verificados: `emit_weak_wrappers=False`, `overrides=0`.
**O caminho de lift de produção está inalterado.**

---

## Uma dedução que virei medição (e o plumbing que NÃO escrevi)

O plano exigia propagar o conjunto de overrides por
`_worker_init`/`_WORKER_STATE`/`_worker_lift`/`_parallel_lift`, *"exactamente como
o midasm"*, dizendo que o *silent drop* sob `-j` é uma falha de fase.

Ao ler o código, isso é **desnecessário aqui**: o mid-asm é emitido dentro de
`lift_function`, que corre **no worker**; o par `__imp_`/wrapper é emitido em
`_function_def_lines`, que corre no **processo principal** (os workers só
devolvem `body_lines`). Copiar o plumbing seria cargo-cult — código morto a
fingir uma garantia.

Em vez disso implementei o que o requisito realmente **quer**: um teste que
compara os `.c` inteiros de `-j1` e `-j2`, byte a byte, com a emissão ligada.
Isso protege o output (o que interessa) mesmo que a implementação mude de sítio,
e é mais forte do que assertar sobre `_WORKER_STATE`. Segue o precedente da
Fase 18: **medir antes de classificar**, em vez de inflar.

---

## Prova por mutação (obrigatória — 4 mutações, todas discriminativas)

| # | Defeito injectado | Detectado? |
|---|---|---|
| 1 | `PPC_WEAK` sem `__attribute__((weak))` no header | **Sim** — 3 falhas: `L1a` (`external` em vez de `weak external`), `L3a` e `L5a` com `duplicate symbol` |
| 2 | `_weak_on = True` (emissão ignora a flag) | **Sim** — 6 falhas: os 3 goldens, os `zero __imp_func_`, o aviso, e a recusa a disparar com a flag off |
| 3 | `function_table` a apontar para `__imp_` | **Sim** — 6 falhas, incluindo os goldens do caminho por omissão |
| 4 | `emit_weak_wrappers` default `True` no parser | **Sim** — `test_recomp_config.py` falha o `is False` |

Todas revertidas; verde total reconfirmado depois de cada uma.

> **Nota de processo:** a reversão da mutação 1 por `git checkout --` falhou em
> silêncio (`ppc_hooks.h` ainda era untracked nesse momento) e o ficheiro ficou
> mutado. Foi apanhado por reinspecção do ficheiro e restaurado, com nova
> confirmação de verde antes de seguir. Registado porque é precisamente o tipo de
> falso resultado que a metodologia deste repo existe para não deixar passar.

---

## Ordem TDD (RED → GREEN)

| Commit | O quê |
|---|---|
| `6461a77` | **RED** — suite com 20 asserções vermelhas antes de existir código. `L6` (controlo) já passava; `L3a`/`L5a` falhavam com `duplicate symbol`: a linha de base honesta de que sem símbolo fraco não há override |
| `52a15de` | **GREEN** — header, emissão, flag, recusa, parser |
| `391f8bf` | `L7` — guarda de `-O1`/`-O2` |
| `6bd6e68` | Docs (Task 2) |

---

## Aceites do plano

| # | Aceite | Estado |
|---|---|---|
| 1 | `ppc_hooks.h` com macros, Clang-first, ramo não-Clang forte | ✅ |
| 2 | Emissão atrás de flag; corpo → `__imp_`, wrapper fraco chama-o | ✅ |
| 3 | Sem a flag, output byte-a-byte idêntico (golden por `diff`) | ✅ (3 variantes) |
| 4 | `test_weak_override_symbols.py` PASS com prova de `nm` | ✅ 56 asserções |
| — | Worker/parallel carrega o override | ✅ por `-j1 == -j2` (ver secção acima) |
| — | EA órfão falha alto | ✅ rc=2, duas mensagens distintas |

### Verificação (reproduzível)

```bash
.venv/bin/python3 tools/test_weak_override_symbols.py     # 56 ok
.venv/bin/python3 tools/test_recomp_config.py
.venv/bin/python3 tools/test_midasm_emit.py
.venv/bin/python3 tools/test_invalid_instructions.py
test -f runtime/ppu/ppc_hooks.h
```

Não-regressão adicional (todos PASS): `test_switch_tables`, `test_ppu_jumptables`,
`test_ppu_lift`, `test_lift_parity`.

---

## Desvios do plano

1. **[Regra 1 — aceite errado] `nm` `W`/`w` → `nm -m` `weak external`.** O
   critério do plano estava escrito para ELF e reprovaria o mecanismo correcto no
   Mach-O. Medido e corrigido para o que o Mach-O regista. Documentado.
2. **[Regra 2 — funcionalidade crítica em falta] L4, L5 e L7 acrescentados.**
   O plano só pedia "override forte ganha". Faltavam os caminhos que decidem se
   o mecanismo serve o **jogo**: `function_table` (toda a chamada indirecta),
   chamada da mesma TU, e sobrevivência a `-O1`/`-O2` (perfil de build suportado).
3. **[Medição em vez de dedução] plumbing de worker não escrito.** Justificado
   acima; requisito coberto por comparação de output `-j1`/`-j2`.
4. **Aviso extra** quando `--emit-weak-wrappers` é passado sem `--config`.

---

## O que este plano NÃO fez (por desenho)

- **Nenhuma migração** (CE03C / OPD / qualquer `patch_*.py`) — é 19-02/19-03.
- **Nenhum `gow2_func_overrides.cpp`**, nenhum wiring no `build_macos.sh`,
  nenhum smoke in-boot. O `XEN-05` **não** se move com este plano.
- `[optimizations]` continua parseado e ignorado.

## Notas para o 19-02

- O mecanismo está provado **offline**; falta a ponta host (TU de overrides +
  build + smoke). O guard anti-duplicate-symbol do `build_macos.sh` para os
  overrides tem a mesma armadilha que o do mid-asm (o preâmbulo emite
  *declarações* que um `grep` ingénuo confunde com *definições*) — mas agora com
  um agravante próprio: as definições passam a estar dentro de `PPC_FUNC(...)`,
  não de `void func_...(`, portanto um guard copiado do mid-asm **não as vê**.
- Escrever o override com **linkage C++, sem `extern "C"`**. Com `extern "C"` o
  símbolo tem outro nome, o wrapper fraco continua a ganhar, e o override é um
  no-op **silencioso** — o pior resultado possível.
- Ligar via `[main].emit_weak_wrappers = true` no `gow2_recomp.toml` mantém o
  `RELIFT` existente a funcionar sem mudar a linha de comando.

## Known Stubs

Nenhum. Não há stub, TODO nem `skip` introduzido por este plano.

## Self-Check: PASSED

- 7/7 ficheiros declarados existem em disco.
- 4/4 commits citados existem em `git log`.
- `tools/test_weak_override_symbols.py`: **56 asserções ok**, saída 0.
- `git diff --name-only 6461a77^..HEAD` devolve exactamente os 6 ficheiros do
  `files_modified` do plano — zero dados de jogo, zero ficheiros da outra sessão
  activa (`.planning/PROJECT.md`, `MILESTONES.md`, `ROADMAP.md`, `CLAUDE.md`,
  `games/gow2/CLAUDE.md`, `config.json`, `scripts/gsd_plan*.sh` ficaram
  modificados na árvore e **fora** de todos os commits).
