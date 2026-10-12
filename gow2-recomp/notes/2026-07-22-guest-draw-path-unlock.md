# Guest draw path unlock (menu pipeline) — 2026-07-22

## Objectivo

Pós-hold (legal→intro→logos), o path guest deve pintar (não só HOSTRES overlay).

## Bugs encontrados e corrigidos (ps3recomp)

| # | Sintoma | Causa | Fix |
|---|---------|-------|-----|
| 1 | draw count=1 inútil | 0x1820 tratado como DRAW (é IDX_FMT); real é **0x1824** val=`0x03000000` → count=4 | `rsx_commands.h/.c` method map |
| 2 | verts (0,0,0) | offset main `0x80912C40` lido sem IO→EA | `gcm_io_offset_to_ea` + `metal_vtx_ea` |
| 3 | UV zero no FS | attr8 não no BasicVertex; passthrough tc0=0 | pack UV em attr1; seed v[8]; passthrough tc0=col.xy |
| 4 | FS sempre preto | decompile `return r[0]` sem TEX body | fallback FS `tex0.sample(tc0)` |
| 5 | draws=0 após hold | FIFO só em label poll | `rsx_process_fifo` no `SetFlipCommand` |
| 6 | drawable preto c/ draws | RT 720p + blit retina falhava | draw directo no drawable se size mismatch |

## Evidência in-boot

- Pré-fix: `DRAW_INDEX val=0x0` count=1; pós: `count=4`, idx=[0,1,2,3], NDC ±1, UV 0..1.
- Pós-hold: `guest_present draws=1` (antes 0); frames com max=255 (não pure black).
- nb≈0.013 pós-hold ainda **não** é legal cheio (0.13) nem menu — sparse; próximo discriminador.

## Recipe

```bash
export PS3_RSX_BACKEND=metal PS3_RSX_FIFO=1
export PS3_METAL_VP_OFF=1   # opcional: força passthrough VS
export PS3_FRAME_DUMP=1 PS3_FRAME_DUMP_STRIDE=40
export PS3_BOOT_LOGO_MS=400
./boot_gow2 EBOOT.ELF
# pós DONE: guest_present draws≥1; frames não pure black
```

## Ainda aberto (menu)

1. Conteúdo guest pós-hold ainda sparse (≠ legal denso) — textura/fase correcta?
2. Registry CRC / walk A1 / OPD 534CB8 morto — UI FlashUI ainda Invalid combination.
3. Pad autostart já default; progressão FSM menu continua o alvo.
4. Fix estrutural do decompiler FP (TEX body) em vez do fallback.

## Commits

- ps3recomp: `fix(rsx/metal): draw path real — VB_INDEX_BATCH…`
