# E83 — o escritor tem nome, e a cadeia sobe até um patch nosso

> **§5 PARCIALMENTE RETRATADO no mesmo dia, por medição própria.** Escrevi que
> `func_00264AF8 → func_002649FC` era «um caso medido a produzir a parede» da classe
> «trampolim em cauda com alvo para trás». **O trampolim está no caminho, não é a
> causa.** Ver §7 no fim deste documento. Tudo o resto (§1–§4, §6) **sobrevive**: são
> medições, não inferências.

**Data:** 2026-09-01 · **Antecessor:** [E82](2026-09-01-E82-o-que-chega-ao-wrap-de-angulo.md)

O E82 mediu que `func_00457E44` é **vítima**: lê `período = −200` e `mínimo = 64` de
`0x00541280`/`0x00541284`, onde o ELF tem 2π e 0. Falta o **escritor**. Esta sessão
tem-no, e mais quatro níveis acima dele.

Nível de evidência: `in-boot` para o escritor (2/2 corridas idênticas), **estático** para
a cadeia de chamadas acima dele.

---

## 1. O escritor, medido

`PS3_WATCH_STORE=0x00541280,0x00541284` (watch de qualquer largura, no `heap_w()`),
`PS3_WATCH_STORE_CAP=-1`. Duas corridas, **idênticas**:

```
[WATCHSTORE] w32 [0x00541280]=0xC3480000 ra0=func_002649FC+0x660 ra1=func_00237CDC+0x39C
[WATCHSTORE] w32 [0x00541284]=0x40       ra0=func_002649FC+0x680 ra1=func_00237CDC+0x39C
```

**Exactamente duas escritas na corrida inteira.** População pequena: a máquina em volta
está sã, o defeito é pontual.

## 2. A aritmética fecha em dois sítios independentes

`func_002649FC` (`ppu_recomp_002.cpp:255861`) escreve:

```c
vm_write32(ctx->gpr[31] + 0x108, <fpr10>);      /* -> 0x00541280 */
ctx->gpr[0] = (int64_t)(int32_t)(0x40);
vm_write32(ctx->gpr[31] + 0x10C, ctx->gpr[0]);  /* -> 0x00541284, valor 0x40 */
```

- `r31 + 0x108 = 0x00541280` ⟹ **`r31 = 0x00541178`**
- `r31 + 0x10C = 0x00541284` ⟹ mesma resposta
- e o valor escrito no segundo, `0x40`, é **literalmente** o `gpr[0] = 0x40` da linha
  anterior — o watch e o código concordam no valor, não só no endereço.

`0x00541178` é o **TOC do EBOOT** (OPD do `e_entry`, E80).

> **`ctx->gpr[31]` contém o TOC em vez do ponteiro do objecto.** A estrutura de ~0x120
> bytes que a função devia inicializar é escrita por cima do rodata do jogo, e
> `0x541280`/`0x541284` são só dois dos campos.

## 3. Quem produz `r31` — quatro níveis, todos lidos, nenhum culpado ainda

```
func_00237CDC
  ├─ obj = func_00264BA8(...)          <- a fabrica; r3 = retorno
  │    └─ ... ps3_indirect_call ...
  │        ctx->gpr[2] = 0x00541178ULL;  /*TOCFIX ld r2,N(r1)*/   <- PATCH NOSSO
  │        r3 = vm_read32(r3 + 0x48); func_00263554(...)
  └─ func_00264988(r3 = obj)
       ctx->gpr[31] = ctx->gpr[3];     <- r31 := o argumento. CORRECTO.
       ... branch condicional ...
       └─ func_00264AF8
            { g_trampoline_fn = func_002649FC; return; }   <- b PARA TRAS
                 └─ func_002649FC: escreve em r31 + 0x100..0x114
```

Aplicando *"se o input estivesse certo, o output estaria certo?"* a cada nível:

| nível | decide alguma coisa? | veredicto |
|---|---|---|
| `func_00457E44` | não — lê a memória que lhe deram | **vítima** (E82) |
| `func_002649FC` | não — escreve em `r31 + off` | **vítima** |
| `func_00264988` | faz `r31 := r3`, que é o correcto | **vítima** |
| `func_00237CDC` | passa o retorno da fábrica | **vítima** |
| `func_00264BA8` | **produz o valor** | **o alvo seguinte** |

## 4. Os oito fragmentos de uma função só

`FUN_00264988` é **uma** função para o Ghidra (`0x00264988–0x00264B20`). No lift são
**oito** símbolos, espalhados por **dois chunks diferentes**:

| símbolo | chunk | prólogo |
|---|---|---|
| `func_00264988` | 000 | real (`_cs_31 = ctx->gpr[31]`, salva) |
| `func_002649FC` | **002** | **lê da stack** (`vm_read64(gpr[1]+0x78)`) — fragmento |
| `func_00264AC8` | 000 | — |
| `func_00264AF8` | 000 | — |
| `func_00264B20`, `func_00264B60`(002), `func_00264B84`, `func_00264BA8` | 000/002 | — |

`0x002649FC` **não tem entrada própria em nenhum dos dois mapas** (`functions.json` nem
`ghidra_out/functions.json`) — é um ponto de entrada que o lifter descobriu, cujos
registadores são herdados.

## 5. Correcção a mim próprio: o item do CLAUDE.md **não** pode sair

Ontem escrevi, na leitura do E78, que o item *«auditar `g_trampoline_fn = func_...` onde
o alvo tem EA guest MENOR que o site»* podia sair do `CLAUDE.md` como **refutado por
construção**. **Isso estava errado, e o erro é meu.**

O que é verdade, e verifiquei em `tools/ppu_lifter.py:3645-3654`: o trampolim de
**fallthrough auto-anexado** resolve sempre para `≥ end > start` — pelos dois caminhos
(`func_by_addr[fallthrough_to]` e o fallback `sorted_addrs[addr_idx + 1]`). Essa metade
está mesmo refutada por construção.

O que **não** é verdade: que isso cubra o item inteiro. `func_00264AF8` termina com

```c
{ g_trampoline_fn = (void(*)(void*))func_002649FC; return; }
```

um trampolim **incondicional, em posição de cauda, com alvo para trás** — que **não** é
um fallthrough auto-anexado, é um `b` do jogo (o back-edge do ciclo interno de
`FUN_00264988`, legítimo no PPC porque origem e destino são a mesma função). O item do
`CLAUDE.md` cobre os dois, e a metade que sobra tem agora **um caso medido a produzir a
parede do boot**.

O item deve ser **reformulado**, não removido. O `tools/audit_fallthrough.py` já
distingue as duas coisas e já contava esta população: **675 candidatos em posição de
cauda com alvo para trás**, todos marcados LOW-confidence exactamente porque o sítio não
tem entrada no `functions.json` — que é a definição de fragmento.

## 6. O que fica para a medição seguinte

`func_00264BA8` produz o `r3`. Duas hipóteses, e a ordem é a que o `CLAUDE.md` manda
(*suspeitar de nós primeiro*):

- **H-A (nossa):** o `/*TOCFIX ld r2,N(r1)*/` que grava `0x00541178` está no corpo de
  `func_00264BA8`, a duas linhas do `r3` que é devolvido. É um patch **nosso**. Já houve
  um caso neste projecto em que a "referência válida" era uma região fabricada por um
  workaround nosso.
- **H-B (do jogo/lift):** a fábrica devolve o que `func_00263554` devolver, e o defeito
  está mais acima.

**Discriminador (desenhado, não corrido):** uma sonda no **call site** em
`func_00237CDC` — não no callee — que capture, na mesma linha e na mesma invocação, o
`r3` devolvido por `func_00264BA8` e o `r3` passado a `func_00264988`. Se já sai
`0x00541178` da fábrica, o alvo é o corpo dela (e H-A entra em teste directo); se sai são
e chega podre, o alvo é o que está entre as duas chamadas.

## Refutado nesta sessão

1. **«o escritor não passa por `vm_write*`»** — passa: dois `w32` no `heap_w()`. (A nota
   de 2026-08-01 no `ppu_loader.cpp` dizia isso de **outro** endereço, `0x400C3D88`; não
   se generaliza.)
2. **«`func_002649FC` calcula mal o offset»** — os offsets `+0x108`/`+0x10C` são
   coerentes com a estrutura e com o valor `0x40` do código. O offset está certo; a base
   é que não.
3. **«o item de fallthrough do CLAUDE.md está integralmente refutado»** — refutado eu,
   ver §5.

---

## 7. Correcção do §5 — o back-edge é o caminho, não a causa

Escrita no mesmo dia, antes da medição seguinte, porque o custo de uma retratação
cresce a cada ronda que fica de pé.

### O que a mediu

Contei a população da forma que tinha acusado — trampolim incondicional em **posição de
cauda** com alvo **para trás**, classificada por o sítio e o alvo caírem na **mesma**
função do export Ghidra (`tail_backedges.py` sobre os 7 chunks + `ghidra_out/functions.json`):

| | |
|---|---|
| trampolins em cauda com alvo para trás | **17 294** |
| **na mesma função do Ghidra** (a forma que acusei) | **15 583** |
| em funções diferentes (tail-call legítimo) | 891 |
| sítio ou alvo sem dono no export | 820 |

O caso do E83 (`func_00264AF8 → func_002649FC`, dentro de `FUN_00264988`) **está** na
lista — mas com 15 583 companheiros. Se a forma fosse por si só o defeito, o boot não
chegava ao `frame=1`.

### O argumento que não depende da contagem

A contagem sozinha seria fraca (podia haver 15 583 sítios nunca executados). O que fecha
é estrutural e já estava medido no §3:

- os fragmentos partilham o **mesmo `ppu_context*`**, passado por ponteiro; um trampolim
  entre eles **não pode** alterar `r31`;
- `func_00264988` faz `ctx->gpr[31] = ctx->gpr[3]` — **r31 é o argumento**;
- logo **`r31` errado ⟺ `r3` errado**, e o caminho por onde o fluxo passou depois é
  irrelevante para o valor.

**Se `r3` estivesse certo, as escritas iam para o objecto — back-edge ou não.**

### O que sobrevive e o que cai

| | |
|---|---|
| **sobrevive** (medido) | as duas escritas `w32`, 2/2 corridas; `r31 = 0x00541178` com a aritmética a fechar em dois sítios independentes; `r31 := r3` em `func_00264988`; a fábrica ser `func_00264BA8`; `FUN_00264988` ser 8 símbolos em 2 chunks; `0x002649FC` não ter entrada em nenhum dos dois mapas |
| **cai** (inferido) | «o trampolim em cauda com alvo para trás produz esta parede» |
| **fica em aberto, sem este caso a apoiá-lo** | se a forma alguma vez produz defeito. Os 15 583 são hoje **uma população por caracterizar**, não 15 583 bugs — e nada neste ledger dá evidência de que algum deles esteja errado |

### Consequência para o alvo

Nenhuma: o §6 já apontava para `func_00264BA8` e para o `r3`, e é isso que a sonda
`R31SRC` mede. A retratação não muda o próximo passo — muda a razão pela qual ele é o
próximo passo.
