# E55 — o instrumento que perdia campos: as cinco fábricas estão equilibradas

**Data:** 2026-08-07 · **Antecessor:** [E54](2026-08-07-E54-o-pop-negativo-escreve-retracto-o-mecanismo-do-E43.md)

O E54 marcou como **sem base** as contagens que o E50 publicou para quatro das
cinco fábricas, porque a sonda `SCOPE` é cega ao ramo negativo do pop. Aqui
refaço-as com a vigia — que não é cega — e o resultado só saiu limpo depois de
arranjar o instrumento.

---

## Primeiro, o que a vigia respondeu logo

Vigia sobre os cinco cursores (`fábrica + 0xC8`), sem cap, duas corridas:

**`func_0039DAF4` — o ramo negativo do pop, aquele que escreve — nunca correu.
Zero escritas, nas cinco fábricas, nas duas corridas.** Não há underflow em
lado nenhum, e agora está medido com um instrumento que o veria.

## Depois, um desequilíbrio que não existia

As mesmas corridas davam «mais pops que pushes» nalgumas fábricas:

| | corrida A | corrida B |
|---|---|---|
| `0x400C6210` | 176/176 | **175/176** |
| `0x403008E8` | 175/175 | **174/175** |
| `0x40300E80` | **174/175** | 175/175 |
| `0x400D6DA0` | 175/175 | 175/175 |
| `0x401002F0` | **174/176** | **175/176** |

**As fábricas afectadas mudam entre corridas.** Isso não é uma propriedade do
programa — é assinatura de artefacto. E o défice fecha ao cêntimo: 3 linhas
partidas por corrida, 3 pushes em falta por corrida, nas duas.

## A causa: a linha não era atómica

`ps3_watch_store_note()` compunha cada linha com **cinco `fprintf` seguidos** —
cabeçalho, `ra0`, `ra1`, newline. Outro fio a escrever no mesmo `stderr`
metia-se pelo meio, e a linha saía partida, sem o `ra0`:

```
[WATCHSTORE] tid=34507411 w8 [0x401003B8]=0x0 (alvo 0x401003B8)[SPUJOB] spu job returned cleanly
```

O parser descartava-a e o push desaparecia da conta.

Isto já me tinha mordido antes: no [E47](2026-08-07-E47-um-so-fio-nao-e-corrida.md)
o `grep` de `func_0039DA78` deu **174** em vez de 176, e eu expliquei-o como
«duas linhas partidas» sem tirar a consequência — que o instrumento estava a
perder medições em silêncio.

**Arranjo:** compor a linha num buffer e emitir **um** `fprintf`.

## E o resultado limpo

Reconstruído, mesma corrida, mesmo tudo:

```
linhas partidas por interleave: 0
  0x400C6210   push=176  pop=176   ramo_negativo=0  OK
  0x400D6DA0   push=175  pop=175   ramo_negativo=0  OK
  0x401002F0   push=176  pop=176   ramo_negativo=0  OK
  0x403008E8   push=175  pop=175   ramo_negativo=0  OK
  0x40300E80   push=175  pop=175   ramo_negativo=0  OK
```

**As cinco equilibradas, por medição** — não por aritmética a tapar buracos.
O que o E50 afirmou sem base fica **reposto**, com o instrumento certo.

## Cada fábrica tem o seu inicializador

A vigia apanhou também quem põe o cursor a −1 no arranque, e não é o mesmo para
todas:

| fábrica | inicializador |
|---|---|
| `0x400C6210` | `func_002B11B8` |
| `0x401002F0` | `func_002A8094` |
| `0x403008E8` | `func_002879BC` |
| `0x40300E80` | `func_0028F07C` |
| `0x400D6DA0` | `func_002B11B8` |

## A regra que sai daqui

Já tinha escrito no E54 que instrumentar o **dado** cobre ramos que não se
conhece, e instrumentar o **sítio** só cobre o que já se assumiu. Falta a outra
metade:

> Um instrumento que **perde campos** quando é concorrido mente do mesmo modo
> que um que **não cobre um ramo**. Nos dois casos o número sai mais pequeno e
> nada no output diz que faltou alguma coisa.

Contar as linhas que o parser **rejeitou** — e não só as que aceitou — foi o que
apanhou isto. Um parser que descarta em silêncio é o mesmo defeito outra vez.

## Refutado por medição

1. **«duas das cinco fábricas têm mais pops que pushes»** — artefacto; as
   fábricas afectadas mudam entre corridas e o défice iguala exactamente o
   número de linhas partidas.
2. **«o ramo negativo do pop corre nalguma das cinco»** — zero escritas de
   `func_0039DAF4`, cinco fábricas, três corridas.
3. Tudo o que E11–E17 e E19–E54 já refutaram.

## Próximo

1. a pergunta do E53, intacta e ainda a que decide o fix: **quem chama
   `FUN_0024f028` e porque é que corre depois de `func_0041F924`** — oito
   candidatos no dump (`FUN_00021410`, `FUN_0002631c`, `FUN_00038c10`,
   `FUN_0009f2a4`, `FUN_000c72fc`, `FUN_001440e8`, `FUN_0024f1cc`,
   `FUN_0024f24c`);
2. as duas rotas latentes (`func_00264BA8`, fallback `0x1c4`);
3. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
