# E47 — um só fio: não é corrida, é ordem de programa

> **UMA SECCAO REFUTADA pelo [E48](2026-08-07-E48-o-salto-e-um-ciclo-e-o-jogo-mesmo-nao-verifica.md):**
> a hipotese do salto para tras como padrao `2550C8` (ver «Hipotese para a rodada
> seguinte»). O veredicto do `tid` unico, o controlo multi-fio e o determinismo
> **sobrevivem intactos**.

**Data:** 2026-08-07 · **Antecessor:** [E46](2026-08-07-E46-a-cadeia-inteira-em-dezassete-linhas.md)

O E46 deixou duas leituras vivas — corrida entre fios, ou ordem de programa no
mesmo fio — e disse que só o `tid` decidia. Está medido.

---

## O instrumento

- `runtime/ppu/ppu_loader.cpp`: `ps3_dbg_tid()` exposto (C++ puro, não
  `extern "C"` — uma especificação de ligação é ilegal em escopo de bloco, e as
  sondas vivem no meio de uma função), e `tid=` na linha da vigia `WATCHSTORE`;
- `patch_39e5a8_empty_factory_probe.py` v2: `tid=` na sonda `EMPTYFAB`, com
  upgrade do bloco v1 já aplicado — o lift não é versionado, logo o patch tem de
  saber subir de versão sem passar por um re-lift.

## O veredicto

```
[EMPTYFAB] tid=34107303 fab=0x400C6210 cursor=-1 VAZIO lr=0x0024E424
[EMPTYFAB] tid=34107303 fab=0x403008E8 cursor=-1 VAZIO lr=0x0024E424
[EMPTYFAB] tid=34107303 fab=0x40300E80 cursor=-1 VAZIO lr=0x0024E25C
[EMPTYFAB] tid=34107303 fab=0x400D6DA0 cursor=-1 VAZIO lr=0x00237D38
[EMPTYFAB] tid=34107303 fab=0x400C6210 cursor=-1 VAZIO lr=0x002B2DE0
[EMPTYFAB] tid=34107303 fab=0x401002F0 cursor=-1 VAZIO lr=0x002A9A84
```

| evento | contagem | tids distintos |
|---|---|---|
| escritas do cursor `0x400C62D8` | 353 | **1** (`34107303`) |
| leituras da fábrica (`EMPTYFAB`) | 259 | **1** (`34107303`) |

**Um só fio, ponta a ponta.** Produtor e consumidor são o mesmo.

## O controlo que impede a leitura preguiçosa

«Um só tid» seria vazio se o processo fosse mono-fio. Não é, e a prova está no
próprio log: **duas linhas `WATCHSTORE` saíram partidas a meio por um
`[SPUJOB]` de outro fio**, escrito no mesmo `stderr` ao mesmo tempo. Há
concorrência real na corrida; ela é que não toca nesta cadeia.

(É também porque é que a contagem por `grep` de `func_0039DA78` dá 174 e não
176: duas das suas linhas ficaram partidas. O total das três origens fecha em
353.)

## O que isto mata, e o que deixa

**Morre a leitura 1 do E46 (corrida entre fios).** Não há fix de sincronização a
fazer aqui: nenhum `lock`, nenhuma barreira, nenhuma ordem de `event_flag`
mudaria isto.

**Fica a leitura 2: ordem de programa.** O mesmo fio pede produto, recebe nulo,
dereferencia-o, e só depois empurra. Em linha recta.

## Hipótese para a rodada seguinte — e liga a uma dívida antiga

> **HIPOTESE REFUTADA em 2026-08-07 pelo [E48](2026-08-07-E48-o-salto-e-um-ciclo-e-o-jogo-mesmo-nao-verifica.md).**
> O salto para tras NAO e' o padrao `2550C8`. O Ghidra ve' **uma so' funcao** na
> regiao (`FUN_0024e198`, `0x24E198..0x24E558`, 960 bytes) e `0x24E2AC`,
> `0x24E3D0` e `0x24E424` estao todos la' dentro; a decompilacao mostra um `for`
> sobre lista ligada cuja aresta de retorno E' exactamente este salto.
> **SOBREVIVE:** tudo o resto deste ledger -- o veredicto do `tid` unico, o
> controlo do processo multi-fio, e o determinismo das tres corridas.
> **CAI:** so' esta seccao. A auditoria sistematica do padrao `2550C8` continua
> por fazer, mas perde o caso concreto que eu lhe queria dar aqui.
> Mantido como registo -- nao construir sobre esta seccao.

Não é medição, é hipótese, e está aqui por ser barata de testar:

`func_0024E3D0` — a função que consome o nulo — **acaba a saltar para trás**:

```c
{ g_trampoline_fn = (void(*)(void*))func_0024E2AC; return; }
```

`0x24E2AC` é **0x124 bytes ANTES** de `0x24E3D0`. É exactamente a forma do bug
que o `patch_fallthrough_2550c8.py` corrigiu (`func_002550C8` caía em
`func_002550E8` em vez de `func_00255178`) e que o CLAUDE.md lista como
auditoria sistemática **ainda por fazer**:

> *«auditar `g_trampoline_fn = func_...` onde o alvo tem EA guest MENOR que o
> site — fallthrough para trás disfarçado, mesmo padrão do fix 2550C8».*

Se o lift pôs o consumidor antes do produtor, a ordem errada é **nossa**, não do
jogo — e o fix é de lift, não de HLE. Se o salto para trás for legítimo (um
ciclo), a ordem errada é do jogo sob a nossa HLE e o fix é outro.

**Discriminador:** ler o guest em `0x24E2AC..0x24E424` no disassembly e ver se
`0x24E3D0` tem mesmo um caminho de entrada que justifique o salto para trás, ou
se é fallthrough inventado.

## Determinismo

Três corridas sãs consecutivas dão os mesmos números: `EMPTYFAB=259`,
`WATCHSTORE=353`, `FIOSOPEN=241`, `UNCOMMITTED=19`, os mesmos 6 `VAZIO` com as
mesmas fábricas e os mesmos `lr`. Bisecção sai barata daqui para a frente.

## Refutado por medição

1. **«é uma corrida entre o produtor e o consumidor»** — um só `tid` nas 353
   escritas e nas 259 leituras.
2. **«um só tid porque o processo é mono-fio»** — há outro fio a escrever no
   mesmo `stderr` ao mesmo tempo, a ponto de partir linhas a meio.
3. Tudo o que E11–E17 e E19–E46 já refutaram.

## Próximo

1. **disassembly de `0x24E2AC..0x24E424`** — o salto para trás de
   `func_0024E3D0` é ciclo legítimo ou fallthrough inventado pelo lifter;
2. se for inventado: a auditoria sistemática do padrão em todo o lift, que o
   CLAUDE.md já pedia e que passa a ter um caso concreto a puxar por ela;
3. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
