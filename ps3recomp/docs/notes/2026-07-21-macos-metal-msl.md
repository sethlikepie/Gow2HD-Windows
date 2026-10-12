# M2 — FP/VP → MSL + Metal PSO cache (2026-07-21)

## Decisão de emissão

| Campo | Valor |
|-------|--------|
| **MSL_EMIT** | **A** (sem SPIRV-Cross) |
| Mecanismo | `rsx_fp_decompile` / `rsx_vp_decompile` (HLSL dialect) → conversão mecânica → MSL |
| Entries | `fs_main` / `vs_main` |
| Fallback | PSO fixo M1 se decompile/compile falhar |

## API

- `rsx_fp_decompile_msl(ucode, max_bytes, out, out_size)`
- `rsx_vp_decompile_msl(ucode, max_bytes, out, out_size)`
- Backend: `metal_set_shader` — hash FNV-1a `(fp, vp)` → cache até 32 PSOs
- Counters: `PS3_TRACE_RSX_SHADERS` no shutdown (`[RSX-SH metal] hits=…`)

## Aceite medido

| ID | Resultado |
|----|-----------|
| S1 | **in-boot** `compiled PSO (ea=0x47F80000, fp=1 instr, hash=0x9ECDEF7F, passthrough)` com `PS3_SHADER_DEMO=1` + `PS3_RSX_BACKEND=metal` |
| S1 offline | `/tmp/test_msl` — MOV fixture → Metal `newLibraryWithSource` + `fs_main`; TEX fixture → PSO link com passthrough VS |
| S2 | Fallback `current_shader=NULL` → `s_draw.pso` M1; logs em fail |
| S3 | Sem CRC bypass |
| S4 | Path HLSL D3D12 intacto (`rsx_fp_decompile` / `rsx_vp_decompile` inalterados no corpo) |

## Smoke

```bash
# offline
cc -std=c11 -Iinclude -x objective-c libs/video/tests/test_msl_decompiler.c \
   libs/video/rsx_fp_decompiler.c -framework Metal -framework Foundation \
   -DTEST_MSL_METAL_COMPILE=1 -o /tmp/test_msl && /tmp/test_msl

# in-boot (fixture inject — NÃO é conteúdo do jogo)
# PID-kill; PS3_SHADER_DEMO só diagnóstico
cd ../gow2-recomp
( set -a; . ./env_gow2.sh; set +a
  export PS3_RSX_BACKEND=metal PS3_SHADER_DEMO=1 PS3_TRACE_RSX_SHADERS=1
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/m2_smoke.log 2>&1 & BPID=$!
sleep 22; kill -TERM $BPID; sleep 1; kill -9 $BPID 2>/dev/null
grep 'compiled PSO' /tmp/m2_smoke.log
```

## Limitações (M3+)

- Texturas dummy 1×1 brancas em `texture(0..15)` até `bind_texture` Metal
- Pack/bit ops raros podem falhar compile Metal → fallback M1
- VP const bank zeroed (`buffer(1)`); upload de constantes RSX é follow-up
- `PS3_SHADER_DEMO` **não** conta como pixels de conteúdo do jogo
