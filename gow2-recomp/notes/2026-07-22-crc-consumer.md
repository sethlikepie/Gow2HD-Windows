# Consumer CRC / combination vs SHADERSRC (2026-07-22)

## Pergunta

As 889 defs materializadas por HOSTRES+SHADERSRC alimentam o lookup
`USE_PRECALCULATED_SHADER_CRCS` (`func_001655F0`), ou continuam órfãs?

## Cadeia (confirmada in-boot)

```
sprintf TEXTURE=…;…     (func_001F2AD4)
   → func_001655F0
        root = **(TOC-0x3B10)     → 0x4306ADF0   (= ICG component H3)
        tbl  = *(root+0xCC)       → 0x4306B3A0
        → 001643F8(tbl, shader, str)
             → 001DEC38(str)      CRC32
             → 00163088 map lookup
             → store res @ shader+0x10
```

## Medição (`/tmp/vdec_crclk.log`, `/tmp/vdec_crclk2.log`)

| Sinal | Valor |
|-------|------:|
| HOSTRES | 23 inflates (gowshader.cfx…) |
| SS-DONE2 | 18, Σi=889 |
| **CRC-LK** | **≥400, hit=1 always, miss=0** |
| **Invalid shader combination** | **0** (janela ≥ R_Perm full + 12s flips) |
| res @ sh+0x10 | sempre `0x01010000` (Default.ps3fx) |
| root | `0x4306ADF0` |
| tbl | `0x4306B3A0` (perto typemap `0x4306B160`) |

Snips observados:

- `TEXTURE=1;CONSTCOLOR=1;HASCOLOR=0;TRANSFORM=0` (maioria)
- `TEXTURE=1;CONSTCOLOR=0;HASCOLOR=0;TRANSFORM=0`

CRC exemplo: `0x3BDF6D6D` para a string CONSTCOLOR=1.

## Contraste com pre-HOSTRES

| Boot | SHADERSRC ΣN | Invalid (ordem ~3k linhas) |
|------|-------------:|---------------------------:|
| `vdec_a1chain` (sem HOSTRES) | 0 | **459** |
| `vdec_crclk2` (HOSTRES) | 889 | **0** |

## Veredito

1. **Sim, o consumer usa o registry sob o componente ICG** — lookup corre
   centenas de vezes por frame e **encontra entrada** (`miss=0`).
2. **HOSTRES+SHADERSRC N>0 é condição necessária** para a tabela de
   precalc deixar de estar vazia no path medido (Invalid some).
3. **SHADERSRC objs** (`0x4306D284`…) e **CRC tbl** (`0x4306B3A0`) são
   irmãos no mesmo root `0x4306ADF0`, não o mesmo objecto — mas o pipeline
   de init que popula um também deixa o mapa de combinations utilizável.
4. `res=0x01010000` **não** parece ponteiro de microcódigo; é um valor
   compacto/sentinel de “combination known”. O spam Invalid cessa, mas
   **não** prova pixels/RSX com shaders de conteúdo (ainda `PS3_NO_RSX` /
   Default.ps3fx). Não celebrar [D] completo.
5. Walk/ICGLdr/A1 (`00171244` / `0032109C`) **continua 0** — path WAD nested
   à parte; este aceite é **defs HOSTRES + CRC precalc**.

## Probes

`recomp_mid_v2/patch_crc_lookup_probe.py` — `[CRC-LK]` / `[CRC-HASH]`,
gated `PS3_TRACE_CRCLK`.

## Próximo

- ~~Interpretar `0x01010000` + Metal~~ → `notes/2026-07-22-metal-after-crc.md`
  (Invalid=0, legal WARNING nos frames, VP passthrough).
- Interpretar `0x01010000` (flags? índice?) e o que o set_shader consome a
  jusante.
- Boot com RSX/Metal: Default.ps3fx deixa de ser ERROR? draw real?
- Nested walk/ICGLdr para conteúdo WAD (A1) permanece em paralelo.
