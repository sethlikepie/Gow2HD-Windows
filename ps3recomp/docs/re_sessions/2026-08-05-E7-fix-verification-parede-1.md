# Sessão de RE — **E7**: verificação in-boot do fix da parede 1 — 2026-08-05

Continuação directa de [`2026-08-05-E6-parede1-mesma-doenca.md`](2026-08-05-E6-parede1-mesma-doenca.md)
(censo `seen=1 would_prune=1` em 3/3 corridas, mesma doença da parede 4). Esta sessão
implementa o FIX (`patch_24e1e8_wall1_subtag_skip.py`) e verifica-o in-boot, seguindo o
precedente exacto de [`2026-08-04-E4-fix-verification-parede-4.md`](2026-08-04-E4-fix-verification-parede-4.md):
uma ronda pode reportar `CONVERTIDO` e passar os testes offline e ainda assim não fazer
nada — só o build/boot real prova.

---

## O fix

`games/gow2/recomp_mid_v2/patch_24e1e8_wall1_subtag_skip.py` — poda por subtag no
dispatcher genérico de objectos (`func_0024E1E8` e os fragmentos irmãos que o lifter gera
para o mesmo padrão em `ppu_recomp_000/002/004.cpp`). Insere, imediatamente após o cálculo
do índice da fábrica (`ctx->gpr[11] = vm_read32((ctx->gpr[23] + ctx->gpr[0]))`), uma leitura
do `w0` completo do próprio objecto (`ctx->gpr[21]`) e aplica o MESMO predicado da parede 4
(`tools/test_wall4_subtag_fix.py::prune(w0)`, reaproveitado, não reimplementado):
`low16(w0)==1 && subtag(w0)!=1`. Se verdadeiro, salta a chamada
`ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);` (candidato B: poda o despacho ANTES de
acontecer, sem tocar em `func_002545B0` nem no que ele desreferencia). Incondicional, sem
gate `PS3_*` — fix, não diagnóstico (CLAUDE.md regra 5).

## As duas rondas (v1 refutada por build real, v2 corrigida)

### Ronda 1 (commit `cac26db`) — CONVERTIDO, testado offline, **não compilava**

A v1 declarava `bool ps3_e6w1_skip` ao nível do corpo da função lifted, sem escopo `{}`
próprio. O tri-estado (CONVERTED 8/8 sítios) e o teste offline
(`tools/test_wall1_subtag_fix.py`, predicado importado de `test_wall4_subtag_fix.py`)
passaram os dois — mas nunca tinham sido submetidos a um compilador real. Ao construir
`recomp_macos_e6fix` (cópia de `recomp_macos_v2`) e compilar com `clang++ -std=c++20 -c`,
**3 dos 4 chunks tocados falharam**:

```
ppu_recomp_000.cpp: 1 erro  -- error: redefinition of 'ps3_e6w1_skip'
ppu_recomp_002.cpp: 5 erros -- error: redefinition of 'ps3_e6w1_skip' (×5)
ppu_recomp_004.cpp: 2 erros -- error: redefinition of 'ps3_e6w1_skip' (×2)
```

Causa: o mesmo dispatcher genérico aparece **2+ vezes dentro da mesma função** lifted em
000/002/004 (o lifter repete o padrão para cada ponto de entrada alcançável); a v1
declarava a flag sem escopo confinado, colidindo entre ocorrências irmãs na mesma função.
`ppu_recomp_001.cpp` (o fix v2 da parede 4, `patch_outer_subtag_prune.py`, inalterado nesta
sessão) compilou limpo — o erro era específico à v1 do fix da parede 1.

### Ronda 2 (commit `0786280`) — corrigida, compilação real limpa

Todo o bloco (comentário + flag + verificação + chamada guardada) passou a ficar dentro de
um ÚNICO par de chavetas por ocorrência — cada ocorrência ganha o seu próprio escopo, sem
colisão de nome entre ocorrências irmãs na mesma função. Predicado e semântica inalterados.

**Revalidação, desta vez até ao compilador real:**

| estado | comando | resultado | rc |
|---|---|---|---|
| CONVERTED | `patch_24e1e8_wall1_subtag_skip.py <copia limpa>` | 8/8 sítios (000:1, 002:5, 004:2) | 0 |
| ALREADY-APPLIED | mesmo comando, 2ª corrida | 8/8 sítios detectados, 0 escritos | 0 |
| NO-EFFECT | dir com `.cpp` sintético sem a agulha | "agulha não encontrada" | **2** |
| **compilação real** | `clang++ -std=c++20 -O0 -w -c` nos 3 chunks (000/002/004) | **0 erros nos três** | 0 |

`tools/test_wall1_subtag_fix.py` e `tools/test_wall4_subtag_fix.py`: `PASS` os dois, antes
e depois da correcção (o predicado nunca mudou, só o escopo da variável de controlo).
Produção (`recomp_macos_v2/ppu_recomp_00{0,1,2,3,4}.cpp`) nunca tocada por nenhuma das
validações — hash idêntico antes/depois de todo o trabalho (verificado 3× ao longo da
sessão):

```
ppu_recomp_000.cpp  e63b24050323bf0056515328b958b9511d79bc927a61f17bc3cecc8df19cb94b
ppu_recomp_001.cpp  136ab046d1a0ced07fc5bda330eba8263783e7732434bb5a8253e7db15d0a989
ppu_recomp_002.cpp  9f31e151207b15039296bd99c30b475b00de0094d3acdbfbedcf78352e339653
ppu_recomp_003.cpp  578caf4ea62f5981662a3cfd27894f5630a74089df88bcf3de7d89749354821e
ppu_recomp_004.cpp  ac5d910b77ba6c740c01c7cee21d3beaaecd56dc5135afbe30e9520443762bf5
```

---

## Lift dedicado e catálogo completo

`../gow2-recomp/recomp_macos_e6fix/` (cópia fresca de `recomp_macos_v2`, `apply_all_patches.sh`
corrido do zero após a correcção v2). `TOTAL: 143 patches | APPLIED=3 ALREADY-APPLIED=26
NO-MATCH=0 UNVERIFIED=109 FAILED=5 SKIPPED=0`. `patch_24e1e8_wall1_subtag_skip.py`:
`APPLIED CONVERTED sitios_novos=8 ja_tinha=0`. Os 5 `FAILED` são pré-existentes (PROBEs sem
agulha satisfazível num lift fresco — mesma lista documentada no cabeçalho de
`apply_all_patches.sh`, nenhum altera o boot default).

**Os três verificadores estruturais do `verify_lift.sh` (chamado no fim do catálogo):**

| verificador | resultado |
|---|---|
| `lift_parity` | **PASS** (delta OK, 1 achado resolvido, 0 novos) |
| `MANIFEST` | **PASS** (243/246 marcadores intactos, 0 ausentes) |
| `audit_boundaries` | **PASS** (delta OK, 0 achados novos) |

**Achado não relacionado, registado por transparência:** o `rc` global do
`apply_all_patches.sh` é `1`, não por nenhum dos três verificadores acima, mas pelo gate de
delta `UNVERIFIED` (`current=56 baseline=42 novos=20`). Confirmado por data dos commits que
isto é dívida PRÉ-EXISTENTE: `games/gow2/lift_baseline/UNVERIFIED_BASELINE.json` foi
congelado em `1101bb6` (2026-07-29, 42 ids), e a lista `NOVO` é inteiramente composta por
patches trazidos pelo merge-subtree `7f7581d` (2026-08-02) — três dias antes desta sessão,
sem qualquer relação com a parede 1. `patch_24e1e8_wall1_subtag_skip.py` **não aparece** na
lista `NOVO`. Fora de âmbito desta sessão corrigir essa dívida de catálogo; não escondida,
registada aqui para quem for atrás.

---

## Build

`OUT=./boot_gow2_e6fix ./build_macos.sh recomp_macos_e6fix`: `dur=27s objs=7 errors=0`, link
OK, binário `116M`. Runtime PPU + HLE NID table + imagens SPU + boot host: sem erros.

---

## Medição in-boot (Task 2) — 6 corridas, `smoke_chain_gate.sh --bin boot_gow2_e6fix`

Confirmação ANTES de lançar (mandatória): `env | grep -c
'PS3_LIST254_EMPTY_IF_NULL\|PS3_POOL_NULL_IF_BAD\|PS3_LIST547_EMPTY_IF_BAD'` → **0** — os
três gates de diagnóstico da parede 1/4 estão TODOS unset nesta shell. `pgrep -fl boot_gow2`
vazio antes e depois das 6 corridas — zero órfãos (kill sempre por PID via
`measure_one()`, TERM→9, nunca `pkill -f`).

| corrida | `st620` | `startseq` | `nopic` | `AUTO_LOAD` | `[STATE] frame=1` | `FATAL 0x00514E80` | `[PAREDE1-SKIP]` | `ICALL-BAD` (lr variados) | `SPUJOB` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 11 | 2 | 4 | 0 | 0 | **0** | 2 | 8 | 604 |
| 2 | 1  | 0 | 0 | 0 | 0 | **0** | 0 | 0 | 169 |
| 3 | 11 | 2 | 4 | 0 | 0 | **0** | 2 | 8 | 576 |
| 4 | 11 | 2 | 4 | 0 | 0 | **0** | 2 | 8 | 692 |
| 5 | 11 | 2 | 4 | 0 | 0 | **0** | 2 | 8 | 624 |
| 6 | 11 | 2 | 4 | 0 | 0 | **0** | 2 | 8 | 617 |

`smoke_chain_gate.sh`'s próprio sumário: `elo_stopped=nenhum em 0 de 6 (limiar: 4)` — o
critério GATE-01/02 completo (que exige AUTO_LOAD) continua a falhar, tal como o E5 já
media honestamente ANTES desta sessão (isso é esperado; o alvo desta sessão é só o `FATAL`
de wall 1, não a cadeia inteira). Corrida 2 é a flakiness de timing já conhecida
(`CLAUDE.md`: "Um run preso em st620=1 pode ser flakiness de timing") — log com 1511 linhas
vs. ~4000 das outras 5; não conta contra o critério porque nunca chega perto do dispatch de
wall 1 (`[PAREDE1-SKIP]=0`, coerente).

### O critério decisivo: `FATAL: stuck calling 0x00514E80` — **0 ocorrências em 6/6 corridas**

Baseline de ontem (E5, mesmo binário-classe, sem o fix): `FATAL` presente em **6/6**
corridas, sempre a mesma cadeia (`lr=0x0024E2D4`, dentro de `func_0024E1E8`, 2000 chamadas
repetidas ao mesmo `ctr=0x00514E80`). Com o fix: **grep `FATAL` = 0** nas 6 corridas desta
sessão, sem excepção.

`[PAREDE1-SKIP]` (a sonda do próprio fix, incondicional, não um gate) disparou exactamente
**2 vezes** em 5 das 6 corridas (0 na corrida 2 flakey, que nunca chega lá) — coerente com a
E6 ("`func_0024E1E8` só é exercitado uma vez neste caminho patológico por corrida"; aqui
conta-se 2 porque o predicado de poda dispara tanto na primeira leitura de classificação
como, provavelmente, num segundo ponto do mesmo objecto — não investigado mais fundo, fora
de âmbito desta sessão).

### O que muda para além do `FATAL` — progresso real, não só ausência de crash

Nas 5 corridas saudáveis (excl. corrida 2), o processo já não morre: corre a janela completa
de 90s do `smoke_chain_gate.sh` (killed por SIGTERM no timeout, não por abort), produzindo
actividade contínua e substancial que NUNCA tinha sido alcançada antes (o `FATAL` matava o
processo assim que `func_0024E1E8` despachava o registo WAD, muito antes de qualquer
cascata `[POSTINTRO]`):

```
[POSTINTRO] B71 after B70B0
[POSTINTRO] B71 after alloc0x2024 r3=0x407F4648
[POSTINTRO] B71 393E0 HLE-lite obj=0x407F4648 vt=0x00511318
[POSTINTRO] B71 after 393E0
[POSTINTRO] B71 after 251230
[FACTORY] freelist REPLENISH fo=0x400D6808 fl=0x47AB4000 shell=0x47AB4400 type=0x0004
[POSTINTRO] B71 icallA ent=0x400D6808 vt=0x00515700 code=0x0039E794
[FACTORY] reuse product fo=0x400D6808 hdr=0x40638918 prod=0x4063891C
[POSTINTRO] B71 icallA reuse product r3=0x4063891C reused=1
[POSTINTRO] B71 skip icallB (reuse product=0x4063891C)
[PADPOLL t=...ms] cellPadGetData port=0 #1 (game IS polling pad)
[SPUJOB] spu job returned cleanly   -- 576 a 692 vezes por corrida, distribuídas
                                        continuamente da linha ~76 até à ~4480 (não um
                                        burst único seguido de silêncio -- actividade real
                                        e sustentada durante toda a janela de 90s)
```

Nota honesta: `st620`/`startseq`/`nopic`/`AUTO_LOAD` ficam numericamente IDÊNTICOS à
referência de ontem (`11/2/4/0`) — esses contadores medem progressão da intro/movie-FSM
que já saturava tanto antes como depois do fix (não são sensíveis ao que acontece depois de
`B71`). O progresso real desta sessão é qualitativo e a jusante: centenas de ciclos
`SPUJOB`/`FACTORY`/`B71` que antes eram inatingíveis porque o processo já estava morto.
`[STATE] frame=1` continua em 0/6 — o marcador do "loop principal despachou o primeiro
estado" não dispara, e `AUTO_LOAD` continua a não ser criada dentro da janela de 90s.

### Sinal residual, NÃO perseguido nesta sessão (nomeado para quem retomar)

Em 5/6 corridas aparecem **8** linhas `[ICALL-BAD] ctr=0x00514E80` com `lr` DIFERENTE em
cada uma (`0x000C9994, 0x0013EF60, 0x002526A8, 0x0005842C, 0x00058504, 0x000FF02C,
0x0002DBC0, 0x000D3F44` — nenhum destes é o `lr=0x0024E2D4` da parede 1 original). Isto
**não é** a doença da parede 1 outra vez: não há "`FATAL: stuck calling ... (N times)`" —
cada `lr` aparece exactamente uma vez, o runtime regista e segue em frente (mesmo padrão de
"log e continua" que já existe para `[ICALL-HEAP] skip`). Não investigado nesta sessão
(regra: não perseguir duas paredes na mesma corrida) — fica nomeado para uma sessão futura
decidir se `0x00514E80` continua a ser lido como ponteiro de dados noutros pontos do código,
e se vale a pena o mesmo tratamento (poda por classificação) nesses call-sites.

---

## Suites de não-regressão (repo `ps3recomp`)

| suite | resultado |
|---|---|
| `cmake -B build-tests -DCMAKE_BUILD_TYPE= && ctest --test-dir build-tests` | **24/24** (2 skip: `lift_selftest`, `spu_lift_selftest` — SDK PS3 indisponível, esperado) |
| `./scripts/test_python.sh` | **15 PASS / 2 SKIP / 0 FAIL** |

Medido antes e depois da correcção v2 do fix — idêntico nas duas medições, sem regressão.

---

## Veredicto: **QUEBRADA**

O `FATAL: stuck calling 0x00514E80` desaparece em **6/6 corridas** (excede o limiar de ≥3),
com `PS3_LIST254_EMPTY_IF_NULL` (e os outros dois gates de diagnóstico da parede 4)
confirmadamente `unset` (`env | grep -c` → 0). O boot deixa de morrer em `func_0024E1E8` e
passa a correr a janela completa de medição, com actividade real e sustentada (centenas de
ciclos `SPUJOB`/`FACTORY`/`B71`) que era estruturalmente inatingível antes deste fix.

**Não há parede nova a nomear** — nenhuma das 6 corridas mostra um hang, um `FATAL`, nem um
`ctr` repetido em loop; o processo corre até ao timeout do smoke (SIGTERM, comportamento
esperado desta ferramenta) fazendo trabalho real. `AUTO_LOAD` continua a não ser criada
dentro dos 90s (mesmo valor da referência), mas isso não é evidência de uma nova parede —
é, na leitura mais honesta dada a actividade contínua observada, o próprio aviso do
`lib_boot_chain_metrics.sh`: "`thr_created=0` não distingue, por si só, 'o loop nunca é
alcançado' de 'o loop é alcançado e corre bem, mas não sai do jogo dentro da janela'" — só
que, ao contrário do que a E5 media (onde havia um abort nomeado ANTES do B71/loop
principal), agora não há abort nenhum, e a cascata B71/FACTORY progride mais fundo do que
antes. O sinal residual `[ICALL-BAD] ctr=0x00514E80` de `lr` variados (não repetido, não
fatal) fica nomeado acima como o próximo ponto de atenção, sem ser tratado como bloqueio
desta fase.

## Commits desta sessão

- `cac26db` — fix v1 (parede 1), commitado ANTES de qualquer medição in-boot
- `0786280` — correcção v1→v2 (redefinição de `ps3_e6w1_skip`), commitado ANTES do rebuild
- este ledger — commitado com o veredicto, ANTES do relatório final ao coordenador

## Reconciliação com uma medição paralela (commit `15e6b1e`, mesma sessão de trabalho)

Enquanto esta sessão corria, foi commitado em paralelo um addendum ao ledger da E6
(`15e6b1e`, mesmo autor) com uma medição independente — `boot_gow2_p1fix`, construído com
`FORCE_REBUILD_LIFT=1` a partir do MESMO `recomp_macos_e6fix` (fonte bit-a-bit idêntica,
confirmado: nenhum `.cpp` do lift tem mtime posterior às ~11:34, quando o
`apply_all_patches.sh` desta sessão terminou de escrever; só os `.o` foram recompilados de
novo por essa medição, às ~11:39, sobrepondo os meus — sem efeito no MEU binário, já ligado
às 11:36). Essa medição concorda com esta no essencial — `FATAL` **0 em 3/3** corridas,
mesmo veredicto (QUEBRADA) — mas diverge num detalhe: reporta `startseq=1` nas 3 corridas e
caracteriza o sintoma seguinte como "STALL" (`st620=11 -> 11` a girar no sampler).

**A minha medição, relida com atenção a este ponto:** `st620 11 -> 11` também aparece nos
MEUS logs (2× por corrida, 5/5 corridas verificado por grep) — não é uma divergência, é o
mesmo padrão, só não tinha sido nomeado assim no corpo principal deste ledger acima. Mas
`StartSeq(handle=0) -> async decode thread` aparece **exactamente 2 vezes**, em posições de
log consistentes, nas 5 corridas saudáveis desta sessão (`grep -c` = 2 em todas, verificado
de novo agora) — **não** 1 como a medição paralela reporta. `startseq=2` bate com a
referência de ontem (E5, sem o fix) — ou seja, pela MINHA medição, não há regressão neste
contador.

**Não resolvo esta divergência aqui** — as duas medições correram no MESMO computador, em
janelas de tempo sobrepostas (a minha: 11:37–11:44; o build+corridas da paralela: build às
11:39–11:40, corridas a seguir), competindo por CPU/IO com os meus próprios builds/testes em
curso. Contenção de sistema é um confundidor plausível para um contador sensível a timing de
thread assíncrona (`StartSeq` depende do agendamento real do "async decode thread"), e é
exactamente a classe de flakiness que o `CLAUDE.md` já documenta para `st620`. Não é,
portanto, evidência forte de uma diferença REAL de comportamento entre os dois binários
(mesma fonte, mesmo compilador, mesmas flags). Registo os dois resultados lado a lado em vez
de escolher um:

| medição | binário | corridas limpas | `FATAL` | `startseq` |
|---|---|---:|---:|---:|
| esta sessão (E7) | `boot_gow2_e6fix` (build normal) | 5 | 0/5 | **2** em 5/5 |
| paralela (E6 addendum, `15e6b1e`) | `boot_gow2_p1fix` (`FORCE_REBUILD_LIFT=1`) | 3 | 0/3 | **1** em 3/3 |

Item para quem retomar: repetir a medição de `startseq` **sem** contenção de CPU concorrente
(máquina livre, um binário de cada vez) para decidir se `startseq=1` é real ou foi artefacto
da corrida simultânea. Até lá, tratar `startseq` como **não confirmado** em qualquer direcção
— nem regressão confirmada, nem descartada.

---

## O que esta sessão NÃO afirma

- **Não** afirma que `AUTO_LOAD`/GATE-01-02 (critério completo do ROADMAP) está fechado —
  continua a falhar (`elo_stopped=nenhum em 0/6`), tal como a E5 já media antes desta sessão.
- **Não** trata o `[ICALL-BAD]` residual como parede nova nem como resolvido — fica aberto,
  nomeado, para decisão de uma sessão futura.
- **Não** corrige a dívida do `UNVERIFIED_BASELINE.json` (pré-existente, 2026-07-29 vs.
  merge de 2026-08-02) — fora de âmbito, só registada.
- **Não** toca em `func_002545B0`, pixels, shaders ou `SHADERSRC`.
- **Não** usa nenhum gate de diagnóstico (`PS3_LIST254_EMPTY_IF_NULL` etc.) como parte do
  fix nem da medição de aceite — confirmado unset em todas as 6 corridas.
