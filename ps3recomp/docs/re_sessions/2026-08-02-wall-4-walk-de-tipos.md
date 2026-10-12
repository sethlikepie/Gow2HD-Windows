# Sessão de RE — Parede 4 (walk de tipos, `func_0041F700`) — 2026-08-02

Sessão conduzida com `docs/RE_PLAYBOOK_GOW2.md` (a primeira). Fase 20, plano 20-02, `RE-02`.

**Veredicto em uma linha:** a parede continua **ABERTA**, mas três hipóteses caíram por
medição e a sessão devolve um **mecanismo candidato medido estaticamente** que liga as
paredes 1, 3 e 4 à mesma causa — e o experimento in-boot que o confirma ou o mata.

---

## Parede escolhida e porquê (custo, não ambição)

Candidatos com medição fresca (2026-08-01/02) no `.planning/ROADMAP.md` § `Phase 11`:
paredes 1 (`func_002545B0`), 2 (`func_002182A4`), 4 (`func_0041F700`) e a wall D
(shaders / `SHADERSRC N=0`).

Escolhida a **parede 4**. Razões de **custo**, não de ambição:

- tem um **EA único e concreto** já medido (`0x0041F700`, 27 539 083 iterações = 99,96%
  do rasto), portanto a pergunta já está formulada por outra sessão;
- é a única parede **sem gate de diagnóstico**, logo não há paliativo nosso a contaminar
  a evidência (o `CLAUDE.md` manda suspeitar de nós primeiro — aqui não há de quê);
- é atacável **inteiramente offline**: `ghidra_out/` + o lift em disco. **Zero boots**,
  zero risco de repetir o incidente de load 75.

Wall D foi posta de lado por custo: exigiria um boot com `PS3_MOVIE_EOS=1` até ao
`R_PermA` para ter qualquer sinal novo.

---

## Pergunta (uma frase)

> **Porque é que `func_0041F700` executa 27 539 083 iterações (99,96% do rasto) durante o
> boot, se a poda `child_tag == parent_tag` devia limitar o walk do grafo de tipos?**

---

## Âncoras

| âncora | valor |
|---|---|
| **EA guest** | `0x0041F700` (push do walk) — e, descobertos nesta sessão, `0x0041F924` (pop), `0x0041FA90` (registrador da tabela), `0x0041FF70` (walk exterior), `0x002545B0` (parede 1) |
| **símbolo Ghidra** | `.opd.FUN_0041f700  size=296` |
| **tabela de tipos** | `PTR_DAT_0053ef1c` (lida no lift como `vm_read32(ctx->gpr[2] - 0x225C)`) |
| **função liftada** | `func_0041F700` em `../gow2-recomp/recomp_macos_v2/ppu_recomp_001.cpp:388805` |
| **tag de log** | nenhuma — esta parede **não tem** instrumentação própria (é um achado da sessão; ver Próximo experimento) |

---

## Ferramentas usadas (comando + rc + o que devolveu)

Oráculo: `../gow2-recomp/ghidra_out` (existente, 25/jul, **13 143 funções, 13 096 com C
decompilado**). **Não** foi regenerado — `analyze_eboot_ghidra.sh` custa dezenas de
minutos e nada nesta sessão dependia de análise nova.

| # | comando | rc | devolveu |
|---|---|---|---|
| T1 | `python3 tools/ghidra_lookup.py ../gow2-recomp/ghidra_out 0x0041F700` | 0 | C decompilado de `.opd.FUN_0041f700`, 296 bytes |
| T2 | `... 0x0041F700 --callers --no-c` | 0 | `(nenhum -- pode ser chamada indirecta / por tabela)` |
| T3 | `... --grep 'PTR_DAT_0053ef1c' --max 40` | 0 | **18** funções (abaixo do limite 40 ⇒ lista completa) |
| T4 | `... --grep 'PTR_DAT_0053ef1c[^;]*\) = ' --max 40` | 0 | **1** função escreve a tabela: `0x0041FA90` |
| T5 | `... --writes 0xC8` | 0 | `nenhum resultado` — **falso negativo do tooling** (ver abaixo) |
| T6 | `... --grep 'char \*\)\(param_1 \+ 200\)' --max 20` | 0 | 20+ sítios, entre eles o **pop** `0x0041F924` |
| T7 | `... 0x0041FA90 / 0x0041FF70 / 0x002545B0 / 0x0041F828 / 0x0041F924 / 0x00254250` | 0 | C decompilado de cada |
| T8 | `awk '/^void func_0041F700/,/^}/' ../gow2-recomp/recomp_macos_v2/ppu_recomp_001.cpp` | 0 | função liftada completa, 60 linhas |
| T9 | idem para `func_0041F924`, filtrado por `rlwinm|rldicl|indirect_call` | 0 | as máscaras do pop |
| T10 | reimplementação de `ppc_rlwinm`/`ppc_rldicl` (de `tools/ppu_lifter.py:3491,3531`) em Python, aplicada aos dois `w0` medidos | 0 | tabela de índices (§ H3) |

**Achado de tooling (T5), já escrito no playbook §3.1:** `--writes 0xC8` devolve zero
para um campo que É escrito, porque o Ghidra emite o offset em **decimal**
(`*(char *)(param_1 + 200)`) quando o tipo inferido é `char *`. O `--writes` só constrói
padrões em hexadecimal. Um vazio do `--writes` **não** é prova de que ninguém escreve.

---

## Hipóteses

| # | hipótese | experimento discriminador | resultado | nível de evidência |
|---|---|---|---|---|
| **H1** | O lifter perdeu um *visited-set* / memoização que o binário original tem — a explosão é nossa | comparar o C do Ghidra em `0x0041F700` com a função liftada; se o original tivesse conjunto de visitados, apareceria como estrutura extra | **REFUTADA** | offline-unit |
| **H2** | `func_0041F700` é auto-recursiva; pôr um limite lá dentro resolve | procurar chamada a `0x0041F700` dentro de `0x0041F700`, no original **e** no lift; `--callers` para chamadas directas | **REFUTADA** | offline-unit |
| **H3** | Existe um limite de profundidade que o lift perdeu (o cursor em `+0xC8`) | comparar push e pop no original e no lift, e calcular numericamente os índices que cada um usa | **REFUTADA como perda do lift** — e devolveu o mecanismo candidato | offline-unit |
| **H4** | O grafo de tipos é mesmo completo e 12⁷ ≈ 35 M é o comportamento correcto do jogo | procurar no binário qualquer verificação de limite sobre o índice de tipo | **NÃO RESOLVIDA** — enfraquecida (ver §H4) | offline-unit |

### H1 — refutada: o lift é fiel; a forma exponencial está no jogo

O C do Ghidra para `0x0041F700` **não tem nenhum conjunto de visitados**. A única poda é

```c
uVar1 = *(uint *)(param_2 + 4);                       /* w0 do PAI  */
for (puVar2 = *(undefined4 **)(param_2 + 0x80); puVar2 != (undefined4 *)(param_2 + 0x80);
     puVar2 = (undefined4 *)*puVar2) {
  uVar3 = *(uint *)(puVar2[2] + 4);                   /* w0 do FILHO */
  if ((uVar3 >> 0x10 & 0xfff) != (uVar1 >> 0x10 & 0xfff)) {      /* <-- a poda */
    (*(code *)**(undefined4 **)(**(int **)(puVar4 + (uVar3 & 0xffff) * 4) + 0x40))
              (*(int **)(puVar4 + (uVar3 & 0xffff) * 4), puVar2[2] + 4);
  }
}
```

e o lift reproduz **as duas máscaras bit a bit**:

| máscara | Ghidra (original) | lift (`ppu_recomp_001.cpp`) | valor |
|---|---|---|---|
| poda (pai e filho) | `uVar? >> 0x10 & 0xfff` | `ppc_rldicl(w0, 48, 52)` | `(w0 >> 16) & 0xFFF` |
| índice da tabela | `(uVar3 & 0xffff) * 4` | `ppc_rlwinm(w0, 2, 14, 29)` | `(w0 & 0xFFFF) * 4` |

Verificado numericamente com as definições de `ppc_rlwinm`/`ppc_rldicl` copiadas de
`tools/ppu_lifter.py:3491` e `:3531`. **A explosão combinatória é a forma do código do
jogo, não um defeito do lifter.** Isto acrescenta uma 5.ª refutação à linha *"o lift é
fiel — 4 suspeitas de bug do lifter, 4 refutadas contra o binário"* do ROADMAP.

Verificação lateral do padrão de fallthrough do `CLAUDE.md`: o lift parte a função em
`func_0041F700` + `func_0041F7F8` (`g_trampoline_fn = func_0041F7F8`), enquanto o Ghidra
dá `size=296` (`0x0041F700..0x0041F828`) numa peça só. O alvo do trampolim tem EA
**maior** que o site ⇒ **não** é o fallthrough-para-trás do `func_002550C8`. Fragmentação
normal.

### H2 — refutada: a recursão é indirecta, por vtable

Nem o C original nem a função liftada contêm uma chamada a `0x0041F700`. A única saída é

```
ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x40);   /* slot +0x40 da vtable do gestor */
ps3_indirect_call(ctx);
```

e `--callers` confirma: `(nenhum -- pode ser chamada indirecta / por tabela)`.
`0x0041F700` **é** o handler `+0x40` de um dos gestores registados em
`PTR_DAT_0053ef1c`; as "27 M iterações" são **re-entradas** por `ps3_indirect_call`, não
voltas de um `for` numa só moldura.

**Consequência prática:** qualquer limite posto dentro de `func_0041F700` seria um gate
sobre o sintoma. O ciclo fecha-se **pela tabela de tipos**, e é lá que a causa tem de ser
nomeada.

### H3 — refutada como perda do lift; e devolve o mecanismo candidato

Os dois lados do walk são um **par push/pop** sobre um cursor de **1 byte com sinal** no
próprio gestor:

| | função | operação sobre `gestor+0xC8` | slot da tabela que usa |
|---|---|---|---|
| **push** | `0x0041F700` | `*(char*)(mgr+200) += 1`; grava o nó em `mgr+0x48 + cur*4` | `tab[w0 & 0xFFFF]`, handler `+0x40` |
| **pop** | `0x0041F924` | `*(char*)(mgr+200) -= 1`; lê o nó de `mgr+0x48 + cur*4` | `tab[(w0>>16) & 0xFFF]`, handler `+0x44` |

O par existe **igual** no original e no lift (`ppc_rldicl(w0,48,52)` seguido de
`ppc_rlwinm(.,2,18,29)` no pop). **Nada foi perdido pelo lifter.**

Mas push e pop **indexam a mesma tabela por campos diferentes da mesma palavra**. E a
tabela é escrita por **uma única** função (T4), `0x0041FA90`, que indexa por
`((uint)param_1[1] >> 0xe & 0x3ffc)` — isto é, **`((w0 >> 16) & 0xFFF) * 4`**, o campo do
**pop**. Ou seja: **a tabela é indexada por `subtag` por construção.**

Aplicado aos dois `w0` medidos no ROADMAP (cálculo T10):

| `w0` | push `0x0041F700` → `tab[w0 & 0xFFFF]` | `subtag = (w0>>16)&0xFFF` | pop `0x0041F924` → `tab[subtag]` | registrador `0x0041FA90` | |
|---|---|---|---|---|---|
| `0xC0010001` (família *listas*) | slot **1** | 1 | slot **1** | slot 1 | ✅ coincidem |
| `0x40030001` (família *registo WAD*) | slot **1** | 3 | slot **3** | slot 3 | ❌ **DIVERGEM** |

**O mecanismo candidato**, portanto: o walk só é coerente enquanto valer o invariante
**`w0 & 0xFFFF == (w0 >> 16) & 0xFFF`**. A família das listas satisfá-lo (1 == 1); o
registo WAD medido **não** (1 ≠ 3). Para um nó desses:

- o **push** entrega-o ao gestor **1** e incrementa o cursor **do gestor 1**;
- o **pop** devolve-o ao gestor **3** e decrementa o cursor **do gestor 3**;
- ⇒ o cursor do gestor 1 **nunca desce** e o walk **nunca desenrola**.

E fecha com a parede 1 sem precisar de hipótese nova: `func_002545B0` também indexa por
`*(ushort *)(obj + 6)` = os 16 bits **baixos** (big-endian) ⇒ `tab[1]`. É por isso que o
handler do gestor 1 lê `arg+0x7C` como sentinela de lista num objecto cujo `+0x7C` é
`matriz[0][3]`: **não porque `tab[1]` seja a fábrica errada, mas porque um objecto de
subtag 3 foi despachado para o slot 1 pelo campo baixo.**

Tally completo dos 18 consumidores da tabela (T3 + inspecção do C de cada):

| campo usado como índice | funções |
|---|---|
| **low16** (`w0 & 0xFFFF`) | `0x00253DE4`, `0x00253F8C`, `0x002545B0`, `0x00254788`, `0x00256B1C`, `0x0041E780`, `0x0041EA00`, **`0x0041F700`**, `0x0041F828`, `0x0041FCC0`, `0x0041FF70`, `0x00420254` — **12** |
| **subtag** (`(w0>>16) & 0xFFF`) | `0x00254250`, **`0x0041F924`**, **`0x0041FA90`** (o registrador) — **3** |
| slot fixo, sem tag | `0x00252688`, `0x002532B0`, `0x00253BAC` — **3** |

Nota de aritmética que reforça a leitura: entre `+0x48` e `+0xC8` há `0x80` bytes = **32
slots** de 4 bytes, e o cursor é um `char` **com sinal**. Um walk que empurra sem
desenrolar passa de 127 e torna-se negativo, e o `vm_write32(mgr + 0x48 + cur*4)` passa a
escrever **abaixo** do gestor. No pop, `cVar1 < 0` faz `iVar6 = 0` e a seguir
`*(uint *)(iVar6 + 4)` lê o endereço guest **4**.

### H4 — não resolvida, mas enfraquecida

`0x00254250` verifica `if (uVar1 < 0x21)` sobre o tag antes de o usar — 0x21 = 33, que
bate certo com as *26 entradas, tags 0..32* já medidas no ROADMAP. **`func_0041F700` não
faz verificação de limite nenhuma** sobre o seu `w0 & 0xFFFF` (que tem 16 bits de
alcance, não 5). Não é prova de defeito, mas mostra que o próprio jogo trata este índice
como algo que precisa de ser validado noutros sítios.

---

## O que fica REFUTADO por medição

1. **"O lifter perdeu a memoização / o limite do walk 4."** As duas máscaras da poda e do
   índice batem bit a bit entre o C do Ghidra e a função liftada (H1, H3). *(offline-unit)*
2. **"`func_0041F700` é auto-recursiva; limita-se lá dentro."** Nem o original nem o lift
   contêm chamada a si próprio; o ciclo fecha por `vtable+0x40` (H2). *(offline-unit)*
3. **"`--writes` diz que ninguém escreve `+0xC8`, logo não há pop."** Falso negativo do
   tooling: o Ghidra emite o offset em decimal. O pop existe e é `0x0041F924` (T5/T6).
   *(offline-unit)*
4. **"Só há duas hipóteses para a parede 1: ou o tag do objecto está errado, ou `tab[1]`
   despacha para a fábrica errada"** (critério 1 da Fase 11). Há uma **terceira**, e é
   consistente com toda a medição: o objecto tem **dois** campos de tag e o caminho de
   push escolhe o **baixo**, enquanto a tabela é **registada pelo alto**. *(offline-unit)*

---

## Veredicto: **ABERTO** — avançado, não resolvido

Nada foi corrigido. Nenhum gate foi ligado. Nenhum boot foi corrido. O que a sessão
entrega é **um mecanismo candidato medido estaticamente** que explica as paredes 1, 3 e 4
com uma única causa (`push` indexa por low16, tabela é registada por subtag), mais três
refutações que impedem a próxima sessão de gastar tempo no lifter.

---

## O que esta sessão NÃO afirma

- **Não** afirma que o slot 3 da tabela está preenchido, nem que contém um gestor de
  registos WAD. Isso não foi medido — a tabela é preenchida em runtime.
- **Não** afirma que o cursor `+0xC8` do gestor 1 cresce sem descer numa corrida real.
  É uma **previsão** do mecanismo, ainda por observar in-boot.
- **Não** afirma que `w0 = 0x40030001` é a palavra lida naquele sítio de despacho — isso
  é medição de **outra** sessão (2026-08-01/02, registada no ROADMAP); esta sessão
  aceitou-a como entrada e calculou as consequências.
- **Não** afirma que o jogo original tem um bug. O mais provável é o inverso: que o
  invariante `low16 == subtag` seja garantido pelo produtor do objecto, e que o defeito
  esteja **a montante**, em quem construiu ou enfileirou o registo WAD. Um jogo que
  saiu em disco não fazia 27 M de iterações no boot.
- **Não** afirma nada sobre pixels, shaders ou `SHADERSRC`. A parede D não foi tocada.
- **Não** reabre nada da lista *"O que já está ELIMINADO por medição"* do ROADMAP.

---

## Próximo experimento para quem retomar (Fase 11)

Por ordem de custo. **E1 é o discriminador**: separa o mecanismo candidato da hipótese
nula ("o grafo é mesmo assim") numa só corrida.

**E1 — despejar os dois índices no sítio do despacho (in-boot, uma corrida, gated).**
Sonda `PS3_TYPEWALK_TRACE=1` (OFF por default) no push `0x0041F700`, a imprimir para cada
nó: `w0`, `low16 = w0 & 0xFFFF`, `subtag = (w0>>16) & 0xFFF`, `tab[low16]`, `tab[subtag]`
e o cursor `*(char*)(mgr+0xC8)`. Limitar a ~200 linhas — o rasto tem 27 M de eventos.

| previsão se o mecanismo for real | previsão se for falso |
|---|---|
| aparecem nós com `low16 != subtag`, e para esses **`tab[subtag]` é não-nulo e diferente de `tab[low16]`**; o cursor do gestor `tab[1]` **sobe monotonicamente** e passa de 32 | todos os nós têm `low16 == subtag` (a divergência era só do `w0` de uma sessão anterior), e o cursor sobe e desce ⇒ a explosão é mesmo combinatória e H4 fica de pé |

**E2 — quem escreve o campo low16.** `0x00254250` faz
`*(short *)(param_2 + 6) = (short)*(undefined4 *)(param_1 + 0x20)` — escreve os 16 bits
baixos do `w0` a partir de `param_1+0x20`. Se o invariante é estabelecido por um
produtor, é aqui ou perto. Offline: `ghidra_lookup --grep` por escritas a `+ 6)` e por
quem escreve `param_1 + 0x20`.

**E3 — sonda de CONTROLO.** Esta parede **não tem tag de log**. Antes de qualquer
bissecção, pôr uma sonda que *tem* de aparecer (contagem de entradas em `0x0041F700`),
senão um silêncio não distingue "não passou aqui" de "a sonda não foi compilada" — foi
exactamente assim que as Fases 7–10 mediram uma string inexistente.

**E4 — só depois.** Se E1 confirmar, o fix é **a montante** (o produtor do registo WAD,
ou quem o enfileira na lista de irmãos), **nunca** um limite dentro de `func_0041F700`
nem um quarto gate de diagnóstico. Um gate NUNCA fecha uma parede.

---

## Anti-padrões: o que esta sessão recusou fazer

- Não pôs um `if (depth > 32) return;` em `func_0041F700`. Seria um gate sobre o sintoma
  — e H2 mostra que nem sequer estaria na moldura certa.
- Não "corrigiu" o índice do push para `subtag`. Sem E1, isso seria divergir do PPC
  original com base numa inferência estática.
- Não editou nenhum `ppu_recomp_*.cpp`. Zero texto gerado por IA entrou no lift.
- Não correu nenhum boot, logo não há `pkill`, órfãos, nem logs no git.
- Não declarou a parede fechada nem "avançada em X%". Está **aberta**.
