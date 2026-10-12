# E12 — `pool = 5`: uma contagem usada como ponteiro

**Data:** 2026-08-05 · **Antecessor:** [E11](2026-08-05-E11-nem-string-nem-lifter.md)
**Predições:** [E12-predições](2026-08-05-E12-predicoes.md), commit `b809f2a`, escritas antes da corrida.

Causa raiz encontrada, com a cadeia fechada de ponta a ponta e um controlo que
passa. **Nenhuma das duas predições acertou** — e o ramo "nenhuma das duas"
deu uma resposta melhor do que qualquer uma delas previa.

---

## A linha

```
[FLHEAD] TEXTO pool=0x00000005 pool+4=0x00000009 head=0x726D0D2E lr=0x00218364
```

**`pool = 5`.** Não é um endereço de heap (P-A) nem de região estática (P-B) —
é o **inteiro cinco**. Logo `pool+4 = 9`, e `func_00263554` lê `*(9)` como
cabeça da free-list, tirando `0x726D0D2E` da memória baixa (que está committed,
por isso não há aviso na leitura — só nas escritas, muito depois).

A sonda `FLHEAD` já trazia um classificador para este caso e chamou-lhe `TEXTO`.
O rótulo estava escrito à espera; faltava alguém correr a sonda com tecto que
chegasse.

## A contagem

| rótulo | nº |
|---|---|
| `[FLHEAD] ok` | **8624** |
| `[FLHEAD] TEXTO` | **1** |

**Um pop patológico em 8625.** Mesma forma da parede 4, onde a contagem também
deu `n = 1`. E os únicos outros `pool` não-ponteiro são dois `0x00000000`, que o
código já trata.

`lr = 0x00218364` — um único sítio de chamada, e um endereço que sessões
anteriores já tinham marcado: existem `patch_218340_pool_index_probe.py` e
`patch_218364_pool_null_gate.py` no catálogo.

## Controlo (P-C) — passa

Era a condição que eu tinha escrito para os números valerem alguma coisa:

| | |
|---|---|
| `head` do `FLHEAD TEXTO` (linha 39134) | `0x726D0D2E` |
| base da 1ª escrita `UNCOMMITTED` (linha 39138) | **`0x726D0D2E`** |
| `[CPY284] #13`, mesma corrida | `this=0x00000000` |

Mesmo valor, ordem correcta, quatro linhas de distância. A cadeia fica provada:

```
lr=0x00218364        pool = *(tabela…) devolve 5   <- uma CONTAGEM, não um ponteiro
func_00263554        head = *(pool+4) = *(9) = 0x726D0D2E   (memória baixa)
                     o "objecto alocado" passa a ser esse valor
func_002210BC        grava campos; this fica inválido
func_00220284        trata como ponteiro de floats -> 16 escritas UNCOMMITTED
```

## O controlo falhado que salvou a medição

A **primeira** corrida deste E12 tinha `PS3_TRACE_FLHEAD_CAP=400`. A sonda
imprimiu 409 linhas e parou na linha **3987**; o primeiro acesso mau estava na
**4866**. Nenhuma `FLHEAD` mostrava `head=0x726D…`.

Nessa altura todos os `pool` impressos estavam em heap (`0x4xxxxxxx`) — eu tinha
**P-A servido de bandeja**, e teria escrito "o campo é de um objecto de heap"
sobre uma medição que não cobria sequer o evento. O que impediu isso foi o
controlo P-C ter sido escrito **antes**, e ter falhado.

Com o tecto a 100000: 8625 pops, cobertura até à linha 39267 contra o evento na
39134. Aí o `TEXTO` apareceu.

> A distinção que fez a diferença: um controlo falhado por **cobertura** é
> *inconclusivo*, não *negativo*. As duas coisas parecem-se — em ambas o sinal
> procurado está ausente — e levam a conclusões opostas. Separá-las custou
> comparar dois números de linha.

## Predições: veredicto

| | previsto | resultado |
|---|---|---|
| **P-A** heap → largura de store / lifetime | — | **refutada** |
| **P-B** estático → layout/offset | — | **refutada** |
| **P-C** controlo | tem de bater | **passa** |
| nenhuma das duas | alvo muda | **acertou**, e o alvo é mais acima do que eu supunha |

O E11 tinha proposto H3 (escrita estreita sobre a cabeça da free-list). **Também
está errada**: a palavra não é sobrescrita — é **lida do sítio errado**, porque o
ponteiro base é 5. Os três bytes constantes (`r`, `m`, `.`) e o byte variável são
simplesmente o que está em memória baixa no endereço 9, e não uma palavra
corrompida.

## Classificação dos candidatos a fix

| tipo | o que muda | nota |
|---|---|---|
| **Raiz** | porque é que `*(tabela+idx)` em `0x00218364` devolve 5 | onde o trabalho deve ir |
| **Cirúrgico** | rejeitar no pop um `pool` que não é ponteiro plausível | 1 ponto de decisão, reversível |
| **Paliativo** | `patch_218364_pool_null_gate.py` | já existe, gated — **não é fix** |

O gate paliativo já está no catálogo e OFF. Continua a ser instrumento de
diagnóstico, não solução: com ele ligado o objecto continua mal classificado e o
sintoma reaparece noutro sítio.

## Refutado por medição — não voltar a perseguir

1. **H3 (escrita estreita sobre `*(pool+4)`)** — a palavra não é corrompida; é
   lida de `*(9)` porque `pool = 5`.
2. **P-A / P-B** — `pool` não é heap nem estático; é um inteiro pequeno.
3. **`func_00220284` como sítio do bug** — é a vítima final, 4 níveis a jusante.
4. **A free-list estar corrompida** — 8624 de 8625 pops são `ok`.

## Próximo (E13)

1. **Porque é que `*(tabela+idx)` devolve 5 em `0x00218364`?** É a pergunta raiz.
   `patch_218340_pool_index_probe.py` já existe — correr e ver `idx` e a tabela.
   Hipótese a testar: o índice é calculado a partir de um campo que às vezes
   guarda uma contagem, e lê-se a célula errada da tabela.
2. Confirmar o `n = 1` em 3 corridas (aqui foi medido numa; a segunda corrida
   foi um flake de 1886 linhas).
3. `*(r30+0x460C)` em `func_00242C94` — o caminho declarado para o menu.
