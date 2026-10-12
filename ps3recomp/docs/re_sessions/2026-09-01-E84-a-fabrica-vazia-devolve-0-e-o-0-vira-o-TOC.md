# E84 — a fábrica vazia devolve 0, e o 0 vira o TOC

**Data:** 2026-09-01 · **Antecessor:** [E83](2026-09-01-E83-o-escritor-tem-nome-e-a-cadeia-sobe.md)

O E82 nomeou a vítima (`func_00457E44` lê rodata corrompido). O E83 nomeou o escritor
(`func_002649FC` com `r31 = TOC`) e subiu quatro níveis até `func_00264BA8`. Esta sessão
sobe os que faltavam e **fecha a cadeia de ponta a ponta**, com uma corrida por nível.

Nível de evidência: `in-boot` em cada seta marcada ✔; `lido no lift` nas outras.

---

## A cadeia inteira

```
func_00237CDC
  └─ obj = func_00264BA8()                          [a "fabrica de objecto"]
       ├─ r3 = *(TOC-0x2080)->+0x24 = 0x400D6DA0    ✔ pre-virtual: objecto de heap valido
       ├─ vt = *(r3) = 0x00511458  (rodata)         ✔
       │  opd = vt[0x48] = 0x0051B2E8 -> ctr=0x0039E5A8, toc=0x00541178   ✔ tudo legitimo
       ├─ bctrl -> func_0039E5A8(obj)               [a FABRICA de produtos: pop de pilha]
       │     cursor = (int8)*(obj+0x48+0x80)
       │     if (cursor < 0) return 0;              <- VAZIA
       │     return *(obj+0x48 + cursor*4)
       │  ... devolve r3 = 0x00000000               ✔ pos-virtual, sem ICALL-HEAP/ICALL-BAD entre
       ├─ r3 = *(0 + 0x48)                          [le' memoria BAIXA, nao mapeada no ELF]
       ├─ r3 = func_00263554(r3)                    [pop de free-list: devolve *(pool+4) = *(0x4C)]
       └─ devolve 0x00541178                        ✔ arg-func_00237CDC ... CTRL=TOC
  └─ func_00264988(r3 = 0x00541178)
       r31 := r3                                    [correcto -- e' o argumento]
       ... -> func_00264AF8 -> func_002649FC        [back-edge interno; caminho, nao causa: E83 §7]
            vm_write32(r31 + 0x108, -200.0f)        ✔ WATCHSTORE  -> 0x00541280 (era 2pi)
            vm_write32(r31 + 0x10C, 0x40)           ✔ WATCHSTORE  -> 0x00541284 (era 0)
  ... mais tarde, no tick do frame 1 ...
func_00457E44  (wrap de angulo)                     ✔ FPRWRAP: min=0x40, per=-200, p1=0
  do { p1 += per; } while (p1 < min);               nao converge: per e' negativo
```

**Sete níveis. Cada um é vítima do de cima, até `func_0039E5A8`.** Aplicando *«se o input
estivesse certo, o output estaria certo?»*: em todos os níveis abaixo da fábrica a resposta
é sim. Na fábrica a resposta é **também sim** — se a pilha tivesse produto, devolvia-o. O
nível que **decide** é quem devia ter empurrado um produto e não empurrou.

## O que esta sessão mediu (in-boot)

| ponto | corrida | valor |
|---|---|---|
| `pre-virtual` | r31src v1, v2 | `r3=r11=0x400D6DA0`; v2 acrescenta `vtable=0x00511458 opd=0x0051B2E8 ctr=0x0039E5A8 toc=0x00541178` |
| `pos-virtual` | v1, v2 | `r3=0x00000000` — **nenhuma** linha entre pre e pos: não foi `ICALL-HEAP`, não foi `ICALL-BAD`, não foi disjuntor nosso |
| `arg-func_00237CDC` | v1, v2 | `r3=0x00541178 CTRL=TOC` |
| watch `0x541280/84` | E83 ×2, r31src v1, v2 | as mesmas duas escritas, sempre de `func_002649FC` |

Determinismo: 4 corridas com watch, 4 iguais; 2 corridas com R31SRC, 2 iguais.

## O que NÃO ficou medido (e fica dito)

- **Como exactamente `*(0x4C)` vale o que faz a cadeia chegar a `0x00541178`.** Lido no
  lift: `func_00263554` é o pop de free-list (a sonda `FLHEAD-PROBE` já existente diz-o)
  e devolve `*(pool+4)` com `pool = 0x48`. A corrida com `PS3_WATCH_STORE=0x48,0x4C` deu
  **zero escritas — mas também zero linhas `R31SRC`**: não chegou ao caminho. **Zero
  inconclusivo, não refutação.** O valor pode vir de uma escrita host directa (fora do
  `vm_write*`) ou ser o que a página 0 tem desde o load; a sonda que decide é uma leitura
  directa de `0x40..0x50` no `pos-virtual`, não um watch.
- **Por que a fábrica está vazia.** Isso é a pergunta que os ledgers E38–E77 já
  perseguem («o que garante, no console, que a fábrica tem produto quando o walker
  pergunta?»). Esta sessão **não** a responde; **liga-a ao sintoma**: até hoje sabia-se
  que a fábrica devolvia 0 (`EMPTYFAB`), e sabia-se separadamente que o tick travava num
  wrap de ângulo (E78). Agora é **uma** parede, não duas.

## O que muda no mapa

1. **A parede [F] tem uma causa, não um sintoma.** «O wrap de ângulo não termina» (E78)
   e «a fábrica devolve 0» (E65/E75) eram dois itens. São o mesmo defeito, ligado por
   sete níveis medidos.
2. **O E78 fica corrigido em duas metades** (já registado no E82): o argumento é são e as
   constantes no ELF são sãs — o que está errado é a memória em runtime.
3. **Um objecto inteiro (~0x120 bytes) é escrito por cima do rodata a partir de
   `0x00541178`.** `0x541280/84` são só os dois campos que o wrap lê. Há outros campos
   nessa gama — e outros consumidores desse rodata podem estar a ler lixo sem ainda
   travar. É um sintoma a esperar, não a procurar.
4. **`0 + 0x48` como pool é um `NULL->campo` do jogo sem guarda**, exactamente a classe
   que a skill *walking-a-corruption-upstream* descreve como o caso real deste projecto
   («an allocator popped a free-list from guest address 0»). No console isso seria um
   acesso a página não mapeada; aqui a página 0 existe e devolve dados.

## Candidatos a fix, classificados — e nenhum é este ledger

| tipo | onde | o que faz | veredicto |
|---|---|---|---|
| **paliativo** | `func_00264BA8` pós-virtual | `if (r3 == 0) return 0;` | **proibido como fix**; mascara a fábrica vazia e o sintoma reaparece noutro consumidor |
| **paliativo** | `vm_read32` | falhar em EA < 0x1000 | igual: esconde o nível acima; útil só como **sonda** para contar quantos sítios lêem a página 0 |
| **raiz** | quem empurra produtos para a fábrica de `0x400D6DA0` | fazer o push acontecer no momento certo | é a pergunta do E75/E77; **esta sessão não a responde** |

Nenhum fix foi feito. Nomear o mecanismo a sete níveis não é ter o fix, e fica dito.

## Refutado nesta sessão

1. **«a chamada virtual não resolve e um disjuntor nosso zera o r3»** (H1 do E83) —
   resolve para `0x0039E5A8` com vtable e OPD legítimos; nenhuma linha de disjuntor entre
   pre e pos; o zero é o **retorno real** do método.
2. **«o TOCFIX de `func_00264BA8` está envolvido»** (H-A do E83) — escreve em `r2`, com o
   valor certo, e o lift é fiel ao `ld r2,0x28(r1)` original (disassembly do EBOOT).
3. **«`*(0x48)` vem de uma escrita `vm_write*`»** — **NÃO refutado**: a corrida não chegou
   ao caminho (zero `R31SRC`). Fica em aberto.
