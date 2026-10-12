/* Behavioral contract for the native prompt mesh builder.
 * Break caught: a family leaves a glyph unchanged, draws a letter or keycap
 * outside its source bounds, or gives every key the same backing shape. */
#include "host_prompt_mesh.h"

#include <math.h>
#include <stdio.h>

static int in_bounds(const host_prompt_mesh* mesh, float xmin, float xmax)
{
    unsigned i;
    for (i = 0; i < mesh->count; i++)
        if (!isfinite(mesh->xy[i][0]) || !isfinite(mesh->xy[i][1]) ||
            mesh->xy[i][0] < xmin || mesh->xy[i][0] > xmax ||
            mesh->xy[i][1] < 0.f || mesh->xy[i][1] > 1.f)
            return 0;
    return 1;
}

static float width_of(const host_prompt_mesh* mesh)
{
    float lo = 1e9f, hi = -1e9f;
    unsigned i;
    for (i = 0; i < mesh->count; i++) {
        if (mesh->xy[i][0] < lo) lo = mesh->xy[i][0];
        if (mesh->xy[i][0] > hi) hi = mesh->xy[i][0];
    }
    return hi - lo;
}

int main(void)
{
    int ok = 1;
    host_prompt_mesh mesh;
    unsigned f;
#define CHECK(cond) do { if (!(cond)) { printf("  [FAIL] %s\n", #cond); ok = 0; } } while (0)

    /* PlayStation preserves every glyph and backing of the title. */
    for (f = HOST_PROMPT_FACE_CROSS; f <= HOST_PROMPT_FACE_L1; f++) {
        mesh.count = 99;
        CHECK(host_prompt_face_mesh((host_prompt_face)f, HOST_PROMPT_SET_PS, 1.f, &mesh) == 0);
        CHECK(mesh.count == 0);
        CHECK(host_prompt_backing_mesh((host_prompt_face)f, HOST_PROMPT_SET_PS, &mesh) == 0);
    }

    /* Keyboard Cross is the space symbol: an open bracket wider than the
     * Cross, sitting in the lower half, inside the bounds. */
    CHECK(host_prompt_cross_mesh(HOST_PROMPT_SET_KB, &mesh) == 1);
    CHECK(mesh.count > 0 && mesh.count % 3u == 0u);
    CHECK(in_bounds(&mesh, -0.15f, 1.15f));
    CHECK(width_of(&mesh) > 1.0f);

    /* Keyboard letters: Circle=E, Square=J, Triangle=K, all non-empty and
     * inside the source bounds even for a wide (41.5x36) Triangle source. */
    for (f = HOST_PROMPT_FACE_CIRCLE; f <= HOST_PROMPT_FACE_TRIANGLE; f++) {
        CHECK(host_prompt_face_mesh((host_prompt_face)f, HOST_PROMPT_SET_KB,
                                    41.5625f / 36.f, &mesh) == 1);
        CHECK(mesh.count > 0 && mesh.count % 3u == 0u);
        CHECK(in_bounds(&mesh, 0.f, 1.f));
    }
    /* Distinct letters produce distinct meshes (E is 2 paths, K is 3). */
    {
        unsigned e, k;
        host_prompt_face_mesh(HOST_PROMPT_FACE_CIRCLE, HOST_PROMPT_SET_KB, 1.f, &mesh);
        e = mesh.count;
        host_prompt_face_mesh(HOST_PROMPT_FACE_TRIANGLE, HOST_PROMPT_SET_KB, 1.f, &mesh);
        k = mesh.count;
        CHECK(e > 0 && k > 0 && e != k);
    }

    /* Shift (L1) is an up arrow on the keyboard. */
    CHECK(host_prompt_face_mesh(HOST_PROMPT_FACE_L1, HOST_PROMPT_SET_KB, 1.f, &mesh) == 1);
    CHECK(mesh.count == 9u);
    CHECK(mesh.xy[0][1] == 0.12f); /* the tip points up */

    /* Backings: wide for Space and Shift, square for letter keys. */
    CHECK(host_prompt_backing_mesh(HOST_PROMPT_FACE_CROSS, HOST_PROMPT_SET_KB, &mesh) == 1);
    CHECK(in_bounds(&mesh, -0.3f, 1.3f) && width_of(&mesh) > 1.4f);
    CHECK(host_prompt_backing_mesh(HOST_PROMPT_FACE_L1, HOST_PROMPT_SET_KB, &mesh) == 1);
    CHECK(width_of(&mesh) > 1.4f);
    CHECK(host_prompt_backing_mesh(HOST_PROMPT_FACE_TRIANGLE, HOST_PROMPT_SET_KB, &mesh) == 1);
    CHECK(in_bounds(&mesh, 0.f, 1.f) && width_of(&mesh) < 0.9f);
    CHECK(mesh.count == 3u * 6u + 4u * 4u * 3u);
    /* Controllers keep the title's round button. */
    CHECK(host_prompt_backing_mesh(HOST_PROMPT_FACE_CROSS, HOST_PROMPT_SET_XBOX, &mesh) == 0);
    CHECK(host_prompt_backing_mesh(HOST_PROMPT_FACE_TRIANGLE, HOST_PROMPT_SET_SWITCH, &mesh) == 0);

    /* Xbox: A (three strokes) for Cross, Y for Triangle. */
    CHECK(host_prompt_cross_mesh(HOST_PROMPT_SET_XBOX, &mesh) == 1);
    CHECK(in_bounds(&mesh, 0.f, 1.f));
    /* An A, not an H: the topmost vertex is at the centre (apex). */
    {
        unsigned i, top = 0;
        for (i = 1; i < mesh.count; i++) if (mesh.xy[i][1] < mesh.xy[top][1]) top = i;
        CHECK(fabsf(mesh.xy[top][0] - 0.5f) < 0.06f);
    }
    CHECK(host_prompt_face_mesh(HOST_PROMPT_FACE_TRIANGLE, HOST_PROMPT_SET_XBOX, 1.f, &mesh) == 1);

    /* Switch: B with BOTH bowls for Cross (without the lower one it reads as
     * an E, seen in-boot); X for Triangle. */
    CHECK(host_prompt_cross_mesh(HOST_PROMPT_SET_SWITCH, &mesh) == 1);
    CHECK(mesh.count > 0);
    CHECK(in_bounds(&mesh, 0.f, 1.f));
    CHECK(host_prompt_face_mesh(HOST_PROMPT_FACE_TRIANGLE, HOST_PROMPT_SET_SWITCH, 1.f, &mesh) == 1);

    /* Shoulder/stick labels are redrawn in the title's own font. */
    {
        static const host_prompt_set sets[] = {
            HOST_PROMPT_SET_KB, HOST_PROMPT_SET_XBOX, HOST_PROMPT_SET_SWITCH };
        const float native = host_prompt_font_advance('L') + host_prompt_font_advance('1');
        host_prompt_text t;
        unsigned si, i;
        CHECK(native > 70.f && native < 78.f);          /* "L1" = 43.3 + 30.9 */
        for (si = 0; si < 3u; si++)
            for (f = HOST_PROMPT_FACE_L1; f <= HOST_PROMPT_FACE_R3; f++) {
                float lo = 1e9f, hi = -1e9f;
                CHECK(host_prompt_shoulder_text((host_prompt_face)f, sets[si], native, &t) == 1);
                CHECK(t.count > 0 && t.count % 3u == 0u && t.count <= HOST_PROMPT_TEXT_MAX_VERTS);
                for (i = 0; i < t.count; i++) {
                    CHECK(t.uv[i][0] >= 0.f && t.uv[i][0] <= 1.f);
                    CHECK(t.uv[i][1] >= 0.f && t.uv[i][1] <= 1.f);
                    CHECK(t.xy[i][1] >= -39.2f && t.xy[i][1] <= 12.4f);
                    if (t.xy[i][0] < lo) lo = t.xy[i][0];
                    if (t.xy[i][0] > hi) hi = t.xy[i][0];
                }
                /* Centred on the native label and never much wider than it. */
                CHECK(hi - lo <= native * 1.5f);
                CHECK(fabsf((lo + hi) * 0.5f - native * 0.5f) < native * 0.25f);
            }
        /* Xbox R1 = "RB": two glyph quads; the first samples the atlas cell of R. */
        CHECK(host_prompt_shoulder_text(HOST_PROMPT_FACE_R1, HOST_PROMPT_SET_XBOX, native, &t) == 1);
        CHECK(t.count == 12u);
        CHECK(fabsf(t.uv[0][0] - 0.3886719f) < 1e-4f && fabsf(t.uv[1][0] - 0.2871094f) < 1e-4f);
        /* Keyboard L1 = the Shift arrow: head + stem, tip pointing up. */
        CHECK(host_prompt_shoulder_text(HOST_PROMPT_FACE_L1, HOST_PROMPT_SET_KB, native, &t) == 1);
        CHECK(t.count == 9u && t.xy[0][1] < t.xy[1][1]);
        /* "CTRL" is scaled to fit an "L2" key. */
        CHECK(host_prompt_shoulder_text(HOST_PROMPT_FACE_L2, HOST_PROMPT_SET_KB, native, &t) == 1);
        CHECK(t.count == 24u);
        /* PlayStation and non-shoulder faces keep the native label. */
        CHECK(host_prompt_shoulder_text(HOST_PROMPT_FACE_L2, HOST_PROMPT_SET_PS, native, &t) == 0);
        CHECK(host_prompt_shoulder_text(HOST_PROMPT_FACE_CROSS, HOST_PROMPT_SET_KB, native, &t) == 0);
        CHECK(host_prompt_font_advance('?') == 0.f);
    }

    /* Outline layers thicken strokes in place: same triangle count, wider
     * extent, and the stroke stays centred where the thin one is. */
    {
        host_prompt_mesh thin, thick;
        host_prompt_face_mesh(HOST_PROMPT_FACE_CIRCLE, HOST_PROMPT_SET_KB, 1.f, &thin);
        CHECK(host_prompt_face_mesh_grown(HOST_PROMPT_FACE_CIRCLE, HOST_PROMPT_SET_KB,
                                          1.f, 0.04f, &thick) == 1);
        CHECK(thick.count == thin.count);             /* same paths, wider stroke */
        CHECK(width_of(&thick) > width_of(&thin) + 0.035f);
        CHECK(fabsf(width_of(&thick) - width_of(&thin) - 0.04f) < 0.01f); /* grows evenly */
        /* The grow does not leak into the next, plain glyph. */
        host_prompt_face_mesh(HOST_PROMPT_FACE_CIRCLE, HOST_PROMPT_SET_KB, 1.f, &thick);
        CHECK(thick.xy[0][0] == thin.xy[0][0]);
    }

    /* A null output is rejected rather than dereferenced. */
    CHECK(host_prompt_cross_mesh(HOST_PROMPT_SET_KB, NULL) == 0);
    CHECK(host_prompt_backing_mesh(HOST_PROMPT_FACE_CROSS, HOST_PROMPT_SET_KB, NULL) == 0);

#undef CHECK
    printf("%s\n", ok ? "test_host_prompt_mesh: PASS" : "test_host_prompt_mesh: FAIL");
    return ok ? 0 : 1;
}
