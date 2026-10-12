# E29 — a string está no endereço 0, e eu retirei duas conclusões certas

**Data:** 2026-08-05 · **Antecessor:** [E28](2026-08-05-E28-a-linha-que-nao-fecha.md)

Sonda própria, sítio único, com leitura directa da memória baixa. **O mecanismo
fecha**, e obriga-me a repor dois ledgers que retirei com base num instrumento
defeituoso.

---

## A medição

```
[POP212] #238 mgr=0x400C6B50 off=0xD0 pool=0x00000000 -> ret=0x2F725F70 "/r_p" <ENVENENADO>
[POP212] ..pool NULO: vm_read32(4)=0x00000000  vm_read32(0)=0x2F776164
```

`vm_read32(0)` = `0x2F776164` = **`"/wad"`**.

E a string encaixa nas duas famílias de valores da série inteira:

| endereço | bytes | valor | onde apareceu |
|---|---|---|---|
| `*(0)` | `/wad` | `0x2F776164` | medido agora |
| `*(4)` | `/r_p` | `0x2F725F70` | o `ret` envenenado (E14, E25) |
| `*(9)` | `rma.` | `0x726D612E` | o `head` do E8/E12 |

**A string no endereço guest 0 é `/wad/r_perma.wad`.** Um único facto explica os
dois valores que a série persegue desde o E8.

## O mecanismo, completo

```
1. algo escreve "/wad/r_perma.wad" no endereco guest 0        (por explicar)
2. *(mgr + 0xD0) == 0   -> o ponteiro do pool indice 0 e' NULO
3. func_00263554(0) trata o endereco 0 como um pool:
      head = *(0 + 4) = "/r_p"          <- nao e' zero, logo nao e' "lista vazia"
      next = *(0x2F725F70)              <- UNCOMMITTED, devolve 0
      *(0 + 4) = 0                      <- o pop ESCREVE na memoria baixa
4. devolve "/r_p" como objecto alocado
```

O passo 3 explica porque é que a minha sonda leu `*(4) = 0`: **o pop já a tinha
consumido e zerado**. A leitura de `*(0)` sobreviveu porque o pop não lhe toca.

E explica a outra família: com `pool = 5` (E12), `head = *(9)` = `"rma."`.

## Duas retractações minhas que estavam erradas

O [E17](2026-08-05-E17-retracto-do-E15.md) retirou o E15, e o
[E28](2026-08-05-E28-a-linha-que-nao-fecha.md) retirou a leitura do E12. **Os
dois argumentos assentavam na vigia de tradução** (`PS3_WATCH_LOWPTR`), que
reporta zero traduções abaixo de `0x1000`.

Essa vigia **dá falsos negativos**, e está agora provado:

- está no binário (`strings` confirma `PS3_WATCH_LOWPTR`);
- **dispara** — controlo com limite alto dá 3 acertos simbolizados;
- e mesmo assim deu **zero** com limite `0x1000` numa corrida onde a minha
  própria sonda leu `vm_read32(0)` e recebeu `0x2F776164`, o que exige traduzir
  o endereço 0.

**Causa do falso negativo: não determinada.** O que está estabelecido é que o
seu zero não constitui prova de ausência.

Consequência, e é o que interessa:

| ledger | o que eu disse | estado real |
|---|---|---|
| [E12](2026-08-05-E12-pool-e-cinco.md) `pool=5 → *(9) = "rma."` | retirado no E28 | **CORRECTO** — reposto |
| [E15](2026-08-05-E15-a-string-no-endereco-4.md) string em memória baixa lida como ponteiro | retirado no E17 | **CORRECTO no essencial** — a string está no 0, não no 4 |
| E17 e E28, os argumentos de retractação | apresentados como refutações | **inválidos** — o instrumento não suporta a conclusão |

O E15 errou o endereço por 4 bytes e errou o autor da escrita (disse "o guest
escreveu através de um ponteiro nulo" — continua por provar). Acertou no
essencial: **há texto de caminho em memória baixa e ele é consumido como
ponteiro.**

## O que aprendi com isto, e é a lição cara da sessão

Passei quatro ledgers a construir sobre uma refutação errada. O que a produziu
não foi falta de cuidado — o E17 e o E28 **verificaram o instrumento** com um
controlo, e o controlo passou.

O que falhou é mais subtil: **o controlo provou que a vigia dispara nalgum
caso, não que dispara no caso em questão.** Um controlo com limite `0x50000000`
exercita o caminho comum; nada garante que o mesmo caminho serve leituras a
`[0, 0x1000)`.

> Regra: **o controlo tem de exercitar o mesmo caso que a medição.** Provar que
> um instrumento funciona em geral não prova que funciona no regime onde se lhe
> vai pedir um zero. Para um zero valer como refutação, é preciso um positivo
> conhecido *dentro do mesmo regime*.

Aqui isso seria trivial: escrever e ler deliberadamente um endereço em
`[0, 0x1000)` e confirmar que a vigia o vê. Não o fiz, e custou quatro ledgers.

## Refutado / reposto

**Reposto:** o E12 e o núcleo do E15.
**Refutado:** os argumentos de retractação do E17 e do E28 (o zero da vigia).
**Mantido de tudo o resto:** as medições directas — `ALLOC878 #237/#238`,
`POP212 #238`, as 11 alocações sãs com passo `0x34`, `pool=0` confirmado por
sonda própria, `off=0xD0` (índice 0) contra `off=0xF8` (índice 10) nas sãs.

## Próximo (E30)

1. **Quem escreve `/wad/r_perma.wad` no endereço 0?** Agora com um instrumento
   em que se possa confiar: uma vigia de escrita validada **no mesmo regime**
   (positivo conhecido em `[0, 0x1000)` antes de aceitar qualquer zero).
2. **Porque é que `*(mgr + 0xD0)` é nulo?** `func_0040FD64` pede sempre o índice
   0; as 225 alocações sãs usam o índice 10. Quem devia criar o pool 0?
3. Corrigir ou substituir `PS3_WATCH_LOWPTR` — está a dar falsos negativos e já
   causou duas retractações erradas.
