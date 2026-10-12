# E60 — o fecho é legítimo; a travessia é que corre fora de qualquer escopo

**Data:** 2026-08-07 · **Antecessor:** [E59](2026-08-07-E59-a-ponte-as-duas-pontas-na-mesma-corrida.md)

Fecho da cadeia. Duas perguntas em aberto desde o E53, ambas respondidas.

---

## O segundo caminho funila no mesmo sítio

```c
void _opd_FUN_000d3fac(void) {
  uVar2 = _opd_FUN_00263b70(0x650);     /* aloca */
  _opd_FUN_000d3dd8(uVar2);             /* inicializa -> */
  *(undefined4 *)ppuVar1[-0x175e] = uVar2;
}
```

e dentro de `FUN_000d3dd8`:

```c
uVar7 = _opd_FUN_0024f1cc(*(undefined4 *)(*piVar2 + 0x3d0));
```

`FUN_0024f1cc` é **outro dos oito chamadores** de `FUN_0024f028`. Logo:

| entrada | como pede | vítimas (E59) |
|---|---|---|
| `FUN_0024f24c` | por **nome** (`goGrapplePtActive`) | `0x40780850`, `0x40780C20` |
| `FUN_0024f1cc` | por **id** | `0x40781018`, `0x407810E0` |

**Dois pontos de entrada, uma só travessia.** Não são dois defeitos.

## Quem fecha os escopos — e a resposta é «ninguém a mais»

Sonda `SCOPE` v3 (`ra3`/`ra4` nos pops que esvaziam). Nas seis vezes em que os
escopos de `0x400C6210` e `0x403008E8` são fechados, a cadeia é sempre a mesma,
sem excepção:

```
ps3_indirect_call  ->  func_0042A0C8+0x654  ->  func_0041F924+0x880  ->  pop
```

E `FUN_0042a0c8` é **estruturalmente idêntico** a `FUN_0041f924`:

```c
cVar1 = *(char *)(param_1 + 200);          /* o cursor */
... decrementa, tira o topo ...
for (filhos de iVar6) {
    if (tag_do_filho != tag_do_pai)
        vt[0x44](fabrica[tag_do_filho]);   /* fecha o escopo do filho */
}
```

Diferem na tabela (`PTR_DAT_0053fc18` contra `PTR_DAT_0053ef1c`) e no offset da
lista de filhos (`+0x38` contra `+0x80`). São o mesmo protocolo para classes
diferentes — e o par simétrico de `FUN_0041f700`/`FUN_0042af18`, que ao entrar
chamam `vt[0x40]`.

**Isto é um protocolo enter/leave bem formado sobre um grafo de cena.** O fecho
não é prematuro: é o fim normal de uma travessia com escopo.

## A resolução das duas leituras do E53

O E53 deixou: *«`func_0041F924` fecha cedo demais»* ou *«o B71 chega a este
passo fora da fase em que os escopos existem»*.

**Cai a primeira.** O fecho é o `leave` legítimo do protocolo.

**Fica a segunda:** o passo #41 do B71 corre uma travessia nova **depois** de o
escopo ter sido deixado, e o desenho do jogo assume que ela corre dentro de um.

## E porque é que isso é nosso, não do jogo

O argumento é o da nota de 2026-08-01, citado no E41, e continua a ser um
argumento e não uma medição:

> *«Num PS3 real `*(NULL + 0x20)` faria fault na página nula. Logo o jogo nunca
> pode tomar este caminho com produto nulo.»*

Se isso vale, então **a nossa corrida chega ao B71 #41 num estado em que a
consola nunca chegaria** — e a causa está a montante desta cadeia, no que
sequencia o B71, não em nada que estes dezoito ledgers meçam.

É onde esta cadeia acaba e outra começa.

## Nota de instrumento, declarada

A condição que faz a sonda `SCOPE` v3 imprimir `ra3`/`ra4` testa
`(int8_t)ctx->gpr[10] < 0`. No bloco do **pop** isso é o cursor novo, e está
certo — é dele que saem os números acima. No bloco do **push**, `gpr[10]` é o
*valor empurrado*, não o cursor: essas linhas `[SCOPE+]` são ruído e não devem
ser lidas. Fica escrito em vez de corrigido em silêncio; as conclusões deste
ledger usam só as linhas de pop.

## Refutado por medição

1. **«há dois defeitos, um por caminho»** — os dois caminhos entram na mesma
   travessia (`FUN_0024f028`), por nome e por id.
2. **«`func_0041F924` fecha cedo demais»** — o fecho é o `leave` de um protocolo
   enter/leave simétrico, idêntico em forma ao `FUN_0042a0c8` que o chama.
3. Tudo o que E11–E17 e E19–E59 já refutaram.

## Onde a cadeia fica

```
[a montante, POR INVESTIGAR]  o que leva o B71 ao passo #41 fora de escopo
func_0042A0C8 -> func_0041F924   leave legitimo; escopos por-tag a -1   (E60)
func_000B71B8 #41                pede "goGrapplePtActive"               (E56/E57)
  func_0024F24C / func_0024F1CC    por nome / por id                    (E60)
    func_0024F028                    ramo normal                        (E53)
      FUN_0024e198                     por elemento, fabrica[tag]
        func_0039E5A8                    cursor=-1 -> NULL              (E45/E51)
      func_0024E3D0                  *(0x40780850+6) = 0                (E59)
        func_00263554                  "rm_." como endereco             (E25/E26)
          func_00220284                  16 escritas UNCOMMITTED        (E8)
```

## Próximo (outra cadeia)

1. **o que sequencia o B71** — porque é que o passo #41 corre depois do `leave`.
   É a única pergunta acima da linha, e é onde o fix vive;
2. as duas rotas latentes (`func_00264BA8` E52, fallback `0x1c4` E53), que
   mordem se a montante mudar;
3. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
