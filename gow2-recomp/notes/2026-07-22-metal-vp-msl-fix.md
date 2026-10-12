# Fix MSL VP/FP decompile (2026-07-22)

## Sintoma (antes)

```
[RSX metal] VS library failed: ... assigning to 'float3' from 'const constant float4'
[RSX metal] set_shader: VP PSO failed, retry passthrough
```

## Causas

1. **Writemask parcial** (`o[i].xyz = vp_c[n]`) — LHS float3, RHS float4.
2. **FP** mesmo padrão (`r[4].yzw = float4 + float4`).
3. **Constantes NaN** impressas como `nan` (identificador inválido no Metal).
4. **Splat DP3** 4× da expressão const → linha truncada / parênteses abertos.
5. **SampleGrad** reescrito como `sample(s, uv, ddx, ddy)` em vez de
   `sample(s, uv, gradient2d(ddx, ddy))`.

## Fix (ps3recomp)

| Ficheiro | Mudança |
|----------|---------|
| `rsx_vp_decompiler.c` | `emit_masked_assign` — RHS casa com mask; splat escalar |
| `rsx_fp_decompiler.c` | idem + `asfloat(0x..)` + temp `_s` + SampleGrad→gradient2d |
| `rsx_metal_backend.m` | dump FS falho se `PS3_TRACE_METAL_SHADER` |

## Smoke (Metal, R_Perm full + 12s)

| Sinal | Antes | Depois |
|-------|------:|-------:|
| VS library failed | 5 | **0** |
| FS library failed | 5 | **0** |
| VP PSO failed / passthrough | 5 | **0** |
| compiled PSO com `VP` | 0 (só passthrough) | **10** |
| Invalid combination | 0 | 0 |
| frames dump | ~500 | **740** |

Log: `/tmp/vdec_vpfix5.log`.
