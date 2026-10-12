/*
 * ps3recomp - navegação das animações ANMX do GoW2 (ver wad_anim.h).
 * Navegação verificada contra os dados reais (scripts diag_*.py). O decode
 * do stream comprimido está em anim_decode_quat (transcrição em progresso).
 */
#include "wad_anim.h"

#include <math.h>
#include <string.h>

/* Todos os campos numéricos do pacote ANM são little-endian no asset. */
static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static float lef32(const uint8_t* p) { uint32_t u = le32(p); float f; memcpy(&f, &u, 4); return f; }

int anim_pkg_open(const uint8_t* wad, size_t len, anim_pkg_t* out) {
    memset(out, 0, sizeof *out);
    /* record "ANM_hero": nome 24B começa 24B depois de [type u32][sz u32 LE] */
    static const char TAG[] = "ANM_hero";
    const uint8_t* hit = NULL;
    for (size_t i = 0; i + 24 < len; i++) {
        const uint8_t* h = memchr(wad + i, 'A', len - i - 24);
        if (!h) break;
        i = (size_t)(h - wad);
        if (i >= 8 && !memcmp(h, TAG, 8)) {
            uint32_t sz = le32(wad + i - 4);
            if (sz > 0x1000) { hit = h; break; }
        }
    }
    if (!hit) return -1;
    /* record ANM: {u16 type, u16, u32 size, nome[24]="ANM_hero"}; o nome
     * começa em `hit`, o header do pacote (campos + offsets) vem logo a
     * seguir ao nome de 24B: pkg = hit + 24. */
    const uint8_t* pkg = hit + 24;
    /* header @pkg: {u32,u32,u32,u32 size,(u16 2,u16 nclips),u32,u32 offs[]} */
    uint16_t nclips = le16(pkg + 18);
    if (nclips == 0 || nclips > 4096) return -1;
    out->pkg = pkg;
    out->nclips = nclips;
    out->clip_off = (const uint32_t*)(pkg + 24);   /* offsets LE (host LE) */
    return 0;
}

/* offset LE robusto (não assume host LE) */
static uint32_t clip_offset(const anim_pkg_t* p, uint32_t i) {
    return le32((const uint8_t*)p->pkg + 24 + i * 4);
}

int anim_clip_name(const anim_pkg_t* p, uint32_t i, char out[24]) {
    if (i >= p->nclips) return -1;
    const uint8_t* c = p->pkg + clip_offset(p, i);
    memcpy(out, c + 0x14, 23);
    out[23] = 0;
    return 0;
}

/* Sub-offsets de um clip: nsub em word[3]; os offsets seguem numa janela
 * a partir de +0x34 (verificado: AirAttacks, walkBlend). */
static int clip_suboffsets(const anim_pkg_t* p, uint32_t ci, uint32_t sub[64]) {
    const uint8_t* c = p->pkg + clip_offset(p, ci);
    uint32_t csz = (ci + 1 < p->nclips ? clip_offset(p, ci + 1) : 0xF3D80) - clip_offset(p, ci);
    uint32_t nsub = le32(c + 0x0C);
    if (nsub == 0 || nsub > 64) return 0;
    int n = 0;
    for (uint32_t k = 0; k < 11 && (uint32_t)n < nsub; k++) {
        uint32_t so = le32(c + 0x34 + k * 4);
        if (so > 0 && so < csz) sub[n++] = so;
    }
    return n;
}

static void fill_subanim(const uint8_t* S, anim_subanim_t* out) {
    memset(out, 0, sizeof *out);
    out->base = S;
    memcpy(out->name, S + 0x24, 23);
    out->duration = lef32(S + 0x14);
    /* descritores de secção em +0x60 (16B), até dt fora de [1e-4, 0.3] */
    int n = 0;
    for (int i = 0; i < 8; i++) {
        const uint8_t* dsc = S + 0x60 + i * 16;
        float dt = lef32(dsc + 12);
        if (!(dt > 1e-4f && dt < 0.3f)) break;
        for (int k = 0; k < 4; k++) out->section[n].fields[k] = le16(dsc + k * 2);
        out->section[n].off = le32(dsc + 8);
        out->section[n].dt = dt;
        n++;
    }
    out->nsections = n;
}

int anim_find_subanim(const anim_pkg_t* p, const char* name, anim_subanim_t* out) {
    for (uint32_t ci = 0; ci < p->nclips; ci++) {
        uint32_t sub[64];
        int ns = clip_suboffsets(p, ci, sub);
        const uint8_t* c = p->pkg + clip_offset(p, ci);
        for (int s = 0; s < ns; s++) {
            const uint8_t* S = c + sub[s];
            if (!memcmp(S + 0x24, name, strlen(name) + 1)) {
                fill_subanim(S, out);
                return 0;
            }
        }
    }
    return -1;
}

int anim_section_ntracks(const anim_subanim_t* s, int sec) {
    if (sec < 0 || sec >= s->nsections) return 0;
    const uint8_t* base = s->base + s->section[sec].off;
    uint16_t first_off = le16(base + 10);   /* off do 1º record = tamanho da área */
    return first_off / 12;
}

int anim_section_track(const anim_subanim_t* s, int sec, int t, anim_track_t* out,
                       const uint8_t** stream, uint32_t* stream_len) {
    int nt = anim_section_ntracks(s, sec);
    if (t < 0 || t >= nt) return -1;
    const uint8_t* base = s->base + s->section[sec].off;
    const uint8_t* r = base + t * 12;
    out->a = le16(r + 0); out->b = le16(r + 2); out->c = le16(r + 4);
    out->d = le16(r + 6); out->e = le16(r + 8); out->off = le16(r + 10);
    /* tamanho do stream = até o off do próximo track, ou fim da secção */
    uint32_t sec_end = (sec + 1 < s->nsections) ? s->section[sec + 1].off
                                                : s->section[sec].off + 0x10000;
    uint32_t next = (t + 1 < nt) ? le16(base + (t + 1) * 12 + 10)
                                 : (sec_end - s->section[sec].off);
    *stream = base + out->off;
    /* secções de rotação (sec0/sec1) têm off ascendente; as de ref de arma
     * (sec2/sec3: a>=bone_id, b==0) têm outro layout -> len inválido. */
    *stream_len = (next > out->off) ? (next - out->off) : 0;
    return 0;
}

/* Uma secção é de rotação (tracks comprimidos) se o 1º record tem a==0
 * (âncora) e b>0; caso contrário é ref de arma/anexo (bone id em a). */
int anim_section_is_rotation(const anim_subanim_t* s, int sec) {
    if (sec < 0 || sec >= s->nsections) return 0;
    const uint8_t* base = s->base + s->section[sec].off;
    return le16(base + 0) == 0 && le16(base + 2) > 0;
}

/* ---- decode do stream comprimido: transcrição do decoder do jogo ----
 * (func_0022AD7C). Ainda em progresso — devolve identidade por enquanto. */
int anim_decode_quat(const anim_track_t* rec, const uint8_t* stream, uint32_t slen,
                     float dt, int frame, float out_quat[4]) {
    (void)rec; (void)stream; (void)slen; (void)dt; (void)frame;
    out_quat[0] = out_quat[1] = out_quat[2] = 0.0f;
    out_quat[3] = 1.0f;
    return -1;   /* não suportado ainda */
}
