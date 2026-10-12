#ifndef PS3RECOMP_HOST_PROMPT_MESH_H
#define PS3RECOMP_HOST_PROMPT_MESH_H

#include "host_prompts.h"

/* Room for the largest replacement (a keycap with a cut-out "CTRL" label). */
#define HOST_PROMPT_MESH_MAX_VERTS 1024u

/*
 * Normalized triangle list replacing one native prompt draw in-place.
 * (0,0) is the top-left of the source draw's bounds and (1,1) its
 * bottom-right; y grows downward like the title's own FLP coordinates.
 * Wide keycaps (Space, Shift) may extend x past [0,1]; y never leaves it.
 */
typedef struct {
    float xy[HOST_PROMPT_MESH_MAX_VERTS][2];
    unsigned count;
} host_prompt_mesh;

/* Logical PS3 button a native prompt glyph stands for. */
typedef enum {
    HOST_PROMPT_FACE_CROSS = 0,
    HOST_PROMPT_FACE_CIRCLE,
    HOST_PROMPT_FACE_SQUARE,
    HOST_PROMPT_FACE_TRIANGLE,
    HOST_PROMPT_FACE_L1,
    HOST_PROMPT_FACE_R1,
    HOST_PROMPT_FACE_L2,
    HOST_PROMPT_FACE_R2,
    HOST_PROMPT_FACE_L3,
    HOST_PROMPT_FACE_R3
} host_prompt_face;

/*
 * Glyph drawn in place of `face` for the active family. `aspect` is the
 * source glyph's width / height so letters keep their proportions.
 * Returns zero when the family keeps the title's own glyph (PlayStation).
 */
int host_prompt_face_mesh(host_prompt_face face, host_prompt_set set,
                          float aspect, host_prompt_mesh* out);

/*
 * Same glyph with every stroke thickened by `grow` (normalized units) in
 * place: the outline/rim layers of the animated buttons.
 */
int host_prompt_face_mesh_grown(host_prompt_face face, host_prompt_set set,
                                float aspect, float grow, host_prompt_mesh* out);

/*
 * Button backing drawn behind `face`: the keyboard turns the round button
 * into a keycap -- wide for Space (Cross) and Shift (L1), square for the
 * letter keys. Controllers keep the round button (returns zero).
 */
int host_prompt_backing_mesh(host_prompt_face face, host_prompt_set set,
                             host_prompt_mesh* out);

/* The keycap in relief: part 0 = base (the key's side, drawn from the
 * backing's shadow), part 1 = raised top face (from the backing itself). */
int host_prompt_backing_mesh_part(host_prompt_face face, host_prompt_set set,
                                  int part, host_prompt_mesh* out);

/* Keyboard look of the three-layer animated buttons: layer 0 = coloured key
 * base, 1 = dark raised top face, 2 = the label. Zero for other families. */
int host_prompt_keycap_layer(host_prompt_face face, host_prompt_set set,
                             int layer, float aspect, host_prompt_mesh* out);

/*
 * Shoulder/stick label ("L1", "R1", "L2"... on the rounded key) rewritten in
 * the title's OWN font: the game's label quads are redrawn with other glyphs
 * of the same atlas, so contrast and style stay native on any background.
 * Keyboard Shift-arrow/Q/CTRL/R/F/G, Xbox LB/RB/LT/RT/LS/RS, Switch
 * L/R/ZL/ZR/LS/RS; zero = keep the native label (PlayStation).
 *
 * Output is in the label's local space: positions like the native glyph quad
 * (x right, y from -39.19 up to the baseline 0) and atlas UVs. The text is
 * centred on the native label's advance `native_advance` (sum of its two
 * glyph advances) and scaled down when it would be much wider.
 */
#define HOST_PROMPT_TEXT_MAX_VERTS 96u
typedef struct {
    float xy[HOST_PROMPT_TEXT_MAX_VERTS][2];
    float uv[HOST_PROMPT_TEXT_MAX_VERTS][2];
    unsigned count;
} host_prompt_text;

int host_prompt_shoulder_text(host_prompt_face face, host_prompt_set set,
                              float native_advance, host_prompt_text* out);

/* Advance of one glyph of the title font, 0 when the glyph is not known. */
float host_prompt_font_advance(char c);

/* Shorthand for the Cross glyph with a square source. */
int host_prompt_cross_mesh(host_prompt_set set, host_prompt_mesh* out);

#endif
