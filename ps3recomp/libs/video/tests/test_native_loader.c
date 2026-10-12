/*
 * ps3recomp - teste do loader nativo (psarc_read + wad_assets) contra as
 * fixtures sinteticas de gen_native_fixtures.py (zero dados de jogo).
 *
 * Uso:
 *   test_native_loader <fixture_dir>              # suite (exit 0 = verde)
 *   test_native_loader --extract <psarc> <path> <out>   # utilitario p/ validar
 *                                                 # paridade com dados reais
 */
#include "psarc_read.h"
#include "wad_assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;
#define CHECK(cond, ...) do { \
    if (cond) { printf("  ok: " __VA_ARGS__); printf("\n"); } \
    else { g_fail++; printf("FAIL: " __VA_ARGS__); printf("\n"); } \
} while (0)

static uint8_t* read_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc((size_t)sz ? (size_t)sz : 1);
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return NULL; }
    *out_len = (size_t)sz;
    return buf;
}

static void path_join(char* dst, size_t cap, const char* dir, const char* name) {
    snprintf(dst, cap, "%s/%s", dir, name);
}

static void test_psarc(const char* dir) {
    char pth[1024];
    printf("[psarc]\n");
    path_join(pth, sizeof pth, dir, "fixture.psarc");
    psarc_t* p = psarc_open(pth);
    CHECK(p != NULL, "psarc_open(%s)", pth);
    if (!p) return;

    /* manifesto esperado */
    char mpth[1024];
    path_join(mpth, sizeof mpth, dir, "manifest.txt");
    FILE* mf = fopen(mpth, "r");
    CHECK(mf != NULL, "abrir manifest.txt");
    char line[512];
    uint32_t n = 0;
    while (mf && fgets(line, sizeof line, mf)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        const char* got = psarc_path(p, n);
        CHECK(got && strcmp(got, line) == 0, "path[%u] = '%s' (esperado '%s')", n, got ? got : "(null)", line);

        char epth[1024], ename[64];
        snprintf(ename, sizeof ename, "expected_%u.bin", n);
        path_join(epth, sizeof epth, dir, ename);
        size_t want_len = 0;
        uint8_t* want = read_file(epth, &want_len);
        CHECK(want != NULL, "ler %s", ename);

        size_t got_len = 0;
        uint8_t* data = psarc_extract(p, line, &got_len);
        CHECK(data != NULL, "extract('%s')", line);
        if (data && want) {
            CHECK(got_len == want_len, "tamanho %zu == %zu", got_len, want_len);
            CHECK(got_len == want_len && memcmp(data, want, want_len) == 0,
                  "conteudo byte-exacto (%zu bytes)", want_len);
        }
        /* tolerancia de '/' inicial: com e sem devem funcionar */
        char alt[600];
        if (line[0] == '/') snprintf(alt, sizeof alt, "%s", line + 1);
        else snprintf(alt, sizeof alt, "/%s", line);
        size_t alt_len = 0;
        uint8_t* alt_data = psarc_extract(p, alt, &alt_len);
        CHECK(alt_data != NULL && alt_len == got_len, "extract('%s') (variante de slash)", alt);
        free(alt_data);
        free(data);
        free(want);
        n++;
    }
    if (mf) fclose(mf);
    CHECK(psarc_count(p) == n, "count %u == %u", psarc_count(p), n);

    size_t nada_len = 0;
    uint8_t* nada = psarc_extract(p, "nao/existe.bin", &nada_len);
    CHECK(nada == NULL, "extract de path inexistente -> NULL");
    free(nada);
    psarc_close(p);
}

static void test_wad(const char* dir) {
    char pth[1024];
    printf("[wad]\n");
    path_join(pth, sizeof pth, dir, "fixture.wad");
    size_t wlen = 0;
    uint8_t* wad = read_file(pth, &wlen);
    CHECK(wad != NULL, "ler fixture.wad");
    if (!wad) return;

    path_join(pth, sizeof pth, dir, "modl_off.txt");
    FILE* f = fopen(pth, "r");
    uint32_t modl_off = 0;
    if (f) { if (fscanf(f, "%x", &modl_off) != 1) modl_off = 0; fclose(f); }
    CHECK(modl_off != 0, "modl_off = %#x", modl_off);

    /* ---- malha ---- */
    wad_mesh_t m;
    int rc = wad_mesh_extract(wad, wlen, modl_off, &m);
    CHECK(rc == 0, "wad_mesh_extract rc=%d", rc);
    if (rc == 0) {
        path_join(pth, sizeof pth, dir, "expected_mesh.bin");
        size_t glen = 0;
        uint8_t* gold = read_file(pth, &glen);
        CHECK(gold != NULL, "ler expected_mesh.bin");
        if (gold) {
            size_t blen = 4 + 4 + (size_t)m.nverts * 5 * 4 + 4 + (size_t)m.ntris * 3 * 4;
            uint8_t* got = malloc(blen);
            memcpy(got, "KMH2", 4);
            memcpy(got + 4, &m.nverts, 4);
            memcpy(got + 8, m.vtx5, (size_t)m.nverts * 5 * 4);
            memcpy(got + 8 + (size_t)m.nverts * 5 * 4, &m.ntris, 4);
            memcpy(got + 12 + (size_t)m.nverts * 5 * 4, m.idx, (size_t)m.ntris * 3 * 4);
            CHECK(blen == glen, "KMH2 %zu bytes == %zu", blen, glen);
            CHECK(blen == glen && memcmp(got, gold, glen) == 0,
                  "malha byte-exacta vs golden (%u verts, %u tris)", m.nverts, m.ntris);
            free(got);
            free(gold);
        }
        wad_mesh_free(&m);
    }

    /* ---- textura ---- */
    uint8_t* rgba = NULL;
    uint32_t tw = 0, th = 0;
    rc = wad_tex_decode(wad, wlen, "~fixture_tex", &rgba, &tw, &th);
    CHECK(rc == 0, "wad_tex_decode rc=%d", rc);
    if (rc == 0) {
        path_join(pth, sizeof pth, dir, "expected_tex.bin");
        size_t glen = 0;
        uint8_t* gold = read_file(pth, &glen);
        CHECK(gold != NULL, "ler expected_tex.bin");
        if (gold && glen >= 8) {
            uint32_t gw, gh;
            memcpy(&gw, gold, 4);
            memcpy(&gh, gold + 4, 4);
            CHECK(tw == gw && th == gh, "dim %ux%u == %ux%u", tw, th, gw, gh);
            CHECK(glen == 8 + (size_t)gw * gh * 4 && tw == gw && th == gh
                      && memcmp(rgba, gold + 8, (size_t)gw * gh * 4) == 0,
                  "RGBA byte-exacto vs golden (%ux%u)", gw, gh);
        }
        free(gold);
        free(rgba);
    }

    uint8_t* no_rgba = NULL;
    rc = wad_tex_decode(wad, wlen, "~nao_existe", &no_rgba, &tw, &th);
    CHECK(rc != 0 && no_rgba == NULL, "tag inexistente -> erro limpo");

    /* ---- skinning (BONI + peso w) ---- */
    wad_skin_t sk;
    rc = wad_skin_extract(wad, wlen, modl_off, &sk);
    CHECK(rc == 0, "wad_skin_extract rc=%d", rc);
    if (rc == 0) {
        path_join(pth, sizeof pth, dir, "expected_skin.bin");
        size_t glen = 0;
        uint8_t* gold = read_file(pth, &glen);
        CHECK(gold != NULL, "ler expected_skin.bin");
        if (gold && glen >= 4) {
            uint32_t gn;
            memcpy(&gn, gold, 4);
            CHECK(sk.nverts == gn, "skin nverts %u == %u", sk.nverts, gn);
            if (sk.nverts == gn && glen == 4 + (size_t)gn * 2 + (size_t)gn * 4) {
                int okb = 1, okw = 1;
                for (uint32_t i = 0; i < gn; i++) {
                    okb &= (sk.b0[i] == gold[4 + i * 2] && sk.b1[i] == gold[4 + i * 2 + 1]);
                    float gw;
                    memcpy(&gw, gold + 4 + (size_t)gn * 2 + (size_t)i * 4, 4);
                    okw &= (sk.w[i] == gw);
                }
                CHECK(okb, "indices b0/b1 byte-exactos (%u verts)", gn);
                CHECK(okw, "pesos w exactos (%u verts)", gn);
            } else {
                CHECK(0, "tamanho do golden de skin (%zu)", glen);
            }
        }
        free(gold);
        wad_skin_free(&sk);
    }
    free(wad);
}

int main(int argc, char** argv) {
    if (argc >= 3 && strcmp(argv[1], "--rig") == 0) {
        /* validacao com dados reais: extrai o rig e imprime resumo */
        size_t wlen = 0;
        uint8_t* wad = read_file(argv[2], &wlen);
        if (!wad) { fprintf(stderr, "nao li %s\n", argv[2]); return 2; }
        uint32_t modl = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 16) : 0x2d29cbu;
        wad_rig_t rig;
        if (wad_rig_extract(wad, wlen, modl, &rig) != 0) return 3;
        printf("bones=%u  verts=%u\n", rig.nbones, rig.nverts);
        const char* probe[] = { "pelvis", "neck", "head", "lHumerus", "rWrist", "lTibia" };
        for (size_t i = 0; i < sizeof probe / sizeof *probe; i++) {
            int b = wad_rig_find_bone(&rig, probe[i]);
            if (b >= 0) {
                const float* m = &rig.local_bind[b * 16];
                printf("  %-10s id=%3d pai=%3d t=(%7.3f,%7.3f,%7.3f)\n", probe[i], b,
                       rig.parent[b], m[12], m[13], m[14]);
            } else {
                printf("  %-10s NAO ENCONTRADO\n", probe[i]);
            }
        }
        uint32_t un = 0;
        for (uint32_t v = 0; v < rig.nverts; v++)
            if (rig.g0[v] == 0xFF) un++;
        printf("verts sem bone: %u\n", un);
        wad_rig_free(&rig);
        free(wad);
        return un ? 4 : 0;
    }
    if (argc >= 5 && strcmp(argv[1], "--extract") == 0) {
        psarc_t* p = psarc_open(argv[2]);
        if (!p) return 2;
        size_t len = 0;
        uint8_t* data = psarc_extract(p, argv[3], &len);
        if (!data) { fprintf(stderr, "path nao encontrado: %s\n", argv[3]); psarc_close(p); return 3; }
        FILE* f = fopen(argv[4], "wb");
        if (!f || fwrite(data, 1, len, f) != len) { perror("write"); return 4; }
        fclose(f);
        printf("wrote %s: %zu bytes\n", argv[4], len);
        free(data);
        psarc_close(p);
        return 0;
    }
    const char* dir = argc > 1 ? argv[1] : "/tmp/native_fixtures";
    test_psarc(dir);
    test_wad(dir);
    if (g_fail) { printf("== %d FALHAS ==\n", g_fail); return 1; }
    printf("== todos os testes verdes ==\n");
    return 0;
}
