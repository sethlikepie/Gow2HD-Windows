# E90 — o oráculo destravado, e o fix de raiz da parede [F]

**Data:** 2026-09-01 · **Antecessor:** [E88/E89](2026-09-01-E88-o-desequilibrio-e-por-construcao-push-itera-os-filhos-do-momento.md)
· Nível: **in-boot** (nosso runtime, 3 corridas ON + 1 OFF de controlo) + **oráculo RPCS3** (16 corridas, breakpoints no interpretador).

> Supera o §6 do E88 («se o código é o mesmo, o console tem o mesmo −1; só o oráculo o diz»).
> O oráculo disse: **não tem**. A diferença é nossa — o preenchimento host do ring do WAD (F2B).

## 1. Destravar o oráculo (o que estava errado no E81)

| Obstáculo | Causa medida | Solução (em `tools/rpcs3_probe/`) |
|---|---|---|
| RPCS3 «preso na intro» (E81) | o jogo abre `/dev_hdd0/game/<TITLE_ID>/USRDIR/gow2.psarc`; com o ELF solto o `_sys_process_get_paramsfo` falha e o path vira `/dev_hdd0/game/pp_home/...` → `fios open archive '/gow2.psarc' failed`. **O oráculo nunca teve os dados do jogo** | arrancar por `dev_hdd0/game/NPUA80491/USRDIR/EBOOT_skipintro.BIN` (PARAM.SFO ao lado) |
| Laço da intro espera `*0x592668 == 4` | o RPCS3 não passa de 1–2 (vdec) | `make_skipintro_elf.py`: `bne` em `0x000CE0FC` → `nop` (mesma saída do laço que o skip do jogo) |
| `c` não retoma a emulação | o stub só despacha `vCont;c`/`vCont;s` (Emu/GDB.cpp) | `rsp.py`: `cont()`/`step()` enviam `vCont` |
| breakpoints nunca disparam | `ppu_breakpoint` devolve falso com o recompilador LLVM | `config.yml`: `PPU Decoder: Interpreter (static)` |
| registos da thread errada | o stub reporta uma thread qualquer parada | `Hg 0000000001000000` antes de cada `g` |
| amostragem impossível (E80 §6b) | o 0x03 só é lido dentro de `wait_with_interrupts`, que só corre depois de um `vCont` | `oracle_sample.py`/`oracle_flip.py`: cont → sleep → 0x03 → S05 |

Com isto o oráculo chega ao **#054 do B71** em ~30 s de emulação (`oracle_scopes.py`, corridas 7/10/11).

## 2. O que o oráculo mostrou (corridas 11–16)

1. **Mesmo arranque que o nosso**: conjunto B a nível 0 por `func_002B6288` (×5), R_LglScA (`push22 [] cur −1`, `push1 [] cur −1`, `pop1 [12]`…) e R_PermA com o **mesmo** padrão de push-all/pop-all que o E88 mediu em nós — a «assimetria por construção» do E88 §2 **existe também no console** e é inócua.
2. **Sequência de registos do R_PermA idêntica** à nossa durante 3458 registos após o 2.º `0x13` (`oracle_records.py` vs `[DESTROY]` do E88): os bytes são os mesmos.
3. **A divergência é o fim do stream.** No oráculo o pedido do R_PermA é UM só (`req=20169344`, servido pelo jogo em chunks de `0x20000` via `FUN_002b9f00`→`FUN_002b3d1c`) e termina em `06 0B 0C 0D 0E 0F 10` com `avail=0 rem=0` → `FUN_002ba76c` devolve 0 e o #038 segue para `FUN_000415f0`/`FUN_00041014`/`FUN_00041494` (pilhas em `oracle_r12.json`). **Um único `0x06`** (o da raiz A, `pcls=1`).
4. Em nós, depois do `10`: `[FIOSOPEN] F2B-STREAM-ALIGN residual from=20054688 -> 20038352` — o bloco de `patch_f2ba9bc_stream_align_install.py` **rebobina o ficheiro**, o leitor lê lixo (`w0=0xF46F3520`), re-processa registos antigos e encontra um **segundo `0x06`** que despeja o R_PermA (pop-all → conjunto A a −1). É exactamente o `pop1` extra da linha 9694 do E88.
5. Antes do R_PermA, nós processávamos ainda **96 registos-fantasma de tipo 0** (`00x96`, ring vazio lido como cabeçalhos) que o oráculo não tem.

## 3. A causa, em nós

O ring do WAD (`type_sys+0x1A8`) é alimentado no console pelos areads em chunk do próprio jogo. Em Julho (parede do DecodeAu) esse caminho «encravava» e foi substituído por um **preenchimento host** (`f2b_stream_fill`/`ensure`/`pre_consume` + `F2B-STREAM-PUMP` + `EOF-DONE/QUIET` + `ALIGN/RESYNC`): enche 256 KB de cada vez, ignora o tamanho do pedido (`+0x74`/`+0x1ac`) e, no fim, rebobina. Entretanto o encravamento de raiz foi resolvido (fallthrough `func_002550C8`, freelist) e o `AREAD-HLE` em `func_002B3D1C` serve chunks exactos — o preenchimento host ficou a **quebrar o fim do stream**.

## 4. O fix (E90) — predições escritas antes dos logs (`PREDICOES.md` do job)

`patch_e90_f2b_natural_gate.py` (gow2-recomp): `f2b_stream_fill/ensure/pre_consume/eof_try_complete` e o bloco ALIGN/CLAMP passam a no-op; `F2B-STREAM-PUMP` passa a default OFF. O caminho natural (`FUN_002b9f00` → `002B3D1C` → `ps3_fios_aread_hle`, chunks de 131072) alimenta o ring. `PS3_F2B_HOST_FILL=1` repõe o legado só para A/B.

| | P1 sem 2.º `06`/`F46F`/ALIGN | P2 chunks por AREAD-HLE | P3 conjunto A ≥ 0, VAZIO=0 | P4 frame N | sequência = oráculo |
|---|---|---|---|---|---|
| ON #1 | ✔ (ALIGN=0, 1 `handler-POP`) | ✔ (`n=131072 got=131072`, fd do R_PermA) | ✔ (#042–#053 correm; VAZIO/ICALL-BAD=0; FPRWRAP só `sai-cedo`) | ✔ **frame=10000** | ✔ idêntica até ao fim (`10`) |
| ON #2 | ✔ | ✔ | ✔ | ✔ **frame=10500** | — |
| ON #3 | ✔ | ✔ | ✔ | ✔ **frame=10500** | — |
| OFF (controlo) | ✘ ALIGN=1, 2 `handler-POP` | — | ✘ VAZIO=14 | ✘ frame=1 | (= E88) |

Intro não regride (ON #1: `st620 0→1→3→11`, sticky 1). O estado da aplicação continua «preso em 0x0052F3F0» — isso é a parede **[G]**, a seguinte.

## 5. Default invertido (E91)

Build `boot_gow2_e91` com o caminho natural **por default** (sem env) e o pump OFF:

| corrida | env | frame | ALIGN | `handler-POP` 0x06 | VAZIO/ICALL-BAD | st620 máx |
|---|---|---|---|---|---|---|
| default #1 | — | **10 000** | 0 | 1 | 0 | 11 |
| default #2 | — | **10 500** | 0 | 1 | 0 | 11 |
| legado | `PS3_F2B_HOST_FILL=1 PS3_FIOS_STREAM_PUMP=1` | 1 | 1 | 2 | 14 | 11 |

O legado reproduz o E88 ao byte; o default é o fix. Scripts: `patch_e90_f2b_natural_gate.py` (novo, gow2-recomp) e
`patch_fios_f2b_open_block_install.py` (pump default OFF); dependências declaradas em `games/gow2/lift_baseline/PATCH_DEPS.tsv`,
catálogo regenerado.

## 6. O que fica retirado / superado

- E88 §6 («o console tem o mesmo −1») — **superado**: o console termina o #038 com o conjunto A a 0 e o R_PermA como nível 0 (`oracle_r7.json`, tabela no #054).
- E82–E85 «fix de raiz depende do oráculo» — o oráculo respondeu; a causa era nossa (`suspeitar de nós mesmos primeiro`).
- `PS3_E86_KEEP_SCOPE`, `PS3_SKIP_NULLTYPE`, `PS3_E89_LGLSCA_ATTACHED`: diagnósticos, ficam OFF.
