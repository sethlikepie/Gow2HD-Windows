# E30 — síntese: são dois bugs, e só um é a raiz

**Data:** 2026-08-05 · **Antecessor:** [E29](2026-08-05-E29-a-string-esta-no-endereco-zero.md)
**Fontes:** medição própria (sondas `ALLOC878`, `POP212`) + investigação estática
delegada ao Grok (`run-msgo56re-lilrcn`), verificada e corrigida abaixo.

---

## A string, confirmada por duas vias independentes

O Grok encontrou a montagem em runtime: `func_002B446C` formata com
`"/wad/%s%s"` (`*(TOC-0x1420)`) e o sufixo `.wad_ps3`, via sprintf
(`func_00373D00`, `ppu_recomp_001.cpp:237777`). O log confirma:
`[movieio] open '/wad/r_perma.wad_ps3'`.

E os offsets batem com o que eu medi, derivados em separado:

| | valor | offset em `/wad/r_perma.wad_ps3` | medido em |
|---|---|---|---|
| `*(0)` | `0x2F776164` `/wad` | 0 | sonda `POP212` (E29) |
| `*(4)` | `0x2F725F70` `/r_p` | 4 | `ret` envenenado (E14, E25) |
| `*(9)` | `0x726D612E` `rma.` | 9 | `head` (E8, E12) |

**A string `/wad/r_perma.wad_ps3` está no endereço guest 0.** Fecha as duas
famílias de valores da série inteira, com duas derivações independentes a
concordar.

## Correcção ao relatório do Grok

Ele escreveu: *"se `*(gestor + 0x12C) != 0`: `func_0022831C(gestor)` — init
lazy"*. **É ao contrário.** No lift (`func_00212ED0`):

```c
r0 = vm_read32(r31 + 0x12C);
/* compara r0 com 0 */
if (!((cr >> 0) & 2)) goto loc_00212F38;   /* se DIFERENTE, salta */
func_0022831C(ctx);                         /* chamada quando r0 == 0 */
loc_00212F38:
```

A init preguiçosa corre quando o flag diz **"ainda não inicializado"**. Isto
importa porque muda o alvo: a pergunta não é "porque é que a init correu", é
**"porque é que correu e não criou o pool 0"** — ou porque é que o flag já
estava marcado.

O resto do relatório resistiu à verificação.

## O construtor zera o terminador — H-C morre de vez

Três caminhos guest montam a cadeia `next`, e **todos escrevem 0 no último nó**:

| caminho | função | terminador |
|---|---|---|
| construtor do pool | `func_00263680` / `func_0026372C` | `*(last) = 0`, linhas 565333–565336 |
| grow no pop vazio | `func_002635A4` | último `next` = `old_head` (= 0 neste caminho) |
| grow no free | `func_00263400` | `*(last) = 0`, linhas 565076–565078 |

A hipótese H-C (E27, "o builder nunca zera") fica refutada **no código**, não só
por medição.

## Quem cria este pool

`func_0022851C`, o init do gestor (`ppu_recomp_000.cpp:507641-507649`):

```
r5 = 0x34   // tamanho do elemento
r6 = 0      // count inicial 0 -> LAZY
r7 = 8      // grow batch
r8 = 4      // alinhamento
func_002BB1B0(...)
*(manager + 0xD0) = resultado     <- o slot E' atribuido
```

**Se esta init corresse para o gestor `0x400C6B50`, `*(mgr+0xD0)` não seria
nulo.** Medimos nulo. Logo: ou não correu para este gestor, ou correu e o store
não teve efeito.

## São dois bugs, e a ordem importa

**(A) alguém escreve `/wad/r_perma.wad_ps3` no endereço guest 0.**
O E16 mediu **zero** escritas do guest em `[0, 0x1000)` via `vm_write*` — com
controlo a passar. Logo é **código host (HLE)** a fazer `memcpy` para um destino
nulo, num dos caminhos de path (`cellFs*`, FIOS, `movieio`). O Grok localizou o
buffer de origem como local de stack em `func_002B446C`, copiado depois para
estruturas FIOS (`func_002B3890`, `func_0030D578`) — **o destino nulo em si não
está provado**.

**(B) `*(mgr + 0xD0) == 0`** — o pool de índice 0 não existe quando
`func_0040FD64` o pede. Medido por sonda própria: 225 alocações sãs usam índice
10 (`off=0xF8`); a única má usa índice 0 (`off=0xD0`).

**(B) é a raiz.** Sozinho, faz o pop tratar o endereço 0 como pool. **(A) é o
que o torna silencioso**: sem a string lá, `*(4)` seria zero, o pop veria "lista
vazia", chamaria o grow e provavelmente recuperaria. Com a string, devolve texto
e a corrupção propaga-se seis níveis.

## O que resolver, por ordem de retorno

1. **(B), a raiz.** Sonda em `func_00212ED0` no sítio do flag: imprimir
   `*(mgr+0x12C)` e `*(mgr+0xD0)` **antes e depois** do ramo, e se
   `func_0022831C` foi chamada. Três resultados possíveis, todos accionáveis:
   flag já marcado (alguém marcou sem criar), init chamada e slot na mesma nulo
   (a init falha), ou gestor diferente do que foi inicializado.
2. **(A), o agravante.** Vigia de escrita host em `[0, 0x1000)` — mas **validada
   no mesmo regime** (positivo conhecido dentro da gama antes de aceitar
   qualquer zero). A vigia actual dá falsos negativos e já custou duas
   retractações erradas (E29).
3. **Nenhum fix antes disto.** O candidato cirúrgico (podar `head` implausível)
   mascararia (A) e (B) ao mesmo tempo, e o predicado que eu propus no E27 nem
   sequer apanha `0x2F725F70`.

## Sobre a auditoria de fallthrough do CLAUDE.md

Tentei executá-la e **não é executável como está escrita**:

| critério | resultado |
|---|---|
| "trampolim com alvo menor que a função" | **80.171 de 168.379** (48 %) — saltos para trás são *loops* |
| "função que termina em trampolim, alvo ≠ função seguinte" | **18.978 de 21.944** (86 %) — terminar em trampolim é *tail call*, não fallthrough |
| cópias duplicadas com alvos divergentes (foi o que apanhou o `2550C8`) | **0 funções duplicadas neste lift** |

O terceiro é o achado que interessa: o `2550C8` foi encontrado porque a função
existia em dois chunks e **as cópias discordavam**. Neste lift cada EA aparece
uma só vez — os 51.987 símbolos são fragmentos, não cópias. **A verificação que
apanhou aquele bug já não está disponível.**

Para a auditoria voltar a ser possível é preciso um oráculo externo: o
desassemblado do ELF, para saber se a última instrução de cada função é um ramo
(tail call, alvo livre) ou não (fallthrough, alvo obrigatório = próxima
instrução). Fica anotado no lugar da instrução actual, que produz 86 % de ruído.
