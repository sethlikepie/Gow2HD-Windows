# A divergência está na cadeia FIOS — o guest nunca pede o 2.º movie

**Data:** 2026-07-31 · **Método:** comparação de logs entre 5 corridas do binário bom e
3 do mau, com normalização e diff de sequências de eventos.
**Isto é o achado mais preciso desta investigação.** Não é a causa nomeada em
ficheiro:linha, mas localiza a divergência num mecanismo concreto.

## Como se chegou aqui

Depois de o bisect não convergir (nove candidatos eliminados, método inaplicável à
janela), atacou-se pelo lado que não depende de saber que commit separa os dois binários:
**comparar directamente uma execução boa e uma má**.

## A observação, com sinal não-gated

O `[movieio] open` **não tem um único `getenv`** — não é instrumentação opcional, é
comportamento. E é categórico:

| | ficheiros que o guest abre |
|---|---|
| **bom** (`pre_v3`) | `gow2.psarc` · **`/_movies/smlogo_v2.m2v`** · **`/wad/r_lglsca.wad_ps3`** · **`/wad/r_perma.wad_ps3`** |
| **mau** (produção, 3/3 corridas) | `gow2.psarc` — **e mais nada** |

**O binário mau nunca abre um único ficheiro de conteúdo.**

## A sequência que falta

No binário bom, a abertura do 2.º movie é precedida por esta cadeia (visível com
`PS3_TRACE_FIOSOPEN=1`):

```
[FIOSOPEN] STOP-YIELD
[FIOSOPEN] FREELIST-REBUILD media=...
[FIOSOPEN] HOST-POP media=0x43009270 op=0x430094C0 next=0x430095A0
[movie] guest movie -> host VT overlay '...'
[movieio] open '/_movies/smlogo_v2.m2v' -> ...
[cellVdec] decoder opened ...
```

No mau, **nada disto acontece**. O que acontece é:

```
[movie-vt] autostart from cache '.../movie_cache/SmLogo_v2.m2v'
```

Ou seja: o overlay VideoToolbox do **host** toca o filme por *autostart*, mas o **guest**
nunca faz o pedido. O filme aparece no ecrã e ninguém do lado do jogo sabe disso.

**Isso explica por que o overlay sempre pareceu funcionar** enquanto tudo o resto estava
parado — são caminhos independentes.

## A cadeia causal, agora completa

```
guest nao pede o movie via FIOS
   -> nao abre /_movies/smlogo_v2.m2v
      -> nao ha 2.o cellVdec StartSeq
         -> REPLAY-NOPIC nunca dispara (condicao e startseq_count >= 2)
            -> nao abre os WADs
               -> R_PermA nunca e lido (0 em vez de 20169344)
                  -> thr_auto_load nunca termina
```

**Tudo o que esta investigação perseguiu durante a sessão era consequência do primeiro
elo.** As 2855 funções, o TOCFIX, o spinlock, os patches — todos a jusante do ponto onde
a coisa realmente pára.

## Um dado que complica, e que fica registado

O binário mau **não é determinista nos estágios iniciais**:

| corrida | st620 max | QueryAttrEx | linhas | `thr_end` |
|---|---:|---:|---:|---:|
| 1 | 0 | 0 | 3011 | 0 |
| 2 | **11** | **4** | 3785 | 0 |
| 3 | 0 | 0 | 3000 | 0 |

Numa das três, progrediu bastante mais (`st620=11`, 4 `QueryAttrEx` — os mesmos valores do
bom) **e mesmo assim falhou o `thr_auto_load`**. O bom é estável em 5/5.

Isto sugere ou **dois problemas distintos** (um de timing nos estágios iniciais, outro
estável na cadeia FIOS), ou um único mecanismo cuja manifestação varia. Não está
resolvido.

## O que aponta para os patches FIOS

Existem **9** `patch_fios_*.py`, e **3 deles são novos** desde o último binário bom
(estavam na lista dos 15 novos entre `e6d65a2` e hoje):

```
patch_fios_f2b_fo_block.py            <- novo
patch_fios_f2b_open_block_install.py  <- novo
patch_fios_stream_guards_install.py   <- novo
patch_fios_done_cancel_yield.py
patch_fios_f2a_f2b_wad.py
patch_fios_f2b_fo_ctor.py
patch_fios_f2b_open_success.py
patch_fios_stop_yield.py
patch_fios_stream_pump.py
```

E os nomes casam com os eventos em falta: `stop_yield` ↔ `STOP-YIELD`,
`f2b_open_*` ↔ a abertura que não acontece.

⚠️ **Isto é correlação de nomes, não medição.** A Fase 8 já mediu que os 89 patches de
hoje aplicados a um lift limpo reproduzem a falha — o que não exclui os FIOS, mas também
não os acusa.

## A próxima medição, e está a correr

Correr o binário **mau** com `PS3_TRACE_FIOSOPEN=1` e `PS3_TRACE_FIOSSCHED=1`. Três
desfechos:

| resultado | significado |
|---|---|
| `FIOSOPEN=0` | o mecanismo FIOS nem sequer arranca — o problema é a montante dele |
| `FIOSOPEN>0` mas sem `HOST-POP` | a cadeia arranca e **pára** num ponto identificável |
| `FIOSOPEN>0` com `HOST-POP` | o pedido chega e o que falha é a abertura — outro sítio |

O segundo é o mais útil: dá o ponto exacto dentro da cadeia.

## Ressalva

Isto **localiza** a divergência; não a explica. Não sabemos ainda se o guest não pede
porque está bloqueado noutro sítio, se pede e o FIOS não entrega, ou se a condição que
dispara o pedido nunca é satisfeita.

---

# CAUSA ENCONTRADA — e não é um commit

**Medido a 2026-07-31, com `PS3_TRACE_FIOSOPEN=1` no binário de produção:**

```
[FIOSOPEN] 002B4224 poll #122880 container=0x00869E04 io=0x0  <-- SEM OP PARA POLLAR
[FIOSOPEN] 0030D5CC op_alloc #5 r3=0x00000000                 <-- SEM OP LIVRE (F2a)
```

A free-list de operações FIOS **esgota** depois do `MovieStop` e `op_alloc` devolve NULL.
O guest fica a fazer poll — 122 880 vezes — sobre um container sem operação nenhuma.

## As duas peças que faltam

| peça | produção | `pre_v4` (gera o binário bom) |
|---|---:|---:|
| `FIOS-STOP-YIELD` | **0** | 9 |
| `FIOS-FREELIST-REBUILD` | **0** | 8 |
| `FIOS-HOST-POP` | 1 | presente |

E faltam por **duas razões diferentes**, ambas instrutivas:

### 1. `patch_fios_stop_yield.py` — agulha morta por deriva de forma

A `NEEDLE` procura quatro linhas consecutivas. A diferença entre o que ela procura e o que
o lift tem hoje é **uma só**:

```diff
         vm_write32(ctx->gpr[31] + 0x620, ctx->gpr[0]);
-        func_0043FF30(ctx); DRAIN_TRAMPOLINE(ctx);
+        ctx->lr = 0x002C0074; func_0043FF30(ctx); DRAIN_TRAMPOLINE(ctx);
         /* nop */;
```

O `ctx->lr = 0x...;` explícito. Resultado: `SKIP` nos 7 chunks, em silêncio.

**Ironia registada:** o `ctx->lr` explícito foi investigado pela Fase 6 como *causa* da
regressão e **refutado** — com uma reversão cirúrgica, um rebuild e uma medição. E estava
certo: não é a causa **directa**. É a causa **de a agulha do patch morrer**, que é um
efeito de segunda ordem que ninguém procurou.

### 2. `patch_fios_f2a_f2b_wad.py` — não aplica nada

O `main()` só faz `print()`:

```python
print("Apply order after re-lift:")
print("  3) re-apply F2a/F2b host helpers from this session's lift diffs")
```

O docstring descreve o fix com precisão (*"FREELIST-REBUILD at MovieStop + HOST-POP when
op_alloc returns 0"*), mas o código diz "vai reaplicar isto à mão". **Era uma edição
manual no lift, nunca convertida em patch idempotente.**

## Porque nada do que fizemos hoje encontrou isto

| observação da sessão | explicação |
|---|---|
| O bisect não convergiu | **Não há commit culpado.** O re-lift apagou código que nunca esteve versionado |
| Dez candidatos refutados | Todos a jusante do ponto real |
| Reverter TOCFIX/spinlock não repôs nada | Não havia nada partido para reverter |
| O lift é byte-a-byte idêntico no `thr_auto_load` | Verdade — o que falta **falta**, não está diferente |
| `FATAL 0`, o boot pára sem crash | `op_alloc` devolve NULL e o guest faz poll para sempre |

## A lição, e é a razão de ser do marco anterior

O marco v1.0 chamava-se **"RDY-0: desbloquear o re-lift"**, e a regra que existia para
impor está no CLAUDE.md:

> *todo fix neles vira **script idempotente commitado** (`patch_*.py`) para sobreviver a
> re-lift*

Este fix violava-a. O v1.0 tornou o re-lift possível — e o primeiro re-lift a sério apagou
o único fix que nunca tinha sido convertido.

**A regressão é a prova de que a regra era necessária.** E o custo de a descobrir foi uma
sessão inteira, dez candidatos e três métodos falhados.

## Uma segunda lição, sobre agulhas

Um patch cuja agulha deixa de casar **não falha** — dá `SKIP` e o pipeline continua verde.
O marco v1.0 construiu um catálogo de seis estados precisamente para isto, mas o `SKIP`
conta como estado benigno. Vale a pena rever se `SKIP` num patch `FUNCIONAL` devia ser
tratado como benigno ou como alarme.
