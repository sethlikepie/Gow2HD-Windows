/*
 * ps3recomp - Frame dump helper
 */

#include "rsx_frame_dump.h"
#include <stdio.h>
#include <stdlib.h>

static void write_u16_le(FILE* f, u32 v)
{
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}

static void write_u32_le(FILE* f, u32 v)
{
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
    fputc((int)((v >> 16) & 0xFF), f);
    fputc((int)((v >> 24) & 0xFF), f);
}

int rsx_write_bmp_bgra32(const char* path, const u8* pixels, u32 w, u32 h)
{
    if (!path || !pixels || w == 0 || h == 0) {
        return -1;
    }

    const u32 stride   = w * 4u;
    const u32 pix_size = stride * h;
    const u32 data_off = 14u + 40u;

    FILE* f = fopen(path, "wb");
    if (!f) {
        return -1;
    }

    fputc('B', f); fputc('M', f);
    write_u32_le(f, data_off + pix_size);  /* file size */
    write_u16_le(f, 0);                    /* reserved */
    write_u16_le(f, 0);
    write_u32_le(f, data_off);

    write_u32_le(f, 40);                   /* DIB header size */
    write_u32_le(f, w);
    write_u32_le(f, (u32)(-(s32)h));       /* negative => rows are top-down */
    write_u16_le(f, 1);                    /* planes */
    write_u16_le(f, 32);                   /* bits per pixel */
    write_u32_le(f, 0);                    /* BI_RGB, uncompressed */
    write_u32_le(f, pix_size);
    write_u32_le(f, 2835);                 /* 72 DPI, x */
    write_u32_le(f, 2835);                 /* 72 DPI, y */
    write_u32_le(f, 0);                    /* palette colours */
    write_u32_le(f, 0);                    /* important colours */

    size_t written = fwrite(pixels, 1, pix_size, f);
    int rc = (fclose(f) == 0 && written == pix_size) ? 0 : -1;
    if (rc != 0) {
        remove(path);
    }
    return rc;
}

int rsx_frame_dump_enabled(void)
{
    const char* v = getenv("PS3_FRAME_DUMP");
    return (v && *v && *v != '0') ? 1 : 0;
}
