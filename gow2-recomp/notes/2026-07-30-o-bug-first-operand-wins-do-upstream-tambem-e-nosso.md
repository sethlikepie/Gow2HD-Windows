# O bug "first TOC-loaded operand wins" do upstream também é nosso — medido

**Data:** 2026-07-30 · **Origem:** leitura do PR #82 de `sp00nznet/ps3recomp`
**Método:** A/B do `ppu_lifter.py` contra o `EBOOT.ELF` real, ~16 s por variante.
O `tools/ppu_lifter.py` **não foi alterado** — a variante é uma cópia.

## Onde apareceu

O PR #82 do upstream (DRAFT, integração da linhagem do `sagemono`) descreve, de passagem,
uma resolução de conflito:

> **Lifter tools** (`ppu_lifter.py` + …): took theirs, then RE-APPLIED our jump-table-base
> fix (their `discover_jump_tables` had the identical **"first TOC-loaded operand wins"**
> bug that dropped flow's app-loop dispatcher).

Ou seja: as duas linhagens do upstream tiveram, independentemente, o mesmo defeito. Valia
a pena ver se a nossa também.

## Temos o bug — `tools/ppu_lifter.py:3505`

Em `discover_jump_tables`, ao decidir qual dos dois operandos do `lwzx` é o registo-base
da tabela:

```python
for cand in (p[1], p[2]):
    ...
    if base_candidates and r_base is None:
        r_base = cand          # <- fixa o PRIMEIRO e nunca reconsidera
```

O comentário imediatamente acima reconhece que gcc emite as duas ordens
(`lwzx rD, base, idx` e o trocado `lwzx rD, idx, base`) e que ambos os operandos têm de
ser tentados — e os `base_candidates` *são* acumulados dos dois. Mas o `r_base`, não: fica
no primeiro que produzir candidato.

E o `r_base` é o que decide `is_offset`:

```python
is_offset = any(w.mnemonic == 'add' and ... and r_base in [...] for w in win)
```

Se o `r_base` for o operando errado, o `add rC, *, base` não casa, a tabela é lida como
**absoluta** em vez de **offset**, os alvos decodificados não validam, e o dispatcher
inteiro é descartado. Silenciosamente — é a mesma família dos `return`s sem contador que a
issue #75 descreve.

## O que o fix recupera, medido

Variante com o `r_base` acumulado numa lista e o `is_offset` a aceitar qualquer um dos
candidatos:

| | actual (com o bug) | com o fix | delta |
|---|---:|---:|---:|
| dispatchers | 137 | **157** | **+20** |
| case targets | 1 537 | **1 745** | **+208** |
| kept internal | 1 299 | 1 436 | +137 |
| case funcs | 237 | **309** | **+72** |
| **funções emitidas** | **51 917** | **51 991** | **+74** |

**+20 dispatchers e +208 alvos de switch que hoje não são lifted de todo.** Cada
dispatcher descartado é um `switch` do jogo que cai no `default` e vai para
`ps3_indirect_tail` em vez de saltar para o case certo.

## O que isto NÃO prova

- **Não são as 2 855 funções perdidas.** Recupera 74. A regressão principal continua por
  explicar.
- **Não está provado in-boot.** Isto é contagem de emissão, não comportamento. Um lift com
  mais funções não é automaticamente melhor — a lição das 1 398 funções em endereços
  inexistentes está fresca.
- **A minha variante é um A/B, não um patch pronto.** Aceitar qualquer candidato em
  `is_offset` é a correcção mínima que demonstra o problema; a correcção a sério
  provavelmente deve escolher o candidato que **valida mais alvos**, não o primeiro que
  case — que é precisamente o erro original, só que ao contrário.

## Porque vale a pena mesmo assim

É um defeito de correcção do lifter, genérico (não é um hack para o GoW2), confirmado
independentemente por duas linhagens do upstream, e com efeito medido no nosso binário.
Encaixa na regra do CLAUDE.md sobre trazer melhorias do original — e na do próprio
upstream: *"Port generic systems, not game-specific hacks."*

E é o tipo de coisa que a nossa parede pode estar a sofrer: a cadeia do 2.º movie passa por
`func_00029AF0`, que **é** um dispatcher de jump table (137 casos hoje). Se 20 dispatchers
estão a ser descartados, algum pode estar nesse caminho.

## O que o cruzamento com as 2 855 mostrou

Das **103** funções que o fix acrescenta, **100 estão na lista das 2 855 perdidas**. Ou
seja, o bug é **parte** da regressão — responde por ~3,5% dela.

Distribuição por zona das 100:

```
29 em 0x13Bxxx    21 em 0x35Fxxx    20 em 0x0B9xxx    11 em 0x383xxx
 8 em 0x13Axxx     7 em 0x0E3xxx     3 em 0x032xxx     1 em 0x382xxx
```

## E uma pista que persegui e que NÃO se confirmou

A zona `0x032xxx` chamou-me a atenção, porque `0x00032690` é um dos três sítios que fazem
`bl 0xCD7B4` (ver `2026-07-30-ponto1-*`). Achei que podia ser a ligação directa.

Não é. Verificado nos três lifts:

| | `func_00032670` (contém o sítio) | chamadas a `func_000CD7B4` |
|---|:---:|---:|
| ANTIGO (**funciona**) | presente | **8** |
| actual (**falha**) | presente | **19** |
| com o fix | presente | **19** |

A função existe nos três, e o lift que **falha** tem **mais** chamadas a `CD7B4` do que o
que **funciona**. A pista está refutada — a cadeia `CD7B4` não está sub-emitida no lift
actual, está sobre-emitida.

(O que, aliás, é mais uma razão para não medir saúde por contagens: mais não é melhor.)

## Próximo passo proposto

1. Verificar quais são os **20 dispatchers novos** e se algum cai no caminho
   intro → 2.º movie → `thr_auto_load`. Grátis, comparando os dois lifts.
2. Se sim, é candidato directo a causa e vai para a Fase 8.
3. Independentemente disso, o fix merece entrar por si — com a versão "escolhe o candidato
   que valida mais alvos", teste de lifter, e o A/B acima como prova de não-regressão.

## Reprodução

```bash
T=$CLAUDE_JOB_DIR/tmp
mkdir -p $T/toolsab && cp ../ps3recomp/tools/*.py $T/toolsab/
# aplicar o fix na CÓPIA $T/toolsab/ppu_lifter_jtfix.py (3 hunks, ver o commit)
python3 $T/toolsab/ppu_lifter_jtfix.py EBOOT.ELF --functions functions.json \
        -o $T/lift_jtfix -j 4
grep -E "jump tables:" $T/lift_jtfix.log
```
