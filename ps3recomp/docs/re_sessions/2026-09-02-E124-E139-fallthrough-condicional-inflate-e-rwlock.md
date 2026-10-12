# E124–E139 — três defeitos nossos por baixo da parede [G]: fallthrough condicional do lifter, layout do inflate host, giant lock no rwlock

> Continuação directa de [E110 §22](2026-09-02-E110-a-final-da-intro-console-vs-nos-e-o-filho-classe15.md): a parede era o
> callback do estado 25 (`func_00040560`, WAD_R_Hero) que no console retorna em 0,1 s e em nós ficava em spin em
> `func_002DAF04 → func_002DACCC`. Este ledger anda a corrupção para montante até ao **produtor**, corrige-o, e continua
> pelo caminho natural que se abre a seguir. Oráculo: RPCS3 (interpretador, GDB stub, **mudo e sem janela**).
> Todas as medições "nossas" são in-boot; as do console são bps/leituras no RPCS3 com o `EBOOT_skipintro.BIN`.

## §1 O lookup de material recebia uma chave inválida (E124)

O spin em `func_002DAF04` começa numa vcall `TABLE[8]->vt[0x50](mgr, key)`. Estático: `vt[0x50] = FUN_0039d3c4` é um
**lookup no typemap** — `product = typemap[*(mgr+0x44)][key->u16@+6] − 4` — não um lookup na pilha de escopos.

| lado | mgr (TABLE[8]) | key (r4) | key+6 | produto | produto+0x44 (ctx) | ctx+0x9c (arena) |
|---|---|---|---|---|---|---|
| console (bp 0x2DAF40/44) | 0x300D9958 | 0x31533D98 (w0 0x40000008) | 2 | 0x314FF5D8 (vt 0x516708) | 0x314FF2D8 (contentor do hero) | 0x314FE180 (C0DE1111) |
| nós (E124 `[LOOKUP50]`) | 0x400D9958 | **0x00512000** (uma vtable estática) | 58680 | **0** | 0 | 0xDEAD |

A chave é `**( *(obj+0xd0) + 8 )` — o primeiro descritor de material do vector `{vt 0x511f90, count 5, array, arena}`
pendurado em `+0xd0` do objecto classe 15 (MDLX). O lookup é vítima: recebe a chave errada.

## §2 O objecto classe 15 era `0x3F7FFFEC`: um float menos 0x14 (E104/E107 relidos + E125)

Em `func_0009EC0C` o chamador calcula `obj = *(container + 0xb8 + slot*4) − 0x14` com `container = *(entity+4)`.
Console (bp 0x9F0F0): `entity=0x315442B0 {vt 0x511D88, parent 0x307C7630, 0x3169A640, contentor 0x314FF2D8}` e
`parent+0xdc = 0x31540B34` (= MDLX+0x14). Nós (`[PRE283328]`): `r20=0x41573620` com **`*(r20+4) == r20`** (auto-ligado) e
`parent+0xdc = 0x3F800000` (1.0f). A entidade "não era um objecto" no momento do call.

## §3 A entidade nasce certa e é corrompida dentro de `FUN_0009ec0c` (E126/E127 + watch de store)

`[ENTALLOC]`: `TABLE[22]->vt[0x48]()` devolve **o mesmo** produto que o console (`0x414FF2D8`, w1 `0xC0160016`, arena
`C0DE1111`), `FUN_0009b964` devolve memória zerada, e `[9EC0C-ENTRY]` mostra a entidade **igual à do console** módulo a base
do heap (`0x511D88 407C76B0 4169A640 414FF2D8`). `PS3_WATCH_STORE=0x41573624` (entity+4) apanha **um** store de
`0x41573620` vindo de `func_0009EC0C+0x3C08` = `stw r25, 0x344(r31)` — no decompile `param_1[0xd1] = 0`, i.e.
**r31 devia ser a entidade e r25 devia ser 0**. Ambos os registos não-voláteis estavam errados.

## §4 Verificador de callee-saved: o culpado é uma cadeia que devolve com `r1 − 0x80` (E128)

Sonda genérica (`patch_e128_csave_check.py`): antes/depois de CADA call, compara r1 e r14..r31.

| nível | callee | efeito medido |
|---|---|---|
| `func_0009EC0C` | vcall `vt[0xc]` (= `0x9FE48`) | r1 −0x100, r25..r31 lixo |
| `func_0009FE48` | `func_0009E9F4` | r1 −0x100, regs intactos (restaurados de cópias lidas à entrada) |
| `func_0009EA60` | `func_00065ED8` e `func_00066508` | r1 −0x80 cada |
| `func_00065ED8` / `func_00066508` | `func_000626F0`, `func_0024D0F4` | r1 −0x80 (por chamada a `FUN_0024d0f4`) |
| `func_0024D0F4` | — | **`func_0024D144` acaba em `if (...) goto loc_0024D144;` sem trampolim** |

`FUN_0024d0f4` tem frame 0x80; o epílogo (`addi r1,r1,0x80`) está no fragmento `func_0024D1A0`. O fragmento do corpo
do loop, `func_0024D144`, termina no back-edge condicional e, quando o salto não é tomado, a função C devolve com
`g_trampoline_fn = NULL`: a cadeia `DRAIN_TRAMPOLINE` acaba e o controlo volta ao **caller do guest** sem pop da frame.

**Causa no lifter:** `tools/ppu_lifter.py:_last_line_is_terminator` contava `"goto " in t` como terminador incondicional —
um `if (...) goto` também. **População (scan do lift):** 44 fragmentos sem transferência incondicional no fim, 43 com o
sucessor na mesma função Ghidra, **31** da forma "cauda condicional" (corrigidos pelo `patch_e129_fragment_fallthrough.py`;
o resto são `return; /* b -> non-code */`, um `bl` final e uma sonda). É a mesma classe do fix `2550C8`.

**Fix:** lifter (`if (` nunca é terminador) + teste `test_conditional_goto_tail_is_not_a_terminator` + patch idempotente
para o lift em uso. **Aceite (E129):** 0 diferenças no verificador, guard `FREELIST-TAG-GUARD` 0×, `[LOOKUP50]` = console
valor a valor (`key 0x41533D98`, produto `0x414FF5D8`, ctx `0x414FF2D8`, arena `0x414FE180`).

## §5 Depois do fix: o loader segue a ordem do console até ao estado 22 (E130)

Sequência de estados do level loader (jump table `0x4234C`; console = bp no `bctr 0x42348`, nós = sonda nas labels):

| | sequência |
|---|---|
| console | 13 · 14×38 · **25 · 21 · 22×239** (≈5 s) · 16 · 17×5 · 23 · 24×28 · 3 · 4×87 |
| nós (E130) | 0 · 13 · 14×20 · **25 · 21 · 22×∞** |

O estado 22 fica em loop porque `FUN_0003f2b0 → FUN_002ba76c` reporta trabalho pendente: o WAD seguinte, **r_shella**
(o menu), falha em `[movieio] open: no free slot` — `MOVIE_IO_SLOTS=4` e `movie_io_close` **nunca tinha chamador**.

## §6 Os closes de WAD morriam no alocador de ops do FIOS (E131–E137)

Console: cada WAD é aberto (`FUN_0030d578`) e **fechado** (`FUN_0030d0ac` via `FUN_002b3f78`) antes do seguinte;
2 fh vivos no máximo, sempre reutilizados (`0x33018528/…580`). Nós: os closes são chamados com fh válido
(`[STREAM-RELEASE] fh=0x43018BD8`) mas **nunca chegam ao submit** (`[CLOSE-OPALLOC] op=0`): `FUN_00307c8c` devolve 0
porque **`*(sched+4) != 'FIOS'`** — o scheduler do FIOS (`0x43009270`) estava **a zeros** (`[SCHED]` w0..7 = 0).

> Retracção no próprio dia: a 1.ª versão do E131 ancorava a libertação do slot no dtor do fh (`func_0031F540`).
> Medido nos dois lados: o dtor **só corre no shutdown** do scheduler; o fh é reciclado pela execução do op 0xD.
> O E131 ficou ancorado no submit do op de close (`func_0030B058`). Mantido como diagnóstico; a causa real é o §7.

## §7 Quem zerava o scheduler: o nosso preenchedor host do inflate lia o layout errado (E138)

O watch de store não vê a escrita (é host directa). Cronologia: o zero aparece logo a seguir a
`[HOSTINFL] src=0x43D5C602 csz=137 dst=0x43008100 dmax=65536`. Anel da fila LFQ no console (`0x33008200`, 4 elementos):

```
300b6750 300b3d40 0000d5f0 00010000 330083ad 33008100 00010000 00000000
```

= `eaCompressed, eaUncompressed, compressedSize, outputSize, eaWorkToDoCounter, eaEventFlag,
hi16 eventFlagBits | lo16 skipBegin, hi16 skipEnd` (asserts de `edgezlib_inflate_queue.cpp` no ELF confirmam o
contador 4-aligned e `skipBegin + skipEnd ≤ expected`). O `lfq_host_inflate.c` lia **+0x14 (o event flag) como destino** e
**+0x18 (os bits, 0x10000) como tamanho** → 64 KB por cima do event flag, da fila (`+0x80`) e do scheduler (`+0x1170`).
E o "kind == 0x0C" era o `outputSize = 12` do header do psarc: só o 1.º elemento era servido; os blocos de 64 KB do
psarc eram rejeitados — **daí o dearchiver "rejeitar" e o F2B forçar os opens dos WADs**.

**Fix:** layout real; janela `[skipBegin, skipBegin+out)`; conclusão fiel (decremento do contador u32, `cellSpursEventFlagSet`);
o host **também faz o pop** do elemento (sem isso a fila de 12 enchia e o mediathread ficava preso em
`lfq_get_push_pointer`). **Aceite:** scheduler intacto, 0 `invalid mutex`, 0 `HOST-POP`, 34→44 inflates (o psarc real),
e **a intro passa a correr pelo dearchiver natural** (FSM `st620 1 → 10 → 11`, um caminho que nunca tínhamos visto).

## §8 Deadlock nosso no `sys_rwlock` (E139)

Com o FIOS vivo, a thread principal parava em `sys_rwlock_wlock` (`FUN_0030644c`, conclusão de op) enquanto a thread
"fios scheduler" — dona do rwlock — estava na mesma função sem poder correr (o giant lock ficava com a principal).
3 amostras `sample` em 20/40/60 s, idênticas. **Fix:** `sys_rwlock_rlock/wlock` largam o giant lock enquanto bloqueiam
(padrão do `sys_semaphore_wait`). **Aceite:** FSM `0 → 1 → 11 → 3 → 11` (dois filmes), 44 inflates, sem deadlock.

## §9 O que fica em aberto (medido, não corrigido)

- **2.º filme da intro em `st620=11`:** o `cellVdec` HLE descodifica a partir do ficheiro em cache (`popen`), não dos
  AUs que o guest entrega (`DecodeAu #1..N` de `0x43C5C580`); pelo caminho natural do psarc não há ficheiro em cache, logo
  não há `PICOUT` nem `SEQDONE` natural. É o plano **M9** (AUs → VideoToolbox → Metal). O `PS3_MOVIE_EOS` (hook em
  `obj+0x744`) continua a ser a única ponte — forcing declarado, ainda load-bearing.
- **Task SPU edgezlib vs. preenchedor host:** com a fila viva, o task SPU também corre (`[EDGEATT1]`, `[spu-dma] SKIP
  out-of-range GET ea=0x3300`, "indirect branch to unknown LS") — dois consumidores para a mesma fila. Falta decidir
  quem serve (task lifted ou host) e desligar o outro.
- **`[tty] suspicious len=65536`** logo após um inflate do header do psarc (43 linhas de 0x60): um printf do guest com um
  buffer de 64 KB; por caracterizar.
- **fh do FIOS não reciclados** antes do E138 (pool de ~30); com o E138 os closes voltam a submeter — confirmar reuso.
- O **`EBOOT.BIN` original** no RPCS3 tem 70 bytes (placeholder): a sequência da intro no console só é observável com o
  `EBOOT_skipintro.BIN`, que a salta.

## §10 Ferramentas que ficaram

`patch_e128_csave_check.py` (verificador de callee-saved por função, genérico), `patch_e130_loader_states.py`
(sequência de estados do loader), `patch_e129_fragment_fallthrough.py` (fix do lift em uso), sondas E124–E127/E132–E137,
e os scripts de oráculo em `$CLAUDE_JOB_DIR/tmp` (bp por endereço com leitura de campos; `select_main` após cada stop;
leituras de 4 bytes — `read_mem` longo devolve `E01`).

## §11 M9 a sério: o `cellVdec` passa a descodificar os AUs do guest (E140–E144)

`PS3_VDEC_DUMP_AU` (gated) mostrou o ES que o jogo entrega: AU #0 = sequence header `0x000001B3` 1280x720,
`frame_rate_code=5` (30 fps) + GOP + I-picture (19 401 B); AUs seguintes = uma picture (468–513 B; mais tarde
50–175 KB). Um `VTDecompressionSession` MPEG-2 (`kCVPixelFormatType_420YpCbCr8Planar`) devolveu 36/36 frames.
Implementado em `libs/codec/cellVdec_vt.m` + integração em `cellVdec.c` (fila host de AUs copiados no `DecodeAu`,
`PS3_VDEC_VT=0` desliga; o caminho antigo — ffmpeg sobre o ficheiro em cache — fica de fallback).

Três coisas que o player exigiu, medidas contra o console:
- **PTS**: o jogo entrega todos os AUs com `CELL_CODEC_PTS_INVALID`; o libvdec real interpola (`auPts` do item do
  console = 93000 = 31×3000). Interpolamos a partir do `frame_rate_code`.
- **PICOUT por imagem, não por levantamento**: no console a contagem de PICOUT (`obj+0x60c`) anda ~5 imagens à
  frente das levantadas; o player só pede a imagem N enquanto `N < contagem` (`0x2C0BD0`). Fila de imagens no HLE.
- **`CellVdecPicItem` real** (lido do item devolvido no RPCS3): `auPts[0]{hi,lo}` em +0x10, `auPts[1]` inválido em
  +0x18, `auDts` em +0x20/+0x28, `auUserData` em +0x30, `status` +0x40, `attr` +0x44, `picInfo` +0x48 → bloco
  `CellVdecMpeg2Info` em +0x50 (`hsize,vsize`, **+5 = frame_rate_code**, +0x12 = picture_coding_type). O player lê
  `picInfo+5` para a tabela de fps (`0x2C0C08`); o layout antigo dava fps = lixo. Os itens vivem agora em
  `CellVdecResource.memAddr` (4 slots), não na stack do caller. Aceite: `+0x77c = 30.00`.

## §12 O relógio do vídeo é o timebase corrigido pelo tempo do ÁUDIO — e o nosso áudio está em 0 (E145–E147)

`FUN_002c0508` (update do player): `elapsed = (mftb − início)/freq(sc 147)`; `audio = FUN_0045b780(obj+0x720)`
(tempo do stream de som, `+0x774`); se `|elapsed − audio| > tolerância` → `elapsed := audio`; `frame = elapsed × fps`;
levanta a imagem `r9` enquanto `r9 < PICOUT` e `r9 ≤ frame`.

| | console (t≈8 s) | nós |
|---|---|---|
| `r31/r29` (timebase, ticks) | 859 828 688 / 79 800 000 | 71 M / 79 800 000 (avança) |
| `+0x774` tempo do áudio | 0.63 → 0.87 s (avança 1:1) | **0.0** |
| stream de som (`FUN_0045b780`) | `+0x519=1`, estado 1, **samples `+0x154` 25 088 → 25 856**, 48 kHz | `+0x519=1`, estado 1, **samples 0**, 48 kHz |

O contador de samples é actualizado em `FUN_00461fe8` a partir de `FUN_0043f290(+0x1c4) → FUN_00453a68(voice)` = a
**posição da voz do Scream** (tabela por voz ×0x300, `+0x14`), que no console é escrita pelo mixer SPU. Nas nossas
corridas o mixer SPU "returned cleanly" mas a posição nunca avança → tempo do áudio 0 → frame 0 → o player fica na
1.ª imagem (com a EOS forçada desligada). **Próxima parede natural: a posição das vozes do Scream (mixer SPU / cellAudio).**

## §13 Ferramentas e retracções desta parte

Sondas E143–E147 (player, relógio, callback, stream de som); `PS3_VDEC_DUMP_AU`; `PS3_VDEC_STATS`.
Retracção: o "+0x60c preso em 1" (E145) era a janela da sonda (as 12 primeiras linhas), não o HLE — o E146 mostrou
26 PICOUT a chegar ao callback do guest e `+0x60c` a subir.
