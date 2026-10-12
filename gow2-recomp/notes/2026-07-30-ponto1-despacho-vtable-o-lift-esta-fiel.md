# Ponto 1: o despacho por vtable da parede TYPE15 — o lift está fiel ao ELF

**Data:** 2026-07-30 · **Método:** análise estática do EBOOT.ELF + leitura do
`recomp_macos_v3`. **Nada corrido.** Scripts em `$CLAUDE_JOB_DIR/tmp/` (descartáveis;
o que interessa está aqui).

## A hipótese que se foi testar

Depois do marco v1.0 (que descobriu que o TOCFIX matou 36 conversões OPD), levantou-se
esta:

> `1D7FCC` e `1A9D24` só são chamáveis por despacho virtual. Em PPC64 ELFv1 um ponteiro
> de função de vtable é um **descritor OPD** (code + TOC), não um endereço de código. Um
> site emitido como `ps3_indirect_call` genérico em vez de `ps3_call_opd` conta *igual*
> no `lift_parity` e salta para o sítio errado em runtime. Se as classes instaladoras
> nunca são construídas, talvez seja o nosso lift a partir o despacho.

**Resultado: REFUTADA em todos os elos.** O lift está fiel ao ELF em cada ponto medido.

## O que se mediu, elo a elo

### 1. As vtables são reais (confirma a nota de 07-25, com uma precisão)

`0x00513F48` e `0x00513528` são vtables Itanium com RTTI removido, em segmento de
DADOS (`0x00510000-0x00576EA4`). Todos os slots contêm OPDs válidos, todos com
`toc=0x00541178`.

`1D7FCC` e `1A9D24` estão no **primeiro slot** de cada (`+0`). A nota de 07-25 diz
"slot 2 (+8)" — é o mesmo endereço, contado a partir do `offset-to-top` (`vt-8`).
Não há divergência material, só de nomenclatura.

### 2. Os 12 construtores, confirmados independentemente

Varrendo o `.text` por quem **materializa** as duas vtables (via `lwz rX,off(r2)`,
`TOC-0x3520` e `TOC-0x3444`), saem 12 sítios:

```
196014  196FF0  19DCF4  19FBE0  1A3AB0  1A3B68
1AD870  1ADA28  1ADBD4  1CAE1C  1CAE90  1DAFB4
```

São **exactamente** os 12 que a nota de 07-25 mediu in-boot com `tot=0` — a nota
transcreveu-os com `C` no lugar do `1` inicial (`C96014` = `0x00196014`). Confirmação
independente, por outro método, do mesmo conjunto.

### 3. Os chamadores de `CD7B4`/`CD9DC` existem — e vivem em código não declarado

A nota de 07-25 pergunta: *"porque está a cadeia `CD9DC → CD7B4 → CD498` inerte —
**sem chamador estático**?"*. Tem chamador estático. Três:

| sítio | chama | dentro de função declarada? |
|---|---|---|
| `0x0002AC14` | `bl 0xCD9DC` | **NÃO** — gap `0x00029E64..0x0002BA64` (1792 instruções) |
| `0x0002AE68` | `bl 0xCD7B4` | **NÃO** — mesmo gap |
| `0x00032690` | `bl 0xCD7B4` | **NÃO** — gap `0x000323E4..0x00032774` (228 instruções) |

O Ghidra não os viu porque a varredura de 07-25 correu sobre os corpos decompilados,
e estes blocos não pertencem a nenhum.

São blocos **out-of-line**: o `bl` é seguido de `nop` (restauro de TOC) e depois
`b 0x29E28`, que salta de volta para **dentro** da função declarada anterior
(`func_00029AF0`, `0x00029AF0-0x00029E64`). O compilador moveu os caminhos frios
para longe do corpo.

### 4. Mas o lifter recuperou-os — e as chamadas estão lá

Este era o ponto onde a hipótese morria ou vivia. O `func_00029AF0` emitido tem
**2114 linhas** e cobre até `loc_0002BA58` — muito além do fim declarado
`0x00029E64`. O boundary recovery apanhou o gap inteiro.

E as duas chamadas estão emitidas, com o `lr` certo:

```c
ctx->lr = 0x0002AC18; func_000CD9DC(ctx); DRAIN_TRAMPOLINE(ctx);   // linha 1154
ctx->lr = 0x0002AE6C; func_000CD7B4(ctx); DRAIN_TRAMPOLINE(ctx);   // linha 1309
```

**Nada se perdeu.** Não é o caso `func_002550C8`.

### 5. A jump table foi recuperada exactamente

Os dois blocos são **casos de um switch** de `func_00029AF0`:

```c
ctx->gpr[11] = vm_read32(ctx->gpr[2] + -0x7C04);      // base = 0x00029B5C
ctx->gpr[9]  = ppc_rlwinm(ctx->gpr[4], 2, 14, 29);    // índice*4
ctx->gpr[0]  = sign32(vm_read32(gpr[9] + gpr[11]));   // offset
ctx->ctr     = gpr[0] + gpr[11];                      // alvo
switch (ctx->ctr) { /* 74 casos */ default: ps3_indirect_tail(ctx); }
```

| | |
|---|---|
| entradas na tabela real | **108** |
| alvos distintos | **74** |
| casos emitidos pelo lift | **74** |
| alvos sem `case` | **0** |
| `case` sem alvo | **0** |

`0x0002ABA4` (→`CD9DC`) é o índice **52**. `0x0002AE08` (→`CD7B4`) são os índices
**27, 55, 84**. Ambos têm `case`.

### 6. `func_00029AF0` não tem chamador directo — nos dois lados

Zero `bl` para `0x00029AF0` no ELF inteiro. Zero `func_00029AF0(ctx)` no lift inteiro.
**Consistente** — não é perda.

Ela tem OPD em `0x0051A2F0`, que aparece uma única vez nos dados: `0x00510FDC`, o
**slot 3** de mais uma vtable (`0x00510FD0`, header `0x42` + offset-to-top/typeinfo a
zero). Também ela só é alcançável por despacho virtual.

## O que isto elimina, e o que sobra

**Eliminado:** OPD mal convertido, chamada perdida por bounds truncados, jump table
truncada, despacho virtual partido pelo lift. Em cada elo verificado o lift reproduz
o ELF.

**Sobra:** o índice do despacho nunca toma os valores que levam aos blocos. Não é
"código morto no build" — o código está lá, alcançável, correctamente traduzido. É o
*valor* que nunca aparece.

## A pista mais forte, e é sobre nós

O bloco que o lifter gera naturalmente para o `icall2` do `CB56C`
(`patch_type15_cc9d0_base_blocks.py:127`, `CB_CLEAN`) usa **o mesmo padrão de índice**:

```c
ctx->gpr[0] = vm_read16(ctx->gpr[29] + 0x2);          // campo de TIPO no produto
ctx->gpr[0] = ppc_rlwinm(ctx->gpr[0], 2, 14, 29);     // <<< idêntico ao de 29AF0
ctx->gpr[11] = vm_read32(ctx->gpr[27] + ctx->gpr[0]); // tabela[tipo]
ctx->gpr[9]  = vm_read32(ctx->gpr[11] + 0x0);         // vtable
ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x18);         // slot 6
ps3_indirect_call(ctx);
```

É o mesmo sistema de despacho por tipo, em cascata: **o tipo do produto escolhe a
vtable, a vtable escolhe o método**. Se o produto que o `CB56C` instala for um shell
com o campo `+0x2` errado (ou vazio), todo o despacho a jusante — incluindo o que
construiria as classes de `1D7FCC`/`1A9D24` — indexa para o sítio errado ou para nada.

E o `skip_icall2` é **paliativo nosso**. Salta exactamente este despacho.

> Se o produto está incompleto, os construtores não correm; se os construtores não
> correm, ninguém instala o estado de arranque; se ninguém instala o estado, o `f4`
> fica em 0 e o `CC9D0` gira 7,8 milhões de vezes. A cadeia fecha — e a ponta de que
> se puxa é a **qualidade do produto que o `CB56C` instala**, não o lift.

## Próxima medição (in-boot, barata)

1. Registar a distribuição de `ctx->gpr[4]` à entrada do switch de `func_00029AF0` —
   que índices chegam mesmo? Nunca 27/52/55/84, ou a função nem é chamada?
2. Registar `vm_read16(produto + 0x2)` no `CB56C` — o campo de tipo do produto
   instalado é plausível, ou é 0 / lixo do shell?

O (2) é o que decide se a parede é nossa. Ambos gated, OFF por default.

## Ressalvas

- Tudo estático. Nada aqui foi observado em execução.
- A varredura de materialização cobre `lwz off(r2)` e `lis`+`addi`/`ori` até 64 bytes
  de distância. Uma vtable montada em runtime, ou materializada por outra sequência,
  não aparece.
- O `lift_parity` não acusou nada nestas funções, e é coerente: ele compara *contagens*
  de chamada por cadeia, e aqui as contagens batem. Continua cego a um alvo errado com
  a contagem certa — só que, neste caso, o alvo também está certo.
