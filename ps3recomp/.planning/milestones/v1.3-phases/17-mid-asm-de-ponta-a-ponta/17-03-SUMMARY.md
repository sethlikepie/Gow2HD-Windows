---
phase: 17-mid-asm-de-ponta-a-ponta
plan: 03
subsystem: recomp-tooling
tags: [midasm, ppu_lifter, toml, relift, ce03c, ledger, contracts, macos-arm64, gow2, smoke]

requires:
  - phase: 17-mid-asm-de-ponta-a-ponta
    plan: 01
    provides: "emissao de gow2_midasm_<Name>(ctx) a partir de [[midasm_hook]] atras de --config, com golden de nao-regressao e recusas rc=2"
  - phase: 17-mid-asm-de-ponta-a-ponta
    plan: 02
    provides: "hooks host versionados, guard anti-duplicate-symbol que casa com a DEFINICAO e nao com o prototipo, e o simbolo a resolver no boot_gow2 (nm -> T)"
  - phase: 16-invent-rio-de-patches-e-baseline-medido
    provides: "ledger PATCH_MIGRATION.tsv com o CE03C P0/alvo=midasm e o baseline de smoke (st620_max=11, OOB=0)"
provides:
  - "[[midasm_hook]] Ce03cWaitIdle @ 0x000CE03C em games/gow2/config/gow2_recomp.toml"
  - "corpo real do wait-idle em games/gow2/hooks/gow2_midasm_hooks.cpp (deixou de ser no-op)"
  - "PROVA DE RE-LIFT: lift com --config e SEM correr patch nenhum contem gow2_midasm_Ce03cWaitIdle(ctx) dentro de func_000CE03C"
  - "PROVA DE EXECUCAO: objdump mostra bl _gow2_midasm_Ce03cWaitIdle como 1a instrucao de func_000CE03C; smoke in-boot imprime o marcador vindo do hook host"
  - "politica A nos dois patch_*.py do CE03C: MIDASM/skip num lift com --config, comportamento intacto num lift antigo"
  - "build_macos.sh: RELIFT=1 passa --config (PS3_RECOMP_CONFIG sobrepoe)"
  - "ledger: CE03C estado=migrated; patch_fallthrough_2550c8.py alvo=midasm -> alvo=lifter com razao escrita"
  - "dominio da coluna alvo alargado com o valor lifter"
  - "CONTRACTS.tsv: 2 contratos executaveis do caminho [INTROSEQ]/CE03C, verdes nos DOIS mecanismos"
affects: [18 switch tables, 19 weak overrides (pad de setjmp), 21 metrica re-lift-safe / XEN-05]

tech-stack:
  added: []
  patterns:
    - "Agulha de contrato em ALTERNATIVA regex: um so' contrato cobre o mecanismo antigo e o novo, e nao fica vermelho durante a transicao"
    - "Patch legado que RECONHECE o mecanismo que o substituiu (MIDASM/skip) em vez de ser apagado -- producao mistura lifts antigos e novos"
    - "Razao de reclassificacao guardada em constante Python (NOTAS_ALVO) porque render_tsv reconstroi o cabecalho inteiro a cada regen"

key-files:
  created:
    - .planning/phases/17-mid-asm-de-ponta-a-ponta/17-03-SUMMARY.md
  modified:
    - games/gow2/config/gow2_recomp.toml
    - games/gow2/hooks/gow2_midasm_hooks.cpp
    - games/gow2/hooks/gow2_midasm_hooks.h
    - games/gow2/build_macos.sh
    - games/gow2/recomp_mid_v2/patch_ce03c_introseq_block.py
    - games/gow2/recomp_mid_v2/patch_ce03c_movie_done_reset.py
    - games/gow2/lift_baseline/PATCH_MIGRATION.tsv
    - games/gow2/lift_baseline/CONTRACTS.tsv
    - games/gow2/lift_baseline/gen_migration_ledger.py
    - docs/RECOMP_CONFIG.md
    - .planning/REQUIREMENTS.md
    - .planning/WINDOWS.md

key-decisions:
  - "Politica A (patch reconhece o mid-asm e salta) e nao remocao: a producao ainda corre lifts SEM --config, e esses continuam a precisar do texto injectado. Apagar o script partia a producao para provar um ponto"
  - "switch_table_file COMENTADO no TOML: com a chave activa o lifter aborta em 'switch table file not found' e o --config inteiro morre -- medido, foi assim que o primeiro re-lift falhou"
  - "usleep em vez de std::this_thread::sleep_for: <thread>/<chrono> arrastam <atomic>, que o libc++ recusa depois do <stdatomic.h> do ppu_memory.h antes de C++23 -- erro de compilacao medido, nao suposto"
  - "r30 salvo/reposto, r2 NAO: e' exactamente o que o bloco injectado fazia. Divergir seria mudar o comportamento do fix medido a pretexto de o migrar"
  - "O pad de setjmp/longjmp NAO foi migrado e nao se afirma paridade de 143 linhas: um mid-asm corre ao lado de uma instrucao, nao envolve a funcao"

requirements-completed: [XEN-02]

coverage:
  - id: F1
    description: "[[midasm_hook]] do CE03C declarado no TOML e re-lift com --config, SEM correr o patch, contem gow2_midasm_Ce03cWaitIdle(ctx) dentro de func_000CE03C"
    requirement: XEN-01
    verification:
      - kind: integration
        ref: "grep -n 'gow2_midasm_Ce03cWaitIdle' recomp_macos_v3_midasm/ppu_recomp_*.cpp -> ppu_recomp_002.cpp:19537 dentro de func_000CE03C; grep 'CE03C wait-idle 1st movie' no lift = 0 (o patch nao correu)"
        status: pass
    human_judgment: false
  - id: F2
    description: "O hook nao so' liga: e' CHAMADO pelo codigo liftado"
    requirement: XEN-02
    verification:
      - kind: integration
        ref: "nm -u recomp_macos_v3_midasm/ppu_recomp_002.cpp.o -> _gow2_midasm_Ce03cWaitIdle; objdump de func_000CE03C -> 'bl 0x1068cd9a8 <_gow2_midasm_Ce03cWaitIdle>' na 1a instrucao do corpo"
        status: pass
    human_judgment: false
  - id: F3
    description: "Patches legados nao reinjectam o bloco por cima do mid-asm, e continuam a funcionar em lifts antigos"
    requirement: XEN-02
    verification:
      - kind: unit
        ref: "4 casos discriminadores com sha256 antes/depois: MIDASM skip (nao escreve) / APPLIED em lift limpo / ALREADY em producao / rc=2 com corpo divergente"
        status: pass
      - kind: integration
        ref: "apply_all_patches.sh sobre o lift midasm -> ALREADY-APPLIED nos dois (era UNVERIFIED + FAILED antes desta fase)"
        status: pass
    human_judgment: false
  - id: F4
    description: "Smoke intro sobre binario reconstruido do lift com --config nao regride os pisos"
    requirement: XEN-02
    verification:
      - kind: integration
        ref: "/tmp/m1_fase17_smoke_ce03c.log -- st620_max=11 (piso >=3, baseline 11), oob_or_ffff_hits=0 (baseline 0), orfaos=0, kill por PID [in-boot]"
        status: pass
    human_judgment: false
  - id: F5
    description: "Evidencia in-boot de que o hook CORRE: o marcador do aceite do ledger aparece no log e so' pode vir do host"
    requirement: XEN-02
    verification:
      - kind: integration
        ref: "grep '[INTROSEQ] CE03C wait-idle 1st movie' no log = 1, com o lift a ter ZERO ocorrencias da string; perfil de marcadores CE03C identico ao da producao (7/7) [in-boot]"
        status: pass
    human_judgment: false
  - id: F6
    description: "Ledger e contratos reflectem a migracao; 2550C8 sai de midasm com razao escrita"
    verification:
      - kind: unit
        ref: "test_gen_migration_ledger.py 8/8 PASS; test_check_contracts.py 7/7 PASS"
        status: pass
      - kind: integration
        ref: "check_contracts.py verde para os dois patches contra recomp_macos_v2 (antigo) E recomp_macos_v3_midasm (novo)"
        status: pass
    human_judgment: false
  - id: F7
    description: "docs/RECOMP_CONFIG.md descreve o mid-asm realmente consumido, com exemplo end-to-end"
    requirement: XEN-06
    verification: []
    human_judgment: true
    rationale: "Correspondencia doc<->consumidor e' julgamento editorial. O que e' verificavel (os comandos do exemplo) foi corrido nesta sessao e a saida esta' copiada verbatim."

duration: ~80min
completed: 2026-08-02
status: complete
---

# Fase 17 Plano 03: o piloto CE03C migrado para mid-asm — Summary

**Re-liftei o GoW2 com `--config` e **sem** correr o `patch_ce03c_introseq_block.py`, e o
`.cpp` gerado tem `gow2_midasm_Ce03cWaitIdle(ctx);` como primeira linha de
`func_000CE03C` — e o `objdump` do binário construído a partir desse lift mostra o `bl`
para o símbolo host na primeira instrução do corpo, com o marcador `[INTROSEQ] CE03C
wait-idle 1st movie` a sair no log in-boot vindo do **hook host**, porque o lift tem zero
ocorrências dessa string.**

## Performance

- **Duração:** ~80 min (inclui re-lift 12 s, `apply_all_patches` ×2, build completo e smoke)
- **Tasks:** 3/3
- **Ficheiros:** 1 criado, 12 modificados (+ 4 espelhados no `../gow2-recomp`)
- **Níveis de evidência:** `offline-unit` (re-lift, `grep`, `nm`, `objdump`, suites) e
  `in-boot` (smoke de 25 s, kill por PID)

## Os cinco aceites — estado medido

| # | Aceite | Estado |
|---|---|---|
| 1 | `[[midasm_hook]]` no TOML; re-lift **sem** o patch e o `.cpp` contém a chamada | **Cumprido** — saída literal abaixo |
| 2 | Patch legado passa a no-op/skip quando o mid-asm é a fonte de verdade | **Cumprido** — política A, 4 ramos exercitados com `sha256` antes/depois |
| 3 | Ledger `estado=migrated`; 2550C8 sai de `alvo=midasm` com razão escrita | **Cumprido** — `alvo=lifter`, razão no cabeçalho e sobrevive ao regen |
| 4 | `CONTRACTS.tsv` ganha contrato executável para o marcador `[INTROSEQ]` | **Cumprido** — 2 contratos, verdes nos **dois** mecanismos |
| 5 | Smoke: `st620_max ≥ 3`, `OOB/0xFFFF = 0` | **Cumprido** — `11` e `0`; e o `grep` do marcador **apareceu** (in-boot) |

---

## 1. O TOML final

`games/gow2/config/gow2_recomp.toml` (a secção, sem os comentários que a explicam):

```toml
[[midasm_hook]]
name = "Ce03cWaitIdle"
address = 0x000CE03C
```

Sem `after_instruction` (default: **antes** da instrução — a chamada sai no topo do corpo)
e sem `return` (o corpo natural do guest **tem** de continuar). `jump_address` não é usado:
o 17-01 recusa-o com `rc=2`.

**Uma alteração ao `[main]` que não estava no plano e sem a qual nada disto corre:**
`switch_table_file = "gow2_switch_tables.toml"` foi **comentado**. Com a chave activa o
lifter aborta logo no arranque:

```
Error: .../config/gow2_recomp.toml -> gow2_switch_tables.toml: switch table file not found
  Config: 1 midasm_hook(s) loaded from config/gow2_recomp.toml
EXIT=1
```

O ficheiro só nasce na Fase 18. Enquanto a chave lá estiver, o `--config` inteiro é
inutilizável — foi assim que o primeiro re-lift desta sessão morreu. Descomentar no mesmo
commit que criar o ficheiro (escrito no próprio TOML e em `docs/RECOMP_CONFIG.md`).

## 2. O comando exacto do re-lift

Script em `/tmp/m1_f17_relift_midasm.sh` (fora dos repos, G6), com guarda que aborta se o
alvo resolver para `recomp_macos_v2` (produção), `recomp_macos_v3` (lift de outra sessão)
ou `recomp_macos`:

```bash
cd /Users/.../gow2-recomp
/Users/.../ps3recomp/.venv/bin/python3 /Users/.../ps3recomp/tools/ppu_lifter.py \
    EBOOT.ELF \
    --functions functions.json \
    --config config/gow2_recomp.toml \
    -o /Users/.../gow2-recomp/recomp_macos_v3_midasm \
    -j 6
```

```
  Config: 0 invalid_instructions pattern(s) loaded from config/gow2_recomp.toml
  Config: 1 midasm_hook(s) loaded from config/gow2_recomp.toml
  ...
  51991 functions lifted
real    0m11.928s
RELIFT_RC=0
```

O directório é **novo**: `recomp_macos_v3_midasm`. A produção (`recomp_macos_v2`) e o
`recomp_macos_v3` de outra sessão **não foram tocados**. Promoção não é desta fase.

## 3. A prova que define a fase (saída literal)

`patch_ce03c_introseq_block.py` **não** correu sobre este lift.

```
$ grep -n 'gow2_midasm_Ce03cWaitIdle' recomp_macos_v3_midasm/ppu_recomp_*.cpp
recomp_macos_v3_midasm/ppu_recomp_002.cpp:295:void gow2_midasm_Ce03cWaitIdle(ppu_context* ctx);
recomp_macos_v3_midasm/ppu_recomp_002.cpp:19537:        gow2_midasm_Ce03cWaitIdle(ctx);
recomp_macos_v3_midasm/ppu_recomp_001.cpp:295:void gow2_midasm_Ce03cWaitIdle(ppu_context* ctx);
recomp_macos_v3_midasm/ppu_recomp_000.cpp:295:void gow2_midasm_Ce03cWaitIdle(ppu_context* ctx);
recomp_macos_v3_midasm/ppu_recomp_000.cpp:167034:        gow2_midasm_Ce03cWaitIdle(ctx);
recomp_macos_v3_midasm/ppu_recomp_003.cpp:295:void gow2_midasm_Ce03cWaitIdle(ppu_context* ctx);
recomp_macos_v3_midasm/ppu_recomp_006.cpp:295:void gow2_midasm_Ce03cWaitIdle(ppu_context* ctx);
recomp_macos_v3_midasm/ppu_recomp_005.cpp:295:void gow2_midasm_Ce03cWaitIdle(ppu_context* ctx);
recomp_macos_v3_midasm/ppu_recomp_004.cpp:295:void gow2_midasm_Ce03cWaitIdle(ppu_context* ctx);
```

A linha `:295` de cada chunk é a **declaração** do preâmbulo (17-01). A chamada está em
`ppu_recomp_002.cpp:19537`, e o contexto mostra que é a primeira linha do corpo:

```c
void func_000CE03C(ppu_context* ctx) {
        gow2_midasm_Ce03cWaitIdle(ctx);
        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x5E44);
        ...
```

E o marcador do bloco injectado **não existe** neste lift:

```
$ grep -c 'CE03C wait-idle 1st movie' recomp_macos_v3_midasm/ppu_recomp_*.cpp
(zero em todos os chunks)
```

### Achado: a chamada saiu DUAS vezes, e importa dizer porquê

O `grep` mostra também `ppu_recomp_000.cpp:167034`. Não é um bug de emissão: o EA
`0x000CE03C` cai dentro de **duas** funções liftadas — `func_000CE03C` (a certa) e
`func_000CE02C`, cujo intervalo atravessa o mesmo endereço. Um `[[midasm_hook]]` é emitido
em **todas** as funções que cobrem o EA, tal como a própria instrução.

Na segunda, a chamada cai **depois** de um `{ g_trampoline_fn = ...; return; }`
incondicional — é código morto e nunca corre. **Verificado por leitura do `.c` gerado, não
assumido.** Mas é uma propriedade a vigiar: se houvesse um `loc_000CE03C:` nesse ponto, o
hook seria alcançável num contexto que ninguém declarou. A guarda de cobertura do 17-01
verifica que o EA **existe**, não que corresponde a **uma só** função. Registado em
`WINDOWS.md` #9 e em `docs/RECOMP_CONFIG.md`.

## 4. Que o hook CORRE, e não só liga (o buraco que o 17-02 deixou aberto)

O 17-02 escreveu, com todas as letras, que o `T` do `nm` prova que o símbolo resolve, **não**
que corre. É este plano que fecha isso, em três níveis:

```
$ nm -u recomp_macos_v3_midasm/ppu_recomp_002.cpp.o | grep gow2_midasm
_gow2_midasm_Ce03cWaitIdle
```

O objecto do **lift** referencia o símbolo — ou seja, a chamada vem do código liftado, não
de outro sítio qualquer. Nenhum outro chunk o referencia.

```
$ objdump -d --disassemble-symbols=__Z13func_000CE03CP11ppu_context boot_gow2_f17_midasm
0000000102302e6c <__Z13func_000CE03CP11ppu_context>:
102302e6c: d10103ff     sub  sp, sp, #0x40
102302e70: a9037bfd     stp  x29, x30, [sp, #0x30]
102302e74: 9100c3fd     add  x29, sp, #0x30
102302e78: f81f83a0     stur x0, [x29, #-0x8]
102302e7c: f85f83a0     ldur x0, [x29, #-0x8]
102302e80: 95172aca     bl   0x1068cd9a8 <_gow2_midasm_Ce03cWaitIdle>
102302e84: f85f83a8     ldur x8, [x29, #-0x8]
...
```

`bl _gow2_midasm_Ce03cWaitIdle` é a **primeira instrução** do corpo, a seguir ao prólogo.

```
$ nm boot_gow2_f17_midasm | grep gow2_midasm
00000001068cd9a8 T _gow2_midasm_Ce03cWaitIdle
$ nm -u boot_gow2_f17_midasm | grep midasm
(vazio)
```

E o terceiro nível é in-boot — secção 7.

## 5. O destino dos patches legados, e porquê

### `patch_ce03c_introseq_block.py` → **política A** (reconhece o mid-asm e salta)

Não foi apagado. A razão é operacional, não sentimental: **a produção ainda corre lifts
antigos**, gerados sem `--config`, e esses continuam a precisar do texto injectado. Um
script apagado partiria a produção para provar um ponto. O que mudou é que ele passou a
**distinguir** os dois mundos, procurando a chamada mid-asm **dentro da região de
`func_000CE03C`** — nunca no ficheiro inteiro, porque o lifter escreve a declaração no
preâmbulo de **todas** as TUs e um `in t` global daria MIDASM em chunks que nem sequer têm
a função.

Quatro ramos exercitados, com `sha256` do ficheiro antes e depois de cada corrida:

| Caso | Lift | Resultado | Escreveu? |
|---|---|---|---|
| A | `recomp_macos_v3_midasm` (com `--config`) | `MIDASM (hook emitido pelo lifter -- nada a injectar)`, rc=0 | **NÃO** |
| B | mesmo lift com a linha do hook removida (lift "antigo") | `APPLIED (139 linhas)`, rc=0 | SIM |
| C | `recomp_macos_v2` (produção, já com o bloco) | `ALREADY`, rc=0 | **NÃO** |
| D | caso B com o corpo gerado alterado | `ERRO: ... o corpo NAO e' o gerado esperado`, **rc=2** | **NÃO** |

O caso D é o que interessa preservar: **o contrato de recusa continua intacto**. O script
não adivinha quando o lifter muda.

### `patch_ce03c_movie_done_reset.py` → mesma política, e não por simetria

Este não estava no plano como alteração de código, mas **a migração partiu-o**. Medido, em
`/tmp/m1_f17_apply_patches.log`, antes de qualquer fix:

```
FAILED           patch_ce03c_movie_done_reset.py
                   | ERRO: ancora ausente em todos os ficheiros.
                   |   O bloco movie_vt_clear_overlay_done() do CE03C nao existe neste lift.
```

A âncora dele é uma linha **de dentro do bloco injectado**. Num lift com `--config` esse
bloco nunca existe, e o script falhava com `rc=2` — mas a chamada
`movie_done_timebased_reset()` **está lá**, vinda do corpo do hook host. Um FAILED seria uma
mentira sobre o estado do lift. Passou a devolver `MIDASM`/rc=0. Em lifts antigos não muda
um bit.

Depois do fix, `apply_all_patches.sh` sobre o lift mid-asm:

```
ALREADY-APPLIED  patch_ce03c_introseq_block.py
ALREADY-APPLIED  patch_ce03c_movie_done_reset.py
```

(era `UNVERIFIED` + `FAILED` antes desta fase — os contratos novos é que os põem verdes).

### O que **não** foi migrado, e não é dado por migrado

O bloco injectado não era só o prólogo. Envolvia também o **corpo natural** num pad de
`setjmp`/`longjmp` (`g_ce03c_play_abort`), com `cellVdec_stop_all_for_play_abort()` e a
reconstrução da freelist do media object no ramo de abort. **Um `[[midasm_hook]]` corre AO
LADO de uma instrução; não ENVOLVE a função.** Não cabe no mecanismo — é weak override,
Fase 19.

Consequência honesta: **num lift feito com `--config` esse pad não existe**, porque o patch
legado passa a MIDASM/skip e mais ninguém o repõe. **Não se afirma paridade com as 143
linhas do lift de produção.** Escrito no `.cpp`, no `.h`, no TOML, no docstring do patch e
registado como `stub` aberto em `WINDOWS.md` #8.

No smoke de 25 s o pad não foi exercitado (o ramo de abort só dispara com o FO residual
depois do StartSeq#2, que não acontece nesta janela) — por isso este smoke **não** é prova
de que a ausência do pad seja inócua. É prova de que a parte migrada se comporta igual.

## 6. Ledger, contratos e a reclassificação do 2550C8

### `PATCH_MIGRATION.tsv`

```
patch_ce03c_introseq_block.py     CORRECTNESS  midasm  0x000CE03C  P0  grep:[INTROSEQ] CE03C wait-idle 1st movie  migrated
patch_ce03c_movie_done_reset.py   CORRECTNESS  midasm  0x000CE03C  P1  grep:movie_done_timebased_reset            migrated
patch_fallthrough_2550c8.py       CORRECTNESS  lifter  0x002550C8  P0  smoke:bytes_read=20169344                  todo
```

O `patch_fallthrough_2550c8.py` **não foi migrado** — foi **reclassificado**. A causa é um
fallthrough cross-fragment errado do lifter (`func_002550C8` caía em `func_002550E8` em vez
de `func_00255178`): defeito de **tradução**, não lacuna de comportamento. Envolvê-lo num
hook esconderia um bug do lifter atrás de código host.

Para isso o domínio da coluna `alvo` foi **alargado com o valor `lifter`**. Faltava
vocabulário — foi por falta dele que a Fase 16 o classificou como `midasm`. A razão vive na
constante `NOTAS_ALVO` de `gen_migration_ledger.py`, e não numa linha `#` escrita à mão,
porque `render_tsv` reconstrói o cabeçalho inteiro a cada `--merge`.

Continua `estado=todo` e **P0**: a reclassificação corrige o mecanismo, não declara o
trabalho feito.

A linha do cabeçalho é **derivada** e corrigiu-se sozinha:

```
# piloto sugerido Fase 17 (mid-asm): patch_ce03c_introseq_block.py
```

### `CONTRACTS.tsv`

Dois contratos novos, com a agulha em **alternativa regex**:

```
midasm-ce03c-waitidle   patch_ce03c_introseq_block.py    ea  0x000CE03C  count_ge
    re:\[INTROSEQ\] CE03C wait-idle 1st movie|gow2_midasm_Ce03cWaitIdle\(ctx\);   1
midasm-ce03c-donereset  patch_ce03c_movie_done_reset.py  ea  0x000CE03C  count_ge
    re:movie_done_timebased_reset\(\);|gow2_midasm_Ce03cWaitIdle\(ctx\);          1
```

A alternativa não é preguiça — é a única forma de o gate não ficar vermelho durante a
transição. Um contrato que exigisse só o marcador injectado ficava vermelho em todo o lift
com `--config`; um que exigisse só a chamada mid-asm ficava vermelho na produção, que ainda
é antiga. O que se verifica é que a região de `func_000CE03C` tem o fix por **um** dos dois
mecanismos — nunca por nenhum.

Verdes nos dois lifts, medido:

```
$ check_contracts.py CONTRACTS.tsv ../gow2-recomp/recomp_macos_v2 --patch patch_ce03c_introseq_block.py
[PASS] midasm-ce03c-waitidle: ... esperado 1 encontrado 1   -> rc=0
$ check_contracts.py CONTRACTS.tsv ../gow2-recomp/recomp_macos_v3_midasm --patch patch_ce03c_introseq_block.py
[PASS] midasm-ce03c-waitidle: ... esperado 1 encontrado 1   -> rc=0
```

(idem para `patch_ce03c_movie_done_reset.py`, rc=0 nos dois).

## 7. O smoke — números e nível de evidência

**Binário reconstruído a partir do lift com `--config`** (desvio consciente, ver secção 9):
`boot_gow2_f17_midasm`, 121 666 728 bytes,
`sha256 c32ebfac49727cd1e3ee1f8d821797b1feda0d4ade16581621f6f1e4f29bf0a7`.
Lift `recomp_macos_v3_midasm`, `sha256` do `cat ppu_recomp_*.cpp` =
`5a5d9c9697f8d861c39300356e7f41c6d9fc126a1e8040e98be0aa4facfff23e`.

Recipe idêntico ao baseline da Fase 16 e ao smoke do 17-02 (`env_gow2.sh` + `PS3_NO_RSX=1`
`PS3_RSX_BACKEND=trace` `PS3_PERF_FSM=1` `PS3_MOVIE_EOS=0`; `PS3_TRACE_*` limpos; probes OFF),
`PS3_ENGINE_ROOT` explícito, 25 s, **kill por PID** (`TERM`, 1 s, `-9`) — nunca `pkill -f`.
Script `/tmp/m1_fase17_smoke_ce03c.sh`, log `/tmp/m1_fase17_smoke_ce03c.log`, ambos fora dos
repositórios. **`sticky` não foi usado como critério** (não-observável, medido na Fase 16).

```
st620_max=11            (piso >=3; baseline Fase 16 = 11)
oob_or_ffff_hits=0      (baseline Fase 16 = 0)
lifted_functions=52142
sequencia_st620=0 1 3 3 3 11 11
linhas_log=3838  bytes=248434
orfaos_pos_run=0
introseq_ce03c_1st=1
```

### A evidência in-boot de que o hook corre

```
$ grep -n 'INTROSEQ.*CE03C' /tmp/m1_fase17_smoke_ce03c.log | head -5
3798:[INTROSEQ] CE03C wait-idle 1st movie st620=1
3807:[INTROSEQ] CE03C wait tick st620=3 i=0
3812:[INTROSEQ] CE03C wait tick st620=3 i=40
3815:[INTROSEQ] CE03C wait tick st620=3 i=80
3819:[INTROSEQ] CE03C wait tick st620=3 i=120
```

**Esta linha só pode vir do hook host.** O lift tem **zero** ocorrências da string
`CE03C wait-idle 1st movie` (secção 3). É o marcador que o ledger usa como aceite
executável do `patch_ce03c_introseq_block.py` — e continua a aparecer depois de o patch
deixar de o escrever.

### Comparação com a produção (bloco injectado) — perfil idêntico

| Marcador | 17-02 (produção, texto injectado) | 17-03 (v3_midasm, hook host) |
|---|---|---|
| `CE03C wait-idle 1st movie` | 1 | 1 |
| `CE03C wait tick st620` | 5 | 5 |
| `CE03C arm +0x714 st` | 1 | 1 |
| **total linhas CE03C** | **7** | **7** |

As últimas 6 linhas dos dois logs são iguais linha a linha, incluindo
`[MOVIEFSM] st620 11 -> 11 ... overlay_done=1` e `[CONTENT] boot logo queue DONE`.

**Nível de evidência:** `in-boot`, **uma** corrida. Não se afirma estabilidade estatística.

### A ressalva honesta desta comparação

O baseline (17-02) é um binário do lift de **produção**; este é de um lift **novo**. São
lifts diferentes: 52 142 funções registadas no boot contra 52 068 (contador **in-boot**, o
mesmo dos dois logs; o lifter, por sua vez, reportou `51991 functions lifted`, que é outro
contador e não deve ser comparado com estes), e o `apply_all_patches` sobre o
lift novo tem 7 FAILED (`patch_1856a8_stream_opd`, `patch_24e3d0_null_product_gate`,
`patch_b71_cb56c_reuse_block`, `patch_b71_skip_icallb_reuse`, `patch_icg_ctor_opd`, mais
dois PROBE fora do gate) que a produção não tem. Portanto os números **coincidirem** é um
sinal forte de não-regressão, mas não é um A/B limpo do hook isolado. O A/B limpo exigia um
segundo lift do mesmo commit **sem** `--config`; não foi corrido, e não se finge que foi.

O que **é** isolado e limpo é o perfil de marcadores CE03C: 7/7 idênticos, com o texto a
vir de sítios diferentes.

## 8. O guard do `build_macos.sh` — a verificação que o plano pediu

O guard do 17-02 foi desenhado para não confundir a **declaração** que o lifter emite no
preâmbulo com uma **definição**. Este era o primeiro lift com `--config` da história do
projecto, ou seja, o primeiro em que a armadilha existe de facto. Verificado antes do build,
com as duas expressões exactas do script:

```
GUARD NAO DISPARA -> gow2_midasm_hooks e' compilado (correcto)
```

E no build real:

```
=== 2. runtime PPU sources -> .o (HOST_OPT=-O0) ===
  host_gow2_factory: definicoes ja' no lift -- nao compilar (evita duplicate symbol)
  host_gow2_f2b: definicao (static ou extern) ja' no lift -- nao compilar (...)
  gow2_midasm_hooks: compilado
...
BUILD_RC=0
```

**O guard não tem defeito.** As duas expressões exigem `{` (ou fim-de-linha) depois do `)`,
e `[^;]*` dentro dos parênteses; a declaração `void gow2_midasm_Ce03cWaitIdle(ppu_context*
ctx);` e a chamada `gow2_midasm_Ce03cWaitIdle(ctx);` acabam ambas em `;` e não casam.

## 9. Deviations from Plan

### Rule 3 — bloqueio removido

**1. `switch_table_file` comentado no `[main]`**
- **Onde:** Task 2, primeira tentativa de re-lift.
- **Problema:** o lifter aborta com `switch table file not found` **antes** de emitir seja o
  que for. O ficheiro só existe na Fase 18. Com a chave activa, `--config` é inutilizável e
  a prova que define a fase é impossível.
- **Fix:** chave comentada, com a mensagem de erro medida escrita ao lado e a instrução de
  descomentar no mesmo commit que criar o ficheiro. Documentado em `RECOMP_CONFIG.md`.
- **Commit:** `405a2f2`.

**2. `patch_ce03c_movie_done_reset.py` ensinado a reconhecer o mid-asm**
- **Onde:** Task 2, depois do primeiro `apply_all_patches` sobre o lift novo.
- **Problema:** `FAILED` com `rc=2` num lift mid-asm — a âncora dele vive dentro do bloco
  injectado. Regressão causada **pela nossa** migração.
- **Fix:** mesma política A. `MIDASM`/rc=0 num lift com `--config`; intacto nos antigos.
- **Verificação:** 3 fixtures + `ALREADY-APPLIED` no `apply_all_patches`.
- **Commit:** `405a2f2`.

### Rule 2 — funcionalidade crítica em falta

**3. `RELIFT=1` do `build_macos.sh` passa `--config`**
- **Onde:** Task 2.
- **Problema:** o bloco `RELIFT=1` chamava o lifter **sem** `--config`. Um re-lift pelo
  caminho oficial produzia um lift **sem** os hooks, e o build seguinte compilava e ligava
  um `gow2_midasm_hooks.o` que ninguém chama — o piloto pareceria migrado sem o estar, em
  silêncio. É exactamente a classe de defeito que esta fase existe para eliminar.
- **Fix:** `--config $HERE/config/gow2_recomp.toml`, com `PS3_RECOMP_CONFIG` para sobrepor e
  ficheiro ausente = comportamento legado (mensagem explícita nos dois ramos).
- **Commit:** `405a2f2`.

### Desvios de forma, declarados

**4. `usleep` em vez de `std::this_thread::sleep_for`**
Escrevi primeiro a versão portátil. **Não compila** neste TU: `ppu_memory.h` inclui
`<stdatomic.h>` e o libc++ recusa `<atomic>` (que `<thread>`/`<chrono>` arrastam) antes de
C++23 — `error: <atomic> is incompatible with <stdatomic.h> before C++23`. Voltei ao
`usleep` do bloco original, com a razão medida escrita no ficheiro. Hoje só o
`build_macos.sh` compila este `.cpp`; se alguém o acrescentar ao build do Windows, o erro
será alto e imediato.

**5. O `grep` do G4 deixou de sair vazio — e está correcto**
`grep -nE 'Sleep\(|intrin\.h|__declspec|GetModuleHandleA|windows\.h' games/gow2/hooks/*`
passa a devolver uma linha:

```
gow2_midasm_hooks.cpp:88:extern "C" __declspec(thread) void (*g_trampoline_fn)(void*);
```

Está dentro de `#if defined(_MSC_VER)` e é **copiada verbatim** do preâmbulo que o
`ppu_lifter.py` emite e da definição em `runtime/ppu/ppu_loader.cpp` — as três têm de casar
ou o link parte. Removê-la para o `grep` sair limpo partiria o MSVC, que é precisamente o
que o G4 protege. Fica declarada em vez de escondida.

**6. Binário reconstruído e smoke sobre lift novo (desvio consciente, autorizado no brief)**
O plano permitia declarar o rebuild como desvio. Foi feito: `apply_all_patches` + build
completo + smoke sobre `recomp_macos_v3_midasm`, para `OUT=boot_gow2_f17_midasm` — **o
`boot_gow2` de produção não foi tocado**. Sem isto, o aceite 5 seria não-exercitado e a
prova de execução ficaria em `nm`/`objdump` (offline). A ressalva de comparabilidade está na
secção 7.

**Total:** 2 auto-fixes Rule 3, 1 Rule 2, 3 desvios de forma declarados. Nenhum alarga o
âmbito: os três primeiros são condições para o plano poder ser executado de todo.

## 10. Técnicas proibidas — nenhuma foi usada

Nenhum CRC bypass, stub de combination, magic estampado à mão, null-check cosmético ou guard
mascarado. O `PS3_SHADER_DEMO` não foi tocado. O hook chama o **código guest real**
(`func_002C0508`) para bombear o movie player, como o bloco original — não simula o que ele
faz. As três guardas de EA (`>= 0x10000 && < 0x4F000000`) são as do bloco original, não
foram alargadas.

## 11. Fronteira monorepo ↔ checkout de build (G8)

O monorepo é a fonte de verdade; o `build_macos.sh` resolve as fontes host por `$HERE`.

| Ficheiro | Monorepo | `../gow2-recomp` |
|---|---|---|
| `gow2_midasm_hooks.{h,cpp}` | `games/gow2/hooks/` | `hooks/` |
| `gow2_recomp.toml` | `games/gow2/config/` | `config/` |
| `build_macos.sh` | `games/gow2/` | raiz |
| `patch_ce03c_*.py` | `games/gow2/recomp_mid_v2/` | `recomp_mid_v2/` |

Espelho verificado por `diff`/`diff -rq` e commitado como `133395b` e `592d5a4`, com paths
explícitos. **Zero dados de jogo commitados**: `EBOOT.ELF`, `functions.json`, `extracted/`,
os dois `boot_gow2*` e os `recomp_macos_v*/` continuam fora de qualquer commit. O
`recomp_mid_v2/patch_41f78c_prune_probe.py` da outra sessão ficou por commitar.

## Task Commits

| # | Task | Commit (monorepo) | Espelho |
|---|---|---|---|
| 1 | TOML + corpo host do CE03C | `ab9e99c` | `133395b` |
| 2 | Patches legados cedem ao mid-asm; `RELIFT` com `--config` | `405a2f2` | `592d5a4` |
| 3 | Ledger, contratos, docs, reclassificação do 2550C8 | `b6f0c79` | — |

## Não-regressão das suites

```
$ .venv/bin/python3 tools/test_midasm_emit.py                              -> rc=0
$ .venv/bin/python3 tools/test_recomp_config.py                            -> rc=0
$ .venv/bin/python3 tools/test_invalid_instructions.py                     -> rc=0
$ .venv/bin/python3 games/gow2/lift_baseline/test_gen_migration_ledger.py  -> 8/8 PASS
$ .venv/bin/python3 games/gow2/lift_baseline/test_check_contracts.py       -> 7/7 PASS
```

## XEN-01 / XEN-02 / XEN-06 — fecham?

- **XEN-01: fechado no 17-01, e agora exercitado a sério.** Era `offline-unit` sobre uma
  fixture sintética de 4 palavras; passou a ser exercitado sobre o EBOOT real, com 51 991
  funções, e a emissão aterrou onde devia. **Sim, fecha.**
- **XEN-02: fecha.** O aceite escrito é `nm ... resolve (não U)` + smoke sem regressão — os
  dois cumpridos, e com um nível acima do pedido: o `objdump` prova a chamada e o log prova
  a execução. Marcado **Completo** na rastreabilidade.
- **XEN-06: fecha para o mid-asm, não para o ficheiro inteiro.** Todas as secções mid-asm
  têm consumidor e teste; acrescentou-se o exemplo end-to-end e a armadilha medida do
  `switch_table_file`. Mas `functions_override`, `registers`, `jump_address` e
  `[optimizations]` continuam **declarados como não consumidos** — o que é a forma honesta,
  e não "completo". **Fecha por completo quando a Fase 18 ligar o `switch_table_file`.**

## O CE03C conta como o 1.º dos ≥3 fixes re-lift-safe (XEN-05)?

**Sim — com uma qualificação escrita.** O aceite do XEN-05 é *"re-lift **sem** correr o patch
migrado e o mecanismo continua presente (`grep`); `verify_lift.sh` e `check_contracts.py` não
regridem"*. Está cumprido: o `grep` está na secção 3, o `check_contracts.py` está verde nos
dois lifts, e nenhum contrato existente regrediu.

A qualificação: o que ficou re-lift-safe foi o **wait-idle**, não as 143 linhas. O pad de
`setjmp` continua a depender de texto injectado — e num lift com `--config` **nem sequer é
reposto**. Contar o CE03C como 1/3 é legítimo (o fix P0 do ledger é o wait-idle, e é esse o
aceite `grep:` que estava lá escrito); contar como "o CE03C inteiro migrado" não seria.

## Next Phase Readiness

- **Fase 18 (switch tables):** o `switch_table_file` está comentado com a instrução de
  descomentar no mesmo commit que criar `gow2_switch_tables.toml`. Sem isso, o `--config`
  aborta e leva o mid-asm com ele.
- **Fase 19 (weak overrides):** herda o padrão de hooks host **e** um caso de uso concreto e
  já dimensionado — o pad de `setjmp` do CE03C (`WINDOWS.md` #8).
- **Auditoria do fallthrough cross-fragment:** o `patch_fallthrough_2550c8.py` está agora
  `alvo=lifter`, `estado=todo`, P0. É trabalho do lifter e continua por fazer.
- **Promoção do lift:** `recomp_macos_v3_midasm` **não** foi promovido. Isso tem gate próprio
  (`accept_relift.sh`) e não é desta fase.

---
*Fase: 17-mid-asm-de-ponta-a-ponta*
*Concluído: 2026-08-02*

## Self-Check: PASSED

Verificado a 2026-08-02 contra o disco e o `git log` (nada relido do stdout — os números
foram re-extraídos):

- **16/16 ficheiros** declarados existem, incluindo os três logs em `/tmp` e o binário
  `boot_gow2_f17_midasm` (121 666 728 bytes, `sha256 c32ebfac…f0a7`).
- **5/5 commits** existem: `ab9e99c`, `405a2f2`, `b6f0c79` (monorepo) e `133395b`, `592d5a4`
  (espelho).
- **Produção intacta:** `boot_gow2` continua com `mtime` de `02/08 17:37` (o do 17-02) e
  `recomp_macos_v2/ppu_recomp_002.cpp` com `31/07 15:29`. Nenhum re-lift, `apply_all_patches`
  ou build escreveu em produção.
- `st620_max=11` e `oob_or_ffff_hits=0` re-extraídos do log com o mesmo pipeline do
  `smoke_m0_baseline.sh` da Fase 16, não relidos do stdout do smoke.
- `grep -c 'CE03C wait-idle 1st movie'` reconfirmado: **1** no log de boot, **0** nos sete
  chunks do lift.
- `git status --short` do monorepo: dos ficheiros de planeamento só o `WINDOWS.md` e o
  `REQUIREMENTS.md` são meus; `PROJECT.md`, `MILESTONES.md`, `ROADMAP.md`, os dois
  `CLAUDE.md`, `config.json`, `GSD-MODEL-ROUTING.md`, `.planning/milestones/v1.2-*` e
  `scripts/gsd_plan*.sh` ficaram intactos e **fora** de todos os commits.
- **`ROADMAP.md` continua por actualizar** para a Fase 17 (3/3), pela mesma razão do 17-01 e
  do 17-02: está modificado no working tree por outra sessão activa. Já registado em
  `WINDOWS.md` #6; não foi duplicado. Nenhum verbo `state.*` nem
  `roadmap.update-plan-progress` foi corrido.
