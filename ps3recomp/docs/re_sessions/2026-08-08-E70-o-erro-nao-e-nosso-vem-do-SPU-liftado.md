# E70 — o `0x80410910` não é nosso: vem do SPU liftado

**Data:** 2026-08-08 · **Antecessor:** [E69](2026-08-08-E69-o-spu-dma-intermitente-e-um-attach-do-Edge-que-falha.md)

O E69 identificou `0x80410910` como o gatilho do `spu-dma` intermitente. Fui
procurá-lo, encontrei-o num `#define` nosso, e **atribuí mal**.

---

## A atribuição errada

```
libs/spurs/cellSpursLFQueue.c:109:#define LFQ_ERR_ALIGN   ((int32_t)0x80410910)
```

Três sítios devolvem-no, todos com o mesmo teste:

```c
if ((queue & 127) || (buffer & 15)) return LFQ_ERR_ALIGN;
```

Parecia fechar: fila desalinhada → erro → usado como EA a jusante. Instrumentei
os três para dizerem **qual** condição falhou e com que endereço.

## A medição refuta-a

Quatro corridas de 45s, configuração idêntica:

| corrida | `LFQALIGN` | `spu-dma` | `UNCOMMITTED` | `st620` |
|---|---|---|---|---|
| 1 | **0** | 22 | 19 | 11 |
| 2 | **0** | 0 | 19 | 11 |
| 3 | **0** | 22 | 19 | 11 |
| 4 | **0** | 0 | 19 | 11 |

**Zero em todas** — incluindo as duas em que o `spu-dma` disparou. Se o nosso
check de alinhamento fosse a fonte, as corridas 1 e 3 tinham-no mostrado.

A correspondência do valor era **coincidência numérica**.

## Onde o erro nasce mesmo

`[ATTERR]` e `[EDGEATT1]` não estão no runtime nem nas libs. Estão no **SPU
liftado**:

```
gow2-recomp/spu_lifted/spu1_v2/spu_recomp.c:8007
    void spu1_spu_func_00009658(spu_context* ctx) {
        ... spu_dbg_log("[ATTERR] a=0x%08X ...", ctx->gpr[7]._u32[0], 0);
        ctx->gpr[3] = spu_ori(ctx->gpr[7], 0);   /* r3 = r7 = codigo de erro */
```

`0x9658` é o **epílogo de erro** de uma função do SPU: quem falha põe o código em
`r7` e salta para ali, que o copia para `r3` e devolve. E logo acima, em
`0x9654`, o caminho de sucesso faz `r7 = 0` antes do mesmo salto.

**O erro é produzido pelo código SPU do próprio jogo** (Edge/SPURS a correr no
SPU), não pela nossa HLE do lado PPU.

Isso muda o subsistema: a próxima cadeia é do lado **SPU**, e é preciso descobrir
quem põe `0x80410910` em `r7` antes de saltar para `0x9658`.

## O que fica de útil, mesmo estando errado

Os três sítios do `LFQ_ERR_ALIGN` **devolviam em silêncio**. Agora dizem qual
condição falhou e com que endereço, com o mesmo padrão de contagem dos outros
(primeiras 16 mais potências de 2). É um caminho de erro mudo a menos, e custou
nada — a instrumentação fica mesmo não tendo confirmado a hipótese.

## A taxa, actualizada

Com estas quatro corridas, o `spu-dma` está em **4 de 11** (~36%), sempre com o
mesmo operando `ea=0x00003300`. E `UNCOMMITTED=19` / `st620=11` continuam
idênticos dispare ou não — pela quarta vez.

## Refutado por medição

1. **«o `0x80410910` vem do nosso `LFQ_ERR_ALIGN`»** — zero disparos nos três
   sítios instrumentados, em quatro corridas, incluindo as duas que mostraram o
   `spu-dma`.
2. **«o gatilho é do lado PPU»** — as sondas que o mostram vivem no SPU liftado;
   o valor sai de `r7` no epílogo de erro em `0x9658`.
3. Tudo o que E11–E17 e E19–E69 já refutaram.

## Próximo

1. **quem põe `0x80410910` em `r7`** antes de saltar para `spu1_spu_func_00009658`
   — busca no SPU liftado, é estática e barata;
2. porque é que falha em ~36% das corridas e não sempre;
3. promover o default do `PS3_GCM_CB` (corrida longa, dois OS);
4. a parede dos E41–E61: fábricas por-tag não repovoadas, B71 no #60;
5. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33).
