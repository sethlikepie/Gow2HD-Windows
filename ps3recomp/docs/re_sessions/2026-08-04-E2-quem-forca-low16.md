# Sessão de RE — **E2**: a linha que força `low16 := 1`, e os três sítios onde o fix pode ir

Continuação de [`2026-08-03-E1-typewalk-trace.md`](2026-08-03-E1-typewalk-trace.md), que mediu
in-boot que **99,96 % do rasto é um `for` que não termina** e deixou duas hipóteses exclusivas
como próximo experimento. Esta sessão fecha-as **estaticamente**, sem correr o boot.

> **Nota de ferramenta:** o Ghidra MCP estava a servir o projecto **errado** — `gow2`
> (processador `A2ALT` auto-detectado, **1488** funções, `.opd` **não** processada) em vez de
> `gow2_full` (`PowerPC:BE:64:64-32addr`, **13 143** funções, 13 006 descritores `.opd`).
> Medido: `get_function_by_address 0x0041F700` → *No function found*; busca por `.opd` → zero.
> O `~/ghidra_projects/gow2/gow2.lock` existe, o do `gow2_full` não.
> **Toda esta sessão foi feita com o export estático `../gow2-recomp/ghidra_out/`**, que tem as
> 13 143. Abrir o `gow2_full` na GUI é um passo humano pendente.

---

## O que ficou provado

### 1. O `w0 = 0x40030001` NÃO é uma constante do programa

Procurado nas **13 096** funções decompiladas, em três formas:

| forma | ocorrências |
|---|---|
| `0x40030001` | **0** |
| `1073938433` (decimal) | **0** |
| `0x4003` (par `lis`/`ori`) | **0** |

**Consequência:** morre a hipótese «alguém registra deliberadamente o WAD no conjunto do
walk». Não há sítio no código que escreva esse valor. Ele é **construído**.

### 2. A linha que o constrói — e está em DOIS caminhos

No desserializador `0x00254C40` (`.opd.FUN_00254c40`, 237 linhas):

```c
uVar16 = *param_3 >> 0x10 & 0xfff;        // subtag = (w0 >> 16) & 0xFFF
if (uVar16 == 0x2a) { … return 0; }       // subtag 0x2A tem caminho próprio
if (uVar16 != 1) {
    …                                      // byte-swap de um cabeçalho de ~0x68 bytes
}
…
*puVar10 = uVar16 & 0xfff0000 | 1;        // <-- linha 92
…
*puVar10 = uVar16 & 0xfff0000 | 1;        // <-- linha 136 (segundo caminho)
```

Preserva o `subtag` (`& 0xfff0000`) e **força `low16 := 1`**. Para um registo WAD
(`subtag = 0x003`) o resultado é exactamente `0x40030001`, o valor que a sonda E1 mediu.

### 3. O walk exterior filtra por FLAGS e despacha por `low16` — nunca vê o subtag

`0x0041FF70` (`.opd.FUN_0041ff70`, 70 linhas):

```c
if ((*(ushort *)(puVar4 + -1) & 0x10) == 0) {          // filtro 1: bit 0x10
  if ((*(ushort *)(puVar4 + -1) & 3) == 0) {           // filtro 2: bits 0..1
    …
    (*(code *)**(undefined4 **)
        (**(int **)(puVar5 + (uint)*(ushort *)(iVar2 + 6) * 4) + 0x40))   // push = vt+0x40
              (…, iVar2 + 4);
```

O índice do gestor é `*(ushort *)(obj + 6) * 4` — **o `low16`**. Os dois filtros são sobre
flags; **nenhum** olha para o tipo. Um registo WAD passa os dois.

---

## O mecanismo completo, das duas pontas

1. Registo WAD entra no stream com `subtag = 0x003`.
2. `0x00254C40` escreve `w0 = 0x40030001` — subtag preservado, `low16` forçado a 1 (§2).
3. `0x0041FF70` enumera-o (passa os dois filtros de flags) e despacha por `low16 = 1` → o
   gestor `tab[1]` (§3).
4. O push `0x0041F700` lê `h + 0x80` (= `q + 0x7C`) como cabeça da lista intrusiva de filhos.
5. Mas num registo WAD, **`+0x7C` é `matriz[0][3]`** — provado estaticamente na parede 1 ao
   desmontar `func_0024C1F8`. Numa matriz de transformação isso é tipicamente `0.0f`.
6. `sent_head = 0x00000000` — **é o que a sonda E1 mediu.** O laço
   `for (no = *(h+0x80); no != h+0x80; no = *no)` arranca em `0`, lê `*0 = 0`, e nunca atinge
   a sentinela. `no_rep = n − 1`, **179 459 343 iterações**.

⇒ **O walk lê um float de uma matriz como ponteiro de lista.**

---

## Os três sítios onde o fix pode ir

| # | Onde | O que muda | Natureza |
|---|---|---|---|
| **A** | `0x00254C40` — não forçar `low16 := 1` quando `subtag` não é um tipo de grafo | corrige a **classificação** | causa raiz |
| **B** | `0x0041FF70` — filtrar também por `subtag`, não só por flags | corrige a **enumeração** | menos invasivo |
| **C** | `0x0041F700` — validar que o objecto tem lista antes de a percorrer | corrige o **consumo** | paliativo defensivo |

**A** é a raiz mas mexe na desserialização de *todos* os registos com `subtag != 1` — risco
alto de regressão fora da parede. **B** é cirúrgico e reversível. **C** é um null-check
disfarçado: pára a hemorragia mas deixa o registo classificado como o tipo errado, e o
CLAUDE.md proíbe null-checks cosméticos como fix.

**Recomendação:** medir **B** primeiro, com uma sonda que conte quantos registos passariam a
ser podados por um filtro de subtag. Se o número for exactamente 1 (o registo WAD), B é o fix.
Se for maior, há mais registos mal classificados e a pergunta volta para A.

---

## O que esta sessão NÃO afirma

- **Não** foi medido in-boot. Tudo aqui é estático, sobre o export de 13 143 funções.
- **Não** foi provado que `tab[1]` é o gestor errado para `subtag=3` — só que é o que o
  `low16` selecciona. Saber qual seria o correcto exige ler a tabela `0x00868D48`, que é
  `.bss` preenchida em runtime.
- **Não** foi verificado se o `subtag = 0x2A` (o caminho especial da linha ~50) tem relação
  com isto.

## Próximo experimento

1. **Sonda de contagem para a hipótese B:** no `0x0041FF70`, contar quantos objectos passam os
   dois filtros de flags **e** têm `subtag != 1`. Uma corrida decide entre B e A.
2. **Ler `tab[1]` e `tab[3]` in-boot** com o `gow2_full` aberto — responde a «qual seria o
   gestor correcto».
3. **Não voltar a perseguir:** «alguém registra o WAD deliberadamente» (§1 mata-o com 0/13096
   ocorrências da constante).
