# Dois binários byte-idênticos deram veredictos opostos

**Data:** 2026-07-31 · **Descoberto por:** o planeador da Fase 9, ao preparar o bisect.
**Estado: confirmado quanto à identidade; a medição repetida está a correr.**

## O facto

```
$ md5 -q boot_gow2.pre_v3 boot_gow2.pre_v4
ef5f53ef71f1eb3f6230a2ff3ac55dad
ef5f53ef71f1eb3f6230a2ff3ac55dad
$ cmp boot_gow2.pre_v3 boot_gow2.pre_v4 && echo IDENTICOS
IDENTICOS
```

**São o mesmo binário.** E a tabela do `bisect_regression.sh`, corrida sobre os nove,
registou-os assim:

```
boot_gow2.pre_v3   25 Jul 18:23   4111 linhas   StartSeq=2  thr_end=1  r_perma=1   OK
boot_gow2.pre_v4   25 Jul 18:23    813 linhas   StartSeq=0  thr_end=0  r_perma=0   REGRESSAO
```

O mesmo ficheiro, medido duas vezes na mesma sessão, com a mesma recipe, deu
`StartSeq=2 / thr_end=1` numa corrida e `StartSeq=0 / thr_end=0` na outra.

## O que isto implica, se se confirmar

**Não há necessariamente um commit que "partiu" o boot.** A falha pode ser
**não-determinista** — e nesse caso:

1. **O bisect da Fase 9 procuraria um commit que não existe.** Um `git bisect` sobre uma
   falha intermitente converge para um inocente qualquer, com confiança total e resultado
   errado.
2. **Os nove candidatos eliminados foram-no correctamente**, mas por uma razão que não
   sabíamos: não havia nada partido para reverter.
3. **As quatro corridas iniciais desta investigação** (todas `REGRESSAO`, que foi o que
   fez levantar a hipótese de regressão) podem ter sido uma sequência infeliz.
4. **O `boot_gow2.pre_v3` "que funciona"** pode simplesmente ter tido sorte na corrida que
   o classificou.

## O que já apontava para aqui, e não foi seguido

- A **Fase 7 observou flakiness de timing sob carga** e documentou-a — mas tratou-a como
  ruído a descartar, não como sinal sobre a natureza da falha.
- O `smoke_relift_equiv.sh` do marco v1.0 tem um **discriminador FLAKE/REGRESSAO** e um
  portão de **≥4 em 6 corridas** — ou seja, o projecto já sabia que uma corrida só não
  decide. **Todas as medições desta investigação usaram UMA corrida por binário.**
- O `FATAL 0` em todas as corridas: o boot não falha, **pára**. Uma race condition ou um
  deadlock intermitente encaixa; um bug determinista de código gerado encaixa pior.

## O erro de método, e é meu

Corri o bissect inteiro — nove binários — com **uma corrida cada**, e tratei cada
resultado como veredicto. O próprio projecto tinha um portão de 4-em-6 para isto, escrito
no marco anterior, e eu não o apliquei ao bissect.

Pior: **eu escrevi no contexto da Fase 9 que "uma corrida por passo do bisect chega"**,
com a justificação de que o gate de seis é só para o aceite final. Estava errado — se a
falha é intermitente, uma corrida por passo é exactamente o que faz o bisect mentir.

## A medição a decorrer

Cinco corridas do **mesmo** `boot_gow2.pre_v3`, mesma recipe, mesmo ambiente. O resultado
decide o rumo:

| desfecho | significado | consequência |
|---|---|---|
| 5/5 `OK` | o registo do `pre_v4` foi um artefacto isolado | o bisect faz sentido, mas com N corridas por passo |
| mistura `OK`/`REGRESSAO` | **a falha é não-determinista** | o bisect não se aplica; o problema é uma race |
| 5/5 `REGRESSAO` | o `pre_v3` nunca foi bom | a janela inteira está mal posta |

---

# RESULTADO: a falha NÃO é não-determinista. O registo é que estava errado.

Cinco corridas do mesmo `boot_gow2.pre_v3`, mesma recipe, mesmo ambiente:

```
corrida 1: linhas=11073  StartSeq=2  thr_end=1  R_Perm=199
corrida 2: linhas=11428  StartSeq=2  thr_end=1  R_Perm=199
corrida 3: linhas=11616  StartSeq=2  thr_end=1  R_Perm=199
corrida 4: linhas=11861  StartSeq=2  thr_end=1  R_Perm=199
corrida 5: linhas=11054  StartSeq=2  thr_end=1  R_Perm=199
```

**5/5 OK.** O binário é deterministicamente bom. A hipótese do não-determinismo está
**refutada** — e ainda bem, porque a alternativa tornaria o problema muito pior.

## Então o que aconteceu ao `pre_v4`?

A entrada `pre_v4 → 813 linhas → REGRESSAO` na tabela do bisect é um **artefacto de
medição**, não um resultado. O binário é o mesmo do `pre_v3`, que dá 11 000+ linhas de
forma consistente. 813 linhas é uma corrida **cortada**, não uma corrida que falhou.

Causa provável: o sweep dos nove correu enquanto a máquina tinha outras coisas em curso
(esta sessão teve, mais cedo, uma corrida morta por `SIGKILL` externo, provavelmente por
pressão de memória). Uma corrida cortada a meio produz exactamente esta assinatura —
poucas linhas, todos os contadores a zero.

## O que isto corrige, e o que não corrige

**Corrige:** a tabela dos nove binários tem pelo menos uma entrada inválida. Qualquer
conclusão que dependa da linha do `pre_v4` — em particular a delimitação da janela pelo
lado bom — tem de ser refeita. O `boot_gow2_relift_test` (`StartSeq=0`, 3176 linhas) é o
próximo suspeito de ser o mesmo artefacto.

**Não corrige:** a regressão **é real**. O `pre_v3` é bom (5/5) e a produção falha. Há um
ponto de viragem entre eles e o bisect continua a fazer sentido.

## A lição, que continua a ser minha

O erro de método mantém-se, e agora com prova de que produz resultados errados: **corri
o bissect dos nove binários com uma corrida cada**, e uma dessas corridas mentiu. O
projecto tinha o portão de 4-em-6 escrito no marco anterior, e o `smoke_relift_equiv.sh`
tem até um discriminador FLAKE/REGRESSAO com uma assinatura para isto — corrida curta
demais **é** o critério de FLAKE que ele usa.

E eu escrevi no contexto da Fase 9 que "uma corrida por passo do bisect chega". Está
errado e tem de ser corrigido antes de a fase arrancar: **cada passo do bisect precisa de
repetição**, e uma corrida anormalmente curta tem de ser re-corrida, nunca aceite como
veredicto.

## Ressalva

Confirmado: identidade dos ficheiros, e `pre_v3` bom em 5/5. A validação do lado mau
(produção, 3 corridas) está a correr — só com ela é que os dois extremos do bisect ficam
firmes.
