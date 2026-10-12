/*
 * ps3recomp - V0 gate: every real FP/VP decompiles to GLSL 450 and compiles
 * to SPIR-V with shaderc. Inputs are RPCS3 shaders_cache/raw dirs and/or our
 * own PS3_RSX_DUMP_UCODE dirs (same format). Game data: never commit them.
 *
 *   validate_glsl_corpus [--out <dir>] <raw dir>...
 */
#include "rsx_fp_decompiler.h"
#include "rsx_vp_decompiler.h"
#include <shaderc/shaderc.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static shaderc_compiler_t s_cc;
static shaderc_compile_options_t s_op;
static const char* s_out_dir;
static char s_glsl[256 * 1024];

static int ends_with(const char* s, const char* suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

static unsigned char* read_file(const char* path, long* n)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* b = (unsigned char*)malloc((size_t)*n + 16);
    if (b && fread(b, 1, (size_t)*n, f) != (size_t)*n) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static void save_text(const char* file, const char* variant, const char* text)
{
    if (!s_out_dir) return;
    const char* base = strrchr(file, '/'); base = base ? base + 1 : file;
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.%s.glsl", s_out_dir, base, variant);
    FILE* f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

static void save_glsl(const char* file, const char* variant)
{
    save_text(file, variant, s_glsl);
}

/* 1 = pass. On failure prints "<file> [variant] <reason>". */
static int compile_one(const char* file, const char* variant, int n, shaderc_shader_kind kind)
{
    if (n < 0) {
        printf("%s [%s] decompile/emit failed\n", file, variant);
        /* s_glsl is a stale buffer from a previous file/variant here (the
         * decompiler failed before writing anything of its own) -- saving it
         * would silently mislabel someone else's output as this failure's.
         * Write an explanatory placeholder instead. */
        save_text(file, variant, "// decompile/emit failed - no GLSL was produced "
                                  "(rsx_fp_decompile_glsl/rsx_vp_decompile_glsl "
                                  "returned < 0); this is a placeholder, not the "
                                  "shader's actual output.\n");
        return 0;
    }
    shaderc_compilation_result_t r =
        shaderc_compile_into_spv(s_cc, s_glsl, strlen(s_glsl), kind, file, "main", s_op);
    int ok = shaderc_result_get_compilation_status(r) == shaderc_compilation_status_success;
    if (!ok) {
        const char* msg = shaderc_result_get_error_message(r);
        const char* nl = strchr(msg, '\n');
        printf("%s [%s] %.*s\n", file, variant, nl ? (int)(nl - msg) : (int)strlen(msg), msg);
        save_glsl(file, variant);
    }
    shaderc_result_release(r);
    return ok;
}

static int run_fp(const char* path, const unsigned char* b, long n, int* vpass, int* vtotal)
{
    /* default + the Metal gameplay variants the Vulkan backend will request */
    static const struct { const char* name; int mrt, cbuf, cexp, depth; } V[] = {
        { "default", 1, 0, -1, 0 },
        { "mrt4-cbuf-r0", 4, 1, 1, 0 },
        { "mrt4-cbuf-h0", 4, 1, 0, 0 },
        { "mrt4-cbuf-r0-depth", 4, 1, 1, 1 },
    };
    int all = 1;
    for (size_t i = 0; i < sizeof V / sizeof V[0]; i++) {
        rsx_fp_set_mrt_count(V[i].mrt);
        rsx_fp_set_const_buffer_mode(V[i].cbuf);
        rsx_fp_set_color_export(V[i].cexp);
        rsx_fp_set_depth_export(V[i].depth);
        int k = rsx_fp_decompile_glsl(b, (u32)n, s_glsl, sizeof s_glsl);
        int ok = compile_one(path, V[i].name, k, shaderc_fragment_shader);
        *vpass += ok; (*vtotal)++; all &= ok;
    }
    rsx_fp_set_mrt_count(1); rsx_fp_set_const_buffer_mode(0);
    rsx_fp_set_color_export(-1); rsx_fp_set_depth_export(0);
    return all;
}

static int run_vp(const char* path, const unsigned char* b, long n, int* vpass, int* vtotal)
{
    /* "noseed-default": PS3_RSX_FIX_NO_UV_SEED (bit 2, value 4) is ON when
     * unset -- the shipped default (see rsx_vp_set_fix_override callers in
     * test_glsl_decompiler.c). Pinned explicitly (was -1, "use the ambient
     * environment") so this gate's result cannot silently change with
     * whatever PS3_RSX_FIX_* the invoking shell happens to have set. */
    static const struct { const char* name; int fix; } V[] = {
        { "noseed-default", 4 }, { "fix-all", 1 | 2 | 4 | 8 },
    };
    int all = 1;
    for (size_t i = 0; i < sizeof V / sizeof V[0]; i++) {
        rsx_vp_set_fix_override(V[i].fix);
        int k = rsx_vp_decompile_glsl(b, (u32)n, s_glsl, sizeof s_glsl);
        int ok = compile_one(path, V[i].name, k, shaderc_vertex_shader);
        *vpass += ok; (*vtotal)++; all &= ok;
    }
    rsx_vp_set_fix_override(-1);
    return all;
}

int main(int argc, char** argv)
{
    s_cc = shaderc_compiler_initialize();
    s_op = shaderc_compile_options_initialize();
    shaderc_compile_options_set_target_env(s_op, shaderc_target_env_vulkan,
                                           shaderc_env_version_vulkan_1_2);
    int fp_ok = 0, fp_n = 0, vp_ok = 0, vp_n = 0, v_ok = 0, v_n = 0;
    int dir_open_failed = 0;
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--out") && a + 1 < argc) { s_out_dir = argv[++a]; continue; }
        DIR* d = opendir(argv[a]);
        if (!d) { printf("cannot open %s\n", argv[a]); dir_open_failed = 1; continue; }
        struct dirent* e;
        while ((e = readdir(d)) != NULL) {
            int is_fp = ends_with(e->d_name, ".fp"), is_vp = ends_with(e->d_name, ".vp");
            if (!is_fp && !is_vp) continue;
            char path[1024];
            snprintf(path, sizeof path, "%s/%s", argv[a], e->d_name);
            long n = 0;
            unsigned char* b = read_file(path, &n);
            if (!b || n <= 0) {
                /* Unreadable (fopen/fread failure) or empty: used to `continue`
                 * without touching fp_n/vp_n, so the file vanished from the
                 * report entirely -- a silently-skipped file could never fail
                 * the gate. Count it as a failed file instead. */
                printf("%s unreadable or empty (n=%ld)\n", path, n);
                free(b);
                if (is_fp) fp_n++; else vp_n++;
                continue;
            }
            if (is_fp) { fp_n++; fp_ok += run_fp(path, b, n, &v_ok, &v_n); }
            else       { vp_n++; vp_ok += run_vp(path, b, n, &v_ok, &v_n); }
            free(b);
        }
        closedir(d);
    }
    printf("glsl-corpus: fp %d/%d vp %d/%d variants %d/%d\n", fp_ok, fp_n, vp_ok, vp_n, v_ok, v_n);
    shaderc_compile_options_release(s_op);
    shaderc_compiler_release(s_cc);

    /* Exit code must never encode the raw failure COUNT: on a POSIX shell the
     * low 8 bits alias (256 failures == exit 0, a false pass). Use a fixed,
     * small set of codes instead:
     *   2 = the run couldn't even try (a dir failed to open, or the corpus
     *       was empty) -- these are vacuous passes if left as exit 0/1.
     *   1 = the run tried and at least one file/variant failed.
     *   0 = the run tried and everything passed. */
    if (dir_open_failed) {
        printf("glsl-corpus: FAIL one or more input directories could not be opened\n");
        return 2;
    }
    if (fp_n + vp_n == 0) {
        printf("glsl-corpus: FAIL no .fp/.vp files found in the given directories\n");
        return 2;
    }
    if (fp_ok != fp_n || vp_ok != vp_n) return 1;
    return 0;
}
