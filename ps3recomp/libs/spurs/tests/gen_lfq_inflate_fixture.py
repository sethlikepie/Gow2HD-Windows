#!/usr/bin/env python3
"""Gera a fixture de raw-deflate usada por test_lfq_inflate_main.c.

O fulfiller host (libs/spurs/lfq_host_inflate.c) recebe do dearchiver do FIOS
um comando de 32 bytes cujo `src EA` ja aponta PARA DEPOIS do header zlib de 2
bytes -- ou seja, um stream raw deflate. Reproduzimos isso com
`zlib.compressobj(wbits=-12)`: wbits negativo = raw deflate (sem header nem
adler32), e 12 e' exatamente a janela de 4 KB que o campo +0x1C do elemento
declara (0xFFF4 = -12). Qualquer wbits em [-15,-9] serve para o stb, mas -12
mantem a fixture fiel ao que o jogo emite.

Nao ha dependencia de asset nenhum: o plaintext e' gerado por uma formula
deterministica que o teste em C reimplementa byte a byte, de forma que o
esperado nao precisa de ser baked (so o blob comprimido e' que e'). O hash
FNV-1a do plaintext vai baked tambem, para o teste poder validar que o SEU
gerador coincide com o do Python antes de comparar o que quer que seja.

Uso:  python3 gen_lfq_inflate_fixture.py   (reescreve lfq_inflate_fixture.h)
"""

import os
import zlib

# Alfabeto do plaintext. O padrao (i*i + i//7) % 61 sobre estes 61 bytes gera
# substrings repetidas o suficiente para o deflate emitir matches (nao so
# literais), portanto o teste exercita as duas metades do decoder do stb.
TABLE = (b"abcdefghijklmnopqrstuvwxyz"
         b"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
         b"0123456789")
assert len(TABLE) == 62
TABLE = TABLE[:61]          # 61: primo, evita alinhamento com o passo i//7

PLAIN_LEN = 8192


def gen_plain(n):
    """Tem de ser byte-a-byte identico a lfqi_gen_plain() no teste em C."""
    out = bytearray(n)
    for i in range(n):
        idx = ((i * i + i // 7) & 0xFFFFFFFF) % 61
        out[i] = TABLE[idx]
    return bytes(out)


def fnv1a32(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def c_array(data, per_line=16):
    lines = []
    for off in range(0, len(data), per_line):
        chunk = data[off:off + per_line]
        lines.append("    " + " ".join("0x%02X," % b for b in chunk))
    return "\n".join(lines)


def main():
    plain = gen_plain(PLAIN_LEN)

    # raw deflate: sem header zlib, sem adler32 -- exatamente o que o EA +0x00
    # do elemento aponta. Nivel 9 para a fixture ser pequena e determinista.
    co = zlib.compressobj(9, zlib.DEFLATED, -12)
    blob = co.compress(plain) + co.flush()

    # Sanidade: o round-trip tem de fechar antes de gravarmos seja o que for.
    assert zlib.decompress(blob, -12) == plain, "round-trip raw deflate falhou"

    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "lfq_inflate_fixture.h")

    with open(out, "w") as f:
        f.write("""/* GERADO por gen_lfq_inflate_fixture.py -- nao editar a mao.
 *
 * Fixture raw-deflate (wbits=-12, sem header zlib) para
 * test_lfq_inflate_main.c. O plaintext NAO esta aqui: o teste regenera-o com
 * lfqi_gen_plain(), e LFQI_PLAIN_FNV1A prova que esse gerador em C coincide
 * com o do Python.
 */
#ifndef LFQ_INFLATE_FIXTURE_H
#define LFQ_INFLATE_FIXTURE_H

#include <stdint.h>

#define LFQI_PLAIN_LEN     %d
#define LFQI_PLAIN_FNV1A   0x%08Xu
#define LFQI_DEFLATE_LEN   %d

/* Stream raw deflate de LFQI_PLAIN_LEN bytes de plaintext. */
static const uint8_t lfqi_deflate[LFQI_DEFLATE_LEN] = {
%s
};

#endif /* LFQ_INFLATE_FIXTURE_H */
""" % (PLAIN_LEN, fnv1a32(plain), len(blob), c_array(blob)))

    print("lfq_inflate_fixture.h: plain=%d deflate=%d fnv1a=0x%08X"
          % (PLAIN_LEN, len(blob), fnv1a32(plain)))


if __name__ == "__main__":
    main()
