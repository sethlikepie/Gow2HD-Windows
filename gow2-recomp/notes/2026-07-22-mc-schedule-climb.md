# Micro-ctor schedule climb (opção 1) — 2026-07-22

## Pergunta

Quem deveria chamar a família que chega a `00468C3C` / walk, e por que **nunca entra** no boot natural?

## Grafo (estático + OPD)

```
[OPD 0x00534CB8 → code 0x0032854C]   ← ÚNICO entry root da família
        │
        ├─(CR GT)──► func_00328B08 ──► … ──► 00329490 ──► 00468C3C ──► …
        ├─(CR LT)──► func_00328978 ──► …
        └─(else)  ──► body / 00328860 / 00328E50 / …

func_0032854C: ZERO callers directos no lift (só def + tabela ppu_recomp_030).
OPD em EBOOT: file+0x524CB8, toc=0x541178. Refs absolutas ao EA 0x534CB8: **0**.
```

Ascendentes de fan-in (já censo):
`00328B08/978/F40` ← `0032854C` / trampolins internos; micro-ctor `00329490` ← factories
`0032862C/678/…` — todos **abaixo** do root OPD.

`GroupStartCtor` (`func_00294004`, `[TYMAP-GS]`) **corre 1×** via `func_002B133C` e
instala o typemap (H3). **Não** chama `0032854C` — path ortogonal.

## Probes

Script: `recomp_mid_v2/patch_mc_schedule_probe.py`  
Tags: `[MC-32854C]`, `[MC-328B08]`, `[MC-328978]`, `[MC-328B18]`, `[MC-329490]`,
`[MC-328860]`, `[MC-328E50]` — gated `PS3_TRACE_A1CHAIN` / `PS3_TRACE_MC`.  
`ppu_loader` `[OPD-ICG]` alargado a code `0x32854C` / OPD `0x534CB8` (+ lr).

## Boot natural (50 s, R_Perm full, probes ON)

| Tag | N |
|-----|--:|
| MC-32854C … MC-329490 (todos) | **0** |
| OPD-ICG (incl. 32854C/534CB8) | **0** |
| A1-468C3C | **0** |
| TYMAP-DUMP (vt+8 walk OPD) | **1** (vivo) |
| R_PermA full | **1** |

**Veredito:** a parede sobe **acima** de `0032854C`. O schedule de micro-ctor de
cena **nunca é despachado** (nem por `ps3_call_opd` observável nesta janela).
Não é um branch morto *dentro* da família — a família **não arranca**.

## Implicação

| Hipótese | Estado |
|----------|--------|
| Bug no meio 0032854C→00468C3C | **Refutada** (entry root = 0) |
| OPD 0x534CB8 nunca chamado | **Confirmada** |
| Typemap falta | **Refutada** (DUMP ok) |
| Fase de jogo ainda pré-cena (H1) | **Viva** — falta o *scheduler* que carrega OPD factory |

## Próximo discriminador

1. **Quem materializa/chama OPD `0x00534CB8`** (ou índice na tabela de OPDs
   `0x534C38…`) — procurar load dinâmico (base+index), registo de factory type,
   ou `bctr` com slot vtable de um “Scene/Component allocator”.
2. Correlacionar com FSM pós-logo / `GFX_SCREEN_*` / load de level após
   `st620→0` + pad.
3. **Não** forjar call a `0032854C` como aceite.

## Recipe

```bash
export PS3_TRACE_A1CHAIN=1   # ou PS3_TRACE_MC=1
export PS3_TRACE_TYMAP_DUMP=1
# sem PS3_GATE_FORCE
./boot_gow2 EBOOT.ELF
grep -E 'MC-|OPD-ICG|TYMAP-DUMP' /tmp/log
```

Log: `/tmp/vdec_mc_sched.log`.
