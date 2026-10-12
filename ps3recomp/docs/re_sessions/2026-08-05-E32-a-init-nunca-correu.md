# E32 — a init nunca correu para aquele gestor

> **CONCLUSAO ESVAZIADA em 2026-08-06 pelo [E34](2026-08-06-E34-o-gestor-nao-e-gestor.md).**
> 'A init nunca correu para este gestor' continua factualmente certo, mas e' **irrelevante**:
> `0x400C6B50` nao e' um gestor, logo `func_0022851C` nunca deveria correr para ele.
> **SOBREVIVE:** `POOLSTORE=1` e o unico store ser para `mgr(r31)=0x406387E0`.
> **CAI:** o enquadramento de que faltava uma init.


**Data:** 2026-08-05 · **Antecessor:** [E31](2026-08-05-E31-gestor-meio-inicializado.md)

Sonda própria no store do pool 0 (`PS3_TRACE_POOLSTORE`, sítio único, agulha
ancorada em `lr = 0x002285A8`).

---

## A medição

```
POOLSTORE = 1     GESTOR-MAU = 0
1x  mgr(r31)=0x406387E0
```

`vm_write32(r31 + 0xD0, ...)` em `func_0022851C` executa **uma única vez na
corrida inteira**, e com `r31 = 0x406387E0` — o gestor são. **Nunca com
`0x400C6B50`.**

Na mesma corrida, 238 passagens pelo ponto do flag: 237 com o gestor são, uma
com `0x400C6B50` (`flag=0x4007FCE8`, `pool0=0`).

## O que isto decide

O E31 deixou duas leituras. Escolhe-se agora por medição:

| leitura | veredicto |
|---|---|
| «a init correu e o store não teve efeito» | **refutada** — o store corre 1× e é para o outro gestor |
| «a init nunca correu para este gestor» | **CONFIRMADA** |

Portanto o gestor `0x400C6B50` **existe sem nunca ter passado por
`func_0022851C`**. Alguém o criou e pôs-lhe `+0x12C = 0x4007FCE8` (a tabela de
pools) sem preencher os slots — e é exactamente esse campo que o guard de
`func_00212ED0` lê como "já inicializado".

## A cadeia, com a raiz num só facto

```
existem >= 2 gestores
  0x406387E0  passa por func_0022851C  -> pools criados     (237 usos, sos)
  0x400C6B50  NAO passa                -> +0x12C posto, slots vazios   <- RAIZ
func_00212ED0  guard le' +0x12C != 0 -> salta a init preguicosa
               pool = *(mgr+0xD0) = 0
func_00263554  trata o endereco 0 como pool; *(0) tem "/wad/r_perma.wad_ps3"
               devolve "/r_p"
... seis niveis abaixo: 16 escritas UNCOMMITTED em func_00220284
```

## Duas perguntas, e são de naturezas diferentes

1. **Quem cria `0x400C6B50` e lhe põe `+0x12C`?** Se for código guest, é
   comportamento do jogo e o defeito está no guard (um campo-ponteiro usado
   como booleano). Se for um caminho nosso, é bug de recomp.
2. **Porque é que `func_0022851C` só corre uma vez?** Se o jogo cria dois
   gestores, esperar-se-iam duas inits. Uma só sugere que a segunda chamada não
   acontece — e o E31 mostrou que `func_0022851C` é um **fragmento** cujo `r31`
   é herdado, não estabelecido.

A (2) é a mais promissora como bug de recomp, e liga-se a algo já observado: o
prólogo deste fragmento lê os callee-save da stack em vez de os salvar do
registo. Um fragmento alcançado por um caminho errado corre com o `r31` de
outra pessoa.

**Nenhuma das duas está provada.** O que está medido é que a init corre uma vez,
para o gestor são, e nunca para o mau.

## Refutado por medição

1. **«o store do pool 0 não teve efeito»** — corre 1×, para `0x406387E0`.
2. **«a init preguiçosa cria o pool»** (E31) — não corre; o guard salta-a.
3. Tudo o que E11–E17 e E19–E31 já refutaram.

## Próximo (E33)

1. **Quem chama `func_0022851C`, e quantas vezes?** Uma sonda na entrada
   (já existe `ALCHAIN`, cap 3 — subir o cap) com `r31` e `lr` diz se há uma
   segunda chamada que não chega, ou se só há uma por desenho.
2. **Quem escreve `+0x12C` do gestor `0x400C6B50`?** Agora é um alvo de
   endereço fixo: `PS3_WATCH_EA=0x400C6C7C`, com a vigia validada no mesmo
   regime (E29).
3. (A) do E30 — o path no endereço 0 — continua em aberto.
