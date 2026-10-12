/*
 * winport v34-spu-simd: x86-64 SSE2/SSSE3/SSE4.1 (+AVX2 where noted) versions of the hot SPU op helpers.
 *
 * Included at the END of runtime/spu/spu_helpers.h by work/winport/engine_fixes_v34_spu_simd.py, only when
 * SPU_HELPERS_X86 is set (x86-64 + SSE4.1 + WINPORT_SPU_SIMD != 0). The x86 counterpart of the NEON block
 * (E466): on Windows every helper used to be the scalar *_ref loop, and the EDGE geometry SPU task (lifted
 * spu0 code) is compute-bound on them (work/research/H3_combat_critical_path.md, H4_spu_simd.md).
 *
 * Layout facts everything below relies on (runtime/spu/spu_context.h, spu_helpers.h):
 *   - `_u32[i]` holds SPU BIG-ENDIAN word i as a host integer, so __m128i lane i == word i and every per-lane
 *     op maps 1:1 (spu_ls_read128/write128 do the byte swap at the LS boundary).
 *   - SPU byte k of a register is host byte (k ^ 3) (bytes reversed inside every 32-bit lane), so a cross-lane
 *     byte mover just XORs its indices with 3 -- no explicit reversal needed.
 * Every function here is checked bit-for-bit against its *_ref by work/winport/spu_simd/test_spu_simd.c
 * (>= 1M random inputs + edge cases each, at -march=x86-64-v2 and -v3, -O1 and -O3).
 */
#ifndef SPU_HELPERS_X86_H
#define SPU_HELPERS_X86_H
#include <smmintrin.h>
#ifdef __AVX2__
#include <immintrin.h>
#endif

static inline __m128i spx_ld(u128 a) { __m128i v; memcpy(&v, &a, 16); return v; }
static inline u128 spx_st(__m128i v) { u128 r; memcpy(&r, &v, 16); return r; }
/* host-byte index j of SPU byte j^3, i.e. iota ^ 3 */
#define SPX_IOTA_X3 _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12)

/* ---- constructors ---- */
static inline u128 spu_splat_u32(uint32_t v) { return spx_st(_mm_set1_epi32((int)v)); }
static inline u128 spu_splat_u16(uint16_t v) { return spx_st(_mm_set1_epi16((short)v)); }
static inline u128 spu_splat_u8(uint8_t v)   { return spx_st(_mm_set1_epi8((char)v)); }

/* ---- quadword byte movers (pshufb; index >= 0x80 yields 0 = the zero fill) ---- */
static inline u128 spu_qw_shl_bytes(u128 a, int sh) {   /* d[i] = s[i+sh] (SPU bytes), 0 past the end */
    if ((unsigned)sh >= 16u) return spu_zero();
    const __m128i k = _mm_add_epi8(SPX_IOTA_X3, _mm_set1_epi8((char)sh));          /* SPU src index, <= 30 */
    const __m128i idx = _mm_or_si128(_mm_xor_si128(k, _mm_set1_epi8(3)),
                                     _mm_cmpgt_epi8(k, _mm_set1_epi8(15)));       /* >15 -> 0xFF -> zero */
    return spx_st(_mm_shuffle_epi8(spx_ld(a), idx));
}
static inline u128 spu_qw_rot_bytes(u128 a, int sh) {   /* d[i] = s[(i+sh)&15] */
    const __m128i k = _mm_and_si128(_mm_add_epi8(SPX_IOTA_X3, _mm_set1_epi8((char)(sh & 15))),
                                    _mm_set1_epi8(15));
    return spx_st(_mm_shuffle_epi8(spx_ld(a), _mm_xor_si128(k, _mm_set1_epi8(3))));
}
static inline u128 spu_qw_shr_bytes(u128 a, int sh) {   /* d[i] = s[i-sh], 0 before the start */
    if ((unsigned)sh >= 16u) return spu_zero();
    /* k = (j^3) - sh in [-15, 15]; negative k has bit 7 set (kept by ^3) -> pshufb writes 0 */
    const __m128i k = _mm_sub_epi8(SPX_IOTA_X3, _mm_set1_epi8((char)sh));
    return spx_st(_mm_shuffle_epi8(spx_ld(a), _mm_xor_si128(k, _mm_set1_epi8(3))));
}

/* ---- integer arithmetic ---- */
static inline u128 spu_a(u128 a, u128 b)   { return spx_st(_mm_add_epi32(spx_ld(a), spx_ld(b))); }
static inline u128 spu_sf(u128 a, u128 b)  { return spx_st(_mm_sub_epi32(spx_ld(b), spx_ld(a))); }
static inline u128 spu_ah(u128 a, u128 b)  { return spx_st(_mm_add_epi16(spx_ld(a), spx_ld(b))); }
static inline u128 spu_sfh(u128 a, u128 b) { return spx_st(_mm_sub_epi16(spx_ld(b), spx_ld(a))); }
static inline u128 spu_ai(u128 a, int32_t imm)  { return spx_st(_mm_add_epi32(spx_ld(a), _mm_set1_epi32(imm))); }
static inline u128 spu_ahi(u128 a, int32_t imm) { return spx_st(_mm_add_epi16(spx_ld(a), _mm_set1_epi16((short)imm))); }
static inline u128 spu_sfi(u128 a, int32_t imm) { return spx_st(_mm_sub_epi32(_mm_set1_epi32(imm), spx_ld(a))); }
static inline u128 spu_sfhi(u128 a, int32_t imm){ return spx_st(_mm_sub_epi16(_mm_set1_epi16((short)imm), spx_ld(a))); }

/* ---- 16x16 -> 32 multiplies. Low half of word i = low 16 bits of host lane i. pmaddwd with the other
 * operand's high half zeroed is an exact signed 16x16 product (max 2^30, no overflow). ---- */
#define SPX_LO16 _mm_set1_epi32(0x0000FFFF)
static inline u128 spu_mpy(u128 a, u128 b) { return spx_st(_mm_madd_epi16(_mm_and_si128(spx_ld(a), SPX_LO16), spx_ld(b))); }
static inline u128 spu_mpya(u128 a, u128 b, u128 c) {
    return spx_st(_mm_add_epi32(_mm_madd_epi16(_mm_and_si128(spx_ld(a), SPX_LO16), spx_ld(b)), spx_ld(c)));
}
static inline u128 spu_mpyi(u128 a, int32_t imm) {
    return spx_st(_mm_madd_epi16(_mm_and_si128(spx_ld(a), SPX_LO16), _mm_set1_epi32((int)(uint16_t)imm)));
}
static inline u128 spu_mpyu(u128 a, u128 b) {   /* u16*u16 < 2^32: pmulld low half is exact */
    return spx_st(_mm_mullo_epi32(_mm_and_si128(spx_ld(a), SPX_LO16), _mm_and_si128(spx_ld(b), SPX_LO16)));
}
static inline u128 spu_mpyui(u128 a, int32_t imm) {
    return spx_st(_mm_mullo_epi32(_mm_and_si128(spx_ld(a), SPX_LO16), _mm_set1_epi32((int)(uint16_t)imm)));
}
static inline u128 spu_mpyhh(u128 a, u128 b) {  /* high halves moved down, signed via pmaddwd */
    return spx_st(_mm_madd_epi16(_mm_srli_epi32(spx_ld(a), 16), _mm_srli_epi32(spx_ld(b), 16)));
}
static inline u128 spu_mpyh(u128 a, u128 b) {   /* (a_hi * b_lo) << 16: only the low 16 product bits survive */
    return spx_st(_mm_slli_epi32(_mm_mullo_epi16(_mm_srli_epi32(spx_ld(a), 16), spx_ld(b)), 16));
}

/* ---- bitwise ---- */
static inline u128 spu_and(u128 a, u128 b)  { return spx_st(_mm_and_si128(spx_ld(a), spx_ld(b))); }
static inline u128 spu_or(u128 a, u128 b)   { return spx_st(_mm_or_si128(spx_ld(a), spx_ld(b))); }
static inline u128 spu_xor(u128 a, u128 b)  { return spx_st(_mm_xor_si128(spx_ld(a), spx_ld(b))); }
static inline u128 spu_nand(u128 a, u128 b) { return spx_st(_mm_xor_si128(_mm_and_si128(spx_ld(a), spx_ld(b)), _mm_set1_epi32(-1))); }
static inline u128 spu_nor(u128 a, u128 b)  { return spx_st(_mm_xor_si128(_mm_or_si128(spx_ld(a), spx_ld(b)), _mm_set1_epi32(-1))); }
static inline u128 spu_andc(u128 a, u128 b) { return spx_st(_mm_andnot_si128(spx_ld(b), spx_ld(a))); }
static inline u128 spu_orc(u128 a, u128 b)  { return spx_st(_mm_or_si128(spx_ld(a), _mm_xor_si128(spx_ld(b), _mm_set1_epi32(-1)))); }
static inline u128 spu_andi(u128 a, int32_t imm) { return spx_st(_mm_and_si128(spx_ld(a), _mm_set1_epi32(imm))); }
static inline u128 spu_ori(u128 a, int32_t imm)  { return spx_st(_mm_or_si128(spx_ld(a), _mm_set1_epi32(imm))); }
static inline u128 spu_xori(u128 a, int32_t imm) { return spx_st(_mm_xor_si128(spx_ld(a), _mm_set1_epi32(imm))); }
static inline u128 spu_iohl(u128 a, uint16_t imm){ return spx_st(_mm_or_si128(spx_ld(a), _mm_set1_epi32((int)(uint32_t)imm))); }
static inline u128 spu_selb(u128 a, u128 b, u128 c) {
    const __m128i m = spx_ld(c);
    return spx_st(_mm_or_si128(_mm_andnot_si128(m, spx_ld(a)), _mm_and_si128(m, spx_ld(b))));
}

/* ---- cntb: nibble-LUT popcount per byte ---- */
static inline u128 spu_cntb(u128 a) {
    const __m128i lut = _mm_setr_epi8(0,1,1,2, 1,2,2,3, 1,2,2,3, 2,3,3,4);
    const __m128i m = _mm_set1_epi8(0x0F), v = spx_ld(a);
    return spx_st(_mm_add_epi8(_mm_shuffle_epi8(lut, _mm_and_si128(v, m)),
                               _mm_shuffle_epi8(lut, _mm_and_si128(_mm_srli_epi16(v, 4), m))));
}

/* ---- compares (unsigned via sign-bit flip) ---- */
#define SPX_B32 _mm_set1_epi32((int)0x80000000u)
#define SPX_B16 _mm_set1_epi16((short)0x8000)
#define SPX_B8  _mm_set1_epi8((char)0x80)
static inline u128 spu_ceq(u128 a, u128 b)  { return spx_st(_mm_cmpeq_epi32(spx_ld(a), spx_ld(b))); }
static inline u128 spu_ceqh(u128 a, u128 b) { return spx_st(_mm_cmpeq_epi16(spx_ld(a), spx_ld(b))); }
static inline u128 spu_ceqb(u128 a, u128 b) { return spx_st(_mm_cmpeq_epi8(spx_ld(a), spx_ld(b))); }
static inline u128 spu_cgt(u128 a, u128 b)  { return spx_st(_mm_cmpgt_epi32(spx_ld(a), spx_ld(b))); }
static inline u128 spu_cgth(u128 a, u128 b) { return spx_st(_mm_cmpgt_epi16(spx_ld(a), spx_ld(b))); }
static inline u128 spu_cgtb(u128 a, u128 b) { return spx_st(_mm_cmpgt_epi8(spx_ld(a), spx_ld(b))); }
static inline u128 spu_clgt(u128 a, u128 b) { return spx_st(_mm_cmpgt_epi32(_mm_xor_si128(spx_ld(a), SPX_B32), _mm_xor_si128(spx_ld(b), SPX_B32))); }
static inline u128 spu_clgth(u128 a, u128 b){ return spx_st(_mm_cmpgt_epi16(_mm_xor_si128(spx_ld(a), SPX_B16), _mm_xor_si128(spx_ld(b), SPX_B16))); }
static inline u128 spu_clgtb(u128 a, u128 b){ return spx_st(_mm_cmpgt_epi8(_mm_xor_si128(spx_ld(a), SPX_B8), _mm_xor_si128(spx_ld(b), SPX_B8))); }
static inline u128 spu_ceqi(u128 a, int32_t imm)  { return spx_st(_mm_cmpeq_epi32(spx_ld(a), _mm_set1_epi32(imm))); }
static inline u128 spu_cgti(u128 a, int32_t imm)  { return spx_st(_mm_cmpgt_epi32(spx_ld(a), _mm_set1_epi32(imm))); }
static inline u128 spu_clgti(u128 a, int32_t imm) { return spx_st(_mm_cmpgt_epi32(_mm_xor_si128(spx_ld(a), SPX_B32), _mm_xor_si128(_mm_set1_epi32(imm), SPX_B32))); }
static inline u128 spu_ceqbi(u128 a, int32_t imm) { return spx_st(_mm_cmpeq_epi8(spx_ld(a), _mm_set1_epi8((char)imm))); }
static inline u128 spu_ceqhi(u128 a, int32_t imm) { return spx_st(_mm_cmpeq_epi16(spx_ld(a), _mm_set1_epi16((short)imm))); }
static inline u128 spu_cgtbi(u128 a, int32_t imm) { return spx_st(_mm_cmpgt_epi8(spx_ld(a), _mm_set1_epi8((char)imm))); }
static inline u128 spu_cgthi(u128 a, int32_t imm) { return spx_st(_mm_cmpgt_epi16(spx_ld(a), _mm_set1_epi16((short)imm))); }
static inline u128 spu_clgtbi(u128 a, int32_t imm){ return spx_st(_mm_cmpgt_epi8(_mm_xor_si128(spx_ld(a), SPX_B8), _mm_xor_si128(_mm_set1_epi8((char)imm), SPX_B8))); }
static inline u128 spu_clgthi(u128 a, int32_t imm){ return spx_st(_mm_cmpgt_epi16(_mm_xor_si128(spx_ld(a), SPX_B16), _mm_xor_si128(_mm_set1_epi16((short)imm), SPX_B16))); }

/* ---- shufb. Host byte j of the result is SPU byte j^3, whose selector is host byte j of c (the same
 * reversal applies to both), so c is used as is; the selected SPU byte k lives at host byte k^3 of a/b.
 * pshufb only looks at bits 3:0 (and 7, the zeroing bit), so XOR with 3 fixes the numbering; bit 4 picks
 * b over a; bit 7 set picks the special constant, looked up by the top nibble:
 *   8..B (10x) -> 0x00, C..D (110) -> 0xFF, E..F (111) -> 0x80  (same as spu_shufb_ref). ---- */
static inline u128 spu_shufb(u128 a, u128 b, u128 c) {
    const __m128i sel = spx_ld(c);
    const __m128i idx = _mm_xor_si128(sel, _mm_set1_epi8(3));
    const __m128i pa = _mm_shuffle_epi8(spx_ld(a), idx), pb = _mm_shuffle_epi8(spx_ld(b), idx);
    const __m128i d = _mm_blendv_epi8(pa, pb, _mm_slli_epi16(sel, 3));                 /* bit 4 -> bit 7 */
    const __m128i lut = _mm_setr_epi8(0,0,0,0, 0,0,0,0, 0,0,0,0, (char)0xFF,(char)0xFF,(char)0x80,(char)0x80);
    const __m128i sp = _mm_shuffle_epi8(lut, _mm_and_si128(_mm_srli_epi16(sel, 4), _mm_set1_epi8(0x0F)));
    return spx_st(_mm_blendv_epi8(d, sp, sel));                                        /* bit 7 */
}

/* ---- immediate shifts/rotates (word/half lanes). psll/psrl with a count above the lane width give 0 and
 * psra gives the sign fill, which is exactly what the reference's (sh > 31) / (sh > 15) branches do. ---- */
static inline u128 spu_shli(u128 a, int sh)  { return spx_st(_mm_sll_epi32(spx_ld(a), _mm_cvtsi32_si128(sh & 0x3F))); }
static inline u128 spu_shlhi(u128 a, int sh) { return spx_st(_mm_sll_epi16(spx_ld(a), _mm_cvtsi32_si128(sh & 0x1F))); }
static inline u128 spu_roti(u128 a, int sh)  {
    const __m128i v = spx_ld(a); sh &= 31;
    return spx_st(_mm_or_si128(_mm_sll_epi32(v, _mm_cvtsi32_si128(sh)), _mm_srl_epi32(v, _mm_cvtsi32_si128(32 - sh))));
}
static inline u128 spu_rothi(u128 a, int sh) {
    const __m128i v = spx_ld(a); sh &= 15;
    return spx_st(_mm_or_si128(_mm_sll_epi16(v, _mm_cvtsi32_si128(sh)), _mm_srl_epi16(v, _mm_cvtsi32_si128(16 - sh))));
}
static inline u128 spu_rotmi(u128 a, int i7)  { return spx_st(_mm_srl_epi32(spx_ld(a), _mm_cvtsi32_si128((0 - i7) & 0x3F))); }
static inline u128 spu_rotmai(u128 a, int i7) { return spx_st(_mm_sra_epi32(spx_ld(a), _mm_cvtsi32_si128((0 - i7) & 0x3F))); }
static inline u128 spu_rotmhi(u128 a, int i7) { return spx_st(_mm_srl_epi16(spx_ld(a), _mm_cvtsi32_si128((0 - i7) & 0x1F))); }
static inline u128 spu_rothmi(u128 a, int i7) { return spx_st(_mm_srl_epi16(spx_ld(a), _mm_cvtsi32_si128((0 - i7) & 0x1F))); }
static inline u128 spu_rotmahi(u128 a, int i7){ return spx_st(_mm_sra_epi16(spx_ld(a), _mm_cvtsi32_si128((0 - i7) & 0x1F))); }

/* ---- register-variable word shifts: AVX2 only (vpsllv/vpsrlv give 0 for counts > 31, vpsrav the sign) ---- */
#ifdef __AVX2__
static inline u128 spu_shl(u128 a, u128 b)  { return spx_st(_mm_sllv_epi32(spx_ld(a), _mm_and_si128(spx_ld(b), _mm_set1_epi32(0x3F)))); }
static inline u128 spu_rot(u128 a, u128 b)  {
    const __m128i v = spx_ld(a), s = _mm_and_si128(spx_ld(b), _mm_set1_epi32(31));
    return spx_st(_mm_or_si128(_mm_sllv_epi32(v, s), _mm_srlv_epi32(v, _mm_sub_epi32(_mm_set1_epi32(32), s))));
}
static inline u128 spu_rotm(u128 a, u128 b) { return spx_st(_mm_srlv_epi32(spx_ld(a), _mm_and_si128(_mm_sub_epi32(_mm_setzero_si128(), spx_ld(b)), _mm_set1_epi32(0x3F)))); }
static inline u128 spu_rotma(u128 a, u128 b){ return spx_st(_mm_srav_epi32(spx_ld(a), _mm_and_si128(_mm_sub_epi32(_mm_setzero_si128(), spx_ld(b)), _mm_set1_epi32(0x3F)))); }
#else
static inline u128 spu_shl(u128 a, u128 b)  { return spu_shl_ref(a, b); }
static inline u128 spu_rot(u128 a, u128 b)  { return spu_rot_ref(a, b); }
static inline u128 spu_rotm(u128 a, u128 b) { return spu_rotm_ref(a, b); }
static inline u128 spu_rotma(u128 a, u128 b){ return spu_rotma_ref(a, b); }
#endif

/* ---- sign extension (low sub-lane = low host bits) ---- */
static inline u128 spu_xsbh(u128 a) { return spx_st(_mm_srai_epi16(_mm_slli_epi16(spx_ld(a), 8), 8)); }
static inline u128 spu_xshw(u128 a) { return spx_st(_mm_srai_epi32(_mm_slli_epi32(spx_ld(a), 16), 16)); }

/* ---- form-select masks ---- */
static inline u128 spu_fsm(u128 a) {
    const __m128i bits = _mm_setr_epi32(8, 4, 2, 1);
    return spx_st(_mm_cmpeq_epi32(_mm_and_si128(_mm_set1_epi32((int)(a._u32[0] & 0xF)), bits), bits));
}
/* SPU byte i <- bit (15-i) of v. Host byte j = SPU byte j^3: bytes 0..7 of the SPU take v's high byte. */
static inline u128 spx_fsmb16(uint32_t v) {
    const __m128i pick = _mm_setr_epi8(1,1,1,1, 1,1,1,1, 0,0,0,0, 0,0,0,0);
    const __m128i bit  = _mm_setr_epi8(0x10,0x20,0x40,(char)0x80, 0x01,0x02,0x04,0x08,
                                       0x10,0x20,0x40,(char)0x80, 0x01,0x02,0x04,0x08);
    const __m128i x = _mm_shuffle_epi8(_mm_cvtsi32_si128((int)(v & 0xFFFF)), pick);
    return spx_st(_mm_cmpeq_epi8(_mm_and_si128(x, bit), bit));
}
static inline u128 spu_fsmb(u128 a)       { return spx_fsmb16(a._u32[0]); }
static inline u128 spu_fsmbi(int32_t imm) { return spx_fsmb16((uint32_t)imm); }

/* ---- insertion-pattern generators (cbd/chd/cwd/cdd): default pattern 0x10|i per SPU byte = words
 * 0x10111213 + w*0x04040404, with the 00 01 02 03 .. insert blended in by a compare mask (no partial-register
 * store + reload, which stalled store forwarding in a first word-store version). ---- */
#define SPX_INS_BASE _mm_setr_epi32(0x10111213, 0x14151617, 0x18191A1B, 0x1C1D1E1F)
static inline u128 spu_cbd_pos(int pos) {   /* SPU byte p (host byte p^3) = 0x03 */
    const __m128i m = _mm_cmpeq_epi8(SPX_IOTA_X3, _mm_set1_epi8((char)(pos & 0xF)));
    return spx_st(_mm_blendv_epi8(SPX_INS_BASE, _mm_set1_epi8(3), m));
}
static inline u128 spu_chd_pos(int pos) {   /* SPU bytes t, t+1 = 0x02, 0x03 (t even) */
    const __m128i m = _mm_cmpeq_epi8(_mm_and_si128(SPX_IOTA_X3, _mm_set1_epi8(0x0E)), _mm_set1_epi8((char)(pos & 0xE)));
    return spx_st(_mm_blendv_epi8(SPX_INS_BASE, _mm_set1_epi32(0x02030203), m));
}
static inline u128 spu_cwd_pos(int pos) {   /* word (pos>>2)&3 = 0x00010203 */
    const __m128i m = _mm_cmpeq_epi32(_mm_setr_epi32(0, 1, 2, 3), _mm_set1_epi32((pos >> 2) & 3));
    return spx_st(_mm_blendv_epi8(SPX_INS_BASE, _mm_set1_epi32(0x00010203), m));
}
static inline u128 spu_cdd_pos(int pos) {   /* doubleword (pos>>3)&1 = 0x00010203_04050607 */
    const __m128i m = _mm_cmpeq_epi32(_mm_setr_epi32(0, 0, 1, 1), _mm_set1_epi32((pos >> 3) & 1));
    return spx_st(_mm_blendv_epi8(SPX_INS_BASE, _mm_setr_epi32(0x00010203, 0x04050607, 0x00010203, 0x04050607), m));
}

/* ---- single precision. Only the DEFAULT model (PS3_SPU_FMA_ISA unset) is vectorised; the RTZ/FTZ model
 * keeps the scalar reference (spu_fma_isa_gate() is checked first, exactly as in the *_ref).
 *
 * Values: addps/subps/mulps are the same IEEE round-to-nearest operations as the scalar addss/subss/mulss of
 * the reference, lane by lane. The reference's fma/fms/fnms are written `a*b+c` etc. in one expression, so with
 * clang's default -ffp-contract=on they become ONE fused vfmadd when the target has FMA (-march=x86-64-v3) and
 * a separate mul + add otherwise (-v2); the code below follows the same split on __FMA__. (If the SPU code is
 * ever built with -ffp-contract=off on an FMA target, define WINPORT_SPU_SIMD_FPIN=0 -- the vector-expression
 * fallback follows whatever contraction rule the compiler applies to the reference.)
 *
 * NaN choice: when two or more operands of a lane are NaN, x86 returns the NaN of the FIRST source operand,
 * and clang may commute fadd/fmul operands or pick any vfmadd132/213/231 form -- so WHICH NaN comes out is
 * decided by register allocation, in the reference as much as here (the reference's own pick changes with
 * the -O level, see H4_spu_simd.md). Without FMA (WINPORT_SPU_SIMD_FPIN=1, default) inline asm pins the C
 * order (a, then b, then c) -- 100% identical to the reference at -O1 x86-64-v2, the default SPU build. With
 * FMA the plain vector expressions are used. Every non-NaN result and every single-NaN lane is identical in
 * all configurations. (The real SPU SP pipe has no NaN at all -- exponent 255 is an ordinary number there.) ---- */
#ifndef WINPORT_SPU_SIMD_FPIN
#define WINPORT_SPU_SIMD_FPIN 1
#endif
typedef float spx_v4f __attribute__((vector_size(16)));
static inline spx_v4f spx_ldf(u128 a) { spx_v4f v; memcpy(&v, &a, 16); return v; }
static inline u128 spx_stf(spx_v4f v) { u128 r; memcpy(&r, &v, 16); return r; }
#if WINPORT_SPU_SIMD_FPIN && !defined(__FMA__)
#ifdef __AVX__
#define SPX_FOP(ins, x, y) ({ spx_v4f r_; __asm__("v" ins " %2, %1, %0" : "=x"(r_) : "x"(x), "x"(y)); r_; })
#else
#define SPX_FOP(ins, x, y) ({ spx_v4f r_ = (x); __asm__(ins " %1, %0" : "+x"(r_) : "x"(y)); r_; })
#endif
static inline spx_v4f spx_add(spx_v4f x, spx_v4f y) { return SPX_FOP("addps", x, y); }
static inline spx_v4f spx_sub(spx_v4f x, spx_v4f y) { return SPX_FOP("subps", x, y); }
static inline spx_v4f spx_mul(spx_v4f x, spx_v4f y) { return SPX_FOP("mulps", x, y); }
static inline spx_v4f spx_fma(spx_v4f x, spx_v4f y, spx_v4f z)  { return spx_add(spx_mul(x, y), z); }
static inline spx_v4f spx_fms(spx_v4f x, spx_v4f y, spx_v4f z)  { return spx_sub(spx_mul(x, y), z); }
static inline spx_v4f spx_fnms(spx_v4f x, spx_v4f y, spx_v4f z) { return spx_sub(z, spx_mul(x, y)); }
#else   /* FMA targets (and FPIN=0): plain vector expressions, same shape as the reference, so clang applies the
         * same contraction to both (one vfmadd per lane-set); measured to pick the same NaN as the reference
         * more often than a pinned vfmadd213 does (see H4_spu_simd.md). */
static inline spx_v4f spx_add(spx_v4f x, spx_v4f y) { return x + y; }
static inline spx_v4f spx_sub(spx_v4f x, spx_v4f y) { return x - y; }
static inline spx_v4f spx_mul(spx_v4f x, spx_v4f y) { return x * y; }
static inline spx_v4f spx_fma(spx_v4f x, spx_v4f y, spx_v4f z)  { return x * y + z; }
static inline spx_v4f spx_fms(spx_v4f x, spx_v4f y, spx_v4f z)  { return x * y - z; }
static inline spx_v4f spx_fnms(spx_v4f x, spx_v4f y, spx_v4f z) { return z - x * y; }
#endif
#if WINPORT_SPU_CG83   /* winport v83b-spu-codegen: always inline, reference path out of line */
static __attribute__((noinline)) u128 spu_fa_cold(u128 a, u128 b) { return spu_fa_ref(a, b); }
static __attribute__((noinline)) u128 spu_fs_cold(u128 a, u128 b) { return spu_fs_ref(a, b); }
static __attribute__((noinline)) u128 spu_fm_cold(u128 a, u128 b) { return spu_fm_ref(a, b); }
static __attribute__((noinline)) u128 spu_fma_cold(u128 a, u128 b, u128 c)  { return spu_fma_ref(a, b, c); }
static __attribute__((noinline)) u128 spu_fms_cold(u128 a, u128 b, u128 c)  { return spu_fms_ref(a, b, c); }
static __attribute__((noinline)) u128 spu_fnms_cold(u128 a, u128 b, u128 c) { return spu_fnms_ref(a, b, c); }
#define SPX_AI __attribute__((always_inline))
static inline SPX_AI u128 spu_fa(u128 a, u128 b) { if (__builtin_expect(spu_fma_isa_gate(), 0)) return spu_fa_cold(a, b); return spx_stf(spx_add(spx_ldf(a), spx_ldf(b))); }
static inline SPX_AI u128 spu_fs(u128 a, u128 b) { if (__builtin_expect(spu_fma_isa_gate(), 0)) return spu_fs_cold(a, b); return spx_stf(spx_sub(spx_ldf(a), spx_ldf(b))); }
static inline SPX_AI u128 spu_fm(u128 a, u128 b) { if (__builtin_expect(spu_fma_isa_gate(), 0)) return spu_fm_cold(a, b); return spx_stf(spx_mul(spx_ldf(a), spx_ldf(b))); }
static inline SPX_AI u128 spu_fma(u128 a, u128 b, u128 c)  { if (__builtin_expect(spu_fma_isa_gate(), 0)) return spu_fma_cold(a, b, c);  return spx_stf(spx_fma(spx_ldf(a), spx_ldf(b), spx_ldf(c))); }
static inline SPX_AI u128 spu_fms(u128 a, u128 b, u128 c)  { if (__builtin_expect(spu_fma_isa_gate(), 0)) return spu_fms_cold(a, b, c);  return spx_stf(spx_fms(spx_ldf(a), spx_ldf(b), spx_ldf(c))); }
static inline SPX_AI u128 spu_fnms(u128 a, u128 b, u128 c) { if (__builtin_expect(spu_fma_isa_gate(), 0)) return spu_fnms_cold(a, b, c); return spx_stf(spx_fnms(spx_ldf(a), spx_ldf(b), spx_ldf(c))); }
#else
static inline u128 spu_fa(u128 a, u128 b) { if (spu_fma_isa_gate()) return spu_fa_ref(a, b); return spx_stf(spx_add(spx_ldf(a), spx_ldf(b))); }
static inline u128 spu_fs(u128 a, u128 b) { if (spu_fma_isa_gate()) return spu_fs_ref(a, b); return spx_stf(spx_sub(spx_ldf(a), spx_ldf(b))); }
static inline u128 spu_fm(u128 a, u128 b) { if (spu_fma_isa_gate()) return spu_fm_ref(a, b); return spx_stf(spx_mul(spx_ldf(a), spx_ldf(b))); }
static inline u128 spu_fma(u128 a, u128 b, u128 c)  { if (spu_fma_isa_gate()) return spu_fma_ref(a, b, c);  return spx_stf(spx_fma(spx_ldf(a), spx_ldf(b), spx_ldf(c))); }
static inline u128 spu_fms(u128 a, u128 b, u128 c)  { if (spu_fma_isa_gate()) return spu_fms_ref(a, b, c);  return spx_stf(spx_fms(spx_ldf(a), spx_ldf(b), spx_ldf(c))); }
static inline u128 spu_fnms(u128 a, u128 b, u128 c) { if (spu_fma_isa_gate()) return spu_fnms_ref(a, b, c); return spx_stf(spx_fnms(spx_ldf(a), spx_ldf(b), spx_ldf(c))); }
#endif
/* ordered compares: NaN -> false, -0 == +0, same as the C == / > of the reference */
static inline u128 spu_fceq(u128 a, u128 b) { return spx_st(_mm_castps_si128(_mm_cmpeq_ps(_mm_castsi128_ps(spx_ld(a)), _mm_castsi128_ps(spx_ld(b))))); }
static inline u128 spu_fcgt(u128 a, u128 b) { return spx_st(_mm_castps_si128(_mm_cmpgt_ps(_mm_castsi128_ps(spx_ld(a)), _mm_castsi128_ps(spx_ld(b))))); }

#endif /* SPU_HELPERS_X86_H */
