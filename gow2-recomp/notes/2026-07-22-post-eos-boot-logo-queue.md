# Pós-EOS — fila HOSTRES logo empresa + caminho ao menu (2026-07-22)

## Meta

Ordem visual alvo: **legal → intro (SmLogo) → logos empresa → menu**.

## Diagnóstico (antes)

| Fase | Guest bind | Present host | FSM |
|------|------------|--------------|-----|
| Legal | `ea=0xC0FB0980` 1280×720 só | content-hold legal | — |
| Intro | (flip quase morre) | movie HLE decode | st620 0→1→3→11→0 |
| Pós-EOS | **mesmo** bind único | hold=0 → **preto** | WADs abrem |
| Menu | 0 binds novos | 0 | walk/A1/LDRSH = 0 |

HOSTRES já inflava `scep_e.ctxr` e `bluepoint.ctxr` (DXT1 1280×720, mesmo layout do legal) mas **só** `legalscreen720` ia para content-hold. O guest **nunca** muda o EA do bind; draws pós-movie (1/frame) + VP PSO fail → ecrã preto.

## Fix (HLE honesto de pixels reais)

Não forja shaders. Captura DXT1 HOSTRES no inflate e **sequencia**:

1. `legalscreen720` → hold imediato (`after_movie=0`)
2. SmLogo HLE (inalterado)
3. EOS → queue START `scep_e` → NEXT `bluepoint` (~`PS3_BOOT_LOGO_MS`, default 2500)
4. queue DONE → hold clear → guest (menu ainda natural)

### Env

| Var | Default | Efeito |
|-----|---------|--------|
| `PS3_BOOT_LOGO_QUEUE` | **on** (só `=0` desliga) | fila pós-movie |
| `PS3_BOOT_LOGO_MS` | 2500 | ms por logo |

### Código

- `runtime/ppu/host_res_inflate.c` — captura legal / scep_e / bluepoint
- `libs/video/rsx_host_content.c` — push / on_movie_end / tick
- `libs/video/rsx_metal_backend.m` — tick no present; clear hold; arm queue no EOS

## Prova in-boot (`/tmp/vdec_boot_logos.log`)

```
decoded DXT1 'legalscreen720.ctxr' … after_movie=0
boot logo push #1 legalscreen720
decoded DXT1 'scep_e.ctxr' … nonzero=15727 after_movie=1
decoded DXT1 'bluepoint.ctxr' … nonzero=15073 after_movie=1
… SmLogo 330 frames …
boot logo queue START 'scep_e.ctxr' (ms=2000)
movie end — boot logo queue armed
st620 3→11→0 + R_LglScA + R_PermA full
boot logo queue NEXT 'bluepoint.ctxr'
boot logo queue DONE — hold cleared (guest/menu)
```

Present modes: pré-movie `content=1`; pós-movie `content=1` durante logos; depois `hold=0`.

## Menu / A1

**Ainda aberto.** Fila de logos **não** invoca walk/ICGLdr. Após DONE o guest volta a draw 1/frame no mesmo bind — sem UI de menu até:

- natural: bind/draw texturas WAD + shaders de UI, ou  
- A1: caller do typemap walk (`00468C3C` ainda 0)

## Aceites

| Item | Estado |
|------|--------|
| Legal na tela | ✅ natural HOSTRES hold |
| Intro SmLogo | ✅ HLE present |
| Logos empresa (SCEA/Bluepoint pixels) | ✅ fila HOSTRES pós-EOS (in-boot log) |
| Menu | ⬜ próximo |

## Nota de honestidade

Logos pós-movie são **pixels reais do EBOOT** (HOSTRES), não assets inventados; a **ordem temporal** é host-driven porque o guest não rebinda EA nem entrega draws com textura nova. Desligar: `PS3_BOOT_LOGO_QUEUE=0`.
