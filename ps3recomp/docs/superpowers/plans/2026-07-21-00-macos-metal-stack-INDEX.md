# Índice — stack Metal nativo (macOS/arm64)

> **Política:** em todo port Apple, preferir APIs de **baixo nível e nativas**
> (Metal, VideoToolbox, AudioQueue/AudioUnit, Grand Central Dispatch só quando
> medido) em vez de camadas portáteis lentas (ffmpeg CLI, software blit
> desnecessário, OpenGL, reencodes Theora/VP9). Ver `Claude.md` § «Port Apple».
>
> **Não substitui** a cadeia intro/vdec/WAD (`2026-07-21-intro-vdec-open-force-wad.md`).
> Metal fecha o **último elo host** (present/draw); o funil guest continua separado.

## Inventário do gap

**Backlog completo (vtable + extras Win):**  
[`2026-07-21-00-macos-metal-pending-backlog.md`](2026-07-21-00-macos-metal-pending-backlog.md)

## Ordem de execução (stack visual Mac)

| Ordem | Plano | Depende de | Estado | Aceite resumido |
|------:|-------|------------|--------|-----------------|
| **M0** | [`2026-07-21-macos-videotoolbox-metal-overlay.md`](2026-07-21-macos-videotoolbox-metal-overlay.md) | Janela Metal/SDL | ✅ **GREEN** 2026-07-21 | Logo VT+Metal in-boot 330f+EOS; bridge metal; sem ffmpeg |
| **M1** | [`2026-07-21-macos-metal-rsx-draw-path.md`](2026-07-21-macos-metal-rsx-draw-path.md) | Metal init | ✅ **GREEN** 2026-07-21 | draw path PSO + demo triangle multi-color dump |
| **M2** | [`2026-07-21-macos-metal-shader-msl.md`](2026-07-21-macos-metal-shader-msl.md) | M1 | ✅ **GREEN** 2026-07-21 | FP/VP → MSL → PSO cache in-boot |
| **M3** | [`2026-07-21-macos-metal-bind-texture.md`](2026-07-21-macos-metal-bind-texture.md) | M1; WAD guest | ✅ **GREEN** 2026-07-21 | bind_texture + deswizzle + TEX_DEMO dump |
| **M4** | [`2026-07-21-macos-metal-iosurface-zerocopy.md`](2026-07-21-macos-metal-iosurface-zerocopy.md) | M0 GREEN | ✅ **GREEN** 2026-07-21 | VT→IOSurface/CV zero-copy |
| **M5** | [`2026-07-21-macos-metal-pipeline-state.md`](2026-07-21-macos-metal-pipeline-state.md) | M1 | ✅ **GREEN** 2026-07-21 | blend/depth/scissor/mask hooks + apply |
| **M6** | [`2026-07-21-macos-metal-vertex-attribs.md`](2026-07-21-macos-metal-vertex-attribs.md) | M1 | ✅ **GREEN** 2026-07-21 | attrib snapshot + multi-type upload |
| **M7** | [`2026-07-21-macos-metal-render-targets.md`](2026-07-21-macos-metal-render-targets.md) | M1; M5 rec. | ✅ **GREEN** 2026-07-21 | RT cache + draw offscreen + blit |
| **M8** | [`2026-07-21-macos-metal-content-hold.md`](2026-07-21-macos-metal-content-hold.md) | present M0/M1 | ✅ **GREEN** 2026-07-21 | content hold + movie pause |
| **M9** | [`2026-07-21-macos-metal-vdec-present.md`](2026-07-21-macos-metal-vdec-present.md) | M1; Open vdec | ✅ **GREEN** 2026-07-21 | YUV→BGRA stage; DEMO multi-frame |
| **M10** | [`2026-07-21-macos-metal-default-ops.md`](2026-07-21-macos-metal-default-ops.md) | M0/M1 build | ✅ **GREEN** 2026-07-21 | metal default + matriz smoke |

## Paralelo vs série

- **M0 ∥ M1** — overlay e draw path mínimo (só partilham present).
- **M5 ∥ M6** após M1; **M2/M3** após M1; **M7** após M1 (+M5 ideal).
- **M4** após M0 GREEN; **M8** após present estável; **M9** após draws + vdec Open.
- **M10** cedo (defaults) sem esperar M9.

## Relação com a cadeia GoW2

```
Intro guest (vdec/WAD) ──► pixels CONTEÚDO ──► precisa M1+M2+M3 no Mac
        │
        └── overlay HLE (M0/M4) ──► ver logo + EOS; NÃO abre WAD
```

| Wall guest | Plano guest | Backend Mac que “mostra” |
|-----------|-------------|---------------------------|
| Open/FORCE/WAD | `2026-07-21-intro-vdec-open-force-wad.md` | M1+ (quando draws existirem) |
| Shaders/pixels | `2026-07-20-05-rsx-shaders-pixels.md` | M2+M3 |
| Intro visual imediata | — | **M0** |

## Regras comuns a todos os planos M*

1. **Metal nativo** como caminho default Mac (`PS3_RSX_BACKEND=metal`); SDL null só headless/debug.
2. **Proibido** como “solução Apple”: depender de `ffmpeg` no PATH; reencode Theora/VP9 do cache do jogo; OpenGL.
3. **Preferir:** VideoToolbox (decode), Metal (GPU), AudioQueue/AudioUnit (áudio), `MTLSharedEvent`/locks finos em vez de spin.
4. Commits PT, paths explícitos, sem dados de jogo, PID-kill nos smokes.
5. Windows D3D12 **não regredir**; partilhar contratos (`rsx_backend` vtable) quando possível.
6. Aceite sempre: **in-boot medido** vs offline unit vs não-exercitado.

## Próximo passo prático

1. Executar **M0** (VT overlay) se o objectivo for “ver o logo”.
2. Em paralelo ou a seguir: **M1** se o objectivo for “Mac deixa de ser clear-only no RSX”.
3. Não bloquear WAD/vdec à espera de M2/M3 — são eixos ortogonais.
