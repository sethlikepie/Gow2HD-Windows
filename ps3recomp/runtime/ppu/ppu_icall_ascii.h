#ifndef PPU_ICALL_ASCII_H
#define PPU_ICALL_ASCII_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True when CTR is 2–4 printable ASCII bytes (string-as-code miss). */
int ps3_icall_looks_like_ascii(uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif
