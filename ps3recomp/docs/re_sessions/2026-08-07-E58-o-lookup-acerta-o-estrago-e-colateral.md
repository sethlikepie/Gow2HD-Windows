# E58 — o lookup acerta: o estrago é colateral e silencioso

**Data:** 2026-08-07 · **Antecessor:** [E57](2026-08-07-E57-o-nome-goGrapplePtActive.md)

Sem corrida nova. A medição já estava em disco: o lift tem um `B71-CALLSITE-TRACE`
neste sítio desde trabalho anterior, e eu corro com `PS3_TRACE_B71=1` desde o E43.

---

## A string, confirmada nas duas pontas

`ghidra_out/strings.json`:

```json
{"addr": "0x004C3E00", "val": "goGrapplePtActive", "refs": ["0x0010F5E8"]}
```

Uma única referência em todo o binário, e é a função do passo #41. E o runtime
passa exactamente esse ponteiro:

```
[B71] func_0010F5E8 #001 -> func_0024F24C r3=0x004C3E00
```

## O retorno — e a surpresa

```
[B71] func_0010F5E8 #002 -> func_0024C878 r3=0x4077AC10     (corrida A)
[B71] func_0010F5E8 #002 -> func_0024C878 r3=0x4077AC50     (corrida B)
```

`r3` aqui **é** o `uVar5` do E57 — o retorno de `FUN_0024f24c`, já guardado em
`*(global + 8)` e a caminho de `FUN_0024c878`.

**Não é zero. Não é `0x1c4`. É um objecto de heap plausível, nas duas corridas.**

## O que isto muda

O lookup por nome **acerta**. `func_002B2BA0` resolve `goGrapplePtActive` para
um id válido, a travessia completa-se, e o B71 recebe um objecto e segue para o
passo #42.

Logo a cadeia dos seis ledgers anteriores não é *«o lookup falha»*. É:

> **A travessia completa-se com sucesso aparente, e estraga outras coisas pelo
> caminho.**

Os elementos visitados cujo `vt[0x48]` deu `NULL` ficaram com o índice de classe
a zero (`*(elem+6) = *(NULL+0x20)`), e é isso que mais tarde leva ao pool 0, ao
`"rm_."` como endereço e às 16 escritas em memória não commitada. Nada disso
chega ao valor de retorno.

O objecto devolvido (`0x4077AC10`) **não é** o objecto que o E41 mediu partido
(`0x40780850`). São vítimas diferentes da mesma travessia.

## Porque é que esta parede foi tão difícil de ver

Nada falha alto. O nome resolve, o lookup devolve um ponteiro, o B71 continua.
O único sinal é `[vm] UNCOMMITTED`, dezassete linhas depois, num sítio
(`func_00220284`) a nove níveis de distância da causa.

É a definição de estrago colateral silencioso — e explica porque é que dez
levas de investigação atacaram o sintoma.

## Nota de método

Esta medição **não custou uma corrida**. O trace já existia no lift, a env var
já estava ligada nos meus scripts, e a linha estava nos logs de duas corridas
anteriores. Antes de instrumentar, vale sempre a pena procurar no que já se
mediu — foi a segunda vez nesta sessão (a primeira foi o E49, que saiu de
reanalisar o `lr` que a sonda já imprimia).

## Refutado por medição

1. **«o lookup de `goGrapplePtActive` falha / devolve 0»** — devolve
   `0x4077AC10` e `0x4077AC50` em duas corridas.
2. **«correu o fallback `0 + 0x1c4`»** — reconfirmado por outro lado: o retorno
   é um objecto de heap, não `0x1c4` nem derivado dele.
3. **«o nome não está no binário / é outro»** — `0x004C3E00`
   `"goGrapplePtActive"`, referência única, de `0x0010F5E8`.
4. Tudo o que E11–E17 e E19–E57 já refutaram.

## Próximo

1. **quais os elementos que a travessia estraga** — sonda no
   `vm_write16(gpr[25] + 0x6, 0)` de `func_0024E3D0`, a imprimir o objecto
   afectado. Liga as vítimas ao `0x40780850` do E41;
2. quem chama `func_0041F924` (precisa de `ra3` na sonda `SCOPE`);
3. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
