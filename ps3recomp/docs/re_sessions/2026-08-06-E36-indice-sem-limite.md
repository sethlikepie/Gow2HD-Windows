# E36 — o lookup trata o miss; o que falta é o limite do índice

> **HIPOTESE REFUTADA em 2026-08-06 pelo [E38](2026-08-06-E38-o-indice-muda.md).**
> Nao e' indice fora de alcance: `idx=0` esta' **dentro**, e e' a mesma celula que outras
> chaves usam com sucesso (`#19`, `#38` recebem a mesma entrada). Existe de facto um indice
> absurdo na corrida (`24444`, uma vez) mas e' **outra chamada**.
> **SOBREVIVE:** a leitura das 23 linhas do lookup -- a chave e' um ponteiro, o indice e'
> `*(chave+6)` (halfword), e nao ha' verificacao de limite. **CAI:** que a ausencia de
> verificacao explique ESTE caso.


**Data:** 2026-08-06 · **Antecessor:** [E35](2026-08-06-E35-a-chave-nao-registada.md)

Leitura de `func_0039D3C4` (23 linhas). **Corrige a hipótese do E35.**

---

## O lookup, inteiro

```c
r8  = 0                                  // valor por omissao
r10 = *(tab + 0x24)                      // base
r0  = *(tab + 0x44)                      // selector de linha
r9  = (r0*16 - r0*4) + r10               // = r0*12 + base
r11 = vm_read16(key + 0x6) << 2          // <- a CHAVE e' dereferenciada:
                                         //    halfword em +6 = indice de classe
r0  = *(r9 + 0)                          // base da linha
r11 = *(r11 + r0)                        // entrada = linha[indice]
if (r11 == 0) goto fim                   // MISS -> devolve 0
r8  = r11 - 4                            // HIT
fim: return r8
```

## O que isto refuta

O E35 propôs: *«`func_0039D3C4` devolve lixo em vez de falhar»*. **Errado.** O
`r8` é inicializado a 0 e devolvido quando a entrada é nula. **O miss é
tratado.**

Logo, na chamada má, **não houve miss**: a tabela devolveu `0x400C6B54` como
entrada não-nula, e o chamador recebeu `0x400C6B50` (`= entrada - 4`).

## O que sobra, e é mais preciso

Duas coisas novas, das 23 linhas:

**1. A chave não é uma chave — é um ponteiro para objecto.** O índice real é
`*(key + 6)`, um **halfword**. As "chaves" que a sonda do E35 mostrou
(`0x40780850`, `0x4077…`) são endereços de objectos; o que indexa é o campo
`+6` de cada um.

**2. Não há verificação de limite.** `*(key+6) << 2` é somado à base da linha
sem qualquer comparação com o tamanho. Um índice fora de alcance lê **para lá
do fim do array** — e o que está a seguir em memória é, precisamente,
contabilidade do alocador (`0x400C6B54`, escrito por `func_00263178`, medido no
E34).

Isto encaixa em tudo sem sobra: índice grande → leitura fora do array → valor
não-nulo → tratado como entrada válida → `-4` → "gestor".

**Não está provado.** Falta medir `*(0x40780850 + 6)` e o tamanho da linha. É
uma sonda de duas leituras no mesmo sítio.

## A cadeia, na forma mais precisa até agora

```
objecto 0x00514E80 (PAREDE 1) -> *(+0x14) = 0x40780850
func_0039D3C4  indice = *(0x40780850 + 6)  (halfword)
               SEM limite: linha[indice] le' fora do array          <- RAIZ provavel
               devolve memoria do alocador - 4  =  0x400C6B50
func_00212ED0  le'-a como gestor; pool = *(+0xD0) = 0
func_00263554  endereco 0 como pool; *(4) = "/r_p"
... ate' as 16 escritas UNCOMMITTED em func_00220284
```

## Refutado

1. **«o lookup devolve lixo em vez de falhar»** (E35) — trata o miss, devolve 0.
2. **«a chave não está registada»** (E35) — não houve miss; leu-se fora do array.
3. **«a chave é um id»** — é um ponteiro; o índice é `*(chave+6)`.

## Próximo

1. **Medir `*(0x40780850 + 6)`** e o tamanho da linha da tabela. Uma sonda em
   `func_0039D3C4` com o índice e o endereço final decide em uma corrida.
2. Se o índice for absurdo, a pergunta sobe para **quem preenche `+6` do objecto
   `0x40780850`** — e liga-se outra vez à parede 1.
3. O flake `FIOSOPEN=14`, ~1 em 6 (E33), continua por explicar.

## Nota

Este ledger saiu de ler 23 linhas, e só foi possível porque a medição do E35
nomeou a função. Segunda vez na série que a leitura resolve o que a medição
cercou — e segunda vez que refuta a hipótese que a própria medição sugeriu.
