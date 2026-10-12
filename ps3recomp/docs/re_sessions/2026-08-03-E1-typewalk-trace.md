# Sessão de RE — sonda **E1**: o trace in-boot do walk de tipos — 2026-08-03

Sessão conduzida com [`docs/RE_PLAYBOOK_GOW2.md`](../RE_PLAYBOOK_GOW2.md). Continuação directa de
[`2026-08-03-mgr20-quem-escreve.md`](2026-08-03-mgr20-quem-escreve.md), que redefiniu o
discriminador: **a distribuição de `low16` entre os filhos que chegam a `0x0041F700`**, e não
o `mgr20_lo16` (que mede o *slot* no array do gestor, não o tag).

Quatro sessões chegaram aqui **sem uma única medição in-boot**. Esta é a primeira que corre.

> **ESTADO: previsões escritas, sonda por implementar.** Este bloco inicial (§*Pergunta*,
> §*Âncoras*, §*Desenho da sonda*, §*As duas previsões*) foi commitado **ANTES** de a sonda
> existir e **ANTES** de qualquer corrida — é o que torna a sonda discriminadora em vez de
> confirmatória. Os resultados entram em secções novas, abaixo; nada acima da linha
> `<!-- LINHA DE CORTE -->` é reescrito depois de correr.

---

## Pergunta (uma frase)

> **Para os filhos que o walk `0x0041F700` visita in-boot, `tab[low16(filho)]` e
> `tab[subtag(filho)]` são o MESMO gestor?**

Se sim, o par push/pop é coerente e a assimetria de indexação não pode ser a causa das
27 539 083 iterações. Se não, o push credita o cursor `+0xC8` de um gestor e o pop debita o de
outro — um sobe sem descer.

---

## Âncoras

| âncora | valor |
|---|---|
| **EA do push** | `0x0041F700` — `.opd.FUN_0041f700 size=296`, `vt+0x40` da vtable do gestor `0x00515AA0` |
| **EA do pop** | `0x0041F924` — `.opd.FUN_0041f924 size=268`, `vt+0x44` da mesma vtable |
| **EA do anel do walk** | `loc_0041F774` (topo do laço) … `0x0041F7D8` (saída) |
| **EA ancorado pela sonda** | **`0x0041F78C`** — `540913BA rlwinm r9,r0,2,14,29` (`low16*4`), a instrução que as três sessões anteriores publicaram ao byte |
| **tabela de tipos** | `0x00868D48` (`.bss`), alcançada por `lwz r28,-0x225C(r2)` |
| **desserializador** | `0x00254C40` (`vt+0xA0` da classe `0x00515B08`) — força `low16 := 1` |
| **tag de log** | **NOVA**: `[PS3T] … | CHECKPOINTS | TYPEWALK_* | …` (a parede deixa de estar sem instrumentação própria) |
| **gate** | `PS3_TYPEWALK_TRACE=1`, **OFF por default** |

### O estado dos registos em `0x0041F78C` (lido do lift de produção, `ppu_recomp_001.cpp`)

O anel, instrução a instrução a partir de `loc_0041F774` (o EA de cada uma sai da contagem
+4 e **bate com o `0x0041F78C` publicado**, o que fixa a ancoragem):

```
0x0041F774  rldicl r11,r9,0,32      ; r11 = nó corrente da lista
0x0041F778  lwz    r9,8(r11)        ; r9  = nó[2] = h do FILHO
0x0041F77C  rldicl r10,r9,0,32      ; r10 = h(filho)
0x0041F780  addi   r4,r9,4          ; r4  = q(filho) = h+4  (o arg do despacho)
0x0041F784  rldicl r4,r4,0,32
0x0041F788  lwz    r0,4(r10)        ; r0  = w0 do FILHO
0x0041F78C  rlwinm r9,r0,2,14,29    ; <== ÂNCORA (hook ANTES desta instrução)
0x0041F790  rldicl r0,r0,48,52      ; subtag(filho)
0x0041F794  cmpw   r0,r29           ; guarda: subtag(filho) != subtag(pai)?
0x0041F798  beq    loc_0041F7C4
0x0041F79C  lwzx   r11,r28,r9       ; tab[low16(filho)]
```

Logo, **antes** de `0x0041F78C`:

| registo | conteúdo |
|---|---|
| `r0` | `w0` do filho, cru |
| `r10` | `h(filho)` |
| `r4` | `q(filho)` |
| `r3` | `this` — o gestor cujo cursor `+0xC8` esta chamada de push **já incrementou** |
| `r28` | base da tabela de tipos (**tem de ser `0x00868D48`** — sonda de controlo) |
| `r29` | `subtag(pai)` |
| `r30` | `h(pai) + 0x80` (a sentinela da lista de filhos) |
| `r31` | o nó de lista corrente |

Tudo o que a sonda precisa está vivo neste ponto — **e ainda antes da guarda de poda**, o que
faz a amostra cobrir *todos* os filhos visitados, não só os despachados.

### O que o push e o pop fazem ao cursor (lido do C do Ghidra, os dois)

| | `0x0041F700` (push) | `0x0041F924` (pop) |
|---|---|---|
| cursor `+0xC8` de `this` | `++` e guarda `this[0x48 + cur*4] = h(nó)` | `--` e devolve `this[0x48 + cur*4]` (`cur<0` ⇒ devolve **0**) |
| recorre em | `tab[low16(filho)]->vt[0x40]` | `tab[subtag(filho)]->vt[0x44]` |
| guarda | `subtag(filho) != subtag(pai)` | `subtag(filho) != subtag(pai)` |

⇒ a pergunta desta sessão é exactamente *"os dois lados escolhem o mesmo gestor?"*.

**Nota de risco estrutural, já visível estaticamente:** `0x48 + cur*4 == 0xC8` para
`cur == 32`. Um push a profundidade 32 escreve **por cima do próprio byte do cursor**. Por isso
a sonda mede `cur_max`.

---

## Desenho da sonda — e porque é assim

**Mecanismo: `[[midasm_hook]]`.** É a escolha da sessão anterior e mantém-se: a sonda tem de
correr **ao lado** da instrução, sem substituir nada. Um `[[functions_override]]` obrigaria a
reimplementar 296 bytes de PPC só para observar — precisamente a divergência que se quer evitar.

**Dois hooks:**

| nome | EA | papel |
|---|---|---|
| `TypewalkPushEnter` | `0x0041F700` | **E3, a sonda de CONTROLO obrigatória**: conta entradas em push. Sem ela, um silêncio não distingue *"não passou aqui"* de *"a sonda não foi compilada"* |
| `TypewalkPushChild` | `0x0041F78C` | a medição |

### Volume — o problema real, e a decisão escrita

27,5 M iterações × 1 linha = um log impossível de ler e ~3 GB de stderr. **Uma linha por visita
está fora de questão.** A decisão, e a razão de cada parte:

| camada | limite | porquê |
|---|---|---|
| **cabeça crua** | as **primeiras 48 visitas**, uma linha cada | os nós concretos do início do walk, para poder ser lido a olho e comparado com o estático |
| **uma linha por CHAVE NOVA** | chave = `(low16, subtag)`, **até 192 chaves** | **é isto que responde à pergunta.** A distribuição de `low16` é um conjunto de pares distintos, não uma sequência: cada par novo é reportado UMA vez, com o registo completo. Um walk de 27,5 M visitas sobre um grafo de dezenas de tipos tem **poucas dezenas** de pares distintos — logo esta camada é *completa*, não amostrada |
| **agregado periódico** | nas visitas 1, 1 000, 100 000 e depois a cada 2²² (≈4,2 M) | dá as **contagens** e a evolução dos cursores. É emitido periodicamente e não no fim **de propósito**: o boot é morto por `kill -TERM` (G6), que não corre `atexit` — um relatório final nunca chegaria a sair |

Volume previsto: 48 + ≤192 + ~10 dumps × ≤24 linhas ≈ **≤ 500 linhas**. Legível.

Agregação por chave, em tabela de 512 slots com sondagem linear (`count`, `prune`, primeiro
`w0`/`h`/`slot16`/`own_idx`, `hflags` acumulado em OR e AND, `mgr_lo`/`mgr_st` primeiros +
bit de "mudou", `cur_lo`/`cur_st` min e max, máscara dos nibbles de flags vistos).

### O que sai por visita/chave

| campo | expressão | porquê |
|---|---|---|
| `w0` | `r0` (já lido pelo guest — **zero leituras extra**) | a palavra crua |
| `low16` / `subtag` | `w0 & 0xFFFF` / `(w0>>16)&0xFFF` | **o discriminador** |
| `flags` | `w0 >> 28` | `0x4` = ctor-base; `0x8`/`0xC` = passou pelo clone `0x00252ED8` |
| `slot16` | `vm_read16(h + 0x0A)` | o campo que os one-liners escrevem (substitui o `mgr20_lo16`) |
| `own_idx` | `vm_read32(h + 0x20)` | índice no array do gestor; `-1` = nunca inserido |
| `hflags` | `vm_read16(h + 8)` | o bit `4` que `0x002545B0` respeita e o push **não** consulta |
| `mgr_lo` / `mgr_st` | `vm_read32(tab + low16*4)` / `vm_read32(tab + subtag*4)` | **os dois gestores** |
| `same` | `mgr_lo == mgr_st` | **a resposta P1/P2 em um bit** |
| `cur_lo` / `cur_st` | `(int8_t)vm_read8(mgr_lo + 0xC8)` / `… mgr_st + 0xC8` | **os dois cursores** — a assimetria a olho |
| `pai_subtag`, `poda` | `r29`, `subtag == r29` | qual dos filhos é sequer despachado |

Índices `>= 0x21` (fora do alcance `low16 < 0x21` que `0x00254250` verifica e o push não) são
lidos até `0x100` e **marcados `oor=1`**; acima disso o gestor sai a `0` e a linha diz porquê.
Não se inventa um valor para um índice que o binário nunca prometeu.

### G2 — OFF por default

Com `PS3_TYPEWALK_TRACE` por definir: **zero bytes** em stderr e **zero mudança de
comportamento**. O corpo do hook lê um `int` cacheado e devolve. A prova é uma corrida do
binário da sonda com o gate OFF, comparada com a produção — não a afirmação.

<!-- LINHA DE CORTE -->

---

## As duas previsões — escritas ANTES de correr

### P1 — `tab[low16] == tab[subtag]` para **todos** os nós visitados

- `same=1` em 100 % das chaves e em 100 % das visitas;
- `cur_lo` e `cur_st` são o **mesmo** cursor, logo sobem e descem juntos e ficam em `[-1, 32)`;
- ⇒ **o par push/pop é coerente.** A assimetria de indexação medida ao byte pelas sessões
  anteriores existe no binário mas **não se manifesta** neste conjunto de nós — porque nestes
  nós `low16` e `subtag` levam ao mesmo sítio (ou por serem iguais, ou por a tabela ter o mesmo
  ponteiro nos dois slots; a sonda distingue os dois casos e é preciso dizer qual);
- ⇒ **o alvo passa a ser o bit `hflags & 4`** — a poda por objecto que `0x002545B0` respeita,
  que `0x0039D764`/`0x0041BF00`/`0x00254954` põem, e que `0x0041F700` **não consulta**.

### P2 — algum nó dá `tab[low16] != tab[subtag]`

- aparece ≥1 chave com `same=0` e os dois gestores não-nulos e diferentes;
- `cur_lo` do gestor do push **sobe monotonicamente** (uma vez por nó) enquanto o `cur_st` do
  gestor do pop não acompanha;
- ⇒ **é a causa**: o push credita um cursor e o pop debita outro; um gestor sobe sem descer, e
  a `cur_max` cruza 32 (onde `0x48 + 32*4 == 0xC8` **sobrescreve o próprio cursor**);
- ⇒ o fix indicado é fazer o pop indexar pelo **mesmo campo que o push** (`low16`), fiel ao
  desenho `low16` = *"quem me trata"* / `subtag` = *"o que eu sou"* que `0x00254C40` grava.
  Forma: **weak override** de `0x0041F924` (substitui a função inteira — 268 bytes — porque o
  índice é usado nas duas pontas do corpo, o que um mid-asm não consegue reescrever).

**Se o resultado não for P1 nem P2, di-lo.** Casos concretos que não são nenhuma das duas e que
têm de ser reportados como tal, não forçados a um dos lados:

- o push **nunca é alcançado** na corrida (contador E3 a zero) ⇒ as 27,5 M iterações do rasto
  vêm de outra corrida/receita e a moldura inteira precisa de ser refeita;
- `mgr_lo` ou `mgr_st` **nulo** (slot da tabela vazio) ⇒ não é "gestor diferente", é gestor
  inexistente, e a pergunta passa a ser quem devia ter registado aquele slot;
- `same=1` mas `cur_max >= 32` ⇒ a explosão é real e o cursor transborda **sem** assimetria
  nenhuma — refuta P2 *e* enfraquece o consolo de P1.

---

## Sondas de CONTROLO (a correr ANTES de confiar em qualquer número)

A regra que salvou as cinco sessões anteriores. Um trace novo não é de confiança até reproduzir
um número **já medido por outro caminho**.

| # | sonda | o que TEM de dar |
|---|---|---|
| **E3** | contador de entradas em `0x0041F700` (hook próprio, `TypewalkPushEnter`) | **> 0**. Um silêncio sem esta linha não distingue "não passou" de "não compilou" |
| **C1** | `r28` (a base da tabela lida pelo próprio guest) | **`0x00868D48`** — o valor medido in-boot e registado no ROADMAP, e o mesmo que o ELF dá em `PTR_DAT_0053ef1c` |
| **C2** | algum `w0` de pai ou filho igual a **`0x40030001`** | o valor medido na parede 1 e reproduzido estaticamente pelo desserializador (`(3<<16)|1` + `0x40000000`) |
| **C3** | gate OFF ⇒ **0 linhas** `TYPEWALK` e métricas de boot não piores que a produção | G2, e a prova de que a sonda não é o que muda o jogo |

Sem **E3 + C1** verdes, nenhum resultado desta página conta.

---
---

# RESULTADOS — a sonda correu

> **ESTADO: E1 CORRIDA.** Tudo abaixo desta linha foi escrito **depois** das corridas. Nada
> acima dela foi tocado — as duas previsões estão no commit `af5a6cb`, anterior à existência
> da sonda.

## Veredicto em uma linha

> **P1 — mas P1 ganha o critério e perde a conclusão.** `tab[low16] == tab[subtag]` em
> **100 % de ~885 milhões de visitas** (`diff_mgr=0` em 6 corridas), e push/pop estão
> **equilibrados** (176 vs 175, cursor nunca negativo, profundidade ≤ 2). **Zero nós divergem.**
> Mas o alvo que P1 previa — o bit `hflags & 4` — está **também refutado**: o que faz 99,96 %
> do rasto **não é recursão nenhuma**. É **um único laço de irmãos que não termina**, dentro
> de **uma única** chamada de push, porque a cabeça da lista é **`0x00000000`** — e o nó
> repete-se `no_rep = n-1` vezes, medido. O objecto que a produz está identificado ao
> endereço: **`h=0x4077AD5C`, `w0=0x40030001`** — **o registo WAD da parede 1**.
>
> **As paredes 1 e 4 são o MESMO defeito.** O fix não é no push nem no pop.

---

## Sondas de CONTROLO — o que deram

| # | sonda | resultado | nível |
|---|---|---|---|
| **E3** | entradas em `0x0041F700` | `TYPEWALK_ALIVE` na 1.ª chamada; `enters=101` (base) / `enters=176` (com gates). **> 0** ✅ | in-boot |
| **C1** | `r28` == `0x00868D48` | **`tab=0x00868D48 tab_ok=1 tab_bad=0`** em **todas** as corridas ✅ — o valor medido in-boot e no ELF | in-boot |
| **C2** | reproduzir `0x40030001` | ✅ **`paiw0=0x40030001`** — e não num filho qualquer: é o `w0` do **pai** dos dois nós que fazem a explosão. `w0_40030001=2` em 3/3 corridas | in-boot |
| **C2b** | *(não previsto, apareceu)* o conjunto de tags dos filhos | as 12 chaves medidas são `{3,4,9,15,16,17,18,20,21,23,25,32}` — **exactamente** o conjunto que o `ROADMAP.md` §Phase 11 publicou, e o pai é tag 1 ✅ | in-boot |
| **C2c** | *(não previsto)* os dois objectos do walk exterior | a nota de 2026-08-02 registou `0x40638594` (`w0=0xC0010001`) e `0x4077AD28` (`w0=0x40030001`). Medi **`h=0x40638588`** (`q=0x4063858C`, `w0=0xC0010001`) e **`h=0x4077AD5C`** (`w0=0x40030001`) ✅ | in-boot |
| **C3** | gate OFF ⇒ zero bytes, zero mudança | **0 linhas `TYPEWALK`** em 3/3; `st620=3`, `INTROSEQ=7`, `ICALL-BAD=0`, `OOB=0`, `FATAL=0` — **idênticos** aos da produção em 3/3 ✅ | in-boot |

**C1 + C2 + C2b + C2c juntas** são quatro números medidos por outras sessões, reproduzidos
por esta sonda numa corrida. É o que autoriza confiar no resto da página.

---

## Ferramentas (comando + rc)

| # | o quê | rc |
|---|---|---|
| T1 | `ppu_lifter.py … --config …/gow2_recomp.toml -o ./recomp_macos_e1probe -j 4` | 0 — `4 midasm_hook(s) loaded`, 51 991 funções, 7 chunks, 0 stubs |
| T2 | verificação da **ancoragem** no lift novo | a chamada `gow2_midasm_TypewalkPushChild(ctx);` sai **imediatamente antes** de `ppc_rlwinm(…,2,14,29)`, dentro de `loc_0041F774` — o `0x0041F78C` publicado ao byte por três sessões |
| T3 | `apply_all_patches.sh ./recomp_macos_e1probe --status` | rc=1, **`TOTAL: 140 \| APPLIED=118 ALREADY=5 NO-MATCH=0 UNVERIFIED=14 FAILED=3`** — e `diff` do TSV (patch/estado) contra `/tmp/promo2/p1_status.tsv` da **promoção de hoje**: **idêntico**. O `rc=1` e o `NOVO patch_freelist_guard_262610.py` estão **byte a byte** em `/tmp/promo2/step4_p1.log:943` — **pré-existentes, não desta sonda** |
| T4 | `verify_lift.sh` no lift da sonda **e** na produção | **rc=0 nos dois** |
| T5 | `build_macos.sh ./recomp_macos_e1probe` com `OUT=./boot_gow2_e1probe` | 0 — `nm` dá `T _gow2_midasm_TypewalkPushEnter`, `T _gow2_midasm_TypewalkPushChild`, `T _gow2_midasm_TypewalkPop` |
| T6 | **23 boots** (G6, kill por PID, `orfaos=0` em **todos os 23**) | ver tabelas. **3 dos 23 apanharam o flake conhecido da intro** (`st620 1 -> 1` em ciclo, o push nunca e' alcancado, `TYPEWALK=0`): `e1_gates_no_3`, `e1_caller_1`, `e1_caller_3`. **Sao declarados, nao descartados em silencio** -- 3/20 das corridas com gate ON, consistente com os ~33% medidos a 2026-08-03 |
| T7 | `ghidra_lookup.py` nos 4 chamadores + desmontagem própria de `0x002B2DD0` | 0 |

**A produção não foi tocada.** Lift em `recomp_macos_e1probe/`, binário `boot_gow2_e1probe`;
`recomp_macos_v2` e `boot_gow2` intactos (a promoção de hoje às 10:38 está de pé). Logs em
`/tmp/e1_*.log`, fora dos dois repos.

---

## O que a sonda mediu

### Receita de base (gates de diagnóstico OFF) — 3/3 corridas **idênticas**

```
enters=101  visits=1176  pops=100  pops_vazios=0
chaves=12   same_mgr=1176  diff_mgr=0  same_idx=1176  mgr_null=0
tab=0x00868D48  tab_ok=1  tab_bad=0   this_cur=-1..0
```

O walk **termina**, é pequeno, e o push e o pop estão **equilibrados** (101 vs 100 — a
diferença é o push exterior que ainda não desempilhou no instante da amostra). **Nenhuma
explosão nesta receita.**

### Com os gates de diagnóstico 1–3 ligados — 6 corridas

Ligados **só para comparação**, como o `ROADMAP.md` §Phase 11 os declara
(`PS3_LIST254_EMPTY_IF_NULL`, `PS3_POOL_NULL_IF_BAD`, `PS3_LIST547_EMPTY_IF_BAD`). São eles
que deixam o boot entrar no loop principal — e é **aí**, e só aí, que a explosão aparece.
**Isto está declarado, não escondido: a explosão é medida com gates, e um gate NUNCA fecha
uma parede** (anti-padrão 7). Os gates **não tocam** em `0x0041F700`, que continua sem gate.

| corrida | `visits` | `enters` | `pops` | `chaves` | `diff_mgr` | `no_rep` | `this_cur` |
|---|---:|---:|---:|---:|---:|---:|---|
| 1 | 192 937 984 | 176 | 175 | 14 | **0** | — | −1..0 |
| 2 | 192 937 984 | 176 | 175 | 14 | **0** | — | −1..0 |
| 3 | 192 937 984 | 176 | 175 | 14 | **0** | — | −1..0 |
| 4 | 138 412 032 | 176 | 175 | 14 | **0** | 137 620 941 | −1..0 |
| 5 | 180 355 072 | 176 | 175 | 14 | **0** | 178 565 679 | −1..0 |
| 6 | 188 743 680 | 176 | 175 | 14 | **0** | 184 515 197 | −1..0 |

`visits` varia com a janela de 60 s; **tudo o resto é constante**. `pops_vazios=0` e
`tab_bad=0` em 6/6.

### As 14 chaves — e é aqui que a parede cai

**12 chaves legítimas**, o grafo de tipos, uma linha cada (extracto):

```
low16=0x0012 subtag=0x012 same_idx=1 w0=0xC0120012 h=0x42F86AD8 mgr_lo=mgr_st=0x400D7338 n=172 poda=0 cur=-1..0
low16=0x0015 subtag=0x015 same_idx=1 w0=0xE0150015 h=0x42F85AE0 mgr_lo=mgr_st=0x401002F0 n=172 poda=0 cur=-1..0
… (tags 3,4,9,15,16,17,18,20,21,23,25,32 — todos n=172, poda=0, low16==subtag)
```

**12 × 172 = 2 064 visitas.** É *tudo* o que o grafo de tipos contribui. **Bounded, são, e
com `low16 == subtag` em todos.**

**2 chaves de lixo** — e são **99,7 %** do rasto:

```
low16=0x0000 subtag=0x000 same_idx=1 w0=0x00000000 h=0x00000040
    mgr_lo=mgr_st=0x4003ECB8 cur_lo=121..121 hflags=0xFFFF slot16=65535 own_idx=0
    n=179 459 343  poda=0  no=0x2F725F7C..0x00000000  no_rep=179 459 342
    pai=0x4077AD1C  paiw0=0x40030001  pai_st=0x003

low16=0x5F7C subtag=0xF72 same_idx=0 w0=0x2F725F7C h=0x00000000
    mgr_lo=mgr_st=0x00000000 oor=2 cur=-128..-128
    n=893 665  poda=0  no=0x00000000..0x00000000  no_rep=893 665
    pai=0x4077AD1C  paiw0=0x40030001  pai_st=0x003
```

**`no_rep = n − 1`.** O nó de lista **não avança**: fica em `0x00000000` e repete-se em
**todas** as iterações menos a primeira.

### E o chamador — 176 pushes, e **um** é diferente

Os 176 `enters` de uma corrida, um por linha (o cap é 200, logo é **completo**, não amostrado):

| `lr` do chamador | nº | `w0` do nó |
|---|---:|---|
| `0x0042AF5C` (em `FUN_0042af18`) | 169 | `0xC0010001` |
| `0x0042CEAC` (em `FUN_0042cce8`) | 4 | `0xC0010001` |
| `0x002B0DDC` (em `FUN_002b0db0`) | 2 | `0xC0010001` |
| **`0x002B2DE0`** (em **`FUN_002b2dd0`**) | **1** | **`0x40030001`** |

E a linha dessa única chamada:

```
n=176 lr=0x002B2DE0 this=0x400C5048 arg=0x4077AD60 h=0x4077AD5C w0=0x40030001
      cur=-1 sent_head=0x00000000
```

**`sent_head = vm_read32(h + 0x80) = 0x00000000`.** A cabeça da lista de filhos **é zero**.
Nas outras 175 chamadas é sempre um EA válido (`0x42F853B0`, `0x40638608`, `0x40007DE4`, …).

Desmontagem própria do chamador, do ELF:

```
0x002B2DD0  F821FF91  stdu r1,-112(r1)
0x002B2DD4  7C0802A6  mflr r0
0x002B2DD8  F8010080  std  r0,128(r1)
0x002B2DDC  4BFFF885  bl   0x002B2660      <-- retorno = 0x002B2DE0, o lr MEDIDO
0x002B2DE0  60000000  nop
```

E `FUN_002b2660` despacha `vt+0x0C` sobre `*(PTR_DAT_0053fcd8 + 0x14)` — e **`+0x0C` é o
walk exterior `0x0041FF70`**, o achado da 1.ª sessão. A cadeia fecha:

```
FUN_002b2dd0 --bl--> FUN_002b2660 --vt+0x0C--> func_0041FF70 (walk exterior)
             --vt+0x40--> func_0041F700(this=0x400C5048, q=0x4077AD60)
                          h=0x4077AD5C  w0=0x40030001  h+0x80 = 0
```

*(O `lr` continua `0x002B2DE0` lá dentro porque o despacho a jusante é tail — `bctr` como
salto, `ps3_indirect_tail`. O que está **medido** é o `lr` guest à entrada do push; a
atribuição do salto tail é leitura, não medição.)*

---

## O mecanismo, medido de ponta a ponta

1. O walk exterior `func_0041FF70` enumera os objectos registados e chama o push (`vt+0x40`)
   em cada um. **175 deles são da família "listas" (`w0=0xC0010001`)** e produzem o walk
   pequeno e são: 12 filhos, `low16 == subtag`, profundidade ≤ 2, push e pop equilibrados.
2. **Um deles é um registo WAD (`w0=0x40030001`, `h=0x4077AD5C`).**
3. O push lê `h + 0x80` (= `q + 0x7C`) como cabeça da lista intrusiva de filhos. **Para um
   registo WAD, `+0x7C` é `matriz[0][3]`** — provado estaticamente na parede 1 desmontando
   `func_0024C1F8`. Medido aqui: **`sent_head = 0x00000000`**.
4. O laço `for (no = *(h+0x80); no != h+0x80; no = *no)` arranca em `no = 0`. Lê
   `*0 = 0` ⇒ **`no` fica em `0` e nunca atinge a sentinela `h+0x80`**. Medido:
   `no_rep = n − 1`, `no=0x00000000..0x00000000`.
5. Cada iteração lê `h(filho) = vm_read32(0 + 8)` — memória guest baixa, lixo que outras
   threads mexem: ora `0x40` (⇒ `w0 = vm_read32(0x44) = 0`), ora `0` (⇒
   `w0 = vm_read32(4) = 0x2F725F7C`, que em ASCII é `/r_|`, fragmento de um caminho).
6. A guarda `subtag(filho) != subtag(pai)` **não poda** (`poda=0`: `0 != 3`), logo despacha
   `tab[0] = 0x4003ECB8` → `vt+0x40` desse gestor — que **não é** `func_0041F700`
   (`enters` fica em 176). **Não há recursão.** Profundidade máxima medida: **2**
   (`this_cur = -1..0`).

⇒ **99,96 % do rasto é um `for` que não termina, numa única chamada.**

---

## O que fica REFUTADO por medição in-boot

1. **P2 — "algum nó dá `tab[low16] != tab[subtag]`".** **FALSO.** `diff_mgr = 0` em 6
   corridas, ~885 M de visitas somadas. **Zero nós divergem.** *(in-boot)*
2. **"Um gestor sobe sem descer; o cursor `+0xC8` transborda os 127."** **NÃO OBSERVADO
   nestas corridas.** `enters=176` vs `pops=175`, `pops_vazios=0`, `this_cur = -1..0`. O
   cursor de `this` **nunca** passou de 0. *(in-boot)*
   **Ressalva medida:** existe um gestor com o cursor **em 121** — `tab[0] = 0x4003ECB8`,
   constante em `121..121`. **Não é esta chamada que o lá pôs** (o despacho para `tab[0]`
   não reentra no push), e não foi investigado.
3. **"Recursão exponencial: o grafo de tipos é completo, 12⁷ ≈ 35 M"** (`ROADMAP.md`
   §Phase 11 e a nota de 2026-08-02). **REFUTADO in-boot.** Não há recursão profunda: **176
   pushes no total, profundidade máxima 2**, e o grafo de tipos contribui **2 064** visitas
   (12 chaves × 172). A ordem de grandeza batia por coincidência aritmética. *(in-boot)*
4. **"27 539 083 passagens contra 2 088 pops ⇒ falta um pop."** **A desproporção não é a que
   parecia.** Nesta corrida são **176 pushes contra 175 pops** — equilibrados. O número
   grande **não conta chamadas de push**: conta **iterações de um laço** dentro de uma. Um
   pop a mais não mudaria nada. *(in-boot)*
5. **A conclusão de P1 — "o alvo passa a ser o bit `hflags & 4`".** **REFUTADA.** Nenhum bit
   de poda por objecto pára um `for` que gira sobre `no = *no = 0` e nunca chega à sentinela:
   a poda decide **se se desce**, não **quando o laço acaba**. E o `hflags` do nó explosivo é
   `0xFFFF` (lido de um `h` inválido) — não é sequer um valor. *(in-boot)*

---

## Veredicto: **P1 no critério; a parede 4 DISSOLVE-SE na parede 1**

O discriminador escrito antes de correr era *"`tab[low16] == tab[subtag]`?"*. A resposta é
**sim, sempre** — logo **P1**. **Nós que divergem: zero.**

Mas a sonda mediu mais do que o discriminador pedia, e o que mediu **fecha a parede 4 como
parede autónoma**: ela não é um defeito de indexação, nem de recursão, nem de poda. É
**consequência directa** de um registo WAD (`w0=0x40030001`) ser entregue ao walk de tipos
com o `+0x7C` a ser lido como cabeça de lista quando é `matriz[0][3]` — **exactamente e
literalmente a parede 1**. A sessão de 2026-08-02 tinha escrito *"são irmãs, não causa e
efeito"*; a medição corrige isso: **é a mesma causa, vista de dois consumidores**.

---

## Onde vai o fix, e de que forma

**NÃO vai em `0x0041F700` nem em `0x0041F924`.** Está medido que a indexação dos dois está
coerente e que o par está equilibrado; mexer lá seria corrigir divergindo do CELL (regra 8)
sobre um sintoma, com o dado a dizer que o defeito é a montante.

| | alvo | forma | razão escrita |
|---|---|---|---|
| **o fix** | **quem põe `h=0x4077AD5C` (`w0=0x40030001`) no conjunto que `func_0041FF70` enumera** — ou quem lhe carimba `low16 = 1`, que é o que o faz despachar para o gestor de listas | **weak override** (`[[functions_override]]`) | o fix **envolve** o corpo de quem regista/enumera; um mid-asm corre ao lado de uma instrução e não pode remover um objecto de uma enumeração. É a mesma forma que a parede 1 vai precisar — e é o **mesmo** fix |
| **não é** | `[[midasm_hook]]` no push com um guard `if (sent_head == 0) return;` | — | é o **anti-padrão 4** (null-check cosmético) e o **7** (gate a fechar parede). Silencia o sintoma e perde a prova de que o objecto errado chegou ali |
| **não é** | trocar `tab[subtag]` por `tab[low16]` no pop | — | **refutado por medição**: `diff_mgr=0`. Não há nada para trocar |

**A próxima medição, e é pequena:** o mesmo `TypewalkPushEnter` já dá `arg` e `lr`. Falta
**quem regista `0x4077AD5C`** — um hook no registador `0x0041FA90` (`vt+0x08`) a filtrar
`w0 == 0x40030001` nomeia o produtor numa corrida. É o passo directo desta página.

---

## O que esta sessão NÃO afirma

- **Não** afirma que a explosão acontece **sem** os gates de diagnóstico 1–3. Com a receita
  de base, 3/3 corridas dão `visits=1176` e o walk **termina**. A explosão foi medida **com**
  os gates, que são o que faz o boot chegar ao loop principal — e isso está declarado.
  Fechar isto exige as paredes 1–3 corrigidas, não um gate.
- **Não** afirma que os `27 539 083` da medição de 2026-08-01/02 e os `~185 000 000` desta
  são o mesmo número. São receitas e janelas diferentes. O que afirma é que **têm o mesmo
  mecanismo**: um laço que não termina numa única chamada de push.
- **Não** afirma que `mgr_lo == mgr_st` foi *testado* para a chave `low16=0x5F7C
  subtag=0xF72`. Os dois índices são `≥ 0x100` e a sonda **recusa-se a ler** aí (`oor=2`,
  ambos devolvem 0). Para essa chave o discriminador está **por testar**, não confirmado —
  e é por isso que o `same_mgr=1` dela **não conta** para o veredicto. Conta o resto.
- **Não** afirma ter provado que o despacho a jusante de `FUN_002b2660` é tail. O que está
  medido é o `lr` guest à entrada do push (`0x002B2DE0`) e o `bl 0x002B2660` em
  `0x002B2DDC`; a ligação entre os dois é leitura do binário.
- **Não** afirma saber porque é que `tab[0]` (`0x4003ECB8`) tem o cursor em **121**. Fica
  registado — é o único valor perto da saturação dos 127 que a sonda viu.
- **Não** afirma que `0x4077AD5C` é o **mesmo objecto** que o `0x4077AD28` da nota de
  2026-08-02. É a mesma família, a mesma região do heap e **o mesmo `w0`**; o endereço exacto
  varia entre corridas (medi `0x4077AD1C` e `0x4077AD5C` em corridas diferentes).
- **Não** esconde as 3 corridas que não chegaram ao walk. São o flake conhecido da intro
  (~33 %, medido a 2026-08-03 na promoção), estão nomeadas em §*Ferramentas* e **não** foram
  substituídas por outras até dar — cada bloco de corridas está inteiro.
- **Não** corrigiu nada. Nenhum `ppu_recomp_*.cpp` foi editado à mão, nenhum patch foi
  alterado, a produção (`recomp_macos_v2` / `boot_gow2`, promovidas hoje às 10:38) **não foi
  tocada**, e os hooks são **OFF por default** com C3 a prová-lo.
- **Não** toca em pixels, shaders ou `SHADERSRC`.
- **Não** declara a parede 1 fechada. Declara que a parede **4** deixa de ser autónoma.

---

## Próximo experimento para quem retomar

1. **Quem regista `w0=0x40030001` no conjunto do walk exterior.** Hook em `0x0041FA90`
   (`vt+0x08`, o registador) filtrado por `w0 == 0x40030001`, a emitir `lr` + `this`. Uma
   corrida nomeia o produtor. É o alvo do fix.
2. **Porque é que o `low16` desse registo é `1`.** A sessão de 2026-08-03 provou que
   `0x00254C40` força `low16 := 1` em **todo** registo do stream com `subtag != 1`. Se o
   despacho correcto para um registo WAD **não** é `tab[1]`, então ou o desserializador não
   devia ser quem o produz, ou o walk exterior não devia enumerá-lo. **Duas hipóteses
   exclusivas, e a #1 acima separa-as.**
3. **A parede 1 com esta sonda ligada.** `func_002545B0` consome a **mesma** lista; com o
   `sent_head=0` já medido, prever o que ela vê ali é barato e verifica-se numa corrida.
4. **O cursor 121 de `tab[0]`.** Não investigado. Único candidato a saturação dos 127 visto
   in-boot.
5. **Não voltar a perseguir:** recursão exponencial, falta de pop, assimetria push/pop,
   `hflags & 4` como poda que faltava. **Os quatro estão refutados in-boot nesta página.**

---

## Higiene (G6)

| regra | estado |
|---|---|
| Kill por **PID** (`TERM` → `-9`) | **cumprido** — **23 boots**, nenhum `pkill -f` |
| Órfãos | **0** antes e depois de **cada** corrida (verificado por `pgrep`, **23/23**) |
| `timeout(1)` | **não usado** (não existe no macOS) |
| Logs | **só em `/tmp`** (`/tmp/e1_*.log`), fora dos dois repos |
| Produção | **não tocada** — lift em `recomp_macos_e1probe/`, binário `boot_gow2_e1probe` |
| `PS3_NO_RSX=1`, `PS3_RSX_BACKEND=trace`, `PS3_ENGINE_ROOT` explícito | **cumprido** em **23/23** |
| ≥3 corridas | **cumprido** — 1 exploratória + 3 de base + 9 com gates + 4 de chamador + 3+3 de C3 = **23** |
| Ficheiros de outra sessão | `recomp_mid_v2/patch_41f78c_prune_probe.py` (`M`) fica **fora** destes commits |
| Ficheiros interditos | `.planning/ROADMAP.md`, `PROJECT.md`, `MILESTONES.md`, os dois `CLAUDE.md`, `config.json`, `scripts/gsd_plan*.sh`, `.planning/milestones/v1.2-*` — **intactos e fora dos commits** |
| Gates de diagnóstico | ligados **só na comparação**, declarados em §*Com os gates*, e **nunca** como fecho de parede |
