## Decisão

`pista nao fechada`

A comparação comportamental entre 9 estados históricos do `ppu_lifter.py` (+ o baseline de
hoje) não isolou um commit:linha onde a regeneração do lift regride — pelo contrário,
**refuta com uma medição limpa e completa** a hipótese de "perda de descoberta de funções"
(por contagem) para toda a janela testada, e refuta que qualquer mudança do lifter dentro
da vizinhança de 3 níveis de `thr_auto_load` explique o problema. A investigação segue
para a Tarefa 2 (reversão cirúrgica TOCFIX/`ctx->lr`).

# Fase 6, Plano 06-02, Tarefa 1: comparação comportamental entre variantes do `ppu_lifter.py`

**Data:** 2026-07-30 · **Método:** A/B por commit histórico do `tools/ppu_lifter.py`
(`ps3recomp`), cada variante corrida contra o `EBOOT.ELF`/`functions.json` de hoje
(`gow2-recomp`), ~15s por variante, zero build de C++. `tools/ppu_lifter.py` do repo
**não foi alterado** — cópias em `/tmp/toolsab_diag06_run_<commit>/`.

## ERRATA que motivou esta mudança de alvo

O alvo original desta tarefa ("cruzar as 2855 funções perdidas reais contra os mecanismos
de descoberta") está **inválido** — ver
`notes/2026-07-30-CORRECCAO-as-2855-comparavam-dois-lifts-que-falham.md` (commit
`61d57dd`), descoberta pela própria tabela do `bisect_regression.sh` do Plano 06-01:
`boot_gow2.pre_v4` (o binário gerado pelo lift usado como referência do "que funciona")
é, na verdade, **REGRESSAO** (813 linhas, `StartSeq=0`). As 2855 comparavam dois lifts que
ambos falham. Esta tarefa não persegue esse número.

## PASSO 1 — baseline de hoje

```
python3 ../ps3recomp/tools/ppu_lifter.py EBOOT.ELF --functions functions.json \
        -o /tmp/lift_diag06_hoje -j 4
```

```
Boundary recovery: split 3 merged function(s) via prologue scan
Truncated-bounds repair: extended 180 function(s), 18984 byte(s) of code recovered
jump tables: 157 dispatchers, 1745 case targets, 1436 kept internal, +309 case funcs
switch tables: 157 from discovery, 0 from config, 0 overridden by config, 157 total
Generated 30929 mid-function tail-entry wrappers total
51991 functions lifted
```

Números FRESCOS de hoje (pós-commit `0585636`/`2e21639`, "o registo-base da jump table é
o que VALIDA ALVOS, não o primeiro que decodifica", RED→GREEN, suite 22/22). Os números
antigos do plano (137/1537/51917) estão desactualizados — **não usados** nesta comparação.

## PASSO 2 — 9 variantes históricas do `ppu_lifter.py`

Commits candidatos: os dois vigentes quando nasceram os binários conhecidos-OK
(`46a4c3f` para `boot_gow2.rdy0_ref`, `145fe58` para `boot_gow2.wip`), e os 7 seguintes
até ao estado mais próximo do `boot_gow2_v4` (conhecido-REGRESSAO): `1694fca`, `fbbecd6`,
`efefcfb`, `27c7657`, `1c30e12`, `34e5bab`, `4e815cf`.

| commit | data | correlação com binário conhecido | funções lifted | dispatchers | case targets | kept internal | mid-function total |
|---|---|---|---:|---:|---:|---:|---:|
| `46a4c3f` | 24 Jul 15:06 | vigente p/ `rdy0_ref` (**OK**) | 51726 | — (bug local: `jump-table discovery skipped: can only concatenate list (not "int") to list`, refutado como causa — resolvido no commit seguinte) | — | — | 30973 |
| `145fe58` | 25 Jul 14:46 | vigente p/ `wip` (**OK**) | 51839 | 137 | 1537 | 1299 | 30849 |
| `1694fca` | 25 Jul 19:00 | — | 51839 | 137 | 1537 | 1299 | 30849 |
| `fbbecd6` | 25 Jul 19:22 | — | 51839 | 137 | 1537 | 1299 | 30849 |
| `efefcfb` | 25 Jul 23:52 | — | 51839 | 137 | 1537 | 1299 | 30849 |
| `27c7657` | 26 Jul 00:06 | — | 51839 | 137 | 1537 | 1299 | 30849 |
| `1c30e12` | 26 Jul 02:31 | — | 51917 | 137 | 1537 | 1299 | 30927 |
| `34e5bab` | 26 Jul 02:47 | — | 51917 | 137 | 1537 | 1299 | 30927 |
| `4e815cf` | 26 Jul 02:59 | vigente perto de `boot_gow2_v4` (**REGRESSAO**) | 51917 | 137 | 1537 | 1299 | 30927 |
| `0585636`/`2e21639` | 30 Jul 12:39 (hoje) | vigente p/ `recomp_macos_v2` produção (**REGRESSAO**) | 51991 | 157 | 1745 | 1436 | 30929 |

**A contagem de funções lifted CRESCE monotonicamente ao longo de toda a cronologia**
(51726 → 51839 → 51917 → 51991), do estado conhecido-OK mais antigo até ao estado que
produz a `recomp_macos_v2` de produção que falha hoje. **Nunca cai.** Isto refuta, com uma
medição completa (não uma amostra), a hipótese de "perda de descoberta de funções" medida
por contagem, para toda a janela testada.

`efefcfb`/`27c7657` (jump tables no TOML + dedup de switch tables) dão exactamente os
mesmos números que `1694fca`/`fbbecd6` — confirma que são no-op sem `--config`
(`build_macos.sh` nunca passa `--config`), como já suspeitado na nota
`2026-07-30-duas-hipoteses-do-lifter-refutadas-por-ab.md`. `1c30e12` (truncated-bounds
repair) reproduz o resultado já medido por A/B nesta sessão: +78 funções, nunca remove —
REFUTADO como causa de perda.

## Vizinhança de `thr_auto_load` (`func_00147038`) — 3 níveis, byte-a-byte

Em vez de cruzar uma lista de "perdidas" (alvo inválido), comparei directamente o CÓDIGO
LIFTADO da vizinhança de chamada de `func_00147038` entre os estados extremos da janela
testada: `145fe58` (25 Jul, precursor do binário OK `boot_gow2.wip`), `4e815cf` (26 Jul
02:59, precursor mais próximo do binário REGRESSAO `boot_gow2_v4`), e `hoje` (30 Jul,
lifter que gera a `recomp_macos_v2` de produção, REGRESSAO).

Nível 0: `func_00147038` (thr_auto_load) — o corpo lifted, incluindo os 2 sítios TOCFIX
estáticos e os 8 sítios `ctx->lr = `, é **byte-a-byte idêntico** nos três estados (`diff`
vazio).

Nível 1 — os 6 callees directos (`func_00146C88`, `func_0036CF44`, `func_00373AF0`,
`func_0037750C`, `func_004B8FF8`, `func_004B9C58`): **todos byte-a-byte idênticos** nos
três estados.

Nível 2 — os 5 callees dos callees (`func_0036AA24`, `func_0036AB18`, `func_0037245C`,
`func_003724D8`, `func_0037401C`): **todos byte-a-byte idênticos** nos três estados.

**Resultado: as 12 funções da vizinhança de chamada de 3 níveis de `thr_auto_load` nunca
mudaram, em código C liftado, desde o estado que gerou o binário OK mais recente
(`145fe58`, precursor de `boot_gow2.wip`) até ao estado que gera a produção que falha
hoje.** Isto refuta que qualquer mudança do `ppu_lifter.py` nesta vizinhança explique a
regressão — o TOCFIX e o `ctx->lr` explícito já lá estavam (desde o commit `11a1c3c`,
21 Jul, ANTES até de `rdy0_ref` 24 Jul) quando o boot ainda funcionava, e continuam
idênticos agora que não funciona.

## O que isto elimina, com medição completa (não amostra)

- "Perda de descoberta de funções", medida por contagem total: **REFUTADA** para toda a
  janela 25-30 Jul — a contagem só cresce.
- Qualquer mudança do `ppu_lifter.py` na tradução da vizinhança de 3 níveis de
  `thr_auto_load`: **REFUTADA** — código idêntico do binário OK mais recente até à
  produção actual que falha.
- TOCFIX/`ctx->lr` como algo **introduzido dentro da janela da regressão**: **REFUTADO**
  — já existiam, sem alteração, em `145fe58` (precursor de um binário que funciona).

## O que isto NÃO elimina

- TOCFIX/`ctx->lr`, presentes desde 21 Jul e inalterados, ainda podem ser a causa **em
  combinação com algo fora desta vizinhança de 3 níveis** (outra função corrompida altera
  estado partilhado que `thr_auto_load` lê mais tarde) — só um teste dinâmico (reversão +
  binário real) decide isto, e é exactamente a Tarefa 2.
- Uma mudança fora desta vizinhança de 3 níveis (noutra função qualquer, no preâmbulo dos
  chunks, no `build_macos.sh`, num `patch_*.py`, ou no runtime/`ppu_loader.cpp`/HLE) que
  não foi testada por esta comparação estática.
- A análise é 100% estática (diff de código C gerado). Nenhum binário foi construído nesta
  tarefa — a confirmação dinâmica fica para a Tarefa 2.
