#include "host_prompt_mesh.h"

#include <math.h>
#include <string.h>

static void add_tri(host_prompt_mesh* out, float ax, float ay, float bx, float by,
                    float cx, float cy)
{
    const float tri[3][2] = { { ax, ay }, { bx, by }, { cx, cy } };

    if (out->count + 3 > HOST_PROMPT_MESH_MAX_VERTS)
        return;
    memcpy(&out->xy[out->count], tri, sizeof(tri));
    out->count += 3;
}

/* Stroke growth of the glyph being built (normalized units): outline layers
 * of the animated buttons thicken every stroke in place instead of scaling
 * the whole glyph, which would shift the strokes apart. */
static float s_grow;

static void add_quad_raw(host_prompt_mesh* out, float ax, float ay, float bx, float by,
                         float cx, float cy, float dx, float dy)
{
    add_tri(out, ax, ay, bx, by, cx, cy);
    add_tri(out, ax, ay, cx, cy, dx, dy);
}

/* A stroke quad: with s_grow, each corner moves outward from the centroid. */
static void add_quad(host_prompt_mesh* out, float ax, float ay, float bx, float by,
                     float cx, float cy, float dx, float dy)
{
    if (s_grow > 0.0f) {
        const float mx = (ax + bx + cx + dx) * 0.25f, my = (ay + by + cy + dy) * 0.25f;
        float* p[4][2] = { { &ax, &ay }, { &bx, &by }, { &cx, &cy }, { &dx, &dy } };
        for (unsigned i = 0; i < 4u; i++) {
            const float vx = *p[i][0] - mx, vy = *p[i][1] - my;
            *p[i][0] += (vx > 0.0f ? s_grow : vx < 0.0f ? -s_grow : 0.0f);
            *p[i][1] += (vy > 0.0f ? s_grow : vy < 0.0f ? -s_grow : 0.0f);
        }
    }
    add_quad_raw(out, ax, ay, bx, by, cx, cy, dx, dy);
}

static void add_rect(host_prompt_mesh* out, float left, float top,
                     float right, float bottom)
{
    add_quad(out, left, top, right, top, right, bottom, left, bottom);
}

/* Filled rounded rectangle: center column, side bands and 4-step corner fans. */
static void add_rounded_rect(host_prompt_mesh* out, float left, float top,
                             float right, float bottom, float r)
{
    static const float cs[5] = { 1.0f, 0.9239f, 0.7071f, 0.3827f, 0.0f };
    static const float sn[5] = { 0.0f, 0.3827f, 0.7071f, 0.9239f, 1.0f };
    const float cx[4] = { left + r, right - r, right - r, left + r };
    const float cy[4] = { top + r, top + r, bottom - r, bottom - r };
    const float dx[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
    const float dy[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
    unsigned c, k;

    add_quad_raw(out, left + r, top, right - r, top, right - r, bottom, left + r, bottom);
    add_quad_raw(out, left, top + r, left + r, top + r, left + r, bottom - r, left, bottom - r);
    add_quad_raw(out, right - r, top + r, right, top + r, right, bottom - r, right - r, bottom - r);
    for (c = 0; c < 4u; c++)
        for (k = 0; k < 4u; k++)
            add_tri(out, cx[c], cy[c],
                    cx[c] + dx[c] * r * cs[k],     cy[c] + dy[c] * r * sn[k],
                    cx[c] + dx[c] * r * cs[k + 1], cy[c] + dy[c] * r * sn[k + 1]);
}

/*
 * Stroked vector glyphs. Every letter is a few polylines in a design box
 * (x 0..1 left->right, y 0..1 top->bottom), drawn as thick strokes with round
 * joins and caps, so curves stay smooth and the outline/rim layers of the
 * animated buttons are the SAME path with a wider stroke (always aligned).
 * Geometry is built in "screen" units (x scaled by the bounds' aspect) so
 * round joins stay round on screen, then mapped back to the 0..1 bounds.
 */
#define GLYPH_H      0.64f      /* glyph height, fraction of the bounds */
#define GLYPH_STROKE 0.10f      /* stroke width, fraction of the bounds height */
#define PATH_END     { -1.0f, -1.0f }

typedef struct { float x, y; } gpt;
typedef struct { float ox, oy, sx, sy, aspect; } gbox;   /* design -> bounds */

static void to_bounds(const gbox* b, float dx, float dy, float* x, float* y)
{
    *x = b->ox + dx * b->sx;
    *y = b->oy + dy * b->sy;
}

/* Filled disc of radius r (in height units) centred at bounds point (cx,cy). */
static void add_disc(host_prompt_mesh* out, const gbox* b, float cx, float cy, float r)
{
    const unsigned n = 12u;
    unsigned k;
    for (k = 0; k < n; k++) {
        const float a0 = 6.2831853f * (float)k / (float)n;
        const float a1 = 6.2831853f * (float)(k + 1u) / (float)n;
        add_tri(out, cx, cy,
                cx + cosf(a0) * r / b->aspect, cy + sinf(a0) * r,
                cx + cosf(a1) * r / b->aspect, cy + sinf(a1) * r);
    }
}

/* One stroke segment between two bounds points, width w (height units). */
static void add_segment(host_prompt_mesh* out, const gbox* b, float x0, float y0,
                        float x1, float y1, float w)
{
    /* work in screen-proportional units: x * aspect */
    const float ax = x0 * b->aspect, bx = x1 * b->aspect;
    float nx = -(y1 - y0), ny = bx - ax;
    const float len = sqrtf(nx * nx + ny * ny);
    if (len < 1e-6f) return;
    nx *= (w * 0.5f) / len;
    ny *= (w * 0.5f) / len;
    add_quad_raw(out, (ax + nx) / b->aspect, y0 + ny, (bx + nx) / b->aspect, y1 + ny,
                 (bx - nx) / b->aspect, y1 - ny, (ax - nx) / b->aspect, y0 - ny);
}

/* Polylines separated by PATH_END, terminated by two PATH_ENDs. */
static void add_paths(host_prompt_mesh* out, const gbox* b, const gpt* p, float w)
{
    while (!(p[0].x < 0.0f && p[1].x < 0.0f)) {
        float px, py;
        if (p[0].x < 0.0f) { p++; continue; }
        to_bounds(b, p[0].x, p[0].y, &px, &py);
        add_disc(out, b, px, py, w * 0.5f);             /* round cap / join */
        while (p[1].x >= 0.0f) {
            float qx, qy;
            to_bounds(b, p[1].x, p[1].y, &qx, &qy);
            add_segment(out, b, px, py, qx, qy, w);
            add_disc(out, b, qx, qy, w * 0.5f);
            px = qx; py = qy;
            p++;
        }
        p++;
    }
}

/* Arc of a design-space circle, appended to a point list. */
static unsigned arc(gpt* dst, float cx, float cy, float rx, float ry,
                    float deg0, float deg1, unsigned steps)
{
    unsigned i;
    for (i = 0; i <= steps; i++) {
        const float t = (deg0 + (deg1 - deg0) * (float)i / (float)steps) * 0.0174533f;
        dst[i].x = cx + cosf(t) * rx;
        dst[i].y = cy + sinf(t) * ry;
    }
    return steps + 1u;
}

/* Design paths per letter (built once into a static buffer). */
static const gpt* letter_paths(char c)
{
    static gpt buf[160];
    unsigned n = 0;
#define P(X, Y) (buf[n].x = (X), buf[n].y = (Y), n++)
#define END()   (buf[n].x = -1.0f, buf[n].y = -1.0f, n++)
    switch (c) {
    case 'A':
        P(0.0f, 1.0f); P(0.5f, 0.0f); P(1.0f, 1.0f); END();
        P(0.22f, 0.64f); P(0.78f, 0.64f); END();
        break;
    case 'B':
        P(0.08f, 0.0f); P(0.08f, 1.0f); END();
        P(0.08f, 0.0f); P(0.56f, 0.0f);
        n += arc(&buf[n], 0.56f, 0.24f, 0.30f, 0.24f, -90.0f, 90.0f, 8);
        P(0.08f, 0.48f); END();
        P(0.08f, 0.48f); P(0.60f, 0.48f);
        n += arc(&buf[n], 0.60f, 0.74f, 0.34f, 0.26f, -90.0f, 90.0f, 8);
        P(0.08f, 1.0f); END();
        break;
    case 'E':
        P(0.95f, 0.0f); P(0.08f, 0.0f); P(0.08f, 1.0f); P(0.95f, 1.0f); END();
        P(0.08f, 0.5f); P(0.78f, 0.5f); END();
        break;
    case 'J':
        P(0.30f, 0.0f); P(0.92f, 0.0f); END();
        P(0.72f, 0.0f); P(0.72f, 0.66f);
        n += arc(&buf[n], 0.40f, 0.66f, 0.32f, 0.34f, 0.0f, 180.0f, 8);
        END();
        break;
    case 'K':
        P(0.08f, 0.0f); P(0.08f, 1.0f); END();
        P(0.92f, 0.0f); P(0.08f, 0.60f); END();
        P(0.36f, 0.40f); P(0.95f, 1.0f); END();
        break;
    case 'X':
        P(0.0f, 0.0f); P(1.0f, 1.0f); END();
        P(1.0f, 0.0f); P(0.0f, 1.0f); END();
        break;
    case 'Y':
        P(0.0f, 0.0f); P(0.5f, 0.5f); P(1.0f, 0.0f); END();
        P(0.5f, 0.5f); P(0.5f, 1.0f); END();
        break;
    case ' ':   /* the space symbol: a wide open bracket */
        P(0.0f, 0.45f); P(0.0f, 0.9f); P(1.0f, 0.9f); P(1.0f, 0.45f); END();
        break;
    default:
        return NULL;
    }
    END();
#undef P
#undef END
    return buf;
}

/* Where the letter sits: controller buttons centre it; a keyboard key puts a
 * smaller label on the key's raised top face, a little above the centre. */
static float s_letter_cy = 0.5f, s_letter_h = GLYPH_H;

/* A letter in the bounds; `grow` widens the stroke (outline layers). */
static void add_letter(host_prompt_mesh* out, char c, float aspect)
{
    const gpt* p = letter_paths(c);
    gbox b;
    const float w = GLYPH_STROKE * (s_letter_h / GLYPH_H) + s_grow;
    /* width / height: B needs room for two open bowls at ~60 px */
    const float design_aspect = c == ' ' ? 2.2f : c == 'B' ? 0.86f : 0.8f;
    if (!p) return;
    b.aspect = aspect > 0.1f ? aspect : 1.0f;
    b.sy = s_letter_h - GLYPH_STROKE;             /* keep the stroke inside */
    b.sx = b.sy * design_aspect / b.aspect;
    b.ox = 0.5f - b.sx * 0.5f;
    b.oy = s_letter_cy - b.sy * 0.5f;
    add_paths(out, &b, p, w);
}

/* The space symbol on a wide key's top face. */
static void add_space_symbol(host_prompt_mesh* out, float aspect, float cy, float h)
{
    gbox b;
    b.aspect = aspect > 0.1f ? aspect : 1.0f;
    b.sy = h; b.sx = 0.92f; b.ox = 0.04f; b.oy = cy - h * 0.5f;
    add_paths(out, &b, letter_paths(' '), GLYPH_STROKE * 0.85f + s_grow);
}

/* Logical PS3 face -> label per family (the action keeps its PS3 position). */
static char face_label(host_prompt_face face, host_prompt_set set)
{
    static const char kb[]   = { ' ', 'E', 'J', 'K' };
    static const char xbox[] = { 'A', 'B', 'X', 'Y' };
    static const char sw[]   = { 'B', 'A', 'Y', 'X' };
    if (face > HOST_PROMPT_FACE_TRIANGLE)
        return 0;
    switch (set) {
    case HOST_PROMPT_SET_KB:     return kb[face];
    case HOST_PROMPT_SET_XBOX:   return xbox[face];
    case HOST_PROMPT_SET_SWITCH: return sw[face];
    default:                     return 0;
    }
}

int host_prompt_face_mesh_grown(host_prompt_face face, host_prompt_set set,
                                float aspect, float grow, host_prompt_mesh* out)
{
    int r;
    s_grow = grow > 0.0f ? grow : 0.0f;
    r = host_prompt_face_mesh(face, set, aspect, out);
    s_grow = 0.0f;
    return r;
}

int host_prompt_face_mesh(host_prompt_face face, host_prompt_set set,
                          float aspect, host_prompt_mesh* out)
{
    if (!out)
        return 0;

    memset(out, 0, sizeof(*out));
    if (set == HOST_PROMPT_SET_PS)
        return 0;

    if (face == HOST_PROMPT_FACE_CROSS) {
        /* Keyboard: the space symbol, wider than a letter (Space is a wide
         * key); Xbox A / Switch B go through the same stroked letters. */
        const char c = set == HOST_PROMPT_SET_KB ? ' ' :
                       set == HOST_PROMPT_SET_XBOX ? 'A' :
                       set == HOST_PROMPT_SET_SWITCH ? 'B' : 0;
        if (!c) return 0;
        if (c == ' ') add_space_symbol(out, aspect, 0.44f, 0.30f);
        else add_letter(out, c, aspect);
        return 1;
    }

    if (face == HOST_PROMPT_FACE_L1) {
        if (set != HOST_PROMPT_SET_KB)
            return 0; /* controller shoulder labels: not measured yet */
        /* Shift: a solid up arrow. */
        add_tri(out, 0.50f, 0.12f, 0.12f, 0.52f, 0.88f, 0.52f);
        add_rect(out, 0.34f, 0.52f, 0.66f, 0.88f);
        return 1;
    }

    {
        const char label = face_label(face, set);
        if (!label)
            return 0;
        if (set == HOST_PROMPT_SET_KB) {        /* label on the key's top face */
            s_letter_cy = 0.45f; s_letter_h = 0.50f;
        }
        add_letter(out, label, aspect);
        s_letter_cy = 0.5f; s_letter_h = GLYPH_H;
        return out->count != 0;
    }
}

int host_prompt_backing_mesh_part(host_prompt_face face, host_prompt_set set,
                                  int part, host_prompt_mesh* out)
{
    const int wide = face == HOST_PROMPT_FACE_CROSS || face == HOST_PROMPT_FACE_L1;
    if (!out)
        return 0;

    memset(out, 0, sizeof(*out));
    if (set != HOST_PROMPT_SET_KB)
        return 0; /* controllers keep the title's round button */
    /* A keycap in relief: the base (the key's side, showing below) and the
     * raised top face, offset up so the base reads as the key's thickness. */
    if (part == 0) {
        if (wide) add_rounded_rect(out, -0.24f, 0.24f, 1.24f, 0.90f, 0.14f);
        else      add_rounded_rect(out, 0.06f, 0.12f, 0.94f, 0.98f, 0.16f);
    } else {
        if (wide) add_rounded_rect(out, -0.20f, 0.16f, 1.20f, 0.76f, 0.12f);
        else      add_rounded_rect(out, 0.10f, 0.04f, 0.90f, 0.84f, 0.14f);
    }
    return 1;
}

int host_prompt_backing_mesh(host_prompt_face face, host_prompt_set set,
                             host_prompt_mesh* out)
{
    return host_prompt_backing_mesh_part(face, set, 1, out);
}

int host_prompt_keycap_layer(host_prompt_face face, host_prompt_set set,
                             int layer, float aspect, host_prompt_mesh* out)
{
    const int wide = face == HOST_PROMPT_FACE_CROSS;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (set != HOST_PROMPT_SET_KB || face > HOST_PROMPT_FACE_TRIANGLE)
        return 0;
    if (layer == 0) {                            /* coloured base: key side */
        if (wide) add_rounded_rect(out, -0.30f, 0.20f, 1.30f, 0.98f, 0.15f);
        else      add_rounded_rect(out, 0.04f, 0.10f, 0.96f, 1.00f, 0.18f);
    } else if (layer == 1) {                     /* dark raised top face */
        if (wide) add_rounded_rect(out, -0.26f, 0.12f, 1.26f, 0.80f, 0.13f);
        else      add_rounded_rect(out, 0.09f, 0.02f, 0.91f, 0.82f, 0.15f);
    } else {                                     /* the label on the top */
        if (wide) {
            add_space_symbol(out, aspect, 0.44f, 0.26f);
        } else {
            s_letter_cy = 0.42f; s_letter_h = 0.48f;
            add_letter(out, face_label(face, set), aspect);
            s_letter_cy = 0.5f; s_letter_h = GLYPH_H;
        }
    }
    return out->count != 0;
}

/* Label of a shoulder/stick face per family; "^" = the Shift arrow. */
static const char* shoulder_label(host_prompt_face face, host_prompt_set set)
{
    static const char* kb[]   = { "^",  "Q",  "CTRL", "R",  "F",  "G"  };
    static const char* xbox[] = { "LB", "RB", "LT",   "RT", "LS", "RS" };
    static const char* sw[]   = { "L",  "R",  "ZL",   "ZR", "LS", "RS" };
    const unsigned i = (unsigned)face - (unsigned)HOST_PROMPT_FACE_L1;
    if (face < HOST_PROMPT_FACE_L1 || face > HOST_PROMPT_FACE_R3)
        return NULL;
    switch (set) {
    case HOST_PROMPT_SET_KB:     return kb[i];
    case HOST_PROMPT_SET_XBOX:   return xbox[i];
    case HOST_PROMPT_SET_SWITCH: return sw[i];
    default:                     return NULL;
    }
}

/*
 * Title font glyphs, read from FLP_HUDA's font (CharNumberToSymbolIdMap ->
 * MeshesRefs -> mesh-part quad + UVs; advance = SymbolWidths / 16). Numeric
 * metadata only; 'L' and '1' also match the quads measured in-boot.
 */
typedef struct { char c; float x0, x1, y0, y1, u0, u1, v0, v1, adv; } prompt_glyph;
static const prompt_glyph k_font[] = {
    { 'B', 2.0625f, 43.3125f, -39.1875f,  0.0f,    0.8613281f, 0.9355469f, 0.6894531f, 0.7597656f, 47.4375f },
    { 'C', 0.0f,    45.375f,  -39.1875f,  0.0f,    0.3066406f, 0.3886719f, 0.6894531f, 0.7597656f, 43.3125f },
    { 'F', 2.0625f, 41.25f,   -39.1875f, 12.375f,  0.7871094f, 0.8574219f, 0.1191406f, 0.2128906f, 41.25f },
    { 'G', 0.0f,    51.5625f, -39.1875f, 12.375f,  0.8378906f, 0.9316406f, 0.0058594f, 0.0996094f, 49.5f },
    { 'L', 2.0625f, 43.3125f, -39.1875f,  0.0f,    0.0917969f, 0.1660156f, 0.7714844f, 0.8417969f, 43.3125f },
    { 'Q', 0.0f,    53.625f,  -39.1875f, 12.375f,  0.5097656f, 0.6074219f, 0.0058594f, 0.0996094f, 51.5625f },
    { 'R', 2.0625f, 57.75f,   -39.1875f, 10.3125f, 0.2871094f, 0.3886719f, 0.2246094f, 0.3144531f, 49.5f },
    { 'S', 2.0625f, 37.125f,  -39.1875f,  0.0f,    0.5527344f, 0.6152344f, 0.7714844f, 0.8417969f, 39.1875f },
    { 'T', 0.0f,    43.3125f, -39.1875f,  0.0f,    0.6816406f, 0.7597656f, 0.6894531f, 0.7597656f, 41.25f },
    { 'Z', 0.0f,    43.3125f, -39.1875f,  0.0f,    0.7714844f, 0.8496094f, 0.6894531f, 0.7597656f, 43.3125f },
    { '1', 2.0625f, 26.8125f, -39.1875f,  0.0f,    0.8378906f, 0.8808594f, 0.7714844f, 0.8417969f, 30.9375f },
    { '2', 2.0625f, 35.0625f, -39.1875f,  0.0f,    0.6269531f, 0.6855469f, 0.7714844f, 0.8417969f, 39.1875f },
    { '3', 2.0625f, 35.0625f, -39.1875f,  2.0625f, 0.5019531f, 0.5605469f, 0.5175781f, 0.5917969f, 39.1875f },
};

static const prompt_glyph* font_glyph(char c)
{
    unsigned i;
    for (i = 0; i < sizeof k_font / sizeof k_font[0]; i++)
        if (k_font[i].c == c) return &k_font[i];
    return NULL;
}

float host_prompt_font_advance(char c)
{
    const prompt_glyph* g = font_glyph(c);
    return g ? g->adv : 0.0f;
}

static void text_vert(host_prompt_text* out, float x, float y, float u, float v)
{
    if (out->count >= HOST_PROMPT_TEXT_MAX_VERTS) return;
    out->xy[out->count][0] = x; out->xy[out->count][1] = y;
    out->uv[out->count][0] = u; out->uv[out->count][1] = v;
    out->count++;
}

/* Vertex of glyph g at local (x,y), with the UV the atlas maps there
 * (u grows with x, v with y -- as in the native quads). */
static void glyph_vert(host_prompt_text* out, const prompt_glyph* g, float ox, float s,
                       float x, float y)
{
    text_vert(out, ox + x * s, y * s,
              g->u0 + (x - g->x0) / (g->x1 - g->x0) * (g->u1 - g->u0),
              g->v0 + (y - g->y0) / (g->y1 - g->y0) * (g->v1 - g->v0));
}

int host_prompt_shoulder_text(host_prompt_face face, host_prompt_set set,
                              float native_advance, host_prompt_text* out)
{
    const char* label = shoulder_label(face, set);
    float width = 0.0f, s = 1.0f, x;
    unsigned i;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!label || native_advance <= 0.0f)
        return 0;

    if (label[0] == '^') {
        /* Shift arrow, solid in the text colour: every vertex samples the
         * middle of the 'L' stem, an opaque texel of the same font. */
        const prompt_glyph* l = font_glyph('L');
        const float u = l->u0 + 0.12f * (l->u1 - l->u0);
        const float v = l->v0 + 0.50f * (l->v1 - l->v0);
        const float c = native_advance * 0.5f, h = 39.1875f;
        const float hw = h * 0.42f, sw = h * 0.16f, base = -h * 0.50f;
        text_vert(out, c, -h, u, v);            /* head */
        text_vert(out, c - hw, base, u, v);
        text_vert(out, c + hw, base, u, v);
        text_vert(out, c - sw, base, u, v);     /* stem */
        text_vert(out, c + sw, base, u, v);
        text_vert(out, c + sw, 0.0f, u, v);
        text_vert(out, c - sw, base, u, v);
        text_vert(out, c + sw, 0.0f, u, v);
        text_vert(out, c - sw, 0.0f, u, v);
        return 1;
    }

    for (i = 0; label[i]; i++) {
        const prompt_glyph* g = font_glyph(label[i]);
        if (!g) return 0;                        /* never draw a partial label */
        width += g->adv;
    }
    if (width > native_advance * 1.35f)
        s = native_advance * 1.35f / width;      /* "CTRL" on an "L2" key */
    x = (native_advance - width * s) * 0.5f;
    for (i = 0; label[i]; i++) {
        const prompt_glyph* g = font_glyph(label[i]);
        glyph_vert(out, g, x, s, g->x1, g->y1);
        glyph_vert(out, g, x, s, g->x0, g->y1);
        glyph_vert(out, g, x, s, g->x1, g->y0);
        glyph_vert(out, g, x, s, g->x0, g->y1);
        glyph_vert(out, g, x, s, g->x0, g->y0);
        glyph_vert(out, g, x, s, g->x1, g->y0);
        x += g->adv * s;
    }
    return 1;
}

int host_prompt_cross_mesh(host_prompt_set set, host_prompt_mesh* out)
{
    return host_prompt_face_mesh(HOST_PROMPT_FACE_CROSS, set, 1.0f, out);
}
