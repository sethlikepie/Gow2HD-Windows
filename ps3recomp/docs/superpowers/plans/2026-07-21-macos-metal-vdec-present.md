# Mac Metal — cellVdec frames → present (M9)

> **For agentic workers:** `subagent-driven-development` ou `executing-plans`.  
> **Depende de:** M1 (e idealmente M7); caminho guest **Open+StartSeq** (plano intro-vdec).  
> **Não substitui** WAD; mostra frames do **decoder HLE** quando o guest pede output.

**Goal:** Quando `cellVdec` no POSIX decodifica `.m2v` para YUV/RGBA (já há path ffmpeg/host no `cellVdec.c`), **apresentar** esses frames no Metal (blit fullscreen ou textura amostrada), para o utilizador ver vídeo pelo **caminho natural** (não só overlay `PS3_MOVIE_HLE`).

**Architecture:**

```
cellVdec decode thread → frame YUV420P/RGBA (host)
        → opcional: cópia guest buffer (já existe)
        → rsx_metal_vdec_frame / movie_frame path
        → present
```

Preferir reutilizar **`rsx_metal_movie_frame`** (M0/M1) com um flag `source=vdec|hle_overlay` para não duplicar blit. Zero-copy depois (M4) se o decode der CVPixelBuffer.

**Tech Stack:** `libs/codec/cellVdec.c`, Metal present; **sem** exigir shaders guest.

## Global Constraints

- Overlay HLE (M0) e vdec present podem coexistir: documentar prioridade (vdec guest > overlay se ambos).
- handle=0 Open ainda é bug guest/HLE — M9 assume frames a existir; se Open falhar, smoke RED honesto.
- Política Apple: Metal; evitar segundo pipeline OpenGL.

---

### Task 1–3: Implementado 2026-07-21

- [x] YUV420P→BGRA + `rsx_metal_movie_frame_stage` após decode
- [x] HLE priority (`hle_source` / `PS3_MOVIE_HLE` defer)
- [x] DEMO autostart multi-frame; guest Open still wall (documented)

---

## Aceite M9

| ID | Critério |
|----|----------|
| C1 | Com Open/StartSeq e decode a produzir frames: ≥1 present vdec-metal |
| C2 | Sem Open: smoke falha com motivo vdec, não crash Metal |
| C3 | M0 overlay ainda funciona isolado |

## Fora de escopo

HW decode dentro do cellVdec (pode continuar software/ffmpeg na lib); sync A/V perfeito; WAD.
