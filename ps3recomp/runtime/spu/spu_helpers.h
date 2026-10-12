/*
 * ps3recomp - SPU instruction semantics helpers
 *
 * Pure-C, header-only implementation of the per-instruction semantics used
 * by lifter-generated code. Extracted from spu_lifter.py so the helpers
 * have one source of truth and can be unit-tested directly (see
 * runtime/spu/tests/test_spu_helpers.c).
 *
 * Each helper is `static inline u128 spu_<mnemonic>(...)`. Naming, lane
 * widths and big-endian conventions match runtime/spu/spu_context.h.
 */

#ifndef SPU_HELPERS_H
#define SPU_HELPERS_H

#include "spu_context.h"
#include <string.h>
#include <math.h>
#include <float.h>

/* winport v34-spu-simd: x86-64 SIMD helpers (spu_helpers_x86.h, work/winport/engine_fixes_v34_spu_simd.py).
 * -DWINPORT_SPU_SIMD=0 at compile time restores the scalar *_ref code everywhere. */
#ifndef WINPORT_SPU_SIMD
#define WINPORT_SPU_SIMD 1
#endif
#if WINPORT_SPU_SIMD && defined(__x86_64__) && defined(__SSE4_1__) && (defined(__clang__) || defined(__GNUC__))
#define SPU_HELPERS_X86 1
#include <smmintrin.h>
#ifdef __AVX2__
#include <immintrin.h>
#endif
#else
#define SPU_HELPERS_X86 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
#include <intrin.h>
static inline int spu_clz32(uint32_t x) {
    unsigned long idx;
    if (!x) return 32;
    _BitScanReverse(&idx, x);
    return 31 - (int)idx;
}
#else
static inline int spu_clz32(uint32_t x) { return x ? __builtin_clz(x) : 32; }
#endif

/* Deteccao NEON subida para aqui: os construtores (spu_splat_*) sao usados por
 * spu_il/ila/ilhu, que somam 155 ocorrencias nas funcoes SPU mais quentes, e
 * precisavam do <arm_neon.h> antes da sua propria definicao. */
#if defined(__aarch64__) && defined(__ARM_NEON) && \
    defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define SPU_HELPERS_NEON 1
#include <arm_neon.h>
#endif
#if SPU_HELPERS_X86   /* winport v34-spu-simd: defined in spu_helpers_x86.h (end of file) */
static inline u128 spu_splat_u32(uint32_t v);
static inline u128 spu_splat_u16(uint16_t v);
static inline u128 spu_splat_u8(uint8_t v);
static inline u128 spu_qw_shl_bytes(u128 a, int sh);
static inline u128 spu_qw_rot_bytes(u128 a, int sh);
static inline u128 spu_qw_shr_bytes(u128 a, int sh);
static inline u128 spu_a(u128 a, u128 b);
static inline u128 spu_sf(u128 a, u128 b);
static inline u128 spu_ah(u128 a, u128 b);
static inline u128 spu_sfh(u128 a, u128 b);
static inline u128 spu_ai(u128 a, int32_t imm);
static inline u128 spu_ahi(u128 a, int32_t imm);
static inline u128 spu_sfi(u128 a, int32_t imm);
static inline u128 spu_sfhi(u128 a, int32_t imm);
static inline u128 spu_and(u128 a, u128 b);
static inline u128 spu_or(u128 a, u128 b);
static inline u128 spu_xor(u128 a, u128 b);
static inline u128 spu_nand(u128 a, u128 b);
static inline u128 spu_nor(u128 a, u128 b);
static inline u128 spu_andc(u128 a, u128 b);
static inline u128 spu_orc(u128 a, u128 b);
static inline u128 spu_andi(u128 a, int32_t imm);
static inline u128 spu_ori(u128 a, int32_t imm);
static inline u128 spu_xori(u128 a, int32_t imm);
static inline u128 spu_iohl(u128 a, uint16_t imm);
static inline u128 spu_cntb(u128 a);
static inline u128 spu_ceq(u128 a, u128 b);
static inline u128 spu_ceqh(u128 a, u128 b);
static inline u128 spu_ceqb(u128 a, u128 b);
static inline u128 spu_cgt(u128 a, u128 b);
static inline u128 spu_cgth(u128 a, u128 b);
static inline u128 spu_cgtb(u128 a, u128 b);
static inline u128 spu_clgt(u128 a, u128 b);
static inline u128 spu_clgth(u128 a, u128 b);
static inline u128 spu_clgtb(u128 a, u128 b);
static inline u128 spu_ceqi(u128 a, int32_t imm);
static inline u128 spu_cgti(u128 a, int32_t imm);
static inline u128 spu_clgti(u128 a, int32_t imm);
static inline u128 spu_selb(u128 a, u128 b, u128 c);
static inline u128 spu_shufb(u128 a, u128 b, u128 c);
static inline u128 spu_mpy(u128 a, u128 b);
static inline u128 spu_mpya(u128 a, u128 b, u128 c);
static inline u128 spu_mpyu(u128 a, u128 b);
static inline u128 spu_mpyi(u128 a, int32_t imm);
static inline u128 spu_mpyh(u128 a, u128 b);
static inline u128 spu_mpyhh(u128 a, u128 b);
static inline u128 spu_mpyui(u128 a, int32_t imm);
static inline u128 spu_shli(u128 a, int sh);
static inline u128 spu_shlhi(u128 a, int sh);
static inline u128 spu_roti(u128 a, int sh);
static inline u128 spu_rothi(u128 a, int sh);
static inline u128 spu_rotmi(u128 a, int i7);
static inline u128 spu_rotmai(u128 a, int i7);
static inline u128 spu_rotmhi(u128 a, int i7);
static inline u128 spu_rothmi(u128 a, int i7);
static inline u128 spu_rotmahi(u128 a, int i7);
static inline u128 spu_shl(u128 a, u128 b);
static inline u128 spu_rot(u128 a, u128 b);
static inline u128 spu_rotm(u128 a, u128 b);
static inline u128 spu_rotma(u128 a, u128 b);
static inline u128 spu_fa(u128 a, u128 b);
static inline u128 spu_fs(u128 a, u128 b);
static inline u128 spu_fm(u128 a, u128 b);
static inline u128 spu_fma(u128 a, u128 b, u128 c);
static inline u128 spu_fms(u128 a, u128 b, u128 c);
static inline u128 spu_fnms(u128 a, u128 b, u128 c);
static inline u128 spu_fceq(u128 a, u128 b);
static inline u128 spu_fcgt(u128 a, u128 b);
static inline u128 spu_cbd_pos(int pos);
static inline u128 spu_chd_pos(int pos);
static inline u128 spu_cwd_pos(int pos);
static inline u128 spu_cdd_pos(int pos);
static inline u128 spu_fsm(u128 a);
static inline u128 spu_fsmb(u128 a);
static inline u128 spu_fsmbi(int32_t imm);
static inline u128 spu_xsbh(u128 a);
static inline u128 spu_xshw(u128 a);
static inline u128 spu_ceqbi(u128 a, int32_t imm);
static inline u128 spu_ceqhi(u128 a, int32_t imm);
static inline u128 spu_cgthi(u128 a, int32_t imm);
static inline u128 spu_clgtbi(u128 a, int32_t imm);
static inline u128 spu_clgthi(u128 a, int32_t imm);
static inline u128 spu_cgtbi(u128 a, int32_t imm);
#endif

/* ---- constructors ---- */
static inline u128 spu_splat_u32_ref(uint32_t v) {
    u128 r; r._u32[0]=v; r._u32[1]=v; r._u32[2]=v; r._u32[3]=v; return r;
}
#if SPU_HELPERS_NEON
/* quatro stores escalares -> um vdup */
static inline u128 spu_splat_u32(uint32_t v) { u128 r; vst1q_u32((uint32_t*)&r, vdupq_n_u32(v)); return r; }
#elif !SPU_HELPERS_X86
static inline u128 spu_splat_u32(uint32_t v) { return spu_splat_u32_ref(v); }
#endif
/* Scalar into the SPU preferred slot (BE word 0), rest zero. rchcnt/rdch and
 * other channel reads land the result here; the lifter emits spu_pref_u32(). */
static inline u128 spu_pref_u32(uint32_t v) {
    u128 r; r._u32[0]=v; r._u32[1]=0; r._u32[2]=0; r._u32[3]=0; return r;
}
static inline u128 spu_splat_u16_ref(uint16_t v) {
    u128 r; for (int i=0;i<8;i++) r._u16[i]=v; return r;
}
#if SPU_HELPERS_NEON
static inline u128 spu_splat_u16(uint16_t v) { u128 r; vst1q_u16((uint16_t*)&r, vdupq_n_u16(v)); return r; }
#elif !SPU_HELPERS_X86
static inline u128 spu_splat_u16(uint16_t v) { return spu_splat_u16_ref(v); }
#endif
static inline u128 spu_splat_u8_ref(uint8_t v) {
    u128 r; for (int i=0;i<16;i++) r._u8[i]=v; return r;
}
#if SPU_HELPERS_NEON
static inline u128 spu_splat_u8(uint8_t v) { u128 r; vst1q_u8((uint8_t*)&r, vdupq_n_u8(v)); return r; }
#elif !SPU_HELPERS_X86
static inline u128 spu_splat_u8(uint8_t v) { return spu_splat_u8_ref(v); }
#endif
static inline u128 spu_zero(void) { u128 r; memset(&r,0,sizeof r); return r; }
/* ---- SPU (big-endian) byte/quadword views --------------------------------
 * The runtime convention is `_u32[i] = value of BE word i`. On an LE host the
 * raw union bytes (_u8[]) and dwords (_u64[]) therefore do NOT correspond to
 * the SPU's byte/quadword numbering. Every cross-lane byte/bit mover below
 * converts to an explicit BE byte array (index = SPU byte number) or to a
 * hi/lo u64 pair (hi = words 0:1, lo = words 2:3) and back. Fixing this made
 * the SPURS task-lib ls-pattern mask (rotqmbybi+rotqmbi over a splat
 * constant) come out right for the first time. */
/* SPU_HELPERS_NEON: AArch64 little-endian, where `_u32[i]` holding BE word i
 * means the SPU byte order is just the host bytes reversed inside every 32-bit
 * lane (vrev32q_u8), and every cross-lane byte mover is one table lookup.
 * TBL (vqtbl1q_u8) yields 0 for any index >= 16, which is exactly the zero-fill
 * the shift helpers below need, so they lose their special cases too. */
#if SPU_HELPERS_NEON
/* Carga/armazenamento de um registador SPU como vector NEON.
 *
 * Vale porque `_u32[i]` guarda a palavra BE i: a ordem das faixas da uniao e a
 * ordem das faixas do vector sao a MESMA, logo cada operacao por elemento mapeia
 * 1:1 e nao e' preciso nenhum vrev aqui (isso ja' foi feito na fronteira do LS,
 * em spu_ls_read128/write128).
 *
 * Porque isto existe (E466): as ajudantes da ULA eram lacos escalares e o clang
 * NAO as vectorizava. Medido no codigo gerado de spu0_spu_func_0000EAF8, a
 * funcao mais quente das threads de SPU (25% do tempo proprio de uma delas):
 * 973 instrucoes, 516 escalares w/x contra 35 vectoriais. */
static inline uint8x16_t  spu_ld8 (u128 a) { return vld1q_u8 ((const uint8_t *)&a); }
static inline uint16x8_t  spu_ld16(u128 a) { return vld1q_u16((const uint16_t*)&a); }
static inline uint32x4_t  spu_ld32(u128 a) { return vld1q_u32((const uint32_t*)&a); }
static inline u128 spu_st8 (uint8x16_t v) { u128 r; vst1q_u8 ((uint8_t *)&r, v); return r; }
static inline u128 spu_st16(uint16x8_t v) { u128 r; vst1q_u16((uint16_t*)&r, v); return r; }
static inline u128 spu_st32(uint32x4_t v) { u128 r; vst1q_u32((uint32_t*)&r, v); return r; }
static inline uint8x16_t spu_neon_iota(void) {
    static const uint8_t k[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    return vld1q_u8(k);
}
#endif

static inline void spu_to_be(u128 a, uint8_t* b) {
#ifdef SPU_HELPERS_NEON
    vst1q_u8(b, vrev32q_u8(vld1q_u8((const uint8_t*)&a)));
#else
    for (int w=0; w<4; w++) { uint32_t v = a._u32[w];
        b[4*w+0]=(uint8_t)(v>>24); b[4*w+1]=(uint8_t)(v>>16);
        b[4*w+2]=(uint8_t)(v>>8);  b[4*w+3]=(uint8_t)v; }
#endif
}
static inline u128 spu_from_be(const uint8_t* b) {
#ifdef SPU_HELPERS_NEON
    u128 r; vst1q_u8((uint8_t*)&r, vrev32q_u8(vld1q_u8(b))); return r;
#else
    u128 r; for (int w=0; w<4; w++)
        r._u32[w] = ((uint32_t)b[4*w+0]<<24)|((uint32_t)b[4*w+1]<<16)|
                    ((uint32_t)b[4*w+2]<<8)|(uint32_t)b[4*w+3];
    return r;
#endif
}
/* brsl/brasl link register: the return address goes in the PREFERRED SLOT
 * (SPU word 0 = _u32[0]) ONLY, the other three words zero — SPU v128::from32r.
 * Splatting the address to all four words is a known mislift class (the SPU
 * code may read/rotate r0 as data, e.g. computing a table index from the
 * return address), so keep the non-preferred words zero. */
static inline u128 spu_link(uint32_t ret_addr) {
    u128 r; r._u32[0]=ret_addr; r._u32[1]=r._u32[2]=r._u32[3]=0; return r;
}
static inline u128 spu_qw_shl_bytes_ref(u128 a, int sh) {   /* left, zero-fill */
    uint8_t s[16], d[16]; if (sh >= 16) return spu_zero();
    spu_to_be(a, s);
    for (int i=0;i<16;i++) { int j=i+sh; d[i] = (j<16) ? s[j] : 0; }
    return spu_from_be(d);
}
static inline u128 spu_qw_rot_bytes_ref(u128 a, int sh) {   /* rotate left */
    uint8_t s[16], d[16]; spu_to_be(a, s);
    for (int i=0;i<16;i++) d[i] = s[(i+sh)&15];
    return spu_from_be(d);
}
static inline u128 spu_qw_shr_bytes_ref(u128 a, int sh) {   /* right, zero-fill */
    uint8_t s[16], d[16]; if (sh >= 16) return spu_zero();
    spu_to_be(a, s);
    for (int i=0;i<16;i++) { int j=i-sh; d[i] = (j>=0) ? s[j] : 0; }
    return spu_from_be(d);
}

#ifdef SPU_HELPERS_NEON
/* d[i] = s[i+sh], zero past the end: index i+sh >= 16 makes TBL emit 0. The
 * callers mask sh to 5 bits, so i+sh <= 46 and the u8 index never wraps. */
static inline u128 spu_qw_shl_bytes(u128 a, int sh) {
    const uint8x16_t be = vrev32q_u8(vld1q_u8((const uint8_t*)&a));
    const uint8x16_t idx = vaddq_u8(spu_neon_iota(), vdupq_n_u8((uint8_t)sh));
    u128 r; vst1q_u8((uint8_t*)&r, vrev32q_u8(vqtbl1q_u8(be, idx))); return r;
}
/* d[i] = s[(i+sh) & 15]. (i+sh) mod 16 only depends on sh mod 16. */
static inline u128 spu_qw_rot_bytes(u128 a, int sh) {
    const uint8x16_t be = vrev32q_u8(vld1q_u8((const uint8_t*)&a));
    const uint8x16_t idx = vandq_u8(vaddq_u8(spu_neon_iota(), vdupq_n_u8((uint8_t)(sh & 15))),
                                    vdupq_n_u8(0x0F));
    u128 r; vst1q_u8((uint8_t*)&r, vrev32q_u8(vqtbl1q_u8(be, idx))); return r;
}
/* d[i] = s[i-sh], zero before the start: i-sh < 0 wraps the u8 index to
 * >= 0xF1, which is out of range for TBL and therefore 0 -- same for sh >= 16. */
static inline u128 spu_qw_shr_bytes(u128 a, int sh) {
    const uint8x16_t be = vrev32q_u8(vld1q_u8((const uint8_t*)&a));
    const uint8x16_t idx = vsubq_u8(spu_neon_iota(), vdupq_n_u8((uint8_t)sh));
    u128 r; vst1q_u8((uint8_t*)&r, vrev32q_u8(vqtbl1q_u8(be, idx))); return r;
}
#elif !SPU_HELPERS_X86
static inline u128 spu_qw_shl_bytes(u128 a, int sh) { return spu_qw_shl_bytes_ref(a, sh); }
static inline u128 spu_qw_rot_bytes(u128 a, int sh) { return spu_qw_rot_bytes_ref(a, sh); }
static inline u128 spu_qw_shr_bytes(u128 a, int sh) { return spu_qw_shr_bytes_ref(a, sh); }
#endif
static inline void spu_to_hilo(u128 a, uint64_t* hi, uint64_t* lo) {
    *hi = ((uint64_t)a._u32[0]<<32) | a._u32[1];
    *lo = ((uint64_t)a._u32[2]<<32) | a._u32[3];
}
static inline u128 spu_from_hilo(uint64_t hi, uint64_t lo) {
    u128 r;
    r._u32[0]=(uint32_t)(hi>>32); r._u32[1]=(uint32_t)hi;
    r._u32[2]=(uint32_t)(lo>>32); r._u32[3]=(uint32_t)lo;
    return r;
}
/* ---- winport v83b-spu-codegen: WINPORT_SPU_CG83 (compile time, default 1) ---- */
#ifndef WINPORT_SPU_CG83
#define WINPORT_SPU_CG83 1
#endif
#if WINPORT_SPU_CG83 && SPU_HELPERS_X86
/* lane i of the __m128i = SPU word i (word 0 = most significant). sh in 1..7 at every call site (& 7, 0 returns a). */
static inline u128 spu_qw_shl_bits(u128 a, int sh) {
    if (!sh) return a;
    __m128i x; memcpy(&x, &a, 16);
    const __m128i r = _mm_or_si128(_mm_sll_epi32(x, _mm_cvtsi32_si128(sh)),
                                   _mm_srl_epi32(_mm_srli_si128(x, 4), _mm_cvtsi32_si128(32 - sh)));
    u128 o; memcpy(&o, &r, 16); return o;
}
static inline u128 spu_qw_rot_bits(u128 a, int sh) {
    if (!sh) return a;
    __m128i x; memcpy(&x, &a, 16);
    const __m128i r = _mm_or_si128(_mm_sll_epi32(x, _mm_cvtsi32_si128(sh)),
                                   _mm_srl_epi32(_mm_shuffle_epi32(x, _MM_SHUFFLE(0, 3, 2, 1)), _mm_cvtsi32_si128(32 - sh)));
    u128 o; memcpy(&o, &r, 16); return o;
}
static inline u128 spu_qw_shr_bits(u128 a, int sh) {
    if (!sh) return a;
    __m128i x; memcpy(&x, &a, 16);
    const __m128i r = _mm_or_si128(_mm_srl_epi32(x, _mm_cvtsi32_si128(sh)),
                                   _mm_sll_epi32(_mm_slli_si128(x, 4), _mm_cvtsi32_si128(32 - sh)));
    u128 o; memcpy(&o, &r, 16); return o;
}
#else
static inline u128 spu_qw_shl_bits(u128 a, int sh) {
    uint64_t hi, lo; if (!sh) return a;
    spu_to_hilo(a, &hi, &lo);
    return spu_from_hilo((hi<<sh)|(lo>>(64-sh)), lo<<sh);
}
static inline u128 spu_qw_rot_bits(u128 a, int sh) {
    uint64_t hi, lo; if (!sh) return a;
    spu_to_hilo(a, &hi, &lo);
    return spu_from_hilo((hi<<sh)|(lo>>(64-sh)), (lo<<sh)|(hi>>(64-sh)));
}
static inline u128 spu_qw_shr_bits(u128 a, int sh) {
    uint64_t hi, lo; if (!sh) return a;
    spu_to_hilo(a, &hi, &lo);
    return spu_from_hilo(hi>>sh, (lo>>sh)|(hi<<(64-sh)));
}
#endif
/* ---- end winport v83b-spu-codegen ---- */


/* ---- integer arithmetic (SIMD) ---- */
static inline u128 spu_a_ref(u128 a, u128 b)  { u128 r; for(int i=0;i<4;i++) r._u32[i]=a._u32[i]+b._u32[i]; return r; }
static inline u128 spu_sf_ref(u128 a, u128 b) { u128 r; for(int i=0;i<4;i++) r._u32[i]=b._u32[i]-a._u32[i]; return r; }
static inline u128 spu_ah_ref(u128 a, u128 b) { u128 r; for(int i=0;i<8;i++) r._u16[i]=a._u16[i]+b._u16[i]; return r; }
static inline u128 spu_sfh_ref(u128 a, u128 b){ u128 r; for(int i=0;i<8;i++) r._u16[i]=b._u16[i]-a._u16[i]; return r; }
static inline u128 spu_ai_ref(u128 a, int32_t imm) { u128 r; for(int i=0;i<4;i++) r._u32[i]=a._u32[i]+(uint32_t)imm; return r; }
static inline u128 spu_ahi_ref(u128 a, int32_t imm){ u128 r; for(int i=0;i<8;i++) r._u16[i]=a._u16[i]+(uint16_t)imm; return r; }
static inline u128 spu_sfi_ref(u128 a, int32_t imm) { u128 r; for(int i=0;i<4;i++) r._u32[i]=(uint32_t)imm-a._u32[i]; return r; }
static inline u128 spu_sfhi_ref(u128 a, int32_t imm){ u128 r; for(int i=0;i<8;i++) r._u16[i]=(uint16_t)imm-a._u16[i]; return r; }

/* ---- multiply (low halfword of each word × ... -> 32-bit, per SPU mpy) ----
 * Sub-lane indexing assumes a little-endian host (matches the recompiler's
 * target). The "low halfword of word i" in SPU BE semantics is _s16[2i] on
 * an LE host (NOT _s16[2i+1], which would be correct on a BE host). */
static inline u128 spu_mpy_ref(u128 a, u128 b)  { u128 r; for(int i=0;i<4;i++) r._s32[i]=(int32_t)a._s16[i*2]*(int32_t)b._s16[i*2]; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_mpy(u128 a, u128 b) { return spu_mpy_ref(a, b); }
#endif
/* mpya: 16x16 signed multiply of low halves + add rc (per word, RRR form). */
static inline u128 spu_mpya_ref(u128 a, u128 b, u128 c) { u128 r; for(int i=0;i<4;i++) r._s32[i]=(int32_t)a._s16[i*2]*(int32_t)b._s16[i*2]+c._s32[i]; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_mpya(u128 a, u128 b, u128 c) { return spu_mpya_ref(a, b, c); }
#endif
/* sfx: extended subtract rb-ra-1+carry; carry-in = low bit of old rt (RT is 3rd src). */
static inline u128 spu_sfx(u128 a, u128 b, u128 t) { u128 r; for(int i=0;i<4;i++) r._u32[i]=b._u32[i]+~a._u32[i]+(t._u32[i]&1u); return r; }
/* fi: floating interpolate (reciprocal refine after frest) — Newton-step approximation. */
/* fi = "floating interpolate": on hardware it REFINES the estimate in rb that
 * frest/frsqest left, using the interpolation tables the estimate encodes.
 * It refines a RECIPROCAL and a RECIPROCAL SQUARE ROOT alike, so the Newton
 * step for 1/x that used to be here (y*(2 - a*y)) was wrong for every
 * frsqest+fi pair -- 1/sqrt(16) came out -0.5 instead of 0.25, and for small
 * inputs it doubled the result. Since spu_frest/spu_frsqest already compute
 * the accurate value (not a 12-bit estimate), the refinement has nothing left
 * to do: return the estimate, like RPCS3's interpreter does. */
static inline u128 spu_fi(u128 a, u128 b)   { (void)a; return b; }
static inline u128 spu_mpyu_ref(u128 a, u128 b) { u128 r; for(int i=0;i<4;i++) r._u32[i]=(uint32_t)a._u16[i*2]*(uint32_t)b._u16[i*2]; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_mpyu(u128 a, u128 b) { return spu_mpyu_ref(a, b); }
#endif
static inline u128 spu_mpyi_ref(u128 a, int32_t imm) { u128 r; for(int i=0;i<4;i++) r._s32[i]=(int32_t)a._s16[i*2]*(int16_t)imm; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_mpyi(u128 a, int32_t imm) { return spu_mpyi_ref(a, imm); }
#endif

/* ---- bitwise logic (whole 128 bits) ---- */
static inline u128 spu_and_ref(u128 a, u128 b) { u128 r; r._u64[0]=a._u64[0]&b._u64[0]; r._u64[1]=a._u64[1]&b._u64[1]; return r; }
static inline u128 spu_or_ref(u128 a, u128 b)  { u128 r; r._u64[0]=a._u64[0]|b._u64[0]; r._u64[1]=a._u64[1]|b._u64[1]; return r; }
static inline u128 spu_xor_ref(u128 a, u128 b) { u128 r; r._u64[0]=a._u64[0]^b._u64[0]; r._u64[1]=a._u64[1]^b._u64[1]; return r; }
static inline u128 spu_nand_ref(u128 a, u128 b){ u128 r; r._u64[0]=~(a._u64[0]&b._u64[0]); r._u64[1]=~(a._u64[1]&b._u64[1]); return r; }
static inline u128 spu_nor_ref(u128 a, u128 b) { u128 r; r._u64[0]=~(a._u64[0]|b._u64[0]); r._u64[1]=~(a._u64[1]|b._u64[1]); return r; }
static inline u128 spu_andc_ref(u128 a, u128 b){ u128 r; r._u64[0]=a._u64[0]&~b._u64[0]; r._u64[1]=a._u64[1]&~b._u64[1]; return r; }
static inline u128 spu_orc_ref(u128 a, u128 b) { u128 r; r._u64[0]=a._u64[0]|~b._u64[0]; r._u64[1]=a._u64[1]|~b._u64[1]; return r; }
static inline u128 spu_andi_ref(u128 a, int32_t imm){ u128 r; for(int i=0;i<4;i++) r._u32[i]=a._u32[i]&(uint32_t)imm; return r; }
static inline u128 spu_ori_ref(u128 a, int32_t imm) { u128 r; for(int i=0;i<4;i++) r._u32[i]=a._u32[i]|(uint32_t)imm; return r; }
static inline u128 spu_xori_ref(u128 a, int32_t imm){ u128 r; for(int i=0;i<4;i++) r._u32[i]=a._u32[i]^(uint32_t)imm; return r; }

/* ---- count leading zeros / population count per byte ---- */
static inline u128 spu_clz_ref(u128 a)  { u128 r; for(int i=0;i<4;i++) r._u32[i]=(uint32_t)spu_clz32(a._u32[i]); return r; }
static inline u128 spu_cntb_ref(u128 a) { u128 r; for(int i=0;i<16;i++){ uint8_t v=a._u8[i],c=0; while(v){c+=v&1;v>>=1;} r._u8[i]=c; } return r; }

/* ---- compares: all-ones / all-zeros per lane ---- */
static inline u128 spu_ceq_ref(u128 a, u128 b)  { u128 r; for(int i=0;i<4;i++) r._u32[i]=(a._u32[i]==b._u32[i])?0xFFFFFFFFu:0; return r; }
static inline u128 spu_ceqh_ref(u128 a, u128 b) { u128 r; for(int i=0;i<8;i++) r._u16[i]=(a._u16[i]==b._u16[i])?0xFFFFu:0; return r; }
static inline u128 spu_ceqb_ref(u128 a, u128 b) { u128 r; for(int i=0;i<16;i++) r._u8[i]=(a._u8[i]==b._u8[i])?0xFFu:0; return r; }
static inline u128 spu_cgt_ref(u128 a, u128 b)  { u128 r; for(int i=0;i<4;i++) r._u32[i]=(a._s32[i]>b._s32[i])?0xFFFFFFFFu:0; return r; }
static inline u128 spu_cgth_ref(u128 a, u128 b) { u128 r; for(int i=0;i<8;i++) r._u16[i]=(a._s16[i]>b._s16[i])?0xFFFFu:0; return r; }
static inline u128 spu_cgtb_ref(u128 a, u128 b) { u128 r; for(int i=0;i<16;i++) r._u8[i]=(a._s8[i]>b._s8[i])?0xFFu:0; return r; }
static inline u128 spu_clgt_ref(u128 a, u128 b) { u128 r; for(int i=0;i<4;i++) r._u32[i]=(a._u32[i]>b._u32[i])?0xFFFFFFFFu:0; return r; }
static inline u128 spu_clgth_ref(u128 a, u128 b){ u128 r; for(int i=0;i<8;i++) r._u16[i]=(a._u16[i]>b._u16[i])?0xFFFFu:0; return r; }
static inline u128 spu_clgtb_ref(u128 a, u128 b){ u128 r; for(int i=0;i<16;i++) r._u8[i]=(a._u8[i]>b._u8[i])?0xFFu:0; return r; }
static inline u128 spu_ceqi_ref(u128 a, int32_t imm) { u128 r; for(int i=0;i<4;i++) r._u32[i]=(a._s32[i]==imm)?0xFFFFFFFFu:0; return r; }
static inline u128 spu_cgti_ref(u128 a, int32_t imm) { u128 r; for(int i=0;i<4;i++) r._u32[i]=(a._s32[i]>imm)?0xFFFFFFFFu:0; return r; }
static inline u128 spu_clgti_ref(u128 a, int32_t imm){ u128 r; for(int i=0;i<4;i++) r._u32[i]=(a._u32[i]>(uint32_t)imm)?0xFFFFFFFFu:0; return r; }

/* ---- select / shuffle ---- */
static inline u128 spu_selb_ref(u128 a, u128 b, u128 c) {
    u128 r;
    r._u64[0]=(a._u64[0]&~c._u64[0])|(b._u64[0]&c._u64[0]);
    r._u64[1]=(a._u64[1]&~c._u64[1])|(b._u64[1]&c._u64[1]);
    return r;
}
#if SPU_HELPERS_NEON
static inline u128 spu_selb(u128 a, u128 b, u128 c) { return spu_st8(vbslq_u8(spu_ld8(c), spu_ld8(b), spu_ld8(a))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_selb(u128 a, u128 b, u128 c) { return spu_selb_ref(a, b, c); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_cntb(u128 a) { return spu_st8(vcntq_u8(spu_ld8(a))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_cntb(u128 a) { return spu_cntb_ref(a); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_clz(u128 a) { return spu_st32(vclzq_u32(spu_ld32(a))); }
#else
static inline u128 spu_clz(u128 a) { return spu_clz_ref(a); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_clgti(u128 a, int32_t imm) { return spu_st32(vcgtq_u32(spu_ld32(a), vdupq_n_u32((uint32_t)imm))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_clgti(u128 a, int32_t imm) { return spu_clgti_ref(a, imm); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_cgti(u128 a, int32_t imm) { return spu_st32(vcgtq_s32(vreinterpretq_s32_u32(spu_ld32(a)), vdupq_n_s32(imm))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_cgti(u128 a, int32_t imm) { return spu_cgti_ref(a, imm); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_ceqi(u128 a, int32_t imm) { return spu_st32(vceqq_s32(vreinterpretq_s32_u32(spu_ld32(a)), vdupq_n_s32(imm))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_ceqi(u128 a, int32_t imm) { return spu_ceqi_ref(a, imm); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_ceqb(u128 a, u128 b) { return spu_st8(vceqq_u8(spu_ld8(a), spu_ld8(b))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_ceqb(u128 a, u128 b) { return spu_ceqb_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_ceqh(u128 a, u128 b) { return spu_st16(vceqq_u16(spu_ld16(a), spu_ld16(b))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_ceqh(u128 a, u128 b) { return spu_ceqh_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_ceq(u128 a, u128 b) { return spu_st32(vceqq_u32(spu_ld32(a), spu_ld32(b))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_ceq(u128 a, u128 b) { return spu_ceq_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_clgtb(u128 a, u128 b) { return spu_st8(vcgtq_u8(spu_ld8(a), spu_ld8(b))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_clgtb(u128 a, u128 b) { return spu_clgtb_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_clgth(u128 a, u128 b) { return spu_st16(vcgtq_u16(spu_ld16(a), spu_ld16(b))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_clgth(u128 a, u128 b) { return spu_clgth_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_clgt(u128 a, u128 b) { return spu_st32(vcgtq_u32(spu_ld32(a), spu_ld32(b))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_clgt(u128 a, u128 b) { return spu_clgt_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_cgtb(u128 a, u128 b) { return spu_st8(vcgtq_s8(vreinterpretq_s8_u8(spu_ld8(a)), vreinterpretq_s8_u8(spu_ld8(b)))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_cgtb(u128 a, u128 b) { return spu_cgtb_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_cgth(u128 a, u128 b) { return spu_st16(vcgtq_s16(vreinterpretq_s16_u16(spu_ld16(a)), vreinterpretq_s16_u16(spu_ld16(b)))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_cgth(u128 a, u128 b) { return spu_cgth_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_cgt(u128 a, u128 b) { return spu_st32(vcgtq_s32(vreinterpretq_s32_u32(spu_ld32(a)), vreinterpretq_s32_u32(spu_ld32(b)))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_cgt(u128 a, u128 b) { return spu_cgt_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_sfhi(u128 a, int32_t imm) { return spu_st16(vsubq_u16(vdupq_n_u16((uint16_t)imm), spu_ld16(a))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_sfhi(u128 a, int32_t imm) { return spu_sfhi_ref(a, imm); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_sfi(u128 a, int32_t imm) { return spu_st32(vsubq_u32(vdupq_n_u32((uint32_t)imm), spu_ld32(a))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_sfi(u128 a, int32_t imm) { return spu_sfi_ref(a, imm); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_ahi(u128 a, int32_t imm) { return spu_st16(vaddq_u16(spu_ld16(a), vdupq_n_u16((uint16_t)imm))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_ahi(u128 a, int32_t imm) { return spu_ahi_ref(a, imm); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_ai(u128 a, int32_t imm) { return spu_st32(vaddq_u32(spu_ld32(a), vdupq_n_u32((uint32_t)imm))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_ai(u128 a, int32_t imm) { return spu_ai_ref(a, imm); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_sfh(u128 a, u128 b) { return spu_st16(vsubq_u16(spu_ld16(b), spu_ld16(a))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_sfh(u128 a, u128 b) { return spu_sfh_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_ah(u128 a, u128 b) { return spu_st16(vaddq_u16(spu_ld16(a), spu_ld16(b))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_ah(u128 a, u128 b) { return spu_ah_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_sf(u128 a, u128 b) { return spu_st32(vsubq_u32(spu_ld32(b), spu_ld32(a))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_sf(u128 a, u128 b) { return spu_sf_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_a(u128 a, u128 b) { return spu_st32(vaddq_u32(spu_ld32(a), spu_ld32(b))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_a(u128 a, u128 b) { return spu_a_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_xori(u128 a, int32_t imm) { return spu_st32(veorq_u32(spu_ld32(a), vdupq_n_u32((uint32_t)imm))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_xori(u128 a, int32_t imm) { return spu_xori_ref(a, imm); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_ori(u128 a, int32_t imm) { return spu_st32(vorrq_u32(spu_ld32(a), vdupq_n_u32((uint32_t)imm))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_ori(u128 a, int32_t imm) { return spu_ori_ref(a, imm); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_andi(u128 a, int32_t imm) { return spu_st32(vandq_u32(spu_ld32(a), vdupq_n_u32((uint32_t)imm))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_andi(u128 a, int32_t imm) { return spu_andi_ref(a, imm); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_orc(u128 a, u128 b) { return spu_st8(vorrq_u8(spu_ld8(a), vmvnq_u8(spu_ld8(b)))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_orc(u128 a, u128 b) { return spu_orc_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_andc(u128 a, u128 b) { return spu_st8(vbicq_u8(spu_ld8(a), spu_ld8(b))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_andc(u128 a, u128 b) { return spu_andc_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_nor(u128 a, u128 b) { return spu_st8(vmvnq_u8(vorrq_u8(spu_ld8(a), spu_ld8(b)))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_nor(u128 a, u128 b) { return spu_nor_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_nand(u128 a, u128 b) { return spu_st8(vmvnq_u8(vandq_u8(spu_ld8(a), spu_ld8(b)))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_nand(u128 a, u128 b) { return spu_nand_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_xor(u128 a, u128 b) { return spu_st8(veorq_u8(spu_ld8(a), spu_ld8(b))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_xor(u128 a, u128 b) { return spu_xor_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_or(u128 a, u128 b) { return spu_st8(vorrq_u8(spu_ld8(a), spu_ld8(b))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_or(u128 a, u128 b) { return spu_or_ref(a, b); }
#endif
#if SPU_HELPERS_NEON
static inline u128 spu_and(u128 a, u128 b) { return spu_st8(vandq_u8(spu_ld8(a), spu_ld8(b))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_and(u128 a, u128 b) { return spu_and_ref(a, b); }
#endif
/* shufb special selectors per Cell BE ISA:
 *   sel & 0xE0 == 0xE0 -> 0x80
 *   sel & 0xC0 == 0xC0 -> 0xFF
 *   sel & 0xC0 == 0x80 -> 0x00
 *   otherwise           -> concat{a,b}[sel & 0x1F] */
static inline u128 spu_shufb_ref(u128 a, u128 b, u128 c) {
    uint8_t cat[32], ctl[16], d[16];
    spu_to_be(a, cat); spu_to_be(b, cat+16); spu_to_be(c, ctl);
    for (int i=0;i<16;i++) {
        uint8_t s=ctl[i];
        if      ((s & 0xE0)==0xE0) d[i]=0x80;
        else if ((s & 0xC0)==0xC0) d[i]=0xFF;
        else if ((s & 0xC0)==0x80) d[i]=0x00;
        else                       d[i]=cat[s & 0x1F];
    }
    return spu_from_be(d);
}

#if defined(__aarch64__) && defined(__ARM_NEON) && \
    defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
/* AArch64: the whole thing is one table lookup plus three selects. shufb is
 * the hottest non-waiting leaf of a GoW2 gameplay profile (2 132 of ~7 300
 * samples/s, 2026-09-17), and the scalar loop above costs 16 iterations with
 * three branches each plus four byte-order conversions.
 *
 * `_u32[i]` holds BE word i, so reversing the bytes inside every 32-bit lane
 * yields the SPU byte order (index = SPU byte number) the selector addresses;
 * the same reverse puts the result back. spu_shufb_ref stays as the portable
 * reference and the equivalence is unit-tested (test_spu_helpers.c). */
#include <arm_neon.h>
static inline u128 spu_shufb(u128 a, u128 b, u128 c) {
    uint8x16x2_t tbl;
    tbl.val[0] = vrev32q_u8(vld1q_u8((const uint8_t*)&a));
    tbl.val[1] = vrev32q_u8(vld1q_u8((const uint8_t*)&b));
    const uint8x16_t sel = vrev32q_u8(vld1q_u8((const uint8_t*)&c));
    uint8x16_t d = vqtbl2q_u8(tbl, vandq_u8(sel, vdupq_n_u8(0x1F)));
    const uint8x16_t hi2 = vandq_u8(sel, vdupq_n_u8(0xC0));
    const uint8x16_t hi3 = vandq_u8(sel, vdupq_n_u8(0xE0));
    /* Same precedence as the reference: 0x80 group, then 0xC0, then 0xE0. */
    d = vbslq_u8(vceqq_u8(hi2, vdupq_n_u8(0x80)), vdupq_n_u8(0x00), d);
    d = vbslq_u8(vceqq_u8(hi2, vdupq_n_u8(0xC0)), vdupq_n_u8(0xFF), d);
    d = vbslq_u8(vceqq_u8(hi3, vdupq_n_u8(0xE0)), vdupq_n_u8(0x80), d);
    u128 r;
    vst1q_u8((uint8_t*)&r, vrev32q_u8(d));
    return r;
}
#elif !SPU_HELPERS_X86
static inline u128 spu_shufb(u128 a, u128 b, u128 c) { return spu_shufb_ref(a, b, c); }
#endif

/* ---- shift / rotate immediate (word lanes) ---- */
static inline u128 spu_shli_ref(u128 a, int sh)  { u128 r; sh&=0x3F; for(int i=0;i<4;i++) r._u32[i]=(sh>31)?0:(a._u32[i]<<sh); return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_shli(u128 a, int sh) { return spu_shli_ref(a, sh); }
#endif
static inline u128 spu_shlhi_ref(u128 a, int sh) { u128 r; sh&=0x1F; for(int i=0;i<8;i++) r._u16[i]=(sh>15)?0:(uint16_t)(a._u16[i]<<sh); return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_shlhi(u128 a, int sh) { return spu_shlhi_ref(a, sh); }
#endif
static inline u128 spu_roti_ref(u128 a, int sh)  { u128 r; sh&=31; for(int i=0;i<4;i++) r._u32[i]= sh ? ((a._u32[i]<<sh)|(a._u32[i]>>(32-sh))) : a._u32[i]; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_roti(u128 a, int sh) { return spu_roti_ref(a, sh); }
#endif
static inline u128 spu_rothi_ref(u128 a, int sh) { u128 r; sh&=15; for(int i=0;i<8;i++) r._u16[i]=(uint16_t)((a._u16[i]<<sh)|(a._u16[i]>>(16-sh))); return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_rothi(u128 a, int sh) { return spu_rothi_ref(a, sh); }
#endif
static inline u128 spu_rotmi_ref(u128 a, int i7)  { u128 r; int sh=(0-i7)&0x3F; for(int i=0;i<4;i++) r._u32[i]=(sh>31)?0:(a._u32[i]>>sh); return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_rotmi(u128 a, int i7) { return spu_rotmi_ref(a, i7); }
#endif
static inline u128 spu_rotmai_ref(u128 a, int i7) { u128 r; int sh=(0-i7)&0x3F; for(int i=0;i<4;i++) r._s32[i]=(sh>31)?(a._s32[i]>>31):(a._s32[i]>>sh); return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_rotmai(u128 a, int i7) { return spu_rotmai_ref(a, i7); }
#endif
static inline u128 spu_rotmhi_ref(u128 a, int i7) { u128 r; int sh=(0-i7)&0x1F; for(int i=0;i<8;i++) r._u16[i]=(sh>15)?0:(uint16_t)(a._u16[i]>>sh); return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_rotmhi(u128 a, int i7) { return spu_rotmhi_ref(a, i7); }
#endif
static inline u128 spu_shlqbyi(u128 a, int sh) { return spu_qw_shl_bytes(a, sh & 0x1F); }
static inline u128 spu_rotqbyi(u128 a, int sh) { return spu_qw_rot_bytes(a, sh & 0x0F); }
static inline u128 spu_shlqbii(u128 a, int sh) { return spu_qw_shl_bits(a, sh & 7); }
static inline u128 spu_rotqbii(u128 a, int sh) { return spu_qw_rot_bits(a, sh & 7); }

/* ---- single-precision float (4 lanes): ISA-accuracy gate (PS3_SPU_FMA_ISA) --
 * The real Cell SPU single-precision pipe is NOT IEEE-754: the WHOLE family
 * (fa/fs/fm and the fused fma/fms/fnms) shares ONE rounding model that
 *   (1) rounds the final result TOWARD ZERO (truncate, never round up), and
 *   (2) has no denormal support -- subnormal OPERANDS are treated as signed
 *       zero on the way IN, and subnormal RESULTS flush to signed zero on the
 *       way OUT.
 * By default (gate off) these six helpers keep the plain round-to-nearest,
 * denormal-allowed native-float behaviour below -- byte-identical to before
 * this gate existed -- because GoW2's currently-working SPU code may depend on
 * it and there is no way to boot-test the game from here, so "more spec-correct"
 * cannot be assumed safe to make the default. PS3_SPU_FMA_ISA=1 opts the entire
 * single-precision family into the round-toward-zero + flush-to-zero model at
 * once (enabling it for only the fused ops would create a mixed-rounding hybrid
 * that never exists on real hardware -- hence fa/fs/fm are gated together with
 * fma/fms/fnms under this one switch; the historical PS3_SPU_FMA_ISA name is
 * kept for continuity even though its scope is the whole SP pipe).
 *
 * We deliberately do NOT use fesetround(FE_TOWARDZERO): it mutates
 * process/thread-global libc FP state, and SPU job isolation
 * (runtime/spu/spu_workload.c) recovers faulting jobs via sigaltstack+siglongjmp
 * -- if a signal/longjmp fires while the rounding mode is temporarily changed,
 * the corrupted mode can silently leak into unrelated code running later on that
 * worker thread. Instead we build the EXACT mathematical result as a
 * non-overlapping double-double via an error-free transform (spu_two_sum) and
 * round that down to float ourselves (spu_rtz_ftz_f32_dd), truncating toward
 * zero and flushing subnormals.
 *
 * Why the error-free transform (not a plain `(double)a*b + (double)c`): the
 * float*float product is exact in a double (<=48-bit mantissa), but the single
 * `+`/`-` that follows rounds to nearest DOUBLE first, and truncating that
 * already-rounded double to float can double-round -- for a value whose exact
 * sum sits just below a float boundary, round-to-nearest-double snaps it UP to
 * the boundary and the truncation then keeps the boundary instead of the float
 * below it. This is a real, reproducible divergence (see the adversarial
 * vectors generated by gen_test_spu_fma_precision.py), so we carry the exact
 * residual in `lo` and let spu_rtz_ftz_f32_dd break the tie on the TRUE value. */
static inline int spu_fma_isa_gate(void) {
    static int on = -1;
    if (on < 0) {
        extern char* getenv(const char*);
        /* Repo convention (cf. cellVdec.c PS3_VDEC_REPLAY_PIC): an explicit
         * "0" DISABLES; only a non-empty, non-"0" value enables. */
        const char* e = getenv("PS3_SPU_FMA_ISA");
        on = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return on;
}

/* Flush a subnormal single-precision OPERAND to signed zero (no denormal
 * support on the SPU SP pipe). |x| < FLT_MIN (2^-126) means subnormal. */
static inline float spu_ftz_in(float x) {
    if (fabsf(x) < FLT_MIN) return signbit(x) ? -0.0f : 0.0f;
    return x;
}

/* Knuth/Møller TwoSum: exact error-free transform. Returns hi = a+b rounded to
 * nearest double and lo = the exact residual, so hi+lo == a+b EXACTLY (both
 * operands and all four ops are plain double arithmetic -- valid under the
 * default -O2/no-fast-math build; do not compile this file with -ffast-math). */
static inline void spu_two_sum(double a, double b, double* hi, double* lo) {
    double s  = a + b;
    double bb = s - a;
    *lo = (a - (s - bb)) + (b - bb);
    *hi = s;
}

/* Round the EXACT value (hi+lo), given as a non-overlapping double-double, down
 * to float using round-toward-zero + flush-to-zero, matching the SPU SP pipe.
 * Never touches the ambient/global FP rounding mode -- see the gate comment. */
static inline float spu_rtz_ftz_f32_dd(double hi, double lo) {
    if (hi == 0.0) return signbit(hi) ? -0.0f : 0.0f; /* exact +-0 */

    uint64_t bits; memcpy(&bits, &hi, sizeof bits);
    uint32_t sign   = (uint32_t)(bits >> 63) & 1u;
    uint32_t rawexp = (uint32_t)((bits >> 52) & 0x7FFu);
    uint64_t mant52 = bits & 0xFFFFFFFFFFFFFull;

    if (rawexp == 0x7FFu) return (float)hi; /* inf/nan: no rounding decision */

    /* Named dexp (not `exp`) to avoid shadowing <math.h>'s exp(). */
    int32_t dexp = (int32_t)rawexp - 1023; /* unbiased double exponent */

    if (dexp < -126) return sign ? -0.0f : 0.0f; /* below smallest normal: FTZ */
    if (dexp > 127) {                            /* overflow: saturate to +-FLT_MAX */
        uint32_t out = (sign << 31) | (0xFEu << 23) | 0x7FFFFFu;
        float f; memcpy(&f, &out, sizeof f); return f;
    }

    /* f0 = hi truncated toward zero to 23 mantissa bits. */
    uint32_t mant23  = (uint32_t)(mant52 >> 29);
    uint32_t biased  = (uint32_t)(dexp + 127);
    uint32_t f0_bits = (sign << 31) | (biased << 23) | mant23;
    float f0; memcpy(&f0, &f0_bits, sizeof f0);

    /* Double-rounding correction. rem = |hi| - |f0| is exact (the low mantissa
     * bits of hi dropped by the truncation; exact by Sterbenz). Orient lo to
     * the magnitude direction: the exact magnitude is |f0| + rem + lo_aligned.
     * If that falls strictly BELOW |f0| (lo opposes and outweighs rem) the
     * round-toward-zero result is the next float toward zero; it can never
     * exceed |f0|+1 float-ULP, so no upward correction is ever needed. */
    double rem = fabs(hi) - fabs((double)f0);
    double lo_aligned = sign ? -lo : lo;
    if (lo_aligned < 0.0 && -lo_aligned > rem) {
        uint32_t prev = f0_bits - 1u;                 /* one float ULP toward zero */
        if (((prev >> 23) & 0xFFu) == 0u)             /* underflowed to subnormal: FTZ */
            return sign ? -0.0f : 0.0f;
        float f; memcpy(&f, &prev, sizeof f); return f;
    }
    return f0;
}

static inline u128 spu_fa_ref(u128 a, u128 b) {
    u128 r;
    if (spu_fma_isa_gate()) {
        for (int i=0;i<4;i++) {
            float fa=spu_ftz_in(a._f32[i]), fb=spu_ftz_in(b._f32[i]);
            double hi, lo; spu_two_sum((double)fa, (double)fb, &hi, &lo);
            r._f32[i] = spu_rtz_ftz_f32_dd(hi, lo);
        }
        return r;
    }
    for(int i=0;i<4;i++) r._f32[i]=a._f32[i]+b._f32[i];
    return r;
}
#if !SPU_HELPERS_X86
static inline u128 spu_fa(u128 a, u128 b) { return spu_fa_ref(a, b); }
#endif
static inline u128 spu_fs_ref(u128 a, u128 b) {
    u128 r;
    if (spu_fma_isa_gate()) {
        for (int i=0;i<4;i++) {
            float fa=spu_ftz_in(a._f32[i]), fb=spu_ftz_in(b._f32[i]);
            double hi, lo; spu_two_sum((double)fa, -(double)fb, &hi, &lo);
            r._f32[i] = spu_rtz_ftz_f32_dd(hi, lo);
        }
        return r;
    }
    for(int i=0;i<4;i++) r._f32[i]=a._f32[i]-b._f32[i];
    return r;
}
#if !SPU_HELPERS_X86
static inline u128 spu_fs(u128 a, u128 b) { return spu_fs_ref(a, b); }
#endif

/* Float<->int conversions with scale (RI8), semantica do hardware e do RPCS3
 * (`g_spu_imm.scale[x]` = 2^x):
 *   cflts/cfltu: float * 2^(173 - i8)  -> i8=173 e' a conversao simples
 *   csflt/cuflt: int   * 2^(i8 - 155)  -> i8=155 e' a conversao simples
 * Havia um `-155` a mais nos dois, o que punha a escala em 2^-155: TODA
 * conversao float->int do codigo liftado dava 0 (e int->float tambem). A
 * reducao de argumento de seno/coseno do GoW2 -- `n = cflts(x*2/pi +- 0.5,
 * 173)` -- ficava com n=0, o polinomio corria no angulo por reduzir e os
 * billboards das particulas saiam com o raio inflado e a crescer com o
 * angulo. O factor vai em double: 2^173 nao cabe num float. */
static inline u128 spu_cflts(u128 a, int i8){ u128 r; double f=exp2((double)(173-i8));
    for(int i=0;i<4;i++){ double v=(double)a._f32[i]*f; if(v>2147483647.0)v=2147483647.0; if(v<-2147483648.0)v=-2147483648.0; r._s32[i]=(int32_t)v; } return r; }
static inline u128 spu_cfltu(u128 a, int i8){ u128 r; double f=exp2((double)(173-i8));
    for(int i=0;i<4;i++){ double v=(double)a._f32[i]*f; if(v<0)v=0; if(v>4294967295.0)v=4294967295.0; r._u32[i]=(uint32_t)v; } return r; }
static inline u128 spu_csflt(u128 a, int i8){ u128 r; double f=exp2((double)(i8-155));
    for(int i=0;i<4;i++) r._f32[i]=(float)((double)a._s32[i]*f); return r; }
static inline u128 spu_cuflt(u128 a, int i8){ u128 r; double f=exp2((double)(i8-155));
    for(int i=0;i<4;i++) r._f32[i]=(float)((double)a._u32[i]*f); return r; }
static inline u128 spu_fm_ref(u128 a, u128 b) {
    u128 r;
    if (spu_fma_isa_gate()) {
        for (int i=0;i<4;i++) {
            float fa=spu_ftz_in(a._f32[i]), fb=spu_ftz_in(b._f32[i]);
            /* float*float is exact in a double, so lo=0 (no residual). */
            r._f32[i] = spu_rtz_ftz_f32_dd((double)fa * (double)fb, 0.0);
        }
        return r;
    }
    for(int i=0;i<4;i++) r._f32[i]=a._f32[i]*b._f32[i]; return r;
}
#if !SPU_HELPERS_X86
static inline u128 spu_fm(u128 a, u128 b) { return spu_fm_ref(a, b); }
#endif

/* fma/fms/fnms: fused multiply-(add|sub). Under the gate, flush subnormal
 * operands, form the exact product (float*float is exact in a double), then
 * carry the exact residual of the +-c through spu_two_sum so the final
 * round-toward-zero decides on the TRUE value (no double-rounding). Semantics:
 *   fma  = a*b + c ;  fms = a*b - c ;  fnms = c - a*b . */
static inline u128 spu_fma_ref(u128 a, u128 b, u128 c) {
    u128 r;
    if (spu_fma_isa_gate()) {
        for (int i=0;i<4;i++) {
            float fa=spu_ftz_in(a._f32[i]), fb=spu_ftz_in(b._f32[i]), fc=spu_ftz_in(c._f32[i]);
            double hi, lo; spu_two_sum((double)fa * (double)fb, (double)fc, &hi, &lo);
            r._f32[i] = spu_rtz_ftz_f32_dd(hi, lo);
        }
        return r;
    }
    for(int i=0;i<4;i++) r._f32[i]=a._f32[i]*b._f32[i]+c._f32[i];
    return r;
}
#if !SPU_HELPERS_X86
static inline u128 spu_fma(u128 a, u128 b, u128 c) { return spu_fma_ref(a, b, c); }
#endif
static inline u128 spu_fms_ref(u128 a, u128 b, u128 c) {
    u128 r;
    if (spu_fma_isa_gate()) {
        for (int i=0;i<4;i++) {
            float fa=spu_ftz_in(a._f32[i]), fb=spu_ftz_in(b._f32[i]), fc=spu_ftz_in(c._f32[i]);
            double hi, lo; spu_two_sum((double)fa * (double)fb, -(double)fc, &hi, &lo);
            r._f32[i] = spu_rtz_ftz_f32_dd(hi, lo);
        }
        return r;
    }
    for(int i=0;i<4;i++) r._f32[i]=a._f32[i]*b._f32[i]-c._f32[i];
    return r;
}
#if !SPU_HELPERS_X86
static inline u128 spu_fms(u128 a, u128 b, u128 c) { return spu_fms_ref(a, b, c); }
#endif
static inline u128 spu_fnms_ref(u128 a, u128 b, u128 c) {
    u128 r;
    if (spu_fma_isa_gate()) {
        for (int i=0;i<4;i++) {
            float fa=spu_ftz_in(a._f32[i]), fb=spu_ftz_in(b._f32[i]), fc=spu_ftz_in(c._f32[i]);
            double hi, lo; spu_two_sum(-((double)fa * (double)fb), (double)fc, &hi, &lo);
            r._f32[i] = spu_rtz_ftz_f32_dd(hi, lo);
        }
        return r;
    }
    for(int i=0;i<4;i++) r._f32[i]=c._f32[i]-a._f32[i]*b._f32[i];
    return r;
}
#if !SPU_HELPERS_X86
static inline u128 spu_fnms(u128 a, u128 b, u128 c) { return spu_fnms_ref(a, b, c); }
#endif
static inline u128 spu_fceq_ref(u128 a, u128 b) { u128 r; for(int i=0;i<4;i++) r._u32[i]=(a._f32[i]==b._f32[i])?0xFFFFFFFFu:0; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_fceq(u128 a, u128 b) { return spu_fceq_ref(a, b); }
#endif
static inline u128 spu_fcgt_ref(u128 a, u128 b) { u128 r; for(int i=0;i<4;i++) r._u32[i]=(a._f32[i]>b._f32[i])?0xFFFFFFFFu:0; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_fcgt(u128 a, u128 b) { return spu_fcgt_ref(a, b); }
#endif

/* ---- double-precision float (2 slots) ----
 * A double spans 2 words; spu_ls_read128 stores word i in _u32[i] big-endian,
 * so SPU double slot k = (_u32[2k]<<32)|_u32[2k+1]. The union's _f64[k] would
 * swap those two words on a little-endian host, so reconstruct bits explicitly. */
static inline double spu_get_df(u128 x, int k) {
    uint64_t bits = ((uint64_t)x._u32[2*k] << 32) | (uint64_t)x._u32[2*k+1];
    double d; memcpy(&d, &bits, sizeof d); return d;
}
static inline void spu_set_df(u128* x, int k, double d) {
    uint64_t bits; memcpy(&bits, &d, sizeof bits);
    x->_u32[2*k]   = (uint32_t)(bits >> 32);
    x->_u32[2*k+1] = (uint32_t)bits;
}
/* E222: comparacoes double (2 slots de 64 bits, resultado all-ones/zero por slot). */
static inline u128 spu_dfceq(u128 a, u128 b)  { u128 r; for(int k=0;k<2;k++){ uint32_t m=(spu_get_df(a,k)==spu_get_df(b,k))?0xFFFFFFFFu:0u; r._u32[2*k]=m; r._u32[2*k+1]=m; } return r; }
static inline u128 spu_dfcgt(u128 a, u128 b)  { u128 r; for(int k=0;k<2;k++){ uint32_t m=(spu_get_df(a,k)>spu_get_df(b,k))?0xFFFFFFFFu:0u; r._u32[2*k]=m; r._u32[2*k+1]=m; } return r; }
static inline u128 spu_dfcmeq(u128 a, u128 b) { u128 r; for(int k=0;k<2;k++){ double x=spu_get_df(a,k), y=spu_get_df(b,k); x=x<0?-x:x; y=y<0?-y:y; uint32_t m=(x==y)?0xFFFFFFFFu:0u; r._u32[2*k]=m; r._u32[2*k+1]=m; } return r; }
static inline u128 spu_dfcmgt(u128 a, u128 b) { u128 r; for(int k=0;k<2;k++){ double x=spu_get_df(a,k), y=spu_get_df(b,k); x=x<0?-x:x; y=y<0?-y:y; uint32_t m=(x>y)?0xFFFFFFFFu:0u; r._u32[2*k]=m; r._u32[2*k+1]=m; } return r; }
static inline u128 spu_dfa(u128 a, u128 b) { u128 r; for(int k=0;k<2;k++) spu_set_df(&r,k, spu_get_df(a,k)+spu_get_df(b,k)); return r; }
static inline u128 spu_dfs(u128 a, u128 b) { u128 r; for(int k=0;k<2;k++) spu_set_df(&r,k, spu_get_df(a,k)-spu_get_df(b,k)); return r; }
static inline u128 spu_dfm(u128 a, u128 b) { u128 r; for(int k=0;k<2;k++) spu_set_df(&r,k, spu_get_df(a,k)*spu_get_df(b,k)); return r; }
/* double FMA family (SPU ISA): RT is the accumulator c. spu_lifter.py emits
 * spu_dfXX(ra,rb,rt) (see the dfma/dfms/dfnms/dfnma case in spu_lifter.py).
 * dfma=ra*rb+rt, dfms=ra*rb-rt, dfnms=rt-ra*rb, dfnma=-(ra*rb+rt). Reconstruct
 * doubles big-endian via spu_get/set_df.
 *
 * The real Cell double-precision instructions are genuinely FUSED (the product
 * is kept to full width and the whole a*b+-c expression rounds ONCE), so we use
 * the C99 fma() library function -- a single-rounding fused multiply-add -- and
 * NOT plain `a*b + c`, which would round the product to double first and then
 * round the sum, double-rounding away the low bits fma() preserves. The four
 * ops map to fma() by pushing the sign into the operands:
 *   dfma  = fma(a,  b,  c)   ; dfms  = fma(a,  b, -c)
 *   dfnms = fma(-a, b,  c)   ; dfnma = fma(-a, b, -c)
 * fma() is round-to-nearest with denormals -- Cell's DP pipe is fully IEEE-754,
 * so no round-toward-zero/flush-to-zero divergence and no gate here. (The
 * additive base version of these helpers used plain mul-then-add, which
 * double-rounds; this fused form is the precision fix and supersedes it.) */
static inline u128 spu_dfma(u128 a, u128 b, u128 c)  { u128 r; for(int k=0;k<2;k++) spu_set_df(&r,k, fma( spu_get_df(a,k), spu_get_df(b,k),  spu_get_df(c,k))); return r; }
static inline u128 spu_dfms(u128 a, u128 b, u128 c)  { u128 r; for(int k=0;k<2;k++) spu_set_df(&r,k, fma( spu_get_df(a,k), spu_get_df(b,k), -spu_get_df(c,k))); return r; }
static inline u128 spu_dfnms(u128 a, u128 b, u128 c) { u128 r; for(int k=0;k<2;k++) spu_set_df(&r,k, fma(-spu_get_df(a,k), spu_get_df(b,k),  spu_get_df(c,k))); return r; }
static inline u128 spu_dfnma(u128 a, u128 b, u128 c) { u128 r; for(int k=0;k<2;k++) spu_set_df(&r,k, fma(-spu_get_df(a,k), spu_get_df(b,k), -spu_get_df(c,k))); return r; }
/* fesd: extend single (word slots 0,2) -> double (slots 0,1). */
static inline u128 spu_fesd(u128 a) { u128 r; spu_set_df(&r,0,(double)a._f32[0]); spu_set_df(&r,1,(double)a._f32[2]); return r; }
/* frds: round double (slots 0,1) -> single (word slots 0,2); 1,3 = 0. */
static inline u128 spu_frds(u128 a) { u128 r; r._u32[1]=0; r._u32[3]=0; r._f32[0]=(float)spu_get_df(a,0); r._f32[2]=(float)spu_get_df(a,1); return r; }
/* mpyhhu: unsigned multiply of the high 16 bits of each word. */
static inline u128 spu_mpyhhu(u128 a, u128 b){ u128 r; for(int i=0;i<4;i++) r._u32[i]=(uint32_t)a._u16[2*i+1]*(uint32_t)b._u16[2*i+1]; return r; }
/* mpyhha/mpyhhau (high-16 x high-16 multiply accumulated into RT) live next to
 * spu_mpyhh below -- that is where the base version landed; not duplicated here. */
/* bg: borrow generate for (b - a); 1 = no borrow (b >= a, unsigned). */
static inline u128 spu_bg(u128 a, u128 b)   { u128 r; for(int i=0;i<4;i++) r._u32[i]=(b._u32[i]>=a._u32[i])?1u:0u; return r; }

/* ---- Phase 2: register-variable shifts/rotates ---- */
static inline u128 spu_shl_ref(u128 a, u128 b)   { u128 r; for(int i=0;i<4;i++){ uint32_t sh=b._u32[i]&0x3F; r._u32[i]=(sh>31)?0:(a._u32[i]<<sh); } return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_shl(u128 a, u128 b) { return spu_shl_ref(a, b); }
#endif
static inline u128 spu_shlh(u128 a, u128 b)  { u128 r; for(int i=0;i<8;i++){ uint32_t sh=b._u16[i]&0x1F; r._u16[i]=(sh>15)?0:(uint16_t)(a._u16[i]<<sh); } return r; }
static inline u128 spu_rot_ref(u128 a, u128 b)   { u128 r; for(int i=0;i<4;i++){ uint32_t sh=b._u32[i]&31; r._u32[i]= sh ? ((a._u32[i]<<sh)|(a._u32[i]>>(32-sh))) : a._u32[i]; } return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_rot(u128 a, u128 b) { return spu_rot_ref(a, b); }
#endif
static inline u128 spu_roth(u128 a, u128 b)  { u128 r; for(int i=0;i<8;i++){ uint32_t sh=b._u16[i]&15; r._u16[i]= sh ? (uint16_t)((a._u16[i]<<sh)|(a._u16[i]>>(16-sh))) : a._u16[i]; } return r; }
static inline u128 spu_shlqbi(u128 a, u128 b){ return spu_qw_shl_bits(a, b._u32[0] & 7); }
static inline u128 spu_rotqbi(u128 a, u128 b){ return spu_qw_rot_bits(a, b._u32[0] & 7); }
static inline u128 spu_shlqby(u128 a, u128 b){ return spu_qw_shl_bytes(a, b._u32[0] & 0x1F); }
static inline u128 spu_rotqby(u128 a, u128 b){ return spu_qw_rot_bytes(a, b._u32[0] & 0x0F); }
static inline u128 spu_shlqbybi(u128 a, u128 b){ return spu_qw_shl_bytes(a, (b._u32[0]>>3) & 0x1F); }
static inline u128 spu_rotqbybi(u128 a, u128 b){ return spu_qw_rot_bytes(a, (b._u32[0]>>3) & 0x0F); }

/* ---- Phase 2: rotmahi ---- */
static inline u128 spu_rotmahi_ref(u128 a, int i7) { u128 r; int sh=(0-i7)&0x1F; for(int i=0;i<8;i++) r._s16[i]=(sh>15)?(a._s16[i]>>15):(a._s16[i]>>sh); return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_rotmahi(u128 a, int i7) { return spu_rotmahi_ref(a, i7); }
#endif

/* ---- Phase 2: byte/half immediate compares ---- */
static inline u128 spu_ceqbi_ref(u128 a, int32_t imm)  { u128 r; uint8_t v=(uint8_t)imm; for(int i=0;i<16;i++) r._u8[i]=(a._u8[i]==v)?0xFFu:0; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_ceqbi(u128 a, int32_t imm) { return spu_ceqbi_ref(a, imm); }
#endif
static inline u128 spu_ceqhi_ref(u128 a, int32_t imm)  { u128 r; int16_t v=(int16_t)imm; for(int i=0;i<8;i++) r._u16[i]=(a._s16[i]==v)?0xFFFFu:0; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_ceqhi(u128 a, int32_t imm) { return spu_ceqhi_ref(a, imm); }
#endif
static inline u128 spu_clgtbi_ref(u128 a, int32_t imm) { u128 r; uint8_t v=(uint8_t)imm; for(int i=0;i<16;i++) r._u8[i]=(a._u8[i]>v)?0xFFu:0; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_clgtbi(u128 a, int32_t imm) { return spu_clgtbi_ref(a, imm); }
#endif
static inline u128 spu_clgthi_ref(u128 a, int32_t imm) { u128 r; uint16_t v=(uint16_t)imm; for(int i=0;i<8;i++) r._u16[i]=(a._u16[i]>v)?0xFFFFu:0; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_clgthi(u128 a, int32_t imm) { return spu_clgthi_ref(a, imm); }
#endif
static inline u128 spu_cgthi_ref(u128 a, int32_t imm)  { u128 r; int16_t v=(int16_t)imm; for(int i=0;i<8;i++) r._u16[i]=(a._s16[i]>v)?0xFFFFu:0; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_cgthi(u128 a, int32_t imm) { return spu_cgthi_ref(a, imm); }
#endif
static inline u128 spu_cgtbi_ref(u128 a, int32_t imm)  { u128 r; int8_t v=(int8_t)imm; for(int i=0;i<16;i++) r._u8[i]=(a._s8[i]>v)?0xFFu:0; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_cgtbi(u128 a, int32_t imm) { return spu_cgtbi_ref(a, imm); }
#endif

/* ---- Phase 2: misc one-offs ---- */
static inline u128 spu_fscrrd(u128 a) { (void)a; return spu_zero(); }
static inline u128 spu_gb(u128 a) {
    uint32_t v = ((a._u32[0]&1)<<3)|((a._u32[1]&1)<<2)|((a._u32[2]&1)<<1)|(a._u32[3]&1);
    u128 r = spu_zero(); r._u32[0]=v; return r;
}
static inline u128 spu_gbh(u128 a) {
    /* SPU half i contributes bit (7-i); half 2w = high half of word w. */
    uint32_t v=0;
    for (int w=0; w<4; w++) {
        v |= ((a._u32[w]>>16)&1) << (7-2*w);
        v |= ( a._u32[w]     &1) << (7-2*w-1);
    }
    u128 r = spu_zero(); r._u32[0]=v; return r;
}
static inline u128 spu_cg(u128 a, u128 b)   { u128 r; for(int i=0;i<4;i++) r._u32[i]=(uint32_t)(((uint64_t)a._u32[i]+(uint64_t)b._u32[i])>>32); return r; }
static inline u128 spu_addx(u128 a, u128 b, u128 t) { u128 r; for(int i=0;i<4;i++) r._u32[i]=a._u32[i]+b._u32[i]+(t._u32[i]&1); return r; }
/* LE host: high half of word i = _s16[2i+1], low half = _s16[2i]. */
static inline u128 spu_mpyh_ref(u128 a, u128 b) { u128 r; for(int i=0;i<4;i++) r._s32[i]=((int32_t)a._s16[2*i+1] * (int32_t)b._s16[2*i]) << 16; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_mpyh(u128 a, u128 b) { return spu_mpyh_ref(a, b); }
#endif
static inline u128 spu_mpyhh_ref(u128 a, u128 b){ u128 r; for(int i=0;i<4;i++) r._s32[i]=(int32_t)a._s16[2*i+1] * (int32_t)b._s16[2*i+1]; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_mpyhh(u128 a, u128 b) { return spu_mpyhh_ref(a, b); }
#endif
/* mpyhha/mpyhhau: high-16 x high-16 multiply ACCUMULATED into RT (=c).
 * Same high-halfword selection (_s16[2i+1]) as spu_mpyhh, plus the accumulate. */
static inline u128 spu_mpyhha(u128 a, u128 b, u128 c) { u128 r; for(int i=0;i<4;i++) r._s32[i]=c._s32[i]+(int32_t)a._s16[2*i+1]*(int32_t)b._s16[2*i+1]; return r; }
static inline u128 spu_mpyhhau(u128 a, u128 b, u128 c){ u128 r; for(int i=0;i<4;i++) r._u32[i]=c._u32[i]+(uint32_t)a._u16[2*i+1]*(uint32_t)b._u16[2*i+1]; return r; }
static inline u128 spu_mpys(u128 a, u128 b) { u128 r; for(int i=0;i<4;i++){ int32_t p=(int32_t)a._s16[2*i]*(int32_t)b._s16[2*i]; r._s32[i]=(int16_t)(p>>16); } return r; }
static inline u128 spu_mpyui_ref(u128 a, int32_t imm) { u128 r; for(int i=0;i<4;i++) r._u32[i]=(uint32_t)a._u16[2*i]*(uint32_t)(uint16_t)imm; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_mpyui(u128 a, int32_t imm) { return spu_mpyui_ref(a, imm); }
#endif
static inline u128 spu_fcmeq(u128 a, u128 b){ u128 r; for(int i=0;i<4;i++){ float fa=fabsf(a._f32[i]),fb=fabsf(b._f32[i]); r._u32[i]=(fa==fb)?0xFFFFFFFFu:0; } return r; }
static inline u128 spu_fcmgt(u128 a, u128 b){ u128 r; for(int i=0;i<4;i++){ float fa=fabsf(a._f32[i]),fb=fabsf(b._f32[i]); r._u32[i]=(fa>fb)?0xFFFFFFFFu:0; } return r; }
/* frsqest: 1/sqrt(|a|) -- the SPU IGNORES the sign (hardware masks it off, as
 * does RPCS3's _mm_rsqrt_ps(and(a, ~signbit))). Returning 0 for a <= 0, as
 * this did, turns a normalize of a vector the game knows is safe into a zero
 * length and everything downstream scales by garbage. 1/sqrt(0) saturates to
 * the largest magnitude instead of +inf (no infinities on the SPU). */
static inline u128 spu_frsqest(u128 a){
    u128 r;
    for(int i=0;i<4;i++){
        const float x = fabsf(a._f32[i]);
        r._f32[i] = (x > 0.0f) ? 1.0f/sqrtf(x) : FLT_MAX;
    }
    return r;
}
/* frest: floating reciprocal estimate (~12-bit, refined by fi). 1/a per word. */
static inline u128 spu_frest(u128 a){
    u128 r;
    for(int i=0;i<4;i++){
        const float x = a._f32[i];
        /* 1/0 is the largest magnitude with the dividend's sign, not 0: the
         * SPU has no infinities, and a 0 here silently killed whatever the
         * game divided by (sizes, normals) instead of blowing it up as the
         * hardware does, where the game's own clamp then catches it. */
        r._f32[i] = (x != 0.0f) ? 1.0f/x : (signbit(x) ? -FLT_MAX : FLT_MAX);
    }
    return r;
}

/* ---- Phase 3: sign extension ----
 * LE host: low sub-lane = _u8[2i] / _s16[2i] / _s32[2i] (the byte/half/word
 * at the *lower* storage address). Same caveat as the mpy family. */
static inline u128 spu_xsbh_ref(u128 a) { u128 r; for(int i=0;i<8;i++) r._s16[i] = (int8_t)a._u8[2*i]; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_xsbh(u128 a) { return spu_xsbh_ref(a); }
#endif
static inline u128 spu_xshw_ref(u128 a) { u128 r; for(int i=0;i<4;i++) r._s32[i] = (int16_t)a._s16[2*i]; return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_xshw(u128 a) { return spu_xshw_ref(a); }
#endif
static inline u128 spu_xswd(u128 a) { u128 r; r._u32[1]=a._u32[1]; r._u32[0]=(uint32_t)((int32_t)a._u32[1]>>31); r._u32[3]=a._u32[3]; r._u32[2]=(uint32_t)((int32_t)a._u32[3]>>31); return r; }

/* ---- Phase 3: OR across ---- */
static inline u128 spu_orx(u128 a) {
    u128 r = spu_zero();
    r._u32[0] = a._u32[0] | a._u32[1] | a._u32[2] | a._u32[3];
    return r;
}

/* ---- Phase 3: form-select mask from bits ---- */
static inline u128 spu_fsm_ref(u128 a) {
    u128 r; uint32_t v = a._u32[0] & 0xF;
    for(int i=0;i<4;i++) r._u32[i] = ((v>>(3-i))&1) ? 0xFFFFFFFFu : 0;
    return r;
}
#if !SPU_HELPERS_X86
static inline u128 spu_fsm(u128 a) { return spu_fsm_ref(a); }
#endif
static inline u128 spu_fsmh(u128 a) {
    /* SPU half i <- bit i (MSB-first). Compose per-word values so the BE
     * halfword numbering is honoured on the LE host. */
    uint32_t v = a._u32[0] & 0xFF; u128 r;
    for (int w=0; w<4; w++) {
        uint32_t hi = ((v>>(7-2*w))&1)   ? 0xFFFF0000u : 0;
        uint32_t lo = ((v>>(7-2*w-1))&1) ? 0x0000FFFFu : 0;
        r._u32[w] = hi | lo;
    }
    return r;
}
static inline u128 spu_fsmb_ref(u128 a) {
    uint32_t v = a._u32[0] & 0xFFFF; uint8_t d[16];
    for(int i=0;i<16;i++) d[i] = ((v>>(15-i))&1) ? 0xFFu : 0;   /* SPU byte i */
    return spu_from_be(d);
}
#if !SPU_HELPERS_X86
static inline u128 spu_fsmb(u128 a) { return spu_fsmb_ref(a); }
#endif
static inline u128 spu_fsmbi_ref(int32_t imm) {
    uint32_t v = imm & 0xFFFF; uint8_t d[16];
    for(int i=0;i<16;i++) d[i] = ((v>>(15-i))&1) ? 0xFFu : 0;   /* SPU byte i */
    return spu_from_be(d);
}
#if !SPU_HELPERS_X86
static inline u128 spu_fsmbi(int32_t imm) { return spu_fsmbi_ref(imm); }
#endif

/* ---- Phase 3: constant generators (insertion shuffle patterns) ---- */
static inline u128 spu_cbd_pos_ref(int pos) {
    uint8_t d[16]; for(int i=0;i<16;i++) d[i] = (uint8_t)(0x10|i);
    d[pos & 0xF] = 0x03;
    return spu_from_be(d);
}
#if !SPU_HELPERS_X86
static inline u128 spu_cbd_pos(int pos) { return spu_cbd_pos_ref(pos); }
#endif
static inline u128 spu_chd_pos_ref(int pos) {
    uint8_t d[16]; for(int i=0;i<16;i++) d[i] = (uint8_t)(0x10|i);
    int t = (pos & 0xF) & ~1;
    d[t] = 0x02; d[t+1] = 0x03;
    return spu_from_be(d);
}
#if !SPU_HELPERS_X86
static inline u128 spu_chd_pos(int pos) { return spu_chd_pos_ref(pos); }
#endif
static inline u128 spu_cwd_pos_ref(int pos) {
    uint8_t d[16]; for(int i=0;i<16;i++) d[i] = (uint8_t)(0x10|i);
    int t = (pos & 0xF) & ~3;
    d[t]=0x00; d[t+1]=0x01; d[t+2]=0x02; d[t+3]=0x03;
    return spu_from_be(d);
}
#if !SPU_HELPERS_X86
static inline u128 spu_cwd_pos(int pos) { return spu_cwd_pos_ref(pos); }
#endif
static inline u128 spu_cdd_pos_ref(int pos) {
    uint8_t d[16]; for(int i=0;i<16;i++) d[i] = (uint8_t)(0x10|i);
    int t = (pos & 0xF) & ~7;
    for(int j=0;j<8;j++) d[t+j] = (uint8_t)j;
    return spu_from_be(d);
}
#if !SPU_HELPERS_X86
static inline u128 spu_cdd_pos(int pos) { return spu_cdd_pos_ref(pos); }
#endif
static inline u128 spu_cbd(u128 a, int i7){ return spu_cbd_pos((int)a._u32[0]+i7); }
static inline u128 spu_chd(u128 a, int i7){ return spu_chd_pos((int)a._u32[0]+i7); }
static inline u128 spu_cwd(u128 a, int i7){ return spu_cwd_pos((int)a._u32[0]+i7); }
static inline u128 spu_cdd(u128 a, int i7){ return spu_cdd_pos((int)a._u32[0]+i7); }
static inline u128 spu_cbx(u128 a, u128 b){ return spu_cbd_pos((int)(a._u32[0]+b._u32[0])); }
static inline u128 spu_chx(u128 a, u128 b){ return spu_chd_pos((int)(a._u32[0]+b._u32[0])); }
static inline u128 spu_cwx(u128 a, u128 b){ return spu_cwd_pos((int)(a._u32[0]+b._u32[0])); }
static inline u128 spu_cdx(u128 a, u128 b){ return spu_cdd_pos((int)(a._u32[0]+b._u32[0])); }

/* ---- Phase 3: rotate-and-mask family ---- */
static inline u128 spu_rotm_ref(u128 a, u128 b)   { u128 r; for(int i=0;i<4;i++){ uint32_t sh=(0-b._u32[i])&0x3F; r._u32[i]=(sh>31)?0:(a._u32[i]>>sh); } return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_rotm(u128 a, u128 b) { return spu_rotm_ref(a, b); }
#endif
static inline u128 spu_rotma_ref(u128 a, u128 b)  { u128 r; for(int i=0;i<4;i++){ uint32_t sh=(0-b._u32[i])&0x3F; r._s32[i]=(sh>31)?(a._s32[i]>>31):(a._s32[i]>>sh); } return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_rotma(u128 a, u128 b) { return spu_rotma_ref(a, b); }
#endif
static inline u128 spu_rothm(u128 a, u128 b)  { u128 r; for(int i=0;i<8;i++){ uint32_t sh=(0-b._u16[i])&0x1F; r._u16[i]=(sh>15)?0:(uint16_t)(a._u16[i]>>sh); } return r; }
static inline u128 spu_rothma(u128 a, u128 b) { u128 r; for(int i=0;i<8;i++){ uint32_t sh=(0-b._u16[i])&0x1F; r._s16[i]=(sh>15)?(a._s16[i]>>15):(a._s16[i]>>sh); } return r; }
static inline u128 spu_rothmi_ref(u128 a, int i7) { u128 r; int sh=(0-i7)&0x1F; for(int i=0;i<8;i++) r._u16[i]=(sh>15)?0:(uint16_t)(a._u16[i]>>sh); return r; }
#if !SPU_HELPERS_X86
static inline u128 spu_rothmi(u128 a, int i7) { return spu_rothmi_ref(a, i7); }
#endif
static inline u128 spu_rotqmbi(u128 a, u128 b)   { return spu_qw_shr_bits(a, (0-(int)b._u32[0]) & 7); }
static inline u128 spu_rotqmby(u128 a, u128 b)   { return spu_qw_shr_bytes(a, (0-(int)b._u32[0]) & 0x1F); }
static inline u128 spu_rotqmbybi(u128 a, u128 b) { return spu_qw_shr_bytes(a, (0-((int)b._u32[0]>>3)) & 0x1F); }
static inline u128 spu_rotqmbii(u128 a, int i7)  { return spu_qw_shr_bits(a, (0-i7) & 7); }
static inline u128 spu_rotqmbyi(u128 a, int i7)  { return spu_qw_shr_bytes(a, (0-i7) & 0x1F); }

/* ---- Phase 3: halfword/byte immediate logic ---- */
static inline u128 spu_andhi(u128 a, int32_t imm) { u128 r; uint16_t v=(uint16_t)imm; for(int i=0;i<8;i++) r._u16[i]=a._u16[i]&v; return r; }
static inline u128 spu_andbi(u128 a, int32_t imm) { u128 r; uint8_t  v=(uint8_t)imm;  for(int i=0;i<16;i++) r._u8[i]=a._u8[i]&v;   return r; }
static inline u128 spu_orhi(u128 a, int32_t imm)  { u128 r; uint16_t v=(uint16_t)imm; for(int i=0;i<8;i++) r._u16[i]=a._u16[i]|v; return r; }
static inline u128 spu_orbi(u128 a, int32_t imm)  { u128 r; uint8_t  v=(uint8_t)imm;  for(int i=0;i<16;i++) r._u8[i]=a._u8[i]|v;   return r; }
static inline u128 spu_xorhi(u128 a, int32_t imm) { u128 r; uint16_t v=(uint16_t)imm; for(int i=0;i<8;i++) r._u16[i]=a._u16[i]^v; return r; }

/* ---- Phase 3: borrow-generate extended ---- */
static inline u128 spu_bgx(u128 a, u128 b, u128 t) {
    u128 r;
    for(int i=0;i<4;i++) {
        uint64_t s = (uint64_t)b._u32[i] + (uint64_t)(~a._u32[i]) + (uint64_t)(t._u32[i]&1);
        r._u32[i] = (uint32_t)(s >> 32);
    }
    return r;
}

/* ---- Phase 3: mfspr stub ---- */
static inline u128 spu_mfspr(u128 a) { (void)a; return spu_zero(); }

/* ---- immediate loaders ---- */
static inline u128 spu_il(int32_t imm)   { return spu_splat_u32((uint32_t)imm); }
static inline u128 spu_ila(uint32_t i18) { return spu_splat_u32(i18 & 0x3FFFF); }
static inline u128 spu_ilh(uint16_t imm) { return spu_splat_u16(imm); }
static inline u128 spu_ilhu(uint16_t imm){ return spu_splat_u32((uint32_t)imm << 16); }
static inline u128 spu_iohl_ref(u128 a, uint16_t imm){ u128 r; for(int i=0;i<4;i++) r._u32[i]=a._u32[i]|(uint32_t)imm; return r; }
#if SPU_HELPERS_NEON
static inline u128 spu_iohl(u128 a, uint16_t imm){ return spu_st32(vorrq_u32(spu_ld32(a), vdupq_n_u32((uint32_t)imm))); }
#elif !SPU_HELPERS_X86
static inline u128 spu_iohl(u128 a, uint16_t imm){ return spu_iohl_ref(a, imm); }
#endif

#if SPU_HELPERS_X86   /* winport v34-spu-simd */
#include "spu_helpers_x86.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* SPU_HELPERS_H */
