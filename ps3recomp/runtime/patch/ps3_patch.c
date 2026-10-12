/*
 * ps3recomp - game patches (see include/ps3emu/ps3_patch.h for the contract).
 */
#include "ps3emu/ps3_patch.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* skip_ws(const char* s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void put_be(uint8_t* d, uint64_t v, int n)
{
    for (int i = 0; i < n; i++) d[i] = (uint8_t)(v >> (8 * (n - 1 - i)));
}

static void put_le(uint8_t* d, uint64_t v, int n)
{
    for (int i = 0; i < n; i++) d[i] = (uint8_t)(v >> (8 * i));
}

/* Integer value: decimal, 0x hex or negative. Range-checked for `bytes`. */
static int parse_int(const char* s, int bytes, uint64_t* out)
{
    char* end = NULL;
    s = skip_ws(s);
    if (*s == '-') {
        long long v = strtoll(s, &end, 0);
        if (end == s) return 0;
        if (bytes < 8 && v < -(1LL << (8 * bytes - 1))) return 0;
        *out = (uint64_t)v;
    } else {
        unsigned long long v = strtoull(s, &end, 0);
        if (end == s) return 0;
        if (bytes < 8 && v >> (8 * bytes)) return 0;
        *out = v;
    }
    end = (char*)skip_ws(end);
    return *end == '\0' || *end == '#';
}

int ps3_patch_parse_line(const char* line, ps3_patch_write* out)
{
    char type[16];
    int n = 0;
    const char* s = skip_ws(line);
    if (*s == '\0' || *s == '\n' || *s == '#') return 0;
    while (s[n] && !isspace((unsigned char)s[n]) && n < (int)sizeof type - 1) {
        type[n] = s[n];
        n++;
    }
    type[n] = '\0';
    s = skip_ws(s + n);

    char* end = NULL;
    unsigned long long addr = strtoull(s, &end, 0);
    if (end == s || addr > 0xFFFFFFFFull) return -1;
    s = skip_ws(end);
    if (*s == '\0') return -1;

    memset(out, 0, sizeof *out);
    out->addr = (uint32_t)addr;

    struct { const char* name; int bytes; int be; } ints[] = {
        { "byte", 1, 1 }, { "be16", 2, 1 }, { "be32", 4, 1 }, { "be64", 8, 1 },
        { "le16", 2, 0 }, { "le32", 4, 0 }, { "le64", 8, 0 },
    };
    for (size_t i = 0; i < sizeof ints / sizeof ints[0]; i++) {
        if (strcmp(type, ints[i].name) != 0) continue;
        uint64_t v;
        if (!parse_int(s, ints[i].bytes, &v)) return -1;
        out->len = (uint32_t)ints[i].bytes;
        if (ints[i].be) put_be(out->data, v, ints[i].bytes);
        else            put_le(out->data, v, ints[i].bytes);
        return 1;
    }

    if (!strcmp(type, "bef32") || !strcmp(type, "lef32") ||
        !strcmp(type, "bef64") || !strcmp(type, "lef64")) {
        double d = strtod(s, &end);
        if (end == s) return -1;
        end = (char*)skip_ws(end);
        if (*end != '\0' && *end != '#') return -1;
        const int wide = type[4] == '6';
        const int be = type[0] == 'b';
        uint64_t bits;
        if (wide) { memcpy(&bits, &d, 8); }
        else { float f = (float)d; uint32_t b32; memcpy(&b32, &f, 4); bits = b32; }
        out->len = wide ? 8u : 4u;
        if (be) put_be(out->data, bits, (int)out->len);
        else    put_le(out->data, bits, (int)out->len);
        return 1;
    }

    if (!strcmp(type, "utf8") || !strcmp(type, "c_utf8")) {
        if (*s != '"') return -1;
        s++;
        uint32_t k = 0;
        while (*s && *s != '"') {
            char c = *s++;
            if (c == '\\' && *s) {
                c = *s++;
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
            }
            if (k >= PS3_PATCH_MAX_BYTES - 1) return -1;
            out->data[k++] = (uint8_t)c;
        }
        if (*s != '"') return -1;
        if (type[0] == 'c') out->data[k++] = 0;
        out->len = k;
        return k ? 1 : -1;
    }
    return -1;
}

ps3_patch_stats ps3_patch_apply_file(const char* path, uint8_t* mem,
                                     const ps3_patch_range* loaded, int nloaded,
                                     const ps3_patch_range* exec, int nexec)
{
    ps3_patch_stats st = { 0, 0, 0 };
    FILE* f = path && *path ? fopen(path, "r") : NULL;
    if (!f) {
        if (path && *path) fprintf(stderr, "[patch] cannot open %s\n", path);
        return st;
    }
    char line[1024];
    char name[256] = "?";
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        line[strcspn(line, "\r\n")] = '\0';
        const char* s = skip_ws(line);
        if (!strncmp(s, "# patch:", 8)) {
            snprintf(name, sizeof name, "%s", skip_ws(s + 8));
            continue;
        }
        ps3_patch_write w;
        const int r = ps3_patch_parse_line(line, &w);
        if (r == 0) continue;
        int inside = 0;
        if (r > 0)
            for (int i = 0; i < nloaded; i++)
                if (w.addr >= loaded[i].lo && (uint64_t)w.addr + w.len <= loaded[i].hi) inside = 1;
        if (r < 0 || !inside) {
            fprintf(stderr, "[patch] %s:%d rejected (%s): %s\n", path, lineno, name, line);
            st.rejected++;
            continue;
        }
        int in_code = 0;
        for (int i = 0; i < nexec; i++)
            if (w.addr < exec[i].hi && w.addr + w.len > exec[i].lo) in_code = 1;
        memcpy(mem + w.addr, w.data, w.len);
        if (in_code) st.code++;
        else         st.applied++;
    }
    fclose(f);
    fprintf(stderr, "[patch] %s: %d data write(s) applied, %d code write(s) without effect "
                    "(recompiled code, needs re-lift), %d rejected\n",
            path, st.applied, st.code, st.rejected);
    return st;
}
