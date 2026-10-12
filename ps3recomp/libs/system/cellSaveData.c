/*
 * ps3recomp - cellSaveData HLE implementation
 *
 * Game save data management with callback-driven flow.
 * Save data is stored under: {root}/gamedata/dev_hdd0/home/00000001/savedata/{dirName}/
 */

#include "cellSaveData.h"
#include "ps3emu/guest_call.h"
#include "ps3emu/host_compat.h"
#include "../../runtime/ppu/ppu_memory.h"
#include "../../runtime/memory/vm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

/* glibc's <sys/stat.h> exposes st_atime/st_mtime/st_ctime as macros (st_atim.tv_sec
 * etc.) that collide with the identically named members of CellSaveData*Stat; undef
 * them so the struct fields resolve, and read host times via the explicit timespec
 * fields in the POSIX branches below. */
#ifdef st_atime
#  undef st_atime
#endif
#ifdef st_mtime
#  undef st_mtime
#endif
#ifdef st_ctime
#  undef st_ctime
#endif

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#  include <direct.h>
#  include <fcntl.h>   /* winport v46-savedata */
#  define HOST_MKDIR(p) _mkdir(p)
#  define HOST_STAT     _stat64
#  define HOST_STAT_T   struct __stat64
#else
#  include <unistd.h>
#  include <dirent.h>
#  include <sys/types.h>
#  define HOST_MKDIR(p) mkdir(p, 0755)
#  define HOST_STAT     stat
#  define HOST_STAT_T   struct stat
#endif

/* ---------------------------------------------------------------------------
 * Internal helpers
 * -----------------------------------------------------------------------*/

static char s_save_root[1024] = "./gamedata/dev_hdd0/home/00000001/savedata";

/*
 * Where saves live on the host. A path relative to the working directory lost
 * every save made from another directory, so the default is per user and
 * stable: PS3_SAVEDATA_ROOT if set, else
 *   macOS   ~/Library/Application Support/ps3recomp/dev_hdd0/home/00000001/savedata
 *   Windows %APPDATA%\ps3recomp\dev_hdd0\home\00000001\savedata
 *   other   ~/.local/share/ps3recomp/dev_hdd0/home/00000001/savedata
 * falling back to the old relative path when no home directory is known.
 */
static const char* savedata_root(void)
{
    static int resolved = 0;
    if (resolved) return s_save_root;
    resolved = 1;
    const char* env = getenv("PS3_SAVEDATA_ROOT");
    if (env && env[0]) {
        snprintf(s_save_root, sizeof(s_save_root), "%s", env);
        return s_save_root;
    }
#if defined(_WIN32)
    const char* base = getenv("APPDATA");
    if (base && base[0])
        snprintf(s_save_root, sizeof(s_save_root),
                 "%s\\ps3recomp\\dev_hdd0\\home\\00000001\\savedata", base);
#elif defined(__APPLE__)
    const char* base = getenv("HOME");
    if (base && base[0])
        snprintf(s_save_root, sizeof(s_save_root),
                 "%s/Library/Application Support/ps3recomp/dev_hdd0/home/00000001/savedata", base);
#else
    const char* base = getenv("HOME");
    if (base && base[0])
        snprintf(s_save_root, sizeof(s_save_root),
                 "%s/.local/share/ps3recomp/dev_hdd0/home/00000001/savedata", base);
#endif
    return s_save_root;
}

/* Ensure directory (and parents) exist */
static void ensure_dirs(const char* path)
{
    char tmp[1024];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            HOST_MKDIR(tmp);
            *p = saved;
        }
    }
    HOST_MKDIR(tmp);
}

static void build_save_path(char* buf, size_t buf_size, const char* dirName)
{
    snprintf(buf, buf_size, "%s/%s", savedata_root(), dirName);
#ifdef _WIN32
    for (char* p = buf; *p; p++) {
        if (*p == '/') *p = '\\';
    }
#endif
}

static int dir_exists(const char* path)
{
    HOST_STAT_T st;
    if (HOST_STAT(path, &st) != 0)
        return 0;
#ifdef _WIN32
    return (st.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(st.st_mode);
#endif
}

/* HLE pointer args arrive as RAW GUEST EAs: ps3_hle_call passes the guest GPRs
 * (r3..r10) straight into the C handler without translating pointers (see
 * runtime/ppu/ppu_hle.cpp). Every host-side use of dirName here — printf("%s"),
 * build_save_path()'s snprintf, marshal_statget_init()'s strnlen/memcpy — treats
 * it as HOST memory, so a raw EA (e.g. 0x004C4CE0, a rodata string in the guest
 * image) is fed to host strlen and faults. Translate the EA to a host pointer
 * once at each HLE entry; a null EA stays null so the existing !dirName / "%s"
 * guards keep working. (vm_to_host(0) would return vm_base, not NULL.) */
static inline const char* savedata_dir_host(const char* guest_ea)
{
    return guest_ea ? (const char*)vm_to_host((uint32_t)(uintptr_t)guest_ea) : NULL;
}

/* ---------------------------------------------------------------------------
 * Guest-callback marshalling
 *
 * The funcStat / funcFile callbacks the game registers are GUEST function
 * pointers (OPD addresses), not host-callable. We have to:
 *   1. Allocate guest memory for the StatGet/StatSet/CBResult structures
 *   2. Write the structs in big-endian format
 *   3. Dispatch the OPD via g_ps3_guest_caller with the guest pointers
 *   4. Read the structures back (game may have updated CBResult.result)
 *
 * Layout (sizes in PS3 ABI, all big-endian):
 *   CellSaveDataCBResult:    4 + 4 + 4 + 256        = 268 bytes
 *   CellSaveDataStatGet:     4 + 4 + 56 + 1408 + 24 + 4 ≈ 1500 bytes
 *   CellSaveDataStatSet:     4 + 4 + 4              = 12 bytes (pointers + reCreateMode)
 *
 * We use a fixed scratch region at 0x024E0000 (128 KB, just before the
 * cmdbuf region at 0x02500000). Only one savedata operation is in flight
 * at a time, so a bump allocator is sufficient.
 * -----------------------------------------------------------------------*/

#define SAVEDATA_SCRATCH_BASE  0x024E0000u
#define SAVEDATA_SCRATCH_SIZE  0x00020000u  /* 128 KB */

static uint32_t s_scratch_next = SAVEDATA_SCRATCH_BASE;

static void scratch_reset(void) { s_scratch_next = SAVEDATA_SCRATCH_BASE; }

static uint32_t scratch_alloc(uint32_t size)
{
    /* 16-byte align */
    size = (size + 0xF) & ~0xFu;
    if (s_scratch_next + size > SAVEDATA_SCRATCH_BASE + SAVEDATA_SCRATCH_SIZE)
        return 0;
    uint32_t addr = s_scratch_next;
    s_scratch_next += size;
    /* Zero-init */
    memset(vm_base + addr, 0, size);
    return addr;
}

/* PS3 CellSaveDataCBResult is POINTER-based (RPCS3 / official SDK), not the
 * host-side char invalidMsg[256] layout in our C header:
 *   s32  result            +0
 *   u32  progressBarInc    +4
 *   s32  errNeedSizeKB     +8
 *   ptr  invalidMsg        +0xC   (guest EA of char buffer)
 *   ptr  userdata          +0x10  (guest EA from AutoLoad* userdata arg)
 * GoW2 funcStat (func_00147BB8) does lwz r9,0x10(r3) and treats it as a
 * struct base (writes +0x28, reads +0x4). Leaving userdata=0 made it poke
 * low memory (0xC / 0x28) → post-AutoLoad OOB walk. */
#define SAVEDATA_CBRESULT_SIZE   32u
#define SAVEDATA_INVALIDMSG_SIZE 256u

/* Big-endian struct field writers. Offsets are in PS3 ABI layout. */
static void marshal_cbresult_init(uint32_t addr, s32 result,
                                  uint32_t invalid_msg_ea, uint32_t userdata_ea)
{
    vm_write32(addr + 0,  (uint32_t)result);     /* result */
    vm_write32(addr + 4,  0);                    /* progressBarInc */
    vm_write32(addr + 8,  0);                    /* errNeedSizeKB */
    vm_write32(addr + 0xC, invalid_msg_ea);      /* invalidMsg ptr */
    vm_write32(addr + 0x10, userdata_ea);        /* userdata ptr */
}

/* PS3 ABI layout of CellSaveDataStatGet (all big-endian on PPC, 32-bit pointers):
 *   s32 hddFreeSizeKB                     +0
 *   u32 isNewData                         +4
 *   CellSaveDataDirStat dir               +8   (s64*3 + char[32] = 56 bytes)
 *   CellSaveDataSystemFileParam getParam  +64  (128+128+1024+4+4+8+256 = 1552 bytes)
 *   u32 bind                              +1616
 *   s32 sizeKB                            +1620
 *   s32 sysSizeKB                         +1624
 *   u32 fileNum                           +1628
 *   u32 fileListNum                       +1632
 *   ptr fileList                          +1636 (32-bit guest ptr)
 *   total                                 1640 bytes
 */
#define SAVEDATA_STATGET_SIZE   1640u
#define SAVEDATA_STATSET_SIZE   16u    /* setParam ptr + reCreateMode + indicator ptr */

static int savedata_trace_on(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char* e = getenv("PS3_TRACE_SAVEDATA");
        cached = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return cached;
}

static void marshal_statget_init(uint32_t addr, int is_new, const char* dirName,
                                  s32 sizeKB, u32 fileNum)
{
    vm_write32(addr + 0,    1024 * 1024);    /* hddFreeSizeKB = 1 GB */
    vm_write32(addr + 4,    is_new ? 1 : 0); /* isNewData */
    vm_write64(addr + 8,    0);              /* dir.st_atime */
    vm_write64(addr + 16,   0);              /* dir.st_mtime */
    vm_write64(addr + 24,   0);              /* dir.st_ctime */
    if (dirName) {
        size_t len = strnlen(dirName, CELL_SAVEDATA_DIRNAME_SIZE - 1);
        memcpy(vm_base + addr + 32, dirName, len);
        ((char*)vm_base)[addr + 32 + len] = 0;
    }
    /* getParam (+64..+1615) left zero — no PARAM.SFO data */
    vm_write32(addr + 1616, 0);              /* bind */
    vm_write32(addr + 1620, (uint32_t)sizeKB); /* sizeKB */
    vm_write32(addr + 1624, 0);              /* sysSizeKB */
    vm_write32(addr + 1628, fileNum);        /* fileNum */
    vm_write32(addr + 1632, 0);              /* fileListNum */
    vm_write32(addr + 1636, 0);              /* fileList ptr (NULL — no files) */
}

/* First-run AutoLoad: game funcStat often writes ERR_NODATA (-4) when
 * isNewData=1 (no save to load). Mapping that to CELL_SAVEDATA_ERROR_NODATA
 * (0x8002B40B) makes thr_auto_load store status=4 and was observed with a
 * post-AutoLoad OOB spin. For is_new, treat NODATA as successful "no save /
 * new game" completion (CELL_OK) so the title FSM can advance. Existing-save
 * NODATA still surfaces as ERROR_NODATA. */
static int savedata_nodata_ok_legacy(void)
{
    /* E93 (2026-09-02): the console/RPCS3 return CELL_SAVEDATA_ERROR_CBRESULT
     * for ANY negative funcStat result (GoW2 first run: ERR_NODATA -> 0x8002b401,
     * measured on the oracle). The old NODATA+isNew -> CELL_OK mapping made
     * thr_auto_load believe a save was loaded (+0xc=0 -> FUN_000bae7c applies
     * nothing) and the title FSM took the wrong branch (+0x7c0=1). Legacy
     * mapping only for A/B: PS3_SAVEDATA_NODATA_OK=1. */
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_SAVEDATA_NODATA_OK"); on = (e && *e && *e != '0') ? 1 : 0; }
    return on;
}

static s32 autoload_map_cb_result(s32 cb, int is_new)
{
    if (cb >= 0)
        return CELL_OK;
    if (cb == CELL_SAVEDATA_CBRESULT_ERR_NODATA && savedata_nodata_ok_legacy()) {
        if (is_new) {
            printf("[cellSaveData] AutoLoad first-run: funcStat ERR_NODATA + isNewData=1 → CELL_OK (new game)\n");
            if (savedata_trace_on()) {
                fprintf(stderr, "[SAVEDATA-PROBE] map ERR_NODATA+isNew → API CELL_OK\n");
                fflush(stderr);
            }
            return CELL_OK;
        }
        return CELL_SAVEDATA_ERROR_NODATA;
    }
    return CELL_SAVEDATA_ERROR_CBRESULT;
}

/* Enumerate save directories matching a prefix. Returns count, fills dirList up to max. */
static u32 enumerate_save_dirs(const char* prefix, CellSaveDataDirList* dirList, u32 max)
{
    u32 count = 0;

    ensure_dirs(savedata_root());

#ifdef _WIN32
    {
        char search[1024];
        snprintf(search, sizeof(search), "%s\\*", s_save_root);
        for (char* p = search; *p; p++) {
            if (*p == '/') *p = '\\';
        }

        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(search, &fd);
        if (hFind == INVALID_HANDLE_VALUE)
            return 0;

        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                continue;
            if (fd.cFileName[0] == '.')
                continue;
            if (prefix && prefix[0] && strncmp(fd.cFileName, prefix, strlen(prefix)) != 0)
                continue;
            if (count < max && dirList) {
                memset(&dirList[count], 0, sizeof(CellSaveDataDirList));
                strncpy(dirList[count].dirName, fd.cFileName,
                        CELL_SAVEDATA_DIRNAME_SIZE - 1);
            }
            count++;
        } while (FindNextFileA(hFind, &fd));

        FindClose(hFind);
    }
#else
    {
        DIR* dp = opendir(s_save_root);
        if (!dp)
            return 0;

        struct dirent* de;
        while ((de = readdir(dp)) != NULL) {
            if (de->d_name[0] == '.')
                continue;
            /* Check it's a directory */
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", s_save_root, de->d_name);
            if (!dir_exists(full))
                continue;
            if (prefix && prefix[0] && strncmp(de->d_name, prefix, strlen(prefix)) != 0)
                continue;
            if (count < max && dirList) {
                memset(&dirList[count], 0, sizeof(CellSaveDataDirList));
                strncpy(dirList[count].dirName, de->d_name,
                        CELL_SAVEDATA_DIRNAME_SIZE - 1);
            }
            count++;
        }
        closedir(dp);
    }
#endif

    return count;
}

/* Enumerate files in a save directory. Returns count, fills fileList up to max. */
static u32 enumerate_save_files(const char* save_path,
                                 CellSaveDataFileStat* fileList, u32 max)
{
    u32 count = 0;

#ifdef _WIN32
    {
        char search[1024];
        snprintf(search, sizeof(search), "%s\\*", save_path);
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(search, &fd);
        if (hFind == INVALID_HANDLE_VALUE)
            return 0;

        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            if (count < max && fileList) {
                memset(&fileList[count], 0, sizeof(CellSaveDataFileStat));
                fileList[count].fileType = CELL_SAVEDATA_FILETYPE_NORMALFILE;
                strncpy(fileList[count].fileName, fd.cFileName,
                        CELL_SAVEDATA_FILENAME_SIZE - 1);
                ULARGE_INTEGER sz;
                sz.HighPart = fd.nFileSizeHigh;
                sz.LowPart  = fd.nFileSizeLow;
                fileList[count].st_size = (u64)sz.QuadPart;
            }
            count++;
        } while (FindNextFileA(hFind, &fd));

        FindClose(hFind);
    }
#else
    {
        DIR* dp = opendir(save_path);
        if (!dp)
            return 0;

        struct dirent* de;
        while ((de = readdir(dp)) != NULL) {
            if (de->d_name[0] == '.')
                continue;
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", save_path, de->d_name);
            HOST_STAT_T st;
            if (HOST_STAT(full, &st) != 0)
                continue;
#ifdef _WIN32
            if (st.st_mode & _S_IFDIR)
                continue;
#else
            if (S_ISDIR(st.st_mode))
                continue;
#endif
            if (count < max && fileList) {
                memset(&fileList[count], 0, sizeof(CellSaveDataFileStat));
                fileList[count].fileType = CELL_SAVEDATA_FILETYPE_NORMALFILE;
                strncpy(fileList[count].fileName, de->d_name,
                        CELL_SAVEDATA_FILENAME_SIZE - 1);
                fileList[count].st_size = (u64)st.st_size;
                fileList[count].st_atime = (s64)PS3_STAT_ATIME_SEC(st);
                fileList[count].st_mtime = (s64)PS3_STAT_MTIME_SEC(st);
                fileList[count].st_ctime = (s64)PS3_STAT_CTIME_SEC(st);
            }
            count++;
        }
        closedir(dp);
    }
#endif

    return count;
}

/* Execute file callback: read/write/delete files in the save directory */
static s32 process_file_op(const char* save_path, CellSaveDataFileSet* set)
{
    if (!set || !set->fileName)
        return CELL_OK;

    char file_path[1024];
    snprintf(file_path, sizeof(file_path), "%s/%s", save_path, set->fileName);
#ifdef _WIN32
    for (char* p = file_path; *p; p++) {
        if (*p == '/') *p = '\\';
    }
#endif

    switch (set->fileOperation) {
    case CELL_SAVEDATA_FILEOP_READ: {
        FILE* fp = fopen(file_path, "rb");
        if (!fp) {
            printf("[cellSaveData] file read: cannot open '%s'\n", file_path);
            return 0; /* excSize = 0 */
        }
        if (set->fileOffset > 0) {
#ifdef _MSC_VER
            _fseeki64(fp, (long long)set->fileOffset, SEEK_SET);
#else
            fseeko(fp, (off_t)set->fileOffset, SEEK_SET);
#endif
        }
        size_t read_size = (size_t)set->fileSize;
        if (read_size > set->fileBufSize) read_size = set->fileBufSize;
        size_t got = fread(set->fileBuf, 1, read_size, fp);
        fclose(fp);
        return (s32)got;
    }

    case CELL_SAVEDATA_FILEOP_WRITE:
    case CELL_SAVEDATA_FILEOP_WRITE_NOTRUNC: {
        ensure_dirs(save_path);
        const char* mode;
        if (set->fileOperation == CELL_SAVEDATA_FILEOP_WRITE && set->fileOffset == 0) {
            mode = "wb";
        } else {
            mode = "r+b";
        }
        FILE* fp = fopen(file_path, mode);
        if (!fp) {
            fp = fopen(file_path, "wb");
        }
        if (!fp) {
            printf("[cellSaveData] file write: cannot open '%s'\n", file_path);
            return 0;
        }
        if (set->fileOffset > 0) {
#ifdef _MSC_VER
            _fseeki64(fp, (long long)set->fileOffset, SEEK_SET);
#else
            fseeko(fp, (off_t)set->fileOffset, SEEK_SET);
#endif
        }
        size_t write_size = (size_t)set->fileSize;
        if (write_size > set->fileBufSize) write_size = set->fileBufSize;
        size_t wrote = fwrite(set->fileBuf, 1, write_size, fp);
        fclose(fp);
        return (s32)wrote;
    }

    case CELL_SAVEDATA_FILEOP_DELETE:
        remove(file_path);
        return 0;

    default:
        return 0;
    }
}

/* Write a simplified PARAM.SFO with the save's title/subtitle/detail */
static void write_param_sfo(const char* save_path, const CellSaveDataSystemFileParam* param)
{
    if (!param) return;

    char sfo_path[1024];
    snprintf(sfo_path, sizeof(sfo_path), "%s/PARAM.SFO", save_path);
#ifdef _WIN32
    for (char* p = sfo_path; *p; p++) {
        if (*p == '/') *p = '\\';
    }
#endif

    FILE* fp = fopen(sfo_path, "wb");
    if (!fp) return;

    /* Write a simplified text-based PARAM.SFO for easy debugging.
       Games don't read this directly - our stat callback fills it from here. */
    fprintf(fp, "TITLE=%s\n", param->title);
    fprintf(fp, "SUB_TITLE=%s\n", param->subTitle);
    fprintf(fp, "DETAIL=%s\n", param->detail);
    fprintf(fp, "ATTRIBUTE=%u\n", param->attribute);
    fprintf(fp, "LIST_PARAM=%s\n", param->listParam);
    fclose(fp);
}

/* Read simplified PARAM.SFO */
static void read_param_sfo(const char* save_path, CellSaveDataSystemFileParam* param)
{
    if (!param) return;
    memset(param, 0, sizeof(CellSaveDataSystemFileParam));

    char sfo_path[1024];
    snprintf(sfo_path, sizeof(sfo_path), "%s/PARAM.SFO", save_path);
#ifdef _WIN32
    for (char* p = sfo_path; *p; p++) {
        if (*p == '/') *p = '\\';
    }
#endif

    FILE* fp = fopen(sfo_path, "rb");
    if (!fp) return;

    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (strncmp(line, "TITLE=", 6) == 0)
            strncpy(param->title, line + 6, CELL_SAVEDATA_SYSP_TITLE_SIZE - 1);
        else if (strncmp(line, "SUB_TITLE=", 10) == 0)
            strncpy(param->subTitle, line + 10, CELL_SAVEDATA_SYSP_SUBTITLE_SIZE - 1);
        else if (strncmp(line, "DETAIL=", 7) == 0)
            strncpy(param->detail, line + 7, CELL_SAVEDATA_SYSP_DETAIL_SIZE - 1);
        else if (strncmp(line, "ATTRIBUTE=", 10) == 0)
            param->attribute = (u32)atoi(line + 10);
        else if (strncmp(line, "LIST_PARAM=", 11) == 0)
            strncpy(param->listParam, line + 11, CELL_SAVEDATA_SYSP_LPARAM_SIZE - 1);
    }
    fclose(fp);
}

/* Core save/load implementation shared by List/Fixed/Auto variants */
static s32 savedata_execute(const char* dirName, int is_save,
                             CellSaveDataSetBuf* setBuf,
                             CellSaveDataStatCallback funcStat,
                             CellSaveDataFileCallback funcFile,
                             void* userdata)
{
    if (!dirName || !funcStat)
        return CELL_SAVEDATA_ERROR_PARAM;

    char save_path[1024];
    build_save_path(save_path, sizeof(save_path), dirName);

    int is_new = !dir_exists(save_path);

    printf("[cellSaveData] %s dir='%s' (new=%d)\n",
           is_save ? "SAVE" : "LOAD", dirName, is_new);

    /* If loading and directory doesn't exist, it's not an error -
       the stat callback will see isNewData=1 and can handle it */

    /* Prepare stat get */
    u32 file_list_max = setBuf ? setBuf->fileListMax : 64;
    CellSaveDataFileStat* fileList = NULL;
    u32 fileNum = 0;

    if (file_list_max > 0) {
        fileList = (CellSaveDataFileStat*)calloc(file_list_max, sizeof(CellSaveDataFileStat));
        if (!is_new) {
            fileNum = enumerate_save_files(save_path, fileList, file_list_max);
        }
    }

    CellSaveDataStatGet statGet;
    memset(&statGet, 0, sizeof(statGet));
    statGet.hddFreeSizeKB = 1024 * 1024; /* report 1GB free */
    statGet.isNewData = is_new ? 1 : 0;
    strncpy(statGet.dir.dirName, dirName, CELL_SAVEDATA_DIRNAME_SIZE - 1);

    if (!is_new) {
        HOST_STAT_T hst;
        if (HOST_STAT(save_path, &hst) == 0) {
            statGet.dir.st_atime = (s64)PS3_STAT_ATIME_SEC(hst);
            statGet.dir.st_mtime = (s64)PS3_STAT_MTIME_SEC(hst);
            statGet.dir.st_ctime = (s64)PS3_STAT_CTIME_SEC(hst);
        }
        read_param_sfo(save_path, &statGet.getParam);
    }

    statGet.fileNum = fileNum;
    statGet.fileListNum = fileNum < file_list_max ? fileNum : file_list_max;
    statGet.fileList = fileList;
    statGet.bind = 0;
    statGet.sizeKB = 0;
    statGet.sysSizeKB = 0;

    /* Call stat callback */
    CellSaveDataCBResult cbResult;
    memset(&cbResult, 0, sizeof(cbResult));
    cbResult.result = CELL_SAVEDATA_CBRESULT_OK_NEXT;

    CellSaveDataStatSet statSet;
    memset(&statSet, 0, sizeof(statSet));

    funcStat(&cbResult, &statGet, &statSet);

    if (cbResult.result < 0) {
        printf("[cellSaveData] stat callback returned error %d\n", cbResult.result);
        free(fileList);
        if (cbResult.result == CELL_SAVEDATA_CBRESULT_ERR_NODATA)
            return CELL_SAVEDATA_ERROR_NODATA;
        return CELL_SAVEDATA_ERROR_CBRESULT;
    }

    /* Write PARAM.SFO if stat set provided params and we're saving */
    if (is_save && statSet.setParam) {
        ensure_dirs(save_path);
        write_param_sfo(save_path, statSet.setParam);
    }

    /* Call file callback repeatedly */
    if (funcFile && cbResult.result == CELL_SAVEDATA_CBRESULT_OK_NEXT) {
        while (1) {
            CellSaveDataFileGet fileGet;
            memset(&fileGet, 0, sizeof(fileGet));

            CellSaveDataFileSet fileSet;
            memset(&fileSet, 0, sizeof(fileSet));

            cbResult.result = CELL_SAVEDATA_CBRESULT_OK_NEXT;

            funcFile(&cbResult, &fileGet, &fileSet);

            if (cbResult.result == CELL_SAVEDATA_CBRESULT_OK_LAST ||
                cbResult.result < 0) {
                /* Process last operation if set */
                if (fileSet.fileName && fileSet.fileBuf) {
                    s32 exc = process_file_op(save_path, &fileSet);
                    (void)exc;
                }
                break;
            }

            if (fileSet.fileName && fileSet.fileBuf) {
                s32 exc = process_file_op(save_path, &fileSet);
                (void)exc;
            } else {
                /* No file operation requested, done */
                break;
            }
        }
    }

    free(fileList);

    if (cbResult.result < 0) {
        if (cbResult.result == CELL_SAVEDATA_CBRESULT_ERR_NODATA)
            return CELL_SAVEDATA_ERROR_NODATA;
        return CELL_SAVEDATA_ERROR_CBRESULT;
    }

    printf("[cellSaveData] %s complete for '%s'\n",
           is_save ? "SAVE" : "LOAD", dirName);
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Auto save/load driven by the game's GUEST callbacks
 *
 * cellSaveDataAutoSave2/AutoLoad2 receive every pointer as a raw guest EA
 * (setBuf, funcStat/funcFile OPDs, userdata). The flow follows the system
 * utility (and RPCS3's savedata_op):
 *   1. funcStat(cbResult, statGet, statSet): statGet describes the directory
 *      (isNewData, PARAM title/detail, file list in setBuf->buf, sizes).
 *      result < 0 -> error; OK_LAST/OK_LAST_NOCONFIRM -> done, no file work.
 *      statSet.setParam (save) is written as the directory's PARAM;
 *      statSet.reCreateMode == RECREATE_YES* empties the directory first.
 *   2. funcFile(cbResult, fileGet, fileSet) until OK_LAST: each call names one
 *      READ / WRITE / WRITE_NOTRUNC / DELETE on a file of the directory, done
 *      with the guest buffer; fileGet.excSize reports the bytes moved.
 * No dialog is ever shown: the memory card is automatic.
 *
 * Guest ABI (big-endian, 32-bit pointers):
 *   SetBuf   +0 dirListMax +4 fileListMax +8..+31 reserved +32 bufSize +36 buf
 *   StatGet  see marshal_statget_init; getParam title +64 subTitle +192
 *            detail +320 attribute +1344 parental +1348 listParam +1352
 *   StatSet  +0 setParam* +4 reCreateMode +8 indicator*
 *   FileStat +0 fileType +8 size(u64) +16 atime +24 mtime +32 ctime
 *            +40 fileName[13]                                   (56 bytes)
 *   FileGet  +0 excSize                                          (68 bytes)
 *   FileSet  +0 fileOperation +4 reserved* +8 fileType +12 secureFileId[16]
 *            +28 fileName* +32 fileOffset +36 fileSize +40 fileBufSize
 *            +44 fileBuf*                                        (48 bytes)
 * -----------------------------------------------------------------------*/

#define SD_PARAM_TITLE      64u
#define SD_PARAM_SUBTITLE   192u
#define SD_PARAM_DETAIL     320u
#define SD_PARAM_ATTRIBUTE  1344u
#define SD_PARAM_LISTPARAM  1352u
#define SD_FILESTAT_SIZE    56u
#define SD_FILEGET_SIZE     68u
#define SD_FILESET_SIZE     48u
#define SD_RECREATE_MASK    0xFFFFu
#define SD_RECREATE_YES     2u
#define SD_RECREATE_YES_RESET_OWNER 3u

static void guest_strcpy(uint32_t ea, const char* src, size_t cap)
{
    size_t n = src ? strnlen(src, cap - 1) : 0;
    if (n) memcpy(vm_base + ea, src, n);
    vm_base[ea + n] = 0;
}

static void guest_strget(char* dst, size_t cap, uint32_t ea)
{
    size_t i = 0;
    if (ea)
        for (; i + 1 < cap && vm_base[ea + i]; i++) dst[i] = (char)vm_base[ea + i];
    dst[i] = 0;
}

/* Host name of a file the game asks for. Content files have fixed names;
 * data files must be a plain 1..12-char name (no path, no leading dot). */
static int savedata_file_name(uint32_t type, uint32_t name_ea, char* out, size_t cap)
{
    switch (type) {
    case CELL_SAVEDATA_FILETYPE_CONTENT_ICON0: snprintf(out, cap, "ICON0.PNG"); return 1;
    case CELL_SAVEDATA_FILETYPE_CONTENT_ICON1: snprintf(out, cap, "ICON1.PAM"); return 1;
    case CELL_SAVEDATA_FILETYPE_CONTENT_PIC1:  snprintf(out, cap, "PIC1.PNG");  return 1;
    case CELL_SAVEDATA_FILETYPE_CONTENT_SND0:  snprintf(out, cap, "SND0.AT3");  return 1;
    default: break;
    }
    guest_strget(out, cap < CELL_SAVEDATA_FILENAME_SIZE ? cap : CELL_SAVEDATA_FILENAME_SIZE, name_ea);
    if (!out[0] || out[0] == '.') return 0;
    for (const char* c = out; *c; c++)
        if (*c == '/' || *c == '\\' || *c == ':') return 0;
    return 1;
}

static int savedata_is_system_file(const char* name)
{
    return strcmp(name, "PARAM.SFO") == 0 || strcmp(name, "PARAM.PFD") == 0;
}

/* ---- winport v46-savedata (engine_fixes_v46_savedata.py) ----
 * Names of the regular files of a save directory, sorted (RPCS3 orders old saves alphabetically).
 * The Windows build used to list nothing here, so GoW2's save funcStat (func_00147F44) found
 * none of its files in an existing save and returned ERR_BROKEN (-3) -> "error saving". */
typedef struct { char name[CELL_SAVEDATA_FILENAME_SIZE]; } sd46_name_t;
#define SD46_MAX_FILES 256u
static int sd46_cmp(const void* a, const void* b)
{
    return strcmp(((const sd46_name_t*)a)->name, ((const sd46_name_t*)b)->name);
}
/* with_system: also return PARAM.SFO / PARAM.PFD (for removal). */
static uint32_t sd46_collect(const char* save_path, sd46_name_t* out, uint32_t max, int with_system)
{
    uint32_t n = 0;
#ifdef _WIN32
    char pat[1100];
    snprintf(pat, sizeof pat, "%s\\*", save_path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const char* nm = fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (nm[0] == '.' || (!with_system && savedata_is_system_file(nm))) continue;
            if (strlen(nm) >= CELL_SAVEDATA_FILENAME_SIZE) continue;
            if (n < max) snprintf(out[n++].name, CELL_SAVEDATA_FILENAME_SIZE, "%s", nm);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR* dp = opendir(save_path);
    struct dirent* de;
    while (dp && (de = readdir(dp)) != NULL) {
        const char* nm = de->d_name;
        if (nm[0] == '.' || (!with_system && savedata_is_system_file(nm))) continue;
        if (strlen(nm) >= CELL_SAVEDATA_FILENAME_SIZE) continue;
        char full[1100];
        HOST_STAT_T st;
        snprintf(full, sizeof full, "%s/%s", save_path, nm);
        if (HOST_STAT(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (n < max) snprintf(out[n++].name, CELL_SAVEDATA_FILENAME_SIZE, "%s", nm);
    }
    if (dp) closedir(dp);
#endif
    qsort(out, n, sizeof *out, sd46_cmp);
    return n;
}

/* Fills statGet's size, file count and (when the game gave a buffer) the
 * FileStat list for the files of an existing directory (RPCS3 savedata_op parity). */
static void savedata_list_files(const char* save_path, uint32_t get_ea,
                                uint32_t list_ea, uint32_t list_max)
{
    static sd46_name_t names[SD46_MAX_FILES];
    uint64_t total = 0;
    uint32_t n = 0, listed = 0;
    const uint32_t cnt = sd46_collect(save_path, names, SD46_MAX_FILES, 0);
    for (uint32_t i = 0; i < cnt; i++) {
        const char* nm = names[i].name;
        char full[1100];
        HOST_STAT_T st;
#ifdef _WIN32
        snprintf(full, sizeof full, "%s\\%s", save_path, nm);
#else
        snprintf(full, sizeof full, "%s/%s", save_path, nm);
#endif
        if (HOST_STAT(full, &st) != 0) continue;
        total += ((uint64_t)st.st_size + 1023u) & ~(uint64_t)1023u;   /* firmware rounds up */
        n++;
        if (list_ea && listed < list_max) {
            const uint32_t e = list_ea + listed * SD_FILESTAT_SIZE;
            memset(vm_base + e, 0, SD_FILESTAT_SIZE);
            uint32_t type = CELL_SAVEDATA_FILETYPE_NORMALFILE;
            if (!strcmp(nm, "ICON0.PNG")) type = CELL_SAVEDATA_FILETYPE_CONTENT_ICON0;
            else if (!strcmp(nm, "ICON1.PAM")) type = CELL_SAVEDATA_FILETYPE_CONTENT_ICON1;
            else if (!strcmp(nm, "PIC1.PNG")) type = CELL_SAVEDATA_FILETYPE_CONTENT_PIC1;
            else if (!strcmp(nm, "SND0.AT3")) type = CELL_SAVEDATA_FILETYPE_CONTENT_SND0;
            vm_write32(e + 0, type);
            vm_write64(e + 8, (uint64_t)st.st_size);
            vm_write64(e + 16, (uint64_t)PS3_STAT_ATIME_SEC(st));
            vm_write64(e + 24, (uint64_t)PS3_STAT_MTIME_SEC(st));
            vm_write64(e + 32, (uint64_t)PS3_STAT_CTIME_SEC(st));
            guest_strcpy(e + 40, nm, CELL_SAVEDATA_FILENAME_SIZE);
            listed++;
        }
    }
    vm_write32(get_ea + 1620, (uint32_t)(total / 1024u) + 35u);      /* sizeKB (RPCS3: files + sysSizeKB) */
    vm_write32(get_ea + 1624, 35u);                                  /* sysSizeKB */
    vm_write32(get_ea + 1628, n);                                    /* fileNum */
    vm_write32(get_ea + 1632, listed);                               /* fileListNum */
    vm_write32(get_ea + 1636, list_ea);                              /* fileList = setBuf->buf (RPCS3) */
    if (savedata_trace_on() || getenv("PS3_SAVEDATA_LOG")) {
        printf("[cellSaveData] v46 statGet: fileNum=%u fileListNum=%u sizeKB=%u\n",
               n, listed, (unsigned)(total / 1024u) + 35u);
        for (uint32_t i = 0; i < listed; i++)
            printf("[cellSaveData]   file %-12s size=%llu\n",
                   (const char*)(vm_base + list_ea + i * SD_FILESTAT_SIZE + 40),
                   (unsigned long long)vm_read64(list_ea + i * SD_FILESTAT_SIZE + 8));
    }
}

static void savedata_remove_files(const char* save_path)
{
    static sd46_name_t names[SD46_MAX_FILES];
    const uint32_t cnt = sd46_collect(save_path, names, SD46_MAX_FILES, 1);
    for (uint32_t i = 0; i < cnt; i++) {
        char full[1100];
#ifdef _WIN32
        snprintf(full, sizeof full, "%s\\%s", save_path, names[i].name);
#else
        snprintf(full, sizeof full, "%s/%s", save_path, names[i].name);
#endif
        remove(full);
    }
}

/* One file operation of funcFile. Returns the byte count for excSize, or -1
 * when the request is invalid (bad name / buffer). */
static s32 savedata_file_op(const char* save_path, uint32_t set_ea)
{
    const uint32_t op      = vm_read32(set_ea + 0);
    const uint32_t type    = vm_read32(set_ea + 8);
    const uint32_t name_ea = vm_read32(set_ea + 28);
    const uint32_t offset  = vm_read32(set_ea + 32);
    const uint32_t size    = vm_read32(set_ea + 36);
    const uint32_t bufsize = vm_read32(set_ea + 40);
    const uint32_t buf     = vm_read32(set_ea + 44);
    char name[32], path[1100];
    if (!savedata_file_name(type, name_ea, name, sizeof name)) return -1;
    snprintf(path, sizeof path, "%s/%s", save_path, name);

    if (op == CELL_SAVEDATA_FILEOP_DELETE) {
        remove(path);
        return 0;
    }
    if (!buf || !ppu_guest_range_committed(buf, bufsize ? bufsize : 1u)) return -1;
    const uint32_t n = size < bufsize ? size : bufsize;

    if (op == CELL_SAVEDATA_FILEOP_READ) {
        FILE* fp = fopen(path, "rb");
        if (!fp) return 0;
        size_t got = 0;
        if (fseek(fp, (long)offset, SEEK_SET) == 0)
            got = fread(vm_base + buf, 1, n, fp);
        fclose(fp);
        return (s32)got;
    }
    if (op == CELL_SAVEDATA_FILEOP_WRITE || op == CELL_SAVEDATA_FILEOP_WRITE_NOTRUNC) {
        ensure_dirs(save_path);
        FILE* fp = NULL;
        if (op == CELL_SAVEDATA_FILEOP_WRITE_NOTRUNC || offset != 0)
            fp = fopen(path, "r+b");
        if (!fp) fp = fopen(path, op == CELL_SAVEDATA_FILEOP_WRITE ? "wb" : "w+b");
        if (!fp) return -1;
        size_t put = 0;
        if (fseek(fp, (long)offset, SEEK_SET) == 0)
            put = fwrite(vm_base + buf, 1, n, fp);
        fclose(fp);
#ifndef _WIN32
        /* WRITE (not NOTRUNC) makes the file end at the data just written. */
        if (op == CELL_SAVEDATA_FILEOP_WRITE) {
            if (truncate(path, (off_t)offset + (off_t)put) != 0) { /* keep data */ }
        }
#else
        /* winport v46-savedata: same truncation on Windows. */
        if (op == CELL_SAVEDATA_FILEOP_WRITE) {
            int tfd = _open(path, _O_RDWR | _O_BINARY);
            if (tfd >= 0) { (void)_chsize_s(tfd, (long long)offset + (long long)put); _close(tfd); }
        }
#endif
        return (s32)put;
    }
    return -1;
}

static void savedata_write_param_guest(const char* save_path, uint32_t param_ea)
{
    CellSaveDataSystemFileParam p;
    memset(&p, 0, sizeof p);
    /* winport v46-savedata: SD_PARAM_* are offsets inside CellSaveDataStatGet (getParam at +64);
     * param_ea points at a CellSaveDataSystemFileParam itself, so subtract the +64. */
    const uint32_t pb = param_ea - 64u;
    guest_strget(p.title, sizeof p.title, pb + SD_PARAM_TITLE);
    guest_strget(p.subTitle, sizeof p.subTitle, pb + SD_PARAM_SUBTITLE);
    guest_strget(p.detail, sizeof p.detail, pb + SD_PARAM_DETAIL);
    p.attribute = vm_read32(pb + SD_PARAM_ATTRIBUTE);
    guest_strget(p.listParam, sizeof p.listParam, pb + SD_PARAM_LISTPARAM);
    ensure_dirs(save_path);
    write_param_sfo(save_path, &p);
}

static void savedata_read_param_guest(const char* save_path, uint32_t get_ea)
{
    CellSaveDataSystemFileParam p;
    read_param_sfo(save_path, &p);
    guest_strcpy(get_ea + SD_PARAM_TITLE, p.title, 128);
    guest_strcpy(get_ea + SD_PARAM_SUBTITLE, p.subTitle, 128);
    guest_strcpy(get_ea + SD_PARAM_DETAIL, p.detail, 1024);
    vm_write32(get_ea + SD_PARAM_ATTRIBUTE, p.attribute);
    guest_strcpy(get_ea + SD_PARAM_LISTPARAM, p.listParam, 8);
}

/* ---- winport v46-savedata-selftest (engine_fixes_v46_savedata.py; test-only, PS3_SAVEDATA_SELFTEST=1) ---- */
static void sd46_selftest(const char* save_path, const char* dirName, uint32_t setbuf_ea, uint32_t userdata_ea,
                          uint32_t cb_ea, uint32_t msg_ea, uint32_t get_ea, uint32_t set_ea)
{
    const char* e = getenv("PS3_SAVEDATA_SELFTEST_OPD");
    const uint32_t opd = (e && *e) ? (uint32_t)strtoul(e, 0, 0) : 0x005217A0u; /* GoW2 save funcStat */
    uint8_t snap[0x40];
    const int have = userdata_ea && ppu_guest_range_committed(userdata_ea, sizeof snap);
    if (have) memcpy(snap, vm_base + userdata_ea, sizeof snap);
    marshal_cbresult_init(cb_ea, CELL_SAVEDATA_CBRESULT_OK_NEXT, msg_ea, userdata_ea);
    memset(vm_base + get_ea, 0, SAVEDATA_STATGET_SIZE);
    marshal_statget_init(get_ea, 0, dirName, 0, 0);
    savedata_read_param_guest(save_path, get_ea);
    const uint32_t list_max = vm_read32(setbuf_ea + 4);
    const uint32_t buf_size = vm_read32(setbuf_ea + 32);
    const uint32_t buf      = vm_read32(setbuf_ea + 36);
    uint32_t fit = buf_size / SD_FILESTAT_SIZE;
    if (fit > list_max) fit = list_max;
    savedata_list_files(save_path, get_ea, fit ? buf : 0u, fit);
    memset(vm_base + set_ea, 0, SAVEDATA_STATSET_SIZE);
    if (have) {
        /* The game's save manager (func_000BBAC4 / func_000BC058) zeroes these before every save/load
         * thread; the load that just ran left matched(+1C)=count, so mimic that reset. */
        vm_write32(userdata_ea + 0x10, 0); vm_write32(userdata_ea + 0x18, 0); vm_write32(userdata_ea + 0x1C, 0);
        vm_write32(userdata_ea + 0x20, 0); vm_write32(userdata_ea + 0x24, 0); vm_base[userdata_ea + 0x28] = 0;
        const uint32_t tbl = vm_read32(userdata_ea + 0), cnt = vm_read32(userdata_ea + 4);
        printf("[cellSaveData] v46 SELFTEST before: count=%u matched(+1C)=%u unknown(+20)=%u state(+C)=%u\n",
               cnt, vm_read32(userdata_ea + 0x1C), vm_read32(userdata_ea + 0x20), vm_read32(userdata_ea + 0xC));
        for (uint32_t i = 0; i < cnt && i < 32 && tbl; i++) {
            const uint32_t ne = vm_read32(tbl + i * 0x18);
            char nm[16]; guest_strget(nm, sizeof nm, ne);
            printf("[cellSaveData] v46 SELFTEST   want %-12s size=%u\n", nm, vm_read32(tbl + i * 0x18 + 8));
        }
    }
    g_ps3_guest_caller(opd, cb_ea, get_ea, set_ea, 0);
    if (have)
        printf("[cellSaveData] v46 SELFTEST after: matched(+1C)=%u unknown(+20)=%u\n",
               vm_read32(userdata_ea + 0x1C), vm_read32(userdata_ea + 0x20));
    printf("[cellSaveData] v46 SELFTEST save funcStat on existing '%s' -> cbResult.result=%d (0=accepted, -3=BROKEN)\n",
           dirName, (s32)vm_read32(cb_ea + 0));
    if (have) memcpy(vm_base + userdata_ea, snap, sizeof snap);
}

/* The whole Auto save/load. Returns the API result. */
static s32 savedata_auto_guest(const char* dirName, int is_save, uint32_t setbuf_ea,
                               uint32_t stat_opd, uint32_t file_opd, uint32_t userdata_ea)
{
    if (!dirName || !dirName[0] || !setbuf_ea || !stat_opd)
        return CELL_SAVEDATA_ERROR_PARAM;
    if (!g_ps3_guest_caller) return CELL_SAVEDATA_ERROR_CBRESULT;

    char save_path[1024];
    build_save_path(save_path, sizeof(save_path), dirName);
    const int is_new = !dir_exists(save_path);
    printf("[cellSaveData] Auto%s dir='%s' path='%s' new=%d\n",
           is_save ? "Save" : "Load", dirName, save_path, is_new);

    scratch_reset();
    const uint32_t cb_ea  = scratch_alloc(SAVEDATA_CBRESULT_SIZE);
    const uint32_t msg_ea = scratch_alloc(SAVEDATA_INVALIDMSG_SIZE);
    const uint32_t get_ea = scratch_alloc(SAVEDATA_STATGET_SIZE + 64u);
    const uint32_t set_ea = scratch_alloc(SAVEDATA_STATSET_SIZE);
    const uint32_t fg_ea  = scratch_alloc(SD_FILEGET_SIZE);
    const uint32_t fs_ea  = scratch_alloc(SD_FILESET_SIZE);
    if (!cb_ea || !msg_ea || !get_ea || !set_ea || !fg_ea || !fs_ea)
        return CELL_SAVEDATA_ERROR_CBRESULT;

    /* 1. funcStat */
    marshal_cbresult_init(cb_ea, CELL_SAVEDATA_CBRESULT_OK_NEXT, msg_ea, userdata_ea);
    marshal_statget_init(get_ea, is_new, dirName, 0, 0);
    if (!is_new) {
        HOST_STAT_T hst;
        if (HOST_STAT(save_path, &hst) == 0) {
            vm_write64(get_ea + 8,  (uint64_t)PS3_STAT_ATIME_SEC(hst));
            vm_write64(get_ea + 16, (uint64_t)PS3_STAT_MTIME_SEC(hst));
            vm_write64(get_ea + 24, (uint64_t)PS3_STAT_CTIME_SEC(hst));
        }
        savedata_read_param_guest(save_path, get_ea);
        const uint32_t list_max = vm_read32(setbuf_ea + 4);
        const uint32_t buf_size = vm_read32(setbuf_ea + 32);
        const uint32_t buf      = vm_read32(setbuf_ea + 36);
        uint32_t fit = buf_size / SD_FILESTAT_SIZE;
        if (fit > list_max) fit = list_max;
        savedata_list_files(save_path, get_ea, fit ? buf : 0u, fit);
    }
    g_ps3_guest_caller(stat_opd, cb_ea, get_ea, set_ea, 0);
    s32 r = (s32)vm_read32(cb_ea + 0);   /* cbResult.result */
    printf("[cellSaveData] funcStat returned cbResult.result=%d\n", r);
    if (r < 0)
        return is_save ? CELL_SAVEDATA_ERROR_CBRESULT : autoload_map_cb_result(r, is_new);

    const uint32_t param_ea = vm_read32(set_ea + 0);
    const uint32_t recreate = vm_read32(set_ea + 4) & SD_RECREATE_MASK;
    if (is_save) {
        ensure_dirs(save_path);
        if (!is_new && (recreate == SD_RECREATE_YES || recreate == SD_RECREATE_YES_RESET_OWNER))
            savedata_remove_files(save_path);
        if (param_ea) savedata_write_param_guest(save_path, param_ea);
    } else if (is_new) {
        /* Nothing to load and the game did not stop: the directory is missing. */
        if (r == CELL_SAVEDATA_CBRESULT_OK_NEXT) return CELL_SAVEDATA_ERROR_NODATA;
    }
    if (r == CELL_SAVEDATA_CBRESULT_OK_LAST || r == CELL_SAVEDATA_CBRESULT_OK_LAST_NOCONFIRM ||
        !file_opd)
        return CELL_OK;

    /* 2. funcFile until OK_LAST */
    vm_write32(fg_ea + 0, 0);
    for (unsigned guard = 0; guard < 4096u; guard++) {
        marshal_cbresult_init(cb_ea, CELL_SAVEDATA_CBRESULT_OK_NEXT, msg_ea, userdata_ea);
        memset(vm_base + fs_ea, 0, SD_FILESET_SIZE);
        g_ps3_guest_caller(file_opd, cb_ea, fg_ea, fs_ea, 0);
        r = (s32)vm_read32(cb_ea + 0);
        if (r < 0) {
            printf("[cellSaveData] funcFile returned cbResult.result=%d\n", r);
            return CELL_SAVEDATA_ERROR_CBRESULT;
        }
        if (r == CELL_SAVEDATA_CBRESULT_OK_LAST || r == CELL_SAVEDATA_CBRESULT_OK_LAST_NOCONFIRM)
            break;
        const s32 moved = savedata_file_op(save_path, fs_ea);
        if (moved < 0) {
            printf("[cellSaveData] funcFile: invalid file request (op=%u type=%u)\n",
                   vm_read32(fs_ea + 0), vm_read32(fs_ea + 8));
            return CELL_SAVEDATA_ERROR_PARAM;
        }
        vm_write32(fg_ea + 0, (uint32_t)moved);
    }
    if (!is_save && !is_new && getenv("PS3_SAVEDATA_SELFTEST"))   /* winport v46-savedata-selftest2 */
        sd46_selftest(save_path, dirName, setbuf_ea, userdata_ea, cb_ea, msg_ea, get_ea, set_ea);
    printf("[cellSaveData] Auto%s complete for '%s'\n", is_save ? "Save" : "Load", dirName);
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * API implementations
 * -----------------------------------------------------------------------*/

s32 cellSaveDataListSave2(u32 version, CellSaveDataSetList* setList,
                           CellSaveDataSetBuf* setBuf,
                           CellSaveDataListCallback funcList,
                           CellSaveDataStatCallback funcStat,
                           CellSaveDataFileCallback funcFile,
                           u32 container, void* userdata)
{
    printf("[cellSaveData] ListSave2(version=%u)\n", version);

    if (!setList || !setBuf || !funcList || !funcStat)
        return CELL_SAVEDATA_ERROR_PARAM;

    /* Enumerate existing save dirs */
    u32 dir_max = setBuf->dirListMax;
    if (dir_max == 0) dir_max = CELL_SAVEDATA_DIRLIST_MAX;
    CellSaveDataDirList* dirList = (CellSaveDataDirList*)calloc(dir_max, sizeof(CellSaveDataDirList));

    u32 dirCount = enumerate_save_dirs(
        setList->dirNamePrefix ? setList->dirNamePrefix : "",
        dirList, dir_max);

    /* Call list callback */
    CellSaveDataCBResult cbResult;
    memset(&cbResult, 0, sizeof(cbResult));
    cbResult.result = CELL_SAVEDATA_CBRESULT_OK_NEXT;

    CellSaveDataListGet listGet;
    memset(&listGet, 0, sizeof(listGet));
    listGet.dirListNum = dirCount < dir_max ? dirCount : dir_max;
    listGet.dirList = dirList;

    CellSaveDataListSet listSet;
    memset(&listSet, 0, sizeof(listSet));

    funcList(&cbResult, &listGet, &listSet);

    if (cbResult.result < 0) {
        free(dirList);
        return CELL_SAVEDATA_ERROR_CBRESULT;
    }

    /* Determine selected directory name */
    const char* selectedDir = NULL;
    if (listSet.fixedList && listSet.fixedListNum > 0) {
        selectedDir = listSet.fixedList[0].dirName;
    } else if (listSet.focusDirName) {
        selectedDir = listSet.focusDirName;
    } else if (dirCount > 0) {
        selectedDir = dirList[0].dirName;
    }

    if (!selectedDir || selectedDir[0] == '\0') {
        free(dirList);
        return CELL_SAVEDATA_ERROR_NODATA;
    }

    s32 result = savedata_execute(selectedDir, 1, setBuf, funcStat, funcFile, userdata);
    free(dirList);
    return result;
}

s32 cellSaveDataListLoad2(u32 version, CellSaveDataSetList* setList,
                           CellSaveDataSetBuf* setBuf,
                           CellSaveDataListCallback funcList,
                           CellSaveDataStatCallback funcStat,
                           CellSaveDataFileCallback funcFile,
                           u32 container, void* userdata)
{
    printf("[cellSaveData] ListLoad2(version=%u)\n", version);

    if (!setList || !setBuf || !funcList || !funcStat)
        return CELL_SAVEDATA_ERROR_PARAM;

    u32 dir_max = setBuf->dirListMax;
    if (dir_max == 0) dir_max = CELL_SAVEDATA_DIRLIST_MAX;
    CellSaveDataDirList* dirList = (CellSaveDataDirList*)calloc(dir_max, sizeof(CellSaveDataDirList));

    u32 dirCount = enumerate_save_dirs(
        setList->dirNamePrefix ? setList->dirNamePrefix : "",
        dirList, dir_max);

    CellSaveDataCBResult cbResult;
    memset(&cbResult, 0, sizeof(cbResult));
    cbResult.result = CELL_SAVEDATA_CBRESULT_OK_NEXT;

    CellSaveDataListGet listGet;
    memset(&listGet, 0, sizeof(listGet));
    listGet.dirListNum = dirCount < dir_max ? dirCount : dir_max;
    listGet.dirList = dirList;

    CellSaveDataListSet listSet;
    memset(&listSet, 0, sizeof(listSet));

    funcList(&cbResult, &listGet, &listSet);

    if (cbResult.result < 0) {
        free(dirList);
        return CELL_SAVEDATA_ERROR_CBRESULT;
    }

    const char* selectedDir = NULL;
    if (listSet.fixedList && listSet.fixedListNum > 0) {
        selectedDir = listSet.fixedList[0].dirName;
    } else if (listSet.focusDirName) {
        selectedDir = listSet.focusDirName;
    } else if (dirCount > 0) {
        selectedDir = dirList[0].dirName;
    }

    if (!selectedDir || selectedDir[0] == '\0') {
        free(dirList);
        return CELL_SAVEDATA_ERROR_NODATA;
    }

    s32 result = savedata_execute(selectedDir, 0, setBuf, funcStat, funcFile, userdata);
    free(dirList);
    return result;
}

s32 cellSaveDataFixedSave2(u32 version, CellSaveDataSetList* setList,
                            CellSaveDataSetBuf* setBuf,
                            CellSaveDataFixedCallback funcFixed,
                            CellSaveDataStatCallback funcStat,
                            CellSaveDataFileCallback funcFile,
                            u32 container, void* userdata)
{
    printf("[cellSaveData] FixedSave2(version=%u)\n", version);

    if (!setList || !setBuf || !funcFixed || !funcStat)
        return CELL_SAVEDATA_ERROR_PARAM;

    u32 dir_max = setBuf->dirListMax;
    if (dir_max == 0) dir_max = CELL_SAVEDATA_DIRLIST_MAX;
    CellSaveDataDirList* dirList = (CellSaveDataDirList*)calloc(dir_max, sizeof(CellSaveDataDirList));

    u32 dirCount = enumerate_save_dirs(
        setList->dirNamePrefix ? setList->dirNamePrefix : "",
        dirList, dir_max);

    CellSaveDataCBResult cbResult;
    memset(&cbResult, 0, sizeof(cbResult));
    cbResult.result = CELL_SAVEDATA_CBRESULT_OK_NEXT;

    CellSaveDataListGet listGet;
    memset(&listGet, 0, sizeof(listGet));
    listGet.dirListNum = dirCount < dir_max ? dirCount : dir_max;
    listGet.dirList = dirList;

    CellSaveDataListSet listSet;
    memset(&listSet, 0, sizeof(listSet));

    funcFixed(&cbResult, &listGet, &listSet);

    if (cbResult.result < 0) {
        free(dirList);
        return CELL_SAVEDATA_ERROR_CBRESULT;
    }

    const char* selectedDir = NULL;
    if (listSet.fixedList && listSet.fixedListNum > 0) {
        selectedDir = listSet.fixedList[0].dirName;
    } else if (listSet.focusDirName) {
        selectedDir = listSet.focusDirName;
    }

    if (!selectedDir || selectedDir[0] == '\0') {
        free(dirList);
        return CELL_SAVEDATA_ERROR_NODATA;
    }

    s32 result = savedata_execute(selectedDir, 1, setBuf, funcStat, funcFile, userdata);
    free(dirList);
    return result;
}

s32 cellSaveDataFixedLoad2(u32 version, CellSaveDataSetList* setList,
                            CellSaveDataSetBuf* setBuf,
                            CellSaveDataFixedCallback funcFixed,
                            CellSaveDataStatCallback funcStat,
                            CellSaveDataFileCallback funcFile,
                            u32 container, void* userdata)
{
    printf("[cellSaveData] FixedLoad2(version=%u)\n", version);

    if (!setList || !setBuf || !funcFixed || !funcStat)
        return CELL_SAVEDATA_ERROR_PARAM;

    u32 dir_max = setBuf->dirListMax;
    if (dir_max == 0) dir_max = CELL_SAVEDATA_DIRLIST_MAX;
    CellSaveDataDirList* dirList = (CellSaveDataDirList*)calloc(dir_max, sizeof(CellSaveDataDirList));

    u32 dirCount = enumerate_save_dirs(
        setList->dirNamePrefix ? setList->dirNamePrefix : "",
        dirList, dir_max);

    CellSaveDataCBResult cbResult;
    memset(&cbResult, 0, sizeof(cbResult));
    cbResult.result = CELL_SAVEDATA_CBRESULT_OK_NEXT;

    CellSaveDataListGet listGet;
    memset(&listGet, 0, sizeof(listGet));
    listGet.dirListNum = dirCount < dir_max ? dirCount : dir_max;
    listGet.dirList = dirList;

    CellSaveDataListSet listSet;
    memset(&listSet, 0, sizeof(listSet));

    funcFixed(&cbResult, &listGet, &listSet);

    if (cbResult.result < 0) {
        free(dirList);
        return CELL_SAVEDATA_ERROR_CBRESULT;
    }

    const char* selectedDir = NULL;
    if (listSet.fixedList && listSet.fixedListNum > 0) {
        selectedDir = listSet.fixedList[0].dirName;
    } else if (listSet.focusDirName) {
        selectedDir = listSet.focusDirName;
    }

    if (!selectedDir || selectedDir[0] == '\0') {
        free(dirList);
        return CELL_SAVEDATA_ERROR_NODATA;
    }

    s32 result = savedata_execute(selectedDir, 0, setBuf, funcStat, funcFile, userdata);
    free(dirList);
    return result;
}

s32 cellSaveDataAutoSave2(u32 version, const char* dirName,
                           u32 errDialog,
                           CellSaveDataSetBuf* setBuf,
                           CellSaveDataStatCallback funcStat,
                           CellSaveDataFileCallback funcFile,
                           u32 container, void* userdata)
{
    (void)version; (void)errDialog; (void)container;
    dirName = savedata_dir_host(dirName);
    printf("[cellSaveData] AutoSave2(version=%u, dir='%s')\n", version, dirName ? dirName : "<null>");
    return savedata_auto_guest(dirName, 1,
                               (uint32_t)(uintptr_t)setBuf, (uint32_t)(uintptr_t)funcStat,
                               (uint32_t)(uintptr_t)funcFile, (uint32_t)(uintptr_t)userdata);
}

s32 cellSaveDataAutoLoad2(u32 version, const char* dirName,
                           u32 errDialog,
                           CellSaveDataSetBuf* setBuf,
                           CellSaveDataStatCallback funcStat,
                           CellSaveDataFileCallback funcFile,
                           u32 container, void* userdata)
{
    (void)version; (void)errDialog; (void)container;
    dirName = savedata_dir_host(dirName);
    printf("[cellSaveData] AutoLoad2(version=%u, dir='%s')\n", version, dirName ? dirName : "<null>");
    return savedata_auto_guest(dirName, 0,
                               (uint32_t)(uintptr_t)setBuf, (uint32_t)(uintptr_t)funcStat,
                               (uint32_t)(uintptr_t)funcFile, (uint32_t)(uintptr_t)userdata);
}

s32 cellSaveDataDelete2(u32 container)
{
    printf("[cellSaveData] Delete2(container=%u)\n", container);

    /* Without a directory name we can't delete anything meaningful.
       This variant typically shows a UI for deletion - just succeed. */
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Old / non-_2 variants — same wrapper pattern as the _2 versions, with
 * the same guest-callback caveat.
 *
 * Older PS3 SDK builds (e.g. our flOw NPUA80001 dump) link the original
 * cellSaveDataAutoSave / AutoLoad / Delete instead of the _2 variants
 * RPCS3's flOw build uses. Same semantics, different NID.
 * -----------------------------------------------------------------------*/
s32 cellSaveDataAutoSave(u32 version, const char* dirName,
                          u32 errDialog,
                          CellSaveDataSetBuf* setBuf,
                          CellSaveDataStatCallback funcStat,
                          CellSaveDataFileCallback funcFile,
                          u32 container, void* userdata)
{
    (void)version; (void)errDialog; (void)container;
    dirName = savedata_dir_host(dirName);
    printf("[cellSaveData] AutoSave(version=%u, dir='%s')\n", version, dirName ? dirName : "<null>");
    return savedata_auto_guest(dirName, 1,
                               (uint32_t)(uintptr_t)setBuf, (uint32_t)(uintptr_t)funcStat,
                               (uint32_t)(uintptr_t)funcFile, (uint32_t)(uintptr_t)userdata);
}

s32 cellSaveDataAutoLoad(u32 version, const char* dirName,
                          u32 errDialog,
                          CellSaveDataSetBuf* setBuf,
                          CellSaveDataStatCallback funcStat,
                          CellSaveDataFileCallback funcFile,
                          u32 container, void* userdata)
{
    (void)version; (void)errDialog; (void)container;
    dirName = savedata_dir_host(dirName);
    printf("[cellSaveData] AutoLoad(version=%u, dir='%s')\n", version, dirName ? dirName : "<null>");
    return savedata_auto_guest(dirName, 0,
                               (uint32_t)(uintptr_t)setBuf, (uint32_t)(uintptr_t)funcStat,
                               (uint32_t)(uintptr_t)funcFile, (uint32_t)(uintptr_t)userdata);
}

s32 cellSaveDataDelete(u32 version, const char* dirName,
                        u32 container)
{
    (void)version;
    dirName = savedata_dir_host(dirName);
    printf("[cellSaveData] Delete(dir='%s', container=%u)\n",
           dirName ? dirName : "<null>", container);
    return CELL_OK;
}
