# Phase 11: A parede real — Context

**Gathered:** 2026-08-04
**Status:** Ready for planning
**Mode:** Autónomo — o critério 1 desta fase foi RESPONDIDO hoje; o contexto regista a prova

<domain>
## Phase Boundary

Fazer o boot entrar no loop principal (`func_00242C94`) e despachar o primeiro estado
**sem gates de diagnóstico**.

**Fora:** promoção de lift, gates de patches (Fase 4, feita), pixels/shaders.
</domain>

<measured_state>
## O critério 1 está RESPONDIDO — e a resposta é a hipótese A

O ROADMAP põe duas hipóteses exclusivas: *ou o tag do objecto está errado, ou `tab[1]`
despacha para a fábrica errada.* A sessão de RE de hoje
([`docs/re_sessions/2026-08-04-E2-quem-forca-low16.md`](../../../docs/re_sessions/2026-08-04-E2-quem-forca-low16.md))
nomeia o sítio, com prova estática sobre as 13 143 funções do export Ghidra:

### A linha, em DOIS caminhos do desserializador `0x00254C40`

```c
uVar16 = *param_3 >> 0x10 & 0xfff;        // subtag = (w0 >> 16) & 0xFFF
if (uVar16 == 0x2a) { … return 0; }       // subtag 0x2A tem caminho próprio
…
*puVar10 = uVar16 & 0xfff0000 | 1;        // linha 92   <-- força low16 := 1
…
*puVar10 = uVar16 & 0xfff0000 | 1;        // linha 136  <-- segundo caminho
```

Preserva o `subtag` e **força `low16 := 1`**. Para um registo WAD (`subtag = 0x003`) o
resultado é `w0 = 0x40030001` — exactamente o valor que a sonda E1 mediu in-boot.

### O walk exterior filtra por FLAGS e despacha por LOW16 — nunca vê o subtag

`0x0041FF70`:

```c
if ((flags & 0x10) == 0)
  if ((flags & 3) == 0)
    … vt[0x40] de puVar5[ *(ushort *)(obj + 6) * 4 ]     // índice = low16
```

Os dois filtros são sobre flags. **Nenhum olha para o tipo.** Um registo WAD passa ambos.

### Prova negativa que fecha a outra hipótese

A constante `0x40030001` foi procurada nas **13 096** funções decompiladas em três formas:

| forma | ocorrências |
|---|---|
| `0x40030001` | **0** |
| `1073938433` (decimal) | **0** |
| `0x4003` (par `lis`/`ori`) | **0** |

Não há sítio no código que a escreva. O valor é **construído** pela linha acima. Morre a
hipótese «alguém registra o WAD deliberadamente no conjunto do walk».

### O mecanismo completo, das duas pontas

1. Registo WAD entra no stream com `subtag = 0x003`.
2. `0x00254C40` escreve `w0 = 0x40030001` — subtag preservado, `low16` forçado a 1.
3. `0x0041FF70` enumera-o (passa os dois filtros de flags) e despacha por `low16 = 1`.
4. O push `0x0041F700` lê `h + 0x80` (= `q + 0x7C`) como cabeça da lista de filhos.
5. Num registo WAD, **`+0x7C` é `matriz[0][3]`** — provado desmontando `func_0024C1F8`.
   Numa matriz de transformação isso é tipicamente `0.0f`.
6. `sent_head = 0x00000000` — **medido in-boot pela sonda E1**. O laço
   `for (no = *(h+0x80); no != h+0x80; no = *no)` arranca em 0, lê `*0 = 0`, nunca atinge
   a sentinela: **179 459 343 iterações**, `no_rep = n − 1`.

⇒ **O walk lê um float de uma matriz como ponteiro de lista.** A predição estática e a
medição in-boot batem.

### Já eliminado por medição (não voltar a perseguir)

- Registry de tipos populado: 26 entradas, tags 0..32, fábrica válida, vtable viva.
- `tab[low16] != tab[subtag]`: **`diff_mgr = 0`** em 6 corridas, ~885 M de visitas.
- Recursão exponencial, falta de pop, assimetria push/pop, `hflags & 4` como poda.
- `0x47D00000` como referência válida: era o paliativo TYPE15, removido a 2026-08-01.
</measured_state>

<decisions>
## Decisões

### D-11.1 — Os três candidatos de fix, com a natureza de cada
| # | Onde | O que muda | Risco |
|---|---|---|---|
| **A** | `0x00254C40` — não forçar `low16 := 1` quando o subtag não é de grafo | classificação (causa raiz) | mexe em **todos** os registos com `subtag != 1` |
| **B** | `0x0041FF70` — filtrar também por subtag | enumeração | cirúrgico, reversível |
| **C** | `0x0041F700` — validar a lista antes de a percorrer | consumo | **paliativo — proibido como fix** (CLAUDE.md regra 5) |

### D-11.2 — A sonda de contagem decide entre A e B, e corre PRIMEIRO
Contar quantos objectos passam os dois filtros de flags **e** têm `subtag != 1`.
- Se **exactamente 1** (o registo WAD) ⇒ **B** é o fix: um filtro de subtag poda só o caso
  patológico e não toca em mais nada.
- Se **mais que 1** ⇒ há mais registos mal classificados e a pergunta volta para **A**.

Uma corrida decide. Sem esta contagem, escolher entre A e B é palpite.

### D-11.3 — C fica OFF e serve só para comparação
Como os três gates de diagnóstico existentes. Um gate nunca fecha uma parede (critério 2).

### D-11.4 — Testes antes do fix
O utilizador pediu testes. O que é testável **offline**, sem correr o boot:
- que o desassemblador lê `0x00254C40` e encontra as duas escritas de `low16`;
- que o `+0x7C` de um registo WAD é `matriz[0][3]` (fixture do layout);
- que um filtro por subtag poda o `w0 = 0x40030001` e **não** poda os 12 tags legítimos
  medidos por E1 (`3,4,9,15,16,17,18,20,21,23,25,32`).
O último é o teste que dá dentes ao fix B **antes** de o escrever.

### D-11.5 — Se não fechar, entrega o mapa
Critério 4 do ROADMAP. Nunca um fecho forjado.
</decisions>

<code_context>
| Âncora | Valor |
|---|---|
| desserializador (força low16) | `0x00254C40` — `vt+0xA0` da classe `0x00515B08` |
| walk exterior (enumera) | `0x0041FF70` |
| push (percorre a lista) | `0x0041F700` — `vt+0x40` do gestor `0x00515AA0` |
| pop | `0x0041F924` — `vt+0x44` |
| âncora da sonda E1 | `0x0041F78C` (`rlwinm r9,r0,2,14,29`) |
| tabela de tipos | `0x00868D48` (`.bss`) |
| layout WAD (`+0x7C = matriz[0][3]`) | provado em `func_0024C1F8` |
| registo patológico | `w0 = 0x40030001`, `h = 0x4077AD5C` |

## Ferramenta — um passo humano pendente

O Ghidra MCP serve o projecto **errado**: `gow2` (processador `A2ALT` auto-detectado,
**1488** funções, `.opd` **não** processada) em vez de `gow2_full`
(`PowerPC:BE:64:64-32addr`, **13 143** funções, 13 006 `.opd`). Medido:
`get_function_by_address 0x0041F700` → *No function found*; busca por `.opd` → zero.
`~/ghidra_projects/gow2/gow2.lock` existe; o do `gow2_full` não.
**Na GUI: fechar `gow2`, abrir `~/ghidra_projects/gow2_full/gow2_full.gpr`.**
Enquanto isso, usar o export estático `../gow2-recomp/ghidra_out/` (tem as 13 143).

## Armadilhas
- Probes gated por env var, **OFF por default**.
- Matar o boot por PID (TERM → 9). Nunca `pkill -f boot_gow2`.
- `-DCMAKE_BUILD_TYPE=Release` desactiva `assert()`. Usar `-DCMAKE_BUILD_TYPE=` vazio.
- Fix no lift vira `patch_*.py` idempotente commitado.
- Nunca `re.S` num regex que apaga; delimitar o fim, nunca inferi-lo.
</code_context>

<specifics>
## Critérios, com o estado

| # | Critério | Estado |
|---|---|---|
| 1 | Nomeado com prova qual lado está errado | **RESPONDIDO** — hipótese A, a linha localizada |
| 2 | Correcção fiel ao CELL, não um gate | por fazer (D-11.2 decide A ou B) |
| 3 | `AUTO_LOAD` ≥1 vez sem gates, ≥4 de 6 corridas | por medir |
| 4 | Se não fechar, entrega o mapa | D-11.5 |
</specifics>
