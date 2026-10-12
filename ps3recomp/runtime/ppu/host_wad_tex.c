/*
 * Host decode of GoW2 WAD ~texture packages + optional native RSX bind.
 *
 * Runtime layout (T1R buf): body starts at buf (no 24-byte name). Stream
 * size still includes the name → body length = size-24. Header is BE:
 *   +8 data_size, +24 mip_bytes, +36 dim=(w<<16)|h
 * Pixel data is A8R8G8B8 Morton-swizzled (RSX); we deswizzle with
 * rsx_deswizzle_texels then optionally force a native bind_texture
 * with dimensions ≠ 1280×720.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* getenv */

#include "rsx_texture_deswizzle.h"
#include "rsx_host_content.h"

extern uint8_t* vm_base;

static uint32_t be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int env_on(const char* k)
{
    const char* v = getenv(k);
    return v && v[0] && v[0] != '0';
}

/* ARGB8 (A,R,G,B bytes) → RGBA for PPM / content hold. */
static void argb_to_rgba(const uint8_t* src, uint8_t* dst, uint32_t n_pix)
{
    for (uint32_t i = 0; i < n_pix; i++) {
        dst[i * 4 + 0] = src[i * 4 + 1];
        dst[i * 4 + 1] = src[i * 4 + 2];
        dst[i * 4 + 2] = src[i * 4 + 3];
        dst[i * 4 + 3] = src[i * 4 + 0];
    }
}

static int write_ppm(const char* path, const uint8_t* rgba, uint32_t w, uint32_t h)
{
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* row = rgba + (size_t)y * w * 4;
        for (uint32_t x = 0; x < w; x++)
            fwrite(row + x * 4, 1, 3, f);
    }
    fclose(f);
    return 0;
}

/* Neighbor coherence score — lower is better for natural textures. */
static double neighbor_score(const uint8_t* rgba, uint32_t w, uint32_t h)
{
    double s = 0.0;
    uint32_t n = 0;
    uint32_t step = w > 64 ? w / 64 : 1;
    for (uint32_t y = 0; y < h; y += step) {
        for (uint32_t x = 0; x + 1 < w; x += step) {
            size_t i = ((size_t)y * w + x) * 4;
            size_t j = i + 4;
            s += abs((int)rgba[i] - (int)rgba[j]) +
                 abs((int)rgba[i + 1] - (int)rgba[j + 1]) +
                 abs((int)rgba[i + 2] - (int)rgba[j + 2]);
            n++;
        }
    }
    return n ? s / (double)n : 1e9;
}

/* Decode base level. Prefer RSX Morton deswizzle (proven for GoW2 ~tex);
 * only fall back to linear if RSX path is almost empty. Score picks best hdr. */
static uint8_t* decode_base_rgba(const uint8_t* body, uint32_t pay_len,
                                 uint32_t w, uint32_t h, uint32_t* out_hdr,
                                 int* out_swizzled, double* out_score)
{
    uint32_t base_bytes = w * h * 4u;
    uint8_t* best_rgba = NULL;
    double best_sc = 1e18;
    uint32_t best_hdr = 48;
    int best_swz = 1;
    uint64_t best_nz = 0;

    static const uint32_t hdrs[] = { 48, 80, 64, 96, 128, 0 };
    for (int hi = 0; hdrs[hi] || hi == 0; hi++) {
        uint32_t hdr = hdrs[hi];
        if (hdr == 0 && hi > 0) break;
        if (pay_len < hdr + base_bytes) continue;

        const uint8_t* raw = body + hdr;
        uint8_t* linear = (uint8_t*)malloc(base_bytes);
        uint8_t* rgba = (uint8_t*)malloc(base_bytes);
        if (!linear || !rgba) { free(linear); free(rgba); continue; }

        /* Primary: RSX Morton deswizzle + ARGB → RGBA */
        rsx_deswizzle_texels(raw, linear, w, h, 4);
        argb_to_rgba(linear, rgba, w * h);
        uint64_t nz = 0;
        for (uint64_t i = 0; i < (uint64_t)w * h; i++)
            if (rgba[i * 4] | rgba[i * 4 + 1] | rgba[i * 4 + 2]) nz++;
        double sc = neighbor_score(rgba, w, h);
        /* Prefer more nonzero when scores are within 15% */
        int better = 0;
        if (!best_rgba) better = 1;
        else if (sc < best_sc * 0.85) better = 1;
        else if (sc <= best_sc * 1.15 && nz > best_nz + (uint64_t)w * h / 20)
            better = 1;
        else if (sc < best_sc && nz >= best_nz) better = 1;
        if (better) {
            free(best_rgba);
            best_rgba = rgba;
            rgba = NULL;
            best_sc = sc;
            best_hdr = hdr;
            best_swz = 1;
            best_nz = nz;
        }

        free(linear);
        free(rgba);
    }

    /* Linear fallback only if RSX produced almost nothing. */
    if (best_rgba && best_nz < (uint64_t)w * h / 100u) {
        for (int hi = 0; hdrs[hi] || hi == 0; hi++) {
            uint32_t hdr = hdrs[hi];
            if (hdr == 0 && hi > 0) break;
            if (pay_len < hdr + base_bytes) continue;
            uint8_t* rgba = (uint8_t*)malloc(base_bytes);
            if (!rgba) continue;
            argb_to_rgba(body + hdr, rgba, w * h);
            uint64_t nz = 0;
            for (uint64_t i = 0; i < (uint64_t)w * h; i++)
                if (rgba[i * 4] | rgba[i * 4 + 1] | rgba[i * 4 + 2]) nz++;
            double sc = neighbor_score(rgba, w, h);
            if (nz > best_nz && sc < best_sc * 2.0) {
                free(best_rgba);
                best_rgba = rgba;
                best_sc = sc;
                best_hdr = hdr;
                best_swz = 0;
                best_nz = nz;
            } else {
                free(rgba);
            }
        }
    }

    if (out_hdr) *out_hdr = best_hdr;
    if (out_swizzled) *out_swizzled = best_swz;
    if (out_score) *out_score = best_sc;
    return best_rgba;
}

void ps3_host_wad_tex_capture(const char* name, uint32_t buf, uint32_t size)
{
    static int n_ok;
    static int n_try;
    n_try++;

    if (!vm_base || !buf || size < 64 || size > 0x800000u) return;
    if (buf >= 0xFFF00000u) return;
    if (n_ok >= 8) return;

    const uint8_t* p = vm_base + buf;
    uint32_t pay_len = (size > 24) ? (size - 24) : size;
    if (pay_len < 64) return;

    uint32_t data_size = be32(p + 8);
    uint32_t mip_size  = be32(p + 24);
    uint32_t dim_be    = be32(p + 36);
    uint32_t w = (dim_be >> 16) & 0xFFFFu;
    uint32_t h = dim_be & 0xFFFFu;

    if (w < 4 || h < 4 || w > 2048 || h > 2048) {
        static const uint32_t ks[] = { 64, 128, 256, 512, 1024, 0 };
        for (int i = 0; ks[i]; i++) {
            uint32_t side = ks[i], chain = 0, s = side;
            for (;;) {
                chain += s * s * 4u;
                if (s == 1) break;
                s >>= 1;
            }
            if (chain == mip_size || chain == data_size) {
                w = h = side;
                break;
            }
        }
    }
    if (w < 4 || h < 4 || w > 2048 || h > 2048) return;

    uint32_t hdr = 48;
    int swz = 1;
    double sc = 0;
    uint8_t* rgba = decode_base_rgba(p, pay_len, w, h, &hdr, &swz, &sc);
    if (!rgba) return;

    uint64_t nz = 0;
    for (uint64_t i = 0; i < (uint64_t)w * h; i++)
        if (rgba[i * 4] | rgba[i * 4 + 1] | rgba[i * 4 + 2]) nz++;

    fprintf(stderr,
            "[WADTEX] #%d name='%s' %ux%u hdr=%u swz=%s score=%.2f nz=%llu/%u "
            "buf=0x%08X size=%u\n",
            n_ok + 1, name ? name : "?", w, h, hdr,
            swz ? "rsx" : "linear", sc,
            (unsigned long long)nz, w * h, buf, size);
    fflush(stderr);

    if (nz > 16) {
        char path[64];
        snprintf(path, sizeof path, "frame_wad_tex_%02d.ppm", n_ok);
        if (write_ppm(path, rgba, w, h) == 0) {
            fprintf(stderr, "[PIXELS] wrote %s %ux%u (WAD texture)\n", path, w, h);
            fflush(stderr);
        }

        /* Native bind_texture ≠ 1280×720 (opt-out with PS3_WAD_TEX_BIND=0). */
        if (!getenv("PS3_WAD_TEX_BIND") || env_on("PS3_WAD_TEX_BIND")) {
            /* unit 1+ so movie unit0 stays free; first capture binds unit1 */
            unsigned unit = 1u + (unsigned)(n_ok % 7);
            rsx_host_force_bind_rgba(unit, rgba, w, h);
        }

        if (env_on("PS3_WAD_TEX_HOLD")) {
            rsx_host_content_hold(rgba, w, h);
            fprintf(stderr, "[WADTEX] content hold armed with WAD tex\n");
            fflush(stderr);
        }
        n_ok++;
    }

    free(rgba);
}
