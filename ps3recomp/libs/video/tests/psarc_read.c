/*
 * ps3recomp - leitor nativo de PSARC v1.4 (zlib) -- ver psarc_read.h.
 *
 * Port C clean-room do tools/psarc_extract.py, mesma semantica:
 *   header 32B: 'PSAR' | ver | 'zlib' | toc_len | entry_size(30) | n | blk | flags
 *   TOC: n x 30B (md5[16], block_idx u32 BE, usize 5B BE, offset 5B BE)
 *   tabela de blocos: u16 BE cada; 0 = bloco cheio ARMAZENADO (blk bytes)
 *   por bloco: tenta inflate; se falhar, trata como armazenado (raw)
 *   entry 0 = manifesto (paths '\n'-separados, mapeiam p/ entries 1..N)
 * Le com pread por entrada -- nunca mapeia/carrega o arquivo inteiro (7GB).
 */
#include "psarc_read.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

typedef struct {
    uint32_t block_idx;
    uint64_t usize;
    uint64_t offset;
} psarc_entry_t;

struct psarc {
    int            fd;
    uint32_t       n_entries;    /* inclui o manifesto (entry 0) */
    uint32_t       blk;
    psarc_entry_t* ent;
    uint32_t       n_blocks;
    uint16_t*      bsz;
    char**         paths;        /* n_entries-1 paths (entries 1..N) */
    uint32_t       n_paths;
};

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t be40(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 5; i++) v = (v << 8) | p[i];
    return v;
}

static int read_at(int fd, uint64_t off, void* buf, size_t len) {
    uint8_t* dst = buf;
    while (len) {
        ssize_t got = pread(fd, dst, len, (off_t)off);
        if (got <= 0) return -1;
        dst += got; off += (uint64_t)got; len -= (size_t)got;
    }
    return 0;
}

/* Descomprime a entrada ei para um buffer malloc'd de usize bytes. */
static uint8_t* entry_read(psarc_t* p, uint32_t ei, size_t* out_len) {
    if (ei >= p->n_entries) return NULL;
    const psarc_entry_t* e = &p->ent[ei];
    uint8_t* out = malloc(e->usize ? e->usize : 1);
    if (!out) return NULL;
    uint8_t* comp = malloc(p->blk + 16);
    uint8_t* work = malloc(p->blk);
    if (!comp || !work) { free(out); free(comp); free(work); return NULL; }

    uint64_t off = e->offset;
    uint64_t done = 0;
    uint32_t bi = e->block_idx;
    int ok = 1;
    while (done < e->usize) {
        if (bi >= p->n_blocks) { ok = 0; break; }
        uint32_t cs = p->bsz[bi++];
        uint32_t rd = cs ? cs : p->blk;
        if (read_at(p->fd, off, comp, rd) != 0) { ok = 0; break; }
        off += rd;

        const uint8_t* piece = comp;
        size_t piece_len = rd;
        if (cs) {   /* tenta inflate; falha => bloco armazenado (raw) */
            z_stream zs;
            memset(&zs, 0, sizeof zs);
            if (inflateInit(&zs) != Z_OK) { ok = 0; break; }
            zs.next_in = comp; zs.avail_in = rd;
            zs.next_out = work; zs.avail_out = p->blk;
            int zrc = inflate(&zs, Z_FINISH);
            size_t produced = p->blk - zs.avail_out;
            inflateEnd(&zs);
            if (zrc == Z_STREAM_END && produced > 0) {
                piece = work;
                piece_len = produced;
            }   /* senao: raw, como o psarc_extract.py (except zlib.error) */
        }
        size_t want = e->usize - done;
        if (piece_len > want) piece_len = want;   /* out[:usize] do Python */
        memcpy(out + done, piece, piece_len);
        done += piece_len;
        if (piece_len == 0) { ok = 0; break; }
    }
    free(comp);
    free(work);
    if (!ok) { free(out); return NULL; }
    if (out_len) *out_len = e->usize;
    return out;
}

psarc_t* psarc_open(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return NULL; }
    uint8_t h[32];
    if (read_at(fd, 0, h, sizeof h) != 0 || memcmp(h, "PSAR", 4) != 0) {
        fprintf(stderr, "[psarc] %s: nao e PSARC\n", path);
        close(fd);
        return NULL;
    }
    uint32_t toc_len = be32(h + 12);
    uint32_t esz     = be32(h + 16);
    uint32_t n       = be32(h + 20);
    uint32_t blk     = be32(h + 24);
    if (esz < 30 || n == 0 || blk == 0 || toc_len < 32 + (uint64_t)n * esz) {
        fprintf(stderr, "[psarc] %s: header invalido (esz=%u n=%u blk=%u toc=%u)\n",
                path, esz, n, blk, toc_len);
        close(fd);
        return NULL;
    }

    psarc_t* p = calloc(1, sizeof *p);
    p->fd = fd;
    p->n_entries = n;
    p->blk = blk;
    p->ent = calloc(n, sizeof *p->ent);

    uint8_t* toc = malloc((size_t)n * esz);
    if (!toc || read_at(fd, 32, toc, (size_t)n * esz) != 0) {
        fprintf(stderr, "[psarc] %s: TOC truncada\n", path);
        free(toc); psarc_close(p);
        return NULL;
    }
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* e = toc + (size_t)i * esz;
        p->ent[i].block_idx = be32(e + 16);
        p->ent[i].usize     = be40(e + 20);
        p->ent[i].offset    = be40(e + 25);
    }
    free(toc);

    uint64_t tbl_off = 32 + (uint64_t)n * esz;
    uint64_t tbl_bytes = toc_len - tbl_off;
    p->n_blocks = (uint32_t)(tbl_bytes / 2);
    p->bsz = malloc(p->n_blocks ? p->n_blocks * sizeof(uint16_t) : 2);
    uint8_t* tb = malloc(tbl_bytes ? tbl_bytes : 1);
    if (!p->bsz || !tb || read_at(fd, tbl_off, tb, tbl_bytes) != 0) {
        fprintf(stderr, "[psarc] %s: tabela de blocos truncada\n", path);
        free(tb); psarc_close(p);
        return NULL;
    }
    for (uint32_t i = 0; i < p->n_blocks; i++)
        p->bsz[i] = (uint16_t)((tb[i * 2] << 8) | tb[i * 2 + 1]);
    free(tb);

    /* manifesto (entry 0) -> paths */
    size_t mlen = 0;
    uint8_t* man = entry_read(p, 0, &mlen);
    if (!man) {
        fprintf(stderr, "[psarc] %s: manifesto ilegivel\n", path);
        psarc_close(p);
        return NULL;
    }
    p->paths = calloc(n, sizeof(char*));
    uint32_t np = 0;
    size_t s = 0;
    while (s < mlen && np + 1 < n) {
        size_t e = s;
        while (e < mlen && man[e] != '\n') e++;
        size_t l = e - s;
        while (l && (man[s + l - 1] == '\r' || man[s + l - 1] == 0)) l--;
        if (l) {
            char* cp = malloc(l + 1);
            memcpy(cp, man + s, l);
            cp[l] = 0;
            p->paths[np++] = cp;
        }
        s = e + 1;
    }
    free(man);
    p->n_paths = np;
    return p;
}

void psarc_close(psarc_t* p) {
    if (!p) return;
    if (p->fd >= 0) close(p->fd);
    if (p->paths)
        for (uint32_t i = 0; i < p->n_paths; i++) free(p->paths[i]);
    free(p->paths);
    free(p->ent);
    free(p->bsz);
    free(p);
}

uint32_t psarc_count(const psarc_t* p) { return p ? p->n_paths : 0; }

const char* psarc_path(const psarc_t* p, uint32_t i) {
    return (p && i < p->n_paths) ? p->paths[i] : NULL;
}

static int path_eq(const char* a, const char* b) {
    if (a[0] == '/') a++;
    if (b[0] == '/') b++;
    return strcmp(a, b) == 0;
}

uint8_t* psarc_extract(psarc_t* p, const char* path, size_t* out_len) {
    if (!p || !path) return NULL;
    for (uint32_t i = 0; i < p->n_paths; i++)
        if (path_eq(p->paths[i], path))
            return entry_read(p, i + 1, out_len);   /* paths -> entries 1..N */
    return NULL;
}
