/*
 * ps3recomp - extraccao nativa de assets dos WADs do GoW2 (malha + textura).
 *
 * Port C clean-room de gen_wad_mesh_rxs.py (malha MODL: directoria de
 * atributos de 16B, streams de vertice com header de 12B, POS0 (x,y,z,w)
 * f32 BE stride 16, TEX0 half BE stride 4, descritor com index_count em +12
 * e lista de triangulos u32 BE) e de decode_hero_tex.py (textura: framing
 * [type u32][sz u32 LE][nome 24B][corpo], header BE +8/+24/+36, sub-header
 * derivado = payload - mip_chain, base ARGB8 Morton -- deswizzle via o
 * proprio rsx_texture_deswizzle.h do engine).
 */
#ifndef PS3RECOMP_WAD_ASSETS_H
#define PS3RECOMP_WAD_ASSETS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t  nverts;
    float*    vtx5;    /* nverts * (x,y,z,u,v) intercalado, LE nativo (= KMH2) */
    uint32_t  ntris;
    uint32_t* idx;     /* ntris * 3, so triangulos validos */
} wad_mesh_t;

/* Extrai a malha do MODL em `modl_off`. 0 = OK (out preenchido, caller chama
 * wad_mesh_free); -1 = erro (formato/limites). */
int  wad_mesh_extract(const uint8_t* wad, size_t len, uint32_t modl_off,
                      wad_mesh_t* out);
void wad_mesh_free(wad_mesh_t* m);

/* Localiza o registo de textura cujo nome comeca por `tag` (pulando entradas
 * de manifesto sz<64), decodifica o nivel base ARGB8 (busca de sub-header por
 * coerencia, Morton vs linear) e devolve RGBA8 malloc'd. 0 = OK. */
int  wad_tex_decode(const uint8_t* wad, size_t len, const char* tag,
                    uint8_t** rgba_out, uint32_t* w_out, uint32_t* h_out);

/* Skinning por vertice (RE do stream BONI + peso w do POS0): 2 bones por
 * vertice -- BONI bytes 0/1 = indices (paleta local do segmento, <=16;
 * bytes 2/3 sempre 0 no material real), w = 4o float do POS0 (peso do bone
 * 0; NaN -> 1.0). Mesma regra de +12 dos outros streams. */
typedef struct {
    uint32_t nverts;
    uint8_t* b0;     /* indice do bone principal, por vertice */
    uint8_t* b1;     /* indice do bone secundario */
    float*   w;      /* peso do bone principal (0..1) */
} wad_skin_t;

int  wad_skin_extract(const uint8_t* wad, size_t len, uint32_t modl_off,
                      wad_skin_t* out);
void wad_skin_free(wad_skin_t* s);

/* ---- RIG REAL (RE 2026-07-22, notes/2026-07-22-gow2-rig-anmx-re.md) ----
 * Esqueleto: tabela de nomes 24B/bone (ancora: "pelvis" = bone 2, "neck" =
 * bone 6), hierarquia 16B/bone antes dos nomes (parent = rec+1), matrizes
 * bind LOCAIS 4x4 f32 LE (column-major, t em [12..14]) depois dos nomes.
 * Segmentos de skinning apos o index buffer do MODL: records
 * {head 00 SUB 00, u32be first_vert,num_verts,first_idx,num_idx,nb,
 *  bones_globais[nb], f32be bbox[6]} -- validados por invariantes exatas
 * (somas = nverts/nidx, b0/b1 < nb, pacotes auto-contidos). */
#define WAD_RIG_MAX_BONES 192

typedef struct {
    uint32_t nbones;
    int16_t  parent[WAD_RIG_MAX_BONES];      /* -1 = raiz */
    float*   local_bind;                     /* nbones * 16 f32 column-major */
    char*    names;                          /* nbones * 24, NUL-padded */
    uint32_t nverts;
    uint8_t* g0;                             /* bone GLOBAL por vertice */
    uint8_t* g1;
    float*   w;                              /* peso do g0 (0..1) */
    /* grupos de material: runs contiguas de segmentos com o mesmo sub-id
     * (byte do head do record), em TRIANGULOS FILTRADOS -- mesma ordem/
     * criterio do wad_mesh_extract, entao casam com o index buffer dele. */
    uint32_t ngroups;
    uint32_t grp_first[8];
    uint32_t grp_count[8];
    uint8_t  grp_sub[8];
} wad_rig_t;

/* Extrai esqueleto + mapeamento vertice->bone global. Valida as invariantes
 * dos segmentos (falha ruidosa se nao baterem). 0 = OK. */
int  wad_rig_extract(const uint8_t* wad, size_t len, uint32_t modl_off,
                     wad_rig_t* out);
void wad_rig_free(wad_rig_t* r);
int  wad_rig_find_bone(const wad_rig_t* r, const char* name);   /* -1 se nao */

/* Enumera todos os MODLs do WAD (tag 'MODL' seguido da entry 'POS0' na
 * directoria). Devolve quantos escreveu em out[]. */
int  wad_find_modls(const uint8_t* wad, size_t len, uint32_t* out, int max);

#ifdef __cplusplus
}
#endif
#endif
