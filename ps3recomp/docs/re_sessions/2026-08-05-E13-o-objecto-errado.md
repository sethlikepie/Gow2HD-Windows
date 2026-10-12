# E13 — o descritor errado: `obj=0x400C6C68`, `divisor=130`

**Data:** 2026-08-05 · **Antecessor:** [E12](2026-08-05-E12-pool-e-cinco.md)
**Predições:** [E13-predições](2026-08-05-E13-predicoes.md), commit `58df673`, escritas antes da corrida.

**P-2 e P-3 confirmadas, P-1 refutada, controlo passa.** A causa sobe mais um
nível e o padrão é o de sempre: **um método a correr sobre o objecto errado.**

---

## A comparação

`PS3_TRACE_POOLIDX=all`, receita menu-fast, 2 corridas de 90 s, máquina livre.
**Controlo:** 12 linhas com pool plausível (o modo `all` pegou; sem isto a
corrida não decidia nada).

| | `obj` | `divisor` = `*(r29+0)` | `base` = `*(r29+0x14)` | nº |
|---|---|---|---|---|
| **sãs** | `0x406388F8` | **128** | `0x40773710` (run1) / `0x407736F0` (run2) | 12 |
| **má** | `0x400C6C68` | **130** | `0x4007FCE8` | 1 |

Três coisas nesta tabela, e cada uma sozinha já chegava:

1. **O descritor é outro.** As 12 sãs partilham `obj`, `divisor` e `base`. A má
   difere nos três. Não é uma célula mal populada numa tabela boa (P-1) — é
   **outra tabela**, de outro objecto.
2. **`128` contra `130`.** O divisor são é potência de dois, como se espera num
   alocador. `130` não é, e aparece **só** no caso mau.
3. **O `base` são varia entre corridas; o mau não.** `0x40773710` → `0x407736F0`
   é heap a mexer-se normalmente. `0x4007FCE8` é **byte a byte o mesmo nas duas
   corridas** — o que aponta para um objecto estático ou alocado muito cedo, não
   para um vizinho de heap qualquer.

`0x400C6C68` cai na vizinhança `0x400Cxxxx` da fábrica (`this=0x400C5048`) e do
"objecto de Julho" (`this=0x400C61C8`) que a docstring da própria sonda já
tinha mapeado a 2026-08-01.

## A aritmética, com os números medidos

```
r9 = r30 * 16          = 1 * 16   = 16
r9 = r28 * r9          = 4 * 16   = 64
r9 = r9 / divisor      = 64 / 130 = 0      <- trunca
r9 = r9 * 4                       = 0
slot = base + r9       = 0x4007FCE8
pool = *(slot)                    = 5      <- uma contagem
```

O índice está **dentro** do alcance; a divisão é aritmética honesta. O erro está
todo em `r29` apontar para algo que não é um gestor de pools.

## A cadeia completa, cinco níveis

```
func_002182A4 chamada com obj = 0x400C6C68   <- OBJECTO ERRADO (a raiz)
  r29 → divisor 130, base 0x4007FCE8          (não é um descritor de pools)
  pool = *(base) = 5                          (uma contagem, não um ponteiro)
func_00263554                                 
  head = *(pool+4) = *(9) = 0x726D??2E        (memória baixa)
func_002210BC                                 
  this fica inválido
func_00220284                                 
  16 escritas UNCOMMITTED, stride 0x10
```

Cada nível parecia a parede quando visto de baixo. `func_00220284` — onde tudo
começou no E8 — é a vítima final, quatro saltos a jusante da causa.

## Predições: veredicto

| | resultado |
|---|---|
| **P-1** mesma `base`/`divisor` nas sãs → célula mal populada | **refutada** |
| **P-2** outro `divisor`/`base` → `r29` errado | **CONFIRMADA** |
| **P-3** `divisor=130` só no caso mau | **CONFIRMADA** |
| controlo: ≥1 linha com pool plausível | **passa** (12) |

## Contagens — e o que cada uma significa

Duas contagens diferentes, que não se devem misturar:

- **1 em 8625** — pops da free-list (`FLHEAD`) na corrida inteira. Mede quão
  raro é o sintoma no sistema todo.
- **1 em 13** — chamadas ao sítio instrumentado (`POOLIDX`, só `0x218340`).
  Mede quão raro é **neste sítio**: uma em treze chamadas usa o objecto errado.

A segunda é a que interessa para o fix. Uma em treze não é ruído.

## Classificação dos candidatos a fix

| tipo | o que muda | nota |
|---|---|---|
| **Raiz** | porque é que `func_002182A4` é chamada com `obj=0x400C6C68` | é aqui que o trabalho deve ir |
| **Cirúrgico** | rejeitar no sítio um `r29` cujo `divisor` não é potência de dois | 1 ponto de decisão; `128` vs `130` dá o predicado |
| **Paliativo** | `patch_218364_pool_null_gate.py` | já existe, gated — **não é fix** |

O predicado cirúrgico tem conjunto negativo medido: **12 chamadas com
`divisor=128` que não podem ser podadas**, e 1 com `130` que deve. Isso é o
mínimo para escrever o teste antes do fix — mas 12 é uma amostra pequena, e
antes de a usar como gate é preciso confirmar que `128` é o único divisor
legítimo em corridas mais longas.

## Refutado por medição — não voltar a perseguir

1. **A tabela de pools estar corrompida** (P-1) — as 12 sãs lêem uma tabela
   diferente e correcta.
2. **O índice estar fora de alcance** — `idx=0`, e `64/130=0` é aritmética
   honesta.
3. **`func_00220284` ser o sítio do bug** — é a vítima, 4 níveis a jusante.
4. **H3 do E11** (escrita estreita) — já refutada no E12.

## Próximo (E14)

1. **Quem chama `func_002182A4` com `obj=0x400C6C68`?** É a pergunta raiz, e
   agora é uma pergunta de despacho — o mesmo padrão já medido noutros três
   sítios do projecto. A cadeia da docstring aponta para
   `func_0039D51C` → `func_0039E40C` → `vt[0x60]`.
2. Confirmar que `divisor=128` é o único legítimo, em corridas longas, antes de
   o usar como predicado.
3. Fechar a tensão do `dst60=0` herdada do E11 — continua em aberto.
