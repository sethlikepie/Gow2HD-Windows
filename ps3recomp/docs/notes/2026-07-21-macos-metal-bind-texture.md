# M3 — bind_texture + deswizzle Metal (2026-07-21)

## Implementação

| Peça | Detalhe |
|------|---------|
| `metal_bind_texture` | vtable `rsx_backend.bind_texture` |
| Formatos MVP | A8R8G8B8/D8R8G8B8 → `BGRA8Unorm` (BE bswap); DXT1/3/5 → BC1/2/3; R5G6B5/A1R5G5B5/A4R4G4B4 expand→BGRA; B8/G8B8 |
| Deswizzle | `rsx_deswizzle_texels` se LN clear e não-BC |
| Slots | 16× `MetalTexSlot`; present bind real ou dummy 1×1 |
| Sampler | linear + clamp (shared dummy_samp) |
| Smoke sintético | `PS3_METAL_TEX_DEMO=1` → checkerboard 64×64 unit0 + triângulo UV |

## Aceite

| ID | Resultado |
|----|-----------|
| T1 | `bind_texture != NULL`; path guest loga `[RSX metal] bind_texture(...)` quando FIFO marca dirty |
| T2 | **in-boot sintético** `PS3_METAL_TEX_DEMO=1` + `PS3_FRAME_DUMP=1`: frame dump com magenta+cyan (≠ clear) — 2026-07-21 |
| T3 | RGBA path (BGRA upload) medido no TEX_DEMO; DXT1 mapeado BC1 (upload guest quando bind real) |
| T4 | D3D12 intacto |

## Smoke

```bash
cd ../gow2-recomp
( set -a; . ./env_gow2.sh; set +a
  export PS3_RSX_BACKEND=metal PS3_METAL_TEX_DEMO=1 PS3_FRAME_DUMP=1 PS3_NOMOVIES=1
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/m3_smoke.log 2>&1 & BPID=$!
sleep 8; kill -TERM $BPID; sleep 1; kill -9 $BPID 2>/dev/null
grep TEX_DEMO /tmp/m3_smoke.log
# frame_*.bmp no CWD — apagar localmente; NÃO commitar
```

## Notas

- Conteúdo WAD real (0x85 Morton, DXT1 legal screen) promove T3 a “jogo”; M3 GREEN com sintético documentado.
- Follow-up: sampler por `tex->filter` / wrap modes (M5-ish); upload de consts VP (M2 leftover).
