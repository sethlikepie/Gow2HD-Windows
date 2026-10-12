/*
 * ps3recomp - navegação + (futuro) decode das animações ANMX do GoW2.
 *
 * Clean-room RE do pacote ANM_hero (ver notes/2026-07-22-gow2-rig-anmx-re.md):
 * pacote -> clips nomeados -> sub-anims -> secções -> tracks de 12B -> stream
 * comprimido (quats âncora norma 20861 + deltas). Esta unidade faz a
 * NAVEGAÇÃO (toda verificada nos dados reais); o decode do stream comprimido
 * é transcrito do decoder real do lift (func_0022AD7C) e entra em anim_decode_*.
 */
#ifndef PS3RECOMP_WAD_ANIM_H
#define PS3RECOMP_WAD_ANIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Um descritor de secção do sub-anim (16B no asset). */
typedef struct {
    uint16_t fields[4];   /* {a,b,c,d}: classes/tipo (a=0 rot? etc) */
    uint32_t off;         /* offset da secção, relativo ao início do sub-anim */
    float    dt;          /* passo de tempo por frame (1/15, 1/30, 1/60) */
} anim_section_t;

/* Um registo de track (12B no asset): a/b/c = contagens de keys por classe,
 * off = offset do stream relativo ao início da secção. */
typedef struct {
    uint16_t a, b, c, d, e, off;
} anim_track_t;

/* Um sub-anim localizado (ex.: "navWalkFast"). */
typedef struct {
    const uint8_t*  base;      /* início do sub-anim no WAD */
    char            name[24];
    float           duration;  /* segundos */
    int             nsections;
    anim_section_t  section[8];
} anim_subanim_t;

/* Localiza o pacote ANM (record "ANM_hero") e devolve ponteiro/base + nclips.
 * 0 = OK. */
typedef struct {
    const uint8_t* pkg;        /* base dos offsets (record+0x20) */
    uint32_t       nclips;
    const uint32_t* clip_off;  /* nclips offsets LE, relativos a pkg */
} anim_pkg_t;

int  anim_pkg_open(const uint8_t* wad, size_t len, anim_pkg_t* out);

/* Enumera clips: nome embutido no header do clip. Devolve o nº de clips. */
int  anim_clip_name(const anim_pkg_t* p, uint32_t i, char out[24]);

/* Procura um sub-anim por nome (ex.: "navWalkFast") varrendo todos os clips.
 * 0 = OK e *out preenchido. */
int  anim_find_subanim(const anim_pkg_t* p, const char* name, anim_subanim_t* out);

/* Nº de tracks numa secção e acesso a um track. */
int  anim_section_ntracks(const anim_subanim_t* s, int sec);
/* 1 se a secção contém tracks de rotação comprimidos (não refs de arma). */
int  anim_section_is_rotation(const anim_subanim_t* s, int sec);
int  anim_section_track(const anim_subanim_t* s, int sec, int t, anim_track_t* out,
                        const uint8_t** stream, uint32_t* stream_len);

/* Decode de UM track para um quaternion no frame `frame` (0..duration*fps).
 * out_quat = (x,y,z,w). Retorna 0 se decodificou, <0 se ainda não suportado.
 * (Transcrição do decoder do jogo — em progresso.) */
int  anim_decode_quat(const anim_track_t* rec, const uint8_t* stream, uint32_t slen,
                      float dt, int frame, float out_quat[4]);

#ifdef __cplusplus
}
#endif
#endif
