# E15 — a string está no endereço 4: tudo sai de ponteiros nulos

> **REPOSTO NO ESSENCIAL em 2026-08-05 pelo [E29](2026-08-05-E29-a-string-esta-no-endereco-zero.md).**
> A retractacao do E17 assentava na vigia de traducao, que se provou dar FALSOS NEGATIVOS.
> Medido depois: a string esta' no endereco **0** (`/wad/r_perma.wad`), nao no 4, e e' de
> facto consumida como ponteiro. O erro do E15 foi o offset (4 bytes) e o autor da escrita
> (ainda por provar); o nucleo estava certo.


> **RETIRADO em 2026-08-05 pelo [E17](2026-08-05-E17-retracto-do-E15.md).**
> O modelo abaixo exige uma leitura em `*(9)` que **não acontece**: o guard
> `vm_uncommitted()` barra-a e `vm_read32(9)` devolve 0 sem traduzir, medido
> com a vigia de tradução (zero acertos, controlo a passar). Mantido no repo
> como registo do erro e do que o apanhou — não usar como base.

**Data:** 2026-08-05 · **Antecessor:** [E14](2026-08-05-E14-o-caminho-como-objecto.md)

Modelo fechado. Nenhuma corrida nova — só aritmética sobre valores já medidos,
que bate nos dois sem qualquer ajuste.

---

## As duas janelas co-ocorrem

Na mesma corrida (`/tmp/e14_run1.log`), a duas linhas de distância:

```
linha 3369   this = 0x2F725F70   "/r_p"
linha 3371   base = 0x726D612E   "rma."
```

## A aritmética que fecha

Do E12: `pool = 5`, e portanto `head = *(pool+4) = *(9)`.

Se `*(9)` devolve os bytes de offset 5 da string, a string começa no endereço
**4**. Testado contra as duas medições:

| endereço | bytes da string | valor | medido |
|---|---|---|---|
| `*(4)` | `/r_p` | `0x2F725F70` | **= `this` (E14)** |
| `*(9)` | `rma.` | `0x726D612E` | **= `head` (E12, E14)** |

Dois valores independentes, medidos em sessões diferentes, previstos pelo mesmo
modelo de uma só hipótese. Não houve parâmetro a afinar.

## O modelo

**A string `/r_perma.wad` está escrita na memória baixa do guest, a partir do
endereço 4.** Tudo o resto é consequência de ler campos de um objecto nulo:

```
objecto NULO
  *(null + 4)  -> 0x2F725F70  "/r_p"   -> usado como this
  *(null + 9)  -> 0x726D612E  "rma."   -> usado como head da free-list
```

Isto liga-se directamente ao E10, onde a sonda `CPY284` registou
`this = 0x00000000 <THIS-MAU>` na 13ª chamada. Não eram dois problemas: **é o
mesmo**. Ler campos de `NULL` devolve memória baixa, e a memória baixa tem lá
um caminho.

## A cadeia inteira, do princípio

```
[?] alguem escreve "/r_perma.wad" atraves de um ponteiro NULO
    -> a string fica no endereco guest 4
[?] alguem le' campos de um objecto NULO
    *(4) = "/r_p"  -> this        (E14)
    *(9) = "rma."  -> head        (E12)
    pool = 5                       (E12: uma "contagem" que e' so' mais um
                                    byte da memoria baixa)
func_002182A4  descritor r29 com divisor 130, base 0x4007FCE8   (E13)
func_00263554  head da free-list = texto                        (E12)
func_002210BC  this invalido                                    (E10)
func_00220284  16 escritas UNCOMMITTED, stride 0x10             (E8)
```

**Os dois `[?]` são o que resta.** E são a mesma família de defeito: uma escrita
e uma leitura através de nulo. A escrita é a mais interessante — se ninguém
tivesse escrito a string no endereço 4, a leitura de nulo daria zeros e o
sintoma seria um crash limpo em vez de 16 escritas em memória não commitada.

## Porquê `pool = 5`

O E12 chamou-lhe "uma contagem usada como ponteiro" e o E13 manteve. **Está
errado, ou pelo menos é uma coincidência.** `pool = *(base)` onde `base` vem do
descritor mau; o `5` é simplesmente mais um byte de memória baixa, não uma
contagem que alguém pôs lá com sentido. Corrijo aqui porque a formulação
anterior sugeria procurar "quem escreveu a contagem", e isso é perseguir um
fantasma.

## Refutado por medição — não voltar a perseguir

1. **`pool = 5` ser uma contagem com significado** — é conteúdo de memória baixa.
2. **Os valores maus serem lixo aleatório** — são `*(4)` e `*(9)` de uma string
   conhecida, previstos por um modelo de um parâmetro.
3. **Serem dois problemas distintos** (o `this=NULL` do E10 e o texto-como-
   ponteiro do E8) — é o mesmo, visto de dois sítios.
4. Tudo o que E11–E14 já refutaram.

## Próximo (E16)

1. **Quem escreve `/r_perma.wad` no endereço guest 4?** É a pergunta com maior
   retorno: é uma escrita através de nulo, e o runtime já sabe detectar acessos
   a memória baixa. Um watch em escritas a `[0, 0x40)` com o `lr` do guest
   nomeia o culpado numa corrida.
2. Quem lê campos de um objecto nulo em `func_002182A4` — provavelmente cai
   sozinho quando (1) for respondido, por ser o mesmo objecto.
3. `*(r30+0x460C)` em `func_00242C94` — o caminho declarado para o menu.

## Nota de método

Este ledger não custou uma única corrida. Os dois valores já estavam medidos
desde o E8 e o E14; faltava pô-los lado a lado e perguntar que endereço os
explicaria aos dois. **Vale a pena, antes de instrumentar mais, tentar um
modelo com um parâmetro que preveja as medições que já existem** — se bater sem
afinação, é quase de certeza o mecanismo.
