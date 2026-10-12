# Metal smoke pós-CRC hit (2026-07-22)

## Recipe

```bash
unset PS3_NO_RSX
export PS3_RSX_BACKEND=metal PS3_RSX_FIFO=1 PS3_FRAME_DUMP=1
export PS3_TRACE_CRCLK=1 PS3_TRACE_HOSTRES=1
# sem GATE_FORCE, sem METAL_DEMO_DRAW
./boot_gow2 EBOOT.ELF   # ~35s até R_Perm full + 15s
```

Log: `/tmp/vdec_metal.log`. Frames: `frame_*.bmp` (cwd, gitignored / não commitar).

## Contagens

| Sinal | N |
|-------|--:|
| `Invalid shader combination` ERROR | **0** |
| CRC-LK hit / miss | **≥400 / 0** |
| `[RSX metal]` | 99 |
| `set_shader` (log cap) | 20 |
| `draw_indexed` | 20 |
| `bind_texture` + upload | 8+ |
| HOSTRES inflate | 23/23 |
| `frame_*.bmp` | **560** |

## `res=0x01010000`

Sempre o valor em `shader+0x10` para Default.ps3fx com
`TEXTURE=1;CONSTCOLOR=0|1;…`. Não é EA de microcódigo (faixa guest).

Leitura operacional (não RE formal do layout do nó do mapa):

- byte alto `0x01` / seguinte `0x01` alinham com flags TEXTURE/CONSTCOLOR=1
  no path observado; resto zero.
- O lookup **aceita** a combination (não cai no ERROR Invalid; o log de
  `time to load/compile` para Default agora imprime a string **válida**
  `TEXTURE=1;CONSTCOLOR=1;HASCOLOR=0;TRANSFORM=0`).
- FlashUI ainda mostra **uma** linha `Unknown combination string` no path
  de compile (não o ERROR spam Default) — residual.

## Metal path (in-boot)

- Device **Apple M5**, window 1280×720, frame dump on.
- `set_shader` com FP real (`fp_addr=0x003FFD80`…), decompile FP OK (`fp=1
  instr`), **VP MSL falha** (`float3` vs `float4`) → retry **passthrough VS**.
- `bind_texture` 1280×720 fmt=0x86 (legal/content).
- Draws: `draw_indexed prim=6 count=1` (quads).

## Pixels

| Frame | Conteúdo (observado) |
|-------|----------------------|
| ~50 | **Legal WARNING screen** (epilepsy + © SCEA) — conteúdo HOSTRES real |
| ~400 | ecrã preto (fase pós-hold / flip sem draw útil) |

Isto é o primeiro **pixel de asset do jogo** no Mac path natural com
HOSTRES+CRC (content hold + Metal present), **não** `PS3_SHADER_DEMO`.

## O que **não** está vencido

- [D] registry ICGLdr / walk natural (A1) — ainda 0.
- VP decompile Metal (passthrough).
- Gameplay / menu pixels além do legal.
- FlashUI unknown combination residual.

## Próximo

1. ~~Fix MSL VP float3/float4~~ → `notes/2026-07-22-metal-vp-msl-fix.md` (VS/FS fail=0, VP real).
2. Correlacionar FP `0x003FFxxx` com defs CFX (hashes).
3. Nested walk / ICGLdr (A1) em paralelo se o alvo for shaders de cena WAD.
