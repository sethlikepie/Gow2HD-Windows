# Prioridade 1 — timeline legal → pós-EOS (Metal) (2026-07-22)

## Recipe

```bash
unset PS3_NO_RSX
export PS3_RSX_BACKEND=metal PS3_RSX_FIFO=1
export PS3_FRAME_DUMP=1 PS3_FRAME_DUMP_STRIDE=10
export PS3_TRACE_TIMELINE=1 PS3_PERF_FSM=1 PS3_TRACE_HOSTRES=1
# sem GATE_FORCE / METAL_DEMO_DRAW
./boot_gow2 EBOOT.ELF   # até R_Perm full + ~20s
```

Log: `/tmp/vdec_p1_timeline.log`  
Frames: `frame_*.bmp` (stride 10, ~150 ficheiros, **não commitar**).

Instrumentação: `PS3_TRACE_TIMELINE` em `rsx_metal_backend.m` → tags `[TL] f=<n> …`.

## Timeline correlacionada

| Frame (approx) | Evento log | Conteúdo visual |
|---------------:|------------|-----------------|
| 0 | `content_hold 1280x720` (legalscreen720.ctxr DXT1) | — (pré-present) |
| 1–8 | `set_shader` ea=0x003FFxxx, `bind` **único** ea=0xC0FB0980 1280×720 fmt=0x86, `draw_indexed prim=6 count=1` | legal WARNING |
| 10–920 | present ~1 draw/frame; **mesma** textura 1280×720 | **LEGAL** (frac nonblack ≈0.124) |
| **926** | `movie_end hold=1` → `movie_begin` + pause hold | transição |
| 926 | st620 0→1→3→11 (overlay done) →0 | intro movie FSM |
| 930–1470 | present continua (~1 draw/frame) | **PRETO** (nonblack=0.000) |
| ~após 926 FSM | F2B open R_LglScA + R_PermA full | ecrã **ainda preto** |

### FSM (st620)

```
… → 0 (f≈0)
f=926: 0→1 → 3 → 11 (EOS arm) → 0
pós-WAD: st620 permanece 0 (overlay_done=1)
```

### RSX stats (janela medida)

| Sinal | Valor |
|-------|-------|
| binds TL | 80 — **todos** 1280×720 fmt=0x86 ea=**0xC0FB0980** |
| set_shader TL | 64 (cap) — FPs guest `0x003FF6xx–0x003FFDxx` (scratch local, 1–6 instr) |
| draws | ~1 por present após f=2 (`total_draws ≈ frame_count`) |
| texturas WAD/menu | **0** binds distintos |
| logo / menu / gameplay | **não observados** nos frames |

## Classificação visual (amostras)

| Sample | Class |
|--------|-------|
| f10, f50, f100, f200, f400, f600, f800, f900, f920 | **LEGAL** (epilepsy WARNING + © SCEA) |
| f930, f950, f1000, f1100, f1200, f1400, f1470 | **PRETO** |

Não há SmLogo, loading ring, main menu, nem cena de jogo na janela.

## Veredito P1

1. **Pixels úteis = só HOSTRES content-hold (legal screen)** desenhado como fullscreen textured quad.
2. **Não há draws de “jogo”** (menu/cena): após `movie_end` o hold desliga e o ecrã fica preto **apesar** de:
   - draws RSX continuarem (1/frame),
   - R_PermA full + WAD expand,
   - set_shader/bind a repetir o mesmo padrão de suporte.
3. O bind único `0xC0FB0980` é o buffer da textura legal (não texturas WAD).
4. Pós-EOS o pipeline **não materializa** frames de movie HLE no present (ou o movie path não alimenta o drawable) — ecrã preto em vez de vídeo/logo.
5. **Não é aceite [D] de conteúdo de cena**; é aceite de “legal via HOSTRES+Metal” + diagnóstico de parede **pós-legal**.

## Implicação (próximo trabalho)

| Hipótese pós-legal | Experimento |
|--------------------|-------------|
| H-M1: movie HLE não empata frames no present | TL em `rsx_metal_movie_frame*` + contagem CV/movie vs present |
| H-M2: hold deveria rearmar (`PS3_CONTENT_HOLD_AFTER_MOVIE`) | smoke com env=1 — legal sticky até menu? |
| H-M3: menu exige ICGLdr/walk (A1) + assets WAD | retomar nested walk / SHADERSRC de cena |
| H-M4: draws pretos = clear sem textura útil | dump RT clear color + se bind falha pós-movie |

**Recomendado a seguir:** H-M1 (movie path → present) em paralelo com A1 se o alvo for menu/cena.

## Código

- `ps3recomp` `rsx_metal_backend.m`: `[TL]` + `PS3_FRAME_DUMP_STRIDE`
