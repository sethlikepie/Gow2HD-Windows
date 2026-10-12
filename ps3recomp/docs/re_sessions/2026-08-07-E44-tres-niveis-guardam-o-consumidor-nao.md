# E44 — três níveis guardam de propósito; o consumidor não guarda nenhuma vez

**Data:** 2026-08-07 · **Antecessor:** [E43](2026-08-07-E43-o-cursor-nunca-passa-abaixo-de-zero.md)

Leitura do lift (`recomp_macos_v2`) + duas corridas de 35s com `boot_gow2_e41`.
**Paga a dívida que o E43 deixou:** o produto nulo deixa de ser herdado da nota
de 2026-08-01 e passa a medido nesta cadeia.

---

## Os três guards, lidos no lift

| nível | função | o que faz com o vazio |
|---|---|---|
| decremento | `func_0039DAB0` | compara com 0 e **sai sem escrever** se já for negativo |
| leitura | `func_0039E5A8` | `if (cursor < 0) goto fim` — **devolve 0 por contrato** |
| consumidor | `func_0024E3D0` | `vm_read32(gpr[3] + 0x20)` — **sem check nenhum** |

`func_0039E5A8`, na íntegra do que importa:

```c
ctx->gpr[10] = 0;                                  // retorno default
ctx->gpr[0]  = (int64_t)(int8_t)vm_read8(base+0x80);   // cursor com sinal
if (((ctx->cr >> 0) & 8)) goto loc_0039E5D0;       // < 0 -> salta a leitura
ctx->gpr[10] = vm_read32(base + cursor*4);
loc_0039E5D0:
ctx->gpr[3] = ctx->gpr[10];                        // devolve 0 se vazio
```

Devolver NULL com a pilha vazia **é o contrato**, não uma falha. E
`func_0024E3D0` dereferencia o retorno na instrução seguinte à chamada.

## A medição — 3 produtos nulos, com controlo

Corrida com `PS3_TRACE_PRODUCT=1` e `PS3_TRACE_B71=1`, cap 100000.

`func_0024E3D0` chama a vcall **4 vezes na corrida inteira**:

```
12388: [B71] func_0024E3D0 -> ps3_indirect_call r3=0x400C6210 ctr=0x0039E5A8
12389: [B71] func_0024E3D0 -> ps3_indirect_call r3=0x403008E8 ctr=0x0039E5A8
12449: [B71] func_0024E3D0 -> ps3_indirect_call r3=0x400D78D0 ctr=0x0042F698  <- outro alvo
12450: [B71] func_0024E3D0 -> ps3_indirect_call r3=0x40300E80 ctr=0x0039E5A8
```

Três vão a `func_0039E5A8`. **Nenhuma das três produz linha `[PRODUCT]`** — a
última do log inteiro é a 12335.

A sonda `PRODUCT` vive **dentro do ramo não-negativo** (é escrita depois do
`goto loc_0039E5D0`), logo o silêncio significa: cursor negativo, retorno 0.

### O controlo, porque uma ausência sozinha não vale

- **as três fábricas aparecem na sonda mais cedo na mesma corrida** —
  `0x400C6210` na linha 5488 (`cursor=0 produto=0x42F85590`), `0x403008E8` na
  5486, `0x40300E80` na 5485 e 12335. O instrumento cobre estes objectos;
- **o tecto não se esgotou** — 253 impressões contra 100000;
- **o regime da falha está exercitado** — as 16 escritas `UNCOMMITTED` em
  `func_00220284`, o sintoma que o E8 abriu, estão nesta corrida.

Ausência com positivo conhecido no mesmo regime. Conta.

## O que isto muda

**Este sítio nunca acerta.** Das 3 chamadas a `vt[0x48]`, 3 devolvem nulo. Não é
uma janela rara nem uma corrida perdida por pouco — é sistemático.

E `0x400C6210` **teve** produto às 5488 (`cursor=0`) e não tem às 12388. A
pilha esvaziou-se entretanto — legitimamente, como o E43 mostrou. A pergunta
fica na sua forma mais estreita até agora:

> **Porque é que `func_0024E3D0` corre quando esta fábrica está vazia — todas as
> vezes que corre?**

Não é contagem (E43), não é underflow (E43), não é um push saltado: o multiset
de valores é um LIFO perfeitamente encaixado (`-1↔0` onze vezes, `0↔1` 165
vezes, 176 de cada lado). É **ordem**: alguém pede produto fora da janela em que
a fábrica tem produto.

## Confirma o E41, não o retracta

O E41 disse *«o defeito não está em `func_0024E3D0` — está a montante»*. As três
guardas deliberadas sustentam-no: a montante tudo cumpre contrato. A ausência de
check no consumidor é a invariante do jogo (*«o produto existe aqui»*) escrita em
código — e é por isso que chegar aqui vazio é o defeito, não o deref.

## Dívida de instrumento

A sonda `PRODUCT` **não consegue observar o caso que interessa**. Tudo o que
aqui está sobre o caso nulo é inferido de ausência com controlo, não visto.
Antes de construir mais um nível: mover a sonda para **antes** do
`if (cursor < 0)`, ou pôr uma segunda em `loc_0039E5D0`, e reconstruir.

## Correcção a uma leitura minha desta sessão

Na primeira passagem li a distribuição de fábricas com `sort | uniq -c | head` e
concluí que `0x400C6210` **não aparecia** nas 253 chamadas. Falso: aparece uma
vez, e o `head` cortou-a aos 10. Foi o `head` a fabricar a ausência, não a
corrida. A conclusão certa está acima, e mudou de sinal.

## Refutado por medição

1. **«`func_0039E5A8` não tem guard para o vazio»** — tem, e é explícito.
2. **«a fábrica `0x400C6210` nunca é consultada nesta corrida»** — é, às 5488,
   com produto válido (retracção da minha própria leitura, acima).
3. **«o nulo é uma janela de corrida rara»** — 3 em 3 chamadas do sítio.
4. Tudo o que E11–E17 e E19–E43 já refutaram.

## Próximo

1. **mover a sonda `PRODUCT` para antes do ramo** e reconfirmar por observação
   directa o que aqui está por ausência;
2. o `lr` de quem chama `func_0024E3D0` nas 4 vezes — qual é o caminho que pede
   produto a uma fábrica vazia;
3. quem empurra para `0x400C6210` (as 176 pushes) e porque é que nenhuma cai
   antes da linha 12388;
4. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
