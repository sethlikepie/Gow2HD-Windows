/*
 * ps3recomp - cellFs VFS (sys_fs HLE)
 *
 * Backs the game's file I/O with the real host filesystem so it can load its
 * data/config/assets. Guest PS3 paths (/dev_bdvd/..., /app_home/..., /dev_hdd0,
 * etc.) are translated to a host root (the game directory that contains
 * PS3_GAME), set by the boot harness from the EBOOT path (or $PS3_VFS_ROOT).
 *
 * Only the calls the boot actually imports are implemented:
 *   cellFsOpen/Close/Read/Write/Lseek/Stat/Fstat/Opendir/Readdir/Closedir/
 *   Mkdir/Rmdir/Unlink/Fsync.
 *
 * Context-aware HLE: guest pointers (path, buffers, out params) are read/written
 * through vm_base in big-endian.
 */
#ifdef _WIN32
#include <windows.h>
#endif
#include "ppu_recomp.h"      /* ppu_context */
#include "ps3emu/nid.h"      /* ps3_compute_nid */
#include "ps3emu/ps3_mods.h" /* PS3_MODS_DIR overlay */
#include "ps3_checkpoint.h"
#include "ps3_trace.h"       /* Task 5 (bringup-instrumentacao-fase0): subsistema FIOS */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>          /* open/close on MinGW */
extern "C" void ps3_watch_store_bulk(uint32_t ea, uint32_t len, const char* who); /* winport: mingw */
#else
#include <unistd.h>

/* PS3_WATCH_STORE: watch de escritas HOST em bloco (ppu_loader.cpp). */
extern "C" void ps3_watch_store_bulk(uint32_t ea, uint32_t len, const char* who);

/* _ftelli64 / _fseeki64 sao MSVC/MinGW. Em POSIX os equivalentes de 64 bits sao
 * ftello/fseeko (off_t e 64 bits no macOS e no Linux com _FILE_OFFSET_BITS=64).
 * Mapeados aqui para o codigo partilhado nao precisar de saber a diferenca. */
#if !defined(_WIN32)
  #include <stdio.h>
  #define _ftelli64(f)          ((long long)ftello(f))
  #define _fseeki64(f, o, w)    fseeko((f), (off_t)(o), (w))
#endif
#endif
#ifndef O_BINARY
#define O_BINARY 0       /* POSIX has no text/binary distinction */
#endif

extern "C" uint8_t* vm_base;
extern "C" uint32_t ppu_vm_size;
extern "C" void     ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));
extern "C" void     vm_write32(uint64_t a, uint32_t v);
extern "C" void     vm_write64(uint64_t a, uint64_t v);
extern "C" void     movie_hle_start(const char* host_path);   /* libs/video/movie_hle.c */
extern "C" void     movie_hle_start_once(const char* host_path);
extern "C" void     movie_hle_stop(void);

/* ---- movie_io: the host-backed data path (libs/video/movie_hle.c) --------
 *
 * PS3_MOVIE_CACHE holds psarc members that were extracted ahead of time
 * (R_PermA.wad_ps3, R_LglScA.wad_ps3, the intro movie). movie_io serves those
 * bytes straight off the host, bypassing the guest FIOS scheduler + psarc
 * dearchiver + SPU inflate chain that would otherwise have to produce them.
 *
 * It hands back a TAGGED fd (0x4D56xxxx, 'MV' in the high half) rather than an
 * index into g_files[]. Every handler below therefore has to ask movie_io_is()
 * BEFORE it range-checks the fd against FS_MAX -- a tagged fd is a large
 * positive integer and would otherwise be rejected as out of range.
 *
 * movie_hle.c exports no header; hook sites declare what they use. */
extern "C" int                movie_io_is(unsigned fd);
extern "C" unsigned           movie_io_open(const char* guest_path, unsigned* out_size);
extern "C" unsigned long long movie_io_stat(const char* guest_path);
extern "C" unsigned           movie_io_pread(unsigned fd, void* dst, unsigned n, unsigned pos);
extern "C" unsigned           movie_io_read_cur(unsigned fd, void* dst, unsigned n);
extern "C" long long          movie_io_seek(unsigned fd, long long off, int whence);
extern "C" void               movie_io_close(unsigned fd);

/* PS3_MOVIE_IO gate. Off by default: with no cache populated movie_io would
 * just log a miss for every open the game makes. -1 = not yet read. */
static int g_movie_io_gate = -1;
static bool movie_io_enabled(void)
{
    if (g_movie_io_gate < 0) {
        const char* e = getenv("PS3_MOVIE_IO");
        g_movie_io_gate = (e && *e && *e != '0') ? 1 : 0;
    }
    return g_movie_io_gate == 1;
}

/* Host root that the PS3 mount points map into (dir containing PS3_GAME). */
extern "C" const char* ppu_vfs_root = ".";

/* CELL_FS return / mode / flag constants. */
#define CELL_OK              0
#define CELL_FS_ENOENT       (-2147418090)   /* 0x80010006 */
#define CELL_FS_EIO          (-2147418111)
#define CELL_FS_S_IFDIR      0x4000u
#define CELL_FS_S_IFREG      0x8000u
#define CELL_FS_O_RDONLY     0
#define CELL_FS_O_WRONLY     1
#define CELL_FS_O_RDWR       2
#define CELL_FS_O_CREAT      0x0200
#define CELL_FS_O_TRUNC      0x0400
#define CELL_FS_O_APPEND     0x0100
#define CELL_FS_SEEK_SET     0
#define CELL_FS_SEEK_CUR     1
#define CELL_FS_SEEK_END     2
#define CELL_FS_TYPE_DIR     1
#define CELL_FS_TYPE_REG     2

/* ---- guest memory string helpers ---- */
static void guest_strcpy(char* dst, uint32_t gaddr, size_t cap)
{
    size_t i = 0;
    for (; i < cap - 1; i++) {
        if (ppu_vm_size && gaddr + i >= ppu_vm_size) break;
        char c = (char)vm_base[gaddr + i];
        if (!c) break;
        dst[i] = c;
    }
    dst[i] = 0;
}

/* Translate a guest path to a host path under ppu_vfs_root. Known PS3 mount
 * prefixes are stripped; the rest is appended to the root. */
static void host_path(char* out, size_t cap, const char* guest)
{
    const char* rel = guest;
    static const char* mounts[] = {
        "/dev_bdvd/", "/app_home/", "/dev_hdd0/", "/dev_hdd1/",
        "/dev_flash/", "/host_root/", "/dev_usb000/", "/dev_usb/"
    };
    for (size_t i = 0; i < sizeof(mounts)/sizeof(mounts[0]); i++) {
        size_t n = strlen(mounts[i]);
        if (strncmp(guest, mounts[i], n) == 0) { rel = guest + n; break; }
    }
    if (rel == guest && guest[0] == '/') rel = guest + 1;   /* strip leading '/' */
    snprintf(out, cap, "%s/%s", ppu_vfs_root, rel);
    for (char* p = out; *p; p++) if (*p == '\\') *p = '/';
}

static bool host_exists(const char* p)
{
#ifdef _WIN32
    struct _stat64 st; return _stat64(p, &st) == 0;
#else
    struct stat st;    return stat(p, &st) == 0;
#endif
}

/* Strip one leading "<seg>/" from rel if seg matches want (or, when want is
 * null, unconditionally). Returns the new pointer, or rel when it did not
 * apply. */
static const char* drop_segment(const char* rel, const char* want)
{
    const char* slash = strchr(rel, '/');
    if (!slash) return rel;
    size_t n = (size_t)(slash - rel);
    if (want && (strlen(want) != n || strncmp(rel, want, n) != 0)) return rel;
    return slash + 1;
}

/* Full guest->host resolution for the calls that touch a real file.
 *
 * host_path() alone maps the mount point and nothing else, which assumes the
 * host root is rooted at exactly the depth the mount denotes. That does not
 * hold for the layout the tooling actually produces. GoW2 asks for
 *
 *     /dev_hdd0/game/NPUA80491/USRDIR/gow2.psarc
 *
 * while PS3_VFS_ROOT points at <...>/extracted/USRDIR -- the directory whose
 * CONTENTS are USRDIR's. Stripping only "/dev_hdd0/" leaves
 * "game/NPUA80491/USRDIR/gow2.psarc" to append to a root that already ends in
 * USRDIR, so the archive resolved to a doubled path that does not exist and
 * every open of it returned ENOENT. FIOS then reported
 * "fios open archive '/gow2.psarc' failed at open" and no WAD was ever
 * requested -- the whole asset pipeline was dead at its first step.
 *
 * On a real PS3 "/dev_hdd0/game/<TITLEID>/" and "/dev_bdvd/PS3_GAME/" both
 * denote the game root, which is what PS3_VFS_ROOT denotes on the host. So
 * after the literal mapping fails, retry with those install-layer segments
 * folded away, and let a root that already points one level deeper absorb the
 * matching leading segment.
 *
 * The literal mapping is still tried FIRST and wins whenever it exists, so a
 * host tree that really does have <root>/game/<ID>/USRDIR/... keeps working.
 * That makes this purely additive -- it can only turn an ENOENT into a hit,
 * never redirect a path that already resolved. Windows included: same code,
 * no #ifdef. */
static void host_path_resolve(char* out, size_t cap, const char* guest)
{
    host_path(out, cap, guest);
    if (!host_exists(out)) {
        const char* rel = guest;
        static const char* mounts[] = {
            "/dev_bdvd/", "/app_home/", "/dev_hdd0/", "/dev_hdd1/",
            "/dev_flash/", "/host_root/", "/dev_usb000/", "/dev_usb/"
        };
        for (size_t i = 0; i < sizeof(mounts)/sizeof(mounts[0]); i++) {
            size_t n = strlen(mounts[i]);
            if (strncmp(guest, mounts[i], n) == 0) { rel = guest + n; break; }
        }
        if (rel == guest && guest[0] == '/') rel = guest + 1;

        /* "game/<TITLEID>/" (HDD install) and "PS3_GAME/" (disc) are the layers
         * between the mount point and the game root. */
        const char* after = drop_segment(rel, "game");
        if (after != rel) after = drop_segment(after, NULL);   /* the <TITLEID> */
        after = drop_segment(after, "PS3_GAME");

        /* If the root already ends with the next segment (root=<...>/USRDIR and
         * rel=USRDIR/...), that segment is the root itself, not a child. */
        const char* rbase = strrchr(ppu_vfs_root, '/');
        { const char* bs_ = strrchr(ppu_vfs_root, '\\');   /* winport: mingw */ /* Windows separators */
          if (bs_ && (!rbase || bs_ > rbase)) rbase = bs_; }
        rbase = rbase ? rbase + 1 : ppu_vfs_root;
        if (*rbase) after = drop_segment(after, rbase);

        if (after != rel) {
            char cand[1400];
            snprintf(cand, sizeof cand, "%s/%s", ppu_vfs_root, after);
            for (char* p = cand; *p; p++) if (*p == '\\') *p = '/';
            if (host_exists(cand)) snprintf(out, cap, "%s", cand);
        }
    }
    /* Mods replace a resolved host file when PS3_MODS_DIR is set. Additive:
     * miss leaves out[] as the VFS path. */
    ps3_mods_overlay_host(ppu_vfs_root, out, cap);
}

/* ---- fd / dir handle tables ---- */
#define FS_MAX 256
static FILE* g_files[FS_MAX];
static DIR*  g_dirs[FS_MAX];
static char  g_dir_path[FS_MAX][1024];   /* host path per open dir (for readdir stat) */
/* PS3_SKIP_MOVIES: mark .m2v fds so reads return EOF immediately -> the intro
 * movie player treats the video as finished and advances to the menu (the real
 * 3D-render target) without a working MPEG decoder. */
static char  g_fd_is_movie[FS_MAX];
/* fds holding an open .m2v (independent of PS3_SKIP_MOVIES): closing one
 * stops the PS3_MOVIE_HLE host playback. */
static char  g_fd_is_m2v[FS_MAX];
/* winport v48-movies: fds on *.psarc -- their reads tell movie_hle.c which movie the game streams */
static char  g_wp48_psarc[FS_MAX];
#ifdef _WIN32
extern "C" void movie_wp48_psarc_read(unsigned long long pos, unsigned n);
#endif

static int fd_alloc_file(FILE* f)
{
    for (int i = 3; i < FS_MAX; i++) if (!g_files[i] && !g_dirs[i]) { g_files[i] = f; return i; }
    return -1;
}
static int fd_alloc_dir(DIR* d)
{
    for (int i = 3; i < FS_MAX; i++) if (!g_files[i] && !g_dirs[i]) { g_dirs[i] = d; return i; }
    return -1;
}

/* ---- handlers ---- */
static void cellFsOpen(ppu_context* ctx)
{
    char gpath[1024], hpath[1100];
    guest_strcpy(gpath, (uint32_t)ctx->gpr[3], sizeof gpath);
    uint32_t flags  = (uint32_t)ctx->gpr[4];
    uint32_t fd_ptr = (uint32_t)ctx->gpr[5];
    host_path_resolve(hpath, sizeof hpath, gpath);

    /* PS3_MOVIE_IO: offer the path to the host cache FIRST. A hit means the
     * asset was pre-extracted from the psarc and is served directly, which is
     * the only way the WAD stream completes without the FIOS + SPU inflate
     * chain. A miss returns 0 and we fall through to the real host file below,
     * so this can only add opens, never take one away.
     *
     * Read-only opens only: the cache is a read-only mirror of psarc members,
     * and answering a writable open from it would silently drop the writes. */
    if (movie_io_enabled() && (flags & 0x3) == CELL_FS_O_RDONLY) {
        unsigned msz = 0;
        unsigned mfd = movie_io_open(gpath, &msz);
        if (mfd) {
            if (fd_ptr) vm_write32(fd_ptr, mfd);
            ctx->gpr[3] = CELL_OK;
            /* Task 2 (bringup-instrumentacao-fase0): FIRST_FS_OPEN. Este e' o
             * sitio REALMENTE exercitado pelo boot do GoW2 (cellFsOpen aqui e'
             * o handler ctx-based registado via ps3_hle_register_ctx, que
             * ps3_hle_call() consulta antes da tabela generica onde
             * libs/filesystem/cellFs.c:cellFsOpen -- o anchor dado no brief --
             * fica registado mas nunca e' de facto alcancado; ver relatorio). */
            ps3_checkpoint_fire(PS3_CP_FIRST_FS_OPEN, "cellFsOpen", "path=%s", gpath);
            return;
        }
    }

    /* PS3_NOMOVIES: fail .m2v opens with ENOENT so the movie player treats the
     * intro as absent (skip) rather than empty (read-EOF). Different code path
     * than PS3_SKIP_MOVIES; some players skip a missing movie but hang on an
     * empty one. Experiment toward reaching the menu (real 3D-render target). */
    { static int nm = -1; if (nm < 0) nm = getenv("PS3_NOMOVIES") ? 1 : 0;
      size_t L = strlen(gpath);
      if (nm && L >= 4 && (strcmp(gpath+L-4, ".m2v")==0 || strcmp(gpath+L-4, ".M2V")==0)) {
          fprintf(stderr, "[fs] NOMOVIES: fail open '%s' (ENOENT)\n", gpath);
          /* Still play the movie on the host overlay (single pass -- there is
           * no close event to stop a loop): the game skips its decoder path
           * and keeps booting underneath while the player watches the intro. */
          movie_hle_start_once(hpath);
          ctx->gpr[3] = (uint64_t)(int64_t)CELL_FS_ENOENT; return; } }

    /* fopen() mode strings can't express the PS3/POSIX open semantics (e.g.
     * O_WRONLY without create+truncate, or O_CREAT without O_TRUNC), so build
     * real open() flags from the access mode (low 2 bits) + the modifiers, then
     * wrap the fd in a FILE* with fdopen() (which neither creates nor truncates
     * -- that was already decided by open()). */
    int acc = flags & 0x3;
    int oflags = (acc == CELL_FS_O_RDWR)   ? O_RDWR
               : (acc == CELL_FS_O_WRONLY) ? O_WRONLY : O_RDONLY;
    if (flags & CELL_FS_O_CREAT)  oflags |= O_CREAT;
    if (flags & CELL_FS_O_TRUNC)  oflags |= O_TRUNC;
    if (flags & CELL_FS_O_APPEND) oflags |= O_APPEND;

    int hfd = open(hpath, oflags | O_BINARY, 0666);
    if (hfd < 0) {
        fprintf(stderr, "[fs] open FAIL '%s' -> '%s'\n", gpath, hpath);
        ctx->gpr[3] = (uint64_t)(int64_t)CELL_FS_ENOENT; return;
    }
    const char* fmode = (acc == CELL_FS_O_RDWR)
                        ? ((flags & CELL_FS_O_APPEND) ? "ab+" : "rb+")
                        : (acc == CELL_FS_O_WRONLY)
                          ? ((flags & CELL_FS_O_APPEND) ? "ab" : "wb")
                          : "rb";
    FILE* f = fdopen(hfd, fmode);
    if (!f) {
        close(hfd);
        ctx->gpr[3] = (uint64_t)(int64_t)CELL_FS_EIO; return;
    }
    int fd = fd_alloc_file(f);
    if (fd < 0) { fclose(f); ctx->gpr[3] = (uint64_t)(int64_t)CELL_FS_EIO; return; }
    if (fd_ptr) vm_write32(fd_ptr, (uint32_t)fd);
    { const size_t L48 = strlen(gpath); int ps = 0;   /* winport v48-movies */
      if (L48 >= 6) { const char* x = gpath + L48 - 6; const char* w = ".psarc"; ps = 1;
          for (int i = 0; i < 6; i++) { char c = x[i]; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); if (c != w[i]) ps = 0; } }
      g_wp48_psarc[fd] = (char)ps; }
    /* PS3_SKIP_MOVIES: flag .m2v handles for immediate-EOF reads. */
    { static int sk = -1; if (sk < 0) sk = getenv("PS3_SKIP_MOVIES") ? 1 : 0;
      size_t L = strlen(gpath);
      g_fd_is_movie[fd] = (char)(sk && L >= 4 &&
          (strcmp(gpath + L - 4, ".m2v") == 0 || strcmp(gpath + L - 4, ".M2V") == 0)); }
    fprintf(stderr, "[fs] open '%s' -> fd %d%s (lr=0x%llX)\n", gpath, fd,
            g_fd_is_movie[fd] ? " [movie->EOF]" : "",
            (unsigned long long)ctx->lr);

    /* PS3_MOVIE_HLE: mirror .m2v playback on the host (ffmpeg -> D3D12
     * overlay) while the game streams the same file underneath. */
    { size_t L = strlen(gpath);
      if (L >= 4 && (strcmp(gpath + L - 4, ".m2v") == 0 || strcmp(gpath + L - 4, ".M2V") == 0)) {
          g_fd_is_m2v[fd] = 1;
          movie_hle_start(hpath);
      } }

    /* Task 2: FIRST_FS_OPEN -- caminho do open() real no host (once-guard
     * partilhado com o hit da cache movie_io acima). */
    ps3_checkpoint_fire(PS3_CP_FIRST_FS_OPEN, "cellFsOpen", "path=%s", gpath);
    /* Task 5 (bringup-instrumentacao-fase0): FIOS OP_BEGIN -- caminho do
     * open() real no host (NAO movie_io; esse caminho tem o seu proprio
     * OP_BEGIN em libs/video/movie_hle.c:movie_io_open, para nao contar a
     * mesma abertura duas vezes). Sem lock: este ficheiro nao toma nenhum. */
    PS3_TRACE(PS3_TS_FIOS, "cellFsOpen", "OP_BEGIN", "op=%d path=%s state=open", fd, gpath);
    ctx->gpr[3] = CELL_OK;
}

static void cellFsClose(ppu_context* ctx)
{
    uint32_t rawfd = (uint32_t)ctx->gpr[3];
    if (movie_io_is(rawfd)) {   /* before the FS_MAX range check -- see the tag note */
        movie_io_close(rawfd);
        ctx->gpr[3] = CELL_OK; return;
    }
    int fd = (int)rawfd;
    if (fd >= 0 && fd < FS_MAX && g_files[fd]) {
        fclose(g_files[fd]); g_files[fd] = nullptr; g_fd_is_movie[fd] = 0; g_wp48_psarc[fd] = 0;   /* winport v48-movies */
        if (g_fd_is_m2v[fd]) {
            movie_hle_stop();
            g_fd_is_m2v[fd] = 0;
        }
        /* Task 5: FIOS OP_END. Sem lock. */
        PS3_TRACE(PS3_TS_FIOS, "cellFsClose", "OP_END", "op=%d state=closed", fd);
    }
    ctx->gpr[3] = CELL_OK;
}

extern "C" unsigned long long ps3_ms(void);
static inline unsigned long long ps3_ms_fs(void){ return ps3_ms(); }

static void cellFsRead(ppu_context* ctx)
{
    uint32_t rawfd  = (uint32_t)ctx->gpr[3];
    int fd          = (int)rawfd;
    uint32_t buf    = (uint32_t)ctx->gpr[4];
    uint64_t nbytes = ctx->gpr[5];
    uint32_t nread_ptr = (uint32_t)ctx->gpr[6];
    /* movie_io keeps its own cursor per slot, so a plain cellFsRead is a
     * sequential read from it -- the same shape as fread() below. */
    if (movie_io_is(rawfd)) {
        if (ppu_vm_size && (uint64_t)buf + nbytes > ppu_vm_size) nbytes = ppu_vm_size - buf;
        unsigned n = movie_io_read_cur(rawfd, vm_base + buf, (unsigned)nbytes);
        if (nread_ptr) vm_write64(nread_ptr, n);
        ctx->gpr[3] = CELL_OK; return;
    }
    if (fd < 0 || fd >= FS_MAX || !g_files[fd]) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_FS_EIO; return; }
    /* PS3_TRACE_PSARC: log psarc read positions so reads can be correlated
     * with TOC members (which WADs does the game actually load?). */
    /* PS3_TRACE_PSARC=1 -> first 4000 reads; =N (>1) -> first N. */
    { static long tp = -1; if (tp < 0) { const char* e = getenv("PS3_TRACE_PSARC");
          tp = e ? (atol(e) > 1 ? atol(e) : 4000) : 0; }
      if (tp) { static long k = 0; if (k < tp) {
          long long pos = _ftelli64(g_files[fd]);
          fprintf(stderr, "[psarc-rd t=%llums] fd=%d off=0x%llX size=%llu\n", ps3_ms_fs(), fd,
                  (unsigned long long)pos, (unsigned long long)nbytes);
          /* Backtrace the MEMBER read (off != 0/0x20 = the one progression
           * event) to find the main-thread asset-request path. */
#ifdef _WIN32
          if (pos != 0 && pos != 0x20) {
              void* bt[24]; unsigned long h;
              unsigned long got = RtlCaptureStackBackTrace(0, 24, bt, &h);
              uintptr_t mb = (uintptr_t)GetModuleHandleW(0);
              fprintf(stderr, "[MEMRDBT] off=0x%llX frames:", (unsigned long long)pos);
              for (unsigned long i = 0; i < got; i++)
                  fprintf(stderr, " +0x%llX", (unsigned long long)((uintptr_t)bt[i] - mb));
              fprintf(stderr, "\n");
          }
#endif
          k++; } } }
    /* PS3_TRACE_PSARC_RANGE=lo:hi (file offsets, hex ok): for reads that start
     * inside [lo,hi) print the guest call chain -- lr, then the saved LRs of
     * the stack back-chain (PPC64 ABI: back-chain at [r1], LR at +0x10).
     * Who drives the read of one psarc member. Cap 60 lines. */
    { static int on = -1; static unsigned long long lo, hi;
      if (on < 0) { const char* e = getenv("PS3_TRACE_PSARC_RANGE"); on = 0;
          if (e && *e) { char* end = 0; lo = strtoull(e, &end, 0);
              if (end && *end == ':') { hi = strtoull(end + 1, 0, 0); on = hi > lo; } } }
      if (on) { static int k = 0; long long pos = _ftelli64(g_files[fd]);
          if (pos >= 0 && (unsigned long long)pos >= lo && (unsigned long long)pos < hi && k < 60) { k++;
              fprintf(stderr, "[PSARC-CHAIN] off=0x%llX n=%llu lr=0x%08X", (unsigned long long)pos,
                      (unsigned long long)nbytes, (uint32_t)ctx->lr);
              uint32_t sp = (uint32_t)ctx->gpr[1];
              for (int d = 0; d < 10 && sp && sp < 0xF0000000u; d++) {
                  uint32_t back = (uint32_t)vm_read64(sp);
                  if (!back || back <= sp) break;
                  fprintf(stderr, " <- 0x%08X", (uint32_t)vm_read64(back + 0x10));
                  sp = back; }
              fprintf(stderr, " tid=%u\n", (unsigned)ps3_ms_fs()); fflush(stderr); } } }
#ifdef _WIN32
    if (g_wp48_psarc[fd]) movie_wp48_psarc_read((unsigned long long)_ftelli64(g_files[fd]), (unsigned)nbytes);   /* winport v48-movies */
#endif
    if (g_fd_is_movie[fd]) { if (nread_ptr) vm_write64(nread_ptr, 0); ctx->gpr[3] = CELL_OK; return; } /* force EOF */
    if (ppu_vm_size && (uint64_t)buf + nbytes > ppu_vm_size) nbytes = ppu_vm_size - buf;
    /* Task 5 review fix (Minor #5): off= for the trace below, captured BEFORE
     * the fread moves the cursor -- same idiom as movie_io_read_cur()
     * (libs/video/movie_hle.c), gated so the extra _ftelli64() never costs
     * anything on the baseline (PS3_TRACE_FIOS unset). Without this, the
     * host-direct READ/PARTIAL line was the only FIOS site missing the
     * offset the brief lists as mandated -- its movie_io siblings both carry
     * it (real off= for pread, off=%lld pre-read position for read_cur). */
    long long off_before = -1;
    if (ps3_trace_on(PS3_TS_FIOS)) off_before = _ftelli64(g_files[fd]);
    size_t n = fread(vm_base + buf, 1, (size_t)nbytes, g_files[fd]);   /* raw bytes, no swap */
    /* PS3_WATCH_STORE: escrita host directa em vm_base, invisivel aos watches
     * de store. Gated, OFF por default. */
    ps3_watch_store_bulk((uint32_t)buf, (uint32_t)n, "cellFsRead fread");
    /* Task 5: FIOS READ/PARTIAL do caminho direto (NAO movie_io, ja coberto em
     * movie_hle.c). Sem lock. */
    if (n < (size_t)nbytes) {
        PS3_TRACE(PS3_TS_FIOS, "cellFsRead", "PARTIAL",
                  "op=%d off=%lld len=%llu buf=0x%08X state=short got=%llu",
                  fd, off_before, (unsigned long long)nbytes, buf, (unsigned long long)n);
    } else {
        PS3_TRACE(PS3_TS_FIOS, "cellFsRead", "READ",
                  "op=%d off=%lld len=%llu buf=0x%08X state=ok",
                  fd, off_before, (unsigned long long)nbytes, buf);
    }
    { static int tp = -1; if (tp < 0) tp = getenv("PS3_TRACE_PSARC") ? 1 : 0;
      if (tp) { static int k = 0; if (k < 400) { k++;
          fprintf(stderr, "[psarc-rd2] fd=%d n=%llu lr=0x%llX nrp=0x%X" "%s",
                  fd, (unsigned long long)n, (unsigned long long)ctx->lr,
                  nread_ptr, "\n"); } } }
    if (nread_ptr) vm_write64(nread_ptr, n);
    ctx->gpr[3] = CELL_OK;
}

static void cellFsWrite(ppu_context* ctx)
{
    int fd          = (int)(uint32_t)ctx->gpr[3];
    uint32_t buf    = (uint32_t)ctx->gpr[4];
    uint64_t nbytes = ctx->gpr[5];
    uint32_t nwr_ptr = (uint32_t)ctx->gpr[6];
    if (fd < 0 || fd >= FS_MAX || !g_files[fd]) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_FS_EIO; return; }
    size_t n = fwrite(vm_base + buf, 1, (size_t)nbytes, g_files[fd]);
    if (nwr_ptr) vm_write64(nwr_ptr, n);
    ctx->gpr[3] = CELL_OK;
}

static void cellFsLseek(ppu_context* ctx)
{
    uint32_t rawfd = (uint32_t)ctx->gpr[3];
    int fd        = (int)rawfd;
    int64_t off   = (int64_t)ctx->gpr[4];
    uint32_t wh   = (uint32_t)ctx->gpr[5];
    uint32_t pos_ptr = (uint32_t)ctx->gpr[6];
    if (movie_io_is(rawfd)) {
        /* movie_io_seek takes the CELL_FS_SEEK_* values directly (0/1/2). */
        long long p = movie_io_seek(rawfd, off, (int)wh);
        if (p < 0) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_FS_EIO; return; }
        if (pos_ptr) vm_write64(pos_ptr, (uint64_t)p);
        ctx->gpr[3] = CELL_OK; return;
    }
    if (fd < 0 || fd >= FS_MAX || !g_files[fd]) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_FS_EIO; return; }
    int worigin = (wh == CELL_FS_SEEK_END) ? SEEK_END : (wh == CELL_FS_SEEK_CUR) ? SEEK_CUR : SEEK_SET;
    { static int tp = -1; if (tp < 0) tp = getenv("PS3_TRACE_PSARC") ? 1 : 0;
      if (tp) { static int k = 0; if (k < 400) { k++;
          fprintf(stderr, "[psarc-seek] fd=%d off=0x%llX wh=%u lr=0x%llX" "%s",
                  fd, (unsigned long long)off, wh,
                  (unsigned long long)ctx->lr, "\n"); } } }
    _fseeki64(g_files[fd], (long long)off, worigin);
    long long p = _ftelli64(g_files[fd]);
    if (pos_ptr) vm_write64(pos_ptr, (uint64_t)p);
    ctx->gpr[3] = CELL_OK;
}

/* CellFsStat (big-endian, 0x38 bytes): mode(4) uid(4) gid(4) pad(4)
 * atime(8) mtime(8) ctime(8) size(8) blksize(8). */
/* CellFsStat is PACKED(4), sizeof == 0x34 (RPCS3 CHECK_SIZE): mode(0x00)
 * uid(0x04) gid(0x08) atime(0x0C) mtime(0x14) ctime(0x1C) size(0x24)
 * blksize(0x2C). The old 8-aligned layout put size at 0x28, so guests read
 * u64@0x24 = {ctime_lo, size_hi}: the 6.5 GB gow2.psarc came back as
 * "1 byte" ([PSARCSZ] file_size=0x1) and FIOS's dearchiver refused to read
 * the TOC; every file < 4 GB stat'd as size 0. */
static void write_stat(uint32_t sb, uint32_t mode, uint64_t size)
{
    vm_write32(sb + 0x00, mode);
    vm_write32(sb + 0x04, 0);            /* uid */
    vm_write32(sb + 0x08, 0);            /* gid */
    vm_write64(sb + 0x0C, 0);            /* atime */
    vm_write64(sb + 0x14, 0);            /* mtime */
    vm_write64(sb + 0x1C, 0);            /* ctime */
    vm_write64(sb + 0x24, size);         /* size */
    vm_write64(sb + 0x2C, 0x200);        /* blksize */
}

static void cellFsStat(ppu_context* ctx)
{
    char gpath[1024], hpath[1100];
    guest_strcpy(gpath, (uint32_t)ctx->gpr[3], sizeof gpath);
    uint32_t sb = (uint32_t)ctx->gpr[4];
    host_path_resolve(hpath, sizeof hpath, gpath);
    /* Same precedence as cellFsOpen: the cache answers first when it has the
     * member, so a stat and the open that follows it agree on the size. */
    if (movie_io_enabled()) {
        unsigned long long msz = movie_io_stat(gpath);
        if (msz) {
            if (sb) write_stat(sb, CELL_FS_S_IFREG | 0x1B6, msz);
            ctx->gpr[3] = CELL_OK; return;
        }
    }
#ifdef _WIN32
    struct _stat64 st;
    if (_stat64(hpath, &st) != 0) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_FS_ENOENT; return; }
#else
    struct stat st;
    if (stat(hpath, &st) != 0) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_FS_ENOENT; return; }
#endif
    uint32_t mode = (st.st_mode & S_IFDIR) ? (CELL_FS_S_IFDIR | 0x1FF)
                                           : (CELL_FS_S_IFREG | 0x1B6);
    if (sb) write_stat(sb, mode, (uint64_t)st.st_size);
    ctx->gpr[3] = CELL_OK;
}

static void cellFsFstat(ppu_context* ctx)
{
    uint32_t rawfd = (uint32_t)ctx->gpr[3];
    int fd      = (int)rawfd;
    uint32_t sb = (uint32_t)ctx->gpr[4];
    if (movie_io_is(rawfd)) {
        /* Size without disturbing the cursor: seek END, then restore. */
        long long cur = movie_io_seek(rawfd, 0, CELL_FS_SEEK_CUR);
        long long sz  = movie_io_seek(rawfd, 0, CELL_FS_SEEK_END);
        if (cur >= 0) movie_io_seek(rawfd, cur, CELL_FS_SEEK_SET);
        if (sz < 0) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_FS_EIO; return; }
        if (sb) write_stat(sb, CELL_FS_S_IFREG | 0x1B6, (uint64_t)sz);
        ctx->gpr[3] = CELL_OK; return;
    }
    if (fd < 0 || fd >= FS_MAX || !g_files[fd]) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_FS_EIO; return; }
    long long cur = _ftelli64(g_files[fd]);
    _fseeki64(g_files[fd], 0, SEEK_END);
    long long sz = _ftelli64(g_files[fd]);
    _fseeki64(g_files[fd], cur, SEEK_SET);
    if (sb) write_stat(sb, CELL_FS_S_IFREG | 0x1B6, (uint64_t)sz);
    ctx->gpr[3] = CELL_OK;
}

static void cellFsOpendir(ppu_context* ctx)
{
    char gpath[1024], hpath[1100];
    guest_strcpy(gpath, (uint32_t)ctx->gpr[3], sizeof gpath);
    uint32_t fd_ptr = (uint32_t)ctx->gpr[4];
    host_path_resolve(hpath, sizeof hpath, gpath);
    DIR* d = opendir(hpath);
    if (!d) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_FS_ENOENT; return; }
    int fd = fd_alloc_dir(d);
    if (fd < 0) { closedir(d); ctx->gpr[3] = (uint64_t)(int64_t)CELL_FS_EIO; return; }
    strncpy(g_dir_path[fd], hpath, sizeof g_dir_path[fd] - 1);
    if (fd_ptr) vm_write32(fd_ptr, (uint32_t)fd);
    ctx->gpr[3] = CELL_OK;
}

/* CellFsDirent: d_type(1) d_namlen(1) d_name[256]; total 0x102. */
static void cellFsReaddir(ppu_context* ctx)
{
    int fd          = (int)(uint32_t)ctx->gpr[3];
    uint32_t dirent = (uint32_t)ctx->gpr[4];
    uint32_t nread_ptr = (uint32_t)ctx->gpr[5];
    if (fd < 0 || fd >= FS_MAX || !g_dirs[fd]) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_FS_EIO; return; }
    struct dirent* e = readdir(g_dirs[fd]);
    if (!e) { if (nread_ptr) vm_write64(nread_ptr, 0); ctx->gpr[3] = CELL_OK; return; }
    char full[1300]; struct stat st;
    snprintf(full, sizeof full, "%s/%s", g_dir_path[fd], e->d_name);
    uint8_t type = (stat(full, &st) == 0 && (st.st_mode & S_IFDIR))
                   ? CELL_FS_TYPE_DIR : CELL_FS_TYPE_REG;
    size_t nl = strlen(e->d_name); if (nl > 255) nl = 255;
    vm_base[dirent + 0] = type;
    vm_base[dirent + 1] = (uint8_t)nl;
    for (size_t i = 0; i < nl; i++) vm_base[dirent + 2 + i] = (uint8_t)e->d_name[i];
    vm_base[dirent + 2 + nl] = 0;
    if (nread_ptr) vm_write64(nread_ptr, 0x102);
    ctx->gpr[3] = CELL_OK;
}

static void cellFsClosedir(ppu_context* ctx)
{
    int fd = (int)(uint32_t)ctx->gpr[3];
    if (fd >= 0 && fd < FS_MAX && g_dirs[fd]) { closedir(g_dirs[fd]); g_dirs[fd] = nullptr; }
    ctx->gpr[3] = CELL_OK;
}

static void cellFsMkdir(ppu_context* ctx)  { ctx->gpr[3] = CELL_OK; }
static void cellFsRmdir(ppu_context* ctx)  { ctx->gpr[3] = CELL_OK; }
static void cellFsUnlink(ppu_context* ctx) { ctx->gpr[3] = CELL_OK; }
static void cellFsFsync(ppu_context* ctx)
{
    int fd = (int)(uint32_t)ctx->gpr[3];
    if (fd >= 0 && fd < FS_MAX && g_files[fd]) fflush(g_files[fd]);
    ctx->gpr[3] = CELL_OK;
}

extern "C" void ppu_fs_register(void)
{
    ps3_hle_register_ctx(ps3_compute_nid("cellFsOpen"),     "cellFsOpen",     cellFsOpen);
    ps3_hle_register_ctx(ps3_compute_nid("cellFsClose"),    "cellFsClose",    cellFsClose);
    ps3_hle_register_ctx(ps3_compute_nid("cellFsRead"),     "cellFsRead",     cellFsRead);
    ps3_hle_register_ctx(ps3_compute_nid("cellFsWrite"),    "cellFsWrite",    cellFsWrite);
    ps3_hle_register_ctx(ps3_compute_nid("cellFsLseek"),    "cellFsLseek",    cellFsLseek);
    ps3_hle_register_ctx(ps3_compute_nid("cellFsStat"),     "cellFsStat",     cellFsStat);
    ps3_hle_register_ctx(ps3_compute_nid("cellFsFstat"),    "cellFsFstat",    cellFsFstat);
    ps3_hle_register_ctx(ps3_compute_nid("cellFsOpendir"),  "cellFsOpendir",  cellFsOpendir);
    ps3_hle_register_ctx(ps3_compute_nid("cellFsReaddir"),  "cellFsReaddir",  cellFsReaddir);
    ps3_hle_register_ctx(ps3_compute_nid("cellFsClosedir"), "cellFsClosedir", cellFsClosedir);
    ps3_hle_register_ctx(ps3_compute_nid("cellFsMkdir"),    "cellFsMkdir",    cellFsMkdir);
    ps3_hle_register_ctx(ps3_compute_nid("cellFsRmdir"),    "cellFsRmdir",    cellFsRmdir);
    ps3_hle_register_ctx(ps3_compute_nid("cellFsUnlink"),   "cellFsUnlink",   cellFsUnlink);
    ps3_hle_register_ctx(ps3_compute_nid("cellFsFsync"),    "cellFsFsync",    cellFsFsync);
}
