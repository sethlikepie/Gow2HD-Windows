# E68 — a cadeia não depende do regime: o alarme do E67 era só sobre os ciclos

**Data:** 2026-08-08 · **Antecessor:** [E67](2026-08-08-E67-o-callback-com-flush-e-o-que-o-NO_RSX-amplificou.md)

O E67 admitiu que os ciclos do GCM eram específicos do `PS3_NO_RSX=1` e deixou
a pergunta certa a seguir: **o que mais é que eu medi só nesse regime?** A
resposta é «tudo desde o E43» — incluindo a cadeia inteira dos E41–E61.

Medido. **A cadeia não depende do regime.**

---

## A primeira leitura, e porque não a publiquei

As corridas com backend do E67 davam `UNCOMMITTED=0`, contra 19 no `NO_RSX`.
Parecia dizer que a cadeia toda era artefacto do meu setup.

Mas essas corridas tinham as sondas **desligadas** e produziram 3209/4095 linhas
contra 20296 — não chegaram tão longe. Uma ausência sem controlo, outra vez, e
desta vez não caí nela.

## A corrida controlada

Mesmas sondas, mesmo binário, 60s, só o regime a mudar:

| | `PS3_NO_RSX=1` | `PS3_RSX_BACKEND=metal` |
|---|---|---|
| linhas | 12 664 | 12 686 |
| B71 max | **#060** | **#060** |
| `EMPTYFAB` | **259** | **259** |
| `VAZIO` | **6** | **6** |
| `VICTIM` | **4** | **4** |
| `UNCOMMITTED` | **19** | **19** |
| `st620` max | **11** | **11** |

**Idênticos.** Não «parecidos» — os mesmos números em todas as contagens.

## O que isto arruma

**A cadeia dos E41–E61 é independente do regime.** As fábricas por-tag vazias,
os 6 pedidos nulos, as 4 vítimas, as 16 escritas em memória não commitada e o
B71 a parar no #60 acontecem igual com o backend Metal a consumir o FIFO.

Logo o que o E67 admitiu **aplica-se só aos ciclos do GCM** (E64), e não se
espalha para trás. A distinção que o E64 fez — *«não é a mesma parede»* — ganha
uma segunda confirmação, por um caminho que eu não tinha previsto.

## O que fica dito sobre o método

Levantei um alarme sobre o meu próprio trabalho, com base numa ausência, e
depois **não construí sobre ele antes de o medir**. Foi o custo de uma corrida
de 60s por regime, e o resultado foi que o alarme não procedia.

Vale a pena separar as duas coisas: *desconfiar* do regime foi correcto — o E67
provou que ele altera pelo menos um resultado. *Concluir* que ele altera tudo,
a partir de logs mais curtos e sem sondas, teria sido a mesma falha que
diagnostiquei três vezes hoje.

## Refutado por medição

1. **«a cadeia dos E41–E61 é artefacto do `PS3_NO_RSX=1`»** — todas as contagens
   idênticas nos dois regimes, com as mesmas sondas e a mesma duração.
2. **«`UNCOMMITTED=0` com backend»** — era efeito de corridas mais curtas e sem
   sondas (3209/4095 linhas contra 12 686), não do regime.
3. Tudo o que E11–E17 e E19–E67 já refutaram.

## Próximo

1. o `spu-dma SKIP`, que não reproduziu (E66) — agora com dois regimes para
   comparar;
2. promover o default do `PS3_GCM_CB` (corrida longa, dois OS);
3. a parede dos E41–E61, intacta e agora confirmada como real nos dois regimes:
   fábricas por-tag não repovoadas, B71 no #60;
4. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
