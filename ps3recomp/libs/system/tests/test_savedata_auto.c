/*
 * End-to-end contract of the automatic memory card (cellSaveDataAutoSave2 /
 * AutoLoad2) driven by GUEST callbacks, with a fake guest memory and a fake
 * game that answers funcStat/funcFile like GoW2 does.
 *
 * Breaks caught: callbacks called as host pointers (crash on the first save),
 * AutoLoad never running funcFile (a save that never loads back), OK_NEXT /
 * OK_LAST swapped (file loop ends early or never), saves written relative to
 * the working directory, first-boot "no save data" no longer 0x8002B401.
 */
#include "cellSaveData.h"
#include "ps3emu/guest_call.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- runtime symbols the HLE module links against ---------------------- */
uint8_t* vm_base;
ps3_guest_caller_fn g_ps3_guest_caller;
volatile int g_ppu_preempt_req;
int ppu_guest_range_committed(uint32_t addr, uint32_t n) { (void)addr; (void)n; return 1; }

#define VM_SIZE 0x02600000u

static uint32_t rd32(uint32_t ea)
{
    const uint8_t* p = vm_base + ea;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static void wr32(uint32_t ea, uint32_t v)
{
    uint8_t* p = vm_base + ea;
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* ---- fake game ---------------------------------------------------------- */
enum { OPD_STAT_SAVE = 0x10000, OPD_FILE_SAVE = 0x10010,
       OPD_STAT_LOAD = 0x10020, OPD_FILE_LOAD = 0x10030, OPD_FILE_EVIL = 0x10040 };
#define EA_DIR      0x00020000u
#define EA_SETBUF   0x00020100u
#define EA_LISTBUF  0x00021000u
#define EA_PARAM    0x00030000u
#define EA_NAME     0x00031000u
#define EA_SAVEDATA 0x00100000u
#define EA_LOADDATA 0x00200000u
#define DATA_SIZE   4096u

static int s_file_calls;
static uint32_t s_seen_isnew, s_seen_filenum, s_seen_listnum, s_seen_excsize;
static char s_seen_title[64], s_seen_first_file[16];
static int s_stat_result;

static void fake_game(uint32_t opd, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
    const uint32_t cb = (uint32_t)a0, x = (uint32_t)a1, y = (uint32_t)a2;
    (void)a3;
    switch (opd) {
    case OPD_STAT_SAVE:
        s_seen_isnew = rd32(x + 4);
        wr32(cb, CELL_SAVEDATA_CBRESULT_OK_NEXT);
        memset(vm_base + EA_PARAM, 0, 1616);
        strcpy((char*)vm_base + EA_PARAM + 64, "God of War II");
        strcpy((char*)vm_base + EA_PARAM + 320, "Chapter 1");
        wr32(y + 0, EA_PARAM);                  /* setParam */
        wr32(y + 4, 0);                         /* reCreateMode: NO */
        s_file_calls = 0;
        break;
    case OPD_FILE_SAVE:
        memset(vm_base + y, 0, 48);
        if (s_file_calls == 0) {                /* the game data */
            strcpy((char*)vm_base + EA_NAME, "USR-DATA");
            wr32(y + 0, CELL_SAVEDATA_FILEOP_WRITE);
            wr32(y + 8, CELL_SAVEDATA_FILETYPE_SECUREFILE);
            wr32(y + 28, EA_NAME);
            wr32(y + 36, DATA_SIZE);
            wr32(y + 40, DATA_SIZE);
            wr32(y + 44, EA_SAVEDATA);
            wr32(cb, CELL_SAVEDATA_CBRESULT_OK_NEXT);
        } else if (s_file_calls == 1) {         /* the icon */
            wr32(y + 0, CELL_SAVEDATA_FILEOP_WRITE);
            wr32(y + 8, CELL_SAVEDATA_FILETYPE_CONTENT_ICON0);
            wr32(y + 36, 16);
            wr32(y + 40, 16);
            wr32(y + 44, EA_SAVEDATA);
            wr32(cb, CELL_SAVEDATA_CBRESULT_OK_NEXT);
        } else {
            wr32(cb, CELL_SAVEDATA_CBRESULT_OK_LAST);
        }
        s_file_calls++;
        break;
    case OPD_STAT_LOAD:
        s_seen_isnew = rd32(x + 4);
        s_seen_filenum = rd32(x + 1628);
        s_seen_listnum = rd32(x + 1632);
        memcpy(s_seen_title, vm_base + x + 64, sizeof s_seen_title - 1);
        if (s_seen_listnum)
            memcpy(s_seen_first_file, vm_base + rd32(x + 1636) + 40, 13);
        wr32(cb, (uint32_t)s_stat_result);
        s_file_calls = 0;
        break;
    case OPD_FILE_LOAD:
        memset(vm_base + y, 0, 48);
        if (s_file_calls == 0) {
            strcpy((char*)vm_base + EA_NAME, "USR-DATA");
            wr32(y + 0, CELL_SAVEDATA_FILEOP_READ);
            wr32(y + 8, CELL_SAVEDATA_FILETYPE_SECUREFILE);
            wr32(y + 28, EA_NAME);
            wr32(y + 36, DATA_SIZE);
            wr32(y + 40, DATA_SIZE);
            wr32(y + 44, EA_LOADDATA);
            wr32(cb, CELL_SAVEDATA_CBRESULT_OK_NEXT);
        } else {
            s_seen_excsize = rd32(x + 0);       /* fileGet.excSize */
            wr32(cb, CELL_SAVEDATA_CBRESULT_OK_LAST);
        }
        s_file_calls++;
        break;
    case OPD_FILE_EVIL:                         /* tries to escape the save dir */
        memset(vm_base + y, 0, 48);
        strcpy((char*)vm_base + EA_NAME, "../EVIL");
        wr32(y + 0, CELL_SAVEDATA_FILEOP_WRITE);
        wr32(y + 8, CELL_SAVEDATA_FILETYPE_NORMALFILE);
        wr32(y + 28, EA_NAME);
        wr32(y + 36, 16);
        wr32(y + 40, 16);
        wr32(y + 44, EA_SAVEDATA);
        wr32(cb, CELL_SAVEDATA_CBRESULT_OK_NEXT);
        break;
    default:
        assert(!"unknown callback");
    }
}

#define P(ea) ((void*)(uintptr_t)(ea))

static s32 auto_save(void)
{
    return cellSaveDataAutoSave2(0, P(EA_DIR), 0, P(EA_SETBUF),
                                 (CellSaveDataStatCallback)P(OPD_STAT_SAVE),
                                 (CellSaveDataFileCallback)P(OPD_FILE_SAVE), 0, P(0));
}
static s32 auto_load(void)
{
    return cellSaveDataAutoLoad2(0, P(EA_DIR), 0, P(EA_SETBUF),
                                 (CellSaveDataStatCallback)P(OPD_STAT_LOAD),
                                 (CellSaveDataFileCallback)P(OPD_FILE_LOAD), 0, P(0));
}

int main(void)
{
    char root[] = "/tmp/ps3recomp-savedata-XXXXXX";
    assert(mkdtemp(root));
    setenv("PS3_SAVEDATA_ROOT", root, 1);

    vm_base = calloc(1, VM_SIZE);
    assert(vm_base);
    g_ps3_guest_caller = fake_game;
    strcpy((char*)vm_base + EA_DIR, "BCUS98229_GOW2");
    wr32(EA_SETBUF + 0, 1);            /* dirListMax */
    wr32(EA_SETBUF + 4, 8);            /* fileListMax */
    wr32(EA_SETBUF + 32, 8 * 56);      /* bufSize */
    wr32(EA_SETBUF + 36, EA_LISTBUF);  /* buf */

    /* First boot, no memory card data: the game's funcStat answers NODATA and
     * the API reports CBRESULT (0x8002B401), exactly as on the console. */
    s_stat_result = CELL_SAVEDATA_CBRESULT_ERR_NODATA;
    assert(auto_load() == (s32)0x8002B401);
    assert(s_seen_isnew == 1);

    /* Automatic save: data file + icon, PARAM from the game's setParam. */
    for (uint32_t i = 0; i < DATA_SIZE; i++) vm_base[EA_SAVEDATA + i] = (uint8_t)(i * 7 + 3);
    assert(auto_save() == CELL_OK);
    assert(s_seen_isnew == 1);
    assert(s_file_calls == 3);         /* two writes, then OK_LAST */
    {
        char path[512];
        struct stat st;
        snprintf(path, sizeof path, "%s/BCUS98229_GOW2/USR-DATA", root);
        assert(stat(path, &st) == 0 && st.st_size == DATA_SIZE);
        snprintf(path, sizeof path, "%s/BCUS98229_GOW2/ICON0.PNG", root);
        assert(stat(path, &st) == 0 && st.st_size == 16);
    }

    /* Automatic load on the next boot: the game sees the existing save, its
     * title and file list, and the data comes back byte for byte. */
    s_stat_result = CELL_SAVEDATA_CBRESULT_OK_NEXT;
    memset(vm_base + EA_LOADDATA, 0, DATA_SIZE);
    assert(auto_load() == CELL_OK);
    assert(s_seen_isnew == 0);
    assert(strcmp(s_seen_title, "God of War II") == 0);
    assert(s_seen_filenum == 2 && s_seen_listnum == 2);
    assert(strcmp(s_seen_first_file, "USR-DATA") == 0 || strcmp(s_seen_first_file, "ICON0.PNG") == 0);
    assert(s_seen_excsize == DATA_SIZE);
    assert(memcmp(vm_base + EA_SAVEDATA, vm_base + EA_LOADDATA, DATA_SIZE) == 0);

    /* Overwrite keeps working (second save over the existing directory). */
    vm_base[EA_SAVEDATA] ^= 0xFF;
    assert(auto_save() == CELL_OK);
    assert(s_seen_isnew == 0);
    assert(auto_load() == CELL_OK);
    assert(vm_base[EA_LOADDATA] == vm_base[EA_SAVEDATA]);

    /* A path in the file name is refused, never written outside the save. */
    assert(cellSaveDataAutoSave2(0, P(EA_DIR), 0, P(EA_SETBUF),
                                 (CellSaveDataStatCallback)P(OPD_STAT_SAVE),
                                 (CellSaveDataFileCallback)P(OPD_FILE_EVIL), 0, P(0))
           == CELL_SAVEDATA_ERROR_PARAM);
    {
        char path[512];
        struct stat st;
        snprintf(path, sizeof path, "%s/EVIL", root);
        assert(stat(path, &st) != 0);
    }

    printf("test_savedata_auto: PASS\n");
    return 0;
}
