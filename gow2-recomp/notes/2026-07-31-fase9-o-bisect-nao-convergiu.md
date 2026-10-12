# O bisect NÃO convergiu — apesar de o git dizer que sim

**Data:** 2026-07-31 · **Veredicto: NÃO-CONVERGÊNCIA.** O `0c6a01b` que o `git bisect`
nomeia **não é a causa** e não deve ser tratado como tal.

## O que o git imprimiu

```
0c6a01bd024baf03bb64bb0a64bfda6f2f9a88a2 is the first bad commit
bisect found first bad commit
```

## O que o log de decisões mostra

```
git bisect good 8648805    <- o extremo inicial, dado por nós
git bisect skip 6f05874
git bisect skip dac4e78
git bisect skip bf995bc
git bisect skip 7a11626
git bisect skip 921a215
git bisect skip 151ee0c
git bisect bad  0c6a01b
```

**Seis `skip` e um único `bad`. Zero `good` além do extremo que demos à mão.**

O `git bisect` não encontrou um ponto de viragem — encontrou **o único commit que o
oráculo conseguiu avaliar**, e esse deu `bad`. Com todos os outros a `skip`, o algoritmo
não tem alternativa senão apontá-lo.

Isto é o desfecho **não-convergência** previsto no critério 4 da Fase 9. O `0c6a01b` é o
sobrevivente de uma amostra de um, não um culpado.

## Coincidência que merece registo

`0c6a01b` é **o mesmo commit** que a primeira tentativa (com o oráculo defeituoso)
nomeou. Mas por razão diferente:

| tentativa | mecanismo | resultado |
|---|---|---|
| 1.ª | oráculo devolvia exit 2 em todos os passos; o git lê 2 como `bad` | tudo `bad` ⇒ converge no primeiro |
| 2.ª (esta) | oráculo funciona, mas 6 de 7 commits não são avaliáveis | tudo `skip` menos um ⇒ converge nesse |

**Dois mecanismos de erro diferentes, o mesmo commit falso.** É um lembrete de que a
saída `is the first bad commit` não carrega, em si, nenhuma garantia.

## Porque tantos skips

Da leitura do log, os commits testados não são avaliáveis pelo oráculo. As razões que
aparecem no log incluem incompatibilidades de estado entre o commit em teste e o resto da
cadeia — o `apply_all_patches.sh` corre contra um lift daquela era e produz
`FAILED=3`/`UNVERIFIED=17`, e vários patches procuram agulhas que ainda não existem
(`MISSING (5/7)` no `patch_b71_skip_icallb_reuse.py`, sem escritor conhecido no repo
nessa altura).

Ou seja: **os patches de hoje não se aplicam à história de então**, e o oráculo — bem —
recusa-se a dar veredicto sobre um binário que não representa aquele commit.

## O que isto significa

O bisect por reconstrução completa **não é aplicável a esta janela**, porque a cadeia de
build de hoje (89 patches, agulhas de hoje) não é reconstruível contra commits antigos. O
oráculo está correcto ao recusar; o método é que não serve.

## O que sobra, honestamente

Depois de **nove candidatos eliminados por medição** (Fases 6 e 8) e de um bisect que não
converge por incompatibilidade estrutural, as vias que restam são:

1. **Bisect só do runtime**, mantendo lift+patches fixos nos de hoje. Isola a variável
   `runtime/`+`libs/` sem exigir que os patches antigos se apliquem. Mais barato (relink)
   e sem o problema das agulhas. **É a via mais promissora.**
2. **Comparar os dois binários que temos** — `boot_gow2.pre_v3` (bom, 5/5) e produção
   (mau, 3/3) — ao nível dos símbolos e do que cada um carrega, em vez de tentar
   reconstruir a história.
3. **Instrumentar o ponto de divergência.** Sabemos que o bom chega ao `thr_auto_load` e o
   mau não. Um probe no caminho, comparando as duas execuções lado a lado, diz onde
   divergem — sem precisar de saber que commit as separou.

A (3) tem uma vantagem que as outras não têm: **não depende de o bisect ser possível.**

## Ressalva

Nada aqui diz que a regressão não existe. `pre_v3` dá `thr_end=1` em 5/5 e a produção dá
`thr_end=0` em 3/3 — a diferença é real e reprodutível. O que falhou foi **este método**
de a localizar.
