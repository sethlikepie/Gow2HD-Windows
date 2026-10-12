#include "ppu_icall_ascii.h"

int ps3_icall_looks_like_ascii(uint32_t addr)
{
    int pr = 0, ok = 1;
    for (int i = 0; i < 4; i++) {
        unsigned char c = (unsigned char)((addr >> (8 * i)) & 0xFFu);
        if (c == 0)
            continue;
        if (c >= 0x20u && c < 0x7Fu)
            pr++;
        else {
            ok = 0;
            break;
        }
    }
    return ok && pr >= 2;
}
