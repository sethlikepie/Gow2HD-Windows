/*
 * ps3_unself -- decrypt a PS3 NPDRM SELF (EBOOT.BIN) into a plain ELF.
 *
 *   ps3_unself <EBOOT.BIN> <out.elf> [--rap <CONTENT-ID.rap> | --klic <32 hex>]
 *
 * For titles bought on PSN the executable is bound to the owner's license:
 * the .rap file (dev_hdd0/home/<user>/exdata in RPCS3, or dumped from a PS3)
 * yields the klicensee. Free-license titles need nothing extra.
 *
 * Layers, outermost first:
 *   1. rap -> klicensee (AES-128 + the rap2rif permutation rounds);
 *   2. metadata info (0x40 B): AES-128-CBC with the klicensee decrypted by
 *      NP_KLIC_KEY, then AES-256-CBC with the revision's erk/riv;
 *   3. metadata headers, section headers and section keys: AES-128-CTR with
 *      the key/iv from (2);
 *   4. each program segment: AES-128-CTR with its keys, then zlib.
 * The ELF is rebuilt from the plain ELF/program/section headers the SELF
 * carries, with each segment at its p_offset.
 *
 * Written from the public format description (psdevwiki: SELF - SPRX,
 * NPDRM, RAP). Crypto: CommonCrypto (macOS). Compression: zlib.
 */
#include <CommonCrypto/CommonCryptor.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct { uint16_t rev; uint8_t erk[32]; uint8_t riv[16]; } npdrm_key;
static const npdrm_key k_npdrm_keys[] = {
#include "npdrm_keys.inc"
};

static const uint8_t NP_KLIC_KEY[16] = {0xF2,0xFB,0xCA,0x7A,0x75,0xB0,0x4E,0xDC,0x13,0x90,0x63,0x8C,0xCD,0xFD,0xD1,0xEE};
static const uint8_t NP_KLIC_FREE[16] = {0x72,0xF9,0x90,0x78,0x8F,0x9C,0xFF,0x74,0x57,0x25,0xF0,0x8E,0x4C,0x12,0x83,0x87};
static const uint8_t RAP_KEY[16] = {0x86,0x9F,0x77,0x45,0xC1,0x3F,0xD8,0x90,0xCC,0xF2,0x91,0x88,0xE3,0xCC,0x3E,0xDF};
static const uint8_t RAP_PBOX[16] = {0x0C,0x03,0x06,0x04,0x01,0x0B,0x0F,0x08,0x02,0x07,0x00,0x05,0x0A,0x0E,0x0D,0x09};
static const uint8_t RAP_E1[16] = {0xA9,0x3E,0x1F,0xD6,0x7C,0x55,0xA3,0x29,0xB7,0x5F,0xDD,0xA6,0x2A,0x95,0xC7,0xA5};
static const uint8_t RAP_E2[16] = {0x67,0xD4,0x5D,0xA3,0x29,0x6D,0x00,0x6A,0x4E,0x7C,0x53,0x7B,0xF5,0x53,0x8C,0x74};

static void die(const char* msg) { fprintf(stderr, "ps3_unself: %s\n", msg); exit(1); }

static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t be64(const uint8_t* p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }

/* AES decrypt, ECB or CBC, no padding, in place. */
static void aes_dec(const uint8_t* key, size_t keylen, const uint8_t* iv, uint8_t* buf, size_t len)
{
    size_t moved = 0;
    CCCryptorStatus st = CCCrypt(kCCDecrypt, kCCAlgorithmAES, iv ? 0 : kCCOptionECBMode,
                                 key, keylen, iv, buf, len, buf, len, &moved);
    if (st != kCCSuccess || moved != len) die("AES decrypt failed");
}

/* AES-128-CTR (big-endian counter), in place. */
static void aes_ctr(const uint8_t* key, const uint8_t* iv, uint8_t* buf, size_t len)
{
    CCCryptorRef c = NULL;
    if (CCCryptorCreateWithMode(kCCEncrypt, kCCModeCTR, kCCAlgorithmAES, ccNoPadding, iv, key, 16,
                                NULL, 0, 0, kCCModeOptionCTR_BE, &c) != kCCSuccess)
        die("AES-CTR setup failed");
    size_t moved = 0;
    if (CCCryptorUpdate(c, buf, len, buf, len, &moved) != kCCSuccess || moved != len) die("AES-CTR failed");
    CCCryptorRelease(c);
}

static void rap_to_klicensee(const uint8_t rap[16], uint8_t out[16])
{
    uint8_t key[16], iv[16] = {0};
    memcpy(key, rap, 16);
    aes_dec(RAP_KEY, 16, iv, key, 16);
    for (int round = 0; round < 5; round++) {
        for (int i = 0; i < 16; i++) key[RAP_PBOX[i]] ^= RAP_E1[RAP_PBOX[i]];
        for (int i = 15; i >= 1; i--) key[RAP_PBOX[i]] ^= key[RAP_PBOX[i - 1]];
        int o = 0;
        for (int i = 0; i < 16; i++) {
            int p = RAP_PBOX[i];
            uint8_t kc = (uint8_t)(key[p] - o), ec2 = RAP_E2[p];
            if (o != 1 || kc != 0xFF) { o = kc < ec2; key[p] = (uint8_t)(kc - ec2); }
            else key[p] = (uint8_t)(kc - ec2);
        }
    }
    memcpy(out, key, 16);
}

static uint8_t* read_file(const char* path, size_t* n)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* b = malloc((size_t)sz ? (size_t)sz : 1);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) die("read failed");
    fclose(f); *n = (size_t)sz; return b;
}

static int hexbytes(const char* s, uint8_t* out, size_t n)
{
    for (size_t i = 0; i < n; i++) { unsigned v; if (sscanf(s + 2 * i, "%2x", &v) != 1) return 0; out[i] = (uint8_t)v; }
    return strlen(s) == 2 * n;
}

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: ps3_unself <EBOOT.BIN> <out.elf> [--rap FILE | --klic HEX]\n"); return 2; }
    const char *rap_path = NULL, *klic_hex = NULL;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--rap")) rap_path = argv[i + 1];
        else if (!strcmp(argv[i], "--klic")) klic_hex = argv[i + 1];
    }
    size_t n = 0;
    uint8_t* f = read_file(argv[1], &n);
    if (!f) die("cannot open input");
    if (n < 0x80 || memcmp(f, "SCE\0", 4)) die("not a SELF (no SCE header)");

    const uint16_t key_rev = be16(f + 0x08);
    const uint32_t meta_off = be32(f + 0x0C);
    const uint64_t hdr_len = be64(f + 0x10);
    const uint64_t app_off = be64(f + 0x28), elf_off = be64(f + 0x30), phdr_off = be64(f + 0x38),
                   shdr_off = be64(f + 0x40), ctrl_off = be64(f + 0x58), ctrl_size = be64(f + 0x60);
    const uint32_t self_type = be32(f + app_off + 0x0C);
    if (key_rev & 0x8000) die("debug SELF: not encrypted this way");
    if (self_type != 8) die("not an NPDRM application SELF (only NPDRM is supported)");

    /* NPDRM control info: 'NPD\0', version, license, app type, content id. */
    const uint8_t* npd = NULL;
    for (uint64_t o = ctrl_off; o + 0x40 <= ctrl_off + ctrl_size && o + 0x40 <= n; o += 4)
        if (!memcmp(f + o, "NPD\0", 4)) { npd = f + o; break; }
    if (!npd) die("no NPDRM header");
    const uint32_t license = be32(npd + 8);
    char content_id[0x31] = {0};
    memcpy(content_id, npd + 0x10, 0x30);

    uint8_t klic[16];
    if (license == 3) memcpy(klic, NP_KLIC_FREE, 16);
    else if (klic_hex) { if (!hexbytes(klic_hex, klic, 16)) die("--klic wants 32 hex digits"); }
    else if (rap_path) {
        size_t rn = 0; uint8_t* rap = read_file(rap_path, &rn);
        if (!rap || rn != 16) die("the .rap file must be 16 bytes");
        rap_to_klicensee(rap, klic); free(rap);
    } else {
        fprintf(stderr, "ps3_unself: %s needs its license: --rap %s.rap\n", content_id, content_id);
        return 3;
    }

    const npdrm_key* k = NULL;
    for (size_t i = 0; i < sizeof k_npdrm_keys / sizeof k_npdrm_keys[0]; i++)
        if (k_npdrm_keys[i].rev == key_rev) { k = &k_npdrm_keys[i]; break; }
    if (!k) die("no NPDRM key for this key revision");

    /* (2) metadata info */
    uint8_t info[0x40];
    if ((uint64_t)meta_off + 0x20 + 0x40 > n) die("truncated metadata");
    memcpy(info, f + meta_off + 0x20, 0x40);
    uint8_t npkey[16], zero_iv[16] = {0};
    memcpy(npkey, klic, 16);
    aes_dec(NP_KLIC_KEY, 16, NULL, npkey, 16);
    aes_dec(npkey, 16, zero_iv, info, 0x40);
    uint8_t riv[16]; memcpy(riv, k->riv, 16);
    aes_dec(k->erk, 32, riv, info, 0x40);
    for (int i = 0; i < 16; i++)
        if (info[0x10 + i] || info[0x30 + i]) die("metadata did not decrypt (wrong license or key)");

    /* (3) metadata header, section headers, keys */
    const uint64_t mh_off = meta_off + 0x20 + 0x40;
    if (hdr_len <= mh_off || hdr_len > n) die("bad header length");
    const size_t mh_len = (size_t)(hdr_len - mh_off);
    uint8_t* mh = malloc(mh_len);
    memcpy(mh, f + mh_off, mh_len);
    aes_ctr(info, info + 0x20, mh, mh_len);
    const uint32_t sec_count = be32(mh + 0x0C), key_count = be32(mh + 0x10);
    const uint8_t* secs = mh + 0x20;
    const uint8_t* keys = secs + (size_t)sec_count * 0x30;
    if (0x20 + (size_t)sec_count * 0x30 + (size_t)key_count * 16 > mh_len) die("bad metadata counts");

    /* ELF64 headers */
    const uint8_t* eh = f + elf_off;
    if (memcmp(eh, "\x7F" "ELF", 4) || eh[4] != 2) die("only ELF64 is supported");
    const uint64_t e_phoff = be64(eh + 0x20), e_shoff = be64(eh + 0x28);
    const uint16_t e_phnum = be16(eh + 0x38), e_shnum = be16(eh + 0x3C);
    size_t out_len = (size_t)(e_phoff + (uint64_t)e_phnum * 0x38);
    for (int i = 0; i < e_phnum; i++) {
        const uint8_t* ph = f + phdr_off + (size_t)i * 0x38;
        uint64_t end = be64(ph + 0x08) + be64(ph + 0x20);
        if (end > out_len) out_len = (size_t)end;
    }
    if (e_shoff && shdr_off && e_shoff + (uint64_t)e_shnum * 0x40 > out_len)
        out_len = (size_t)(e_shoff + (uint64_t)e_shnum * 0x40);
    uint8_t* out = calloc(1, out_len);
    memcpy(out, eh, 0x40);
    memcpy(out + e_phoff, f + phdr_off, (size_t)e_phnum * 0x38);
    if (e_shoff && shdr_off) memcpy(out + e_shoff, f + shdr_off, (size_t)e_shnum * 0x40);

    /* (4) program segments */
    int written = 0;
    for (uint32_t s = 0; s < sec_count; s++) {
        const uint8_t* sh = secs + (size_t)s * 0x30;
        const uint64_t off = be64(sh), size = be64(sh + 8);
        const uint32_t type = be32(sh + 0x10), prog = be32(sh + 0x14), enc = be32(sh + 0x20),
                       kidx = be32(sh + 0x24), ividx = be32(sh + 0x28), comp = be32(sh + 0x2C);
        if (type != 2 || prog >= e_phnum || !size) continue;
        if (off + size > n) die("segment past end of file");
        const uint8_t* ph = f + phdr_off + (size_t)prog * 0x38;
        const uint64_t p_offset = be64(ph + 0x08), p_filesz = be64(ph + 0x20);
        uint8_t* data = malloc((size_t)size);
        memcpy(data, f + off, (size_t)size);
        if (enc == 3) {
            if (kidx >= key_count || ividx >= key_count) die("bad segment key index");
            aes_ctr(keys + (size_t)kidx * 16, keys + (size_t)ividx * 16, data, (size_t)size);
        }
        if (comp == 2) {
            uLongf dl = (uLongf)p_filesz;
            if (uncompress(out + p_offset, &dl, data, (uLong)size) != Z_OK || dl != p_filesz)
                die("segment did not inflate");
        } else {
            if (size > p_filesz) die("segment larger than p_filesz");
            memcpy(out + p_offset, data, (size_t)size);
        }
        free(data);
        written++;
    }
    FILE* o = fopen(argv[2], "wb");
    if (!o || fwrite(out, 1, out_len, o) != out_len) die("cannot write output");
    fclose(o);
    fprintf(stderr, "ps3_unself: %s -> %s (%zu bytes, %d segments, key rev %u, license %u)\n",
            content_id, argv[2], out_len, written, key_rev, license);
    free(out); free(mh); free(f);
    return 0;
}
