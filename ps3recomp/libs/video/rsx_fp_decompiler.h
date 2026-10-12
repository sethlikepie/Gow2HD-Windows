/*
 * ps3recomp - RSX Fragment Program (NV40 ISA) → HLSL decompiler
 *
 * Translates an RSX fragment shader (NVIDIA NV40 "nvfx" fragment program
 * encoding) into an HLSL pixel shader source string suitable for D3DCompile.
 *
 * See docs/RSX_FRAGMENT_PROGRAM.md for the instruction encoding reference
 * (sourced from Mesa/nouveau nvfx_shader.h). This module is self-contained:
 * it has no D3D12 dependency and can be built/tested standalone.
 */

#ifndef PS3RECOMP_RSX_FP_DECOMPILER_H
#define PS3RECOMP_RSX_FP_DECOMPILER_H

#include "ps3emu/ps3types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Read one fragment-program word from guest (PS3) memory: big-endian load
 * followed by a 16-bit half-word swap, yielding a host-order instruction
 * word. The decoder operates on host-order words. */
u32 rsx_fp_read_word(const u8* p);

/* Decompile an NV40 fragment program into an HLSL pixel shader.
 *
 *   ucode     : pointer to the fragment-program bytecode in guest memory
 *               (big-endian, half-word-swapped — i.e. exactly as the RSX
 *               reads it; this function applies rsx_fp_read_word internally).
 *   max_bytes : safety bound on how far to read (program stops at PROGRAM_END
 *               or when this many bytes are consumed).
 *   out       : caller buffer for the generated HLSL (NUL-terminated).
 *   out_size  : size of `out` in bytes.
 *
 * Returns the number of instructions decoded (>=0) on success, or -1 on
 * error (null args, output overflow). The emitted entry point is
 * `float4 main(PSInput input) : SV_TARGET`, matching the backend's
 * placeholder PSInput { float4 position : SV_POSITION; float4 col : COLOR; }.
 *
 * Texture sampling and full input plumbing are partial — see the doc's
 * "Integration TODO". Unhandled opcodes emit a comment and a safe default so
 * the shader still compiles. */
int rsx_fp_decompile(const u8* ucode, u32 max_bytes, char* out, u32 out_size);

/* Same as rsx_fp_decompile, but emit Metal Shading Language (entry `fs_main`).
 * Reuses the NV40→expression path (via HLSL dialect convert) so ops stay
 * in lock-step with the D3D12 emitter. Returns instruction count or -1. */
int rsx_fp_decompile_msl(const u8* ucode, u32 max_bytes, char* out, u32 out_size);

/* Same as rsx_fp_decompile_msl, but emit Vulkan GLSL 450 (entry `main`).
 * Interface (plan 2026-09-23-vulkan-v0): varyings at locations 0 col0, 1 col1,
 * 2 fog, 3..10 tc0..tc7; set 0 binding 0 sampler2D rsx_tex[16], binding 1
 * std430 rsx_fc[] (const-buffer mode), binding 2 std140 uvec4 rsx_at; alpha
 * test = specialization constant 0; outputs rsx_c0..rsx_c<mrt-1>, depth
 * export -> gl_FragDepth. Honours the same setters as the MSL path. Returns
 * instruction count or -1 (overflow, malformed body, more than one return). */
int rsx_fp_decompile_glsl(const u8* ucode, u32 max_bytes, char* out, u32 out_size);

/* The wrapper stage alone: `hlsl` is rsx_fp_decompile output, `n_instr` its
 * return value (passed through on success). For tests and the corpus tool. */
int rsx_fp_glsl_from_hlsl(const char* hlsl, int n_instr, char* out, u32 out_size);

/* Return the mnemonic for an NV40 fragment opcode (or "?" if unknown).
 * Useful for disassembly/logging. */
const char* rsx_fp_opcode_name(u32 opcode);

/* Total byte length of the fragment program at `ucode` (including inline
 * CONST slots), up to and including the PROGRAM_END instruction, bounded by
 * `max_bytes`. Returns 0 if no terminator is found within the bound. Shared
 * walk logic so callers (e.g. a shader cache hashing the bytecode) agree with
 * the decompiler on where a program ends. */
u32 rsx_fp_program_size(const u8* ucode, u32 max_bytes);

/* Walk the program exactly like rsx_fp_program_size and return an FNV-1a hash
 * of the INSTRUCTION words only (inline 16-byte constant blocks skipped), so a
 * title that patches FP constants per draw keeps one cache key per program.
 * Up to `max_consts` inline constants are written to `consts` (4 floats each,
 * program order = the k in rsx_fc[k]); *n_consts gets the total count. */
u32 rsx_fp_hash_and_consts(const u8* ucode, u32 max_bytes, float* consts,
                           u32 max_consts, u32* n_consts);

/* 0 (default): inline constants are emitted as float4 literals (D3D12, tests).
 * 1: they are emitted as rsx_fc[k] and rsx_fp_decompile_msl adds
 *    `constant float4* rsx_fc [[buffer(0)]]` to fs_main; the caller binds the
 *    constants of each draw (see rsx_fp_hash_and_consts). */
void rsx_fp_set_const_buffer_mode(int on);

/* Colour export register. CELL_GCM / RPCS3: NV4097_SET_SHADER_CONTROL bit
 * 0x40 (32_BITS_EXPORTS) = r0 (fp32); clear = h0 (fp16).
 *   -1 = AUTO (default, tests): r0 if the program wrote r0 or never wrote h0
 *    0 = force h0
 *    1 = force r0
 * Metal/D3D12 set this from rsx_state.shader_control before decompile. */
#define RSX_FP_SHADER_CONTROL_32_BITS_EXPORTS 0x40u
void rsx_fp_set_color_export(int mode);

/* Depth export: NV4097_SET_SHADER_CONTROL & 0xE (CELL_GCM_SHADER_CONTROL_
 * DEPTH_EXPORT). On, rsx_fp_decompile_msl returns FSOut with [[depth(any)]] =
 * r1.z when the program writes r1 (RPCS3 GLFragmentProgram). Default 0. */
#define RSX_FP_SHADER_CONTROL_DEPTH_EXPORT 0x0Eu
void rsx_fp_set_depth_export(int on);

/* Alvos de cor ligados (1..4, SET_SURFACE_COLOR_TARGET). >1: o MSL devolve
 * FSOut com c0..c(n-1) [[color(i)]], dos registos r0,r2,r3,r4 (fp32) ou
 * h0,h4,h6,h8 (fp16), como o RPCS3. Default 1. */
void rsx_fp_set_mrt_count(int n);

#ifdef __cplusplus
}
#endif
#endif /* PS3RECOMP_RSX_FP_DECOMPILER_H */
