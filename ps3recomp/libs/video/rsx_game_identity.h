/*
 * ps3recomp - identidade do jogo a partir dos DADOS DO PROPRIO JOGO.
 *
 * O binario recompilado deve apresentar-se como o jogo real (padrao de jogo):
 * o nome da janela e o icone do Dock vem do PARAM.SFO (chave TITLE) e do
 * ICON0.PNG que acompanham o jogo, nao de strings genericas ("ps3recomp").
 * Nada e' embutido nem versionado -- le-se em runtime da raiz do jogo, tal
 * como o EBOOT.ELF (regra: NUNCA commitar dados de jogo).
 *
 * Raiz do jogo: PS3_GAME_ROOT explicito, senao PS3_VFS_ROOT sem o /USRDIR
 * final (extracted/USRDIR -> extracted/, onde vivem PARAM.SFO/ICON0.PNG).
 *
 * O icone usa AppKit (NSApplication.applicationIconImage) -- API nativa da
 * Apple (Port Apple); SDL_SetWindowIcon e' no-op no macOS (nao ha' icone de
 * janela). So' compila a parte ObjC quando incluido de um .m (__OBJC__).
 */
#ifndef PS3RECOMP_RSX_GAME_IDENTITY_H
#define PS3RECOMP_RSX_GAME_IDENTITY_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline uint32_t gid_rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Raiz do jogo (dir com PARAM.SFO/ICON0.PNG). 0 = OK. */
static inline int gid_game_root(char* out, size_t n) {
    const char* gr = getenv("PS3_GAME_ROOT");
    if (gr && gr[0]) { snprintf(out, n, "%s", gr); return 0; }
    const char* vfs = getenv("PS3_VFS_ROOT");
    if (!vfs || !vfs[0]) return -1;
    snprintf(out, n, "%s", vfs);
    size_t L = strlen(out);
    while (L > 1 && out[L - 1] == '/') out[--L] = 0;   /* barras finais */
    if (L >= 7) {                                       /* /USRDIR final (ci) */
        char* t = out + L - 7;
        if (t[0] == '/' &&
            (t[1] == 'U' || t[1] == 'u') && (t[2] == 'S' || t[2] == 's') &&
            (t[3] == 'R' || t[3] == 'r') && (t[4] == 'D' || t[4] == 'd') &&
            (t[5] == 'I' || t[5] == 'i') && (t[6] == 'R' || t[6] == 'r'))
            out[L - 7] = 0;
    }
    return 0;
}

/* Parser minimo de PARAM.SFO -> valor UTF-8 da chave exacta 'TITLE'. 0 = OK.
 * Tolerante a ficheiro malformado (bounds em cada acesso). */
static inline int gid_read_title(char* out, size_t n) {
    if (!out || n == 0) return -1;
    char root[1024];
    if (gid_game_root(root, sizeof root) != 0) return -1;
    char path[1152];
    snprintf(path, sizeof path, "%s/PARAM.SFO", root);
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    const size_t CAP = 64u * 1024u;
    uint8_t* d = (uint8_t*)malloc(CAP);
    if (!d) { fclose(f); return -1; }
    size_t len = fread(d, 1, CAP, f);
    fclose(f);

    int rc = -1;
    if (len >= 20 && d[0] == 0x00 && d[1] == 'P' && d[2] == 'S' && d[3] == 'F') {
        uint32_t key_start  = gid_rd32(d + 8);
        uint32_t data_start = gid_rd32(d + 12);
        uint32_t ne         = gid_rd32(d + 16);
        for (uint32_t i = 0; i < ne; i++) {
            size_t e = 0x14 + (size_t)i * 16;
            if (e + 16 > len) break;
            uint16_t ko  = (uint16_t)(d[e] | (d[e + 1] << 8));
            uint32_t dl  = gid_rd32(d + e + 4);
            uint32_t doo = gid_rd32(d + e + 12);   /* data_offset @ +12 */
            size_t kp = (size_t)key_start + ko;
            if (kp >= len) continue;
            size_t kmax = len - kp;
            const char* key = (const char*)(d + kp);
            /* chave EXACTA "TITLE" (nao "TITLE_ID"): 5 chars + NUL */
            if (kmax >= 6 && memcmp(key, "TITLE", 5) == 0 && key[5] == '\0') {
                size_t dp = (size_t)data_start + doo;
                if (dp >= len) break;
                size_t avail = len - dp;
                size_t cpy = dl < avail ? dl : avail;
                if (cpy >= n) cpy = n - 1;
                memcpy(out, d + dp, cpy);
                out[cpy] = '\0';
                out[strnlen(out, cpy)] = '\0';   /* corta no NUL interno */
                if (out[0]) rc = 0;
                break;
            }
        }
    }
    free(d);
    return rc;
}

#if defined(__APPLE__) && defined(__OBJC__)
#import <AppKit/AppKit.h>
/* Icone do Dock a partir do ICON0.PNG do jogo (NSImage -> NSApp, nativo). */
static inline void gid_set_dock_icon(void) {
    char root[1024];
    if (gid_game_root(root, sizeof root) != 0) return;
    char path[1152];
    snprintf(path, sizeof path, "%s/ICON0.PNG", root);
    NSString* p = [NSString stringWithUTF8String:path];
    NSImage* img = [[NSImage alloc] initWithContentsOfFile:p];
    if (img && img.valid) {
        [NSApplication sharedApplication].applicationIconImage = img;
        fprintf(stderr, "[identity] Dock icon <- %s\n", path);
    }
}
#endif

#endif /* PS3RECOMP_RSX_GAME_IDENTITY_H */
