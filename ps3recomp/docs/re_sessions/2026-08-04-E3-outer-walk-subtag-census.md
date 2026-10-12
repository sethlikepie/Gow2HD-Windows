# Sessão de RE — sonda **E3**: a contagem que decide entre o fix A e o fix B — 2026-08-04

Continuação directa de [`2026-08-04-E2-quem-forca-low16.md`](2026-08-04-E2-quem-forca-low16.md),
que fechou estaticamente os três sítios candidatos ao fix (D-11.1) e nomeou, na secção
"Próximo experimento", exactamente esta contagem como o passo que decide entre eles (D-11.2).
Conduzida com [`docs/RE_PLAYBOOK_GOW2.md`](../RE_PLAYBOOK_GOW2.md).

> **ESTADO: previsões escritas, sonda por implementar.** Este bloco inicial (§*Pergunta*,
> §*Âncoras*, §*Desenho da sonda*, §*Sondas de CONTROLO planeadas*, §*As duas previsões*) foi
> commitado **ANTES** de o hook `OuterWalkSubtagCensus` existir no `.cpp` e **ANTES** de
> qualquer corrida — é o que torna a sonda discriminadora em vez de confirmatória. Os
> resultados entram em secções novas, abaixo da linha de corte; nada acima dela é reescrito
> depois de correr.

---

## Parede escolhida e porquê (custo, não ambição)

A parede 4 (`func_0041F700`/`func_0041FF70`) já está diagnosticada de ponta a ponta pelas
sessões E1 (medição in-boot: `sent_head=0x00000000`, `no_rep=n-1`, 179 459 343 iterações) e E2
(prova estática: `0x00254C40` força `low16 := 1` preservando `subtag`; `0x0041FF70` filtra só
por flags e despacha por `low16`). O que falta **não é mais diagnóstico** — é a única medição
que falta para escolher **onde** o fix vai: A (`0x00254C40`, a classificação) ou B
(`0x0041FF70`, a enumeração). Sem esta contagem, escolher entre A e B é palpite (D-11.2,
`11-CONTEXT.md`). É a medição de menor custo que resta na parede: uma sonda de contagem, sem
necessidade de nova hipótese nem de nova ferramenta.

---

## Pergunta (uma frase)

> **Dos objectos que passam os DOIS filtros de flags de `0x0041FF70` (`flags & 0x10 == 0` e
> `flags & 3 == 0`) e o gate "tem filho" (`*(obj-4) != 0`), quantos têm `subtag != 1`?**

Se a resposta for **exactamente 1** (o registo WAD, `w0=0x40030001`, medido pela E1), o fix
é **B**: um filtro de subtag no walk exterior poda só o caso patológico e não toca em mais
nada. Se for **mais que 1**, há outros registos mal classificados pelo mesmo mecanismo, e a
pergunta volta para **A** (a linha do desserializador que força `low16 := 1`).

---

## Âncoras

| âncora | valor |
|---|---|
| **EA do hook** | `0x0042003C` — `837C0088  lwz r27, 0x88(r28)`, dentro de `func_0041FF70` |
| **precedida por (has-child gate)** | `0x0042002C  rldicl r28,r26,0,32` / `0x00420030  lwz r0,0x70(r28)` / `0x00420038  beq cr7,0x41FFE8` |
| **precedida por (filtro `flags & 0x10`)** | `0x00420004  rlwinm r0,r9,0,27,27` / `0x0042000C  bne cr7,0x41FFE8` |
| **precedida por (filtro `flags & 3`)** | `0x00420010  rlwinm r0,r9,0,30,31` / `0x00420018  bne cr7,0x4201D0` |
| **função** | `func_0041FF70` — o walk exterior, `.opd.FUN_0041ff70` no Ghidra |
| **registo patológico conhecido (E1)** | `w0 = 0x40030001`, `h = 0x4077AD5C`, `subtag = 0x003` |
| **tabela de tipos (C1)** | `0x00868D48` — lida em `func_0041FF70` por `lwz r24, -0x225C(r2)` (0x0041FF7C), mesma constante TOC que a E1 confirmou para o push interior (lá em `r28`; aqui é `r24`, gestor/registo diferente do mesmo mecanismo — ver `docs/RE_PLAYBOOK_GOW2.md` §2 "obj vs obj+4") |
| **tag de log** | `[PS3T] ... CHECKPOINTS ... OUTER_SUBTAG_ALIVE / OUTER_SUBTAG_MATCH / OUTER_SUBTAG_CENSUS_SUM ...` |
| **gate** | `PS3_OUTER_SUBTAG_CENSUS=1`, **OFF por default** |

Verificado nesta sessão, à mão, com `python3 tools/show_func.py 0x0041FF70 --asm --elf
../gow2-recomp/EBOOT.ELF --functions ../gow2-recomp/functions.json --max 90` contra o
`EBOOT.ELF` real — os quatro checks (flags&0x10, flags&3, has-child, e a instrução em
`0x0042003C`) batem byte a byte com o que `11-01-PLAN.md` (Task 1) descreve.

---

## Desenho da sonda — e porque é assim

**Mecanismo: `[[midasm_hook]]`**, quarta entrada na mesma família "SONDA E1" do
`games/gow2/config/gow2_recomp.toml` — mesma disciplina (mid-asm corre AO LADO da instrução,
sem a substituir; nenhuma escrita em memória guest; nenhum toque em `ctx->gpr`).

**Um hook: `OuterWalkSubtagCensus`**, ancorado em `0x0042003C` — o ponto onde `ctx->gpr[28]`
(o `piVar8` do C do Ghidra) já passou os dois filtros de flags e o gate "tem filho", e está
prestes a fornecer `h_child = *(piVar8 + 0x88)` ao guest. O hook lê o MESMO valor que a
instrução seguinte vai carregar — não interfere, só observa — e daí lê `w0 = *(h_child + 4)`
para extrair `low16`/`subtag`.

**Volume:** o walk exterior visita, no total, uma ordem de grandeza muito menor que o walk
interior da E1 (E1 mediu só 2 objectos alcançando este ponto, via a nota de 2026-08-02 e o
`enters=176` do push interior, que é chamado a partir daqui). Não há necessidade de tabela de
agregação de 512 slots como a E1: um conjunto pequeno (cap 16 pares distintos) chega, e o
dump de resumo pode sair em CADA chamada em vez de periódico por tempo, porque o volume total
é baixo — decisão registada aqui, antes de medir, precisamente para não inflar a sonda além
do que a pergunta exige.

---

## Sondas de CONTROLO planeadas (a correr ANTES de confiar em qualquer número)

| # | sonda | o que TEM de dar | nível |
|---|---|---|---|
| **C-alive** | `OUTER_SUBTAG_ALIVE` na 1ª chamada do hook | presente pelo menos 1 vez nas corridas com o gate ON — sem ela, um silêncio não distingue "não passou aqui" de "não compilou" | in-boot |
| **C1** | `tab` = `ctx->gpr[24]` em todas as chamadas | **`0x00868D48`** — o valor medido no ROADMAP e pela E1 (lá em `r28`, aqui em `r24`, mesmo TOC) | in-boot |
| **C-known** | o `w0=0x40030001` (registo WAD, medido pela E1) aparece entre os `OUTER_SUBTAG_MATCH` | presente pelo menos 1 vez, com `subtag=0x003` | in-boot |
| **C3** | gate OFF ⇒ zero linhas `OUTER_` e métricas de boot (`st620`, `INTROSEQ`) iguais à baseline sem regressão | **0 linhas**, métricas idênticas | in-boot |

Sem **C-alive + C1** verdes, nenhum resultado desta página conta.

<!-- LINHA DE CORTE -->

---

## As duas previsões — escritas ANTES de correr

### Previsão B — a contagem é exactamente 1

- A única leitura de `subtag != 1` no ponto de anexação é o registo WAD (`w0=0x40030001`,
  `subtag=0x003`), medido pela E1 como o único caso patológico da corrida com os gates ON;
- ⇒ **candidato B é o fix**: um filtro de subtag em `0x0041FF70` poda só o caso patológico e
  não toca em mais nada — cirúrgico e reversível (D-11.1);
- ⇒ o próximo passo (`11-03`) escreve o weak override em `0x0041FF70` (ou o midasm hook, se a
  poda puder ser feita sem alterar o fluxo de controlo — a decidir no `11-03`) que acrescenta
  `subtag == 1` ao par de filtros de flags já existente.

### Previsão A — a contagem é maior que 1

- Existe pelo menos mais um objecto, além do registo WAD, que passa os dois filtros de flags
  e o gate "tem filho" com `subtag != 1`;
- ⇒ a pergunta reabre para **candidato A**: o desserializador `0x00254C40` está a produzir
  `low16 := 1` para MAIS tipos do que só o WAD, e um filtro de subtag em B passaria a podar
  objectos que talvez devessem ser enumerados (risco de regressão fora da parede);
- ⇒ o conjunto exacto de pares `(low16, subtag)` que este censo reporta torna-se **input
  obrigatório** para o `11-03` delimitar o alcance de A (que outros `subtag` o
  desserializador está a "confundir" com `low16=1`).

**Se as corridas não convergirem entre si** (ex.: 1 nalgumas, >1 noutras), a secção de
resultados tem de o dizer honestamente — per D-11.2/D-11.5, o `11-03` deve por defeito assumir
a interpretação de "mais objectos" (o caminho investigativo de A) quando os dados não
estiverem assentes, porque o mecanismo de B depende de o número ser provadamente exactamente 1.

---
---

# RESULTADOS — a sonda correu

> **ESTADO: E3 CORRIDA.** Tudo abaixo desta linha foi escrito **depois** das corridas. Nada
> acima dela foi tocado — as duas previsões e as sondas de controlo planeadas estão no commit
> `8e6d1f0`, anterior à existência do hook `OuterWalkSubtagCensus` (commit `26afb7f`, que só
> tem os três ficheiros de código, sem tocar neste ledger).

## Veredicto em uma linha

> **A contagem D-11.2 é exactamente 1, medida de forma determinística em 3/3 corridas de
> medição** (mesma chave `low16=0x0000 subtag=0x000`, mesmo valor corrompido bit-a-bit
> `h_child=0x3F800000`). O objecto contado está no MESMO endereço heap
> (`piVar8=0x4077AD5C`) que a E1 mediu independentemente, dois dias antes, como o registo WAD
> patológico — mas a sonda de controlo C-known (que esperava ver `w0=0x40030001` directamente)
> **NÃO reproduziu** esse valor: o campo que este hook lê (`piVar8+0x88`, seguido de `+4`) é
> um campo DIFERENTE do que a E1 leu (`h+4` de dentro do push). **DECISÃO: B**, sustentada
> pela contagem (o discriminador literal de D-11.2) e pela correspondência de endereço com a
> E1 — não pela correspondência directa do valor `w0` do C-known, que é declarada como não
> reproduzida.

---

## Ferramentas (comando + rc)

| # | o quê | rc |
|---|---|---|
| T0 | `python3 tools/show_func.py 0x0041FF70 --asm --elf ../gow2-recomp/EBOOT.ELF --functions ../gow2-recomp/functions.json --max 90` — reverificação da âncora | 0 — os quatro checks batem byte a byte |
| T1 | `python3 ../ps3recomp/tools/ppu_lifter.py EBOOT.ELF --functions functions.json --config config/gow2_recomp.toml -o ./recomp_macos_e3probe -j 4` | 0 — `Config: 5 midasm_hook(s) loaded`, 51 991 funções, 7 chunks, 0 stubs |
| T2 | verificação da **ancoragem** no lift novo (`grep` em `ppu_recomp_001.cpp`) | a chamada `gow2_midasm_OuterWalkSubtagCensus(ctx);` sai **imediatamente antes** de `ctx->gpr[27] = vm_read32(ctx->gpr[28] + 0x88);`, dentro de `loc_0042002C`, logo depois do gate "tem filho" — a tradução exacta de `0x0042003C` |
| T3 | `./apply_all_patches.sh ./recomp_macos_e3probe --status /tmp/e3_status.tsv` | rc=1 — `TOTAL: 140 patches \| APPLIED=118 ALREADY-APPLIED=5 NO-MATCH=0 UNVERIFIED=14 FAILED=3` — **números idênticos** aos que a E1 mediu (T3 da E1); `FALHARAM` e o `UNVERIFIED NOVO patch_freelist_guard_262610.py` são pré-existentes, não desta sonda (mesma nota da E1). Os três gates de diagnóstico (`patch_218364_pool_null_gate.py`, `patch_254610_empty_list_gate.py`, `patch_2547f8_empty_list_gate.py`) saem `APPLIED`/`PROBE` |
| T4 | `verify_lift.sh` (o de `games/gow2/`, não a cópia desactualizada em `gow2-recomp/`) no lift da sonda | rc=0 — `lift_parity PASS`, `MANIFEST PASS`, `audit_boundaries PASS` |
| T5 | `OUT=./boot_gow2_e3probe ./build_macos.sh ./recomp_macos_e3probe` | 0 — `gow2_midasm_hooks: compilado`, `*** boot_gow2_e3probe built ***`, 116 MB |
| T6 | `nm ./boot_gow2_e3probe \| grep gow2_midasm` | `T _gow2_midasm_OuterWalkSubtagCensus` presente (símbolo forte, resolvido), junto aos 4 hooks já existentes |
| T7 | **7 boots** (G6, kill por PID, `orfaos=0` em **todos os 7**, `pgrep` antes/depois de cada) | ver tabelas abaixo |

**A produção não foi tocada.** `recomp_macos_v2`/`boot_gow2` (promovidos `3 ago 10:38`) ficaram
com o `mtime` intacto durante toda a sessão. Lift em `recomp_macos_e3probe/`, binário
`boot_gow2_e3probe`. Logs em `/tmp/e3_run{A..G}.log`, fora dos dois repos.

---

## Sondas de CONTROLO — o que deram

| # | sonda | resultado | nível |
|---|---|---|---|
| **C-alive** | `OUTER_SUBTAG_ALIVE` na 1ª chamada | presente em **3/3** corridas de medição (D, E, F) ✅ | in-boot |
| **C1** | `tab = ctx->gpr[24]` | **`tab=0x00868D48 tab_ok=1 tab_bad=0`** em **3/3** corridas de medição e na corrida C3 (G) ✅ — a mesma constante que a E1 confirmou (lá em `r28`, aqui em `r24`, mesmo TOC `-0x225C(r2)`) | in-boot |
| **C-known** | `w0=0x40030001` (a assinatura do WAD medida pela E1, lida via `h_child+4`) aparece nos `OUTER_SUBTAG_MATCH` | ❌ **NÃO reproduzido** — `w0_40030001=0` nas 3 corridas. Ver §"O que a sonda mediu" para a explicação: o hook lê `piVar8+0x88` (um campo diferente do `h+4` que a E1 leu de dentro do push), e ESSE campo, no objecto contado, contém um padrão de bits `0x3F800000` (float IEEE-754 = 1.0f), não um ponteiro válido | in-boot |
| **C3** | gate OFF ⇒ zero linhas `OUTER_`, métricas de boot iguais às corridas de medição (mesma janela, 90 s) | ✅ corrida G: **0 linhas `OUTER_`**; `st620=11`, `INTROSEQ=9`, `FATAL=0`, `ICALL-BAD=8` — **idênticos** às corridas D/E/F (mesma janela) | in-boot |

**C-alive + C1 estão verdes** — o resto desta página conta. **C-known não reproduziu** — é uma
descoberta em si (declarada, não escondida), não um "falhanço da sonda": o hook está a ler
exactamente o campo que `11-01-PLAN.md` mandou ler (`h_child = *(piVar8+0x88)`, depois
`w0 = *(h_child+4)`), e esse é um campo DIFERENTE do que a E1 leu (`w0` do próprio objecto
`h`, em `h+4`). Ver a análise abaixo.

---

## O que a sonda mediu

### Receita e janela — o walk exterior só é alcançado com tempo E com os gates

| corrida | gates 1–3 | censo | janela | `OUTER_` |
|---|---|---|---|---|
| A | OFF | OFF | 25 s | 0 (esperado — censo desligado) |
| B | ON | OFF | 25 s | 0 (censo desligado, e a janela curta não chega ao ponto de qualquer forma — ver C) |
| C | ON | ON | 25 s | **0** — a janela de 25 s NÃO chega ao ponto de anexação (nem sequer `OUTER_SUBTAG_ALIVE` saiu) |
| D | ON | ON | 60 s | **3** (`ALIVE`+`MATCH`+`SUM`) |
| E | ON | ON | 90 s | **3** |
| F | ON | ON | 90 s | **3** |
| G | ON | OFF | 90 s | **0** (C3, mesma janela que D/E/F) |

A corrida C (25 s, gates+censo ON) não alcançou o ponto — confirma que 25 s (a janela default
dos smokes curtos do repositório) é curta demais para este ponto do boot, sob a pressão de swap
medida nesta máquina (`vm.swapusage`: `used=9474 MB` de `10240 MB`, perto do limiar que os
scripts do repositório já documentam como fazendo o `st620` avançar mais devagar). As corridas
D (60 s) e E/F (90 s) todas alcançaram o ponto — o alcance não depende da duração exacta acima
de ~60 s nesta máquina, só de ultrapassar um mínimo.

### As três corridas de medição — resultado idêntico, bit a bit

```
D (piVar8=0x4077AD9C, 60s): calls=1 match=1 keys=1 w0_40030001=0 tab=0x00868D48 tab_ok=1 tab_bad=0
E (piVar8=0x4077AD5C, 90s): calls=1 match=1 keys=1 w0_40030001=0 tab=0x00868D48 tab_ok=1 tab_bad=0
F (piVar8=0x4077AD5C, 90s): calls=1 match=1 keys=1 w0_40030001=0 tab=0x00868D48 tab_ok=1 tab_bad=0
```

`OUTER_SUBTAG_MATCH` em todas: `w0=0x00000000 h_child=0x3F800000 low16=0x0000 subtag=0x000`.

**`calls=1` em TODAS as três** — o walk exterior, na janela observada, só chama o hook (i.e.
só passa um nó pelos dois filtros de flags + o gate "tem filho") **uma vez**. Desse ÚNICO nó,
`subtag=0 != 1` — conta para o censo. **Nenhum outro nó** passou os filtros nesta janela.

O endereço de `piVar8` varia 0x40 bytes entre a corrida D (60 s) e as corridas E/F (90 s) —
`0x4077AD9C` vs `0x4077AD5C` — a mesma ordem de variação de endereço heap entre corridas que a
E1 já documentou ("`0x4077AD1C` e `0x4077AD5C` em corridas diferentes"). **`0x4077AD5C`, medido
em E e F, é BIT A BIT o mesmo endereço que a E1 mediu, dois dias antes, como `h` do registo
WAD patológico** (`docs/re_sessions/2026-08-03-E1-typewalk-trace.md`, secção "E o chamador").

### Porque o C-known não reproduziu — e porque isso NÃO invalida a contagem

A E1 mediu, **de dentro do push** (`func_0041F700`), com `h=0x4077AD5C`:
`w0 = vm_read32(h+4) = vm_read32(0x4077AD60) = 0x40030001`.

Este hook, ancorado ANTES do push, no walk exterior, lê um campo DIFERENTE do mesmo `piVar8`:
`h_child = vm_read32(piVar8+0x88)`, e só DEPOIS `w0 = vm_read32(h_child+4)` — ou seja, o `w0`
que este hook reporta é o `w0` do FILHO que `piVar8+0x88` aponta, não o `w0` do PRÓPRIO
`piVar8`. É exactamente a leitura que `11-01-PLAN.md` mandou fazer (Task 1, `<action>`), e é a
MESMA relação campo-a-campo que o walk exterior usa para decidir para onde despachar
(`0x420060 lhz r0,0x6(r9)` onde `r9` é `h_child` truncado — a leitura de `low16` que o PRÓPRIO
guest faz do filho, não do pai).

Se `piVar8=0x4077AD5C` for o MESMO objecto que a E1 identificou (o endereço bate, ver acima),
então `piVar8+4` (o `w0` PRÓPRIO desse objecto) é exactamente o `0x4077AD60` que a E1 já leu e
mediu como `0x40030001` — mas este hook nunca lê `piVar8+4` directamente, só o `+0x88`.
**Não medido nesta sessão**: uma leitura directa de `piVar8+4` confirmaria isto sem inferência.
É o item nº1 de "Próximo experimento", abaixo.

O que ESTÁ medido, e é o que sustenta a leitura de que se trata do mesmo objecto: `h_child =
vm_read32(piVar8+0x88) = 0x3F800000` — um padrão de bits IEEE-754 exacto para `1.0f`. Isto é
**exactamente a mesma classe de bug** que a E1/E2 já provaram para este objecto noutro campo
(`+0x7C`/`+0x80` = `matriz[0][3]`, um float de uma matriz de transformação lido como sentinela
de lista, `sent_head=0x00000000` medido pela E1). `+0x88` fica 8–12 bytes depois de `+0x7C`,
dentro da mesma região de matriz 4×4 — um `1.0f` nessa vizinhança é exactamente o que se espera
de uma matriz de transformação (elemento da diagonal). **Isto é inferência, não medição
directa**: a sessão não desmontou `func_0024C1F8` de novo para confirmar que `+0x88` cai
literalmente dentro da matriz (a E1/E2 já fizeram isso para `+0x7C`); fica registado como tal.

---

## O que fica REFUTADO por medição

1. **Que a corrida de 25 s (a janela default dos smokes curtos) chega ao ponto de anexação.**
   **REFUTADO.** A corrida C (gates+censo ON, 25 s) deu zero linhas `OUTER_`, nem sequer o
   `ALIVE` da primeira chamada — só corridas de ≥60 s (D) e 90 s (E, F) alcançaram o ponto,
   sob a pressão de swap medida nesta máquina (~9,2 GB de 10 GB). *(in-boot)*
2. **Que o `w0=0x40030001` apareceria directamente no primeiro campo que este hook lê.**
   **REFUTADO** — o `w0` medido é `0x00000000`, não `0x40030001`. A explicação (ver acima) é
   que o hook lê um campo diferente (`h_child`, não `piVar8` directamente), não que o objecto
   seja outro — mas isto não estava previsto nas duas previsões escritas antes de correr, e
   fica registado como desvio face à expectativa, não como confirmação silenciosa.

Nada do que a E1/E2 mediram foi refutado por esta sessão — pelo contrário, o endereço
`0x4077AD5C` e o padrão "float lido como ponteiro" reforçam-nas.

---

## Veredicto: **avançado — DECISÃO D-11.2: B**

O discriminador literal de D-11.2 — quantos objectos passam os dois filtros de flags + o gate
"tem filho" com `subtag != 1` — foi medido em **exactamente 1**, de forma determinística, em
**3 corridas independentes** (D, E, F), com o mesmo `key` (`low16=0x0000 subtag=0x000`) e o
mesmo valor corrompido bit-a-bit (`h_child=0x3F800000`) nas três. A sonda de controlo C3
(corrida G, mesma janela de 90 s que as corridas de medição) prova que, com o gate do censo
desligado, o comportamento é idêntico (zero linhas `OUTER_`, mesmas métricas de boot) — G2
cumprido.

Per a regra de decisão escrita antes de correr (D-11.2/`11-CONTEXT.md`): **contagem == 1 ⇒
candidato B**.

**A ressalva honesta, obrigatória por CLAUDE.md regra 4:** a sonda de controlo C-known (que
esperava ver o `w0=0x40030001` da E1 directamente no `OUTER_SUBTAG_MATCH`) não reproduziu esse
valor — o campo que este hook lê é estruturalmente diferente do que a E1 leu (ver análise
acima). A correspondência com o objecto da E1 apoia-se em dois factos MEDIDOS (o endereço
`piVar8=0x4077AD5C` bate byte a byte com o `h` da E1; o valor `h_child=0x3F800000` é a mesma
classe de corrupção — float de matriz lido como ponteiro — já provada pela E1/E2 noutro campo
do MESMO objecto) e uma INFERÊNCIA não verificada directamente nesta sessão (que `piVar8+4`,
não lido aqui, seria `0x40030001`). Esta inferência é razoável e está declarada como tal, mas
o `11-03` deve tratá-la como não-100%-fechada e, se o custo for baixo, confirmar `piVar8+4`
antes de escrever o fix B — o item nº1 do "Próximo experimento" abaixo.

Isto não é o cenário "os dados não estão assentes" que o plano manda tratar como A por
omissão (`D-11.2`/`D-11.5`): a CONTAGEM está assente (1, determinística, 3/3) — o que não está
100% fechado é a IDENTIDADE exacta do único objecto contado, e a evidência disponível (endereço
+ classe de corrupção) aponta consistentemente para o mesmo objecto que a E1 já isolou, não
para um objecto diferente. Por isso a decisão fica **B**, não a posição conservadora de A —
mas com a ressalva escrita para o `11-03` decidir se quer gastar uma leitura extra antes de
escrever o fix.

---

## O que esta sessão NÃO afirma

- **Não** afirma ter lido `piVar8+4` directamente — a correspondência com o `w0=0x40030001` da
  E1 é inferida do endereço batido + da classe de corrupção, não medida nesta sessão. Ver
  "Próximo experimento" nº1.
- **Não** afirma que uma janela mais longa que 90 s não revelaria uma SEGUNDA chamada ao hook
  (`calls=2`). As corridas D (60 s) e E/F (90 s) deram todas `calls=1`, o que é um sinal de
  estabilidade, mas nenhuma corrida desta sessão excedeu 90 s.
- **Não** afirma que `+0x88` cai literalmente dentro da matriz de transformação do WAD — isso é
  inferido por proximidade a `+0x7C`/`+0x80` (já provados pela E1/E2), não desmontado de novo
  nesta sessão.
- **Não** afirma que a explosão de 179 459 343 iterações que a E1 mediu (dentro do push) e o
  `calls=1` que este censo mediu (no walk exterior) são o MESMO instante da mesma corrida —
  são sessões diferentes, dias diferentes; o que se afirma é que ambas apontam para objectos no
  MESMO endereço heap com a MESMA assinatura de corrupção.
- **Não** corrigiu nada. Nenhum `ppu_recomp_*.cpp` foi editado à mão, nenhum patch de fix foi
  escrito (isso é trabalho do `11-03`), a produção (`recomp_macos_v2`/`boot_gow2`) **não foi
  tocada**, e o hook é **OFF por default** com a corrida G a provar isso (C3).
- **Não** re-executou os testes offline do `11-02` (plano paralelo, ficheiro não tocado por
  esta sessão).
- **Não** toca em pixels, shaders ou `SHADERSRC`.

---

## Próximo experimento para quem retomar

1. **Ler `piVar8+4` directamente.** Um hook (ou uma extensão deste) que leia `w0_self =
   vm_read32(piVar8+4)` fecharia definitivamente a ressalva do C-known: se `w0_self ==
   0x40030001`, a identidade com o objecto da E1 fica MEDIDA, não inferida. Custo: uma leitura
   extra, um re-lift, uma corrida. É o passo mais barato que falta nesta parede.
2. **Confirmar `+0x88` na matriz.** Desmontar `func_0024C1F8` (o mesmo alvo que a E1/E2 já
   desmontaram para `+0x7C`) e verificar se `+0x88` é `matriz[?][?]` — fecha a inferência da
   secção "Porque o C-known não reproduziu".
3. **`11-03`: escrever o fix B**, filtrando por `subtag == 1` (ou equivalente) em `0x0041FF70`,
   com os 12 tags legítimos medidos pela E1 (`3,4,9,15,16,17,18,20,21,23,25,32`) como teste de
   não-regressão — o `11-02` já escreveu `test_outer_walk_subtag_prune_predicate` para isto
   exacto, offline, antes do fix existir.
4. **Não voltar a perseguir:** correr esta sonda com janelas <60 s (a corrida C já mostrou que
   não chega); usar o script `smoke_intro_macos.sh` com `pkill -f` (viola CLAUDE.md regra 6 —
   preferir sempre kill por PID como esta sessão fez).

---

## Higiene (G6)

| regra | estado |
|---|---|
| Kill por **PID** (`TERM` → `-9`) | **cumprido** — **7 boots** (A–G), nenhum `pkill -f` |
| Órfãos | **0** antes e depois de **cada** corrida (verificado por `pgrep`, **7/7**) |
| Logs | **só em `/tmp`** (`/tmp/e3_run{A..G}.log`), fora dos dois repos |
| Produção | **não tocada** — lift em `recomp_macos_e3probe/`, binário `boot_gow2_e3probe`;
`recomp_macos_v2`/`boot_gow2` (promovidos `3 ago 10:38`) com `mtime` intacto |
| `PS3_NO_RSX=1` | cumprido em **7/7** |
| ≥3 corridas | cumprido — 7 corridas (2 baseline A/B + 1 exploratória curta C + 3 de medição D/E/F + 1 de C3 na mesma janela G) |
| Gates de diagnóstico (1–3) | ligados **só para alcançar o ponto de medição**, declarados em §"Receita e janela", e **nunca** usados como fecho de parede (a decisão vem da CONTAGEM, não dos gates) |
| Ficheiros de outra sessão | `recomp_mid_v2/patch_41f78c_prune_probe.py` (modificado por outra sessão em `../gow2-recomp`) fica **fora** destes commits |
| Ficheiros interditos | `.planning/ROADMAP.md`, `PROJECT.md`, `MILESTONES.md`, os dois `CLAUDE.md`, `config.json`, `scripts/gsd_plan*.sh`, `.planning/milestones/v1.2-*`, `11-02-PLAN.md`, `tools/test_wall4_subtag_fix.py`, `CMakeLists.txt`, `scripts/test_python.sh` — **intactos e fora dos commits desta sessão** |
