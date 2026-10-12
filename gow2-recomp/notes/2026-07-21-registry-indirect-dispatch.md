# Dispatch indirecto do walk (`func_00171244`) — censo exaustivo do lift, offline (2026-07-21)

Continuação de `notes/2026-07-21-registry-caller-chain-A1.md` (Task 5 do plano
`../ps3recomp/docs/superpowers/plans/2026-07-21-shader-registry-typemap-walk.md`) e de
`notes/2026-07-21-typemap-walk-N1.md` (veredito **A1**: `[CMP-ENTER]=0`, `[TYMAP-171]=0`,
`[OPD-ICG]=0` em 2 boots). Aquela task provou o grafo de chamadas **directas**; esta faz o
mesmo para o universo de chamadas **indirectas/vtable** — censo exaustivo, não amostragem.
**Só leitura**: nenhum `.cpp` editado, nenhum build, nenhum boot, nenhum ficheiro
`.superpowers/sdd/*` tocado. `CLAUDE.md`/`build_macos.sh` não tocados (sessão concorrente).

## Pergunta

O `func_00171244` (walk; OPD `0x522E70`; vt `0x5130B8+8`) tem **zero** callers directos
(só definição + tabela `ppu_recomp_030`) e **nenhuma** referência hex-literal a
`0x5130B8`/`0x522E70` em lado nenhum do lift. Task 5 já apontou `func_0032E200` como
"único candidato" via vcall `this+0x8`, mas fê-lo subindo o grafo de chamadas **directas**
a partir dele — nunca confirmou, por censo exaustivo do universo de chamadas
**indirectas**, que não existe **outro** dispatcher (directo ou indirecto) capaz de
alcançar o walk. É essa lacuna que esta task fecha.

## Método — censo exaustivo, não grep pontual

O runtime distingue duas convenções de chamada indirecta (`ps3recomp/runtime/ppu/ppu_loader.cpp`):

- **`ps3_call_opd(ctx, opd_ea)`** (`:2052-2069`) — resolve um descritor OPD de 2 words
  `{code,toc}` e despacha. Usado pelo lift quando o slot lido é um **OPD** (não um
  endereço de código cru) — é o mecanismo que `func_00171244`/OPD `0x522E70` exige (GATE-
  FORCE chama-o assim, nunca via `ctx->ctr` cru, `:2208`).
- **`ctx->ctr = X; ps3_indirect_call(ctx)`** (`:1512+`) — trata `X` como endereço de
  código directo (`ppu_lookup`). É o mecanismo de `bctr`/`bctrl` "locais" (jump tables de
  FSM, ex. `func_002C0508`, ver comentário `:1707-1716`).

Censo 1 — **todos** os sites `ps3_call_opd(ctx, ...)` do lift inteiro (31 ficheiros
`ppu_recomp_*.cpp`, `recomp_macos_v2/`):

```
rg -n --no-heading "ps3_call_opd\(ctx" *.cpp   →   47 sites, TODOS em ppu_recomp_000/001/003.cpp
```

47 é um número fechado e pequeno — dá para inspeccionar **cada um** (contexto de 6 linhas
antes), não só amostrar. Resultado: dos 47, exactamente **5** leem offset `+0x8` no
registo que alimenta a chamada:

| Site | Ficheiro:linha | Padrão | Classe |
|------|-----------------|--------|--------|
| `[CMP-VCALL]` ×4 | `ppu_recomp_003.cpp:419856,419892,419947,419981` | `vm_read32(obj+0x0)` → `vm_read32(vt+0x8)` | dentro de `func_0032E200` (já conhecido) |
| `[STREAM-OPD]` ×1 | `ppu_recomp_000.cpp:337880` | `vm_read32(obj+0x0)` → `vm_read32(ops+0x8)` | dentro de `func_001856A8` — **classe diferente**, ver abaixo |

(`func_0032E200` tem 8 sites `[CMP-VCALL]` no total — 4 usam `ctx->gpr[9]+0x8`, 4 usam
`ctx->gpr[9]+0x4` ou `+0xC`; só os 4 de `+0x8` interessam ao slot do walk. Os outros 39
sites `ps3_call_opd` usam offsets `+0x4, +0x10, +0x14, +0x18, +0x1C, +0x20, +0x24, +0x28,
+0x2C, +0x40, +0x48, +0x4C` — nenhum é `+0x8`.)

**`func_001856A8` não é um candidato**: é o leitor de stream `SHADERSRC`/ICGLdr (âncora do
plano; check `apply_all_patches.sh` #2 "sem isto SHADERSRC lê sempre N=0"). O
`vm_read32(gpr11+0x8)` em `:337874` lê o slot **"refill"** da tabela `ops` do stream
(`gpr11 = vm_read32(stream+0x0)` = `ops`), confirmado pelo próprio layout que o
`GATE-FORCE` documenta (`ppu_loader.cpp:2160-2163`: "`+0x0` ops, `+0x4` cur, `+0x8` end...
Layout matches 1856A8"). É uma tabela de callbacks de **stream** (classe `IcgStream` ou
equivalente), não a vtable da classe typemap — o vptr desse objecto nunca pode valer
`0x5130B8` (são instâncias de classes C++ diferentes). Confirmado por leitura integral de
`func_001856A8` (`:337830-337917`): o `ps3_call_opd` em `:337880` está dentro do loop de
cópia `loc_00185718`, chamado a cada iteração para repor bytes no buffer — não tem relação
com o tipo `0xF85F9B1E` nem com o typemap.

### Verificação de completude (o `+0x8` é comum no motor — por isso o censo tem de ser pelo `ps3_call_opd`, não pelo padrão cru)

Um segundo censo, **agnóstico a registo** (PCRE2 com backreference, via `rg -U -P`),
procurando o idioma `ctx->gpr[R1] = vm_read32(ctx->gpr[R0] + 0x0); ... vm_read32(ctx->gpr[R1]
+ 0x8)` em qualquer lado do lift (não só perto de `ps3_call_opd`) devolve **3497
ocorrências** espalhadas por 10 ficheiros diferentes (`ppu_recomp_000/001/002/003/004/005/
006/013/021/028.cpp`). Ou seja: "ler vtable, chamar slot 2" é um idioma **genérico do
motor** (dezenas/centenas de classes C++ distintas do jogo partilham a forma
"vptr+8 = 3º método"), não algo específico do typemap. Confirma que **não é seguro**
identificar "o" dispatcher do walk só pelo padrão textual cru — é preciso ancorar na
convenção de chamada correcta. Como sessões anteriores já estabeleceram por evidência
independente que o slot `0x5130B8+8` guarda um **OPD** (não um código cru — é assim que o
GATE-FORCE o chama, `ps3_call_opd(ctx, 0x522E70)`, nunca `ctx->ctr=0x171244`), o universo
relevante fica restrito aos 47 sites `ps3_call_opd`, não aos 3497. Este é o corte
metodológico que justifica o resultado abaixo, não uma omissão.

**Contraprova do resíduo (a hipótese "despacho via `ctx->ctr` cru"):** mesmo que existisse
algures um site dos 3497 que tratasse o slot como código cru (`ctx->ctr=valor;
ps3_indirect_call`), isso teria de **passar pela primeira instrução do corpo** de
`func_00171244` para produzir qualquer efeito — e essa instrução é exactamente onde
`[TYMAP-171]` está instalado (`patch_tymap_probes2.py`), **incondicional**, independente
do mecanismo de chegada. `[TYMAP-171]=0` em 2 boots (N1, N1b — `notes/2026-07-21-typemap-
walk-N1.md`) já fecha esta hipótese empiricamente, por um caminho ortogonal ao censo
estático: não interessa QUANTOS sites textuais poderiam teoricamente alimentar o walk — o
corpo da função nunca executa a primeira linha, por nenhuma via.

## `func_0032E200` — forma confirmada do "dispatcher genérico" pedido

Releitura integral (`ppu_recomp_003.cpp:419639-420005`) confirma a forma exacta que o
brief descreve ("função genérica que lê o vtable pointer de um objecto typemap e chama o
método em +8"): `func_0032E200` é uma rotina de **insert-or-replace num hashmap com buckets
encadeados** — percorre correntes via campos de link `obj+0xC`/`obj+0x10` (`loc_0032E230`,
`loc_0032E344`↔`loc_0032E618`), usa `ppu_lwarx`/`ppu_stwcx` (refcount lock-free) antes/depois
de cada substituição, compara chaves via `func_00471F38`, e — **quando desaloja uma entrada
já ocupada** — chama o método `+0x8` do vtable do valor antigo (`[CMP-VCALL]`, 8 sites,
sempre `vm_read32(obj+0x0)` → `vm_read32(vt+0x8 ou +0x4 ou +0xC)` → `ps3_call_opd`). Não é
"iterar todas as entradas registadas e despachar em cada uma" (não existe outro loop desse
tipo no universo dos 47 `ps3_call_opd`) — é um genérico "map Set()" que só toca o vtable de
UMA entrada por chamada, a que está a ser substituída. Se essa entrada for, nalguma
chamada concreta, o objecto typemap com vt `0x5130B8`, o slot `+0x8` resolve para OPD
`0x522E70` = `func_00171244`. O verdadeiro "dispatcher genérico por tipo" é **interno** ao
próprio walk: `func_00171244` chama `func_0018E814` (`ppu_recomp_000.cpp:347048`,
`[TYMAP-LK]`/`[TYMAP-HIT]`, lookup num mapa em `TOC-0x3A3C`) — mas isso só importa se o
walk chegar a entrar, o que nunca acontece.

**Reconfirmação independente do caller único** (fresh `rg`, não reaproveitando o grep de
Task 5): `func_0032E200` aparece exactamente 3× em todo o lift —
`ppu_recomp_030.cpp:517111` (tabela OPD), `ppu_recomp_003.cpp:419639` (definição),
`ppu_recomp_001.cpp:147773` (`func_0032DF98`, `if ((!((ctx->cr>>0)&2))) { g_trampoline_fn
= func_0032E200; return; }`). Zero outras referências — directas ou (por construção, ver
censo acima) indirectas.

## Gate morto — reconfirmado em primeira mão

Reli directamente `func_00468C3C` (`ppu_recomp_001.cpp:438693-438754`, não só citei Task
5): linha `:438718` é `ctx->gpr[30] = (int64_t)(int32_t)(0);` — `li r30, 0`, literal, sem
qualquer leitura de argumento/WAD/heap entre `:438694` e `:438754`. Esse `0` é escrito em
`sp+0x7C` (`:438752`, imediatamente antes de `func_00330D54(ctx)`) e reencaminhado sem
alteração através de `func_00330D54` (`:149630`) → `func_0032DF98` (`:147742`) até ao gate
`:147773`, exactamente como Task 5 documentou. Sem drift.

## GATE-FORCE prova que a resolução OPD **não** é o problema (descarta classe c)

`ps3_force_post_wad_icgldr` (`ppu_loader.cpp:2200-2213`) chama
`ps3_call_opd(ctx, 0x00522E70u)` manualmente e o comentário de cabeçalho (`:2071-2083`)
descreve isto como "prova o path mecânico" — ou seja, nalguma sessão anterior este call
FOI exercitado com sucesso (senão o comentário não afirmaria "confirms... ICGLdr is
registered" com essa convicção). Isto mostra que a máquina de resolução `ps3_call_opd` →
`ppu_opd_resolve` → `ppu_lookup(0x00171244)` **funciona correctamente** quando alimentada
com o OPD certo — não há gap do lifter na resolução do alvo. Se `func_0032E200` alguma vez
corresse com o objecto certo em `gpr[9]`/`gpr[11]`, a chamada resolveria e executaria
`func_00171244` sem problema. **Classe (c) — falha de resolução do lifter — está
descartada** pela evidência já existente, não precisa de novo probe.

## Veredito

**Dispatcher indirecto natural = `func_0032E200` (`ppu_recomp_003.cpp:419639`), confirmado
por censo exaustivo (não amostragem) como o único site em todo o lift — entre 47 chamadas
`ps3_call_opd` e, por extensão argumentada, entre as 3497 ocorrências cruas do idioma
vtable — capaz de produzir uma chamada a OPD `0x522E70`/`func_00171244`.**

**Classe: (a) nunca alcançado.** Convergência de 3 métodos independentes:

1. Grafo de chamadas directas (Task 5): único caller de `func_0032E200` é
   `func_0032DF98:147773`, gate fechado por um literal `0` escrito em `func_00468C3C:438718`
   — fechado **por construção**, não por falta de dados em runtime.
2. Censo exaustivo de chamadas indirectas (esta task): nenhum outro site — directo ou
   indirecto — pode alcançar `func_00171244`; `func_001856A8`/`[STREAM-OPD]` é uma classe
   distinta (stream ops, não typemap) que só parecia candidata pelo offset coincidente.
3. Prova empírica in-boot (N1, ortogonal aos dois métodos estáticos acima):
   `[CMP-ENTER]=0`, `[TYMAP-171]=0` (probe incondicional na 1ª instrução do walk — fecha
   até a hipótese teórica de um dispatch via `ctx->ctr` cru que o censo por `ps3_call_opd`
   não veria), `[OPD-ICG]=0` (probe incondicional dentro do próprio `ps3_call_opd`,
   independente do lift) — 2 boots, 85s+150s, ~236k linhas de log combinadas.

Classe (b) (alcançado mas filtra o tipo) não se aplica — não há alcance nenhum a filtrar.
Classe (c) (alvo indirecto não resolve) está descartada pela evidência do próprio
GATE-FORCE (secção acima).

## Próximo probe concreto recomendado (não implementado)

O elo que falta **medir empiricamente** (nem N1 nem Task 5 mediram directamente) é se
`func_00468C3C` — o "único ponto de entrada estático" de toda a subárvore, per Task 5 —
chega sequer a **entrar** no boot natural. Task 5 já suspeitava que sim ("fan-in largo...
provavelmente corre... não confirmado in-boot"), mas isso ficou como hipótese não testada.

Achado extra desta task, útil para o próximo passo: o lift já tem instrumentação
(`patch_icg_ctor_opd.py`) nos ascendentes de `func_00468C3C` — `[ICG-PATH-A-OPD]` em
`func_00329490` (`ppu_recomp_001.cpp:142629`, um dos ascendentes de fan-in largo
identificados por Task 5) e `[ICG-VCALL]` em `func_0014A01C`/`func_0014AD94`/
`func_00151248` (construtor/init do componente ICG, `ppu_recomp_000.cpp:277351/278214/
285073`) — e o próprio autor do patch documentou `func_00329490` como estando "on the
**only** static call chain that reaches 32E200 vt+0x8". N1 já correu com
`PS3_TRACE_TYMAP=1` (que activa estas tags) mas a tabela publicada só reporta
CMP-ENTER/CMP-VCALL/TYMAP-*/LDRSH/OPD-ICG — **não tabulou** `[ICG-VCALL]` /
`[ICG-PATH-OPD]` / `[ICG-PATH-A-OPD]` / `[WADLD-GEND]` / `[WADLD-CALL]` / `[WADLD-OPD]` /
`[TYMAP-HIT]` / `[TYMAP-RET]`, apesar de estarem activas nesse boot.

**Probe recomendado (env-gated, zero código novo — reaplicar só se o lift tiver sido
relifted; caso contrário é só reler logs):**

1. Se os logs brutos de N1/N1b (`/tmp/tymap_N1.log` e equivalente) ainda existirem, grep
   directo por essas 8 tags — custo zero, pode já responder a pergunta.
2. Senão, **um** boot natural adicional (mesma recipe exacta de N1, `PS3_TRACE_TYMAP=1
   PS3_TRACE_LDRSH=1 PS3_TRACE_SHADERSRC=1 PS3_TRACE_SHREG=1`, sem `PS3_GATE_FORCE`, ~90s)
   tabulando as 8 tags acima.
3. Opcional, se (1)/(2) deixarem `func_00468C3C` ainda ambíguo: adicionar 1 linha
   incondicional (não gated por `PS3_TRACE_TYMAP`, no mesmo estilo minimalista do
   `[CMP-ENTER]` já existente) na primeira instrução de `func_00468C3C`
   (`ppu_recomp_001.cpp:438693`) — contador simples, sem ler argumentos nem alterar
   controlo de fluxo.

**Poder discriminador:** se `[ICG-PATH-A-OPD]` (que corre dentro de `func_00329490`) tiver
contagem > 0, isso prova que toda a cadeia até esse ascendente corre — estreitando o "nunca
alcançado" para um intervalo pequeno e concreto entre `func_00329490` e `func_0032E200`
(reforça o achado de Task 5: só falta o literal em `func_00468C3C:438718` deixar de ser
`0`). Se `[ICG-PATH-A-OPD]` for também 0, o problema é mais cedo — construção do próprio
componente ICG (classe A3, ainda não investigada; ver Task 2 do plano-mãe,
`ps3_trace_typemap_slot`, não implementada) — e o alvo do próximo trabalho muda de "por que
o gate fica em 0" para "por que o componente/subárvore ICG nem é construído/percorrido".

## Âncoras verificadas nesta sessão (primeira mão, sem drift)

`func_00171244` `ppu_recomp_000.cpp:317308`; `func_0032E200` `ppu_recomp_003.cpp:419639`
(único caller real: `func_0032DF98:147773`; 8× `[CMP-VCALL]`, 4 deles em offset `+0x8`:
`:419856,419892,419947,419981`); `func_0032DF98` `ppu_recomp_001.cpp:147742`;
`func_00330D54` `ppu_recomp_001.cpp:149630`; `func_00468C3C` `ppu_recomp_001.cpp:438693`
(gate-source literal `:438718`); `func_00329490` `ppu_recomp_001.cpp:142629`;
`func_001856A8` `ppu_recomp_000.cpp:337830` (`[STREAM-OPD]` em `:337880`, offset `+0x8` mas
classe distinta — stream ops, não typemap); `func_0018E814` `ppu_recomp_000.cpp:347048`
(`[TYMAP-HIT]` `ps3_call_opd` em `:347102`); `func_00321034` `ppu_recomp_001.cpp:136187`
(registo do tipo `0xF85F9B1E`); `func_0014A01C`/`func_0014AD94`/`func_00151248`
`ppu_recomp_000.cpp:277351/278214/285073` (`[ICG-VCALL]`, ctor/init ICG). Censo: 47 sites
`ps3_call_opd(ctx,...)` em todo o lift (`ppu_recomp_000/001/003.cpp` apenas); 3497
ocorrências do idioma cru `vt=read(obj+0);slot=read(vt+8)` em 10 ficheiros (censo PCRE2
`rg -U -P`, register-agnostic, não usado para identificar o dispatcher — só para justificar
por que o corte pelo universo `ps3_call_opd` é o correcto). Patches citados:
`recomp_mid_v2/patch_32e200_opd.py`, `patch_icg_ctor_opd.py`, `patch_tymap_18e814.py`,
`patch_1856a8_stream_opd.py`, `patch_tymap_probes2.py` — todos já aplicados no lift actual
(`apply_all_patches.sh` default `recomp_macos_v2`).
