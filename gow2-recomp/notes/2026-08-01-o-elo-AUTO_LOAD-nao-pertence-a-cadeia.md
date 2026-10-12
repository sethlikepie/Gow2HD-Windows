# O elo AUTO_LOAD não pertence à cadeia do menu

Data: 2026-08-01, a seguir à correcção do marcador inexistente.

## Ponto de partida

Corrigido o gate, ele passou a dizer `AUTO_LOAD (nunca criada)` — diagnóstico
específico em vez de um veredicto que não podia mudar. A pergunta passou a ser
*"porque é que a thread nunca chega a ser criada?"*.

## A cadeia, verificada contra o EBOOT.ELF

```
OPD 0x00521768 -> fn 0x00147038                    corpo da thread AUTO_LOAD
ponteiro para o OPD em 0x0053D0F4 = TOC-0x4084
func_00146CB8   lê TOC-0x4084 (entry) + TOC-0x4080 (nome), prio=0x3E9,
                stack=0x4000, chama o wrapper sys_ppu_thread_create
chamadores (bl):  0x000BB1E4  e  0x000BBD70
```

O OPD da própria `func_00146CB8` (`0x00521740`) não é referenciado por ninguém —
morto. Portanto só há dois caminhos, e ambos são `bl` estáticos.

**Sítio 0x000BBD70** (em `func_000BBB00`): excluído. A sonda `PS3_TRACE_ALGATE`
no teste que o guarda deu **zero** numa corrida válida, e o fragmento que ali
desemboca (`0x000BB9EC`) não tem **um único** ramo nem ponteiro em todo o
EBOOT.

> Registo de uma hipótese refutada: cheirava a fallthrough cross-fragment
> perdido pelo lifter — o padrão do fix `2550C8`. **Não é.** Em `0x000BB9E8` há
> um `b 0x000BB6C8` explícito. Verificado desmontando o binário, não deduzido.

**Sítio 0x000BB1E4** (em `func_000BB0B0`): subi a cadeia por xref de `bl` com
fronteiras de função reais (alvos de `bl`, não os fragmentos da tabela do lift):

```
func_00010354 -> func_0025C838 -> func_002B2E04 -> func_002B2DD0
  -> func_000B951C -> func_000B9204 -> func_000B6440/6714
    -> func_000B5294 -> func_000BBDC8 -> func_000BB0B0 -> func_00146CB8
```

## A bissecção

Sonda de entrada nos oito degraus, **mais uma sonda de CONTROLO** numa função
que sei correr (`func_0039E6B4`, medida 3184 vezes). Sem controlo não se lê um
zero — foi a lição das quatro sondas que mentiram hoje.

```
degraus alcançados ......... 0 de 8
CONTROLO ................... 2      <- o instrumento funciona
```

## E porquê: o que está imediatamente antes

```
0x002B2E14   bl 0x00242C94        <- o LOOP PRINCIPAL do jogo
0x002B2E18   nop
0x002B2E1C   lwz r31, ...(r2)
0x002B2E24   cmpwi r0, 0
0x002B2E28   bc  -> 0x002B2E3C
0x002B2E2C   bl 0x002B2DD0        <- a cadeia inteira do AUTO_LOAD
```

`func_00242C94` é o loop principal, decodificado antes nesta sessão: só retorna
quando o jogo recebe **REQUEST_EXITGAME**.

Medição numa corrida saudável:

| | |
|---|---:|
| `SetFlipCommand` | **2619** |
| linhas após a chamada ao loop principal | **0** |
| degraus da cadeia AUTO_LOAD | **0** |
| sonda de CONTROLO | **2** |

O loop corre, desenha 2619 flips, e **nunca retorna**.

## A conclusão, e é desconfortável

**"AUTO_LOAD nunca criada" é o comportamento CORRECTO de um jogo que ainda está
a correr.** A thread só é criada por código que corre *depois* do loop
principal terminar. Um binário que "passasse" este elo teria saído do jogo.

O elo não é uma parede. É a confirmação de que o loop não acabou.

E isto é a segunda avaria de instrumento do dia, e a mais cara: o marco v1.1
chama-se literalmente *"o boot volta a chegar ao AUTO_LOAD"*. O alvo do marco é
um evento de encerramento.

## Onde está o caminho para o menu

No próprio loop principal. `func_00242C94` faz:

```c
loc_00242CFC:
    func_00194D3C(*(r31));            // cellSysutilCheckCallback + cellPadGetInfo2
    r9 = *(r31);
    if (*(uint8*)(r9+4) && *(uint8*)(r9+5)) { *(*(TOC-0x244C)) = 1; return; }  // EXITGAME
    ps3_indirect_call(*(r30+0x460C)); // <- o estado corrente do jogo
    goto loc_00242CFC;
```

O menu tem de sair de `*(r30+0x460C)` — o despacho do estado corrente. É aí que
a investigação continua, e é consistente com `pad_total=0` (o loop chama
`cellPadGetInfo2` mas nenhum input é processado).

## Contabilidade honesta

Não cheguei ao menu. O que esta leva entregou:

- dois defeitos **nossos** removidos (o paliativo TYPE15, o marcador inexistente)
- um elo do gate provado **irrelevante** para o objectivo
- a parede reformulada de "uma thread que não arranca" para "o despacho de
  estado dentro do loop principal"

O gate continua 0/3. Mas três das seis corridas que eu teria gasto a perseguir
o AUTO_LOAD ficam poupadas.
