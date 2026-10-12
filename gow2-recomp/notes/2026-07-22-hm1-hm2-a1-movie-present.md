# H-M1 / H-M2 / A1 — movie HLE → present, legal sticky, walk

Data: 2026-07-22. Branch motor: `macos-arm64-port-f0-f2`.
Logs: `/tmp/vdec_hm1_final2.log`, `/tmp/vdec_hm1_decpresent.log`, `/tmp/vdec_a1walk.log`.
Dumps de prova: `frame_movie_1..9.bmp` (não versionados; regeneráveis).

## Resumo executivo

| ID | Pergunta | Veredito |
|----|----------|----------|
| **H-M1** | Por que não há frames de vídeo/logo? | **Causa raiz achada + fix.** Guest GCM flip quase para durante o HLE (~50 flips / ~1s) → com present só no guest só se via o fade preto. Decode passa a ser dono do `nextDrawable` durante o movie. Overlay Metal funciona (staging maxR→255; dumps `frame_movie_*.bmp` com logo). |
| **H-M2** | `PS3_CONTENT_HOLD_AFTER_MOVIE=1` legal sticky? | **SIM.** Após EOS: `movie_stop` + `[CONTENT] re-arm hold` + `present_mode movie=0 content=1` com legal (ctr≈249,249,249). |
| **A1/walk** | Menu/cena com assets WAD? | **Ainda 0.** Typemap object vivo (H3); `00468C3C` / walk / LDRSH **não entram**. WADs abrem; SHADERSRC (quando HOSTRES ligado) N>0 sem walk. |

---

## H-M1 — movie HLE → present

### Sintoma
- Decode: 330 frames SmLogo_v2.m2v (AVAssetReader BGRA).
- CPU probe: `stage #60 max=255`, `cv_src #61 max=250` (decode **não** é preto).
- Frame dumps durante “movie=1”: max=0 (preto puro).
- Legal content-hold (mesmo PSO fullscreen) **não** era preto.

### Hipóteses testadas

| # | Hipótese | Resultado |
|---|----------|-----------|
| 1 | Decode AVFoundation produz frames pretos | **Refutada** — stage/cv_src mid-logo nonblack |
| 2 | Dual `nextDrawable` (decode+guest) pinta preto | **Parcial** — real com dump pesado; não explica guest-only |
| 3 | CVMetalTexture zero-copy inválido após `CFRelease(sample)` | **Provável no path CV** — default passou a CPU BGRA→MTLTexture; ZC só com `PS3_MOVIE_CV_ZEROCOPY=1` |
| 4 | Overlay PSO/UV quebrado | **Refutada** — stamp vermelho 64² aparece no dump; legal usa o mesmo PSO |
| 5 | Guest flip morre durante HLE → guest-only só vê fade | **Confirmada** — ~53 presents, `frames_rgba` só ~28 (ainda fade) |

### Fix (runtime Metal)

1. **`movie_vt_metal.m`**: `rsx_metal_movie_stop()` no fim da thread (EOS) — liberta overlay para content hold.
2. **`alwaysCopiesSampleData = YES`** — evita IOSurface reciclado.
3. **Default present path = CPU BGRA** (`PS3_MOVIE_CV_ZEROCOPY=1` para o antigo iosurface).
4. **Default `PRESENT_ON_GUEST=0`**: decode apresenta cada frame; guest **não** chama `nextDrawable` enquanto HLE movie está activo (`ud != NULL` skip).
5. Diagnóstico gated: `guest_present` maxR, `RICH_FRAME` dump one-shot com `PS3_TRACE_TIMELINE` / `PS3_FRAME_DUMP`.

### Prova in-boot (pós-fix)

```
PRESENT_ON_GUEST=0 (default=0 decode-presents)
present_path=rgba-cpu
guest_present #41 frames=40 stage_nb~3596 maxR=255
RICH_FRAME force dump frames=29 maxR=48
RICH_FRAME2 force dump frames=37 maxR=230
frame_movie_1.bmp  max=48  ctr=(42,39,22)   # fade-in logo
frame_movie_9.bmp  max=234 ctr=(86,24,1)    # logo (≠ legal 249,249,249)
decode end frames=330
movie stop frames_rgba=330
```

Aceite H-M1: **pixels de logo no drawable** (dump) correlacionados com staging nonblack — **provado in-boot**.

---

## H-M2 — `PS3_CONTENT_HOLD_AFTER_MOVIE=1`

### Antes
- EOS setava flag guest, mas `s_movie.active` ficava 1 (último CV/staging) → `movie_on=1` para sempre, content hold nunca voltava.

### Fix
- Thread VT chama `rsx_metal_movie_stop()` → `playback_end` + clear active.
- Com `PS3_CONTENT_HOLD_AFTER_MOVIE=1`: `[CONTENT] re-arm hold after movie 1280x720`.

### Prova
```
[CONTENT] re-arm hold after movie 1280x720
[RSX metal] movie stop (frames_cv=0 frames_rgba=330 ...)
present_mode movie=0 content=1 hold=1 paused=0
# dumps pós-EOS: ctr=(249,249,249) legal
```

Aceite H-M2: **legal sticky pós-movie** — **provado in-boot**.

---

## A1 / walk (menu/cena + WAD)

### Estado (inalterado nesta leva)
| Sinal | Valor |
|-------|-------|
| R_LglScA / R_PermA open+stream | OK |
| HOSTRES → SHADERSRC ΣN | 889 (quando HOSTRES Mac linkado; ver nota H2) |
| TYMAP-DUMP vt+8 = walk OPD | vivo (`0x522E70`) |
| `[A1-468C3C]` / walk / LDRSH / ICGLdr natural | **0** |

Conclusão A1: alvo “menu/cena com assets WAD” **ainda não exercita** a cadeia `00468C3C→walk`. H3 (object built, never walked) mantém-se. Próximo discriminador: progressão FSM/pad além do sticky legal, ou RE do caller natural do walk (WADLD-FIN / outro path).

---

## Env de smoke (Mac)

```bash
. ./env_gow2.sh
export PS3_RSX_BACKEND=metal
export PS3_TRACE_TIMELINE=1          # present_mode + guest_present maxR
export PS3_CONTENT_HOLD_AFTER_MOVIE=1
export PS3_MOVIE_EOS=1
# opcional prova visual em disco:
# export PS3_FRAME_DUMP=1 PS3_FRAME_DUMP_STRIDE=30
# NÃO usar PRESENT_ON_GUEST=1 como default (só ~1s de fade)
./boot_gow2 EBOOT.ELF
```

## Ficheiros tocados (motor)

- `libs/video/movie_vt_metal.m` — stop-on-EOS; `alwaysCopiesSampleData=YES`
- `libs/video/rsx_metal_backend.m` — present ownership, CPU default, re-arm, diags

## Não-aceites / limites

- Zero-copy CV ainda **não** é default (opt-in `PS3_MOVIE_CV_ZEROCOPY=1`).
- Com `PS3_FRAME_DUMP=1` agressivo o GPU contende com VT decode — usar stride alto ou RICH one-shot.
- A1 walk **não** foi desbloqueado por H-M1/H-M2.


## Correcção pós-regressão visual (legal → vídeo → legal)

Utilizador reportou: legal, depois intro completa, depois **legal de novo**.

Causa: `playback_end` default logava "hold stays off" mas **não** punha
`s_content.hold=0`. Com `movie.active=0` o present voltava a `content_on=1`.

Fix (commit seguinte no motor): após HLE real (`was_hle`), default
`hold=0`. Sticky só com `PS3_CONTENT_HOLD_AFTER_MOVIE=1`. Stop prematuro
do `movie_vt_start` (sem begin) **não** limpa o legal pré-movie.

Smoke: POST present `content=1 count=0`, `hold=0` em f=1100+.
