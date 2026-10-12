# E66 — a raiz: o callback de overflow do GCM nunca foi instalado

**Data:** 2026-08-08 · **Antecessor:** [E65](2026-08-08-E65-o-r3-zero-nao-sustenta-o-ciclo.md)

O E65 refutou a minha hipótese sobre o valor de retorno e deixou uma coisa de pé:
**o campo `+0xC` do objecto aponta para lixo**. Segui-o com a vigia, e a raiz
apareceu — e é nossa.

---

## O objecto é o contexto do GCM

Vigia sobre os 16 bytes de `0x0A050000` (o `r3` que os disjuntores já
imprimiam):

```
[0x0A050000] = 0x47E00000   _cellGcmInitBody+0x6C   begin
[0x0A050004] = 0x48000000   _cellGcmInitBody+0x84   end
[0x0A050008] = 0x47E00000   _cellGcmInitBody+0x90   current
[0x0A05000C] = 0x00000000   _cellGcmInitBody+0x9C   callback
```

É o `CellGcmContextData` da API real — `begin/end/current/callback`. E as
contagens de escrita na corrida inteira:

| campo | escritas |
|---|---|
| `+0x0` begin | 1 |
| `+0x4` end | 1 |
| `+0x8` current | **59 363** |
| `+0xC` callback | **1, com `0`** |

## A ponte prometida não existe

`libs/video/cellGcmSys.c`:

```c
gwrite32(cdata + 0xC, 0);   /* callback OPD (bridge fills) */
```

E o comentário do bloco, escrito por quem montou isto, já avisava:

> *«getting it wrong makes the game read `current` as the callback OPD and stall»*

O layout está certo. **O campo é que fica vazio, e nenhuma ponte o preenche.**

Existe um `cellGcmCallback()` neste mesmo ficheiro — mas não serve: é **no-op**
(não repõe o `current`) e **não está exportado por NID**, logo o guest nunca lhe
chegaria.

## O mecanismo, agora inteiro

O guest enche o FIFO (59 363 escritas em `current`), chega ao `end`, e faz:

```c
if (end < current + 1) { if ((*(code*)**(int**)(ctx+0xC))(ctx,1) != 0) return; }
*current = ...; current++;
```

Com `callback = 0`, dereferencia memória baixa; o `ctr` sai com lixo (`"/wad"`,
`0x005F6D6F`); os disjuntores `ICALL` saltam a chamada e devolvem `0` — que ali
quer dizer *«cresceu com sucesso»*; o guest escreve para lá do fim e repete.

**São os ciclos do E64.**

## O fix, e a medição controlada

`PS3_GCM_CB=1` (novo, **OFF por default**): instala um OPD sintético logo a
seguir aos 16 bytes do contexto, com um sentinela que o `ps3_indirect_call`
reconhece e trata host-side — repõe `current = begin` e devolve `CELL_OK`.

**Mesmo binário**, gate ligado e desligado:

| | gate OFF | gate ON |
|---|---|---|
| `ICALL-ASCII` | **≥ 16 384** | **0** |
| `ICALL-HEAP` | **≥ 8 192** | 10 |
| `UNCOMMITTED` | 19 | 19 |
| B71 | #060 | #060 |
| `FIOSOPEN` | 241 | 241 |

E o callback dispara **exactamente uma vez**, no sítio certo:

```
[GCM-CB] #1 wrap ctx=0x0A050000 current=0x47FFFFFC -> 0x47E00000
```

`0x47FFFFFC` é a última palavra antes do `end=0x48000000`. O buffer encheu até
ao fim e deu a volta. É a confirmação de que o mecanismo é o certo.

**Smoke canónico da intro** (`CLAUDE.md`: `st620` max ≥ 3): `st620_max=11` com o
gate OFF **e** com o gate ON. Sem regressão.

## O que NÃO é atribuível

O ciclo `spu-dma SKIP` (≥ 4096 no E64) **não disparou em nenhuma das duas
corridas do par controlado**. Não posso atribuir-lhe nada — é variável entre
corridas e fica por explicar.

## Porque é que o default fica OFF

Todas estas corridas usam `PS3_NO_RSX=1`. Repor `current = begin` só é seguro
**se ninguém estiver a consumir o FIFO**. Com um backend RSX real a ler o
buffer, dar a volta sem verificar o ponteiro GET sobrescreve comandos ainda por
consumir.

O callback fiel espera que o GET passe a região antes de reciclar. O que está
aqui é a versão correcta **para o caminho sem RSX** — e é por isso que não
promovo o default sem uma corrida com o backend ligado.

## E não é a parede dos E41–E61

`UNCOMMITTED=19` e B71 a parar no `#060` **não mudam**. Continuam a ser duas
paredes distintas, como o E64 já dizia. Esta fecha uma; a outra fica aberta.

## Refutado por medição

1. **«o `+0xC` é preenchido mais tarde por alguma ponte»** — uma escrita, com
   `0`, e nunca mais, contra 59 363 no `current`.
2. **«o `cellGcmCallback` que já existe cobre isto»** — é no-op e não está
   exportado; e mesmo chamado não repunha o `current`.
3. Tudo o que E11–E17 e E19–E65 já refutaram.

## Próximo

1. **corrida com backend RSX** (`PS3_RSX_BACKEND=metal`) para decidir se o wrap
   simples chega ou se é preciso esperar pelo GET — é o que falta para promover
   o default;
2. o `spu-dma SKIP`, que não reproduziu;
3. a parede dos E41–E61: fábricas por-tag não repovoadas, B71 no #60;
4. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
