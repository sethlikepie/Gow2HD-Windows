# E62 — o B71 pára no #60, e há uma segunda corrupção mais cedo

> **NUMERO CORRIGIDO em 2026-08-08 pelo [E63](2026-08-08-E63-o-disjuntor-contava-ate-16-e-absorvia-milhares.md):**
> as «16 ocorrencias» eram o **cap do `fprintf`**, nao a contagem -- o disjuntor
> absorvia sem limite e o log mostrava sempre 16. Os numeros reais sao **>= 4096**
> (`ICALL-HEAP`, `"/wad"`) e **>= 8192** (`ICALL-ASCII`, `0x005F6D6F`), em dois
> ciclos apertados na regiao de `FUN_0014e570`.
> **SOBREVIVE:** tudo o resto -- o controlo da corrida de 100s, o B71 parado no #60,
> o `ctr` velho das linhas #059/#060, e a existencia da segunda corrupcao.
> **CAI:** so' a grandeza, e ela cai por tres ordens de grandeza.

**Data:** 2026-08-07 · **Antecessor:** [E61](2026-08-07-E61-o-guard-do-jogo-vigia-a-coisa-errada.md)

Fecho da sessão. Duas medições: uma corrida longa para resolver uma ausência, e
o que ela revelou por acidente.

---

## A ausência, resolvida com controlo

O E61 acabou com uma pergunta: *as fábricas por-tag chegam a ser repovoadas
depois do fecho?* O log de 35s dizia que não — `0x403008E8` e `0x40300E80` com
**zero** eventos depois da linha 20760.

Mas o log acabava na 20960: **200 linhas depois**. Uma ausência no fim de um log
truncado não prova nada.

**Corrida de 100s** (contra 35s):

| | 35s | 100s |
|---|---|---|
| linhas | 20 960 | 21 078 |
| `SCOPE` | 5215 | **5215** |
| `EMPTYFAB` | 259 | **259** |
| `VICTIM` | 4 | **4** |
| `UNCOMMITTED` | 19 | **19** |

**Sessenta e cinco segundos a mais produziram 118 linhas e nenhuma actividade
nova.** O processo não estava truncado a meio do trabalho — está **parado**.

Logo a ausência conta: dentro do alcance da corrida, as fábricas **não são
repovoadas**, e o B71 pára.

## Onde o B71 pára

```
[B71] func_000B71B8 #059 -> func_000B3950  r3=0x00000000
[B71] func_000B71B8 #060 -> func_000B5D10  r3=0x00000000
```

e depois só o ciclo ocioso do `MOVIEFSM`. **Dezanove passos depois do estrago**
(#41), o B71 encalha no #60, com `r3=0`.

## Uma leitura que quase fiz, e a medição travou

Nessas duas linhas o `ctr` vale `0x2F725F7C` — bytes ASCII `"/r_|"`. Texto no
registo de destino de salto é exactamente a assinatura desta cadeia, e era
tentador ler «o B71 acaba a despachar por texto».

**Não é.** `#059` e `#060` são chamadas **directas** (`func_000B3950`,
`func_000B5D10`): o `ctr` não é usado nelas, está velho. E a verificação é
directa — **zero** chamadas indirectas no log inteiro usam esse valor.

## Mas há mesmo despacho por texto, mais cedo

Ao procurar isso, apareceu outra coisa:

```
4277  [ICALL-HEAP] skip ctr=0x2F776164 lr=0x0014F048 r3=0x0A050000 (heap-as-code)
...   16 ocorrencias, linhas 4277-4292, todas com o mesmo ctr
```

`0x2F776164` = **`"/wad"`**. Um caminho de ficheiro no registo de destino de
salto, e desta vez numa chamada **indirecta** a sério.

Quem tenta saltar: `FUN_0014e570` (2116 bytes), `FUN_0014ee3c`, `FUN_0014f020`.

**Isto é uma segunda corrupção, ~16 500 linhas ANTES da nossa** (4277 contra
20778). Não é a mesma cadeia: a nossa começa quando o escopo fecha na 20760.

## E está tapada por um disjuntor nosso

`[ICALL-HEAP] skip ... (heap-as-code)` é um **circuit-breaker do nosso runtime**
que detecta «heap como código» e salta a chamada. Sem ele, o jogo saltaria para
dentro de uma string de caminho.

O `CLAUDE.md` tem a regra escrita:

> *«Remover paliativos antigos NOSSOS (circuit-breakers) quando a evidência
> provar que atrapalham — com prova de não-regressão do baseline.»*

Aqui a evidência não prova que atrapalhe — prova que **esconde**. Dezasseis
despachos por texto absorvidos em silêncio, num sítio que ninguém estava a
investigar. Vale uma auditoria própria.

## Refutado por medição

1. **«as fábricas não são repovoadas»** — era uma ausência no fim de um log
   truncado; **agora** conta, porque 65 segundos a mais dão contagens idênticas
   e o processo está parado.
2. **«o B71 acaba a despachar por texto»** — o `ctr` dessas linhas está velho;
   são chamadas directas, e zero indirectas usam aquele valor.
3. Tudo o que E11–E17 e E19–E61 já refutaram.

## O que fica para a próxima sessão

1. **porque é que as fábricas por-tag não são repovoadas** entre o fim do #38 e
   o #41 — a pergunta do E61, agora com o controlo feito;
2. **o `"/wad"` como código** (linhas 4277–4292, `FUN_0014e570` e vizinhas) —
   segunda corrupção, independente, tapada pelo `ICALL-HEAP`;
3. **auditar o disjuntor `ICALL-HEAP`** — quanto mais é que ele absorve em
   silêncio noutros sítios;
4. o B71 encalhado no **#60** (`func_000B5D10`, `r3=0`);
5. as duas rotas latentes (`func_00264BA8` E52, fallback `0x1c4` E53);
6. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
