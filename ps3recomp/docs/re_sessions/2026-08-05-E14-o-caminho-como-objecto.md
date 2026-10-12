# E14 — o objecto é um caminho: `this = "/r_p"`

**Data:** 2026-08-05 · **Antecessor:** [E13](2026-08-05-E13-o-objecto-errado.md)

Sonda nova (`patch_2182a4_caller_probe.py`, gated `PS3_TRACE_CALLER2182A4`,
OFF por default), 2 corridas de 90 s, receita menu-fast, máquina livre.

---

## O achado

13 entradas em `func_002182A4`. Doze com `this` em heap coerente, espaçadas de
`0x34`. Uma com:

```
[CALL2182A4] this=0x2F725F70 lr=0x00210F8C
```

`0x2F 0x72 0x5F 0x70` = **`"/r_p"`**.

E aqui a peça que fecha a sessão inteira — os **dois** valores patológicos que
perseguimos desde o E8 são janelas de 4 bytes na **mesma string**:

| | valor | offset em `/r_perma.wad` | texto |
|---|---|---|---|
| base das escritas más (E8, `e6wall1`) | `0x726D612E` | **5** | `rma.` |
| `this` mau (E14, produção) | `0x2F725F70` | **0** | `/r_p` |

```
/ r _ p e r m a . w a d
0 1 2 3 4 5 6 7 8
^         ^
|         +-- offset 5: 0x726D612E  "rma."
+-- offset 0: 0x2F725F70  "/r_p"
```

Isto não é coincidência de bytes. **Campos que deviam guardar ponteiros estão a
devolver janelas consecutivas de um caminho de ficheiro.** A doença está no
topo da cadeia, não em `func_00220284`.

## O que a sonda corrigiu em mim

Pus no probe um marcador `<O-MAU>` a testar `this == 0x400C6C68`, à espera que
o objecto do E13 aparecesse aqui. **Nunca disparou** (`O-MAU=0`), embora o caso
mau tenha ocorrido na mesma corrida (`POOLIDX MAU=1`).

Ou seja: o `obj=0x400C6C68` que a sonda `POOLIDX` imprime **não é o `this` de
`func_002182A4`** — é outra coisa nesse ponto do código. Eu tinha assumido que
eram o mesmo e teria escrito isso como facto. A previsão errada, colocada no
código antes de correr, foi o que a apanhou.

> Vale a pena pôr no probe a previsão que se tem, mesmo (sobretudo) quando
> parece óbvia: um marcador que não dispara é informação, e é barato.

## O chamador não é o discriminador

| `lr` | chamadas | `this` |
|---|---|---|
| `0x00210F8C` | 12 | 11 sãs + **a má** |
| `0x0040FD28` | 1 | são |

O chamador raro (`0x0040FD28`) tem `this` são; o mau vem do chamador comum.
**Não há um call site culpado** — o mesmo sítio recebe objectos bons e um mau.
Isso desloca a pergunta: não é "quem chama mal", é **"de onde vem o valor que
lhe é passado"**.

## Determinismo

`POOLIDX MAU = 1` nas duas corridas; 13 e 12 entradas. O caso mau é
reproduzível, um por corrida.

## Cadeia actualizada (seis níveis)

```
??? produz um "objecto" que é o texto /r_perma.wad     <- A RAIZ, ainda por achar
func_002182A4   this = 0x2F725F70 ("/r_p")
                r29 → divisor 130, base 0x4007FCE8      (não é gestor de pools)
                pool = *(base) = 5                      (uma contagem)
func_00263554   head = *(pool+4) = *(9) = 0x726D??2E    (janela do mesmo caminho)
func_002210BC   this inválido
func_00220284   16 escritas UNCOMMITTED
```

## Refutado por medição — não voltar a perseguir

1. **`obj=0x400C6C68` ser o `this` de `func_002182A4`** — o marcador que
   testava essa igualdade nunca disparou, com o caso mau presente.
2. **Haver um call site culpado** — `0x00210F8C` produz 11 sãs e a má.
3. **Os valores maus serem lixo aleatório** — são offsets 0 e 5 da mesma
   string, exactos.
4. Tudo o que E11/E12/E13 já refutaram (escrita estreita, célula mal populada,
   índice fora de alcance, `func_00220284` como sítio do bug).

## Próximo (E15)

1. **De onde vem `0x2F725F70`?** É a raiz. Um objecto cujo campo de ponteiro
   contém o início de um caminho aponta para uma união/struct em que um buffer
   de nome se sobrepõe a ponteiros — ou para um objecto lido do sítio errado
   dentro de um registo de WAD. `/r_perma.wad` é o WAD do E8/paredes B–C, o que
   liga esta parede à cadeia de assets já mapeada.
2. Confirmar se as variantes do E11 (`rm7.`, `rmS.`, `rm%.`) são offsets de
   **outros** caminhos (`/r_perm7…`?) — isso diria se o buffer roda entre WADs.
3. `*(r30+0x460C)` em `func_00242C94` — o caminho declarado para o menu.
