# SHADERSRC loop pós-N (2026-07-22)

## Pergunta

Com HOSTRES a entregar ΣN=889 (`notes/2026-07-22-h2-hostres-shadersrc.md`), o
loop de `func_003CC208` **processa e materializa** records, ou N é só lido e
descartado?

## Resultado (in-boot, `/tmp/vdec_ssloop2.log`)

| Tag | N | Significado |
|-----|--:|------------|
| `[SHADERSRC]` | 18 | entry com N>0 |
| `[SS-CAP]` | 16+ | branch “capacity < N” (não é hard-fail) |
| `[SS-GROW]` | **18** | `func_003CC3F4` aloca N×0x34 e actualiza obj+4/8/C |
| `[SS-RESUME]` | **18** | retoma em fragmento `func_003CC2B8` (lift split, ficheiro 005) |
| `[SS-REC2]` | 48 (cap) | keys lidas do stream (ex. `0xF6B18FDA`, `0x81B6BF4C`) |
| `[SS-DONE2]` | **18** | **i==N em todos** (incl. i=776 no gowshader) |
| `[SS-INS]` / `[SS-INS2]` / `[SS-INSFN]` | **0** | `func_003C8578` nunca |
| Σ i (DONE2) | **889** | = Σ N |

## Fluxo real

```
003CC208  read N
   │
   ├─ N > capacity  →  003CC3F4 GROW (alloc + set base)
   │                      └→ trampoline 003CC2B8  (fragmento split!)
   │
003CC2B8  loop i=0..N-1:
   stream key → 003CBB98 / 003CB2E8 / 003CA760
   path A: 003C3598 (escreve slot 0x34) + i++     ← caminho medido
   path B: 003C8578 insert                         ← 0×
   i==N → DONE
```

**Lifter note:** o corpo do loop pós-grow **não** fica em `001` com o entry;
fica em `ppu_recomp_005.cpp` (`func_003CC2B8`). Probes só no entry dão falso
“loop morto” (primeira medição SS-CAP sem RESUME).

## Veredito

1. **N>0 não é cosmético** — grow + loop completo, **889 records materializados**
   nas tabelas guest (base em obj+0x4, stride 0x34).
2. O path de insert `003C8578` **não** é o usado no boot natural; o fill vai por
   `003C3598` (cópia/slot).
3. Continua **separado** do typemap walk / `[LDRSH]` (ainda 0) — isto é o
   pipeline de **defs CFX/HOSTRES**, não ICGLdr nested.
4. Aceite parcial de fonte: defs no guest. **Não** é aceite de [D] (registry
   ICGLdr / pixels de jogo via walk).

## Patch

`recomp_mid_v2/patch_shadersrc_loop.py` — probes em 001/003/005, gated
`PS3_TRACE_SHADERSRC`.

## Próximo

- ~~Consumer / Invalid~~ → **respondido** em `notes/2026-07-22-crc-consumer.md`:
  CRC lookup `miss=0`, **Invalid=0** com HOSTRES; root ICG partilha árvore.
- Interpretar `res=0x01010000` e path set_shader / Metal.
- Walk/A1 permanece wall à parte para ICGLdr WAD path.
