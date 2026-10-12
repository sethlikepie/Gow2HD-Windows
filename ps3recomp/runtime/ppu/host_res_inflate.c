/*
 * Host-side gzip inflate for GoW2 embedded resources (func_001E7B50 path).
 *
 * Embedded assets in EBOOT (gowshader.cfx, *.ctxr, …) are gzip streams
 * (1f 8b 08 …). The lifted zlib (inflateInit2 wbits=31 + inflate) returns
 * Z_DATA_ERROR (-3) on every call; host gzip/raw-deflate succeeds.
 *
 * ABI of func_001E7B50 / func_001E7A44:
 *   r3 = dst buffer
 *   r4 = ptr to u32 BE uncompressed size (avail_out / out size)
 *   r5 = compressed src
 *   r6 = compressed length
 * Returns Z_STREAM_END (1) on success, Z_DATA_ERROR (-3) on failure.
 * On success writes total_out to *r4.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ppu_context.h" /* via -I runtime/ppu */

extern uint8_t* vm_base;
/* Implemented in lfq_host_inflate.c (stb_image zlib core). */
extern int stbi_zlib_decode_noheader_buffer(char* obuffer, int olen,
                                            const char* ibuffer, int ilen);
/* D3D12 sticky content overlay (rsx_d3d12_backend.c). */
#include "rsx_host_content.h"

static uint32_t be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void wr_be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Skip gzip header; return pointer to deflate payload, or NULL. */
static const uint8_t* skip_gzip_header(const uint8_t* p, size_t len, size_t* out_len)
{
    if (len < 10) return NULL;
    if (p[0] != 0x1f || p[1] != 0x8b || p[2] != 8) return NULL;
    uint8_t flg = p[3];
    size_t i = 10;
    if (flg & 4) { /* FEXTRA */
        if (i + 2 > len) return NULL;
        uint16_t xlen = (uint16_t)(p[i] | (p[i + 1] << 8));
        i += 2u + xlen;
    }
    if (flg & 8) { /* FNAME */
        while (i < len && p[i]) i++;
        if (i >= len) return NULL;
        i++;
    }
    if (flg & 16) { /* FCOMMENT */
        while (i < len && p[i]) i++;
        if (i >= len) return NULL;
        i++;
    }
    if (flg & 2) { /* FHCRC */
        if (i + 2 > len) return NULL;
        i += 2;
    }
    if (i >= len) return NULL;
    /* Prefer dropping the 8-byte gzip trailer (CRC32+ISIZE); raw inflate
     * tolerates trailing bytes but stb is happier without them. */
    size_t rem = len - i;
    if (rem > 8) rem -= 8;
    *out_len = rem;
    return p + i;
}

static void gzip_fname(const uint8_t* in, uint32_t clen, char* fname, size_t fn_sz)
{
    fname[0] = 0;
    if (clen < 12 || !(in[3] & 8)) return;
    size_t fi = 10;
    if (in[3] & 4) {
        uint16_t xlen = (uint16_t)(in[10] | (in[11] << 8));
        fi = 12u + xlen;
    }
    size_t k = 0;
    while (fi < clen && in[fi] && k + 1 < fn_sz)
        fname[k++] = (char)in[fi++];
    fname[k] = 0;
}

static void rgb565(uint16_t c, int* r, int* g, int* b)
{
    *r = ((c >> 11) & 0x1F) * 255 / 31;
    *g = ((c >> 5) & 0x3F) * 255 / 63;
    *b = (c & 0x1F) * 255 / 31;
}

/* Decode DXT1 body (header bytes skipped) into RGBA8. */
static uint8_t* dxt1_to_rgba(const uint8_t* body, uint32_t w, uint32_t h)
{
    uint8_t* rgba = (uint8_t*)malloc((size_t)w * h * 4);
    if (!rgba) return NULL;
    uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    const uint8_t* p = body;
    for (uint32_t by = 0; by < bh; by++) {
        for (uint32_t bx = 0; bx < bw; bx++) {
            uint16_t c0 = (uint16_t)(p[0] | (p[1] << 8));
            uint16_t c1 = (uint16_t)(p[2] | (p[3] << 8));
            uint32_t bits = (uint32_t)p[4] | ((uint32_t)p[5] << 8) |
                            ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
            p += 8;
            int r0, g0, b0, r1, g1, b1;
            rgb565(c0, &r0, &g0, &b0);
            rgb565(c1, &r1, &g1, &b1);
            int pr[4], pg[4], pb[4], pa[4];
            pr[0] = r0; pg[0] = g0; pb[0] = b0; pa[0] = 255;
            pr[1] = r1; pg[1] = g1; pb[1] = b1; pa[1] = 255;
            if (c0 > c1) {
                pr[2] = (2 * r0 + r1) / 3; pg[2] = (2 * g0 + g1) / 3; pb[2] = (2 * b0 + b1) / 3; pa[2] = 255;
                pr[3] = (r0 + 2 * r1) / 3; pg[3] = (g0 + 2 * g1) / 3; pb[3] = (b0 + 2 * b1) / 3; pa[3] = 255;
            } else {
                pr[2] = (r0 + r1) / 2; pg[2] = (g0 + g1) / 2; pb[2] = (b0 + b1) / 2; pa[2] = 255;
                pr[3] = 0; pg[3] = 0; pb[3] = 0; pa[3] = 0;
            }
            for (int py = 0; py < 4; py++) {
                for (int px = 0; px < 4; px++) {
                    uint32_t x = bx * 4u + (uint32_t)px;
                    uint32_t y = by * 4u + (uint32_t)py;
                    if (x >= w || y >= h) continue;
                    int idx = (int)((bits >> (2 * (py * 4 + px))) & 3u);
                    size_t o = ((size_t)y * w + x) * 4;
                    rgba[o + 0] = (uint8_t)pr[idx];
                    rgba[o + 1] = (uint8_t)pg[idx];
                    rgba[o + 2] = (uint8_t)pb[idx];
                    rgba[o + 3] = (uint8_t)pa[idx];
                }
            }
        }
    }
    return rgba;
}

static void host_capture_dxt1_content(const uint8_t* ctxr, uint32_t total,
                                      uint32_t w, uint32_t h, uint32_t header,
                                      const char* name, int after_movie)
{
    uint32_t need = header + ((w + 3) / 4) * ((h + 3) / 4) * 8u;
    if (!ctxr || total < need) {
        fprintf(stderr, "[CONTENT] dxt1 capture short total=%u need=%u\n", total, need);
        fflush(stderr);
        return;
    }
    uint8_t* rgba = dxt1_to_rgba(ctxr + header, w, h);
    if (!rgba) return;
    uint64_t nz = 0;
    for (uint64_t i = 0; i < (uint64_t)w * h; i++)
        if (rgba[i * 4] | rgba[i * 4 + 1] | rgba[i * 4 + 2]) nz++;
    fprintf(stderr,
            "[CONTENT] decoded DXT1 '%s' %ux%u nonzero=%llu/%u after_movie=%d\n",
            name ? name : "?", w, h, (unsigned long long)nz, w * h, after_movie);
    fflush(stderr);
    /* Queue owns hold policy (legal now / company logos post-movie). */
    rsx_host_boot_logo_push(name, rgba, w, h, after_movie);
    free(rgba);
}

/* Returns 1 if handled (success or hard fail with r3 set), 0 to fall back. */
int host_res_inflate(ppu_context* ctx)
{
    if (!vm_base) return 0;

    uint32_t dst  = (uint32_t)ctx->gpr[3];
    uint32_t sizep = (uint32_t)ctx->gpr[4];
    uint32_t src  = (uint32_t)ctx->gpr[5];
    uint32_t clen = (uint32_t)ctx->gpr[6];

    if (!dst || !src || !clen || clen > 0x1000000u) return 0;
    if (src >= 0xFFF00000u || dst >= 0xFFF00000u) return 0;

    uint32_t dmax = 0;
    if (sizep && sizep < 0xFFF00000u)
        dmax = be32(vm_base + sizep);
    if (!dmax || dmax > 0x4000000u) return 0;

    const uint8_t* in = vm_base + src;
    size_t raw_len = 0;
    const uint8_t* raw = skip_gzip_header(in, clen, &raw_len);
    if (!raw || raw_len == 0) {
        /* Not gzip — leave to lifted path. */
        return 0;
    }

    int outlen = stbi_zlib_decode_noheader_buffer(
        (char*)(vm_base + dst), (int)dmax,
        (const char*)raw, (int)raw_len);

    char fname[64];
    gzip_fname(in, clen, fname, sizeof(fname));

    static int n;
    /* Always log first 32 HOSTRES results (ctxr/cfx destinations matter for
     * content-pixel bind_texture tracking). */
    if (n < 32) {
        n++;
        fprintf(stderr,
                "[HOSTRES] #%d src=0x%08X dst=0x%08X clen=0x%X dmax=0x%X raw_len=%zu outlen=%d name='%s' head=",
                n, src, dst, clen, dmax, raw_len, outlen, fname[0] ? fname : "?");
        for (int i = 0; i < 8 && i < (int)clen; i++)
            fprintf(stderr, "%02X", in[i]);
        fputc('\n', stderr);
        fflush(stderr);
    }

    if (outlen <= 0) {
        ctx->gpr[3] = (int64_t)(int32_t)(-3); /* Z_DATA_ERROR */
        return 1;
    }

    if (sizep && sizep < 0xFFF00000u)
        wr_be32(vm_base + sizep, (uint32_t)outlen);

    /* Fullscreen boot DXT1 ctxr = 128-byte header + DXT1 1280x720 (460800).
     * Guest reuses scratch 0x4B000000 — capture each by name at inflate time.
     * legal → hold now; scep_e / bluepoint → post-movie logo queue. */
    if (outlen == 460928 && fname[0]) {
        int after = -1;
        if (strstr(fname, "legalscreen720"))
            after = 0;
        else if (strstr(fname, "scep_e") || strstr(fname, "bluepoint"))
            after = 1;
        if (after >= 0) {
            static int cap_legal, cap_scep, cap_bp;
            int* once = strstr(fname, "legalscreen") ? &cap_legal
                        : strstr(fname, "scep")       ? &cap_scep
                                                      : &cap_bp;
            if (!*once) {
                *once = 1;
                host_capture_dxt1_content(vm_base + dst, (uint32_t)outlen,
                                          1280, 720, 128, fname, after);
            }
        }
    }

    ctx->gpr[3] = (int64_t)(int32_t)(1); /* Z_STREAM_END */
    return 1;
}
