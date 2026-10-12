# Sessão de RE — **E4**: verificação in-boot do fix candidato B — 2026-08-04

Continuação directa de [`2026-08-04-E3-outer-walk-subtag-census.md`](2026-08-04-E3-outer-walk-subtag-census.md)
(DECISÃO D-11.2: B) e de [`2026-08-04-E2-quem-forca-low16.md`](2026-08-04-E2-quem-forca-low16.md)
(os três candidatos). Conduzida com [`docs/RE_PLAYBOOK_GOW2.md`](../RE_PLAYBOOK_GOW2.md).

Esta sessão corresponde à Task 2 do `11-03-PLAN.md`. Ao contrário das sessões anteriores, não
termina com um único veredicto — descobre, por medição, que a **primeira** implementação do
fix B (a que `11-03-PLAN.md` prescrevia literalmente) tem **zero efeito** no boot, localiza a
causa raiz do erro de leitura, e implementa/verifica uma **segunda** versão que fecha a parede.
As duas rondas ficam registadas por inteiro — nada é reescrito por cima.

---

## Pergunta (uma frase)

> **O patch candidato B (`patch_outer_subtag_prune.py`) remove, no boot real, exactamente o
> push do registo WAD (`w0=0x40030001`) do walk exterior `func_0041FF70`, sem tocar nos ~175
> pushes legítimos da família de listas?**

---

## Âncoras (reaproveitadas de E1/E3, não redescobertas aqui)

| âncora | valor |
|---|---|
| walk exterior | `func_0041FF70`, chunk `ppu_recomp_001.cpp` |
| push (consumidor) | `func_0041F700` — hooks `TypewalkPushEnter`/`TypewalkPushChild`/`TypewalkPop` (E1, inalterados) |
| registo patológico (E1) | `w0=0x40030001`, `subtag=3`, `low16=1` |
| gate 1 | `PS3_LIST254_EMPTY_IF_NULL=1` |
| gate 2 | `PS3_POOL_NULL_IF_BAD=1` |
| gate 3 | `PS3_LIST547_EMPTY_IF_BAD=1` |
| trace do walk | `PS3_TYPEWALK_TRACE=1` |
| predicado de poda (11-02) | `tools/test_wall4_subtag_fix.py::prune(w0)` = `(w0&0xFFFF)==1 and ((w0>>16)&0xFFF)!=1` |

---

## Ronda 1 — o fix prescrito pelo plano, verificado, e refutado por medição

### O que o plano mandava fazer

`11-03-PLAN.md` (Task 1, `<action>` do ramo B) descreve o mecanismo assim: em `loc_0042002C`,
`ctx->gpr[27] = vm_read32(ctx->gpr[28] + 0x88)` carrega "o filho" (`h_child`); a seguir, uma
comparação `ctx->gpr[25]` (rotulado `iVar6` no decompile do Ghidra citado pelo plano) contra
`ctx->gpr[27]` (`iVar2`) decide se salta para `loc_00420090` (skip) ou cai no push condicional.
O plano instrui: ler `w0 = vm_read32(h_child+4)`, aplicar `prune(w0)`, e se verdadeiro, forçar
`ctx->gpr[25] = ctx->gpr[27]` para que o ramo **já existente** tome o caminho de skip.

Implementei exactamente isto (`patch_outer_subtag_prune.py`, v1) — tri-estado provado com
fixtures sintéticas e contra uma cópia do `ppu_recomp_001.cpp` de produção (hash antes/depois
confirmado, produção nunca tocada). Commit `7ce01bf`.

### Verificação in-boot — e a refutação

Lift dedicado (`recomp_macos_e4fix/`), patch aplicado + catálogo completo
(`apply_all_patches.sh` rc=0, `118 APPLIED / 5 ALREADY-APPLIED / 3 FAILED(PROBE, pré-existentes)
/ 15 UNVERIFIED` — `patch_outer_subtag_prune.py` novo, sem CONTRACTS.tsv, mesmo padrão que
`patch_freelist_guard_262610.py` já demonstrava), binário dedicado `boot_gow2_e4fix`. Corrida D
(90s, gates 1-3 ON + `PS3_TYPEWALK_TRACE=1`, G6: kill por PID, `orfaos=0`):

```
TYPEWALK_SUM (final): enters=176 chaves=14 w0_40030001=2 no_rep=27362359 (crescendo)
TYPEWALK_CALLER n=176: lr=0x002B2DE0 h=0x4077AD1C w0=0x40030001 sent_head=0x00000000
```

**Idêntico, número a número, à baseline sem fix (E1: `enters=176 chaves=14`, sessão de
2026-08-03).** O patch v1 não mudou NADA do comportamento observável — o push do WAD continua
a acontecer, com a mesma assinatura, o mesmo `sent_head=0`, a mesma explosão.

### Diagnóstico — porquê o v1 não fazia nada

Instrumentação temporária ad-hoc (não commitada, só no lift dedicado `recomp_macos_e4fix`,
removida depois) logo antes do segundo dispatch de `loc_00420090`, medindo directamente os
registos guest nesse ponto:

```
[DBG11-03] gpr27(hchild)=0x3F800000 gpr30=0x4077AD20 gpr31=0x4077AD20 gpr28=0x4077AD1C
```

`gpr27` (o "`h_child`" que o v1 lia) vale **`0x3F800000`** — o MESMO padrão de bits que a
sonda de censo do 11-01/E3 já tinha medido e deixado como ressalva não fechada
("`h_child = vm_read32(piVar8+0x88) = 0x3F800000` — um padrão de bits IEEE-754 exacto para
`1.0f`"). Não é um ponteiro válido: é lixo lido da região da matriz, a MESMA classe de
corrupção que a E1/E2 já tinham provado para `+0x7C`/`+0x80`. Ler `vm_read32(hchild+4)` nesse
endereço (`0x3F800004`) devolve dados irrelevantes, não o `w0=0x40030001` do registo WAD — por
isso `prune()` nunca disparava com sentido para este nó, e o compare que o v1 tentava influenciar
nunca era o que decidia o push real.

Reconstruindo o fluxo completo de `func_0041FF70` a partir do código liftado (não só do
decompile citado pelo plano): a comparação `gpr25 vs gpr27` em `loc_0041FFF8`-ish só protege
**um** de **dois** dispatches `vt+0x40` dentro da função. O segundo — incondicional, sempre
alcançado via `loc_00420090` (quer por fallthrough quer pelo `goto`) — usa
`low16 = vm_read16(gpr28 + 0x6)` (mesmo endereço que `vm_read16(gpr30+0x2)` da chamada `vt+0x48`
logo antes), e essa leitura vem do **próprio nó de topo** (`gpr28`, "`h`" do objecto sendo
enumerado), não de `h_child`. Medido directamente: `vm_read32(gpr28+4) = 0x40030001` — o `w0`
do próprio nó de topo é a classificação patológica, e é ESSE segundo dispatch,
incondicionalmente, que despacha para `tab[1] = func_0041F700` com o registo WAD.

**Isto invalida o mecanismo prescrito pelo plano para o candidato B** (o `h_child`/`iVar6`
comparado não é o que determina se o WAD é empurrado) — não invalida a decisão D-11.2 (B
continua certo: o walk exterior é onde o filtro deve ir), nem os três candidatos de D-11.1.

---

## Ronda 2 — fix corrigido, aplicado, verificado

### O novo mecanismo (v2)

Em vez de tentar guiar um compare condicional que só protegia UM dos dois dispatches, o patch
v2 insere um **terceiro filtro** logo a seguir a `ctx->gpr[28] = ppc_rldicl(ctx->gpr[26], 0,
32);` em `loc_0042002C` — o MESMO ponto onde o gate "tem filho" já existe duas linhas abaixo,
usando o MESMO destino de skip (`goto loc_0041FFE8`) que esse gate e os dois filtros de flags
no topo do laço já usam para "este nó não tem nada aqui, avança". Lê
`w0 = vm_read32(gpr28+4)` (a classificação do PRÓPRIO nó de topo), aplica exactamente o mesmo
`prune(w0)` de `tools/test_wall4_subtag_fix.py` (sem re-derivar), e se verdadeiro, salta.

Isto corresponde, literalmente, à frase de D-11.1 para o candidato B: **"filtrar também por
subtag, não só por flags"** — um terceiro filtro de enumeração ao lado dos dois já existentes,
reutilizando um destino de saída (`loc_0041FFE8`) já presente no código, sem tocar em
`func_0041F700` nem inventar push/pop novo. Continua a não ser o candidato C: não valida a
lista antes de a percorrer, filtra ANTES de qualquer push acontecer, pela classificação do
próprio nó.

Tri-estado reprovado com fixtures novas (âncora mudou de sítio) e contra uma cópia da produção
real — CONVERTIDO/JÁ-APLICADO/SEM-EFEITO(rc=2) todos confirmados. Produção nunca tocada (hash
`ppu_recomp_001.cpp` idêntico antes/depois: `136ab046d1a0ced07fc5bda330eba8263783e7732434bb5a8253e7db15d0a989`).

### Verificação in-boot — 3/3 corridas, determinístico

Lift dedicado NOVO (`recomp_macos_e4fix2/`, para não misturar com o debug ad-hoc da ronda 1),
`apply_all_patches.sh` rc=0 (`119 APPLIED / 5 ALREADY-APPLIED / 3 FAILED(PROBE, mesmos três
pré-existentes) / 14 UNVERIFIED` — `patch_outer_subtag_prune.py` agora `APPLIED`, não
`UNVERIFIED`, porque o lift é novo e o patch escreveu de facto), `verify_lift` PASS
(`lift_parity PASS`, `MANIFEST PASS`, `audit_boundaries PASS`), binário dedicado
`boot_gow2_e4fix2`. Três corridas de medição (D, E, F — 90s, gates 1-3 ON +
`PS3_TYPEWALK_TRACE=1`, G6: kill por PID, `orfaos=0` nas três):

| corrida | `TYPEWALK_CALLER` total | `w0` distintos | `chaves` (SUM final) | `w0_40030001` | `no_rep` | `st620` max |
|---|---:|---|---:|---:|---:|---:|
| D | **175** | 175× `0xC0010001` | 12 | 0 | 0 | 11 |
| E | **175** | 175× `0xC0010001` | 12 | 0 | 0 | 11 |
| F | **175** | 175× `0xC0010001` | 12 | 0 | 0 | 11 |

**Idêntico nas três corridas.** `enters` caiu de **176 para 175** (exactamente o registo WAD
removido, nenhum dos legítimos afectado). Nenhuma linha `TYPEWALK_CALLER`/`TYPEWALK_SUM` com
`w0=0x40030001` ou `sent_head=0x00000000` em nenhuma das três corridas — a assinatura do
registo WAD desapareceu por completo. `chaves=12` (não 14) — as duas chaves de lixo que a E1
media ("`low16=0x0000 subtag=0x000`" com `no_rep=179 459 342`, e "`low16=0x5F7C
subtag=0xF72`") não aparecem mais, porque nunca chegam a ser visitadas (o push que as geraria
nunca acontece). `no_rep=0` nas três — sem laço sem-fim. `st620` chega a **11** nas três
corridas, **idêntico** à ronda 1 (com o fix quebrado) e à baseline — **sem regressão** na
progressão da intro/movie-FSM.

### Comparação directa v1 (quebrado) vs v2 (corrigido), mesma janela/receita

| medida | baseline (E1, sem fix) | v1 (fix quebrado) | v2 (fix corrigido) |
|---|---:|---:|---:|
| `enters` | 176 | 176 | **175** |
| `chaves` | 14 | 14 | **12** |
| `w0_40030001` | 2 | 2 | **0** |
| assinatura WAD (`w0=0x40030001`, `sent_head=0`) | presente | presente | **ausente** |
| `no_rep` | dezenas de milhões, crescendo | dezenas de milhões, crescendo | **0** |
| `st620` max (mesma janela) | — | 11 | 11 (sem regressão) |

---

## Suites de não-regressão (após v2, no repo `ps3recomp`)

| suite | resultado |
|---|---|
| `cmake -B build-tests -DCMAKE_BUILD_TYPE= && ctest --test-dir build-tests` | **23/23** (2 skip: `lift_selftest`, `spu_lift_selftest` — SDK PS3 indisponível neste ambiente, esperado) |
| `./scripts/test_python.sh` | **14 PASS / 2 SKIP / 0 FAIL** (mesmos dois skips) |

Ambos batem exactamente a barra que o `CLAUDE.md` documenta como "armadilha já paga" — sem
regressão face ao estado antes desta sessão.

---

## O que fica REFUTADO por medição

1. **Que o mecanismo prescrito por `11-03-PLAN.md` para o candidato B (mutar `gpr[25]` antes
   do compare `iVar6==iVar2`) remove o push do WAD.** **REFUTADO**, medido em 3/3 corridas da
   ronda 1: `enters/chaves/w0_40030001/no_rep` idênticos à baseline sem fix. *(in-boot)*
2. **Que `h_child` (`ctx->gpr[27] = vm_read32(gpr28+0x88)`) é a classificação relevante do nó
   sendo empurrado.** **REFUTADO** — é lixo (`0x3F800000`, float da matriz), confirmado por
   leitura directa. A classificação relevante é a do PRÓPRIO nó de topo
   (`vm_read32(gpr28+4)`). *(in-boot, medição ad-hoc)*
3. **Que a comparação `iVar6==iVar2` é o único dispatch `vt+0x40` da função.** **REFUTADO** —
   há um segundo dispatch, incondicional, em `loc_00420090`, que é o que efectivamente
   despacha o WAD. *(leitura do código liftado, corroborada pela medição)*

## O que fica CONFIRMADO por medição

1. **D-11.2 DECISÃO: B continua correcta** — o walk exterior é de facto onde o filtro deve
   ir; não foi preciso reabrir para A. *(in-boot, 3/3 corridas convergentes)*
2. **O predicado `prune(w0)` de `tools/test_wall4_subtag_fix.py` é o predicado certo** —
   aplicado à classificação certa (a do próprio nó, não a de `h_child`), remove exactamente o
   caso patológico e nenhum dos 175 legítimos. *(in-boot, 3/3)*
3. **Sem regressão** na progressão do boot (`st620` idêntico) nem nas suites offline.

---

## Veredicto: **resolvido** (para o mecanismo do fix; critério 3 do ROADMAP — `AUTO_LOAD` sem
gates — continua por medir, é o 11-04)

O fix candidato B, na sua forma corrigida (v2), remove **exactamente** o push do registo WAD
do walk exterior, de forma determinística em 3/3 corridas independentes, sem afectar os 175
pushes legítimos e sem regressão medida no resto do boot ou nas suites offline. A primeira
tentativa (fiel à letra do plano) foi implementada, testada em fixtures, aplicada à produção
em cópia, e **verificada in-boot como ineficaz** — não foi descartada em silêncio; fica
registada acima, com a causa raiz.

---

## O que esta sessão NÃO afirma

- **Não** afirma que a parede 4 está fechada per o critério 3 do ROADMAP (`AUTO_LOAD` ≥1 vez
  sem gates de diagnóstico, ≥4 de 6 corridas). Todas as corridas desta sessão correram **com**
  os gates 1-3 ON — a mesma dependência que E1/E3 já documentaram (o walk exterior só é
  alcançado hoje com esses gates). Isso é o `11-04`.
- **Não** re-executa nem invalida a contagem D-11.2 do `11-01`/E3 — essa contagem (n=1,
  medida no ponto de anexação com os dois filtros de flags) continua correcta; o que esta
  sessão corrige é ONDE e COMO intervir no walk para o caso contado, não a contagem em si.
- **Não** toca em `func_0041F700`/`func_0041F924` (push/pop) — o fix fica inteiramente dentro
  de `func_0041FF70`, antes de qualquer push acontecer.
- **Não** confirma a identidade exacta do objecto contado pela E3 (a ressalva de `piVar8+4`
  não lido directamente) — mas esta sessão faz uma leitura ADJACENTE (`gpr28+4`, que é o MESMO
  campo relativo à mesma base) que confirma, no MESMO ponto do código, que o valor é
  `0x40030001` — o que fecha, na prática, a pergunta que a ressalva deixava em aberto: o nó
  identificado pela contagem TEM a classificação patológica que se esperava.
- **Não** toca em pixels, shaders ou `SHADERSRC`.
- **Não** forja o resultado da ronda 1 como sucesso — está documentada como falha medida,
  com a causa raiz, antes da correcção.

## Próximo experimento para quem retomar

1. **`11-04`**: medir `AUTO_LOAD` com o fix v2 aplicado e os gates 1-3 **OFF** — o critério 3
   do ROADMAP, ainda não medido por esta sessão.
2. Se o walk exterior não for alcançável sem os gates (mesma barreira que E1/E3 já
   documentaram), o `11-04` precisa de medir e reportar essa dependência honestamente, não
   forçar os gates como fecho.
