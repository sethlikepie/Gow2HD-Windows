# Recipes

A recipe is a JSON list of capture steps consumed by `probe.py --recipe`.
Ops: `snapshot` (run N seconds, interrupt, capture), `breakpoint` (Z0, continue,
capture on each hit), `mem` (raw dump + BE u32 view). Keys prefixed `_` are
comments and ignored by the runner.

| Recipe | Target | Question it answers |
|---|---|---|
| `gow2_wall_2B2660.json` | GoW2 HD (NPUA80491) | Does `func_002B2660` return on real hardware semantics, and what writes the flag at `0x008691A8` that lets the poll loop at `0x002B2E2C` exit? |

## The GoW2 wall, statically derived

```
002B2E1C  lwz  r31, -0x1488(r2)     ; r2 = TOC 0x00541178 -> slot 0x0053FCF0 -> r31 = 0x008691A8
002B2E20  lwz  r0,  0x0(r31)
002B2E28  bne  cr7, 0x2B2E3C        ; flag != 0 -> leave the loop
002B2E2C  bl   0x2B2DD0             ; the tick
002B2E30  lwz  r0,  0x0(r31)
002B2E38  beq  cr7, 0x2B2E2C        ; still 0 -> tick again
```

`func_002B2DD0` (the tick) calls `func_002B2660`, then `0x000B951C`, then
`0x002B7188`, and returns via `blr` at `0x002B2E00`. The flag at `0x008691A8`
lives in segment 1's `.bss` (file bytes end `0x576EA4`, memsz ends `0x9B9808`),
so it is zero at load: the loop is *designed* to spin until something else
writes it. The open question is which of the three callees does that write, and
under what condition -- which is what `wall-returned` and `poll-loop-exit`
discriminate.

## GoW2 — receita que funciona (E90, 2026-09-01)

O E81 concluiu que o oráculo «estava mais preso do que nós»; era só a falta dos dados do jogo
(ELF solto → `/app_home` sem `gow2.psarc` → `_sys_process_get_paramsfo` falha). Receita medida:

1. `python3 tools/rpcs3_probe/make_skipintro_elf.py EBOOT.ELF <rpcs3>/dev_hdd0/game/NPUA80491/USRDIR/EBOOT_skipintro.BIN`
   (PARAM.SFO e `gow2.psarc` ao lado; o `nop` em `0x000CE0FC` salta a espera da intro).
2. `config.yml`: `PPU Decoder: Interpreter (static)` — com LLVM `ppu_breakpoint` devolve falso.
3. O stub só despacha `vCont;c`/`vCont;s` (`rsp.py` já envia isso); seleccionar a thread principal
   com `Hg 0000000001000000` antes de `g`; amostrar com cont → sleep → `0x03`.
4. `oracle_scopes.py` (breakpoints + estado dos gestores/loader), `oracle_sample.py`/`oracle_flip.py`
   (amostragem), `oracle_records.py` (sequência de registos do despachante de WAD). Uma ligação por corrida.
