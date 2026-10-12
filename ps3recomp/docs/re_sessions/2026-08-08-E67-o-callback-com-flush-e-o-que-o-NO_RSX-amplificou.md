# E67 — o callback com flush honesto, e o que o `PS3_NO_RSX` amplificou

**Data:** 2026-08-08 · **Antecessor:** [E66](2026-08-08-E66-a-raiz-o-callback-de-overflow-do-GCM-nunca-foi-instalado.md)

O E66 deixou o fix a fazer wrap cego e o default OFF, com uma razão escrita:
*«repor `current = begin` só é seguro se ninguém estiver a consumir o FIFO»*.
Resolvido — e a corrida com backend revelou um limite do meu próprio setup.

---

## O handler mudou de sítio e de semântica

Passou do `ppu_loader` para `libs/video/cellGcmSys.c`, onde pertence — e onde
tem acesso ao `rsx_process_fifo()`, que é `static` daquele ficheiro.

```c
s32 ps3_gcm_ctx_overflow(u32 ctx_ea)
{
    if (io && current >= io)
        vm_write32(GCM_CTRL_EA + 0, current - io);   /* 1. put = offset do current */
    rsx_process_fifo();                              /* 2. dreno: get avanca ate' ao put */
    vm_write32(ctx_ea + 0x8, begin);                 /* 3. so' entao recicla */
    return CELL_OK;
}
```

Submete → dreno → recicla. É a ordem que a consola faz, e o passo 2 é o que o
torna seguro com um consumidor real.

**Medido:**

```
[GCM-CB] #1 overflow ctx=0x0A050000 current=0x47FFFFFC -> 0x47E00000 (put=0x1FFFFC get=0x1FFFFC)
```

`put == get`. O FIFO foi drenado **antes** de o buffer ser reciclado.

## A corrida com backend RSX

`PS3_RSX_BACKEND=metal PS3_RSX_FIFO=1`, sem `PS3_NO_RSX`, 30s, mesmo binário:

| | gate OFF | gate ON |
|---|---|---|
| `st620` max | **11** | **11** |
| `GCM-CB` | 0 | 1 |
| `ICALL-ASCII` | **0** | **0** |
| diagnósticos `ICALL` (todos) | **0** | 0 |
| linhas | 3209 | 4095 |

Sem regressão, e o callback dispara uma vez, com o GET alcançado.

## O que isto me obriga a admitir

**Os ≥ 16 384 ciclos do E64 são específicos do `PS3_NO_RSX=1`.**

Com o backend ligado, alguém drena o FIFO e o buffer quase não chega a encher —
zero diagnósticos `ICALL` **dos dois lados**. A falha do callback só se torna
catastrófica quando nada consome o FIFO, que é exactamente a configuração em que
eu corri tudo desde o E43.

O que sobrevive intacto:

- **o campo `+0xC` nunca é instalado** — uma escrita, com `0` (E66). É um buraco
  real na nossa HLE, em qualquer configuração;
- **o fix é correcto e seguro nas duas** — `st620=11`, sem regressão;
- **no caminho de diagnóstico, muda tudo**: ≥ 16 384 despachos absorvidos → 0.

O que **cai** é a grandeza como propriedade do jogo: era, em boa parte,
propriedade do meu setup. Um instrumento que altera o regime que mede — a
terceira variante do mesmo erro nesta sessão, depois da sonda cega a um ramo e
do cap que se cala.

## Repeti um erro que eu próprio tinha documentado

A primeira versão declarava `extern "C" int32_t ps3_gcm_ctx_overflow(...)`
**dentro** do bloco. É ilegal em escopo de bloco, dá
`error: expected unqualified-id`, e eu tinha escrito exactamente isso no
[E47](2026-08-07-E47-um-so-fio-nao-e-corrida.md) — hoje de manhã. Custou um
build. A declaração está agora a escopo de ficheiro, com um comentário que o
diz.

## Porque é que o default continua OFF

Não por dúvida sobre a correcção, mas por cobertura: **uma** corrida de 30s com
backend, num só backend (Metal), sem o caminho Windows. A barra deste projecto
para promover default é mais alta.

Para promover, bastaria: corrida longa com backend nos dois OS, e o smoke da
intro nas duas configurações — que aqui já passa (`st620=11`).

## Refutado por medição

1. **«o wrap cego basta»** — basta no `NO_RSX`, mas o dreno é o que o torna
   defensável com consumidor; agora está lá e mede-se `put == get`.
2. **«os ciclos são propriedade do jogo»** — são do `PS3_NO_RSX=1`; com backend,
   zero diagnósticos `ICALL` dos dois lados.
3. Tudo o que E11–E17 e E19–E66 já refutaram.

## Próximo

1. corrida longa com backend, e o caminho Windows, para promover o default;
2. **rever o que mais medi só em `PS3_NO_RSX=1`** — se este regime amplificou
   isto, pode ter amplificado outras coisas desta sessão;
3. o `spu-dma SKIP`, que não reproduziu (E66);
4. a parede dos E41–E61: fábricas por-tag não repovoadas, B71 no #60;
5. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
