# E69 — o `spu-dma` intermitente: um attach do Edge que falha e ninguém verifica

> **ATRIBUICAO REFUTADA em 2026-08-08 pelo [E70](2026-08-08-E70-o-erro-nao-e-nosso-vem-do-SPU-liftado.md):**
> o `0x80410910` **nao** vem do nosso `LFQ_ERR_ALIGN` -- instrumentados os tres
> sitios, zero disparos em quatro corridas, incluindo as duas que mostraram o
> `spu-dma`. A correspondencia do valor era coincidencia numerica. O erro nasce no
> **SPU liftado** (`spu1_spu_func_00009658`, epilogo de erro, `r3 = r7`).
> **SOBREVIVE:** a taxa, o gatilho temporal (`[ATTERR]` imediatamente antes do
> primeiro skip), a independencia face a' cadeia dos E41-E61, e a correlacao com
> o `ICALL-HEAP`.
> **CAI:** so' a atribuicao do codigo a' nossa LFQueue -- que o E69 nem chegou a
> afirmar, mas que a citacao do comentario do `spu_dma.h` sugeria.

**Data:** 2026-08-08 · **Antecessor:** [E68](2026-08-08-E68-a-cadeia-nao-depende-do-regime.md)

O E66 deixou o ciclo `spu-dma SKIP` como *«não reproduziu»* — o pior estado em
que se pode deixar uma coisa. Caracterizado, e com discriminador.

---

## A taxa

Três corridas de 45s, **configuração idêntica**:

| corrida | `spu-dma` | `UNCOMMITTED` | `st620` |
|---|---|---|---|
| 1 | 0 | 19 | 11 |
| 2 | 0 | 19 | 11 |
| 3 | **22 linhas, ≥1024** | 19 | 11 |

**1 em 3.** Somando as corridas anteriores: **2 em 7** (~29%). Independente do
regime (`NO_RSX` / Metal) e do gate `PS3_GCM_CB`.

E o mais importante: **`UNCOMMITTED=19` e `st620=11` em todas.** Este ciclo não
toca na cadeia dos E41–E61 nem na FSM da intro. É uma falha à parte.

## O discriminador

As linhas imediatamente antes do primeiro skip, na corrida que dispara:

```
[EDGEASSERT] a=0x0000AC30 b=0x00000047
[EDGEPOLL]   a=0x00000000 b=0x00000000
[ATTERR]     a=0x80410910 b=0x00000000        <- erro SPURS
[EDGEATT1]   a=0x0000BA10 b=0x80410910        <- o attach devolve-o
[EDGEASSERT] a=0x0000AA80 b=0x0000010E
[EDGEASSERT] a=0x0000AA80 b=0x00000110
[spu-dma] SKIP #1 out-of-range GET ea=0x00003300 size=13184 lsa=0x0BC00
```

`0x80410910` é um código de erro **SPURS**. Um attach do **Edge** falha, e o
código a seguir usa o resultado sem o verificar.

E isto já estava escrito — no comentário do próprio `spu_dma.h`, ao lado do
guard que eu corrigi ontem:

> *«In-range but uncommitted (e.g. a SPURS error code like `0x80410913` used as
> an EA by unchecked task-lib code)»*

Mesma família de código (`0x8041xxxx`), mesmo mecanismo. Quem escreveu o guard
já tinha visto isto acontecer; o que faltava era ligar o intermitente ao attach.

## O que mais diverge na corrida que falha

| etiqueta | corrida 1 | corrida 3 |
|---|---|---|
| `[SPUJOB]` | 493 | **612** |
| `[ICALL-HEAP]` | 0 | **25** |
| `[movie-vt]` | 24 | **0** |
| `[FIOSOPEN]` | 241 | 244 |

O `ICALL-HEAP` aparece **só** na corrida que falha o attach. Ou seja, os dois
absorvedores que sobraram depois do fix do GCM (E66/E67) andam juntos, e ambos
a jusante desta falha.

## O que isto arruma e o que abre

**Arruma:** o `spu-dma SKIP` deixa de ser «não reproduziu». É intermitente a
~29%, tem um gatilho identificado, e não contamina os outros resultados desta
sessão — o que também valida, mais uma vez, que as contagens que publiquei nos
E41–E61 são estáveis.

**Abre:** *porque é que o attach do Edge falha com `0x80410910`, e porque é que
o código não verifica.* É a próxima cadeia, e tem o mesmo formato das outras
duas: um erro devolvido e ignorado, a virar endereço.

## Refutado por medição

1. **«o `spu-dma SKIP` não reproduz»** — reproduz a ~29% (2 em 7), com o mesmo
   operando `ea=0x00003300`.
2. **«o ciclo afecta a cadeia principal»** — `UNCOMMITTED=19` e `st620=11`
   idênticos nas três corridas, dispare ou não.
3. Tudo o que E11–E17 e E19–E68 já refutaram.

## Próximo

1. **o attach do Edge** — `[EDGEATT1]` a devolver `0x80410910`; porquê, e quem
   ignora o retorno;
2. porque é que o `ICALL-HEAP` anda com ele;
3. promover o default do `PS3_GCM_CB` (corrida longa, dois OS);
4. a parede dos E41–E61: fábricas por-tag não repovoadas, B71 no #60;
5. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33) — este último talvez a mesma
   coisa que aqui, agora que há um gatilho SPURS identificado.
