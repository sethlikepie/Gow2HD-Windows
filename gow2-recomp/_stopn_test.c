#include "spu_context.h"
#include "spu_helpers.h"
#include "spu_dma.h"
#include "spu_interp.h"
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* DMA backing memory (spu_dma.h externs vm_base; spu_channels.c references it). */
static uint8_t g_mem[4096];
uint8_t* vm_base = g_mem;

static void w(uint8_t* p, uint32_t v){ p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v; }
static uint32_t e_ri16(uint32_t op9,uint32_t i16,uint32_t rt){return ((op9&0x1FF)<<23)|((i16&0xFFFF)<<7)|(rt&0x7F);}
static uint32_t e_ri10(uint32_t op8,int i10,uint32_t ra,uint32_t rt){return ((op8&0xFF)<<24)|(((uint32_t)i10&0x3FF)<<14)|((ra&0x7F)<<7)|(rt&0x7F);}
static uint32_t e_stop(uint32_t code){ return (0x000u<<21) | (code & 0x3FFFu); }

static int      g_calls;
static uint32_t g_codes[8];
static int my_stop_handler(spu_context* ctx, uint32_t code){
    (void)ctx;
    if (g_calls < 8) g_codes[g_calls] = code;
    g_calls++;
    return (code == 0x1234u) ? 1 : 0;   /* resume on 0x1234, terminate otherwise */
}

#define LS_SIZE (256u*1024u)
static uint8_t g_ls[LS_SIZE];

int main(void){
    int ok = 1;
    uint32_t prog[] = {
        e_ri16(0x081, 0xAA, 4),   /* il   r4, 0xAA   */
        e_stop(0x1234),           /* stop 0x1234     -> RESUME */
        e_ri10(0x1C, 1, 4, 4),    /* ai   r4, r4, 1  */
        e_stop(0x0000),           /* stop 0          -> TERMINATE */
    };

    /* (a1) default behaviour: NO handler => first stop terminates */
    spu_set_stop_handler(NULL);
    memset(g_ls, 0, sizeof(g_ls));
    for (size_t i=0;i<sizeof(prog)/4;i++) w(&g_ls[i*4], prog[i]);
    int st0 = spu_interp_run_ctx(g_ls, 0, NULL);
    if (st0 != SPU_STATUS_STOPPED_BY_STOP){ printf("FAIL default st=0x%X\n", st0); ok=0; }

    /* (a2) with handler: resume once on 0x1234, then terminate on 0 */
    g_calls = 0; memset(g_codes, 0, sizeof(g_codes));
    spu_set_stop_handler(my_stop_handler);
    memset(g_ls, 0, sizeof(g_ls));
    for (size_t i=0;i<sizeof(prog)/4;i++) w(&g_ls[i*4], prog[i]);
    int st1 = spu_interp_run_ctx(g_ls, 0, NULL);
    if (st1 != SPU_STATUS_STOPPED_BY_STOP){ printf("FAIL serviced st=0x%X\n", st1); ok=0; }
    if (g_calls != 2){ printf("FAIL handler calls=%d (expected 2)\n", g_calls); ok=0; }
    if (g_codes[0] != 0x1234u){ printf("FAIL code0=0x%X (expected 1234)\n", g_codes[0]); ok=0; }
    if (g_calls >= 2 && g_codes[1] != 0u){ printf("FAIL code1=0x%X (expected 0)\n", g_codes[1]); ok=0; }
    spu_set_stop_handler(NULL);   /* restore default; don't leak to other tests */

    /* (b) atomic mailbox round-trip sanity */
    spu_channel ch; memset(&ch, 0, sizeof(ch));
    if (spu_channel_has_data(&ch)){ printf("FAIL fresh chan has data\n"); ok=0; }
    spu_channel_write(&ch, 0xDEADBEEFu);
    if (!spu_channel_has_data(&ch)){ printf("FAIL chan no data after write\n"); ok=0; }
    if (spu_channel_read(&ch) != 0xDEADBEEFu){ printf("FAIL chan read mismatch\n"); ok=0; }
    if (spu_channel_has_data(&ch)){ printf("FAIL chan still has data after read\n"); ok=0; }

    printf(ok ? "[PASS] stop-N: NULL terminates, handler resumes on 0x1234 then stop 0 ends; atomic mbox ok\n"
              : "[FAIL] stop-N / atomic mbox\n");
    return ok ? 0 : 1;
}

