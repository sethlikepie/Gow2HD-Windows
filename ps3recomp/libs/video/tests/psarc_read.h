/*
 * ps3recomp - leitor nativo de PSARC v1.4 (zlib) para tooling/viewers macOS.
 *
 * Port C clean-room do tools/psarc_extract.py (mesma spec publica do formato:
 * header 32B 'PSAR', TOC de entradas de 30B com md5/block_idx/usize/offset,
 * tabela de tamanhos de bloco u16 BE com 0 = bloco cheio armazenado, entry 0
 * = manifesto com 1 path por linha). Le por entrada com pread + inflate por
 * bloco -- nunca carrega o arquivo inteiro (o gow2.psarc tem 7GB).
 */
#ifndef PS3RECOMP_PSARC_READ_H
#define PS3RECOMP_PSARC_READ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct psarc psarc_t;

/* Abre e indexa (header + TOC + tabela de blocos + manifesto).
 * NULL em erro (mensagem em stderr). */
psarc_t* psarc_open(const char* path);
void     psarc_close(psarc_t* p);

/* Numero de entradas nomeadas (sem contar o manifesto). */
uint32_t    psarc_count(const psarc_t* p);
const char* psarc_path(const psarc_t* p, uint32_t i);   /* NULL se fora de range */

/* Extrai a entrada cujo path no manifesto casa com `path` (igualdade exacta,
 * tolerando '/' inicial a mais ou a menos). Retorna buffer malloc'd (caller
 * liberta) e escreve o tamanho em *out_len; NULL se nao existe/erro. */
uint8_t* psarc_extract(psarc_t* p, const char* path, size_t* out_len);

#ifdef __cplusplus
}
#endif
#endif
