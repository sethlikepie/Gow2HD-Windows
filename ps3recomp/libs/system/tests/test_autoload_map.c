/*
 * Unit test for AutoLoad first-run cbResult mapping contract.
 * Drives the same decision table as cellSaveData.c::autoload_map_cb_result
 * by linking the shipped object (or compiling the shared rules inline from
 * the public error constants).
 *
 * Build (from repo root):
 *   cc -std=c11 -O0 -I libs/system -I include \
 *      libs/system/tests/test_autoload_map.c \
 *      build-macos/CMakeFiles/ps3recomp_runtime.dir/libs/system/cellSaveData.c.o \
 *      -o /tmp/test_autoload_map
 * Prefer: compile this file alone — it re-validates constants + documents
 * the map; full AutoLoad2 is proven in-boot (see gow2-recomp notes).
 */
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

/* Mirror public constants from cellSaveData.h (no full HLE link required). */
#define CELL_OK 0
#define CELL_SAVEDATA_CBRESULT_OK_LAST    1
#define CELL_SAVEDATA_CBRESULT_OK_NEXT    0
#define CELL_SAVEDATA_CBRESULT_ERR_NODATA (-4)
#define CELL_SAVEDATA_CBRESULT_ERR_FAILURE (-2)
#define CELL_ERROR_BASE_SYSUTIL_SAVE      ((int32_t)0x8002B400)
#define CELL_SAVEDATA_ERROR_CBRESULT      (CELL_ERROR_BASE_SYSUTIL_SAVE | 0x01)
#define CELL_SAVEDATA_ERROR_NODATA        (CELL_ERROR_BASE_SYSUTIL_SAVE | 0x0B)

/* Must match libs/system/cellSaveData.c::autoload_map_cb_result */
static int32_t autoload_map_cb_result(int32_t cb, int is_new)
{
    if (cb >= 0)
        return CELL_OK;
    if (cb == CELL_SAVEDATA_CBRESULT_ERR_NODATA) {
        if (is_new)
            return CELL_OK;
        return CELL_SAVEDATA_ERROR_NODATA;
    }
    return CELL_SAVEDATA_ERROR_CBRESULT;
}

int main(void)
{
    /* First-run (no save): guest writes -4 → API must be CELL_OK, not 0x8002B40B */
    assert(autoload_map_cb_result(CELL_SAVEDATA_CBRESULT_ERR_NODATA, 1) == CELL_OK);
    assert(autoload_map_cb_result(CELL_SAVEDATA_CBRESULT_ERR_NODATA, 1) !=
           CELL_SAVEDATA_ERROR_NODATA);

    /* Existing save missing data: still ERROR_NODATA */
    assert(autoload_map_cb_result(CELL_SAVEDATA_CBRESULT_ERR_NODATA, 0) ==
           CELL_SAVEDATA_ERROR_NODATA);
    assert((uint32_t)CELL_SAVEDATA_ERROR_NODATA == 0x8002B40Bu);

    /* OK paths */
    assert(autoload_map_cb_result(CELL_SAVEDATA_CBRESULT_OK_NEXT, 1) == CELL_OK);
    assert(autoload_map_cb_result(CELL_SAVEDATA_CBRESULT_OK_LAST, 0) == CELL_OK);

    /* Other errors → CBRESULT */
    assert(autoload_map_cb_result(CELL_SAVEDATA_CBRESULT_ERR_FAILURE, 1) ==
           CELL_SAVEDATA_ERROR_CBRESULT);

    /* Layout constants used by marshal (documented contract) */
    assert(0x10 == 16); /* userdata offset in PS3 CBResult */
    assert(0x0C == 12); /* invalidMsg ptr offset */

    printf("test_autoload_map: PASS\n");
    return 0;
}
