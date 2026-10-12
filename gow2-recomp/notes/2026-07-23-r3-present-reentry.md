# R3 — Present re-entry pós-WAD (SetFlip_after_R_Perm) (2026-07-23)

**Tipo:** discriminadores novos + probe MAINLOOP (sem fix de reentrada).  
**Gate A continua RED.** **Gate B GREEN.**  
**Não claim:** menu / SetFlip_after_R_Perm≥1 / theater.

Prior: R1 (flip morre no open Lgl **antes** ICALL) · R2 (H3 HEAP40 **DEAD**).

---

## Smoke matrix

| Run | Env | SetFlip_after_R_Perm | Gate B | Notas |
|-----|-----|---------------------:|--------|-------|
| `/tmp/r3_timeout120.log` | `TIMEOUT=120` `PS3_NO_RSX=1` | **0** | GREEN | flip total 25593; thr+R_Perm ok |
| `/tmp/r3_mainloop.log` | `TIMEOUT=90` `PS3_TRACE_MAINLOOP=1` | **0** | GREEN | flip total 31405; watermark Lgl |

Recipe: `./rodar_gow2_menu_fast.sh` + `python3 count_menu_gate.py`.

---

## Timeline zoom (±WAD)

Ordem estável (mainloop run; números de linha /tmp/r3_mainloop.log):

| # | Evento |
|--:|--------|
| … | `SetFlipCommand` triplo buffer 0/1/2 (path único, ver §MAINLOOP) |
| 34152 | **último flip do boot** = flip#31405 |
| 34153 | `[MAINLOOP] NOTE /wad/r_lglsca.wad_ps3 last_flip=#31405` |
| 34152+ | `movieio open R_LglScA` |
| 34185 | NOTE `R_PermA` **ainda** last_flip=#31405 (0 flips durante stream) |
| 34197 | R_Perm **full** 20169344 |
| 34202+ | ICALL-BAD ×12 (`0x40637488`) — **depois** do corte de flip |
| 34557 | TYPE15 CLOSE-PRESERVE nodes=12 |
| 34603–09 | thr_auto_load start/end |
| resto | st620 0→0; CC9D0 f4=0; **zero** SetFlip / Pad |

Baseline m3 (sem TRACE): mesmo corte — último SetFlip imediatamente antes de `movieio open R_LglScA`.

---

## Classes A / B / C (brief R3)

| Classe | Hipótese | Veredito | Prova nova (além R1/R2) |
|--------|----------|----------|-------------------------|
| **A** | Movie/logo present acaba e o guest **nunca re-arma** o loop de present do menu pós-load | **PRIMARY — ALIVE** | Watermark: `last_flip` no open Lgl = total final; path único `g={17ADE,17ADF}` some e **não reaparece** pós thr |
| **B** | Estado RSX/GCM partido de modo que SetFlip seria chamado mas não chega | **DEAD** | Guest **deixa de chamar** `SetFlipCommand` (printf some); handler/mode intactos; 31k flips OK pré-WAD |
| **C** | Main stuck **antes** de qualquer present | **REFINED / fraco** | Main **não** está stuck pré-WAD (milhares de flips); pós-WAD está **vivo** em CC9D0 TYPE15 f4=0 — present não re-agendado |

### Discriminadores novos (R3)

| # | Disc | Resultado |
|---|------|-----------|
| **D1** | Content-hold / `BOOT_LOGO_MS` prende present? | **DEAD.** `boot logo queue DONE` ×2; **≥1.2k flips após último DONE** (m3 L4420→5759); hold cleared **antes** do corte no open WAD |
| **D2** | `SetFlipHandler` / `SetFlipMode` limpos no WAD? | **DEAD.** Handler `opd=0x00521B68` **1×** cedo; Mode **1×** cedo; **0** re-sets; **0** SetVBlankHandler no boot inteiro |
| **D3** | Guest espera `GetFlipStatus` e trava? | **DEAD.** `GetFlipStatus` count = **0** no boot inteiro |
| **D4** | Flip “atrasado” (>90s) pós thr? | **DEAD.** `TIMEOUT=120` → SetFlip_after_R_Perm=**0** |
| **D5** | Quem emite flip pré-WAD e reaparece? | **Probe** `PS3_TRACE_MAINLOOP=1`: **único** fingerprint guest `g={0x00017ADE,0x00017ADF}` em **todos** os flips (×31405); EA ∈ `func_00017ACC` `[0x17ACC,0x17AF4)`; `lr=0` (tail/trampoline). No open Lgl: `last_flip=#31405` = total. **Zero** MAINLOOP/SetFlip pós R_Perm |

R1/R2 permanece: ICALL-BAD **não** causa o corte no open Lgl; HEAP40 skip **não** restaura flip.

---

## Root class (mais afiado que M3)

**Classe primária:** transição de modo **intro-present → asset-load** no `FIOS HOST-POP` / `movieio open R_LglScA` **encerra** o único path de flip medido (`func_00017ACC` band). Isso **não** é bug de content-hold, nem handler GCM limpo, nem RSX mudo.

**Bloqueio duro de reentrada (Gate A):** após R_Perm full + thr, o guest **nunca re-agenda** esse path de flip (nem pad). O main fica no spin TYPE15/CC9D0 com **f4=f54=0** (lista preservada M2, product incompleto). O open WAD é o **início do load** (corte esperado de present); a falha de Gate A é **falta de re-arm do frame/present pós-load**, não “flip que devia continuar durante o open”.

Co-sinais (não resolvidos aqui): ICALL freelist em `0x400C3D88` (H1 R2); st620 0→0; pad GetData=0.

---

## Probe shipped (ps3recomp, default OFF)

| Artefacto | Função |
|-----------|--------|
| `libs/video/cellGcmSys.c` | `PS3_TRACE_MAINLOOP=1`: sample flip# + guest stack EAs + hist lr; watermark |
| `cellGcmMainloopNote(why)` | dump last flip no `movieio open` |
| `libs/video/movie_hle.c` | chama note em todo open (no-op se env OFF) |

---

## Aceite R3

| Critério | Status |
|----------|--------|
| SetFlip_after_R_Perm ≥ 1 | **FAIL** (0) |
| Gate B green | **PASS** |
| Sem theater / intro-only claim | **PASS** |
| Root class mais afiada + disc novos | **PASS** (DONE_WITH_CONCERNS) |
| Próximo gate único | **PASS** — ver abaixo |

### Próximo gate único (R4 / fix futuro)

**Quem deveria re-chamar o path `func_00017ACC` (callers estáticos `func_0001E1A8` / `1E34C` / `1EAD8` / `25064` / `25614` / `1E3DC`…) depois de thr**, e que estado (TYPE15 f4 writer / menu FSM / scene arm) o desbloqueia — **sem** forjar flip HLE e **sem** HEAP40 default.

Não promover `PS3_TRACE_MAINLOOP` a default.

---

## Artefactos

- Logs: `/tmp/r3_timeout120.log`, `/tmp/r3_mainloop.log`
- Código: `ps3recomp/libs/video/cellGcmSys.{c,h}`, `movie_hle.c`
- Prior: `notes/2026-07-23-r1-icall-bad-present.md`, `r2-icall-flip-disc.md`, `m3-postb71-flip.md`
- Plano: `ps3recomp/docs/superpowers/plans/2026-07-23-m3plus-postwad-present.md` Task R3
