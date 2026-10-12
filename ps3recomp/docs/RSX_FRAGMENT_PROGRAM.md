# RSX Fragment Program (NV40 ISA) → HLSL

Reference + status for the RSX fragment-program decompiler
(`libs/video/rsx_fp_decompiler.{h,c}`). The RSX fragment shader ISA is the
NVIDIA NV40 ("nvfx") fragment program encoding. Authoritative source for the
bit layout: Mesa/nouveau `src/gallium/drivers/nouveau/nv30/nvfx_shader.h`.

## Instruction format

Each instruction is **4× 32-bit words (16 bytes)**. A source operand of type
CONST is followed by an **inline 16-byte constant** (4 IEEE floats) occupying
the next instruction slot; the decoder skips it.

### Memory layout / word order
PS3 memory is big-endian. Fragment-program words additionally have their two
16-bit halves swapped relative to host order. So the host-order word is:

```
host = swap16halves( be32_load(ptr) )   where swap16halves(v) = (v<<16)|(v>>16)
```

`rsx_fp_read_word()` performs exactly this. The decoder proper operates on
already-host-order `u32` words so it can be unit-tested independently.

### DWORD 0 — OPDEST
| Field | Bits | Notes |
|---|---|---|
| PROGRAM_END | 0 | last instruction |
| OUT_REG | 1..6 | destination register index (6 bits) |
| OUT_REG_HALF | 7 | destination is an H (fp16) register |
| COND_WRITE_ENABLE | 8 | conditional write |
| OUT mask X/Y/Z/W | 9..12 | write mask |
| INPUT_SRC | 13..16 | interpolated input index (POSITION=0, COL0=1, COL1=2, FOGC=3, TC0=4, TC(n)=4+n, FACING=0xE) |
| TEX_UNIT | 17..20 | texture unit for TEX/TXP/… |
| PRECISION | 22..23 | |
| OPCODE | 24..29 | 6-bit opcode |
| OUT_NONE | 30 | no destination write |
| OUT_SAT | 31 | saturate result to [0,1] |

### DWORD 1/2/3 — SRC0 / SRC1 / SRC2
| Field | Bits | Notes |
|---|---|---|
| REG_TYPE | 0..1 | 0=TEMP, 1=INPUT, 2=CONST |
| SRC reg index | 2..7 | 6 bits (NV40 mask 63<<2) |
| SRC_HALF | 8 | source from H register file |
| SWZ X/Y/Z/W | 9..16 | 4× 2-bit swizzle (0=x,1=y,2=z,3=w) |
| NEGATE | 17 | negate source |

Per-source ABS lives outside the source word:
- SRC0_ABS = DWORD1 bit 29
- SRC1_ABS = DWORD2 bit 18
- SRC2 ABS — not modeled in MVP

## Opcodes (6-bit, from nvfx_shader.h)
```
NOP 0x00  MOV 0x01  MUL 0x02  ADD 0x03  MAD 0x04  DP3 0x05  DP4 0x06  DST 0x07
MIN 0x08  MAX 0x09  SLT 0x0A  SGE 0x0B  SLE 0x0C  SGT 0x0D  SNE 0x0E  SEQ 0x0F
FRC 0x10  FLR 0x11  KIL 0x12  PK4B 0x13 UP4B 0x14 DDX 0x15  DDY 0x16  TEX 0x17
TXP 0x18  TXD 0x19  RCP 0x1A  RSQ 0x1B* EX2 0x1C  LG2 0x1D  LIT 0x1E* LRP 0x1F*
STR 0x20  SFL 0x21  COS 0x22  SIN 0x23  PK2H 0x24 UP2H 0x25 POW 0x26* PK4UB 0x27
UP4UB 0x28 PK2US 0x29 UP2US 0x2A DP2A 0x2E TXL 0x2F* TXB 0x31  DIV 0x3A
```
`*` = NV30/NV40 variant value (RSQ_NV30=0x1B, LIT_NV30=0x1E, LRP_NV30=0x1F,
POW_NV30=0x26, TXL_NV40=0x2F, RFL_NV30=0x36).

Branch ops (when DWORD2 bit31 IS_BRANCH set): BRK0 CAL1 IF2 LOOP3 REP4 RET5.

## Decompiler status (MVP)
Implemented in the HLSL emitter: NOP, MOV, MUL, ADD, MAD, DP3, DP4, MIN, MAX,
FRC, FLR, RCP, RSQ, EX2, LG2, SLT/SGE/SLE/SGT/SNE/SEQ, COS, SIN, POW, LRP,
TEX/TXP, KIL. Per-source swizzle, negate, abs, and the dest write-mask are
honored. OUT_SAT wraps the result in `saturate()`.

Not yet handled (emit a `// TODO` comment, result left as the source / zero):
pack/unpack (PK*/UP*), DDX/DDY beyond XY, DST/LIT/DP2A/TXD/TXB, branching
(IF/LOOP/REP/CAL/RET), conditional writes (COND_WRITE_ENABLE), fp16 precision
differences (H registers are treated as full-precision float4).

### Integration TODO (not done this increment)
1. **DONE.** Wire `rsx_fp_decompile()` into the D3D12 backend: the bytecode at
   `fragment_program_addr` is hashed, decompiled once, compiled with
   `D3DCompile`, the PS blob is cached, and the shader identity (`fp_hash` +
   `vp_hash`) is folded into the PSO cache key (`ShaderPsoSet`). Hit/miss/
   decompile-ok/fail counters (`rsx_d3d12_fp_cache_*`) and `[RSX-SH]` tracing
   (`PS3_TRACE_RSX_SHADERS=1`) confirm this runs end-to-end in a live boot —
   see the GoW2 status note below.
2. **DONE.** Interpolated inputs COL0/COL1/FOG/TEXCOORD0..7 (+ WPOS via
   SV_POSITION) are plumbed through the passthrough VS + 12-element input
   layout; `input_expr` maps INPUT_SRC accordingly. TC8/TC9/FACING still → 0.
3. **VP wired to the D3D12 backend (with passthrough fallback).**
   `rsx_vp_decompile()` is called when `transform_program_words>=4`; the
   decompiled vertex program is compiled with `D3DCompile(...,"vs_5_0")` and
   linked against the FP's `ps_5_0` (`ShaderPsoSet` now tracks `fp_hash` +
   `vp_hash` + the owned VS blob). If decompile or compile fails, or no
   transform program is loaded (`vp_load=0`), the backend falls back to the
   built-in passthrough VS — this is a "never hard-fail" path, not an error
   state. **Verified OFFLINE**: `libs/video/tests/test_vp_ps_link.c` decompiles
   a real NV40 VP to `vs_5_0` (`D3DCompile` → `S_OK`) and confirms via
   `D3DReflect` signature comparison that all 12 PS inputs of the real
   PS3_SHADER_DEMO fragment program are matched by the VS outputs. **NOT
   exercised in-boot**: GoW2 at its current boot wall never loads a transform
   program (`vp_load=0` throughout), so the live D3D12 backend always takes
   the passthrough path (`vp_hash=0x00000000`, `vp_ok=0 vp_fail=0`) — the wire
   is live code, but the game hasn't reached the content that would drive it.
   The full path activates automatically once the game emits `vp_load!=0`,
   with no further code change needed.
4. Upload fragment-program constants to a constant buffer (the inline CONST
   slots) and bind textures (`d3d12_bind_texture` is no longer a stub — Morton
   deswizzle + EA resolve are implemented and unit-tested — but GoW2 never
   calls `SET_TEXTURE` at the current boot wall, so it is not exercised
   in-boot either; see the honest acceptance matrix in
   `docs/gow2-recomp-notes.md`).
