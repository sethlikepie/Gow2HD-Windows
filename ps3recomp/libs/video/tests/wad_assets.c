/*
 * ps3recomp - extraccao nativa de assets dos WADs do GoW2 -- ver wad_assets.h.
 *
 * Ports C clean-room, byte-fieis, de:
 *   gen_wad_mesh_rxs.py  (malha: directoria 16B, streams +12, POS0 (x,y,z,w)
 *                         f32 BE, TEX0 half BE, descritor cnt@+12 + lista de
 *                         triangulos u32 BE, filtro de degenerados/invalidos)
 *   decode_hero_tex.py   (textura: framing [type][sz LE][nome 24B][corpo],
 *                         header BE +8/+24/+36, candidatos de sub-header com
 *                         score de coerencia h+v, ARGB8 -> RGBA)
 * O deswizzle Morton vem do proprio engine: rsx_texture_deswizzle.h.
 */
#include "wad_assets.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rsx_texture_deswizzle.h"

#define STREAM_HDR 12u   /* header/sentinela no inicio de cada stream de vertice */

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static float bef32(const uint8_t* p) {
    uint32_t u = be32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
/* half BE -> float (mesma formula do half() do gen_wad_mesh_rxs.py) */
static float behalf(const uint8_t* p) {
    uint16_t h = (uint16_t)((p[0] << 8) | p[1]);
    uint32_t s = (h >> 15) & 1, e = (h >> 10) & 0x1f, m = h & 0x3ff;
    float v;
    if (e == 0)       v = ldexpf((float)m / 1024.0f, -14);
    else if (e == 31) v = NAN;
    else              v = ldexpf(1.0f + (float)m / 1024.0f, (int)e - 15);
    return s ? -v : v;
}

/* ------------------------------------------------------------- directoria -- */
typedef struct { uint32_t off, sz; int present; } dirent_t;
typedef struct { dirent_t modl, pos0, tex0, boni; } wad_dir_t;

static int parse_dir(const uint8_t* wad, size_t len, uint32_t modl_off, wad_dir_t* d) {
    memset(d, 0, sizeof *d);
    size_t o = modl_off;
    for (int k = 0; k < 8; k++) {
        if (o + 16 > len) break;
        const uint8_t* t = wad + o;
        if (!isalnum(t[0]) || !isalnum(t[1]) || !isalnum(t[2]) || !isalnum(t[3])) break;
        uint32_t off = be32(wad + o + 8), sz = be32(wad + o + 12);
        dirent_t* e = NULL;
        if (!memcmp(t, "MODL", 4)) e = &d->modl;
        else if (!memcmp(t, "POS0", 4)) e = &d->pos0;
        else if (!memcmp(t, "TEX0", 4)) e = &d->tex0;
        else if (!memcmp(t, "BONI", 4)) e = &d->boni;
        if (e) { e->off = off; e->sz = sz; e->present = 1; }
        o += 16;
        if (!memcmp(t, "BONI", 4)) break;
    }
    return (d->modl.present && d->pos0.present) ? 0 : -1;
}

/* ------------------------------------------------------------------ malha -- */
int wad_mesh_extract(const uint8_t* wad, size_t len, uint32_t modl_off, wad_mesh_t* out) {
    memset(out, 0, sizeof *out);
    wad_dir_t d;
    if (!wad || modl_off >= len || parse_dir(wad, len, modl_off, &d) != 0) return -1;

    uint32_t n = d.pos0.sz / 16;
    size_t pbase = (size_t)modl_off + d.pos0.off + STREAM_HDR;
    if (n == 0 || pbase + (size_t)(n - 1) * 16 + 12 > len) return -1;

    float* vtx5 = malloc((size_t)n * 5 * sizeof(float));
    uint8_t* valid = malloc(n);
    if (!vtx5 || !valid) { free(vtx5); free(valid); return -1; }

    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* r = wad + pbase + (size_t)i * 16;
        float x = bef32(r), y = bef32(r + 4), z = bef32(r + 8);
        int ok = x == x && y == y && z == z &&
                 fabsf(x) < 1e4f && fabsf(y) < 1e4f && fabsf(z) < 1e4f;
        valid[i] = (uint8_t)ok;
        vtx5[i * 5 + 0] = ok ? x : 0.0f;
        vtx5[i * 5 + 1] = ok ? y : 0.0f;
        vtx5[i * 5 + 2] = ok ? z : 0.0f;
        vtx5[i * 5 + 3] = 0.0f;
        vtx5[i * 5 + 4] = 0.0f;
    }
    if (d.tex0.present) {
        size_t tbase = (size_t)modl_off + d.tex0.off + STREAM_HDR;
        if (tbase + (size_t)n * 4 <= len) {
            for (uint32_t i = 0; i < n; i++) {
                float u = behalf(wad + tbase + (size_t)i * 4);
                float v = behalf(wad + tbase + (size_t)i * 4 + 2);
                if (u != u || v != v) { u = 0.0f; v = 0.0f; }
                vtx5[i * 5 + 3] = u;
                vtx5[i * 5 + 4] = v;
            }
        }
    }

    /* descritor: cnt u32 BE em +12, depois a lista de indices */
    size_t db = (size_t)modl_off + d.modl.off;
    if (db + 16 > len) { free(vtx5); free(valid); return -1; }
    uint32_t cnt = be32(wad + db + 12);
    size_t ib = db + 16;
    if (cnt < 3 || ib + (size_t)cnt * 4 > len) { free(vtx5); free(valid); return -1; }

    uint32_t max_tris = cnt / 3;
    uint32_t* idx = malloc((size_t)max_tris * 3 * sizeof(uint32_t));
    if (!idx) { free(vtx5); free(valid); return -1; }
    uint32_t nt = 0;
    for (uint32_t w = 0; w + 2 < cnt; w += 3) {
        uint32_t a = be32(wad + ib + (size_t)w * 4);
        uint32_t b = be32(wad + ib + (size_t)(w + 1) * 4);
        uint32_t c = be32(wad + ib + (size_t)(w + 2) * 4);
        if (a == b || b == c || a == c) continue;
        if (a >= n || b >= n || c >= n) continue;
        if (!valid[a] || !valid[b] || !valid[c]) continue;
        idx[nt * 3 + 0] = a;
        idx[nt * 3 + 1] = b;
        idx[nt * 3 + 2] = c;
        nt++;
    }
    free(valid);
    if (nt == 0) { free(vtx5); free(idx); return -1; }

    out->nverts = n;
    out->vtx5 = vtx5;
    out->ntris = nt;
    out->idx = idx;
    return 0;
}

void wad_mesh_free(wad_mesh_t* m) {
    if (!m) return;
    free(m->vtx5);
    free(m->idx);
    memset(m, 0, sizeof *m);
}

/* ---------------------------------------------------------------- textura -- */
static uint32_t mip_chain_bytes(uint32_t w, uint32_t h, uint32_t bpp) {
    uint32_t total = 0, cw = w, ch = h;
    for (;;) {
        total += cw * ch * bpp;
        if (cw == 1 && ch == 1) break;
        cw = cw > 1 ? cw / 2 : 1;
        ch = ch > 1 ? ch / 2 : 1;
    }
    return total;
}

static void argb_to_rgba(const uint8_t* argb, uint8_t* rgba, size_t texels) {
    for (size_t i = 0; i < texels; i++) {
        rgba[i * 4 + 0] = argb[i * 4 + 1];
        rgba[i * 4 + 1] = argb[i * 4 + 2];
        rgba[i * 4 + 2] = argb[i * 4 + 3];
        rgba[i * 4 + 3] = argb[i * 4 + 0];
    }
}

static size_t nonzero_count(const uint8_t* rgba, uint32_t w, uint32_t h) {
    size_t nz = 0, n = (size_t)w * h;
    for (size_t i = 0; i < n; i++)
        if (rgba[i * 4] | rgba[i * 4 + 1] | rgba[i * 4 + 2]) nz++;
    return nz;
}

/* score de coerencia h+v -- port exacto do neighbor_score() (h: passo w/64,
 * ambos os eixos amostrados no mesmo passo; v: passo h/64) */
static double neighbor_score(const uint8_t* rgba, uint32_t w, uint32_t h) {
    uint32_t step_x = w / 64 ? w / 64 : 1;
    double sh = 0.0;
    size_t nh = 0;
    for (uint32_t y = 0; y < h; y += step_x) {
        size_t row = (size_t)y * w;
        for (uint32_t x = 0; x + step_x < w; x += step_x) {
            const uint8_t* a = rgba + (row + x) * 4;
            const uint8_t* b = rgba + (row + x + step_x) * 4;
            sh += abs((int)a[0] - b[0]) + abs((int)a[1] - b[1]) + abs((int)a[2] - b[2]);
            nh++;
        }
    }
    uint32_t step_y = h / 64 ? h / 64 : 1;
    double sv = 0.0;
    size_t nv = 0;
    for (uint32_t x = 0; x < w; x += step_y) {
        for (uint32_t y = 0; y + step_y < h; y += step_y) {
            const uint8_t* a = rgba + ((size_t)y * w + x) * 4;
            const uint8_t* b = rgba + ((size_t)(y + step_y) * w + x) * 4;
            sv += abs((int)a[0] - b[0]) + abs((int)a[1] - b[1]) + abs((int)a[2] - b[2]);
            nv++;
        }
    }
    return (nh ? sh / (double)nh : 1e9) + (nv ? sv / (double)nv : 1e9);
}

/* nivel base: busca de sub-header {derivado, 48..256} x {swizzled, linear},
 * fica com o de melhor score (port de decode_base_level()) */
static uint8_t* decode_base_level(const uint8_t* payload, size_t plen,
                                  uint32_t w, uint32_t h) {
    size_t base_bytes = (size_t)w * h * 4;
    size_t padded_bytes = (size_t)rsx_morton_padded_texels(w, h) * 4;

    long cands[9];
    int ncand = 0;
    long derived = (long)plen - (long)mip_chain_bytes(w, h, 4);
    if (derived >= 0 && derived <= 4096) cands[ncand++] = derived;
    static const long fixed[] = { 48, 64, 80, 96, 128, 152, 256 };
    for (size_t i = 0; i < sizeof fixed / sizeof *fixed; i++) {
        int dup = 0;
        for (int j = 0; j < ncand; j++) dup |= (cands[j] == fixed[i]);
        if (!dup) cands[ncand++] = fixed[i];
    }

    uint8_t* lin = malloc(base_bytes);
    uint8_t* rgba = malloc(base_bytes);
    uint8_t* best = NULL;
    double best_score = 0.0;
    if (!lin || !rgba) { free(lin); free(rgba); return NULL; }

    for (int ci = 0; ci < ncand; ci++) {
        long hdr = cands[ci];
        if (hdr < 0 || (size_t)hdr + base_bytes > plen) continue;
        const uint8_t* raw = payload + hdr;
        for (int swiz = 1; swiz >= 0; swiz--) {
            const uint8_t* argb;
            if (swiz) {
                if ((size_t)hdr + padded_bytes > plen) continue;
                rsx_deswizzle_texels(raw, lin, w, h, 4);
                argb = lin;
            } else {
                argb = raw;
            }
            argb_to_rgba(argb, rgba, (size_t)w * h);
            if (nonzero_count(rgba, w, h) < (size_t)w * h / 100) continue;
            double sc = neighbor_score(rgba, w, h);
            if (!best || sc < best_score) {
                if (!best) best = malloc(base_bytes);
                if (!best) break;
                memcpy(best, rgba, base_bytes);
                best_score = sc;
            }
        }
    }
    free(lin);
    free(rgba);
    return best;
}

int wad_skin_extract(const uint8_t* wad, size_t len, uint32_t modl_off, wad_skin_t* out) {
    memset(out, 0, sizeof *out);
    wad_dir_t d;
    if (!wad || modl_off >= len || parse_dir(wad, len, modl_off, &d) != 0) return -1;
    if (!d.boni.present) return -1;

    uint32_t n = d.pos0.sz / 16;
    size_t pbase = (size_t)modl_off + d.pos0.off + STREAM_HDR;
    size_t bbase = (size_t)modl_off + d.boni.off + STREAM_HDR;
    if (n == 0 || pbase + (size_t)(n - 1) * 16 + 16 > len ||
        bbase + (size_t)n * 4 > len)
        return -1;

    out->b0 = malloc(n);
    out->b1 = malloc(n);
    out->w = malloc((size_t)n * sizeof(float));
    if (!out->b0 || !out->b1 || !out->w) { wad_skin_free(out); return -1; }

    for (uint32_t i = 0; i < n; i++) {
        out->b0[i] = wad[bbase + (size_t)i * 4];
        out->b1[i] = wad[bbase + (size_t)i * 4 + 1];
        float w = bef32(wad + pbase + (size_t)i * 16 + 12);
        out->w[i] = (w == w) ? w : 1.0f;
    }
    out->nverts = n;
    return 0;
}

void wad_skin_free(wad_skin_t* s) {
    if (!s) return;
    free(s->b0);
    free(s->b1);
    free(s->w);
    memset(s, 0, sizeof *s);
}

/* ------------------------------------------------------------- rig real -- */
static float lef32(const uint8_t* p) {
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

/* Localiza a tabela de nomes do esqueleto: acha "pelvis" (bone 2) com
 * "neck" em +4*24 e "head" em +5*24. Devolve offset do bone 0 ("main"). */
static long find_skel_names(const uint8_t* wad, size_t len) {
    static const char P[] = "pelvis";
    for (size_t i = 0; i + 6 * 24 < len; i++) {
        const uint8_t* hit = memmem(wad + i, len - i, P, sizeof P);   /* inclui NUL */
        if (!hit) return -1;
        size_t o = (size_t)(hit - wad);
        i = o;
        if (o < 2 * 24) continue;
        size_t base = o - 2 * 24;
        if (base + 8 * 24 > len) continue;
        if (!memcmp(wad + base + 6 * 24, "neck", 5) && !memcmp(wad + base + 7 * 24, "head", 5))
            return (long)base;
    }
    return -1;
}

int wad_rig_extract(const uint8_t* wad, size_t len, uint32_t modl_off, wad_rig_t* out) {
    memset(out, 0, sizeof *out);
    out->nbones = 0;

    long names = find_skel_names(wad, len);
    if (names < 0) { fprintf(stderr, "[rig] esqueleto nao encontrado (ancora pelvis/neck/head)\n"); return -1; }

    /* conta bones: nomes ASCII validos consecutivos a partir de "main" */
    uint32_t nb = 0;
    while (nb < WAD_RIG_MAX_BONES) {
        const uint8_t* nm = wad + names + (size_t)nb * 24;
        size_t l = strnlen((const char*)nm, 24);
        int ok = l >= 2 && l < 24;
        for (size_t k = 0; ok && k < l; k++)
            if (nm[k] < 32 || nm[k] >= 127) ok = 0;
        if (!ok) break;
        nb++;
    }
    if (nb < 8) { fprintf(stderr, "[rig] tabela de nomes curta (%u)\n", nb); return -1; }
    /* ultimo nome pode ser lixo/terminador ("0000{"): descarta se nao-alfa */
    if (!(((wad + names)[(nb - 1) * 24] | 32) >= 'a' && (((wad + names)[(nb - 1) * 24]) | 32) <= 'z'))
        nb--;

    size_t hier = (size_t)names - (size_t)(nb + 1) * 16;  /* records p/ nb+1 entradas (inclui terminador) */
    /* hierarquia: record i em names - (N)*16 ... na pratica records comecam
     * imediatamente antes dos nomes com stride 16, record i <-> bone i.
     * Recalcula pelo self-id: procura o record cujo u16 self em +4 == 1
     * imediatamente antes dos nomes. */
    hier = (size_t)names - (size_t)nb * 16;
    {
        /* valida: para bone 3 (vertebrae2), parent deve ser 2 (pelvis) */
        const uint8_t* r3 = wad + hier + 3 * 16;
        uint16_t self3 = (uint16_t)(r3[4] | (r3[5] << 8));
        uint16_t par3 = (uint16_t)(r3[8] | (r3[9] << 8));
        if (self3 != 3 || par3 + 1 != 2) {
            /* tenta com um record extra de header antes (observado: main tem
             * flags proprios) -- desloca uma entrada */
            hier -= 16;
            r3 = wad + hier + 3 * 16;
            self3 = (uint16_t)(r3[4] | (r3[5] << 8));
            par3 = (uint16_t)(r3[8] | (r3[9] << 8));
            if (self3 != 3 || par3 + 1 != 2) {
                fprintf(stderr, "[rig] hierarquia nao validou (self=%u par=%u)\n", self3, par3);
                return -1;
            }
        }
    }
    for (uint32_t i = 0; i < nb; i++) {
        const uint8_t* r = wad + hier + (size_t)i * 16;
        uint16_t par = (uint16_t)(r[8] | (r[9] << 8));
        out->parent[i] = (i == 0) ? -1 : (par == 0xFFFF ? 0 : (int16_t)(par + 1));
        if (i > 0 && (out->parent[i] < 0 || (uint32_t)out->parent[i] >= i + 64)) {
            fprintf(stderr, "[rig] parent invalido bone %u -> %d\n", i, out->parent[i]);
            return -1;
        }
    }

    /* matrizes bind locais: apos os nomes ((nb+1) entradas de 24B) + 0x1c */
    size_t bind = (size_t)names + (size_t)(nb + 1) * 24 + 0x1c;
    if (bind + (size_t)nb * 64 > len) { fprintf(stderr, "[rig] bind fora do wad\n"); return -1; }
    out->local_bind = malloc((size_t)nb * 16 * sizeof(float));
    for (uint32_t i = 0; i < nb; i++)
        for (int k = 0; k < 16; k++)
            out->local_bind[i * 16 + k] = lef32(wad + bind + (size_t)i * 64 + (size_t)k * 4);
    /* sanity: main ~ identidade */
    if (fabsf(out->local_bind[0] - 1.0f) > 1e-3f || fabsf(out->local_bind[15] - 1.0f) > 1e-3f) {
        fprintf(stderr, "[rig] bind[main] nao e identidade (m0=%f)\n", out->local_bind[0]);
        free(out->local_bind);
        out->local_bind = NULL;
        return -1;
    }
    out->names = malloc((size_t)nb * 24);
    memcpy(out->names, wad + names, (size_t)nb * 24);
    out->nbones = nb;

    /* ---- segmentos -> bone global por vertice ---- */
    wad_dir_t dd;
    if (parse_dir(wad, len, modl_off, &dd) != 0 || !dd.boni.present) goto fail;
    uint32_t n = dd.pos0.sz / 16;
    size_t db = (size_t)modl_off + dd.modl.off;
    uint32_t cnt = be32(wad + db + 12);
    size_t segp = db + 16 + (size_t)cnt * 4 + 8 + 24;   /* pula {5,16}+bbox */
    size_t bbase = (size_t)modl_off + dd.boni.off + STREAM_HDR;
    size_t pbase = (size_t)modl_off + dd.pos0.off + STREAM_HDR;

    out->g0 = malloc(n);
    out->g1 = malloc(n);
    out->w = malloc((size_t)n * sizeof(float));
    uint8_t* vval = malloc(n);   /* validade de posicao (mesmo criterio do mesh) */
    if (!out->g0 || !out->g1 || !out->w || !vval) { free(vval); goto fail; }
    memset(out->g0, 0xFF, n);
    memset(out->g1, 0xFF, n);
    for (uint32_t v = 0; v < n; v++) {
        const uint8_t* r = wad + pbase + (size_t)v * 16;
        float x = bef32(r), y = bef32(r + 4), z = bef32(r + 8);
        vval[v] = x == x && y == y && z == z &&
                  fabsf(x) < 1e4f && fabsf(y) < 1e4f && fabsf(z) < 1e4f;
    }
    size_t ibx = db + 16;
    uint32_t ftri_cursor = 0;    /* triangulos filtrados acumulados */

    uint64_t sumv = 0, sumi = 0;
    int nseg = 0;
    while (segp + 23 + 24 <= len) {
        const uint8_t* h = wad + segp;
        if (h[0] != 0 || h[2] != 0) break;
        uint32_t fv = be32(h + 3), nv = be32(h + 7);
        uint32_t fi = be32(h + 11), ni = be32(h + 15);
        uint32_t nbp = be32(h + 19);
        uint8_t sub = h[1];
        if (nbp == 0 || nbp > 16 || nv == 0 || nv > n) break;
        const uint8_t* bl = h + 23;
        int good = 1;
        uint32_t pal[16];
        for (uint32_t k = 0; k < nbp; k++) {
            pal[k] = be32(bl + (size_t)k * 4);
            if (pal[k] >= nb) good = 0;
        }
        if (!good) break;
        for (uint32_t v = fv; v < fv + nv && v < n; v++) {
            uint8_t b0 = wad[bbase + (size_t)v * 4];
            uint8_t b1 = wad[bbase + (size_t)v * 4 + 1];
            if (b0 >= nbp || b1 >= nbp) {
                fprintf(stderr, "[rig] seg%d: b0/b1 %u/%u >= paleta %u no vert %u\n",
                        nseg, b0, b1, nbp, v);
                free(vval);
                goto fail;
            }
            out->g0[v] = (uint8_t)pal[b0];
            out->g1[v] = (uint8_t)pal[b1];
            float wv = bef32(wad + pbase + (size_t)v * 16 + 12);
            out->w[v] = (wv == wv) ? wv : 1.0f;
        }
        /* triangulos filtrados deste segmento (mesmo criterio do mesh) */
        uint32_t ftris = 0;
        for (uint32_t wdx = fi; wdx + 2 < fi + ni && wdx + 2 < cnt; wdx += 3) {
            uint32_t a3 = be32(wad + ibx + (size_t)wdx * 4);
            uint32_t b3 = be32(wad + ibx + (size_t)(wdx + 1) * 4);
            uint32_t c3 = be32(wad + ibx + (size_t)(wdx + 2) * 4);
            if (a3 == b3 || b3 == c3 || a3 == c3) continue;
            if (a3 >= n || b3 >= n || c3 >= n) continue;
            if (!vval[a3] || !vval[b3] || !vval[c3]) continue;
            ftris++;
        }
        if (out->ngroups > 0 && out->grp_sub[out->ngroups - 1] == sub) {
            out->grp_count[out->ngroups - 1] += ftris;
        } else if (out->ngroups < 8) {
            out->grp_first[out->ngroups] = ftri_cursor;
            out->grp_count[out->ngroups] = ftris;
            out->grp_sub[out->ngroups] = sub;
            out->ngroups++;
        }
        ftri_cursor += ftris;

        sumv += nv;
        sumi += ni;
        nseg++;
        segp += 23 + (size_t)nbp * 4 + 24;
    }
    free(vval);
    if (sumv != n || sumi != cnt) {
        fprintf(stderr, "[rig] invariantes falharam: sumv=%llu (n=%u) sumi=%llu (cnt=%u) nseg=%d\n",
                (unsigned long long)sumv, n, (unsigned long long)sumi, cnt, nseg);
        goto fail;
    }
    out->nverts = n;
    fprintf(stderr, "[rig] %u bones, %d segmentos, %u verts mapeados (invariantes OK)\n", nb, nseg, n);
    return 0;

fail:
    wad_rig_free(out);
    return -1;
}

void wad_rig_free(wad_rig_t* r) {
    if (!r) return;
    free(r->local_bind);
    free(r->names);
    free(r->g0);
    free(r->g1);
    free(r->w);
    memset(r, 0, sizeof *r);
}

int wad_rig_find_bone(const wad_rig_t* r, const char* name) {
    for (uint32_t i = 0; i < r->nbones; i++)
        if (!strncmp(r->names + (size_t)i * 24, name, 24)) return (int)i;
    return -1;
}

int wad_find_modls(const uint8_t* wad, size_t len, uint32_t* out, int max) {
    int n = 0;
    size_t i = 0;
    while (n < max && i + 32 < len) {
        const uint8_t* hit = memmem(wad + i, len - i, "MODL", 4);
        if (!hit) break;
        size_t o = (size_t)(hit - wad);
        if (o + 32 <= len && !memcmp(wad + o + 16, "POS0", 4))
            out[n++] = (uint32_t)o;
        i = o + 1;
    }
    return n;
}

int wad_tex_decode(const uint8_t* wad, size_t len, const char* tag,
                   uint8_t** rgba_out, uint32_t* w_out, uint32_t* h_out) {
    *rgba_out = NULL;
    if (!wad || !tag || !*tag) return -1;
    size_t taglen = strlen(tag);
    if (taglen > 24) return -1;

    size_t start = 0;
    while (start + taglen <= len) {
        const uint8_t* hit = memmem(wad + start, len - start, tag, taglen);
        if (!hit) break;
        size_t i = (size_t)(hit - wad);
        start = i + 1;
        if (i < 4 || i + 24 > len) continue;
        uint32_t sz = le32(wad + i - 4);
        if (sz < 64) continue;   /* entrada de manifesto (so nome), como no Python */

        size_t rec_end = i + sz;
        if (rec_end > len) rec_end = len;
        const uint8_t* payload = wad + i + 24;
        size_t plen = rec_end - (i + 24);
        if (plen < 40) continue;

        uint32_t data_size = be32(payload + 8);
        uint32_t mip_size  = be32(payload + 24);
        uint32_t dim       = be32(payload + 36);
        uint32_t w = (dim >> 16) & 0xFFFF, h = dim & 0xFFFF;
        if (w < 4 || h < 4 || w > 2048 || h > 2048 || mip_chain_bytes(w, h, 4) != mip_size) {
            int rec = 0;   /* recuperacao de dim quadrada POT (mesma do Python) */
            static const uint32_t sides[] = { 32, 64, 128, 256, 512, 1024 };
            for (size_t s = 0; s < sizeof sides / sizeof *sides; s++) {
                uint32_t mc = mip_chain_bytes(sides[s], sides[s], 4);
                if (mc == mip_size || mc == data_size) { w = h = sides[s]; rec = 1; break; }
            }
            if (!rec) continue;
        }
        uint8_t* rgba = decode_base_level(payload, plen, w, h);
        if (rgba) {
            *rgba_out = rgba;
            *w_out = w;
            *h_out = h;
            return 0;
        }
    }
    return -1;
}
