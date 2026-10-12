/*
 * ps3recomp - cellGcmSys HLE module implementation
 *
 * Manages RSX initialization, display buffer registration, flip control,
 * command buffer control, tile/zcull configuration, IO memory mapping,
 * report/label areas, and address-to-offset translation.
 *
 * Actual rendering is handled elsewhere -- this module just tracks state.
 */

#include "cellGcmSys.h"
int wp_stats_on(void);   /* winport release: WINPORT_STATS=1 logs the per-second statistics (win_compat.c) */
#include "gcm_wait_ns.h"
#include "gcm_eager_put.h"
#include "gcm_ovf_unlock.h"
#include "gcm_fifo_nolock.h"
#include "gcm_grow_cmd.h"
#include "gcm_fifo_fast.h"
#include "rsx_overlay.h"    /* runtime overlay: settings load before D3D12 init */
#ifdef _WIN32
#include "rsx_game_identity.h" /* overlay title from the game's own PARAM.SFO */
#endif
#include "rsx_commands.h"   /* live-path bridge to the real RSX command processor + backends */
#include "rsx_capture.h"    /* PS3_RSX_CAPTURE frame recorder (probe, OFF by default) */
#include "ps3_checkpoint.h"
#include "ps3_trace.h"      /* ps3_trace_on(PS3_TS_SNAPSHOT) -- gate do contador de flips abaixo */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <pthread.h>
#endif

/* Darwin/Linux host backtrace for PS3_TRACE_MAINLOOP flip issuer (R6). */
#if defined(__APPLE__) || defined(__linux__)
#include <execinfo.h>
#include <dlfcn.h>
#endif

/* ---------------------------------------------------------------------------
 * Minimal RSX/GPU: the control register must live in GUEST memory so the game
 * can read/write put/get and our "GPU" can advance get. Reserve a fixed guest
 * address in main memory, just past the import-OPD scratch (0x0A000000). The
 * "GPU" pretends the FIFO is consumed instantly (get=put) and bumps ref.
 * -----------------------------------------------------------------------*/
#define GCM_CTRL_EA    0x0A010000u  /* guest CellGcmControl {put@0,get@4,ref@8} */
#define GCM_LABEL_EA   0x0A020000u  /* guest label area (GPU completion words) */
#define GCM_REPORT_EA  0x0A030000u  /* guest report area */
#define GCM_NOTIFY_EA  0x0A040000u  /* guest notify area */
extern unsigned int vm_read32(unsigned long long a);
extern void         vm_write32(unsigned long long a, unsigned int v);

/* ---------------------------------------------------------------------------
 * Timestamp helpers (same pattern as sys_timer)
 * -----------------------------------------------------------------------*/

#ifdef _WIN32
static LARGE_INTEGER s_qpc_freq;
static int           s_qpc_init = 0;

static void ensure_qpc_init(void)
{
    if (!s_qpc_init) {
        QueryPerformanceFrequency(&s_qpc_freq);
        s_qpc_init = 1;
    }
}

static u64 get_timestamp_ns(void)
{
    LARGE_INTEGER now;
    ensure_qpc_init();
    QueryPerformanceCounter(&now);
    /* Convert to nanoseconds: (count * 1e9) / freq */
    return (u64)((double)now.QuadPart * 1000000000.0 / (double)s_qpc_freq.QuadPart);
}
#else
static u64 get_timestamp_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ULL + (u64)ts.tv_nsec;
}
#endif

/* ---------------------------------------------------------------------------
 * Internal state
 * -----------------------------------------------------------------------*/

static int  s_gcm_initialized = 0;
static u32  s_flip_mode   = CELL_GCM_DISPLAY_VSYNC;
static u32  s_flip_status = CELL_GCM_FLIP_STATUS_DONE;
static u32  s_debug_level = CELL_GCM_DEBUG_LEVEL0;

/* Task 2 (bringup-instrumentacao-fase0): contador de flips bem sucedidos,
 * para o campo last_flip= do subsistema SNAPSHOT (ps3_checkpoint.c /
 * ps3_gcm_flip_count() abaixo). Distinto de s_ml_flip_n (mais abaixo), que
 * so' incrementa sob PS3_TRACE_MAINLOOP.
 *
 * Review pos-Task 2 (achado #1, "Also fix"): a versao original incrementava
 * SEMPRE e com `++` simples (nao atomico). Isso viola G2 no baseline OFF (o
 * runtime deve ser um no-op estrito com todas as PS3_TRACE_* por definir) e
 * e' uma corrida de dados: cellGcmSetFlipCommand e' alcancavel a partir de
 * mais de uma thread host (qualquer thread PPU guest que chame a NID pode
 * correr em paralelo com outra). Agora o incremento so' acontece quando
 * SNAPSHOT esta ligado (o unico consumidor de last_flip=; ps3_trace_on()
 * cacheia o getenv() na primeira chamada, portanto e' barato mesmo no
 * caminho quente) e usa __sync_fetch_and_add em vez de `++`. */
static u32  s_ps3_flip_count = 0;

/* Display buffers */
static CellGcmDisplayInfo s_display_buffers[CELL_GCM_MAX_DISPLAY_BUFFER_NUM];
static int s_display_buffer_set[CELL_GCM_MAX_DISPLAY_BUFFER_NUM];
static u32 s_current_display_buffer_id = 0;

/* Configuration */
static CellGcmConfig s_config;

/* Command buffer control */
static CellGcmControl s_control;

/* Offset table storage */
static u16 s_io_address_table[65536];
static u16 s_ea_address_table[65536];

static CellGcmOffsetTable s_offset_table = {
    s_io_address_table,
    s_ea_address_table,
};

/* Local memory bump allocator */
static u32 s_local_mem_allocated = 0;  /* next free offset in local memory */

/* IO memory mapping table */
typedef struct IoMapping {
    u32 ea;         /* effective address in main memory */
    u32 io;         /* IO offset (RSX-visible) */
    u32 size;       /* size of mapping */
    int active;     /* 1 if this slot is in use */
} IoMapping;

static IoMapping s_io_mappings[CELL_GCM_MAX_IO_MAPPINGS];
static int s_io_mapping_count = 0;

/* Callback handlers — these are GUEST OPD addresses passed by the
 * recompiled game, not host function pointers. Stored as uint32_t and
 * invoked through g_ps3_guest_caller (see ps3emu/guest_call.h). */
static u32 s_flip_handler_opd     = 0;
static u32 s_vblank_handler_opd   = 0;
static u32 s_user_handler_opd     = 0;
static u32 s_second_v_handler_opd = 0;
/* Legacy host-typed slots kept around for any caller still treating
 * these as host pointers. New code should use the _opd slots. */
static CellGcmFlipHandler    s_flip_handler    = NULL;
static CellGcmVBlankHandler  s_vblank_handler  = NULL;
static CellGcmUserHandler    s_user_handler    = NULL;
static CellGcmSecondVHandler s_second_v_handler = NULL;

/* Flip timing */
static u64 s_last_flip_time = 0;

/* VBlank counter (incremented on each flip as approximation) */
static u32 s_vblank_count = 0;

/* IO map reservation tracking */
static u32 s_io_map_reserved = 0;  /* bytes reserved for IO mapping */

/* Default FIFO mode */
static s32 s_default_fifo_mode = 0;

/* Graphics / queue handlers */
static CellGcmGraphicsHandler s_graphics_handler = NULL;
static CellGcmQueueHandler    s_queue_handler    = NULL;

/* Second V / VBlank frequency */
static u32 s_second_v_frequency = 0;
static u32 s_vblank_frequency   = 0;

/* User command */
static u32 s_user_command = 0;

/* Tile configuration (up to 15 tiles, 8 commonly used) */
static CellGcmTileInfo s_tiles[CELL_GCM_MAX_TILE_COUNT];

/* Zcull configuration (8 zcull regions) */
static CellGcmZcullInfo s_zcull[CELL_GCM_MAX_ZCULL_COUNT];

/* Report data area (256 slots, each 16 bytes) */
static CellGcmReportData s_report_data[CELL_GCM_MAX_REPORT_COUNT];

/* Label area (256 labels, each u32) */
static u32 s_labels[CELL_GCM_MAX_LABEL_COUNT];

/* ---------------------------------------------------------------------------
 * Internal helpers
 * -----------------------------------------------------------------------*/

/* Populate the offset table entries for an IO mapping */
static void populate_offset_table(u32 ea, u32 io, u32 size)
{
    /*
     * The offset table uses 1MB pages (20-bit shift).
     * ioAddress[ea >> 20] = io >> 20   (EA -> IO offset)
     * eaAddress[io >> 20] = ea >> 20   (IO offset -> EA)
     */
    u32 pages = size >> 20;  /* number of 1MB pages */
    u32 ea_page = ea >> 20;
    u32 io_page = io >> 20;

    for (u32 i = 0; i < pages && (ea_page + i) < 65536 && (io_page + i) < 65536; i++) {
        s_io_address_table[ea_page + i] = (u16)(io_page + i);
        s_ea_address_table[io_page + i] = (u16)(ea_page + i);
    }
}

/* Clear offset table entries for an IO mapping */
static void clear_offset_table(u32 ea, u32 io, u32 size)
{
    u32 pages = size >> 20;
    u32 ea_page = ea >> 20;
    u32 io_page = io >> 20;

    for (u32 i = 0; i < pages && (ea_page + i) < 65536 && (io_page + i) < 65536; i++) {
        s_io_address_table[ea_page + i] = 0xFFFF;
        s_ea_address_table[io_page + i] = 0xFFFF;
    }
}

/* Find an IO mapping by EA */
static IoMapping* find_mapping_by_ea(u32 ea)
{
    for (int i = 0; i < CELL_GCM_MAX_IO_MAPPINGS; i++) {
        if (s_io_mappings[i].active && s_io_mappings[i].ea == ea)
            return &s_io_mappings[i];
    }
    return NULL;
}

/* Find an IO mapping by IO offset */
static IoMapping* find_mapping_by_io(u32 io)
{
    for (int i = 0; i < CELL_GCM_MAX_IO_MAPPINGS; i++) {
        if (s_io_mappings[i].active && s_io_mappings[i].io == io)
            return &s_io_mappings[i];
    }
    return NULL;
}

/* Find a free IO mapping slot */
static IoMapping* find_free_mapping(void)
{
    for (int i = 0; i < CELL_GCM_MAX_IO_MAPPINGS; i++) {
        if (!s_io_mappings[i].active)
            return &s_io_mappings[i];
    }
    return NULL;
}

/* Valid tiled pitch sizes (power-of-two aligned, common RSX values) */
static const u32 s_valid_pitches[] = {
    0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700, 0x0800,
    0x0A00, 0x0C00, 0x0D00, 0x0E00, 0x1000, 0x1400, 0x1800,
    0x1A00, 0x1C00, 0x2000, 0x2800, 0x3000, 0x3400, 0x3800,
    0x4000, 0x5000, 0x6000, 0x6800, 0x7000, 0x8000, 0xA000,
    0xC000, 0xD000, 0xE000, 0x10000
};
static const int s_valid_pitch_count = sizeof(s_valid_pitches) / sizeof(s_valid_pitches[0]);

/* ---------------------------------------------------------------------------
 * API implementations -- Initialization
 * -----------------------------------------------------------------------*/

/* NID: 0xB2E761D4 */
s32 cellGcmInit(u32 cmdSize, u32 ioSize, u32 ioAddress)
{
    printf("[cellGcmSys] Init(cmdSize=0x%X, ioSize=0x%X, ioAddr=0x%08X)\n",
           cmdSize, ioSize, ioAddress);

    if (s_gcm_initialized) {
        printf("[cellGcmSys] WARNING: already initialized\n");
        return CELL_GCM_ERROR_FAILURE;
    }

    memset(s_display_buffers, 0, sizeof(s_display_buffers));
    memset(s_display_buffer_set, 0, sizeof(s_display_buffer_set));
    memset(&s_config, 0, sizeof(s_config));
    memset(&s_control, 0, sizeof(s_control));
    vm_write32(GCM_CTRL_EA + 0, 0);   /* guest control register: put=get=ref=0 */
    vm_write32(GCM_CTRL_EA + 4, 0);
    vm_write32(GCM_CTRL_EA + 8, 0);
    memset(s_io_mappings, 0, sizeof(s_io_mappings));
    memset(s_tiles, 0, sizeof(s_tiles));
    memset(s_zcull, 0, sizeof(s_zcull));
    memset(s_report_data, 0, sizeof(s_report_data));
    memset(s_labels, 0, sizeof(s_labels));
    memset(s_io_address_table, 0xFF, sizeof(s_io_address_table));
    memset(s_ea_address_table, 0xFF, sizeof(s_ea_address_table));

    /*
     * Populate a plausible configuration.
     * In a real implementation these would reflect actual allocated memory.
     */
    s_config.localAddress    = 0xC0000000;  /* typical RSX local mem base */
    s_config.localSize       = 256 * 1024 * 1024;  /* 256 MB */
    s_config.ioAddress       = ioAddress;
    s_config.ioSize          = ioSize;
    s_config.memoryFrequency = 650000000;   /* 650 MHz */
    s_config.coreFrequency   = 500000000;   /* 500 MHz */

    s_local_mem_allocated = 0;
    s_io_mapping_count = 0;
    s_current_display_buffer_id = 0;
    s_flip_handler = NULL;
    s_vblank_handler = NULL;
    s_user_handler = NULL;
    s_second_v_handler = NULL;
    s_last_flip_time = get_timestamp_ns();
    s_flip_status = CELL_GCM_FLIP_STATUS_DONE;
    s_flip_mode = CELL_GCM_DISPLAY_VSYNC;
    s_debug_level = CELL_GCM_DEBUG_LEVEL0;
    s_vblank_count = 0;
    s_io_map_reserved = 0;
    s_default_fifo_mode = 0;
    s_graphics_handler = NULL;
    s_queue_handler = NULL;
    s_second_v_frequency = 0;
    s_vblank_frequency = 0;
    s_user_command = 0;

    /* Set up the initial IO mapping for the command buffer region */
    if (ioAddress != 0 && ioSize > 0) {
        populate_offset_table(ioAddress, 0, ioSize);

        s_io_mappings[0].ea     = ioAddress;
        s_io_mappings[0].io     = 0;
        s_io_mappings[0].size   = ioSize;
        s_io_mappings[0].active = 1;
        s_io_mapping_count = 1;
    }

    s_gcm_initialized = 1;
    return CELL_OK;
}

/* Reusable _cellGcmInitBody core (NID 0x15BAE46B). See cellGcmSys.h for why the
 * guest vm is injected as callbacks. The CellGcmContextData layout here is the
 * one proven in the shipping Simpsons port (begin@0/end@4/current@8/callback@C);
 * getting it wrong makes the game read `current` as the callback OPD and stall
 * in cellGcmFlush. The command buffer is the start of the game's IO region. */
u32 cellGcmSetupContext(u32 ctx_out_addr, u32 cmdSize, u32 ioSize, u32 ioAddress,
                        CellGcmGuestAlloc galloc, CellGcmGuestWrite32 gwrite32)
{
    if (cmdSize < 0x10000)
        cmdSize = 0x10000;
    {
        static int grow = -1;
        if (grow < 0)
            grow = ps3_gcm_grow_cmd_parse(getenv("PS3_GCM_GROW_CMD"));
        if (grow) {
            u32 grown = ps3_gcm_grow_cmd_size(cmdSize, ioSize);
            if (grown != cmdSize) {
                fprintf(stderr, "[cellGcmSys] grow cmdSize 0x%X -> 0x%X (io=0x%X)\n",
                        cmdSize, grown, ioSize);
                cmdSize = grown;
            }
        }
    }

    cellGcmInit(cmdSize, ioSize, ioAddress);

    u32 cmdbuf = ioAddress;                 /* FIFO lives at the IO region start */
    u32 cdata  = galloc ? galloc(16, 16) : 0;
    if (cdata && gwrite32) {
        gwrite32(cdata + 0x0, cmdbuf);              /* begin   */
        gwrite32(cdata + 0x4, cmdbuf + cmdSize);    /* end     */
        gwrite32(cdata + 0x8, cmdbuf);              /* current */
        gwrite32(cdata + 0xC, 0);                   /* callback OPD (bridge fills) */
        /* PS3_GCM_CB=1: instala mesmo a ponte que o comentario acima promete.
         *
         * MEDIDO 2026-08-08 (ledger E66): o campo +0xC era escrito UMA vez, com
         * 0, e nunca mais -- enquanto o +0x8 (current) levava 59 363 escritas.
         * Quando o buffer enche, o guest faz `(*(code*)**(int**)(ctx+0xC))(ctx,1)`
         * e, com 0 la', dereferencia memoria baixa: o `ctr` sai com lixo
         * ("/wad", 0x005F6D6F), os disjuntores ICALL saltam a chamada e devolvem
         * 0 = "cresceu com sucesso", o guest escreve para la' do fim e repete.
         * Sao os >= 8192 / >= 4096 ciclos do E64.
         *
         * O `cellGcmCallback` que ja' existe neste ficheiro nao serve para isto:
         * e' no-op (nao repoe o `current`) e nao esta' exportado por NID, logo o
         * guest nunca lhe chegaria.
         *
         * Aqui: um OPD sintetico logo a seguir aos 16 bytes do contexto, cujo
         * word de codigo e' um sentinela que o `ps3_indirect_call` reconhece e
         * trata host-side (repoe current=begin, devolve CELL_OK).
         *
         * Default ON (E385/E466): sem isto o callback fica 0, o FIFO
         * overflow dereferencia lixo e o open do WAD a seguir a intro
         * cai no FREELIST-TAG-GUARD (r3=0). PS3_GCM_CB=0 restaura o no-op. */
        {
            const char* e = getenv("PS3_GCM_CB");
            int on = !e || (*e && *e != '0');
            if (on) {
                gwrite32(cdata + 0x10, 0x0A0C0DE0u); /* code word = sentinela */
                gwrite32(cdata + 0x14, 0);           /* toc */
                gwrite32(cdata + 0xC,  cdata + 0x10);/* callback = &OPD */
                fprintf(stderr,
                        "[cellGcmSys] PS3_GCM_CB: callback OPD em 0x%08X "
                        "(sentinela 0x0A0C0DE0)\n", cdata + 0x10);
            }
        }
        if (ctx_out_addr)
            gwrite32(ctx_out_addr, cdata);          /* *context = &ctxdata */
    }
    return cdata;
}

/* ---------------------------------------------------------------------------
 * _cellGcmInitBody (NID 0x15BAE46B) — the function the SDK's cellGcmInit()
 * macro actually calls; imported by virtually every PS3 game (GoW2 included).
 * The generated NID DB mis-registered GCM init under 0xB477F06A/0xB16E457D --
 * NIDs no game imports -- so the real init never bound and GCM stayed
 * uninitialized (ioAddress=0, no FIFO). This is the correct binding: it bridges
 * to cellGcmSetupContext with vm-backed alloc/write so the default context and
 * FIFO land in real guest memory. gpr3=context_out, r4=cmdSize, r5=ioSize,
 * r6=ioAddress (PPC ABI marshalled by ps3_hle_call).
 * -----------------------------------------------------------------------*/
#define GCM_CTX_EA 0x0A050000u   /* fixed guest slot for the default context data */
static u32  gcm_ctx_galloc(u32 size, u32 align) { (void)size; (void)align; return GCM_CTX_EA; }
static void gcm_ctx_write32(u32 addr, u32 value) { vm_write32(addr, value); }

s32 _cellGcmInitBody(u32 ctx_out, u32 cmdSize, u32 ioSize, u32 ioAddress)
{
    fprintf(stderr, "[cellGcmSys] _cellGcmInitBody(ctxOut=0x%08X cmd=0x%X io=0x%X ioAddr=0x%08X)\n",
            ctx_out, cmdSize, ioSize, ioAddress);
    cellGcmSetupContext(ctx_out, cmdSize, ioSize, ioAddress, gcm_ctx_galloc, gcm_ctx_write32);
    return CELL_OK;
}

s32 cellGcmGetConfiguration(CellGcmConfig* config)
{
    if (!config)
        return CELL_GCM_ERROR_INVALID_VALUE;

    /* config is a GUEST effective address: write each field through vm_base with
     * big-endian swap (vm_write32). A raw `*config = s_config` derefs the guest
     * EA as a host pointer -> crash (write fault at the config EA), and even if
     * mapped it would store host-endian (LE) where the title reads BE. */
    uint32_t g = (uint32_t)(uintptr_t)config;
    vm_write32(g + 0x00, s_config.localAddress);
    vm_write32(g + 0x04, s_config.ioAddress);
    vm_write32(g + 0x08, s_config.localSize);
    vm_write32(g + 0x0C, s_config.ioSize);
    vm_write32(g + 0x10, s_config.memoryFrequency);
    vm_write32(g + 0x14, s_config.coreFrequency);
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Command buffer control
 * -----------------------------------------------------------------------*/

/* NID: 0x8572A8E0 */
/* ---------------------------------------------------------------------------
 * Minimal RSX FIFO processor (PS3_RSX_FIFO). Now that GCM init gives a real
 * command buffer (s_config.ioAddress), parse the NV40/RSX FIFO from get..put
 * and execute the SYNC-relevant methods so the title's GPU waits complete:
 *   - SET_REFERENCE        -> control-register ref
 *   - SEMAPHORE_OFFSET/REL -> write the release value to the semaphore word
 *   - label writes         -> same, to the label word
 * Render-state methods are decoded and skipped (no rasterization). Jump/call/
 * return are followed within the buffer. Then get is advanced to put.
 * -----------------------------------------------------------------------*/
static unsigned int s_rsx_sema_off = 0;   /* last SEMAPHORE_OFFSET (IO offset) */

/* ---------------------------------------------------------------------------
 * RSX renderer bridge (PS3_RSX_BACKEND).
 *
 * The real command processor (rsx_commands.c) + host backends
 * (rsx_d3d12_backend.c / rsx_null_backend.c) + shader decompilers existed but
 * were never wired to the live GCM path -- only exercised by libs/video/tests.
 * This bridge feeds every method the proven FIFO walker decodes (which already
 * navigates jumps/calls/returns and the SYNC methods correctly on real titles)
 * into rsx_process_method(), so state tracking + clear/draw dispatch reach the
 * registered backend. Opt-in via the PS3_RSX_BACKEND env var; unset = the old
 * sync-only behavior, fully non-breaking:
 *   trace -> lightweight logging backend, NO window, NO guest-memory reads
 *            (safe inside a fragile boot; answers "does this boot emit draws?")
 *   null  -> Win32 window + GDI clear color (rsx_null_backend.c)
 *   d3d12 -> real D3D12 GPU backend (rsx_d3d12_backend.c)
 * -----------------------------------------------------------------------*/
extern int rsx_null_backend_init(unsigned int w, unsigned int h, const char* title);
extern int rsx_null_backend_pump_messages(void);
extern int rsx_d3d12_backend_init(unsigned int w, unsigned int h, const char* title);
extern int rsx_d3d12_backend_pump_messages(void);
#ifdef __APPLE__
extern int rsx_metal_backend_init(unsigned int w, unsigned int h, const char* title);
extern int rsx_metal_backend_pump_messages(void);
#endif

static rsx_state s_rsx_state;
static int       s_rsx_state_inited = 0;
static int       s_rsx_in_frame     = 0;
/* -1 unresolved, 0 off, 1 trace, 2 null/sdl, 3 d3d12, 4 metal (Apple) */
static int       s_rsx_mode         = -1;

static unsigned s_trace_clears, s_trace_da, s_trace_di, s_trace_sh;
static void trace_clear(void* u, u32 flags, u32 color, float depth, u8 stencil) {
    (void)u; (void)depth; (void)stencil;
    if (s_trace_clears < 40)
        fprintf(stderr, "[rsx-bridge] CLEAR flags=0x%X color=0x%08X\n", flags, color);
    s_trace_clears++;
}
static void trace_draw_arrays(void* u, u32 prim, u32 first, u32 count) {
    (void)u;
    if (s_trace_da < 80)
        fprintf(stderr, "[rsx-bridge] DRAW_ARRAYS prim=%u first=%u count=%u\n", prim, first, count);
    s_trace_da++;
}
static void trace_draw_indexed(void* u, u32 prim, u32 off, u32 count) {
    (void)u;
    if (s_trace_di < 80)
        fprintf(stderr, "[rsx-bridge] DRAW_INDEXED prim=%u off=%u count=%u\n", prim, off, count);
    s_trace_di++;
}
static void trace_set_shader(void* u, const rsx_state* st) {
    (void)u;
    if (s_trace_sh < 40)
        fprintf(stderr, "[rsx-bridge] SET_SHADER fp=0x%08X vp_load=%u out_mask=0x%X\n",
                st->fragment_program_addr, st->transform_program_load,
                st->vertex_attrib_output_mask);
    s_trace_sh++;
}
static rsx_backend s_trace_backend = {
    .set_shader   = trace_set_shader,
    .clear        = trace_clear,
    .draw_arrays  = trace_draw_arrays,
    .draw_indexed = trace_draw_indexed,
};

/* Resolve the selected backend once; returns the mode (0 = off).
 * On macOS, boot_macos often already called rsx_metal_backend_init (or null);
 * do NOT replace a live backend with the trace stub — that was killing the
 * movie overlay (audio OK, black window). */
static int rsx_bridge_mode(void) {
    if (s_rsx_mode >= 0) return s_rsx_mode;
    const char* e = getenv("PS3_RSX_BACKEND");
    if      (!e || !*e)            { s_rsx_mode = 0; return 0; }
    else if (!strcmp(e, "trace"))  s_rsx_mode = 1;
    else if (!strcmp(e, "null") || !strcmp(e, "sdl")) s_rsx_mode = 2;
    else if (!strcmp(e, "d3d12"))  s_rsx_mode = 3;
#ifdef __APPLE__
    else if (!strcmp(e, "metal"))  s_rsx_mode = 4;
#endif
    else                           s_rsx_mode = 1; /* unknown -> safe trace */

    rsx_state_init(&s_rsx_state);
    s_rsx_state_inited = 1;

    /* If the host boot already registered a backend (boot_macos), keep it. */
    if (rsx_get_backend() != NULL && s_rsx_mode >= 2) {
        fprintf(stderr, "[rsx-bridge] mode=%d keeping pre-registered backend\n", s_rsx_mode);
        return s_rsx_mode;
    }

    if (s_rsx_mode == 1) {
        rsx_set_backend(&s_trace_backend);
        fprintf(stderr, "[rsx-bridge] trace backend active (no window)\n");
    } else if (s_rsx_mode == 2) {
        if (rsx_null_backend_init(1280, 720, "ps3recomp (null)") != 0) {
            fprintf(stderr, "[rsx-bridge] null init FAILED -> trace\n");
            rsx_set_backend(&s_trace_backend); s_rsx_mode = 1;
        }
    } else if (s_rsx_mode == 3) {
        /* Windows host startup for the runtime overlay: settings (window
         * size, VSync, pad mapping) must be loaded before the backend reads
         * them. The launcher may hand over PS3_OVERLAY_SETTINGS; otherwise the
         * per-user default path is used. A host that already initialized the
         * overlay makes this a no-op. Without a D3D12 window nothing draws the
         * menu, so the overlay is shut down again and Start/pad input reaches
         * the game exactly as before. */
#ifdef _WIN32
        (void)rsx_overlay_init(rsx_overlay_settings_path_from_env());
        {   /* Strip / quit dialog name the game, not the "Jogo" placeholder:
             * TITLE from the game's PARAM.SFO (PS3_GAME_ROOT or PS3_VFS_ROOT
             * minus USRDIR), the same source the Metal window title uses. */
            char title[64];
            if (gid_read_title(title, sizeof title) == 0)
                rsx_overlay_set_title(title);
        }
#endif
        if (rsx_d3d12_backend_init(1280, 720, "ps3recomp (D3D12)") != 0) {
            fprintf(stderr, "[rsx-bridge] d3d12 init FAILED -> trace\n");
#ifdef _WIN32
            rsx_overlay_shutdown();
#endif
            rsx_set_backend(&s_trace_backend); s_rsx_mode = 1;
        }
#ifdef __APPLE__
    } else if (s_rsx_mode == 4) {
        if (rsx_metal_backend_init(1280, 720, "ps3recomp (Metal)") != 0) {
            fprintf(stderr, "[rsx-bridge] metal init FAILED -> trace\n");
            rsx_set_backend(&s_trace_backend); s_rsx_mode = 1;
        } else {
            fprintf(stderr, "[rsx-bridge] metal backend active\n");
        }
#endif
    }
    return s_rsx_mode;
}

/* PS3_RSX_STATS histogram (see rsx_process_fifo) */
static unsigned long long g_rsx_method_hist[0x1000];

/* ---------------------------------------------------------------------------
 * RSX 2D engine: NV3062 destination surface, NV3089 scaled image, NV308A
 * inline image, NV309E swizzled surface -- ported from upstream sp00nznet
 * (libs/video/cellGcmSys.c gcm_2d_method / nv3089_blit). These objects sit on
 * subchannels 1-7 with their own method numbers; the walker used to drop the
 * subchannel and feed them to the 3D processor, so NV3089 blit rectangles were
 * written into BLEND_ENABLE/FUNC (0x310-0x31C) and no blit ever ran (E441).
 * -----------------------------------------------------------------------*/
int gcm_io_offset_to_ea(u32 ioOffset, u32* out_ea);

/* Command-buffer wrap recorded by ps3_gcm_ctx_overflow: when the walker's get
 * reaches s_fifo_wrap_at it continues at s_fifo_wrap_to (the console's JUMP to
 * begin, without writing past a buffer that has no slot reserved for it). */
static unsigned int s_fifo_wrap_at, s_fifo_wrap_to;
static int s_fifo_wrap_pending;
/* The wrap queued behind the pending one (see ps3_gcm_ctx_overflow). */
static unsigned int s_fifo_wrap_next_at, s_fifo_wrap_next_to;
static int s_fifo_wrap_next_pending;

static u32 gcm2d_resolve(int local, u32 offset)
{
    if (local) return 0xC0000000u + offset;
    u32 ea = 0;
    if (gcm_io_offset_to_ea(offset, &ea)) return ea;
    return offset;
}

extern int ppu_guest_range_committed(uint32_t addr, uint32_t n);

/* [start, end) inside the 32-bit guest space and committed. The CPU 2D paths
 * take every size, pitch and step from the command stream; a desynced stream
 * must not reach vm_base + anything. */
static int gcm2d_range_ok(u64 start, u64 end)
{
    if (end <= start || end > 0x100000000ull) return 0;
    return ppu_guest_range_committed((u32)start, (u32)(end - start));
}

static int gcm2d_trace(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_TRACE_GCM2D"); on = (e && *e && *e != '0') ? 1 : 0; }
    return on;
}

/* CPU-side 2D writes into guest memory (NV308A inline words, the NV3089 copy)
 * are OFF by default: E442 showed the walker reading 3D payloads (VP microcode,
 * float constants) as headers on subchannels 1/4/6/7, and a desynced stream fed
 * to these paths writes arbitrary guest addresses. PS3_GCM2D_CPU=1 enables. */
static int gcm2d_cpu_writes(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_GCM2D_CPU"); on = (e && *e && *e != '0') ? 1 : 0; }
    return on;
}

static struct {
    u32 dst_dma;      /* NV3062 0x0184/0x0188 context DMA            */
    u32 color_fmt;    /* NV3062 0x0300 SET_COLOR_FORMAT             */
    u32 pitch;        /* NV3062 0x0304 SET_PITCH (dst in bits 16-31) */
    u32 dst_offset;   /* NV3062 0x030C SET_OFFSET_DESTIN            */
    u32 point;        /* NV308A 0x0304 SET_POINT ((y<<16)|x)        */
    u32 size_out;     /* NV308A 0x0308 SET_SIZE_OUT                 */
} s_gcm2d;

static struct {
    u32 src_dma, dst_surf, fmt, op, clip_pt, clip_sz, out_pt, out_sz;
    u32 ds_dx, dt_dy, in_sz, in_fmt, in_off, in_uv;
} s_nv3089;

static struct {
    u32 fmt;        /* 0x0300: (log2h << 24) | (log2w << 16) | format */
    u32 offset;     /* 0x0304: destination offset                     */
    int active;
    int subch;      /* NV309E and NV308A share SET_POINT/SET_OFFSET 0x0304 */
} s_nv309e;

/* Morton/Z-order offset in an NV309E swizzled surface. */
static u32 nv309e_swz(u32 x, u32 y, u32 l2w, u32 l2h)
{
    u32 n = l2w < l2h ? l2w : l2h, off = 0;
    for (u32 i = 0; i < n; i++)
        off |= (((x >> i) & 1u) << (2 * i)) | (((y >> i) & 1u) << (2 * i + 1));
    if (l2w > l2h)      off |= (x >> n) << (2 * n);
    else if (l2h > l2w) off |= (y >> n) << (2 * n);
    return off;
}

/* One NV3089 blit into the bound NV3062 destination (or NV309E swizzled
 * texture). A backend that keeps render targets on the GPU gets it first. */
static void nv3089_blit(void)
{
    u32 out_w = s_nv3089.out_sz & 0xFFFF, out_h = s_nv3089.out_sz >> 16;
    u32 out_x = s_nv3089.out_pt & 0xFFFF, out_y = s_nv3089.out_pt >> 16;
    u32 in_pitch = s_nv3089.in_fmt & 0xFFFF;
    u32 dst_pitch = s_gcm2d.pitch >> 16;
    if (!out_w || !out_h || !in_pitch) return;
    if (out_w > 4096u || out_h > 4096u) return;
    u32 f = s_nv3089.fmt & 0xFF;
    const int src_local = (s_nv3089.src_dma != 0xFEED0001u);
    const int dst_local = (s_gcm2d.dst_dma  != 0xFEED0001u);
    u32 l2w = (s_nv309e.fmt >> 16) & 0xFF, l2h = (s_nv309e.fmt >> 24) & 0xFF;
    const int to_swizzled = (f == 3 && s_nv309e.active && s_nv309e.offset &&
                             l2w <= 12u && l2h <= 12u &&
                             (1u << l2w) == out_w && (1u << l2h) == out_h);

    if (!to_swizzled) {
        rsx_backend* b = rsx_get_backend();
        if (b && b->blit_scaled) {
            rsx_blit_desc d;
            memset(&d, 0, sizeof(d));
            d.src_local = src_local;  d.dst_local = dst_local;
            d.src_offset = s_nv3089.in_off;  d.src_pitch = in_pitch;  d.src_fmt = f;
            d.in_w = s_nv3089.in_sz & 0xFFFF;  d.in_h = s_nv3089.in_sz >> 16;
            d.u0 = s_nv3089.in_uv & 0xFFFF;  d.v0 = s_nv3089.in_uv >> 16;
            d.ds_dx = s_nv3089.ds_dx;  d.dt_dy = s_nv3089.dt_dy;
            d.dst_offset = s_gcm2d.dst_offset;  d.dst_pitch = dst_pitch;
            d.dst_fmt = s_gcm2d.color_fmt;
            d.out_x = out_x;  d.out_y = out_y;  d.out_w = out_w;  d.out_h = out_h;
            int done = b->blit_scaled(b->userdata, &d);
            if (gcm2d_trace()) {
                static int n = 0;
                if (n++ < 48)
                    printf("[GCM2D] blit %ux%u fmt=0x%X src=0x%08X(%s pitch %u uv %u,%u d %08X,%08X) "
                           "-> dst=0x%08X(%s pitch %u) at %u,%u : %s\n",
                           out_w, out_h, f, s_nv3089.in_off, src_local ? "L" : "M", in_pitch,
                           d.u0, d.v0, d.ds_dx, d.dt_dy, s_gcm2d.dst_offset,
                           dst_local ? "L" : "M", dst_pitch, out_x, out_y,
                           done ? "GPU" : "cpu");
            }
            if (done) return;
        }
    }
    if (!dst_pitch && !to_swizzled) return;
    if (!gcm2d_cpu_writes()) return;

    if (to_swizzled) {
        u32 src2 = gcm2d_resolve(src_local, s_nv3089.in_off);
        u32 dst2 = gcm2d_resolve(1, s_nv309e.offset);
        u32 u0b = s_nv3089.in_uv & 0xFFFF, v0b = s_nv3089.in_uv >> 16;
        {   /* the whole source footprint and the swizzled destination */
            u64 max_sv = ((u64)v0b << 8) + (u64)(out_h - 1u) * s_nv3089.dt_dy;
            u64 max_su = ((u64)u0b << 8) + (u64)(out_w - 1u) * s_nv3089.ds_dx;
            u64 s_end = (u64)src2 + (max_sv >> 20) * in_pitch + (max_su >> 20) * 4u + 4u;
            u64 d_end = (u64)dst2 + (u64)out_w * out_h * 4u;
            if (!gcm2d_range_ok(src2, s_end) || !gcm2d_range_ok(dst2, d_end)) return;
        }
        extern u8* vm_base;
        const u8* sbase = vm_base + src2;
        u8* dbase = vm_base + dst2;
        for (u32 y = 0; y < out_h; y++) {
            u64 sv = ((u64)v0b << 8) + (u64)y * s_nv3089.dt_dy;
            const u8* srow = sbase + (u32)(sv >> 20) * in_pitch;
            for (u32 x = 0; x < out_w; x++) {
                u64 su = ((u64)u0b << 8) + (u64)x * s_nv3089.ds_dx;
                memcpy(dbase + (u64)nv309e_swz(x, y, l2w, l2h) * 4,
                       srow + (u32)(su >> 20) * 4, 4);
            }
        }
        return;
    }
    /* 32-bit colour formats only (3/7/8/0xD); anything else needs a per-format
     * converter and is skipped rather than written wrong. */
    if (f != 3 && f != 7 && f != 8 && f != 0xD) {
        static int w = 0;
        if (w++ < 4) printf("[GCM2D] NV3089 colour format 0x%X unsupported, blit skipped\n", f);
        return;
    }
    u32 src = gcm2d_resolve(src_local, s_nv3089.in_off);
    u32 dst = gcm2d_resolve(dst_local, s_gcm2d.dst_offset);
    u32 u0 = s_nv3089.in_uv & 0xFFFF, v0 = s_nv3089.in_uv >> 16;   /* 12.4 */
    {   /* the whole source footprint and destination rectangle */
        u64 max_sv = ((u64)v0 << 8) + (u64)(out_h - 1u) * s_nv3089.dt_dy;
        u64 max_su = ((u64)u0 << 8) + (u64)(out_w - 1u) * s_nv3089.ds_dx;
        u64 s_end = (u64)src + (max_sv >> 20) * in_pitch + (max_su >> 20) * 4u + 4u;
        u64 d_start = (u64)dst + (u64)out_y * dst_pitch + (u64)out_x * 4u;
        u64 d_end = (u64)dst + (u64)(out_y + out_h - 1u) * dst_pitch + (u64)(out_x + out_w) * 4u;
        if (!gcm2d_range_ok(src, s_end) || !gcm2d_range_ok(d_start, d_end)) return;
    }
    for (u32 y = 0; y < out_h; y++) {
        u64 sv = ((u64)v0 << 8) + (u64)y * s_nv3089.dt_dy;         /* 20.12 */
        u32 sy = (u32)(sv >> 20);
        for (u32 x = 0; x < out_w; x++) {
            u64 su = ((u64)u0 << 8) + (u64)x * s_nv3089.ds_dx;
            u32 sx = (u32)(su >> 20);
            vm_write32(dst + (out_y + y) * dst_pitch + (out_x + x) * 4,
                       vm_read32(src + sy * in_pitch + sx * 4));
        }
    }
}

static void gcm_2d_method(u32 subch, u32 method, u32 data)
{
    if (method < 0x0100u) return;   /* channel methods: the walker's own switch */
    if (gcm2d_trace()) {
        static u8 seen[8][2048];
        u32 mi = (method >> 2) & 2047u;
        if (subch < 8 && !seen[subch][mi]) {
            seen[subch][mi] = 1;
            printf("[GCM2D] first subch=%u method=0x%04X data=0x%08X\n", subch, method, data);
        }
    }
    if (subch == 2 || subch == 3) {          /* NV3062 context surface 2D */
        switch (method) {
        case 0x0184: case 0x0188: s_gcm2d.dst_dma = data; return;
        case 0x0300: s_gcm2d.color_fmt  = data; return;
        case 0x0304: s_gcm2d.pitch      = data; return;
        case 0x030C: s_gcm2d.dst_offset = data; return;
        }
        return;
    }
    if (subch == 4 || subch == 5) {          /* NV308A image-from-CPU, or NV309E */
        if (method == 0x0300) {              /* NV309E only: SET_FORMAT */
            s_nv309e.fmt = data; s_nv309e.active = 1; s_nv309e.subch = (int)subch;
            return;
        }
        if (method == 0x0304 && s_nv309e.active && s_nv309e.subch == (int)subch) {
            s_nv309e.offset = data;
            return;
        }
        switch (method) {
        case 0x0304: s_gcm2d.point    = data; return;
        case 0x0308: s_gcm2d.size_out = data; return;
        case 0x030C: return;                 /* SIZE_IN */
        }
        if (method >= 0x0400 && method <= 0x07FC && gcm2d_cpu_writes()) {
            /* Inline COLOR word (Y32, one row per point.y) into the destination. */
            u32 idx = (method - 0x0400) >> 2;
            u32 px = (s_gcm2d.point & 0xFFFF) + idx, py = s_gcm2d.point >> 16;
            u32 dst_pitch = s_gcm2d.pitch >> 16;
            u32 base = gcm2d_resolve(s_gcm2d.dst_dma != 0xFEED0001u, s_gcm2d.dst_offset);
            u64 a = (u64)base + (u64)py * dst_pitch + (u64)px * 4u;
            if (gcm2d_range_ok(a, a + 4u)) vm_write32((u32)a, data);
        }
        return;
    }
    if (subch == 6 || subch == 7) {          /* NV3089 scaled image from memory */
        switch (method) {
        case 0x0184: s_nv3089.src_dma  = data; return;
        case 0x0198: s_nv3089.dst_surf = data; return;
        case 0x0300: s_nv3089.fmt      = data; return;
        case 0x0304: s_nv3089.op       = data; return;
        case 0x0308: s_nv3089.clip_pt  = data; return;
        case 0x030C: s_nv3089.clip_sz  = data; return;
        case 0x0310: s_nv3089.out_pt   = data; return;
        case 0x0314: s_nv3089.out_sz   = data; return;
        case 0x0318: s_nv3089.ds_dx    = data; return;
        case 0x031C: s_nv3089.dt_dy    = data; return;
        case 0x0400: s_nv3089.in_sz    = data; return;
        case 0x0404: s_nv3089.in_fmt   = data; return;
        case 0x0408: s_nv3089.in_off   = data; return;
        case 0x040C: s_nv3089.in_uv    = data; nv3089_blit(); return;   /* trigger */
        }
        return;
    }
    /* subchannel 1 (NV0039 memory-to-memory) and anything else: not yet. */
}

/* Flip at its place in the command stream (RPCS3 gcmSetPrepareFlip / rsx_methods
 * flip_command): on the console the display switches when the RSX reaches the flip,
 * after the commands that compose that buffer. Presenting at the PPU call showed a
 * buffer whose composition was still unflushed -- 2 flips old, sometimes garbage
 * (2026-09-16 gameplay shots). _cellGcmSetFlipCommand records the stream offset of
 * context->current (no guest write: writing GCM_FLIP_COMMAND there was overwritten
 * by the title's own current once host timing changed, and presents stopped) and the
 * FIFO walker presents when get reaches it. A new flip while one is still pending
 * presents the old one right away, so presents never stall. The flip status, handler
 * and vblank stay at the call. PS3_GCM_FLIP_IN_STREAM=0 = old immediate present. */
static int gcm_flip_in_stream_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_GCM_FLIP_IN_STREAM"); on = (e && e[0] == '0') ? 0 : 1; }
    return on;
}
static int gcm_fifo_on(void)
{
    static int on = -1;
    if (on < 0) on = getenv("PS3_RSX_FIFO") ? 1 : 0;
    return on;
}

/* VSYNC flip mode (CELL_GCM_DISPLAY_VSYNC, what GoW2 sets): the console executes a flip
 * at a 60 Hz vblank, so a title's frame loop never runs faster than that -- GoW2's flip
 * handler posts a semaphore (2 frames in flight) its render loop waits on. This HLE
 * completes the flip at the call, so without a limit the loop ran as fast as the host
 * could present (120 Hz display, 12k+ flips/s in loading loops; 2026-09-16 trace). The
 * flip call therefore waits for the next vblank of a 60 Hz grid, giant lock released like
 * any blocking call; a title slower than 60 Hz never waits (the grid restarts).
 * PS3_GCM_VSYNC=0 = no limit (old behavior). */
static int gcm_vsync_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_GCM_VSYNC"); on = (e && e[0] == '0') ? 0 : 1; }
    return on;
}
/* ---- winport v59-async-rsx (engine_fixes_v59_async_rsx.py, research/A1_async_rsx_design.md) ----
 * WINPORT_ASYNC_RSX=1: the "rsx-async" thread walks the FIFO and drives the renderer WITHOUT the PPU giant lock;
 * GCM calls only ring a bell. wp59_rsx_lock (SRW) guards walker + D3D12. Default 0 = old paths. */
extern unsigned char* vm_base;
static int wp59_async_on(void)
{
#ifdef _WIN32
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("WINPORT_ASYNC_RSX"); on = (e && e[0] == '1') ? 1 : 0;
        if (on) {
            const char* a = getenv("WINPORT_ASYNC_RSX_ACQ"); const char* am = getenv("WINPORT_ASYNC_RSX_ACQ_MS");
            const char* sf = getenv("WINPORT_ASYNC_RSX_SYNCFLIP"); const char* pu = getenv("WINPORT_ASYNC_RSX_POLL_US");
            fprintf(stderr, "[ASYNC59] async RSX walker ON (acq=%s acq_ms=%s syncflip=%s poll_us=%s)\n",
                    a ? a : "1", am ? am : "5", sf ? sf : "0", pu ? pu : "200");
            fflush(stderr);
        }
    }
    return on;
#else
    return 0;
#endif
}
int wp59_async_rsx_on(void) { return wp59_async_on(); }
static __thread int t_wp59_rsx = 0;            /* 1 on the rsx-async thread */
static volatile long long wp59_gwait_ns, wp59_gwaits, wp59_lockwait_ns, wp59_locks_other, wp59_owner_bad;
static volatile long long wp59_pres_rsx, wp59_pres_other, wp59_acq_parks, wp59_acq_to, wp59_acq_ok;
static volatile long wp59_flips_presented;
/* get / ref / label: atomic big-endian release store in async mode (another thread polls them; no reservation
 * scan from a non-guest thread). Old path: vm_write32. */
static void wp59_store_be32(u32 ea, u32 v)
{
    if (wp59_async_on() && vm_base) {
        __atomic_store_n((u32*)(void*)(vm_base + ea), __builtin_bswap32(v), __ATOMIC_RELEASE);
        return;
    }
    vm_write32(ea, v);
}
#ifdef _WIN32
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
static SRWLOCK wp59_lock = SRWLOCK_INIT;
static volatile unsigned long wp59_lock_owner = 0;
static HANDLE wp59_bell_ev = NULL, wp59_prog_ev = NULL;
static volatile long wp59_started = 0;
static void wp59_start_once(void);
/* 1 = taken here (pass to wp59_rsx_unlock); 0 = the caller already holds it (no recursion on an SRW lock). */
int wp59_rsx_lock(void)
{
    const unsigned long me = (unsigned long)GetCurrentThreadId();
    if (__atomic_load_n(&wp59_lock_owner, __ATOMIC_ACQUIRE) == me) return 0;
    if (!t_wp59_rsx) {
        const u64 t0 = get_timestamp_ns();
        AcquireSRWLockExclusive(&wp59_lock);
        __atomic_add_fetch(&wp59_lockwait_ns, (long long)(get_timestamp_ns() - t0), __ATOMIC_RELAXED);
        __atomic_add_fetch(&wp59_locks_other, 1ll, __ATOMIC_RELAXED);
    } else {
        AcquireSRWLockExclusive(&wp59_lock);
    }
    __atomic_store_n(&wp59_lock_owner, me, __ATOMIC_RELEASE);
    return 1;
}
void wp59_rsx_unlock(int taken)
{
    if (!taken) return;
    __atomic_store_n(&wp59_lock_owner, 0ul, __ATOMIC_RELEASE);
    ReleaseSRWLockExclusive(&wp59_lock);
}
/* Renderer calls this at every D3D12 frame entry: in async mode the caller must hold the RSX lock. */
void wp59_d3d_check(const char* where)
{
    if (!wp59_async_on()) return;
    if (__atomic_load_n(&wp59_lock_owner, __ATOMIC_ACQUIRE) != (unsigned long)GetCurrentThreadId()) {
        const long long n = __atomic_add_fetch(&wp59_owner_bad, 1ll, __ATOMIC_RELAXED);
        if (n <= 10) fprintf(stderr, "[ASYNC59] D3D12 entry %s without the RSX lock (tid %lu)\n",
                             where ? where : "?", (unsigned long)GetCurrentThreadId());
    }
}
/* Wait for `ev` or `ns`, whichever first; sub-ms through a per-thread high-resolution waitable timer. */
static void wp59_wait2(HANDLE ev, u64 ns)
{
    static __thread HANDLE tm = NULL; static __thread int bad = 0;
    if (!tm && !bad) {
        tm = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!tm) bad = 1;
    }
    if (tm) {
        LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(ns / 100ull ? ns / 100ull : 1ull);
        if (SetWaitableTimer(tm, &due, 0, NULL, NULL, FALSE)) {
            HANDLE hs[2]; DWORD n = 0;
            if (ev) hs[n++] = ev;
            hs[n++] = tm;
            WaitForMultipleObjects(n, hs, FALSE, INFINITE);
            return;
        }
    }
    if (ev) WaitForSingleObject(ev, (DWORD)((ns + 999999ull) / 1000000ull)); else Sleep((DWORD)((ns + 999999ull) / 1000000ull));
}
static void wp59_bell(void)
{
    wp59_start_once();
    if (wp59_bell_ev) SetEvent(wp59_bell_ev);
}
/* Guest side of the overflow / flip waits: ring, then wait for the RSX thread to report progress. */
static void wp59_wait_progress(void)
{
    const u64 t0 = get_timestamp_ns();
    static u64 ns = 0;
    if (!ns) { ns = ps3_gcm_wait_ns_parse(getenv("PS3_GCM_WAIT_NS")); if (!ns || ns > 2000000ull) ns = 500000ull; }
    wp59_bell();
    wp59_wait2(wp59_prog_ev, ns);
    __atomic_add_fetch(&wp59_gwait_ns, (long long)(get_timestamp_ns() - t0), __ATOMIC_RELAXED);
    __atomic_add_fetch(&wp59_gwaits, 1ll, __ATOMIC_RELAXED);
}
#else
int wp59_rsx_lock(void) { return 0; }
void wp59_rsx_unlock(int taken) { (void)taken; }
void wp59_d3d_check(const char* where) { (void)where; }
static void wp59_bell(void) { }
static void wp59_wait_progress(void) { }
static void wp59_start_once(void) { }
#endif
/* ---- end winport v59-async-rsx block A ---- */
/* ---- winport v60-async-seg (engine_fixes_v60_async_seg.py): segmented command-buffer flow control ---- */
volatile u32 wp60_ovf_words = 0;              /* r4 of the overflow callback (words requested), set by ppu_loader */
static volatile long wp60_lg = 0, wp60_lr = 0; /* wraps registered by the guest / taken by the walker */
static volatile u32 wp60_lap_end[4];           /* ring offset where lap (n & 3) ended */
static u32 wp60_ring_end = 0;                  /* real ring end (EA), from ctx->end before v60 first changes it */
static volatile long long wp60_segs, wp60_seg_wait_ns, wp60_wraps, wp60_stalls;
static u32 wp60_seg_bytes(void)
{
    static long v = -1;
    if (v < 0) {
        const char* e = getenv("WINPORT_ASYNC_SEG_KB"); long k = (e && *e) ? strtol(e, NULL, 0) : 0;
        if (k < 0 || k > 65536) k = 0;
        v = k * 1024l;
        if (v && wp59_async_on()) { fprintf(stderr, "[ASYNC60] segmented flow control ON: %ld KB segments\n", k); fflush(stderr); }
    }
    return (u32)v;
}
static int wp60_seg_on(void) { return wp59_async_on() && wp60_seg_bytes() != 0u; }
/* ---- end winport v60-async-seg vars ---- */
/* ---- winport v64-defer-vtx (engine_fixes_v64_defer_vtx.py): label/ref writes wait for deferred vertex conversion ---- */
static int (*volatile wp64_pend_fn)(void) = 0;   /* renderer: 1 while deferred conversions are outstanding */
static void (*volatile wp64_join_fn)(void) = 0;  /* renderer: finish them all (caller holds the RSX lock) */
void wp64_register(int (*pend)(void), void (*join)(void))
{
    wp64_join_fn = join;
    __atomic_store_n(&wp64_pend_fn, pend, __ATOMIC_RELEASE);
    fprintf(stderr, "[DEFER64] engine: label/ref writes wait for deferred vertex conversion\n"); fflush(stderr);
}
#define WP64_QCAP 4096u
static struct { u32 ea, v; } wp64_q[WP64_QCAP];
static u32 wp64_qn = 0;
static u64 wp64_q_t0 = 0;
static volatile long long wp64_queued, wp64_direct, wp64_syncs[5];   /* sync reasons: walk end, acquire, age, full, wrap */
static void wp64_flush_q(void)
{
    for (u32 i = 0; i < wp64_qn; i++) wp59_store_be32(wp64_q[i].ea, wp64_q[i].v);
    wp64_qn = 0;
}
static void wp64_sync(int why)
{
    int (*pend)(void) = __atomic_load_n(&wp64_pend_fn, __ATOMIC_ACQUIRE);
    if (!pend) return;
    const int p = pend();
    if (!p && !wp64_qn) return;
    if (p && wp64_join_fn) wp64_join_fn();
    if (wp64_qn) wp64_flush_q();
    __atomic_add_fetch(&wp64_syncs[why], 1ll, __ATOMIC_RELAXED);
}
static u64 wp64_age_ns(void)
{
    static u64 v = 0;
    if (!v) { const char* e = getenv("WINPORT_DEFER_VTX_LABEL_US"); long x = (e && *e) ? strtol(e, NULL, 0) : 1000;
              v = (u64)((x >= 10 && x <= 100000) ? x : 1000) * 1000ull; }
    return v;
}
static void wp64_label_store(u32 ea, u32 v)
{
    int (*pend)(void) = __atomic_load_n(&wp64_pend_fn, __ATOMIC_ACQUIRE);
    if (!pend) { wp59_store_be32(ea, v); return; }
    if (!pend()) {                                  /* nothing deferred: keep the order, write now */
        if (wp64_qn) wp64_flush_q();
        wp59_store_be32(ea, v);
        __atomic_add_fetch(&wp64_direct, 1ll, __ATOMIC_RELAXED);
        return;
    }
    if (wp64_qn == 0) wp64_q_t0 = get_timestamp_ns();
    else if (wp64_qn >= WP64_QCAP || get_timestamp_ns() - wp64_q_t0 > wp64_age_ns()) {
        wp64_sync(wp64_qn >= WP64_QCAP ? 3 : 2);
        wp59_store_be32(ea, v);
        __atomic_add_fetch(&wp64_direct, 1ll, __ATOMIC_RELAXED);
        return;
    }
    wp64_q[wp64_qn].ea = ea; wp64_q[wp64_qn].v = v; wp64_qn++;
    __atomic_add_fetch(&wp64_queued, 1ll, __ATOMIC_RELAXED);
}
static void wp64_report(double s)
{
    if (!__atomic_load_n(&wp64_pend_fn, __ATOMIC_ACQUIRE)) return;
    if (s <= 0.0) s = 1.0;
    if (!wp_stats_on()) ; else fprintf(stderr, "[DEFER64L] labels queued=%.0f/s direct=%.0f/s | syncs walk_end=%.0f acquire=%.0f age=%.0f full=%.0f wrap=%.0f (/s)\n",
            (double)__atomic_exchange_n(&wp64_queued, 0ll, __ATOMIC_RELAXED) / s,
            (double)__atomic_exchange_n(&wp64_direct, 0ll, __ATOMIC_RELAXED) / s,
            (double)__atomic_exchange_n(&wp64_syncs[0], 0ll, __ATOMIC_RELAXED) / s,
            (double)__atomic_exchange_n(&wp64_syncs[1], 0ll, __ATOMIC_RELAXED) / s,
            (double)__atomic_exchange_n(&wp64_syncs[2], 0ll, __ATOMIC_RELAXED) / s,
            (double)__atomic_exchange_n(&wp64_syncs[3], 0ll, __ATOMIC_RELAXED) / s,
            (double)__atomic_exchange_n(&wp64_syncs[4], 0ll, __ATOMIC_RELAXED) / s);
}
/* ---- end winport v64-defer-vtx block ---- */
static void gcm_sleep_ns(u64 ns);   /* winport v96c-ppupace: defined below */
/* ---- winport v96c-ppupace (engine_fixes_v96c_ppupace.py): WINPORT_PPU_PACE96=<pct> ---- */
static int wp96c_pct = -1, wp96c_log;
static double wp96c_ema, wp96c_maxms = 12.0; static u64 wp96c_prev; static long long wp96c_wprev;
static volatile long long wp96c_n, wp96c_sleep_ns, wp96c_walk_total;   /* walk_total: ns the async walker spent walking (monotonic) */
static void wp96c_report(double s)
{
    if (wp96c_pct <= 0 || !(wp96c_log)) return;
    fprintf(stderr, "[K96C] PPU pace %d%%: flips=%.0f/s slept=%.1f ms/s T=%.2f ms (walker busy/flip avg %.2f ms)\n", wp96c_pct,
            (double)__atomic_exchange_n(&wp96c_n, 0ll, __ATOMIC_RELAXED) / s,
            (double)__atomic_exchange_n(&wp96c_sleep_ns, 0ll, __ATOMIC_RELAXED) / 1e6 / s, wp96c_ema * wp96c_pct / 100.0, wp96c_ema);
}
/* T = pct% of the async walker's average busy time per flip: walker-bound (combat) -> the PPU flips at the walker's
 * steady rate instead of short/long bursts; PPU-bound -> T is below the PPU's own period, no sleeping. The PPU's own
 * intervals are NOT used (sleeping would raise them and ratchet the rate down, measured: run P97, 98 -> 50 fps). */
static void wp96c_pace(void)
{
    if (wp96c_pct < 0) {
        const char* e = getenv("WINPORT_PPU_PACE96"); int v = (e && *e) ? atoi(e) : 0; if (v < 0 || v > 100) v = 0;
        if ((e = getenv("WINPORT_PPU_PACE96_MAXMS")) && atof(e) > 0.0 && atof(e) <= 50.0) wp96c_maxms = atof(e);
        e = getenv("WINPORT_PPU_PACE96_LOG"); wp96c_log = (e && e[0] == '1');
        if (v && !wp59_async_on()) { fprintf(stderr, "[K96C] PPU pacing needs WINPORT_ASYNC_RSX=1 -> off\n"); v = 0; }
        if (v) fprintf(stderr, "[K96C] even PPU frame pacing ON: flip interval >= %d%% of the walker's busy time per flip (max sleep %.1f ms)\n", v, wp96c_maxms);
        wp96c_pct = v;
    }
    if (wp96c_pct <= 0) return;
    __atomic_add_fetch(&wp96c_n, 1ll, __ATOMIC_RELAXED);
    u64 now = get_timestamp_ns();
    const long long wt = __atomic_load_n(&wp96c_walk_total, __ATOMIC_RELAXED);
    const double busy = (double)(wt - wp96c_wprev) / 1e6; wp96c_wprev = wt;
    if (!wp96c_prev) { wp96c_prev = now; return; }
    double iv = (double)(now - wp96c_prev) / 1e6;                     /* ms since the previous flip */
    if (iv > 250.0 || busy > 250.0) { wp96c_prev = now; return; }      /* load / stall: skip */
    wp96c_ema += (busy - wp96c_ema) / 16.0;
    const double T = wp96c_ema * (double)wp96c_pct / 100.0;
    if (iv < T) {
        double ms = T - iv; if (ms > wp96c_maxms) ms = wp96c_maxms;
        extern int ppu_giant_lock_held_by_caller(void);
        extern void ppu_giant_lock_release(void);
        extern void ppu_giant_lock_acquire(void);
        const int held = ppu_giant_lock_held_by_caller();
        if (held) ppu_giant_lock_release();
        gcm_sleep_ns((u64)(ms * 1e6));
        if (held) ppu_giant_lock_acquire();
        const u64 t2 = get_timestamp_ns();
        __atomic_add_fetch(&wp96c_sleep_ns, (long long)(t2 - now), __ATOMIC_RELAXED);
        now = t2;
    }
    wp96c_prev = now;
}
/* ---- end winport v96c-ppupace decl ---- */
static void gcm_sleep_ns(u64 ns)
{
    ps3_gcm_sleep_ns(ns);
}
static u64 gcm_overflow_wait_ns(void)
{
    static u64 ns = ~(u64)0;
    if (ns == ~(u64)0)
        ns = ps3_gcm_wait_ns_parse(getenv("PS3_GCM_WAIT_NS"));
    return ns;
}
static void gcm_overflow_wait(void)
{
    if (wp59_async_on() && !t_wp59_rsx) { wp59_wait_progress(); return; }   /* winport v59-async-rsx */
    const u64 ns = gcm_overflow_wait_ns();
    if (ns)
        gcm_sleep_ns(ns);
}
static volatile u64 s_vsync_next_ns = 0;   /* next vblank of the grid; __atomic_* access only */
static void gcm_vsync_pace(void)
{
    extern int ppu_giant_lock_held_by_caller(void);
    extern void ppu_giant_lock_release(void);
    extern void ppu_giant_lock_acquire(void);
    if (!gcm_vsync_on() || s_flip_mode != CELL_GCM_DISPLAY_VSYNC) return;
    { extern volatile long g_wp51_ff_k; if (g_wp51_ff_k > 1) return; }   /* winport v51-rt-cutscene: no 60 Hz pacing while skipping */
    const u64 period = 1000000000ull / 60ull;
    u64 next = __atomic_load_n(&s_vsync_next_ns, __ATOMIC_SEQ_CST);
    u64 now = get_timestamp_ns();
    if (next != 0 && now < next && next - now <= period && ppu_giant_lock_held_by_caller()) {
        ppu_giant_lock_release();
        gcm_sleep_ns(next - now);
        ppu_giant_lock_acquire();
        now = get_timestamp_ns();
    }
    next = (next != 0 && now < next + period) ? next + period : now + period;
    __atomic_store_n(&s_vsync_next_ns, next, __ATOMIC_SEQ_CST);
}
static void gcm_bridge_present(u32 bufferId);
/* Flips queued at their stream offsets, oldest first, presented when the walker's get
 * reaches each one (the console executes a flip when the RSX reaches it, and the RSX
 * can run behind the PPU: GoW2's wait points hold it until the title releases each
 * command segment). The HLE and the FIFO walker run under the PPU giant lock; the queue
 * is still safe on its own: entries are only touched under the spinlock, and the count
 * only through atomic builtins, so the walker's per-command check is a single atomic
 * load. The spinlock is never held across a present. */
#define GCM_FLIP_Q_CAP 8u
static struct { u32 off; u32 id; } s_flip_q[GCM_FLIP_Q_CAP];
static u32 s_flip_q_head = 0;           /* under the spinlock */
static volatile long s_flip_qn = 0;     /* entries; atomic: __atomic_load_n / __atomic_store_n only */
static volatile long s_flip_lock = 0;
static void gcm_flip_lock(void)   { while (__sync_lock_test_and_set(&s_flip_lock, 1)) { } }
static void gcm_flip_unlock(void) { __sync_lock_release(&s_flip_lock); }
static inline int gcm_flip_pending(void) { return __atomic_load_n(&s_flip_qn, __ATOMIC_SEQ_CST) != 0; }
static inline long gcm_flip_count(void) { return __atomic_load_n(&s_flip_qn, __ATOMIC_SEQ_CST); }
/* Append; returns 0 when full (the caller makes room first). */
static int gcm_flip_put(u32 off, u32 id)
{
    int ok = 0;
    gcm_flip_lock();
    long n = __atomic_load_n(&s_flip_qn, __ATOMIC_SEQ_CST);
    if (n >= 0 && (u32)n < GCM_FLIP_Q_CAP) {
        const u32 slot = (s_flip_q_head + (u32)n) % GCM_FLIP_Q_CAP;
        s_flip_q[slot].off = off;
        s_flip_q[slot].id = id;
        __atomic_store_n(&s_flip_qn, n + 1, __ATOMIC_SEQ_CST);
        ok = 1;
    }
    gcm_flip_unlock();
    return ok;
}
static u32 s_flip_skipped;   /* flips dropped because the walker reached a newer one (giant lock) */
/* The flip whose offset get has reached: take it, dropping the older ones still queued
 * ahead of it -- the stream can jump past a flip's offset (a released wait point whose
 * JUMP skips the rest of a reserved segment), and such a flip would otherwise block every
 * later one (2026-09-17: half the flips presented only when the queue overflowed). Their
 * commands have run; only the newest buffer id is shown. The scan without the lock is a
 * hint re-checked under it (the walker and the flip call both hold the giant lock). */
static int gcm_flip_take_at(u32 get, u32* id_out)
{
    long n = __atomic_load_n(&s_flip_qn, __ATOMIC_SEQ_CST);
    if (n <= 0) return 0;
    int hint = 0;
    for (long i = 0; i < n && i < (long)GCM_FLIP_Q_CAP; i++)
        if (s_flip_q[(s_flip_q_head + (u32)i) % GCM_FLIP_Q_CAP].off == get) { hint = 1; break; }
    if (!hint) return 0;
    int due = 0;
    gcm_flip_lock();
    n = __atomic_load_n(&s_flip_qn, __ATOMIC_SEQ_CST);
    for (long i = 0; i < n && i < (long)GCM_FLIP_Q_CAP; i++) {
        const u32 slot = (s_flip_q_head + (u32)i) % GCM_FLIP_Q_CAP;
        if (s_flip_q[slot].off != get) continue;
        *id_out = s_flip_q[slot].id;
        s_flip_skipped += (u32)i;
        s_flip_q_head = (slot + 1u) % GCM_FLIP_Q_CAP;
        __atomic_store_n(&s_flip_qn, n - i - 1, __ATOMIC_SEQ_CST);
        due = 1;
        break;
    }
    gcm_flip_unlock();
    return due;
}
/* Take the oldest flip, reached or not. */
static int gcm_flip_take_any(u32* id_out)
{
    if (!gcm_flip_pending()) return 0;
    int due = 0;
    gcm_flip_lock();
    long n = __atomic_load_n(&s_flip_qn, __ATOMIC_SEQ_CST);
    if (n > 0) {
        *id_out = s_flip_q[s_flip_q_head].id;
        s_flip_q_head = (s_flip_q_head + 1u) % GCM_FLIP_Q_CAP;
        __atomic_store_n(&s_flip_qn, n - 1, __ATOMIC_SEQ_CST);
        due = 1;
    }
    gcm_flip_unlock();
    return due;
}
/* PS3_TRACE_GCM_PRESENT=<n> (OFF, diagnostic): the first <n> in-stream presents with
 * where they fired from (walker at a command, end of a drain, forced by the next
 * flip), get/put and the queued stream offset. */
static void gcm_present_trace(const char* via, u32 id, u32 get, u32 off)
{
    static long lim = -1, n = 0;
    if (lim < 0) {
        const char* e = getenv("PS3_TRACE_GCM_PRESENT");
        char* end = NULL;
        long v = (e && *e) ? strtol(e, &end, 10) : 0;
        lim = (e && *e && end && *end == '\0' && v > 0 && v <= 1000000) ? v : 0;
    }
    if (n >= lim) return;
    n++;
    fprintf(stderr, "[GCMPRES] via=%s id=%u get=0x%X put=0x%X off=0x%X\n",
            via, id, get, vm_read32(GCM_CTRL_EA + 0), off);
}
/* PS3_TRACE_FIFO_WAIT=<n> (diagnostic, OFF by default): the first n walker stops at a
 * wait point (JUMP to itself), with put and the label word GoW2's segments release. */
static const char* s_fifo_kick_src = "?";   /* diagnostic: which call ran the walker */
/* E483: quanto do trabalho de FIFO corre SINCRONO na thread do guest e quanto
 * corre na thread de wake. O perfil do gameplay mostra rsx_process_fifo a 6,3%
 * do tempo proprio da thread do PPU enquanto a thread de render esta' a 1,4%
 * ocupada -- mas isso nao diz se ja' ha' assincronia a acontecer. Sem esta
 * reparticao, "mover o FIFO para a thread dedicada" e' uma mudanca
 * arquitectural decidida no escuro.
 *
 * "wake" e' a thread assincrona (poll de 1 ms); tudo o resto -- flip, label,
 * ctrlreg, eagerput, flipdrain, flipwait, overflow -- corre na thread que fez a
 * chamada GCM, ou seja no guest. */
static unsigned long long s_fifo_kick_n_sync, s_fifo_kick_n_wake, s_fifo_kick_bytes;

/* E485: a reparticao sincrono/assincrono nao chega -- diz QUANTO corre na
 * thread do guest, nao QUAL chamada o faz correr. Custou uma volta: montei a
 * campainha em `cellGcmGetControlRegister` a supor que era essa a porta, e a
 * medicao respondeu wake=0 (o sino nunca tocou), ou seja o jogo pede o registo
 * de controlo uma vez e guarda o ponteiro -- aquele sitio nao esta' no laco do
 * frame. Sem atribuicao por origem, a proxima tentativa seria outro palpite. */
enum {
    KS_CTRLREG = 0, KS_FLIP, KS_FLIPDRAIN, KS_FLIPWAIT, KS_LABEL,
    KS_OVERFLOW, KS_OVERFLOWWAIT, KS_EAGERPUT, KS_BELL, KS_WAKE, KS_DRAIN, KS_N
};
static const char* const s_kick_name[KS_N] = {
    "ctrlreg", "flip", "flipdrain", "flipwait", "label",
    "overflow", "overflowwait", "eagerput", "bell", "wake", "drain"
};
static unsigned long long s_kick_n[KS_N];
/* winport v59-async-rsx Stage 0: walk time per kick source (always on; [GCMDK] printed with WINPORT_GCMDIAG=1) */
static volatile long long wp59_src_ns[KS_N], wp59_src_n[KS_N], wp59_bells[KS_N];
static int wp59_syncflip(int which)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("WINPORT_ASYNC_RSX_SYNCFLIP"); on = (e && e[0] == '1') ? 1 : 0; }
    return on && (which == KS_FLIP || which == KS_FLIPDRAIN || which == KS_FLIPWAIT);
}
static void wp59_kick_report(double s)
{
    static unsigned long long prev[KS_N];
    char line[768]; int n = 0;
    if (s <= 0.0) s = 1.0;
    for (int i = 0; i < KS_N; i++) {
        const unsigned long long cur = __atomic_load_n(&s_kick_n[i], __ATOMIC_RELAXED);
        const unsigned long long d = cur - prev[i];
        prev[i] = cur;
        const long long ns = __atomic_exchange_n(&wp59_src_ns[i], 0ll, __ATOMIC_RELAXED);
        const long long w = __atomic_exchange_n(&wp59_src_n[i], 0ll, __ATOMIC_RELAXED);
        const long long b = __atomic_exchange_n(&wp59_bells[i], 0ll, __ATOMIC_RELAXED);
        if (!d && !ns && !b) continue;
        if (n < (int)sizeof line - 1)
            n += snprintf(line + n, sizeof line - (size_t)n, " %s=%.0f/s walk=%.1fms/%.0f",
                          s_kick_name[i], (double)d / s, (double)ns / 1e6 / s, (double)w / s);
        if (b && n < (int)sizeof line - 1)
            n += snprintf(line + n, sizeof line - (size_t)n, " bell=%.0f", (double)b / s);
        if (n >= (int)sizeof line) { n = (int)sizeof line - 1; break; }
    }
    line[n < (int)sizeof line ? n : (int)sizeof line - 1] = 0;
    fprintf(stderr, "[GCMDK]%s\n", n ? line : " none");
}
static void rsx_process_fifo(void);
/* Chamar EM VEZ de `s_fifo_kick_src = ...; s_fifo_kick_n_*++; rsx_process_fifo();`
 * para que origem, contador e caminhada nao possam divergir. */
static void gcm_fifo_kick(int which)
{
    s_fifo_kick_src = s_kick_name[which];
    __atomic_add_fetch(&s_kick_n[which], 1ull, __ATOMIC_RELAXED);
    if (wp59_async_on() && !t_wp59_rsx && !wp59_syncflip(which)) {   /* winport v59-async-rsx: the guest only rings */
        __atomic_add_fetch(&wp59_bells[which], 1ll, __ATOMIC_RELAXED);
        wp59_bell();
        return;
    }
    if (which == KS_BELL || which == KS_WAKE || which == KS_DRAIN) s_fifo_kick_n_wake++;
    else                                      s_fifo_kick_n_sync++;
    {   const u64 t59 = get_timestamp_ns();   /* winport v59-async-rsx Stage 0 */
        rsx_process_fifo();
        __atomic_add_fetch(&wp59_src_ns[which], (long long)(get_timestamp_ns() - t59), __ATOMIC_RELAXED);
        __atomic_add_fetch(&wp59_src_n[which], 1ll, __ATOMIC_RELAXED); }
}
void gcm_fifo_kick_take(unsigned long long* sync, unsigned long long* wake)
{
    if (sync) { *sync = s_fifo_kick_n_sync; }
    if (wake) { *wake = s_fifo_kick_n_wake; }
    /* Impresso AQUI e nao no backend Metal de proposito: `gcm_fifo_kick_take`
     * ja' e' chamado uma vez por segundo pelo contador [FIFOKICK], e assim a
     * atribuicao por origem nao precisa de tocar em `rsx_metal_backend.m`. */
    {
        static unsigned long long prev[KS_N];
        char line[256]; int n = 0; int any = 0;
        for (int i = 0; i < KS_N; i++) {
            const unsigned long long cur = __atomic_load_n(&s_kick_n[i], __ATOMIC_RELAXED);
            const unsigned long long d = cur - prev[i];
            prev[i] = cur;
            if (!d) continue;
            any = 1;
            n += snprintf(line + n, (n < (int)sizeof line) ? sizeof line - (size_t)n : 0,
                          "%s%s=%llu", n ? " " : "", s_kick_name[i], d);
            if (n >= (int)sizeof line) { n = (int)sizeof line - 1; break; }
        }
        if (any) fprintf(stderr, "[FIFOSRC] %s\n", line);
    }
}

/* RSX wake: on the console the RSX leaves a wait point as soon as the title patches the
 * word, whatever the PPU is doing. Here the walker only runs inside GCM calls (flush via
 * the control register, flip, label, overflow), so a released wait point sat until the
 * next such call -- one GoW2 segment batch per frame (2026-09-17: releases land between
 * calls, 13-75 ms later). While the walker is stopped at a wait point this host thread
 * checks the word every millisecond without the lock (a single 32-bit load) and, once it
 * changed, runs the walker under the PPU giant lock like any other caller, presenting the
 * flips it reaches (the Metal backend defers its window-title update to the main thread's
 * event pump). PS3_RSX_WAKE=0 disables it. */
static void rsx_process_fifo(void);
static volatile u32 s_fifo_wait_at = 0xFFFFFFFFu;   /* stopped wait point offset; __atomic_* only */
static volatile u32 s_fifo_wait_word = 0u;          /* the word there when it stopped; __atomic_* only */
static volatile u32 s_fifo_more = 0u;               /* 1: the walker stopped at its guard with work left */

/* winport v32-gcmdiag: WINPORT_GCMDIAG=1 -> one [GCMD] line per second (see engine_fixes_v32_gcmdiag.py) */
static int wp32_diag_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("WINPORT_GCMDIAG"); on = (e && e[0] == '1') ? 1 : 0; }
    return on;
}
static __thread int t_wp32_wake;            /* 1 on the RSX wake thread */
static volatile long wp32_walkers;          /* rsx_process_fifo calls in progress */
static volatile long long wp32_walk_ns[2], wp32_walks[2], wp32_busy_skip;
static volatile long long wp32_ovf_n, wp32_ovf_ns, wp32_ovf_left, wp32_ovf_it, wp32_ovf_sleep, wp32_ovf_lock, wp32_ovf_kick;
static volatile long long wp32_ovf_st[3];   /* overflow-wait ns by walker state: 0 parked at wait point, 1 walking elsewhere, 2 idle */
static volatile long long wp32_flipw_n, wp32_flipw_ns, wp32_flips, wp32_pres;
static volatile long long wp32_wp_stops, wp32_wp_ns, wp32_wp_max, wp32_wp_det, wp32_wp_detlat, wp32_wp_reslat, wp32_wp_bywake;
static volatile long long wp32_wake_polls, wp32_wake_kicks, wp32_wake_lock;
static volatile u32 wp32_stop_at = 0xFFFFFFFFu;
static volatile u64 wp32_stop_t, wp32_det_t;
#define WP32_ADD(v, d) __atomic_add_fetch(&(v), (long long)(d), __ATOMIC_RELAXED)
#define WP32_TAKE(v) __atomic_exchange_n(&(v), 0ll, __ATOMIC_RELAXED)
static int wp32_state(void)
{
    if (__atomic_load_n(&wp32_walkers, __ATOMIC_RELAXED) > 0) return 1;
    if (__atomic_load_n(&s_fifo_wait_at, __ATOMIC_RELAXED) != 0xFFFFFFFFu) return 0;
    return 2;
}

/* winport v32-gcmdiag-w: writer of the parked wait point (ppu_loader.cpp / spu_put_map.c record it) */
extern volatile uint32_t g_wp32_watch_ea;
extern volatile unsigned long long g_wp32_wr_t;
extern volatile int g_wp32_wr_kind;
extern volatile unsigned g_wp32_wr_tid;
extern volatile unsigned long long g_wp32_wr_ra;
unsigned long long wp32_now_ns(void) { return get_timestamp_ns(); }
static volatile long long wp32_wr_n, wp32_wr_lat, wp32_wr_res, wp32_wr_none, wp32_wr_ppu_main, wp32_wr_ppu_other, wp32_wr_spu, wp32_wr_bulk;
static volatile unsigned wp32_main_tid, wp32_last_tid;
static volatile int wp32_last_kind = -1;
static volatile unsigned long long wp32_last_ra;
static volatile long long wp32_wr_img[16];
static void wp32_writer_end(u64 stop_t, u64 t0)
{
    const unsigned long long wt = __atomic_load_n(&g_wp32_wr_t, __ATOMIC_ACQUIRE);
    g_wp32_watch_ea = 0xFFFFFFF0u;
    { void wp32_lbl_end_fwd(u64); wp32_lbl_end_fwd((wt && wt >= stop_t && wt <= t0) ? wt : 0ull); }   /* winport v32-gcmdiag-l */
    if (!wt || wt < stop_t || wt > t0) { WP32_ADD(wp32_wr_none, 1); return; }
    WP32_ADD(wp32_wr_n, 1); WP32_ADD(wp32_wr_lat, wt - stop_t); WP32_ADD(wp32_wr_res, t0 - wt);
    const int k = g_wp32_wr_kind;
    if (k == 0) { if (g_wp32_wr_tid == wp32_main_tid) WP32_ADD(wp32_wr_ppu_main, 1); else WP32_ADD(wp32_wr_ppu_other, 1); }
    else if (k >= 100) { WP32_ADD(wp32_wr_spu, 1); WP32_ADD(wp32_wr_img[(k - 100) & 15], 1); }
    else WP32_ADD(wp32_wr_bulk, 1);
    wp32_last_kind = k; wp32_last_tid = g_wp32_wr_tid; wp32_last_ra = g_wp32_wr_ra;
}
static void wp32_writer_park(u32 io, u32 get)
{
    __atomic_store_n(&g_wp32_wr_t, 0ull, __ATOMIC_RELEASE);
    g_wp32_watch_ea = io + get;
}
static void wp32_writer_report(void)
{
    const long long n = WP32_TAKE(wp32_wr_n), lat = WP32_TAKE(wp32_wr_lat), res = WP32_TAKE(wp32_wr_res);
    char imgs[160]; int o = 0; imgs[0] = 0;
    for (int i = 0; i < 16; i++) { const long long c = WP32_TAKE(wp32_wr_img[i]);
        if (c && o < 140) o += snprintf(imgs + o, sizeof imgs - (size_t)o, " i%d=%lld", i, c); }
    fprintf(stderr, "[GCMDW] writes=%lld wrlat=%.3fms reslat=%.3fms none=%lld | ppu_main=%lld ppu_other=%lld spu=%lld%s bulk=%lld | last kind=%d tid=%u ra=+0x%llX main_tid=%u\n",
            n, n ? (double)lat / (double)n / 1e6 : 0.0, n ? (double)res / (double)n / 1e6 : 0.0, WP32_TAKE(wp32_wr_none),
            WP32_TAKE(wp32_wr_ppu_main), WP32_TAKE(wp32_wr_ppu_other), WP32_TAKE(wp32_wr_spu), imgs, WP32_TAKE(wp32_wr_bulk),
            wp32_last_kind, wp32_last_tid, wp32_last_ra, wp32_main_tid);
}

/* winport v32-gcmdiag-l: label -> SPU read -> patch timing (engine_fixes_v32_gcmdiag.py part 3) */
volatile unsigned g_wp32_lbl_ea = 0xFFFFFFF0u;
static volatile u64 wp32_lbl_t, wp32_lbl_seen_t;
static volatile long long wp32_lbl_w, wp32_lbl_get, wp32_lbl_llar, wp32_l_n, wp32_l_seen, wp32_l_seenlat, wp32_l_work, wp32_l_unseen;
volatile long long g_wp32_job_n[16], g_wp32_job_ns[16], g_wp32_job_cpu[16];
void wp32_lbl_get_note(unsigned ea, int llar)
{
    (void)ea;
    WP32_ADD(*(llar ? &wp32_lbl_llar : &wp32_lbl_get), 1);
    if (wp32_lbl_t && !__atomic_load_n(&wp32_lbl_seen_t, __ATOMIC_RELAXED))
        __atomic_store_n(&wp32_lbl_seen_t, get_timestamp_ns(), __ATOMIC_RELAXED);
}
static void wp32_lbl_written(u32 off)
{
    wp32_lbl_t = get_timestamp_ns();
    __atomic_store_n(&wp32_lbl_seen_t, 0ull, __ATOMIC_RELAXED);
    g_wp32_lbl_ea = GCM_LABEL_EA + off;
    WP32_ADD(wp32_lbl_w, 1);
}
/* called at a wait point's end with the writer's time (0 = unknown) */
static void wp32_lbl_end(u64 wt)
{
    const u64 lt = wp32_lbl_t, st = __atomic_load_n(&wp32_lbl_seen_t, __ATOMIC_RELAXED);
    WP32_ADD(wp32_l_n, 1);
    if (!lt || !st || st < lt || (wt && st > wt)) { WP32_ADD(wp32_l_unseen, 1); return; }
    WP32_ADD(wp32_l_seen, 1); WP32_ADD(wp32_l_seenlat, st - lt);
    if (wt) WP32_ADD(wp32_l_work, wt - st);
}
void wp32_lbl_end_fwd(u64 wt) { wp32_lbl_end(wt); }
static void wp32_lbl_report(void)
{
    const long long n = WP32_TAKE(wp32_l_n), sn = WP32_TAKE(wp32_l_seen), sl = WP32_TAKE(wp32_l_seenlat), wk = WP32_TAKE(wp32_l_work);
    char jb[320]; int o = 0; jb[0] = 0;
    for (int i = 0; i < 16; i++) { const long long c = __atomic_exchange_n(&g_wp32_job_n[i], 0ll, __ATOMIC_RELAXED);
        const long long ns = __atomic_exchange_n(&g_wp32_job_ns[i], 0ll, __ATOMIC_RELAXED);
        const long long cpu = __atomic_exchange_n(&g_wp32_job_cpu[i], 0ll, __ATOMIC_RELAXED);
        if (c && o < 280) o += snprintf(jb + o, sizeof jb - (size_t)o, " i%d=%lld/%.1fms(avg %.3f cpu %.1fms)", i, c, ns / 1e6, (double)ns / (double)c / 1e6, cpu / 1e6); }
    fprintf(stderr, "[GCMDL] label writes=%lld spu_gets=%lld getllar=%lld | wp ends=%lld seen=%lld unseen=%lld label->spu_read=%.3fms spu_read->patch=%.3fms | jobs%s\n",
            WP32_TAKE(wp32_lbl_w), WP32_TAKE(wp32_lbl_get), WP32_TAKE(wp32_lbl_llar), n, sn, WP32_TAKE(wp32_l_unseen),
            sn ? (double)sl / (double)sn / 1e6 : 0.0, sn ? (double)wk / (double)sn / 1e6 : 0.0, jb);
}
/* winport v32-wpspin: WINPORT_WP_SPIN_US -> spin at a wait point before parking (engine_fixes_v32_wpspin.py) */
static u64 wp32_spin_ns(void)
{
    static long long ns = -1;
    if (ns < 0) {
        const char* e = getenv("WINPORT_WP_SPIN_US");
        long v = (e && *e) ? strtol(e, NULL, 0) : 0;
        if (v < 0) v = 0;
        if (v > 20000) v = 20000;
        ns = (long long)v * 1000ll;
        if (v) fprintf(stderr, "[WPSPIN] RSX walker spins up to %ld us at wait points before parking\n", v);
    }
    return (u64)ns;
}
static volatile long long wp32_sp_n, wp32_sp_ok, wp32_sp_to, wp32_sp_ns, wp32_sp_okns;
/* 1 = the wait-point word changed while spinning (the walker re-reads it and goes on) */
static int wp32_spin_release(unsigned char* hbase, u32 io, u32 get, u32 cmd)
{
    const u64 lim = wp32_spin_ns();
    const u64 t0 = get_timestamp_ns();
    u64 now = t0;
    int ok = 0;
    const u32* p = hbase ? (const u32*)(hbase + (size_t)io + get) : NULL;
    if (wp32_diag_on()) wp32_writer_park(io, get);   /* diag: who patches it, label timing */
    for (unsigned i = 0;; i++) {
        const u32 w = p ? __builtin_bswap32(__atomic_load_n(p, __ATOMIC_ACQUIRE)) : vm_read32((unsigned long long)io + get);
        if (w != cmd) { ok = 1; break; }
        __builtin_ia32_pause();
        if ((i & 31u) == 31u) { now = get_timestamp_ns(); if (now - t0 >= lim) break; }
    }
    if (wp32_diag_on()) {
        now = get_timestamp_ns();
        WP32_ADD(wp32_sp_n, 1); WP32_ADD(wp32_sp_ns, now - t0);
        if (ok) { WP32_ADD(wp32_sp_ok, 1); WP32_ADD(wp32_sp_okns, now - t0); wp32_writer_end(t0, now); } else WP32_ADD(wp32_sp_to, 1);
    }
    return ok;
}
static void wp32_spin_report(void)
{
    if (!wp32_spin_ns()) return;
    const long long n = WP32_TAKE(wp32_sp_n), ok = WP32_TAKE(wp32_sp_ok), okns = WP32_TAKE(wp32_sp_okns);
    fprintf(stderr, "[WPSPIN] stops=%lld released=%lld (avg %.3f ms) timeouts=%lld spin=%.1f ms\n",
            n, ok, ok ? (double)okns / (double)ok / 1e6 : 0.0, WP32_TAKE(wp32_sp_to), WP32_TAKE(wp32_sp_ns) / 1e6);
}
/* walker side (under s_walking): a stop ends when a walk leaves the parked wait point */
static void wp32_after_walk(int at_wp, u32 get, u64 t0, const char* src)
{
    const u64 now = get_timestamp_ns();
    const u32 at = wp32_stop_at;
    if (at != 0xFFFFFFFFu && !(at_wp && get == at)) {
        const u64 st = wp32_stop_t, dt = __atomic_load_n(&wp32_det_t, __ATOMIC_RELAXED);
        const long long d = (long long)(t0 - st);
        WP32_ADD(wp32_wp_ns, d);
        if (d > __atomic_load_n(&wp32_wp_max, __ATOMIC_RELAXED)) __atomic_store_n(&wp32_wp_max, d, __ATOMIC_RELAXED);
        if (dt && dt >= st && dt <= t0) { WP32_ADD(wp32_wp_det, 1); WP32_ADD(wp32_wp_detlat, dt - st); WP32_ADD(wp32_wp_reslat, t0 - dt); }
        if (src && src[0] == 'w') WP32_ADD(wp32_wp_bywake, 1);
        wp32_writer_end(st, t0);   /* winport v32-gcmdiag-w */
        __atomic_store_n(&wp32_stop_at, 0xFFFFFFFFu, __ATOMIC_RELAXED);
    }
    if (at_wp && __atomic_load_n(&wp32_stop_at, __ATOMIC_RELAXED) == 0xFFFFFFFFu) {
        wp32_stop_t = now;
        __atomic_store_n(&wp32_det_t, 0ull, __ATOMIC_RELAXED);
        __atomic_store_n(&wp32_stop_at, get, __ATOMIC_RELAXED);
        wp32_writer_park(s_config.ioAddress, get);   /* winport v32-gcmdiag-w */
        WP32_ADD(wp32_wp_stops, 1);
    }
}
static void wp32_ovf_iter_note(int st, u64 a, u64 b, u64 c, u64 d)
{
    WP32_ADD(wp32_ovf_it, 1);
    WP32_ADD(wp32_ovf_sleep, b - a);
    WP32_ADD(wp32_ovf_lock, c - b);
    WP32_ADD(wp32_ovf_kick, d - c);
    WP32_ADD(wp32_ovf_st[st], d - a);
}
static void wp32_report(void)
{
    static u64 last = 0;
    const u64 now = get_timestamp_ns();
    if (!last) { last = now; return; }
    if (now - last < 1000000000ull) return;
    const double s = (double)(now - last) / 1e9, ms = 1e6 * s;   /* ns -> ms per second */
    last = now;
    const long long on = WP32_TAKE(wp32_ovf_n), wst = WP32_TAKE(wp32_wp_stops), wdet = WP32_TAKE(wp32_wp_det);
    const long long dl = WP32_TAKE(wp32_wp_detlat), rl = WP32_TAKE(wp32_wp_reslat);
    fprintf(stderr, "[GCMD] flips=%.1f pres=%.1f | ovf n=%.1f ms=%.1f left_kb=%.0f it=%.0f sleep=%.1f lock=%.1f kick=%.1f "
            "st(wp/walk/idle)=%.1f/%.1f/%.1f | walk main=%.1fms/%.0f wake=%.1fms/%.0f busy=%.0f | "
            "wp stops=%.1f ms=%.1f max=%.2f det=%.0f detlat=%.2f reslat=%.2f bywake=%.0f | flipw n=%.1f ms=%.1f | "
            "wake polls=%.0f kicks=%.0f lock=%.1fms\n",
            WP32_TAKE(wp32_flips) / s, WP32_TAKE(wp32_pres) / s,
            on / s, WP32_TAKE(wp32_ovf_ns) / ms, on ? (double)WP32_TAKE(wp32_ovf_left) / (double)on / 1024.0 : (WP32_TAKE(wp32_ovf_left), 0.0),
            WP32_TAKE(wp32_ovf_it) / s, WP32_TAKE(wp32_ovf_sleep) / ms, WP32_TAKE(wp32_ovf_lock) / ms, WP32_TAKE(wp32_ovf_kick) / ms,
            WP32_TAKE(wp32_ovf_st[0]) / ms, WP32_TAKE(wp32_ovf_st[1]) / ms, WP32_TAKE(wp32_ovf_st[2]) / ms,
            WP32_TAKE(wp32_walk_ns[0]) / ms, WP32_TAKE(wp32_walks[0]) / s, WP32_TAKE(wp32_walk_ns[1]) / ms, WP32_TAKE(wp32_walks[1]) / s,
            WP32_TAKE(wp32_busy_skip) / s,
            wst / s, WP32_TAKE(wp32_wp_ns) / ms, WP32_TAKE(wp32_wp_max) / 1e6, wdet / s,
            wdet ? (double)dl / (double)wdet / 1e6 : 0.0, wdet ? (double)rl / (double)wdet / 1e6 : 0.0, WP32_TAKE(wp32_wp_bywake) / s,
            WP32_TAKE(wp32_flipw_n) / s, WP32_TAKE(wp32_flipw_ns) / ms,
            WP32_TAKE(wp32_wake_polls) / s, WP32_TAKE(wp32_wake_kicks) / s, WP32_TAKE(wp32_wake_lock) / ms);
    wp32_writer_report();   /* winport v32-gcmdiag-w */
    wp32_spin_report();   /* winport v32-wpspin */
    wp32_lbl_report();   /* winport v32-gcmdiag-l */
    wp59_kick_report(s);   /* winport v59-async-rsx Stage 0 */
}
static int gcm_rsx_wake_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_RSX_WAKE"); on = (e && e[0] == '0') ? 0 : 1; }
    return on;
}
/* PS3_GCM_FIFO_NOLOCK=1 (OFF por default; e435nlk default ON was fps=22):
 * a thread do RSX percorre o stream SEM o giant lock.
 *
 * Porque e' fiel: no console o RSX consome o ring do `get` ate' ao `put` em
 * paralelo com o PPU -- e' esse o contrato do ring, o titulo so' escreve depois
 * do `put` e so' publica o `put` quando os comandos ja' estao na memoria. O
 * walker nao executa codigo guest; le o stream por ponteiro host, le texturas e
 * vertices, e grava estado no Metal.
 *
 * Porque importa: medido 2026-09-17, o decode custa ~8 ms por frame e corria
 * inteiro dentro do giant lock, ou seja ~176 ms por segundo de lock segurado
 * por trabalho que nao e' do guest, com 15 threads PPU do jogo a' espera.
 *
 * Exclusao mutua: `rsx_process_fifo_inner` ja' tem o guard `s_walking`
 * (test-and-set), que faz um segundo chamador desistir em vez de reprocessar os
 * mesmos comandos -- o mesmo caminho que ja' existia para o walker preemptado. */
static int gcm_fifo_nolock_on(void)
{
    static int on = -1;
    if (on < 0)
        on = ps3_gcm_fifo_nolock_parse(getenv("PS3_GCM_FIFO_NOLOCK"));
    return on;
}
/* PS3_GCM_EAGER_PUT=1 enables. Default OFF: e435egp (default ON) measured
 * fps_median=21.5 vs w64=29 — extra FIFO kicks on the wake thread raised
 * decode_ms and did not cut overflow enough to win. */
static int gcm_eager_put_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_GCM_EAGER_PUT"); on = (e && e[0] == '1') ? 1 : 0; }
    return on;
}
static int gcm_eager_put_pending(u32 io)
{
    u32 np;
    if (!gcm_eager_put_on() || !io) return 0;
    return ps3_gcm_eager_put(vm_read32(GCM_CTX_EA + 8), io, vm_read32(GCM_CTRL_EA + 0),
                             vm_read32(GCM_CTX_EA + 0), vm_read32(GCM_CTX_EA + 4), &np);
}
/* ---------------------------------------------------------------------------
 * PS3_RSX_ASYNC_FIFO=1 -- a CAMPAINHA (default OFF; POSIX so').
 *
 * No console o titulo publica o `put` e acabou: o RSX consome o ring sozinho,
 * em paralelo com o PPU. Em nos, `cellGcmGetControlRegister` percorria o stream
 * inteiro ali mesmo, na thread do guest -- e' dai' que vem o perfil medido
 * (metal_va_fill 12,7% + rsx_process_fifo 6,3% + rsx_process_method 6,1% +
 * metal_set_shader 5,4% = ~31% do frame do guest) com a thread de render a 1,4%.
 *
 * A prova de que deslocar isto funciona esta' em e911aec2: baixar so' o
 * intervalo de sondagem levou o trabalho de 63% para 20% sincrono. Sondar a
 * 100 us, porem, e' queimar CPU a perguntar. Isto e' a versao certa da mesma
 * coisa -- o guest toca a campainha (um incremento e um signal) e volta ao seu
 * trabalho; quem anda o stream e' a thread dedicada, que deixa de sondar e
 * passa a esperar no condvar com o mesmo timeout de antes.
 *
 * O timeout NAO desaparece de proposito: o caminho de sondagem continua a ser a
 * rede de seguranca dos casos que nao tocam a campainha (o walker parado num
 * wait point, `s_fifo_more`, o eager put). Com a campainha calada, o
 * comportamento e' byte a byte o de hoje.
 *
 * Exclusao mutua: nao ha' nova. `rsx_process_fifo_inner` ja' tem o `s_walking`
 * (test-and-set) desde o walker preemptado -- um segundo chamador desiste em
 * vez de reprocessar os mesmos comandos.
 *
 * Os pontos onde o guest PRECISA mesmo do resultado (flip wait, overflow wait,
 * leitura de label) continuam sincronos: la' esperar e' o contrato. */
#ifndef _WIN32
static pthread_mutex_t s_bell_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_bell_cv = PTHREAD_COND_INITIALIZER;
static volatile unsigned s_bell_seq = 0;    /* toques publicados pelo guest */
static unsigned s_bell_seen = 0;            /* so' a thread de wake le/escreve */
static int gcm_async_fifo_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_RSX_ASYNC_FIFO"); on = (e && e[0] == '1') ? 1 : 0; }
    return on;
}
static void gcm_fifo_bell_ring(void)
{
    /* RELEASE: o `put` que o guest acabou de escrever tem de ser visivel a quem
     * acordar com este toque. */
    __atomic_add_fetch(&s_bell_seq, 1u, __ATOMIC_RELEASE);
    pthread_mutex_lock(&s_bell_mx);
    pthread_cond_signal(&s_bell_cv);
    pthread_mutex_unlock(&s_bell_mx);
}
#else
static int gcm_async_fifo_on(void) { return 0; }
static void gcm_fifo_bell_ring(void) { }
#endif
/* ---------------------------------------------------------------------------
 * PS3_RSX_DRAIN=1 -- consumo CONTINUO (default OFF).
 *
 * Isto e' o que a atribuicao por origem apontou, e nao o sitio onde eu tinha
 * posto a campainha. Medido em gameplay real (110 s, 756 draws, 38 fps):
 *
 *   overflowwait=140  wake=92  ctrlreg=78  flip=39  overflow=30   -> 76% sincrono
 *   decode_ms=11,4 num frame de 26 ms
 *
 * `overflowwait` domina, e e' um sintoma. O guest enche o buffer de comandos
 * inteiro; o `put` so' e' publicado no wrap (~30/s) e no flip; ate' la' a thread
 * do RSX nao tem NADA para consumir, por isso nao consome. Quando o buffer da'
 * a volta, `ps3_gcm_ctx_overflow` tem de esperar que o ring drene antes de o
 * reciclar -- e o laco dessa espera anda o FIFO ELE MESMO, ~4,7 vezes por wrap.
 * Ou seja: o guest escreve uma volta inteira e depois descodifica-a sozinho.
 *
 * No console o RSX vai consumindo ENQUANTO o titulo escreve, portanto no wrap
 * ja' quase nao ha' o que esperar. A thread de wake so' acordava para wait
 * points (`s_fifo_more`, a palavra do wait point) e ignorava o caso simples de
 * haver trabalho publicado por consumir. Isto acrescenta esse caso.
 *
 * O teste e' um load de 32 bits sem lock, que ja' e' o que o poll faz hoje.
 *
 * MEDIDO (2026-09-18, alternado, com PS3_GCM_FIFO_NOLOCK junto -- sem ele a
 * thread de wake trava no giant lock, que foi o que matou a campainha 0/3):
 *
 *   DRAIN+NOLOCK=1  sincrono 52% / 32%   overflow 9 / 36   fps 45 / 43   762 / 828 draws
 *   DRAIN+NOLOCK=0  sincrono 94%         overflow 38       fps 44        865 draws
 *
 * Veredito em duas partes, e as duas contam:
 *   - FUNCIONA e e' SEGURO: o trabalho muda mesmo de thread (94% -> 32-52%),
 *     chega ao gameplay 2/2, e o overflowwait praticamente desaparece.
 *   - NAO COMPRA FPS: 45 / 44 / 43, dentro da dispersao. Confirma a refutacao
 *     anterior de PS3_GCM_FIFO_NOLOCK: o decode nao esta' a ser roubado ao
 *     guest -- ele corre enquanto o guest ja' esperaria de qualquer maneira.
 * Fica OFF por omissao: e' arquitectura mais fiel ao console sem ganho medido,
 * e a quarta corrida (NOLOCK=0 r2) nao chegou a completar. */
static int gcm_rsx_drain_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_RSX_DRAIN"); on = (e && e[0] == '1') ? 1 : 0; }
    return on;
}
static void gcm_rsx_wake_poll(void)
{
    if (wp32_diag_on()) WP32_ADD(wp32_wake_polls, 1);   /* winport v32-gcmdiag */
    extern void ppu_giant_lock_release(void);
    extern void ppu_giant_lock_acquire(void);
    const u32 at = __atomic_load_n(&s_fifo_wait_at, __ATOMIC_SEQ_CST);
    const u32 io = s_config.ioAddress;
    if (!io) return;
    const int more = __atomic_load_n(&s_fifo_more, __ATOMIC_SEQ_CST) != 0u;
    const int eager = gcm_eager_put_pending(io);
    int rang = 0;
#ifndef _WIN32
    {   /* consumir o toque ANTES de andar o stream: um `put` publicado durante a
         * caminhada fica contado e da' outra volta, em vez de se perder. */
        const unsigned seq = __atomic_load_n(&s_bell_seq, __ATOMIC_ACQUIRE);
        rang = (seq != s_bell_seen);
        s_bell_seen = seq;
    }
#endif
    /* Ha' trabalho publicado por consumir? E' o caso que faltava. */
    const int pending = gcm_rsx_drain_on() &&
                        vm_read32(GCM_CTRL_EA + 4) != vm_read32(GCM_CTRL_EA + 0);
    if (!more && !eager && !rang && !pending) {
        if (at == 0xFFFFFFFFu) return;
        if (vm_read32((unsigned long long)io + at) == __atomic_load_n(&s_fifo_wait_word, __ATOMIC_SEQ_CST)) return;
    }
    const int nolock = gcm_fifo_nolock_on();
    const int wd32 = wp32_diag_on();   /* winport v32-gcmdiag */
    u64 wt32 = 0;
    if (wd32) {
        wt32 = get_timestamp_ns();
        if (at != 0xFFFFFFFFu && __atomic_load_n(&wp32_stop_at, __ATOMIC_RELAXED) == at &&
            __atomic_load_n(&wp32_det_t, __ATOMIC_RELAXED) == 0ull &&
            vm_read32((unsigned long long)io + at) != __atomic_load_n(&s_fifo_wait_word, __ATOMIC_SEQ_CST))
            __atomic_store_n(&wp32_det_t, wt32, __ATOMIC_RELAXED);
    }
    if (!nolock) ppu_giant_lock_acquire();
    if (wd32) WP32_ADD(wp32_wake_lock, get_timestamp_ns() - wt32);
    if (eager) {
        u32 np;
        const u32 put = vm_read32(GCM_CTRL_EA + 0);
        if (ps3_gcm_eager_put(vm_read32(GCM_CTX_EA + 8), io, put,
                              vm_read32(GCM_CTX_EA + 0), vm_read32(GCM_CTX_EA + 4), &np)) {
            vm_write32(GCM_CTRL_EA + 0, np);
            gcm_fifo_kick(KS_EAGERPUT);
        }
    }
    if (rang || pending ||
        __atomic_load_n(&s_fifo_more, __ATOMIC_SEQ_CST) != 0u ||
        (at != 0xFFFFFFFFu && __atomic_load_n(&s_fifo_wait_at, __ATOMIC_SEQ_CST) == at)) {
        if (wd32) WP32_ADD(wp32_wake_kicks, 1);
        gcm_fifo_kick(rang ? KS_BELL : (pending ? KS_DRAIN : KS_WAKE));
    }
    if (!nolock) ppu_giant_lock_release();
}
#ifdef _WIN32
static DWORD WINAPI gcm_rsx_wake_thread(LPVOID arg)
{
    (void)arg;
    t_wp32_wake = 1;   /* winport v32-gcmdiag */
    /* winport v29-gcm: with WINPORT_GCM_HRT=1 sleep exactly PS3_RSX_WAKE_US (default 1000) instead of Sleep(1) */
    extern int wp_gcm_hrt_on(void);
    if (wp_gcm_hrt_on()) {
        const char* e = getenv("PS3_RSX_WAKE_US"); long v = (e && *e) ? strtol(e, NULL, 0) : 1000;
        if (v < 10 || v > 100000) v = 1000;
        fprintf(stderr, "[GCMHRT] precise GCM waits on, RSX wake every %ld us\n", v);
        for (;;) { ps3_gcm_sleep_ns((u64)v * 1000ull); gcm_rsx_wake_poll(); }
    }
    for (;;) { Sleep(1); gcm_rsx_wake_poll(); }
    return 0;
}
#else
static void* gcm_rsx_wake_thread(void* arg)
{
#ifdef __APPLE__
    pthread_setname_np("host:gcm-wake");
#endif

    (void)arg;
    /* E484: o intervalo de sondagem decide QUEM chega primeiro ao trabalho.
     * A 1 ms a thread de wake perde quase sempre a corrida para a chamada GCM
     * do guest, e por isso ~70% do FIFO corre na thread critica (medido em
     * 1d614b46). Baixar o intervalo e' o teste mais barato da tese do item 1:
     * se a razao [FIFOKICK] se deslocar para "wake", mover trabalho para a
     * thread dedicada funciona, e ai' vale fazer isto como deve ser -- acordar
     * por sinal em vez de sondar. PS3_RSX_WAKE_US ajusta; default mantem 1 ms. */
    static unsigned long long ival_ns = 0;
    if (!ival_ns) {
        const char* e = getenv("PS3_RSX_WAKE_US");
        long v = (e && *e) ? strtol(e, NULL, 0) : 1000;
        if (v < 10 || v > 100000) v = 1000;
        ival_ns = (unsigned long long)v * 1000ull;
    }
    for (;;) {
        if (gcm_async_fifo_on()) {
            /* Esperar pelo toque, com o MESMO timeout que se dormia antes -- o
             * caminho de timeout continua a ser a rede de seguranca. O teste de
             * `s_bell_seq` sob o mutex fecha a corrida do toque que chega entre
             * o poll anterior e o wait (senao dormia-se com trabalho na mao). */
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            {
                const unsigned long long ns = (unsigned long long)ts.tv_nsec + ival_ns;
                ts.tv_sec  += (time_t)(ns / 1000000000ull);
                ts.tv_nsec  = (long)(ns % 1000000000ull);
            }
            pthread_mutex_lock(&s_bell_mx);
            if (__atomic_load_n(&s_bell_seq, __ATOMIC_ACQUIRE) == s_bell_seen)
                pthread_cond_timedwait(&s_bell_cv, &s_bell_mx, &ts);
            pthread_mutex_unlock(&s_bell_mx);
        } else {
            gcm_sleep_ns(ival_ns);
        }
        gcm_rsx_wake_poll();
    }
    return NULL;
}
#endif
/* ---- winport v59-async-rsx: the rsx-async thread (replaces the wake thread when WINPORT_ASYNC_RSX=1) ---- */
#ifdef _WIN32
static volatile long long wp59_rsx_walk_ns, wp59_rsx_walks, wp59_rsx_idle_ns, wp59_lag_max, wp59_qflips_max;
static int wp59_flip_at(u32 get)   /* hint only: a queued flip sits exactly at get (presented by a walk at get == put) */
{
    long n = gcm_flip_count();
    for (long i = 0; i < n && i < (long)GCM_FLIP_Q_CAP; i++)
        if (s_flip_q[(s_flip_q_head + (u32)i) % GCM_FLIP_Q_CAP].off == get) return 1;
    return 0;
}
/* ---- winport v92b-besem (engine_fixes_v92b_besem.py) ---- */
static int wp92b_besem(void)
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_BESEM92"); v = (e && e[0] == '1');
    if (v) { fprintf(stderr, "[BESEM92] NV4097 back-end semaphore release writes the un-swizzled value (RSX behaviour)\n"); fflush(stderr); } }
  return v; }
static volatile long long wp92b_n;
static volatile u32 wp92b_off, wp92b_raw, wp92b_wr;
static inline u32 wp92b_swz(u32 v) { return (v & 0xFF00FF00u) | ((v >> 16) & 0xFFu) | ((v & 0xFFu) << 16); }
static u32 wp92b_be_value(u32 off, u32 v)   /* method 0x1D70 */
{
    const u32 w = wp92b_besem() ? wp92b_swz(v) : v;
    __atomic_add_fetch(&wp92b_n, 1ll, __ATOMIC_RELAXED);
    wp92b_off = off; wp92b_raw = v; wp92b_wr = w;
    return w;
}
static u32 wp92b_acq_off, wp92b_acq_want;   /* last ACQUIRE that timed out (written by wp59_acq_check) */
/* ---- winport v92b-lblpoll (engine_fixes_v92b_lblpoll.py) ---- */
static volatile long long wp92l_poll[256];
static volatile long long wp92l_rel_n;
static volatile u32 wp92l_rel_off, wp92l_rel_val;
static int wp92l_lbl16(void)
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_LBL16_92"); v = (e && e[0] == '1');
    if (v) { fprintf(stderr, "[LBLPOLL92] cellGcmGetLabelAddress uses 16-byte label slots (libgcm layout)\n"); fflush(stderr); } }
  return v; }
static int wp92l_sem406e(void)
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_SEM406E92"); v = (e && e[0] == '1');
    if (v) { fprintf(stderr, "[SEM406E92] NV406E SEMAPHORE_RELEASE writes its label\n"); fflush(stderr); } }
  return v; }
static void wp92l_report(double s)
{
    if (s <= 0) s = 1.0;
    int best = -1; long long bn = 0;
    for (int i = 0; i < 256; i++) { const long long n = __atomic_exchange_n(&wp92l_poll[i], 0ll, __ATOMIC_RELAXED); if (n > bn) { bn = n; best = i; } }
    if (best >= 0 && (double)bn / s > 100000.0) {
        const u32 st = wp92l_lbl16() ? 16u : 4u;
        fprintf(stderr, "[LBLPOLL92] label index %d polled %.0f/s: guest 0x%08X = 0x%08X (x16 slot 0x%X = 0x%08X, x4 slot 0x%X = 0x%08X)\n",
                best, (double)bn / s, GCM_LABEL_EA + (u32)best * st, vm_read32((unsigned long long)GCM_LABEL_EA + (u32)best * st),
                (u32)best * 16u, vm_read32((unsigned long long)GCM_LABEL_EA + (u32)best * 16u),
                (u32)best * 4u, vm_read32((unsigned long long)GCM_LABEL_EA + (u32)best * 4u));
    }
    const long long r = __atomic_exchange_n(&wp92l_rel_n, 0ll, __ATOMIC_RELAXED);
    if (r && wp_stats_on()) fprintf(stderr, "[SEM406E92] NV406E releases=%.0f/s last offset 0x%X value 0x%08X (%s)\n", (double)r / s,
                   wp92l_rel_off, wp92l_rel_val, wp92l_sem406e() ? "written" : "ignored, WINPORT_SEM406E92=0");
}
/* ---- end winport v92b-lblpoll ---- */
static void wp92b_report(double s)
{
    const long long n = __atomic_exchange_n(&wp92b_n, 0ll, __ATOMIC_RELAXED);
    if (n && wp_stats_on()) fprintf(stderr, "[BESEM92] back-end releases=%.0f/s last label[0x%X] raw=0x%08X wrote=0x%08X (%s)\n",
                   (double)n / (s > 0 ? s : 1.0), wp92b_off, wp92b_raw, wp92b_wr, wp92b_besem() ? "swizzled" : "raw, WINPORT_BESEM92=0");
    /* stall watchdog (log only): no flip presented for >= 2 s (get/put/wait point/last ACQUIRE say where the walker is) */
    static u32 last_get = 0xFFFFFFFFu; static u64 since = 0;
    const u64 now = get_timestamp_ns();
    const u32 get = vm_read32(GCM_CTRL_EA + 4), put = vm_read32(GCM_CTRL_EA + 0);
    static long last_fl = -1; const long fl = __atomic_load_n(&wp59_flips_presented, __ATOMIC_RELAXED);
    if (fl != last_fl) { last_get = get; last_fl = fl; since = now; return; }   /* stall = no flip presented */
    if (now - since < 2000000000ull) return;
    { static u64 said; if (said >= since && now - said < 30000000000ull) return; said = now; }   /* winport release v92: once, then every 30 s */
    const u32 at = __atomic_load_n(&s_fifo_wait_at, __ATOMIC_SEQ_CST);
    fprintf(stderr, "[STALL92] walker stopped %.1f s at get=0x%X put=0x%X waitpoint=%s0x%X word=0x%08X | last ACQUIRE timeout label[0x%X] want 0x%08X now 0x%08X\n",
            (double)(now - since) / 1e9, get, put, at == 0xFFFFFFFFu ? "none " : "", at == 0xFFFFFFFFu ? 0u : at,
            __atomic_load_n(&s_fifo_wait_word, __ATOMIC_SEQ_CST), wp92b_acq_off, wp92b_acq_want,
            vm_read32((unsigned long long)GCM_LABEL_EA + wp92b_acq_off));
}
/* ---- end winport v92b-besem ---- */
static void wp59_report(void)
{
    static u64 last = 0;
    const u64 now = get_timestamp_ns();
    if (!last) { last = now; return; }
    if (now - last < 1000000000ull) return;
    const double s = (double)(now - last) / 1e9, ms = 1e6 * s;
    last = now;
    char bl[320]; int n = 0;
    for (int i = 0; i < KS_N; i++) {   /* bells/s by source (not reset: wp59_kick_report owns the reset) */
        static unsigned long long prevk[KS_N];
        const unsigned long long cur = __atomic_load_n(&s_kick_n[i], __ATOMIC_RELAXED);
        const unsigned long long d = cur - prevk[i]; prevk[i] = cur;
        if (d && n < (int)sizeof bl - 1) n += snprintf(bl + n, sizeof bl - (size_t)n, "%s%s=%.0f", n ? "," : "", s_kick_name[i], (double)d / s);
        if (n >= (int)sizeof bl) { n = (int)sizeof bl - 1; break; }
    }
    bl[n < (int)sizeof bl ? n : (int)sizeof bl - 1] = 0;
    const long long gw = __atomic_exchange_n(&wp59_gwaits, 0ll, __ATOMIC_RELAXED);
    const long long lo = __atomic_exchange_n(&wp59_locks_other, 0ll, __ATOMIC_RELAXED);
    if (!wp_stats_on()) ; else fprintf(stderr, "[ASYNC] walk=%.1fms/%.0f idle=%.1fms | guest wait=%.1fms/%.0f lockwait=%.1fms/%.0f | kicks %s | "
            "acq park=%.0f ok=%.0f timeout=%.0f | lag_max=%.0fKB qflips_max=%lld | pres rsx=%.0f other=%.0f | owner_bad=%lld | seg n=%.0f/s wait=%.2fms/seg wraps=%.0f/s stall=%lld\n",
            (double)__atomic_exchange_n(&wp59_rsx_walk_ns, 0ll, __ATOMIC_RELAXED) / ms,
            (double)__atomic_exchange_n(&wp59_rsx_walks, 0ll, __ATOMIC_RELAXED) / s,
            (double)__atomic_exchange_n(&wp59_rsx_idle_ns, 0ll, __ATOMIC_RELAXED) / ms,
            (double)__atomic_exchange_n(&wp59_gwait_ns, 0ll, __ATOMIC_RELAXED) / ms, (double)gw / s,
            (double)__atomic_exchange_n(&wp59_lockwait_ns, 0ll, __ATOMIC_RELAXED) / ms, (double)lo / s,
            n ? bl : "-",
            (double)__atomic_exchange_n(&wp59_acq_parks, 0ll, __ATOMIC_RELAXED) / s,
            (double)__atomic_exchange_n(&wp59_acq_ok, 0ll, __ATOMIC_RELAXED) / s,
            (double)__atomic_exchange_n(&wp59_acq_to, 0ll, __ATOMIC_RELAXED) / s,
            (double)__atomic_exchange_n(&wp59_lag_max, 0ll, __ATOMIC_RELAXED) / 1024.0,
            __atomic_exchange_n(&wp59_qflips_max, 0ll, __ATOMIC_RELAXED),
            (double)__atomic_exchange_n(&wp59_pres_rsx, 0ll, __ATOMIC_RELAXED) / s,
            (double)__atomic_exchange_n(&wp59_pres_other, 0ll, __ATOMIC_RELAXED) / s,
            __atomic_load_n(&wp59_owner_bad, __ATOMIC_RELAXED),
            (double)wp60_segs / s, wp60_segs ? (double)wp60_seg_wait_ns / (double)wp60_segs / 1e6 : 0.0,   /* winport v60-async-seg */
            (double)__atomic_exchange_n(&wp60_wraps, 0ll, __ATOMIC_RELAXED) / s,
            __atomic_load_n(&wp60_stalls, __ATOMIC_RELAXED));
    __atomic_store_n(&wp60_segs, 0ll, __ATOMIC_RELAXED); __atomic_store_n(&wp60_seg_wait_ns, 0ll, __ATOMIC_RELAXED);
    wp64_report(s);   /* winport v64-defer-vtx */
    wp96c_report(s);   /* winport v96c-ppupace */
    wp92b_report(s);   /* winport v92b-besem */
    wp92l_report(s);   /* winport v92b-lblpoll */
    fflush(stderr);
}
static DWORD WINAPI wp59_rsx_thread(LPVOID arg)
{
    (void)arg;
    t_wp59_rsx = 1;
    t_wp32_wake = 1;   /* [GCMD] counts its walks as "wake" */
    {   typedef HRESULT (WINAPI *set_desc_fn)(HANDLE, PCWSTR);
        set_desc_fn f = (set_desc_fn)(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetThreadDescription");
        if (f) f(GetCurrentThread(), L"rsx-async"); }
    u64 poll_ns = 200000ull;
    { const char* e = getenv("WINPORT_ASYNC_RSX_POLL_US"); long v = (e && *e) ? strtol(e, NULL, 0) : 200;
      if (v >= 20 && v <= 5000) poll_ns = (u64)v * 1000ull; }
    fprintf(stderr, "[ASYNC59] rsx-async thread running (tid %lu, poll %llu us)\n",
            (unsigned long)GetCurrentThreadId(), (unsigned long long)(poll_ns / 1000ull));
    fflush(stderr);
    for (;;) {
        const u32 io = s_config.ioAddress;
        int work = 0;
        u32 put = 0, get = 0;
        if (io) {
            put = vm_read32(GCM_CTRL_EA + 0);
            get = vm_read32(GCM_CTRL_EA + 4);
            const u32 at = __atomic_load_n(&s_fifo_wait_at, __ATOMIC_SEQ_CST);
            if (__atomic_load_n(&s_fifo_more, __ATOMIC_SEQ_CST) != 0u) work = 1;
            else if (at != 0xFFFFFFFFu)
                work = vm_read32((unsigned long long)io + at) != __atomic_load_n(&s_fifo_wait_word, __ATOMIC_SEQ_CST);
            else work = (put != get) || (gcm_flip_pending() && wp59_flip_at(get));
        }
        if (work) {
            const long long lag = (put >= get) ? (long long)(put - get) : 0ll;
            if (lag > wp59_lag_max) wp59_lag_max = lag;
            const long long qf = gcm_flip_count();
            if (qf > wp59_qflips_max) wp59_qflips_max = qf;
            s_fifo_kick_src = "async";
            const u64 t0 = get_timestamp_ns();
            rsx_process_fifo();
            { const long long d96_ = (long long)(get_timestamp_ns() - t0); __atomic_add_fetch(&wp59_rsx_walk_ns, d96_, __ATOMIC_RELAXED); __atomic_add_fetch(&wp96c_walk_total, d96_, __ATOMIC_RELAXED); }   /* winport v96c-ppupace */
            __atomic_add_fetch(&wp59_rsx_walks, 1ll, __ATOMIC_RELAXED);
            if (wp59_prog_ev) SetEvent(wp59_prog_ev);
            wp59_report();
            if (vm_read32(GCM_CTRL_EA + 4) != get) continue;   /* moved: look again at once */
        } else {
            wp59_report();
        }
        const u64 t1 = get_timestamp_ns();
        wp59_wait2(wp59_bell_ev, poll_ns);
        __atomic_add_fetch(&wp59_rsx_idle_ns, (long long)(get_timestamp_ns() - t1), __ATOMIC_RELAXED);
    }
    return 0;
}
static void wp59_start_once(void)
{
    if (!wp59_async_on() || wp59_started) return;
    if (InterlockedCompareExchange(&wp59_started, 1, 0) != 0) return;
    (void)rsx_bridge_mode();   /* backend init on this (guest) thread, before the RSX thread can touch it */
    wp59_prog_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    wp59_bell_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE h = CreateThread(NULL, 0, wp59_rsx_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
    else fprintf(stderr, "[ASYNC59] CreateThread failed (%lu): nothing walks the FIFO now\n", (unsigned long)GetLastError());
}
#endif
/* ---- end winport v59-async-rsx thread ---- */
static void gcm_rsx_wake_start_once(void)
{
    static volatile long started = 0;
    if (wp59_async_on()) { wp59_start_once(); return; }   /* winport v59-async-rsx: rsx-async replaces the wake thread */
    if (!gcm_rsx_wake_on() || __sync_lock_test_and_set(&started, 1)) return;
#ifdef _WIN32
    HANDLE h = CreateThread(NULL, 0, gcm_rsx_wake_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
#else
    pthread_t t;
    if (pthread_create(&t, NULL, gcm_rsx_wake_thread, NULL) == 0) pthread_detach(t);
#endif
}
static u32 s_wait_stop_get = 0xFFFFFFFFu;
static u64 s_wait_stop_ns;
static void gcm_waitpoint_trace(u32 get, u32 put)
{
    static long lim = -1, n = 0;
    if (lim < 0) {
        const char* e = getenv("PS3_TRACE_FIFO_WAIT");
        char* end = NULL;
        long v = (e && *e) ? strtol(e, &end, 10) : 0;
        lim = (e && *e && end && *end == '\0' && v > 0 && v <= 1000000) ? v : 0;
    }
    if (n >= lim) return;
    n++;
    const u64 now = get_timestamp_ns();
    const int same = (get == s_wait_stop_get);
    fprintf(stderr, "[FIFOWAIT] src=%s get=0x%X put=0x%X ahead=%u label[0x%X]=0x%08X%s since_stop_ms=%.2f\n",
            s_fifo_kick_src, get, put, put >= get ? put - get : 0u, s_rsx_sema_off,
            vm_read32((unsigned long long)GCM_LABEL_EA + s_rsx_sema_off),
            same ? " (still unreleased)" : "", s_wait_stop_ns ? (double)(now - s_wait_stop_ns) / 1.0e6 : -1.0);
    if (!same) { s_wait_stop_get = get; s_wait_stop_ns = now; }
}
/* Present a taken flip and open the next backend frame. */
static u32 s_dump_last_logical;
static void gcm_flip_present_id(u32 id)
{
    __atomic_add_fetch(&wp59_flips_presented, 1l, __ATOMIC_RELAXED);   /* winport v59-async-rsx */
    if (wp59_async_on()) __atomic_add_fetch(t_wp59_rsx ? &wp59_pres_rsx : &wp59_pres_other, 1ll, __ATOMIC_RELAXED);
    if (wp32_diag_on()) WP32_ADD(wp32_pres, 1);   /* winport v32-gcmdiag */
    s_dump_last_logical = rsx_stats_logical();
    gcm_bridge_present(id);
    rsx_backend* nb = rsx_get_backend();
    if (nb && nb->begin_frame) nb->begin_frame(nb->userdata);
    s_rsx_in_frame = 1;
    rsx_capture_on_begin_frame();
}

/* CPU time the guest FIFO decode costs, for the backend's PS3_TRACE_FPS line: the walker
 * runs under the PPU giant lock, so this is time the title is not running. Taken (and
 * reset) once per second by gcm_perf_take_dec_ms. */
static volatile long long s_gcm_dec_ns_acc;   /* __atomic_* only: the walker also runs on the RSX wake thread */
double gcm_perf_take_dec_ms(void)
{
    const long long ns = __atomic_exchange_n(&s_gcm_dec_ns_acc, 0ll, __ATOMIC_SEQ_CST);
    return (double)ns / 1.0e6;
}
/* Reading the command stream through vm_read32 pays the accessor's checks on every word,
 * and the walker reads ~2 700 headers plus ~450 000 index words per gameplay frame: the
 * decode cost 12-13 ms of a ~62 ms frame, all of it holding the PPU giant lock. The FIFO
 * lives in the IO window the title mapped at cellGcmInit, committed for its whole size, so
 * inside that window the word can come straight from host memory. Outside it (or with
 * PS3_RSX_FIFO_FASTREAD=0) the accessor still runs. */
static unsigned char* gcm_fifo_host_base(void)
{
    extern unsigned char* vm_base;
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_RSX_FIFO_FASTREAD"); on = (e && e[0] == '0') ? 0 : 1; }
    return on ? vm_base : NULL;
}
static inline unsigned int gcm_fifo_read32(unsigned char* hbase, unsigned int io, unsigned int off)
{
    /* Offsets past the IO window are a desynced JUMP/CALL, not a guest EA.
     * Falling through to vm_read32(io+off) SIGBUS'd on PROT_NONE (0x7e463664)
     * once VM_NOCHECK skipped the uncommitted guard. */
    if (!gcm_fifo_off_in_window(off, s_config.ioSize))
        return 0;
    if (hbase) {
        unsigned int raw;
        memcpy(&raw, hbase + (size_t)io + off, 4);
        return __builtin_bswap32(raw);
    }
    return vm_read32((unsigned long long)io + off);
}
/* winport v59-async-rsx: honour NV406E SEMAPHORE ACQUIRE (0x68) in async mode -- the walker may now run ahead
 * of the SPU that writes the data a draw reads. 1 = stop before this header (not satisfied yet). */
static int wp59_acq_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("WINPORT_ASYNC_RSX_ACQ"); on = wp59_async_on() && !(e && e[0] == '0'); }
    return on;
}
static u32 wp59_sem_off = 0, wp59_acq_get = 0xFFFFFFFFu;
static u64 wp59_acq_t0 = 0;
static int wp59_acq_check(unsigned char* hbase, u32 io, u32 hdr, u32 method, u32 noinc, u32 count)
{
    static u64 lim_ns = 0;
    if (!lim_ns) { const char* e = getenv("WINPORT_ASYNC_RSX_ACQ_MS"); long v = (e && *e) ? strtol(e, NULL, 0) : 5;
                   lim_ns = (u64)((v >= 0 && v <= 1000) ? v : 5) * 1000000ull; if (!lim_ns) lim_ns = 1; }
    for (u32 i = 0; i < count && i < 16u; i++) {
        const u32 m = method + (noinc ? 0u : i * 4u);
        if (m != 0x0064u && m != 0x0068u) continue;
        const u32 val = gcm_fifo_read32(hbase, io, hdr + 4u + i * 4u);
        if (m == 0x0064u) { wp59_sem_off = val; continue; }
        if (wp64_qn) wp64_sync(1);   /* winport v64-defer-vtx: an ACQUIRE must see our own queued releases */
        const u32 cur = vm_read32((unsigned long long)GCM_LABEL_EA + wp59_sem_off);
        if (cur == val) {
            __atomic_add_fetch(&wp59_acq_ok, 1ll, __ATOMIC_RELAXED);
            if (wp59_acq_get == hdr) wp59_acq_get = 0xFFFFFFFFu;
            continue;
        }
        const u64 now = get_timestamp_ns();
        if (wp59_acq_get != hdr) {
            wp59_acq_get = hdr; wp59_acq_t0 = now;
            __atomic_add_fetch(&wp59_acq_parks, 1ll, __ATOMIC_RELAXED);
            return 1;
        }
        if (now - wp59_acq_t0 < lim_ns) return 1;
        {   const long long n = __atomic_add_fetch(&wp59_acq_to, 1ll, __ATOMIC_RELAXED);
            static long long tot = 0; tot++;
            if (tot <= 20) fprintf(stderr, "[ASYNC59] ACQUIRE timeout at get=0x%X label[0x%X]=0x%08X want 0x%08X\n",
                                   hdr, wp59_sem_off, cur, val);
            (void)n; }
        wp92b_acq_off = wp59_sem_off; wp92b_acq_want = val;   /* winport v92b-besem */
        wp59_acq_get = 0xFFFFFFFFu;
    }
    return 0;
}
/* Present from a non-walker thread (forced flips) under the RSX lock in async mode. */
static void wp59_present_locked(u32 id)
{
    const int tk = wp59_async_on() ? wp59_rsx_lock() : 0;
    gcm_flip_present_id(id);
    wp59_rsx_unlock(tk);
}
static void rsx_process_fifo_inner(void);
static void rsx_process_fifo(void)
{
    const int tk59 = wp59_async_on() ? wp59_rsx_lock() : 0;   /* winport v59-async-rsx: walker + D3D12 under the RSX lock */
    const u64 t0 = get_timestamp_ns();
    const int wd32 = wp32_diag_on();   /* winport v32-gcmdiag */
    if (wd32) __atomic_add_fetch(&wp32_walkers, 1, __ATOMIC_SEQ_CST);
    rsx_process_fifo_inner();
    const u64 t1 = get_timestamp_ns();
    if (wd32) { __atomic_sub_fetch(&wp32_walkers, 1, __ATOMIC_SEQ_CST);
                WP32_ADD(wp32_walk_ns[t_wp32_wake ? 1 : 0], t1 - t0); WP32_ADD(wp32_walks[t_wp32_wake ? 1 : 0], 1); }
    __atomic_fetch_add(&s_gcm_dec_ns_acc, (long long)(t1 - t0), __ATOMIC_SEQ_CST);
    wp59_rsx_unlock(tk59);   /* winport v59-async-rsx */
}
/* ---- winport v85b-bulk-methods: WINPORT_RSX_BULK85=1 (engine_fixes_v85b_bulk_methods.py) ---- */
extern int wp85_rsx_bulk(rsx_state* state, u32 method, u32 noinc, const unsigned char* src, u32 n);
static int wp85_bulk_on(void)
{
    static int v = -1;
    if (v < 0) { const char* e = getenv("WINPORT_RSX_BULK85"); v = (e && e[0] == '1') ? 1 : 0;
                 if (getenv("PS3_RSX_STATS")) v = 0;   /* the method histogram counts every word */
                 if (v) { fprintf(stderr, "[BULK85] vertex program / constant runs dispatched in bulk\n"); fflush(stderr); } }
    return v;
}
/* ---- end winport v85b-bulk-methods ---- */
static void rsx_process_fifo_inner(void)
{
    unsigned int io  = s_config.ioAddress;
    unsigned int put = vm_read32(GCM_CTRL_EA + 0);
    unsigned int get = vm_read32(GCM_CTRL_EA + 4);

    static int trace = -1;
    if (trace < 0) { const char* e = getenv("PS3_RSX_TRACE"); trace = e ? 1 : 0; }
    static int tn = 0;
    { static int hdr = 0; if (trace && hdr < 8) {
        fprintf(stderr, "[rsx-fifo] io=0x%08X get=0x%X put=0x%X\n", io, get, put);
        hdr++; fflush(stderr); } }
    /* Task 5 (bringup-instrumentacao-fase0): RSX FIFO_KICK -- um por chamada
     * a esta funcao (cellGcmGetControlRegister com PS3_RSX_FIFO, ou o dreno
     * feito a partir de cellGcmSetFlipCommand/cellGcmGetLabelAddress).
     * Disparo mesmo quando io==0 (early-return abaixo) para o volume de
     * chamadas ficar honesto. Sem lock proprio deste ficheiro. */
    PS3_TRACE(PS3_TS_RSX, "rsx_process_fifo", "FIFO_KICK", "io=0x%08X get=0x%08X put=0x%08X",
              io, get, put);
    if (!io) { wp59_store_be32(GCM_CTRL_EA + 4, put); return; }
    /* One walker at a time: a walker preempted inside vm_read* (giant-lock timeslice) must
     * not have a second caller process the same commands again. */
    static volatile long s_walking = 0;
    if (__sync_lock_test_and_set(&s_walking, 1)) {
        /* Contagem HONESTA: o print era capado a 20, o que fazia "20" parecer o
         * total quando podia ser milhares -- e' esse numero que decide se vale
         * ESPERAR pelo walker em vez de desistir do kick. */
        static volatile long n_busy_total = 0;
        if (wp32_diag_on()) WP32_ADD(wp32_busy_skip, 1);   /* winport v32-gcmdiag */
        const long nb = __sync_add_and_fetch(&n_busy_total, 1);
        if (nb <= 20 || (nb & 0xFFl) == 0)
            fprintf(stderr, "[FIFOSKIP] walker busy on another thread #%ld (src=%s get=0x%X put=0x%X)\n",
                    nb, s_fifo_kick_src, get, put);
        return;
    }

    const u64 wp32_t0 = get_timestamp_ns(); const char* const wp32_src = s_fifo_kick_src;   /* winport v32-gcmdiag */
    int bridge = rsx_bridge_mode();
    if (bridge && !s_rsx_in_frame) {
        rsx_backend* b = rsx_get_backend();
        if (b && b->begin_frame) b->begin_frame(b->userdata);
        s_rsx_in_frame = 1;
        rsx_capture_on_begin_frame();
    }

    /* RSX FIFO subroutine: CALL saves the address after itself and jumps; RETURN
     * resumes there (single level, as the NV40 FIFO and RPCS3's FIFO control). The
     * old walker jumped on CALL without saving and stepped over RETURN (`get += 4`),
     * so after the first CALL/RETURN pair it ran on inside the callee's memory until
     * it hit a garbage jump: E419b measured reads at io+0x1F800000 = EA 0x67600000,
     * past the 48 MB IO window, once the menu frames started. */
    unsigned char* const hbase = gcm_fifo_host_base();
    static unsigned int s_fifo_ret = 0xFFFFFFFFu;
    /* A JUMP to its own address is a wait point: the RSX idles there, get stays on it,
     * until the title patches the word (GoW2 ends every ~3 KB command segment with one,
     * after a SEMAPHORE_RELEASE, and turns it into a JUMP to the next word once it sees
     * the label). The walker used to spin on it until the guard and then publish
     * get = put, skipping every command after the wait point -- in GoW2 gameplay the
     * post-processing chain and the composite into the display buffer, so the flipped
     * buffer was never drawn (2026-09-17 command stream dumps). Now it stops there and
     * publishes get = the wait point, and the next call resumes from it.
     * PS3_RSX_FIFO_WAITPOINT=0 restores the old spin-and-skip for A/B. */
    static int waitpoint_on = -1;
    if (waitpoint_on < 0) { const char* e = getenv("PS3_RSX_FIFO_WAITPOINT"); waitpoint_on = (e && e[0] == '0') ? 0 : 1; }
    int at_waitpoint = 0;
    int guard = 0;
    while (get != put && guard++ < 200000) {
        if (!gcm_fifo_off_in_window(get, s_config.ioSize)) {
            static u32 n_oob = 0;
            if (n_oob < 20u) {
                n_oob++;
                fprintf(stderr, "[FIFOSKIP] get=0x%X past ioSize=0x%X (put=0x%X src=%s) -- stop walker\n",
                        get, s_config.ioSize, put, s_fifo_kick_src);
            }
            break;
        }
        { u32 fid; while (bridge && gcm_flip_take_at(get, &fid)) { gcm_present_trace("walker", fid, get, get); gcm_flip_present_id(fid); } }   /* the flip sits here */
        if (s_fifo_wrap_pending && get == s_fifo_wrap_at) {   /* buffer wrap */
            get = s_fifo_wrap_to;
            __atomic_store_n(&s_fifo_wrap_pending, 0, __ATOMIC_RELEASE);
            if (wp60_seg_on()) {   /* winport v60-async-seg: get first, then the lap count (wp60_consumed reads them in reverse) */
                wp64_sync(4);   /* winport v64-defer-vtx */
                wp59_store_be32(GCM_CTRL_EA + 4, get);
                __atomic_add_fetch(&wp60_lr, 1l, __ATOMIC_RELEASE);
            }
            {   int one = 1;   /* promote the queued wrap (CAS: the overflow may race us) */
                if (__atomic_compare_exchange_n(&s_fifo_wrap_next_pending, &one, 0, 0,
                                                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                    s_fifo_wrap_at = s_fifo_wrap_next_at;
                    s_fifo_wrap_to = s_fifo_wrap_next_to;
                    __atomic_store_n(&s_fifo_wrap_pending, 1, __ATOMIC_RELEASE);
                }
            }
            continue;
        }
        unsigned int cmd = gcm_fifo_read32(hbase, io, get);
        if ((cmd & 0xE0000003u) == 0x20000000u || (cmd & 3u) == 1u) {                  /* old / new jump */
            const unsigned int target = ((cmd & 3u) == 1u) ? (cmd & 0xFFFFFFFCu) : (cmd & 0x1FFFFFFCu);
            if (!gcm_fifo_off_in_window(target, s_config.ioSize)) {
                static u32 n_jmp = 0;
                if (n_jmp < 20u) {
                    n_jmp++;
                    fprintf(stderr, "[FIFOSKIP] JUMP to 0x%X past ioSize=0x%X at get=0x%X cmd=0x%08X -- stop walker\n",
                            target, s_config.ioSize, get, cmd);
                }
                break;
            }
            /* winport v32-wpspin: the SPU job that releases GoW2's wait points does so tens of us after the
             * label; spin for it (WINPORT_WP_SPIN_US) instead of parking for a 1-2 ms poll. */
            if (target == get && waitpoint_on && wp32_spin_ns() && wp32_spin_release(hbase, io, get, cmd))
                continue;   /* released: re-read the patched word at the same get */
            if (target == get && waitpoint_on) {
                at_waitpoint = 1;
                gcm_waitpoint_trace(get, put);
                __atomic_store_n(&s_fifo_wait_word, cmd, __ATOMIC_SEQ_CST);
                __atomic_store_n(&s_fifo_wait_at, get, __ATOMIC_SEQ_CST);
                gcm_rsx_wake_start_once();
                break;
            }
            get = target;
            continue;
        }
        if ((cmd & 3u) == 2u) {                                                        /* call  */
            const unsigned int target = cmd & 0xFFFFFFFCu;
            if (!gcm_fifo_off_in_window(target, s_config.ioSize)) {
                static u32 n_call = 0;
                if (n_call < 20u) {
                    n_call++;
                    fprintf(stderr, "[FIFOSKIP] CALL to 0x%X past ioSize=0x%X at get=0x%X -- stop walker\n",
                            target, s_config.ioSize, get);
                }
                break;
            }
            s_fifo_ret = get + 4; get = target; continue;
        }
        if ((cmd & 0xFFFF0003u) == 0x00020000u) {                                      /* return */
            if (s_fifo_ret != 0xFFFFFFFFu) { get = s_fifo_ret; s_fifo_ret = 0xFFFFFFFFu; }
            else get += 4;   /* RETURN without a CALL: skip it, as before */
            continue;
        }

        unsigned int count  = (cmd >> 18) & 0x7FFu;
        unsigned int subch  = (cmd >> 13) & 0x7u;   /* object the method is for */
        unsigned int method =  cmd & 0x1FFCu;
        unsigned int noinc  =  cmd & 0x40000000u;
        /* PS3_TRACE_FIFO_SYNC (OFF, probe only): E442 saw 3D payloads (VP
         * microcode, floats) decoded as headers on subchannels 1/4/6/7. Keep the
         * last 8 headers; on the first headers with subch != 0 print where the
         * walker is and what it decoded just before; and log the NV406E
         * semaphore methods (0x60 context, 0x64 offset, 0x68 ACQUIRE, 0x6C
         * release), which the walker does not honour. */
        { static int fs = -1;
          if (fs < 0) { const char* e = getenv("PS3_TRACE_FIFO_SYNC"); fs = (e && *e && *e != '0') ? 1 : 0; }
          if (fs) {
              static unsigned int ring_off[8], ring_cmd[8], ring_n = 0;
              static int n_desync = 0;
              if (subch != 0u && n_desync < 20) {
                  n_desync++;
                  fprintf(stderr, "[FIFOSYNC] subch=%u method=0x%04X count=%u at get=0x%X cmd=0x%08X; before:",
                          subch, method, count, get, cmd);
                  for (unsigned int k = 0; k < 8 && k < ring_n; k++) {
                      unsigned int j = (ring_n - 1u - k) & 7u;
                      fprintf(stderr, " [0x%X:%08X]", ring_off[j], ring_cmd[j]);
                  }
                  fprintf(stderr, "\n");
              }
              ring_off[ring_n & 7u] = get;
              ring_cmd[ring_n & 7u] = cmd;
              ring_n++;
              if (method >= 0x0060u && method <= 0x006Cu && count) {
                  static int n_sem = 0;
                  if (n_sem < 60) {
                      n_sem++;
                      unsigned int v = vm_read32((unsigned long long)io + get + 4);
                      fprintf(stderr, "[FIFOSYNC] NV406E 0x%04X = 0x%08X at get=0x%X (sem_off=0x%X label[off]=0x%08X)\n",
                              method, v, get, s_rsx_sema_off,
                              vm_read32((unsigned long long)GCM_LABEL_EA + s_rsx_sema_off));
                  }
              }
          } }
        if (wp59_acq_on() && count && wp59_acq_check(hbase, io, get, method, noinc, count))
            break;   /* winport v59-async-rsx: ACQUIRE not satisfied -> stop before this header (guard stop, resumed later) */
        get += 4;
        for (unsigned int i = 0; i < count; i++) {
            unsigned int val = gcm_fifo_read32(hbase, io, get);
            unsigned int m   = method + (noinc ? 0 : i * 4);
            if (i < 2u && bridge && subch == 0u && hbase && !trace && count - i >= 2u &&   /* winport v85b-bulk-methods */
                g_rsx_capture_state != 2 && wp85_bulk_on() &&
                (unsigned long long)get + 4ull * (count - i) <= (unsigned long long)s_config.ioSize &&
                wp85_rsx_bulk(&s_rsx_state, m, noinc, hbase + (size_t)io + get, count - i)) {
                get += 4u * (count - i);
                break;
            }
            switch (m) {
                case 0x0050:                                   /* NV406E SET_REFERENCE */
                    wp64_label_store(GCM_CTRL_EA + 8, val);   /* winport v64-defer-vtx */
                    /* Task 5: RSX FENCE -- o valor de referencia que o titulo
                     * faz poll para saber que a GPU chegou aqui (GCM_CTRL_EA+8).
                     * Sem lock proprio deste ficheiro. */
                    PS3_TRACE(PS3_TS_RSX, "rsx_process_fifo", "FENCE", "val=0x%08X", val);
                    break;
                case 0x0064: case 0x0068: case 0x006C: {       /* NV406E semaphore: offset/acquire/release */
                    if (m == 0x006C) {   /* winport v92b-lblpoll */
                        __atomic_add_fetch(&wp92l_rel_n, 1ll, __ATOMIC_RELAXED); wp92l_rel_off = wp59_sem_off; wp92l_rel_val = val;
                        if (wp92l_sem406e()) wp64_label_store(GCM_LABEL_EA + wp59_sem_off, val);
                    }
                    /* PS3_TRACE_SEM_ACQ=1 (OFF, probe): the walker does not honour
                     * these. Counts ACQUIREs whose label was NOT yet at the value --
                     * each one is a draw we may run before the SPU finished writing
                     * its vertices (GoW2 Colossus: ring-buffered skinned stream). */
                    static int sq = -1;
                    static unsigned int sq_off = 0, n_acq = 0, n_unsat = 0, n_rel = 0;
                    if (sq < 0) { const char* e = getenv("PS3_TRACE_SEM_ACQ"); sq = (e && e[0] == '1') ? 1 : 0; }
                    if (sq) {
                        if (m == 0x0064) sq_off = val;
                        else if (m == 0x006C) n_rel++;
                        else {
                            const unsigned int cur = vm_read32((unsigned long long)GCM_LABEL_EA + sq_off);
                            n_acq++;
                            if (cur != val) {
                                n_unsat++;
                                if (n_unsat <= 20u)
                                    fprintf(stderr, "[SEMACQ] PENDENTE off=0x%X quer=0x%08X label=0x%08X\n",
                                            sq_off, val, cur);
                            }
                            if ((n_acq % 500u) == 0u) {
                                fprintf(stderr, "[SEMACQ] acquires=%u pendentes=%u releases=%u\n",
                                        n_acq, n_unsat, n_rel);
                                fflush(stderr);
                            }
                        }
                    }
                    break;
                }
                case 0x0010: case 0x1d6c:                       /* SEMAPHORE_OFFSET (offset 0 is valid) */
                    s_rsx_sema_off = val; break;
                case 0x0014: case 0x1d70: case 0x1d74:          /* SEMAPHORE_RELEASE -> publish to the
                    label/semaphore area the title polls (cellGcmGetLabelAddress);
                    offset is index*4 from the label base. */
                    wp64_label_store(GCM_LABEL_EA + s_rsx_sema_off,   /* winport v64-defer-vtx */
                                     m == 0x1d70u ? wp92b_be_value(s_rsx_sema_off, val) : val);   /* winport v92b-besem: 0x1D70 = back-end release, swizzled arg */
                    if (wp32_diag_on()) wp32_lbl_written(s_rsx_sema_off);   /* winport v32-gcmdiag-l */
                    /* Task 5: RSX LABEL -- o word do label/semaforo publicado
                     * (o mesmo que cellGcmGetLabelAddress devolve ao guest). */
                    PS3_TRACE(PS3_TS_RSX, "rsx_process_fifo", "LABEL",
                              "off=%u val=0x%08X", s_rsx_sema_off, val);
                    break;
                default: break;                                 /* render state: skipped */
            }
            /* Feed the real command processor (Layer 2) so state tracking +
             * clear/draw dispatch reach the registered backend. The walker's
             * own switch above still owns SYNC (ref/semaphore/label); Layer 2
             * treats those as unknown no-ops, so there is no double handling. */
            /* Only subchannel 0 is the NV4097 3D object; 2D objects (NV3062/
             * NV3089/NV309E, subchannels 1-7) have their own numbering. */
            if (bridge) {
                if (subch == 0u) rsx_process_method(&s_rsx_state, m, val);
                else gcm_2d_method(subch, m, val);
                /* PS3_RSX_CAPTURE: record the method AFTER its dispatch (one
                 * cached branch when the recorder is not recording). While
                 * recording, the backend interposer (rsx_capture.c) writes
                 * the guest memory a callback reads (FP ucode, textures,
                 * vertices, indices) as RANGE records during the dispatch --
                 * including draws a batching method flushes -- and the
                 * replay must apply those bytes BEFORE it replays the method
                 * that triggered the read. Presents / begin-frames never run
                 * inside rsx_process_method or gcm_2d_method (they happen at
                 * flip points between methods), so FRAME/PRESENT order
                 * relative to methods is unchanged by the move. */
                if (g_rsx_capture_state == 2) rsx_capture_on_method(subch, m, val);
            }
            /* Env-gated method histogram (PS3_RSX_STATS): accumulate every
             * method id; dumped from cellGcmSetFlipCommand every 500 flips.
             * Unlike the 80-line trace this never caps, so draw-path methods
             * show up even when they start thousands of commands in. */
            { static int st = -1; if (st < 0) st = getenv("PS3_RSX_STATS") ? 1 : 0;
              if (st && m < 0x4000) g_rsx_method_hist[m >> 2]++; }
            if (trace) {
                /* Always log draw-path methods (uncapped, small volume).
                 * General method stream still capped at 80 for noise. */
                int is_draw = (m == 0x1808u || m == 0x1814u || m == 0x1820u ||
                               m == 0x181Cu || m == 0x1824u || m == 0x1828u ||
                               m == 0x1830u || m == 0x1834u);
                if (is_draw || tn < 80) {
                    if (!is_draw) tn++;
                    static unsigned draw_n;
                    if (!is_draw || draw_n++ < 200u) {
                        fprintf(stderr, "[rsx] m=0x%04X val=0x%08X%s\n", m, val,
                                (m==0x50||m==0x10||m==0x14||m==0x1d6c||m==0x1d70||m==0x1d74)?" *SYNC":
                                (m==0x1808)?" *BEGIN_END":
                                (m==0x1814)?" *DRAW_ARRAYS":
                                (m==0x181C)?" *IDX_OFF":
                                (m==0x1820)?" *IDX_FMT":
                                (m==0x1824)?" *DRAW_INDEX":"");
                        fflush(stderr);
                    }
                }
            }
            get += 4;
        }
    }
    { u32 fid; while (bridge && gcm_flip_take_at(get, &fid)) { gcm_present_trace("drained", fid, get, get); gcm_flip_present_id(fid); } }   /* drained up to the flip */
    /* PS3_SHADER_DEMO: prova end-to-end de que o pipeline de shader dispara
     * set_shader. O jogo nao emite draws proprios (sem conteudo carregado,
     * gated atras da progressao da intro/movie FSM), entao injetamos um
     * fragment program NV40 REAL + um draw no mesmo s_rsx_state, apos a FIFO
     * do jogo. set_shader dispara de verdade e chega ao backend registrado
     * (verificavel no log). NAO e fabricacao: microcodigo NV40 real, pipeline
     * RSX real, disparo real. NAO substitui o render de conteudo do jogo. */
    {
        static int demo = -1;
        if (demo < 0) { const char* e = getenv("PS3_SHADER_DEMO"); demo = e ? 1 : 0; }
        if (demo && bridge) {
            unsigned int fp = (s_config.ioAddress ? s_config.ioAddress : 0x100000u) + 0x180000u;
            /* FP NV40 real (TEX r0, TC0, END); half-word swap p/ rsx_fp_read_word */
            static const unsigned int fpw[4] = {
                (0x17u<<24)|(0xFu<<9)|(0x4u<<13)|(0x0u<<17)|1u,
                1u|(0u<<9)|(1u<<11)|(2u<<13)|(3u<<15), 0u, 0u };
            int i; for (i = 0; i < 4; i++) {
                unsigned int h = fpw[i], be = (h << 16) | (h >> 16);
                vm_write32(fp + i*4, be);
            }
            rsx_process_method(&s_rsx_state, NV4097_SET_SHADER_PROGRAM, fp | 1u);
            rsx_process_method(&s_rsx_state, NV4097_SET_BEGIN_END, RSX_PRIMITIVE_TRIANGLES);
            rsx_process_method(&s_rsx_state, NV4097_DRAW_ARRAYS, (3u << 24) | 0u);
            rsx_process_method(&s_rsx_state, NV4097_SET_BEGIN_END, 0);
            { static int once = 0; if (!once) { once = 1;
                fprintf(stderr, "[SHADER_DEMO] FP NV40 real @0x%08X + draw injetado -> set_shader deve disparar\n", fp);
                fflush(stderr); } }
        }
    }
    /* The guard ended the walk with commands left. Inside the IO window that is just a long
     * backlog (a whole 2 MB lap after an overflow): stop there like at a wait point and let
     * the next call (or the wake thread) resume, instead of publishing get = put and
     * skipping the rest of the frame (2026-09-17: frames without their post chain). A get
     * outside the window is a desynced stream: resync at put as before. */
    int at_guard = 0;
    if (!at_waitpoint && get != put) {
        if (s_config.ioSize && get < s_config.ioSize) {
            at_guard = 1;
        } else {
            static u32 n_guard = 0;
            if (n_guard < 20u) {
                n_guard++;
                fprintf(stderr, "[FIFOSKIP] walker left the IO window at get=0x%X, resync at put=0x%X (src=%s)\n",
                        get, put, s_fifo_kick_src);
            }
        }
    }
    wp64_sync(0);   /* winport v64-defer-vtx: conversions done + queued labels out before get moves */
    wp59_store_be32(GCM_CTRL_EA + 4, (at_waitpoint || at_guard) ? get : put);   /* drained, or stopped with work left */
    __atomic_store_n(&s_fifo_more, at_guard ? 1 : 0, __ATOMIC_SEQ_CST);
    if (at_guard) gcm_rsx_wake_start_once();
    if (!at_waitpoint) __atomic_store_n(&s_fifo_wait_at, 0xFFFFFFFFu, __ATOMIC_SEQ_CST);
    if (wp32_diag_on()) wp32_after_walk(at_waitpoint, get, wp32_t0, wp32_src);   /* winport v32-gcmdiag */
    __sync_lock_release(&s_walking);
}

CellGcmControl* cellGcmGetControlRegister(void)
{
    /* Return a GUEST address (the game uses it as a guest pointer; returning the
     * host &s_control was read as a guest EA -> OOB). Minimal GPU: advance get to
     * put (FIFO consumed instantly) and bump ref, so the game's FIFO/ref waits
     * complete. */
    unsigned int put = vm_read32(GCM_CTRL_EA + 0);
    unsigned int get = vm_read32(GCM_CTRL_EA + 4);
    /* PS3_RSX_DUMP: dump the real FIFO command stream (now that GCM init sets
     * s_config.ioAddress to a real buffer) so the RSX methods can be decoded. */
    {
        static int dmp = -1;
        if (dmp < 0) { const char* e = getenv("PS3_RSX_DUMP"); dmp = e ? 1 : 0; }
        if (dmp && s_config.ioAddress && put > get) {
            static int n = 0;
            if (n < 3) {
                unsigned int io = s_config.ioAddress;
                fprintf(stderr, "[RSX] put=0x%X get=0x%X io=0x%08X\n", put, get, io);
                unsigned int end = (put - get) > 512u ? get + 512u : put;
                for (unsigned int o = get; o < end; o += 4) {
                    unsigned int v = vm_read32((unsigned long long)io + o);
                    if (v) fprintf(stderr, "  fifo[0x%04X]=0x%08X\n", o, v);
                }
                n++; fflush(stderr);
            }
        }
    }
    {
        static int fifo = -1;
        if (fifo < 0) { const char* e = getenv("PS3_RSX_FIFO"); fifo = e ? 1 : 0; }
        if (fifo && gcm_async_fifo_on()) {
            /* Campainha: o guest publicou o `put` e segue. Quem anda o stream e'
             * a thread do RSX -- como no console. Os sitios onde o guest precisa
             * mesmo do resultado (flip/overflow/label) continuam sincronos. */
            gcm_fifo_bell_ring();
        }
        else if (fifo) { gcm_fifo_kick(KS_CTRLREG); }                     /* parse + sync, sets get=put */
        else      vm_write32(GCM_CTRL_EA + 4, put);            /* legacy: instant consume */
    }
    vm_write32(GCM_CTRL_EA + 8, vm_read32(GCM_CTRL_EA + 8) + 1);/* ref++ */
    return (CellGcmControl*)(unsigned long long)GCM_CTRL_EA;
}

/* ---------------------------------------------------------------------------
 * Display / flip
 * -----------------------------------------------------------------------*/

/* NID: 0xDB23E867 */
u32 cellGcmGetCurrentField(void)
{
    /* Returns 0 or 1 for interlaced; always 0 for progressive. */
    return 0;
}

/* NID: 0xA53D12AE */
void cellGcmSetFlipMode(u32 mode)
{
    printf("[cellGcmSys] SetFlipMode(mode=%u)\n", mode);
    s_flip_mode = mode;
}

/* NID: 0xC44D8F34 */
void cellGcmSetWaitFlip(void)
{
    /*
     * Block until the current flip completes.  In the stub we consider
     * flips to be instantaneous.
     */
    s_flip_status = CELL_GCM_FLIP_STATUS_DONE;
}

/* NID: 0x51C9D62B */
void cellGcmResetFlipStatus(void)
{
    s_flip_status = CELL_GCM_FLIP_STATUS_WAITING;
}

/* NID: 0xE315A0B2 */
u32 cellGcmGetFlipStatus(void)
{
    /* Avoid host-calling the guest OPDs directly (they're not valid host
     * function pointers). Use cellGcmTickVBlank() / TickFlip() instead,
     * which dispatch via g_ps3_guest_caller — see below. */
    if (s_flip_status == CELL_GCM_FLIP_STATUS_WAITING) {
        s_flip_status = CELL_GCM_FLIP_STATUS_DONE;
        s_last_flip_time = get_timestamp_ns();
    }
    return s_flip_status;
}

/* Host-side ticks. The game's host driver (e.g. flow main.cpp) calls
 * these periodically so registered guest handlers fire — that's what
 * drives the title-screen state machine for many games. */
#include "ps3emu/guest_call.h"

void cellGcmTickVBlank(void)
{
    s_vblank_count++;
    if (s_vblank_handler_opd && g_ps3_guest_caller) {
        g_ps3_guest_caller(s_vblank_handler_opd,
                           (uint64_t)s_vblank_count, 0, 0, 0);
    }
}

void cellGcmTickFlip(void)
{
    if (s_flip_handler_opd && g_ps3_guest_caller) {
        g_ps3_guest_caller(s_flip_handler_opd, 1, 0, 0, 0);
    }
}

/* NID: 0xDC09357E */
s32 cellGcmSetDisplayBuffer(u32 bufferId, u32 offset, u32 pitch,
                            u32 width, u32 height)
{
    printf("[cellGcmSys] SetDisplayBuffer(id=%u, offset=0x%X, pitch=%u, %ux%u)\n",
           bufferId, offset, pitch, width, height);

    if (bufferId >= CELL_GCM_MAX_DISPLAY_BUFFER_NUM)
        return CELL_GCM_ERROR_INVALID_VALUE;

    {
        /* PS3_TRACE_FLIP (OFF): only when the slot actually changes — Full Frame
         * / resolution / pitch swaps. One-frame black flashes often sit on this. */
        static int flip_tr = -1;
        if (flip_tr < 0) {
            const char* e = getenv("PS3_TRACE_FLIP");
            flip_tr = (e && e[0] && e[0] != '0') ? 1 : 0;
        }
        const CellGcmDisplayInfo old = s_display_buffers[bufferId];
        const int was = s_display_buffer_set[bufferId];
        if (flip_tr && (!was || old.offset != offset || old.pitch != pitch ||
                        old.width != width || old.height != height)) {
            fprintf(stderr,
                    "[DISPLAY RECONFIG] id=%u set=%d old off=0x%X pitch=%u %ux%u "
                    "-> off=0x%X pitch=%u %ux%u\n",
                    bufferId, was, old.offset, old.pitch, old.width, old.height,
                    offset, pitch, width, height);
            fflush(stderr);
        }
    }

    s_display_buffers[bufferId].offset = offset;
    s_display_buffers[bufferId].pitch  = pitch;
    s_display_buffers[bufferId].width  = width;
    s_display_buffers[bufferId].height = height;
    s_display_buffer_set[bufferId] = 1;
    rsx_capture_on_display_buffer(bufferId, offset, pitch, width, height);

    return CELL_OK;
}

/* PS3_TRACE_MAINLOOP (default OFF): identify guest flip issuer + watermark at WAD.
 * HLE path: g_last_hle_nid + g_last_hle_caller_lr + guest stack EAs from ps3_hle_call.
 * Darwin: host backtrace + dladdr for first N flips / samples (R6 real issuer).
 * Win32: same + RtlCaptureStackBackTrace for first 3 flips. */
static int s_ml_on = -1;
static unsigned s_ml_flip_n = 0;
static unsigned s_ml_last_buf = 0;
static unsigned long long s_ml_last_lr = 0;
static unsigned s_ml_last_g[4] = {0, 0, 0, 0};
static unsigned s_ml_last_nid = 0;
/* unique guest lr histogram (cap 8) */
static unsigned long long s_ml_uniq_lr[8];
static unsigned s_ml_uniq_n[8];
static int s_ml_uniq_used = 0;
/* unique host issuer (func_* / best symbol) histogram (cap 12) */
static char s_ml_uniq_host[12][72];
static unsigned s_ml_uniq_host_n[12];
static int s_ml_uniq_host_used = 0;
static char s_ml_last_host[72] = "-";
static unsigned s_ml_last_nid_hist[8];
static unsigned s_ml_last_nid_hist_n[8];
static int s_ml_nid_hist_used = 0;

static int mainloop_trace_on(void)
{
    if (s_ml_on < 0) {
        const char* e = getenv("PS3_TRACE_MAINLOOP");
        s_ml_on = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return s_ml_on;
}

/* Extract unmangled-ish "func_XXXXXXXX" from C++ symbol, else copy short name. */
static void mainloop_sym_short(const char* sname, char* out, size_t out_sz)
{
    const char* s = sname ? sname : "?";
    const char* f;
    size_t i;
    if (s[0] == '_') s++;
    f = strstr(s, "func_");
    if (f) {
        /* mangled: _Z13func_00017ACCP11ppu_context → func_00017ACC */
        for (i = 0; i + 1 < out_sz && f[i] && f[i] != 'P' && f[i] != '(' &&
             !(f[i] == '_' && f[i + 1] == 'Z'); i++) {
            if (f[i] == 'P' && f[i + 1] >= '0' && f[i + 1] <= '9') break;
            out[i] = f[i];
        }
        /* stop at end of hex-ish name: func_ + 8 hex */
        if (i > 5) {
            size_t j = 5;
            while (j < i && ((out[j] >= '0' && out[j] <= '9') ||
                             (out[j] >= 'A' && out[j] <= 'F') ||
                             (out[j] >= 'a' && out[j] <= 'f')))
                j++;
            i = j;
        }
        out[i] = 0;
        if (i > 0) return;
    }
    /* fallback: first 64 chars of symbol */
    for (i = 0; i + 1 < out_sz && s[i] && i < 64; i++) out[i] = s[i];
    out[i] = 0;
    if (i == 0) { out[0] = '?'; out[1] = 0; }
}

static int mainloop_is_noise_sym(const char* short_sym)
{
    /* Frames we walk past when looking for the guest issuer. */
    if (!short_sym || !short_sym[0]) return 1;
    if (strstr(short_sym, "cellGcm")) return 1;
    if (strstr(short_sym, "ps3_hle")) return 1;
    if (strstr(short_sym, "ps3_import")) return 1;
    if (strstr(short_sym, "ps3_indirect")) return 1;
    if (strstr(short_sym, "mainloop")) return 1;
    if (strstr(short_sym, "DRAIN")) return 1;
    if (strcmp(short_sym, "?") == 0) return 1;
    return 0;
}

static void mainloop_note_host_issuer(const char* short_sym)
{
    int i, found = 0;
    if (!short_sym || !short_sym[0]) short_sym = "?";
    strncpy(s_ml_last_host, short_sym, sizeof(s_ml_last_host) - 1);
    s_ml_last_host[sizeof(s_ml_last_host) - 1] = 0;
    for (i = 0; i < s_ml_uniq_host_used; i++) {
        if (strcmp(s_ml_uniq_host[i], short_sym) == 0) {
            s_ml_uniq_host_n[i]++;
            found = 1;
            break;
        }
    }
    if (!found && s_ml_uniq_host_used < 12) {
        strncpy(s_ml_uniq_host[s_ml_uniq_host_used], short_sym,
                sizeof(s_ml_uniq_host[0]) - 1);
        s_ml_uniq_host[s_ml_uniq_host_used][sizeof(s_ml_uniq_host[0]) - 1] = 0;
        s_ml_uniq_host_n[s_ml_uniq_host_used] = 1;
        s_ml_uniq_host_used++;
    }
}

static void mainloop_note_nid(unsigned nid)
{
    int i, found = 0;
    s_ml_last_nid = nid;
    for (i = 0; i < s_ml_nid_hist_used; i++) {
        if (s_ml_last_nid_hist[i] == nid) {
            s_ml_last_nid_hist_n[i]++;
            found = 1;
            break;
        }
    }
    if (!found && s_ml_nid_hist_used < 8) {
        s_ml_last_nid_hist[s_ml_nid_hist_used] = nid;
        s_ml_last_nid_hist_n[s_ml_nid_hist_used] = 1;
        s_ml_nid_hist_used++;
    }
}

/* Called from movie_io open (WAD) so last flip watermark is correlated in-log. */
void cellGcmMainloopNote(const char* why)
{
    int i;
    if (!mainloop_trace_on())
        return;
    fprintf(stderr,
            "[MAINLOOP] NOTE %s last_flip=#%u buf=%u nid=0x%08X lr=0x%llX "
            "host=%s g={%08X,%08X,%08X,%08X}\n",
            why ? why : "?",
            s_ml_flip_n, s_ml_last_buf, s_ml_last_nid,
            s_ml_last_lr, s_ml_last_host,
            s_ml_last_g[0], s_ml_last_g[1], s_ml_last_g[2], s_ml_last_g[3]);
    fprintf(stderr, "[MAINLOOP] uniq_lr n=%d:", s_ml_uniq_used);
    for (i = 0; i < s_ml_uniq_used; i++)
        fprintf(stderr, " 0x%llX×%u", s_ml_uniq_lr[i], s_ml_uniq_n[i]);
    fprintf(stderr, "\n");
    fprintf(stderr, "[MAINLOOP] uniq_host n=%d:", s_ml_uniq_host_used);
    for (i = 0; i < s_ml_uniq_host_used; i++)
        fprintf(stderr, " %s×%u", s_ml_uniq_host[i], s_ml_uniq_host_n[i]);
    fprintf(stderr, "\n");
    fprintf(stderr, "[MAINLOOP] uniq_nid n=%d:", s_ml_nid_hist_used);
    for (i = 0; i < s_ml_nid_hist_used; i++)
        fprintf(stderr, " 0x%08X×%u", s_ml_last_nid_hist[i], s_ml_last_nid_hist_n[i]);
    fprintf(stderr, "\n");
    fflush(stderr);
}

/* Set by _cellGcmSetFlipCommand when GCM_FLIP_COMMAND went into the stream. */
static int s_flip_deferred = 0;

/* End the backend frame and present bufferId. The ONE present choke point:
 * both the FIFO's GCM_FLIP_COMMAND (in-stream / forced flips) and the
 * immediate HLE flip of cellGcmSetFlipCommand come through here, so the
 * PS3_RSX_CAPTURE recorder sees every present. trace_fn keeps each caller's
 * own PS3_TRACE function name. Returns 1 when b->present ran. */
static int gcm_bridge_present_from(u32 bufferId, const char* trace_fn)
{
    rsx_backend* b = rsx_get_backend();
    if (!b) return 0;
    int presented = 0;
    if (s_rsx_in_frame && b->end_frame) b->end_frame(b->userdata);
    if (b->present) {
        b->present(b->userdata, bufferId);
        PS3_TRACE(PS3_TS_RSX, trace_fn, "PRESENT", "buf=%u", bufferId);
        presented = 1;
    }
    s_rsx_in_frame = 0;
    rsx_capture_on_present(bufferId);
    return presented;
}
static void gcm_bridge_present(u32 bufferId)
{
    (void)gcm_bridge_present_from(bufferId, "rsx_process_fifo");
}

/* winport v59-async-rsx: the immediate (non-deferred) present of a flip call, under the RSX lock in async mode. */
static int gcm_bridge_present_from(u32 bufferId, const char* trace_fn);
static int wp59_bridge_present_locked(u32 bufferId)
{
    const int async = wp59_async_on();
    const int tk = async ? wp59_rsx_lock() : 0;
    const int r = gcm_bridge_present_from(bufferId, "cellGcmSetFlipCommand");
    if (async) {
        __atomic_add_fetch(&wp59_flips_presented, 1l, __ATOMIC_RELAXED);
        __atomic_add_fetch(t_wp59_rsx ? &wp59_pres_rsx : &wp59_pres_other, 1ll, __ATOMIC_RELAXED);
    }
    wp59_rsx_unlock(tk);
    return r;
}
/* NID: 0xEAA52F23 */
static volatile u32 s_winport_flip_requests = 0;  /* winport: GCM helpers for the vendored upstream D3D12 renderer */
static s32 gcm_set_flip_command(u32 bufferId);
s32 cellGcmSetFlipCommand(u32 bufferId)
{
    gcm_vsync_pace();
    return gcm_set_flip_command(bufferId);
}
static s32 gcm_set_flip_command(u32 bufferId)
{
    /* Minimal GPU: a real RSX completes the flip and the title polls for that.
     * Don't reject the flip when the display buffer wasn't registered (GoW2's
     * display-buffer setup lives in the init path that didn't fully run) -- clamp
     * an out-of-range id and proceed so the flip succeeds instead of returning
     * CELL_GCM_ERROR_INVALID_VALUE (which made GoW2's main() abort). */
    if (bufferId >= CELL_GCM_MAX_DISPLAY_BUFFER_NUM) bufferId = 0;

    /* Task 5 (bringup-instrumentacao-fase0): RSX FLIP_REQ -- o jogo pediu um
     * flip para bufferId. Handler HLE sincrono na thread do jogo, giant lock
     * ambiente, sem lock proprio deste ficheiro. FLIP_DONE/PRESENT ficam mais
     * abaixo nesta MESMA funcao, em pontos distintos (ver o comentario junto
     * a cada um) -- o plano de origem exige os tres eventos separados. */
    PS3_TRACE(PS3_TS_RSX, "cellGcmSetFlipCommand", "FLIP_REQ", "buf=%u", bufferId);

    /* Task 2: contador de last_flip= (ver declaracao acima -- gated a
     * SNAPSHOT, atomico) + checkpoint FIRST_FLIP (anchor verificado no
     * brief). ps3_checkpoint_fire() ja' faz o proprio gating de CHECKPOINTS;
     * o contador tem o gate PROPRIO de SNAPSHOT porque e' o unico campo que
     * o consome e corre sempre que a func corre, independente de
     * PS3_TRACE_MAINLOOP. */
    if (ps3_trace_on(PS3_TS_SNAPSHOT)) {
        __sync_fetch_and_add(&s_ps3_flip_count, 1);
    }
    ps3_checkpoint_fire(PS3_CP_FIRST_FLIP, "cellGcmSetFlipCommand", "buf=%u", bufferId);

    /* PS3_TRACE_MAINLOOP: guest/HLE/host issuer of flip; see cellGcmMainloopNote. */
    if (mainloop_trace_on()) {
        /* Match ppu_hle.cpp TLS symbols (uint64_t / uint32_t). */
        extern __thread unsigned long long g_last_hle_caller_lr;
        extern __thread unsigned int g_last_hle_caller_guest[4];
        extern __thread unsigned int g_last_hle_nid;
        int i, found = 0;
        int do_bt;
        s_ml_flip_n++;
        s_ml_last_buf = bufferId;
        s_ml_last_lr = (unsigned long long)g_last_hle_caller_lr;
        for (i = 0; i < 4; i++)
            s_ml_last_g[i] = (unsigned)g_last_hle_caller_guest[i];
        mainloop_note_nid((unsigned)g_last_hle_nid);
        for (i = 0; i < s_ml_uniq_used; i++) {
            if (s_ml_uniq_lr[i] == s_ml_last_lr) {
                s_ml_uniq_n[i]++;
                found = 1;
                break;
            }
        }
        if (!found && s_ml_uniq_used < 8) {
            s_ml_uniq_lr[s_ml_uniq_used] = s_ml_last_lr;
            s_ml_uniq_n[s_ml_uniq_used] = 1;
            s_ml_uniq_used++;
        }
        /* Host BT: first 12 flips + every 500th (issuer identity, not every frame). */
        do_bt = (s_ml_flip_n <= 12u) || ((s_ml_flip_n % 500u) == 0u);
        if (s_ml_flip_n <= 5u || (s_ml_flip_n % 500u) == 0u) {
            fprintf(stderr,
                    "[MAINLOOP] flip#%u buf=%u nid=0x%08X lr=0x%llX g={%08X,%08X,%08X,%08X}\n",
                    s_ml_flip_n, bufferId, s_ml_last_nid, s_ml_last_lr,
                    s_ml_last_g[0], s_ml_last_g[1], s_ml_last_g[2], s_ml_last_g[3]);
            fflush(stderr);
        }
#ifdef _WIN32
        if (do_bt) {
              void* bt[24]; unsigned long h;
              unsigned long got = RtlCaptureStackBackTrace(0, 24, bt, &h);
              uintptr_t mb = (uintptr_t)GetModuleHandleW(0);
              fprintf(stderr, "[MAINLOOP] flip#%u bt:", s_ml_flip_n);
              for (unsigned long bi = 0; bi < got; bi++)
                  fprintf(stderr, " +0x%llX",
                          (unsigned long long)((uintptr_t)bt[bi] - mb));
              fprintf(stderr, "\n");
              fflush(stderr);
              mainloop_note_host_issuer("win32_bt");
          }
#elif defined(__APPLE__) || defined(__linux__)
        if (do_bt) {
            void* bt[32];
            int n = backtrace(bt, 32);
            char issuer[72] = "?";
            int bi, have_issuer = 0;
            fprintf(stderr, "[MAINLOOP] flip#%u host_bt n=%d:", s_ml_flip_n, n);
            for (bi = 0; bi < n && bi < 16; bi++) {
                Dl_info info;
                char short_sym[72];
                memset(&info, 0, sizeof(info));
                if (dladdr(bt[bi], &info) && info.dli_sname) {
                    mainloop_sym_short(info.dli_sname, short_sym, sizeof(short_sym));
                    fprintf(stderr, " %s", short_sym);
                    if (!have_issuer && !mainloop_is_noise_sym(short_sym)) {
                        strncpy(issuer, short_sym, sizeof(issuer) - 1);
                        issuer[sizeof(issuer) - 1] = 0;
                        have_issuer = 1;
                    }
                } else {
                    fprintf(stderr, " %p", bt[bi]);
                }
            }
            fprintf(stderr, " | issuer=%s\n", issuer);
            fflush(stderr);
            mainloop_note_host_issuer(issuer);
        }
#endif
    }

    {   /* winport release: one line per flip (up to 300/s) made the log grow by MBs per minute; only the
         * first 16 are logged, PS3_TRACE_FLIP=1 logs every flip as before. */
        static int tf = -1; static unsigned nflip;
        if (tf < 0) { const char* e = getenv("PS3_TRACE_FLIP"); tf = (e && e[0] == '1') ? 1 : 0; }
        if (tf || nflip < 16) { nflip++; printf("[cellGcmSys] SetFlipCommand(bufferId=%u)\n", bufferId); }
    }
    s_winport_flip_requests++;   /* winport: renderer frame boundary */
    wp96c_pace();   /* winport v96c-ppupace */
#ifdef _WIN32
    {   /* winport v20: guest flip rate, once a second */
        static LARGE_INTEGER fq, t0, tl; static unsigned n, slow; static double gmax;
        LARGE_INTEGER q; QueryPerformanceCounter(&q);
        if (!fq.QuadPart) { QueryPerformanceFrequency(&fq); t0 = tl = q; }
        double gap = 1000.0 * (double)(q.QuadPart - tl.QuadPart) / (double)fq.QuadPart;
        tl = q; n++; if (gap > gmax) gmax = gap; if (gap > 50.0) slow++;
        if (q.QuadPart - t0.QuadPart >= fq.QuadPart) {
            fprintf(stderr, "[FLIP1S] T=%llu guest flips/s=%.1f max_gap=%.1fms gaps>50ms=%u\n",
                    (unsigned long long)GetTickCount64(), (double)n * (double)fq.QuadPart / (double)(q.QuadPart - t0.QuadPart), gmax, slow);
            n = 0; slow = 0; gmax = 0; t0 = q;
        }
    }
#endif

    s_current_display_buffer_id = bufferId;
    s_flip_status = CELL_GCM_FLIP_STATUS_DONE;
    /* Task 5: RSX FLIP_DONE -- o ponto em que este HLE declara o flip
     * concluido (s_flip_status passa a DONE; cellGcmGetFlipStatus/
     * cellGcmSetWaitFlip passam a ver isto). Distinto de FLIP_REQ (acima,
     * pedido) e de PRESENT (abaixo, swap real no backend host). */
    PS3_TRACE(PS3_TS_RSX, "cellGcmSetFlipCommand", "FLIP_DONE", "buf=%u", bufferId);
    s_last_flip_time = get_timestamp_ns();
    s_vblank_count++;
    /* Flip completion is published by the FIFO semaphore-release (the title's
     * GPU fence), handled in rsx_process_fifo -> GCM_LABEL_EA. No direct label
     * write here (it would race the fence value the title polls for). */

    /* s_flip_handler is a GUEST OPD address stashed in a host-typed pointer
     * (2026-07-01) -- calling it directly jumps the host CPU to whatever
     * small guest code address the game registered (observed: crash at
     * RIP==guest OPD, e.g. 0x521B68), since that address was never a valid
     * host function. Dispatch through g_ps3_guest_caller instead, the same
     * mechanism cellGcmTickFlip() already uses correctly for this same
     * handler. */
    if (s_flip_handler_opd && g_ps3_guest_caller)
        g_ps3_guest_caller(s_flip_handler_opd, 0, 0, 0, 0);  /* head 0 = primary display */

    /* PS3_RSX_STATS: dump the accumulated method histogram every 500 flips */
    { static int st = -1; if (st < 0) st = getenv("PS3_RSX_STATS") ? 1 : 0;
      if (st) { static unsigned flips = 0;
          if ((++flips % 500) == 0) {
              fprintf(stderr, "[rsx-stats] flip #%u — method histogram (nonzero):\n", flips);
              for (unsigned mi = 0; mi < 0x1000; mi++)
                  if (g_rsx_method_hist[mi])
                      fprintf(stderr, "  m=0x%04X count=%llu\n", mi << 2, g_rsx_method_hist[mi]);
              fflush(stderr);
          } } }

    /* VRAM occupancy mid/late (PS3_TRACE_VRAM / TEX_PIXELS). phase 1 @200, 2 @800. */
    {
        static int v = -1;
        if (v < 0) {
            const char* e = getenv("PS3_TRACE_VRAM");
            const char* e2 = getenv("PS3_TRACE_TEX_PIXELS");
            v = ((e && e[0] && e[0] != '0') || (e2 && e2[0] && e2[0] != '0')) ? 1 : 0;
        }
        if (v) {
            static unsigned vflips = 0;
            vflips++;
            if (vflips == 200u || vflips == 800u) {
                extern void ps3_scan_vram_occupancy(int phase);
                ps3_scan_vram_occupancy(vflips == 200u ? 1 : 2);
            }
        }
    }

    /* Drain FIFO on every flip so draws recorded this frame reach present.
     * Previously FIFO only ran on label polls; after content-hold the title
     * can flip with an empty draw list while commands sit unprocessed. */
    {
        static int fifo = -1;
        if (fifo < 0) {
            const char* e = getenv("PS3_RSX_FIFO");
            fifo = e ? 1 : 0;
        }
        if (fifo) { gcm_fifo_kick(KS_FLIP); }
    }

    /* RSX bridge: a flip ends the frame -- present what the backend accumulated
     * and pump the host window so it stays responsive. No-op when off. */
    if (rsx_bridge_mode()) {
        const int deferred = s_flip_deferred;
        s_flip_deferred = 0;   /* in-stream flip: presented when the FIFO reaches it */
        /* end_frame + present go through the single present choke point
         * (gcm_bridge_present_from), which also emits this function's
         * PRESENT trace below and feeds the PS3_RSX_CAPTURE recorder. It
         * returns 1 exactly when the old inline block reached b->present. */
        if (!deferred && wp59_bridge_present_locked(bufferId)) {   /* winport v59-async-rsx */
            /* (PS3_TRACE "cellGcmSetFlipCommand" "PRESENT" is emitted
             * inside gcm_bridge_present_from, right after b->present.)
             * Task 5 (bringup-instrumentacao-fase0): RSX PRESENT -- o
             * swap real no backend host, generico por vtable (qualquer
             * backend registado). Em headless (PS3_NO_RSX=1) isto pode
             * NUNCA disparar SE nenhum backend real for registado -- mas
             * ver a nota do FIRST_PRESENT logo abaixo: nos runs medidos
             * este repo regista um backend real mesmo com PS3_NO_RSX=1
             * (ordem de sourcing do env_gow2.sh), por isso PRESENT
             * dispara de facto nesses runs; um run sem PS3_RSX_BACKEND
             * nenhum definido teria s_rsx_mode nunca chegando aqui
             * (rsx_bridge_mode()==0), o que e' um facto a registar, nao
             * um bug. */
            /* Task 2: FIRST_PRESENT -- primeiro present concluido no
             * backend host (qualquer backend: d3d12/metal/null/sdl/vulkan,
             * dispatch generico via a vtable rsx_backend). Correcao
             * pos-Task 2 (review): o comentario original afirmava que
             * isto "nao dispara em runs headless PS3_NO_RSX=1" -- falso,
             * refutado pelos runs reais (in-boot, run 5 de
             * .superpowers/sdd/task-2-report.md). Causa raiz (confirmada
             * por leitura do codigo-fonte): PS3_NO_RSX so' controla a
             * escolha de janela do boot_macos.cpp (gow2-recomp/boot_macos.cpp
             * pick_backend() -> Backend::None); rsx_bridge_mode() aqui em
             * cellGcmSys.c e' um mecanismo INDEPENDENTE que le
             * PS3_RSX_BACKEND por si proprio e regista um rsx_backend
             * real sempre que essa variavel estiver definida, PS3_NO_RSX
             * ou nao. Nos runs do relatorio, env_gow2.sh e' sourced ANTES
             * do wrapper exportar PS3_NO_RSX=1, e o proprio env_gow2.sh so'
             * define PS3_RSX_BACKEND quando PS3_NO_RSX ainda NAO esta
             * definido -- por essa ordem, PS3_RSX_BACKEND=metal (default
             * Darwin) fica definido mesmo num run "headless", e
             * rsx_bridge_mode() regista o backend Metal de verdade
             * (rsx_metal_backend_init), que recebe draws/flips/presents
             * normalmente. PS3_NO_RSX=1 sozinho NAO impede o registo de
             * backend aqui -- so' desliga a janela que o proprio
             * boot_macos.cpp criaria. */
            ps3_checkpoint_fire(PS3_CP_FIRST_PRESENT, "cellGcmSetFlipCommand",
                               "buf=%u", bufferId);
        }
        if (s_rsx_mode == 2) rsx_null_backend_pump_messages();
        else if (s_rsx_mode == 3) rsx_d3d12_backend_pump_messages();
#ifdef __APPLE__
        else if (s_rsx_mode == 4) rsx_metal_backend_pump_messages();
#endif
    }

    return CELL_OK;
}

/* NID: 0xD01B570F */
s32 cellGcmSetFlipCommandWithWaitLabel(u32 bufferId, u32 labelIndex, u32 labelValue)
{
    if (labelIndex >= CELL_GCM_MAX_LABEL_COUNT)
        return CELL_GCM_ERROR_INVALID_VALUE;

    /* Wait until the label reaches the expected value (instant in HLE) */
    s_labels[labelIndex] = labelValue;

    return cellGcmSetFlipCommand(bufferId);
}

/* NID: 0xA2478CA3 */
s32 cellGcmSetPrepareFlip(void* ctx, u32 bufferId)
{
    (void)ctx;  /* command buffer context -- not used in HLE */

    if (bufferId >= CELL_GCM_MAX_DISPLAY_BUFFER_NUM)
        return CELL_GCM_ERROR_INVALID_VALUE;

    if (!s_display_buffer_set[bufferId])
        return CELL_GCM_ERROR_INVALID_VALUE;

    printf("[cellGcmSys] SetPrepareFlip(bufferId=%u)\n", bufferId);

    s_current_display_buffer_id = bufferId;
    s_flip_status = CELL_GCM_FLIP_STATUS_DONE;
    s_last_flip_time = get_timestamp_ns();

    /* See cellGcmSetFlipCommand: dispatch through g_ps3_guest_caller, not a
     * direct call through the raw guest OPD stashed in a host pointer type. */
    if (s_flip_handler_opd && g_ps3_guest_caller)
        g_ps3_guest_caller(s_flip_handler_opd, 0, 0, 0, 0);

    return CELL_OK;
}

/* NID: 0x1BFAB6EE */
CellGcmDisplayInfo* cellGcmGetDisplayBufferByFlipIndex(u32 index)
{
    u32 id = index % CELL_GCM_MAX_DISPLAY_BUFFER_NUM;
    return &s_display_buffers[id];
}

/* NID: 0x8BADE8BE */
u32 cellGcmGetCurrentDisplayBufferId(void)
{
    return s_current_display_buffer_id;
}

/* NID: 0xD9B7653E */
void cellGcmSetFlipHandler(CellGcmFlipHandler handler)
{
    /* `handler` is a guest OPD address — store as such and remember
     * the legacy host-typed value too for any callers still using
     * the old API style. Real dispatch goes through
     * cellGcmDispatchVBlank()/DispatchFlip() via g_ps3_guest_caller. */
    printf("[cellGcmSys] SetFlipHandler(opd=0x%08X)\n", (unsigned)(size_t)handler);
    s_flip_handler_opd = (u32)(size_t)handler;
    s_flip_handler = handler;
}

/* NID: 0xA547ADDE */
void cellGcmSetVBlankHandler(CellGcmVBlankHandler handler)
{
    printf("[cellGcmSys] SetVBlankHandler(opd=0x%08X)\n", (unsigned)(size_t)handler);
    s_vblank_handler_opd = (u32)(size_t)handler;
    s_vblank_handler = handler;
}

/* NID: 0xF9BFCDA3 */
void cellGcmSetSecondVHandler(CellGcmSecondVHandler handler)
{
    printf("[cellGcmSys] SetSecondVHandler(opd=0x%08X)\n", (unsigned)(size_t)handler);
    s_second_v_handler_opd = (u32)(size_t)handler;
    s_second_v_handler = handler;
}

/* NID: 0x0B4B62D5 */
void cellGcmSetUserHandler(CellGcmUserHandler handler)
{
    printf("[cellGcmSys] SetUserHandler(opd=0x%08X)\n", (unsigned)(size_t)handler);
    s_user_handler_opd = (u32)(size_t)handler;
    s_user_handler = handler;
}

/* NID: 0x21AC3697 */
u64 cellGcmGetLastFlipTime(void)
{
    return s_last_flip_time;
}

/* ---------------------------------------------------------------------------
 * Address translation / IO mapping
 * -----------------------------------------------------------------------*/

/* NID: 0x0E6B0DFF */
s32 cellGcmGetOffsetTable(CellGcmOffsetTable* table)
{
    if (!table)
        return CELL_GCM_ERROR_INVALID_VALUE;

    *table = s_offset_table;
    return CELL_OK;
}

/* NID: 0xDB769B32 */
/* The HLE ABI passes guest pointers in registers, so `offset` is a GUEST
 * effective address, not a host pointer. Write through vm_write32 (endian-
 * correct + bounds-checked) instead of dereferencing it as a host pointer
 * (that segfaulted: the game passes a guest stack address like 0x0FEFF770). */
extern void vm_write32(unsigned long long a, unsigned int v);

s32 cellGcmAddressToOffset(u32 address, u32* offset)
{
    u32 off_ea = (u32)(size_t)offset;   /* the register value IS the guest EA */
    if (!off_ea)
        return CELL_GCM_ERROR_INVALID_VALUE;

    /* Local memory: offset = address - localBase */
    if (address >= s_config.localAddress &&
        address < s_config.localAddress + s_config.localSize) {
        vm_write32(off_ea, address - s_config.localAddress);
        return CELL_OK;
    }

    /* IO-mapped main memory: consult offset table */
    u32 page = address >> 20;
    if (page < 65536 && s_io_address_table[page] != 0xFFFF) {
        u32 io_page = s_io_address_table[page];
        vm_write32(off_ea, (io_page << 20) | (address & 0xFFFFF));
        return CELL_OK;
    }

    /* Legacy fallback for initial IO region */
    if (s_config.ioAddress != 0 && address >= s_config.ioAddress &&
        address < s_config.ioAddress + s_config.ioSize) {
        vm_write32(off_ea, address - s_config.ioAddress);
        return CELL_OK;
    }

    printf("[cellGcmSys] WARNING: AddressToOffset failed for 0x%08X\n", address);
    vm_write32(off_ea, 0);
    return CELL_GCM_ERROR_FAILURE;
}

/* NID: 0x2A6FBA9C */
s32 cellGcmMapMainMemory(u32 ea, u32 size, u32* offset)
{
    if (!offset)
        return CELL_GCM_ERROR_INVALID_VALUE;

    /* Size must be 1MB aligned */
    if (size == 0 || (size & 0xFFFFF) != 0)
        return CELL_GCM_ERROR_INVALID_ALIGNMENT;

    /* EA must be 1MB aligned */
    if ((ea & 0xFFFFF) != 0)
        return CELL_GCM_ERROR_INVALID_ALIGNMENT;

    printf("[cellGcmSys] MapMainMemory(ea=0x%08X, size=0x%X)\n", ea, size);

    /* Find a free IO offset region (bump allocate from local_mem_allocated) */
    u32 io_offset = s_local_mem_allocated;
    s_local_mem_allocated += size;

    IoMapping* mapping = find_free_mapping();
    if (!mapping) {
        printf("[cellGcmSys] WARNING: no free IO mapping slots\n");
        return CELL_GCM_ERROR_FAILURE;
    }

    mapping->ea     = ea;
    mapping->io     = io_offset;
    mapping->size   = size;
    mapping->active = 1;
    s_io_mapping_count++;

    populate_offset_table(ea, io_offset, size);

    vm_write32((u32)(size_t)offset, io_offset);   /* offset is a GUEST EA (BE) */
    return CELL_OK;
}

/* NID: 0x5A41C10F */
s32 cellGcmMapEaIoAddress(u32 ea, u32 io, u32 size)
{
    /* Both EA and IO must be 1MB aligned */
    if ((ea & 0xFFFFF) != 0 || (io & 0xFFFFF) != 0)
        return CELL_GCM_ERROR_INVALID_ALIGNMENT;

    if (size == 0 || (size & 0xFFFFF) != 0)
        return CELL_GCM_ERROR_INVALID_ALIGNMENT;

    printf("[cellGcmSys] MapEaIoAddress(ea=0x%08X, io=0x%08X, size=0x%X)\n", ea, io, size);

    /* Check for overlap with existing mappings */
    if (find_mapping_by_ea(ea) != NULL)
        return CELL_GCM_ERROR_ADDRESS_OVERWRAP;

    IoMapping* mapping = find_free_mapping();
    if (!mapping)
        return CELL_GCM_ERROR_FAILURE;

    mapping->ea     = ea;
    mapping->io     = io;
    mapping->size   = size;
    mapping->active = 1;
    s_io_mapping_count++;

    populate_offset_table(ea, io, size);

    return CELL_OK;
}

/* NID: 0xDB23E867 (disambiguation by context) */
s32 cellGcmUnmapEaIoAddress(u32 ea)
{
    IoMapping* mapping = find_mapping_by_ea(ea);
    if (!mapping)
        return CELL_GCM_ERROR_FAILURE;

    printf("[cellGcmSys] UnmapEaIoAddress(ea=0x%08X)\n", ea);

    clear_offset_table(mapping->ea, mapping->io, mapping->size);
    mapping->active = 0;
    s_io_mapping_count--;

    return CELL_OK;
}

/* NID: 0x3B9BD5BD */
s32 cellGcmUnmapIoAddress(u32 io)
{
    IoMapping* mapping = find_mapping_by_io(io);
    if (!mapping)
        return CELL_GCM_ERROR_FAILURE;

    printf("[cellGcmSys] UnmapIoAddress(io=0x%08X)\n", io);

    clear_offset_table(mapping->ea, mapping->io, mapping->size);
    mapping->active = 0;
    s_io_mapping_count--;

    return CELL_OK;
}

/* Host-side IO→EA (for RSX vertex/index fetch). Returns 1 and *out_ea on hit. */
int gcm_io_offset_to_ea(u32 ioOffset, u32* out_ea)
{
    if (!out_ea) return 0;
    u32 page = ioOffset >> 20;
    if (page < 65536 && s_ea_address_table[page] != 0xFFFF) {
        u32 ea_page = s_ea_address_table[page];
        *out_ea = (ea_page << 20) | (ioOffset & 0xFFFFF);
        return 1;
    }
    /* Legacy: IO region starts at s_config.ioAddress with identity for low map. */
    if (s_config.ioAddress != 0 && ioOffset < s_config.ioSize) {
        *out_ea = s_config.ioAddress + ioOffset;
        return 1;
    }
    return 0;
}

/* NID: 0xC47D0812 */
s32 cellGcmIoOffsetToAddress(u32 ioOffset, u32* ea)
{
    if (!ea)
        return CELL_GCM_ERROR_INVALID_VALUE;

    u32 ea_g = (u32)(size_t)ea;   /* ea is a GUEST EA -> vm_write32 (BE) */
    u32 host_ea = 0;
    if (gcm_io_offset_to_ea(ioOffset, &host_ea)) {
        vm_write32(ea_g, host_ea);
        return CELL_OK;
    }

    printf("[cellGcmSys] WARNING: IoOffsetToAddress failed for 0x%08X\n", ioOffset);
    vm_write32(ea_g, 0);
    return CELL_GCM_ERROR_FAILURE;
}

/* ---------------------------------------------------------------------------
 * Label / report / timestamp
 * -----------------------------------------------------------------------*/

/* NID: 0x21397818 */
u32* cellGcmGetLabelAddress(u8 index)
{
    if (index >= CELL_GCM_MAX_LABEL_COUNT) {
        printf("[cellGcmSys] WARNING: GetLabelAddress index %u out of range\n", index);
        return NULL;
    }
    __atomic_add_fetch(&wp92l_poll[index], 1ll, __ATOMIC_RELAXED);   /* winport v92b-lblpoll */
    /* RSX tick: the title polls a label as a GPU fence (waits label[i]==value).
     * Drain the FIFO here so any pending label-write / semaphore commands execute
     * -- otherwise the fence never lands and the poll spins forever. */
    {
        static int fifo = -1;
        if (fifo < 0) { const char* e = getenv("PS3_RSX_FIFO"); fifo = e ? 1 : 0; }
        if (fifo) { gcm_fifo_kick(KS_LABEL); }
    }
    /* ----- bring-up: break producer-less label spin waits (PS3_GCM_LABEL_SPIN_BREAK=N) -----
     * Same shape as sys_semaphore_wait's PS3_SEM_SPIN_BREAK: some titles poll a
     * GCM label as a GPU fence (while (*cellGcmGetLabelAddress(i) != value) {}),
     * but nothing ever drives the real RSX FIFO (s_config.ioAddress is never
     * set -- GCM init is never called by this title's boot path), so the fence
     * never lands and the poll spins forever. After N consecutive polls of the
     * same index, nudge the label forward by one -- mimicking a GPU completion
     * incrementing its fence -- so an equality or >= wait can be satisfied and
     * the caller proceeds to the next wall. Off by default; polling a different
     * index resets the counter, so real GPU-driven fences are never touched. */
    {
        static int s_label_break = -1;
        if (s_label_break < 0) {
            const char* e = getenv("PS3_GCM_LABEL_SPIN_BREAK");
            s_label_break = (e && atoi(e) > 0) ? atoi(e) : 0;
        }
        if (s_label_break > 0) {
            static int s_spin_index = -1;
            static uint64_t s_spin_cnt = 0;
            if (index == s_spin_index) s_spin_cnt++;
            else { s_spin_index = index; s_spin_cnt = 1; }
            if (s_spin_cnt >= (uint64_t)s_label_break) {
                uint32_t addr = GCM_LABEL_EA + (unsigned)index * 4u;
                uint32_t v = vm_read32(addr) + 1;
                vm_write32(addr, v);
                static int logged = 0;
                if (logged < 20) {
                    fprintf(stderr, "[gcm-label-break] index=%u -> %u (spin broken)\n", index, v);
                    logged++;
                }
                s_spin_cnt = 0;
            }
        }
    }
    /* Return a GUEST address (host &s_labels was used as a guest EA -> OOB). */
    return (u32*)(unsigned long long)(GCM_LABEL_EA + (unsigned)index * (wp92l_lbl16() ? 16u : 4u));   /* winport v92b-lblpoll */
}

/* NID: 0x8572ADE4 */
CellGcmReportData* cellGcmGetReportDataAddress(u32 index)
{
    if (index >= CELL_GCM_MAX_REPORT_COUNT) {
        printf("[cellGcmSys] WARNING: GetReportDataAddress index %u out of range\n", index);
        return NULL;
    }
    /* Return a GUEST address (host &s_report_data was used as a guest EA -> OOB). */
    return (CellGcmReportData*)(unsigned long long)(GCM_REPORT_EA + index * 16u);
}

/* NID: 0x97FC4B73 */
u64 cellGcmGetTimeStamp(u32 index)
{
    if (index >= CELL_GCM_MAX_REPORT_COUNT)
        return 0;

    /*
     * On real hardware this reads the RSX timestamp for a given report index.
     * We return the host timestamp in nanoseconds as a reasonable approximation.
     */
    s_report_data[index].timestamp = get_timestamp_ns();
    return s_report_data[index].timestamp;
}

/* ---------------------------------------------------------------------------
 * Tile / Zcull configuration
 * -----------------------------------------------------------------------*/

/* NID: 0x0B4B62D5 */
s32 cellGcmSetTile(u8 index, u8 location, u32 offset, u32 size,
                   u32 pitch, u8 comp, u16 base, u8 bank)
{
    if (index >= CELL_GCM_MAX_TILE_COUNT)
        return CELL_GCM_ERROR_INVALID_VALUE;

    printf("[cellGcmSys] SetTile(index=%u, loc=%u, offset=0x%X, size=0x%X, pitch=%u)\n",
           index, location, offset, size, pitch);

    s_tiles[index].offset = offset;
    s_tiles[index].size   = size;
    s_tiles[index].pitch  = pitch;
    s_tiles[index].format = comp;
    s_tiles[index].base   = base;
    s_tiles[index].bank   = bank;
    s_tiles[index].bound  = 0;

    /* Build tile register value (simplified) */
    s_tiles[index].tile  = (location << 0) | (pitch << 8);
    s_tiles[index].limit = offset + size - 1;

    return CELL_OK;
}

/* NID: 0x06EDEA25 */
s32 cellGcmSetZcull(u8 index, u32 offset, u32 width, u32 height,
                    u32 cullStart, u32 zFormat, u32 aaFormat,
                    u32 zcullDir, u32 zcullFormat,
                    u32 sFunc, u32 sRef, u32 sMask)
{
    if (index >= CELL_GCM_MAX_ZCULL_COUNT)
        return CELL_GCM_ERROR_INVALID_VALUE;

    printf("[cellGcmSys] SetZcull(index=%u, offset=0x%X, %ux%u)\n",
           index, offset, width, height);

    s_zcull[index].offset      = offset;
    s_zcull[index].width       = width;
    s_zcull[index].height      = height;
    s_zcull[index].cullStart   = cullStart;
    s_zcull[index].zFormat     = zFormat;
    s_zcull[index].aaFormat    = aaFormat;
    s_zcull[index].zcullDir    = zcullDir;
    s_zcull[index].zcullFormat = zcullFormat;
    s_zcull[index].sFunc       = sFunc;
    s_zcull[index].sRef        = sRef;
    s_zcull[index].sMask       = sMask;
    s_zcull[index].bound       = 0;

    return CELL_OK;
}

/* NID: 0x2BB1F1E5 */
s32 cellGcmBindTile(u8 index)
{
    if (index >= CELL_GCM_MAX_TILE_COUNT)
        return CELL_GCM_ERROR_INVALID_VALUE;

    printf("[cellGcmSys] BindTile(index=%u)\n", index);
    s_tiles[index].bound = 1;
    return CELL_OK;
}

/* NID: 0x1F61F9D3 */
s32 cellGcmBindZcull(u8 index)
{
    if (index >= CELL_GCM_MAX_ZCULL_COUNT)
        return CELL_GCM_ERROR_INVALID_VALUE;

    printf("[cellGcmSys] BindZcull(index=%u)\n", index);
    s_zcull[index].bound = 1;
    return CELL_OK;
}

/* NID: 0x75843042 */
s32 cellGcmUnbindTile(u8 index)
{
    if (index >= CELL_GCM_MAX_TILE_COUNT)
        return CELL_GCM_ERROR_INVALID_VALUE;

    printf("[cellGcmSys] UnbindTile(index=%u)\n", index);
    s_tiles[index].bound = 0;
    return CELL_OK;
}

/* NID: 0xA093BE1C */
s32 cellGcmUnbindZcull(u8 index)
{
    if (index >= CELL_GCM_MAX_ZCULL_COUNT)
        return CELL_GCM_ERROR_INVALID_VALUE;

    printf("[cellGcmSys] UnbindZcull(index=%u)\n", index);
    s_zcull[index].bound = 0;
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Misc
 * -----------------------------------------------------------------------*/

/* NID: 0x107BF789 */
/* Real SDK ABI: uint32_t cellGcmGetTiledPitchSize(const uint32_t size) -- ONE
 * argument, returns the tiled pitch *directly in r3*. The previous 2-arg form
 * (status return + pitch via pointer) made callers read the return value as a
 * dimension, get CELL_OK/garbage, and compute multi-GB RSX buffer sizes
 * (size = pitch * height) -> AllocFixedRSX abort. */
u32 cellGcmGetTiledPitchSize(u32 size)
{
    /* Find the smallest valid tiled pitch that is >= size. */
    u32 i;
    for (i = 0; i < (u32)s_valid_pitch_count; i++) {
        if (s_valid_pitches[i] >= size) {
            fprintf(stderr, "[cellGcmSys] GetTiledPitchSize(0x%X) -> 0x%X\n", size, s_valid_pitches[i]);
            return s_valid_pitches[i];
        }
    }
    fprintf(stderr, "[cellGcmSys] GetTiledPitchSize(0x%X) -> 0x%X (max)\n", size, s_valid_pitches[s_valid_pitch_count - 1]);
    return s_valid_pitches[s_valid_pitch_count - 1];
}

/* NID: 0xBC982946 */
void cellGcmSetDebugOutputLevel(u32 level)
{
    printf("[cellGcmSys] SetDebugOutputLevel(level=%u)\n", level);
    s_debug_level = level;
}

/* ---------------------------------------------------------------------------
 * Additional functions needed by Tokyo Jungle
 * -----------------------------------------------------------------------*/

/* Notify data area — similar to report data, 16 bytes per slot */
static u8 s_notify_data[256 * 16];

void* cellGcmGetNotifyDataAddress(u32 index)
{
    if (index >= 256) return NULL;
    /* Return a GUEST address (host &s_notify_data was used as a guest EA -> OOB). */
    return (void*)(unsigned long long)(GCM_NOTIFY_EA + index * 16u);
}

/* Timestamp location — returns CELL_GCM_LOCATION_LOCAL or MAIN */
u32 cellGcmGetTimeStampLocation(u32 index, u32* location)
{
    (void)index;
    if (location) vm_write32((u32)(size_t)location, CELL_GCM_LOCATION_LOCAL); /* GUEST EA */
    return 0;
}

/* SetTileInfo — alternative to SetTile with same parameters */
s32 cellGcmSetTileInfo(u8 index, u8 location, u32 offset, u32 size,
                       u32 pitch, u8 comp, u16 base, u8 bank)
{
    /* Delegates to existing SetTile */
    return cellGcmSetTile(index, location, offset, size, pitch, comp, base, bank);
}

/* Default FIFO size — configures command buffer size before init */
static u32 s_default_fifo_size = 0x40000; /* 256KB default */

s32 cellGcmSetDefaultFifoSize(u32 size)
{
    printf("[cellGcmSys] SetDefaultFifoSize(0x%X)\n", size);
    s_default_fifo_size = size;
    return CELL_OK;
}

/* Internal flip commands. Real SDK signature is (CellGcmContextData* context,
 * uint8_t id, ...) -- the context pointer is the FIRST arg (r3) and the buffer
 * id is the SECOND (r4). The previous 1-arg form read the context as the buffer
 * id (GoW2: r3=0x0A050000 -> "bufferId" huge -> CELL_GCM_ERROR_INVALID_VALUE ->
 * the title's flip failed and main() aborted). */
/* PS3_TRACE_GCM_FLIP=1 (diagnostic, OFF by default): how the title's flips line up
 * with the FIFO -- put against the flip's stream offset at the call, flips still
 * pending when the next one arrives (presented early), flips presented by the drain
 * of their own call. The first 16 flips in detail, then one summary per 120 flips.
 * Only the HLE flip path touches these counters (PPU giant lock). */
static int gcm_flip_trace_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_TRACE_GCM_FLIP"); on = (e && e[0] && e[0] != '0') ? 1 : 0; }
    return on;
}
static struct { u32 calls, put_eq, put_lt, put_gt, forced, own_drain, waits; u64 first_ns, wait_ns; } s_ft;
/* Frames in flight before a flip call waits for the RSX (PS3_GCM_FLIP_INFLIGHT, 1..8, default 2)
 * and the longest wait before the oldest flip is presented anyway (PS3_GCM_FLIP_WAIT_MS,
 * 1..1000, default 100). */
/* A flip about to be presented without the RSX reaching it: where the walker is. */
static void gcm_flip_trace_forced(void)
{
    static u32 n = 0;
    if (n >= 40u) return;
    n++;
    u32 head_off = 0xFFFFFFFFu;
    gcm_flip_lock();
    if (__atomic_load_n(&s_flip_qn, __ATOMIC_SEQ_CST) > 0) head_off = s_flip_q[s_flip_q_head].off;
    gcm_flip_unlock();
    const u32 wait_at = __atomic_load_n(&s_fifo_wait_at, __ATOMIC_SEQ_CST);
    const u32 io = s_config.ioAddress;
    fprintf(stderr, "[GCMFLIP] forced #%u head_off=0x%X get=0x%X put=0x%X queued=%ld wait_at=0x%X word=0x%08X "
            "wrap_pending=%d wrap_at=0x%X label[0x%X]=0x%08X\n",
            n, head_off, vm_read32(GCM_CTRL_EA + 4), vm_read32(GCM_CTRL_EA + 0), gcm_flip_count(), wait_at,
            (wait_at != 0xFFFFFFFFu && io) ? vm_read32((unsigned long long)io + wait_at) : 0u,
            s_fifo_wrap_pending, s_fifo_wrap_at, s_rsx_sema_off,
            vm_read32((unsigned long long)GCM_LABEL_EA + s_rsx_sema_off));
}
static long gcm_flip_inflight(void)
{
    static long v = 0;
    if (v == 0) {
        const char* e = getenv("PS3_GCM_FLIP_INFLIGHT");
        char* end = NULL;
        long x = (e && *e) ? strtol(e, &end, 10) : 0;
        v = (e && *e && end && *end == '\0' && x >= 1 && x <= (long)GCM_FLIP_Q_CAP) ? x : (wp59_async_on() ? 1 : 2);   /* winport v59-async-rsx: 1 frame in flight */
    }
    return v;
}
static u64 gcm_flip_wait_ns(void)
{
    static u64 v = 0;
    if (v == 0) {
        const char* e = getenv("PS3_GCM_FLIP_WAIT_MS");
        char* end = NULL;
        long x = (e && *e) ? strtol(e, &end, 10) : 0;
        v = 1000000ull * (u64)((e && *e && end && *end == '\0' && x >= 1 && x <= 1000) ? x : 100);
    }
    return v;
}
static void gcm_flip_trace_call(u32 off, int forced)
{
    const u32 put = vm_read32(GCM_CTRL_EA + 0), get = vm_read32(GCM_CTRL_EA + 4);
    const u64 now = get_timestamp_ns();
    if (s_ft.calls == 0) s_ft.first_ns = now;
    s_ft.calls++;
    if (put == off) s_ft.put_eq++; else if (put < off) s_ft.put_lt++; else s_ft.put_gt++;
    if (forced) s_ft.forced++;
    if (s_ft.calls <= 16u)
        fprintf(stderr, "[GCMFLIP] #%u off=0x%X put=0x%X get=0x%X forced_prev=%d\n",
                s_ft.calls, off, put, get, forced);
    if ((s_ft.calls % 120u) == 0u) {
        const double ms = (double)(now - s_ft.first_ns) / 1.0e6 / 120.0;
        fprintf(stderr, "[GCMFLIP] 120 flips: put==off %u put<off %u put>off %u | "
                "presented early %u | by own drain %u | waited for RSX %u (%.1f ms total) | skipped %u | %.2f ms/flip\n",
                s_ft.put_eq, s_ft.put_lt, s_ft.put_gt, s_ft.forced, s_ft.own_drain,
                s_ft.waits, (double)s_ft.wait_ns / 1.0e6, s_flip_skipped, ms);
        s_flip_skipped = 0;
        s_ft.put_eq = s_ft.put_lt = s_ft.put_gt = s_ft.forced = s_ft.own_drain = s_ft.waits = 0;
        s_ft.wait_ns = 0;
        s_ft.first_ns = now;
    }
    fflush(stderr);
}

/* PS3_GCM_STREAM_DUMP=<draws> (diagnostic, OFF by default): once a presented frame had at
 * least <draws> indexed draws, the next 6 flip calls write the whole command buffer
 * [begin, end) to gcm_stream_<call>.bin in the working directory, with a header (previous
 * and current flip offsets, pending wrap), so the commands the title wrote for one frame
 * can be parsed offline independently of the FIFO walker. */
static void gcm_stream_dump(u32 begin, u32 end, u32 cur, u32 io)
{
    static long first = -2;
    static u32 calls = 0, prev_off = 0;
    if (first == -2) {
        const char* e = getenv("PS3_GCM_STREAM_DUMP");
        char* ep = NULL;
        long v = (e && *e) ? strtol(e, &ep, 10) : -1;
        first = (e && *e && ep && *ep == '\0' && v >= 0 && v <= 10000000) ? v : -1;
    }
    if (first < 0) return;
    static u32 armed_at = 0xFFFFFFFFu;
    const u32 n = calls++;
    const u32 off = (cur >= io) ? cur - io : 0u;
    if (armed_at == 0xFFFFFFFFu && s_dump_last_logical >= (u32)first) armed_at = n;
    if (armed_at != 0xFFFFFFFFu && n < armed_at + 6u && begin >= io && end > begin &&
        end - begin <= 0x4000000u && ppu_guest_range_committed(begin, end - begin)) {
        char name[64];
        snprintf(name, sizeof name, "gcm_stream_%06u.bin", n);
        FILE* f = fopen(name, "wb");
        if (f) {
            const u32 hdr[8] = { 0x534D4347u, n, prev_off, off, s_fifo_wrap_at,
                                 (u32)s_fifo_wrap_pending, begin - io, end - io };
            fwrite(hdr, sizeof hdr, 1, f);
            for (u32 a = begin; a + 4u <= end; a += 4u) {
                const u32 w = vm_read32(a);
                fwrite(&w, sizeof w, 1, f);
            }
            fclose(f);
            fprintf(stderr, "[GCMDUMP] call=%u last_logical=%u prev_off=0x%X off=0x%X wrap_at=0x%X pend=%d put=0x%X get=0x%X\n",
                    n, s_dump_last_logical, prev_off, off, s_fifo_wrap_at, s_fifo_wrap_pending,
                    vm_read32(GCM_CTRL_EA + 0), vm_read32(GCM_CTRL_EA + 4));
        }
    }
    prev_off = off;
}

s32 _cellGcmSetFlipCommand(u32 context, u32 bufferId)
{
    if (wp32_diag_on()) { WP32_ADD(wp32_flips, 1); wp32_main_tid = (unsigned)GetCurrentThreadId(); wp32_report(); }   /* winport v32-gcmdiag */
    /* RPCS3 _cellGcmSetFlipCommand -> gcmSetPrepareFlip<old_api>: the flip goes at
     * context->current in the stream. Recorded as a stream offset (see
     * gcm_flip_in_stream_on). Only the default context this HLE set up
     * (cellGcmInit -> GCM_CTX_EA); read-only access to its fields. The flip
     * command takes 8 bytes: with less room left before end the console would
     * wrap through the callback first, so the offset would not be the flip's --
     * that case keeps the immediate present. */
    gcm_vsync_pace();
    const u32 io = s_config.ioAddress;
    int traced = 0;
    if (gcm_flip_in_stream_on() && rsx_bridge_mode() && context == GCM_CTX_EA && io &&
        ppu_guest_range_committed(context, 16u)) {
        /* The previous flip is still pending when the title flushed only after that
         * flip call (GoW2 does: put < flip offset at the call). Let the RSX catch up with
         * everything flushed since, which presents it at its own offset; presenting it
         * before this drain showed the frame without its last commands and pushed them
         * into the next frame (98 of 120 menu flips, 2026-09-16 PS3_TRACE_GCM_FLIP). */
        if (gcm_flip_pending() && gcm_fifo_on()) { gcm_fifo_kick(KS_FLIPDRAIN); }
        const u32 id = bufferId < CELL_GCM_MAX_DISPLAY_BUFFER_NUM ? bufferId : 0u;
        const u32 begin = vm_read32(context + 0u);
        const u32 end = vm_read32(context + 4u);
        const u32 cur = vm_read32(context + 8u);
        gcm_stream_dump(begin, end, cur, io);
        if (begin >= io && cur >= begin && end > cur && end - cur >= 8u) {
            u32 old_id;
            int forced = 0;
            /* Frames in flight: with gcm_flip_inflight() flips still waiting for the RSX this
             * call waits, giant lock released, kicking the walker, like the console title
             * blocking on its flip-handler semaphore until the RSX executes a flip. Presenting
             * the oldest flip right away instead (the old single-slot queue) showed frames
             * whose composite the RSX had not reached yet -- GoW2's wait points keep it a few
             * segments behind. It waits only while the RSX has flushed commands left (get != put,
             * or a wait point to leave): GoW2 publishes put past a flip only after the flip call
             * returns, so with get == put the RSX cannot reach the flip before this call ends
             * (2026-09-17: every 100 ms stall had get == put < flip offset). A flip still
             * unreached after PS3_GCM_FLIP_WAIT_MS (a desynced stream, or a wait point only this
             * thread would release) is presented anyway, so presents never stall. */
            if (gcm_flip_count() >= gcm_flip_inflight()) {
                extern int ppu_giant_lock_held_by_caller(void);
                extern void ppu_giant_lock_release(void);
                extern void ppu_giant_lock_acquire(void);
                const u64 t0 = get_timestamp_ns();
                while (gcm_flip_count() >= gcm_flip_inflight()) {
                    if (vm_read32(GCM_CTRL_EA + 4) == vm_read32(GCM_CTRL_EA + 0) &&
                        __atomic_load_n(&s_fifo_wait_at, __ATOMIC_SEQ_CST) == 0xFFFFFFFFu)
                        break;   /* nothing flushed left: the title flushes after this call */
                    if (get_timestamp_ns() - t0 >= gcm_flip_wait_ns()) {
                        if (gcm_flip_trace_on()) gcm_flip_trace_forced();
                        if (!gcm_flip_take_any(&old_id)) break;
                        forced++;
                        if (g_rsx_capture_state == 2) rsx_capture_note_forced_present();
                        gcm_present_trace("forced", old_id, vm_read32(GCM_CTRL_EA + 4), 0xFFFFFFFFu);
                        wp59_present_locked(old_id);   /* winport v59-async-rsx */
                        continue;
                    }
                    if (ppu_giant_lock_held_by_caller()) {
                        ppu_giant_lock_release();
                        gcm_overflow_wait();
                        ppu_giant_lock_acquire();
                    } else {
                        gcm_overflow_wait();
                    }
                    if (gcm_fifo_on()) { gcm_fifo_kick(KS_FLIPWAIT); }
                }
                s_ft.wait_ns += get_timestamp_ns() - t0;
                s_ft.waits++;
                if (wp32_diag_on()) { WP32_ADD(wp32_flipw_n, 1); WP32_ADD(wp32_flipw_ns, get_timestamp_ns() - t0); }   /* winport v32-gcmdiag */
            }
            while (!gcm_flip_put(cur - io, id) && gcm_flip_take_any(&old_id)) {   /* full queue */
                if (gcm_flip_trace_on()) gcm_flip_trace_forced();
                forced++;
                if (g_rsx_capture_state == 2) rsx_capture_note_forced_present();
                gcm_present_trace("forced", old_id, vm_read32(GCM_CTRL_EA + 4), 0xFFFFFFFFu);
                wp59_present_locked(old_id);   /* winport v59-async-rsx */
            }
            gcm_present_trace("queued", id, vm_read32(GCM_CTRL_EA + 4), cur - io);
            s_flip_deferred = 1;
            if (gcm_flip_trace_on()) { gcm_flip_trace_call(cur - io, forced); traced = 1; }
        }
    }
    const s32 rc = gcm_set_flip_command(bufferId);
    if (traced && !gcm_flip_pending()) s_ft.own_drain++;
    return rc;
}

s32 _cellGcmSetFlipCommandWithWaitLabel(u32 context, u32 bufferId, u32 labelIndex, u32 labelValue)
{
    (void)context;
    return cellGcmSetFlipCommandWithWaitLabel(bufferId, labelIndex, labelValue);
}

/* ---------------------------------------------------------------------------
 * Additional functions (RPCS3 parity)
 * -----------------------------------------------------------------------*/

/* FIFO command buffer callback — called when put pointer wraps.
 * In recomp we don't have a real GPU consuming FIFO, so this is a no-op. */
/* Overflow do buffer de comandos: o callback que o `+0xC` do CellGcmContextData
 * aponta quando `current` chega ao `end` (gate PS3_GCM_CB; ledger E66).
 *
 * Vive AQUI e nao no ppu_loader porque precisa do `rsx_process_fifo()`, que e'
 * static deste ficheiro -- e porque a semantica e' de GCM, nao de despacho.
 *
 * Contrato do guest: devolver 0 = ha' espaco, continua a escrever; nao-zero =
 * desiste do append. Na consola o callback submete o que esta' no buffer e
 * espera que o GET passe a regiao antes de a reciclar. Aqui:
 *
 *   1. publica `put` = offset do `current`  (submete o que o guest escreveu);
 *   2. dreno do FIFO -- que no nosso runtime avanca o `get` ate' ao `put`;
 *   3. so' entao repoe `current = begin`.
 *
 * O passo 2 e' o que torna isto seguro com um backend RSX ligado: sem ele, dar
 * a volta sobrescrevia comandos por consumir. Com `PS3_NO_RSX=1` o dreno e'
 * barato e o efeito e' o mesmo wrap de antes.
 *
 * Devolve 0 sempre: depois do dreno ha' sempre espaco, porque o buffer inteiro
 * volta a estar livre. */
/* winport v60-async-seg: the RSX consumed the OLD-lap contents of [.., t) (ring offset) when the guest is in lap lg. */
static int wp60_consumed(u32 t, long lg)
{
    const long lr = __atomic_load_n(&wp60_lr, __ATOMIC_ACQUIRE);   /* first lr, then get (walker: get store, then lr++) */
    if (lr >= lg) return 1;
    if (lr != lg - 1) return 0;
    const u32 lap_end = wp60_lap_end[lr & 3];
    if (t >= lap_end) return 0;                                    /* the RSX must take the wrap first */
    const u32 g = vm_read32(GCM_CTRL_EA + 4);
    return g > t && g <= lap_end;
}
/* Same wrap registration as the v59 full-drain path (pending + queued next, CAS with the walker). */
static void wp60_register_wrap(u32 at, u32 to)
{
    if (__atomic_load_n(&s_fifo_wrap_pending, __ATOMIC_ACQUIRE)) {
        if (__atomic_load_n(&s_fifo_wrap_next_pending, __ATOMIC_ACQUIRE)) {
            static u32 n_ovw = 0;   /* a THIRD lap before the RSX took the first: one wrap is lost -> keep lr in step */
            if (n_ovw < 20u) { n_ovw++;
                fprintf(stderr, "[GCMSEG] wrap at 0x%X not reached yet (get=0x%X) when the next wrap at 0x%X came (lost)\n",
                        s_fifo_wrap_at, vm_read32(GCM_CTRL_EA + 4), at); }
            __atomic_add_fetch(&wp60_lr, 1l, __ATOMIC_RELEASE);
        }
        s_fifo_wrap_next_at = at;
        s_fifo_wrap_next_to = to;
        __atomic_store_n(&s_fifo_wrap_next_pending, 1, __ATOMIC_RELEASE);
        int one = 1;
        if (!__atomic_load_n(&s_fifo_wrap_pending, __ATOMIC_ACQUIRE) &&
            __atomic_compare_exchange_n(&s_fifo_wrap_next_pending, &one, 0, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            s_fifo_wrap_at = s_fifo_wrap_next_at;
            s_fifo_wrap_to = s_fifo_wrap_next_to;
            __atomic_store_n(&s_fifo_wrap_pending, 1, __ATOMIC_RELEASE);
        }
    } else {
        s_fifo_wrap_at = at;
        s_fifo_wrap_to = to;
        __atomic_store_n(&s_fifo_wrap_pending, 1, __ATOMIC_RELEASE);
    }
}
/* Returns 1 when handled (*rc set); 0 = fall back to the full-drain path (ctx->end restored to the ring end). */
static int wp60_ctx_overflow_seg(u32 ctx_ea, s32* rc)
{
    extern int ppu_giant_lock_held_by_caller(void);
    extern void ppu_giant_lock_release(void);
    extern void ppu_giant_lock_acquire(void);
    const u64 t0 = get_timestamp_ns();
    const u32 io = s_config.ioAddress;
    const u32 begin = vm_read32(ctx_ea + 0x0);
    const u32 cur = vm_read32(ctx_ea + 0x8);
    if (!wp60_ring_end) wp60_ring_end = vm_read32(ctx_ea + 0x4);
    const u32 rend = wp60_ring_end;
    static int nojump = -1;
    if (nojump < 0) { const char* e = getenv("PS3_GCM_CB_NOJUMP"); nojump = (e && *e && *e != '0') ? 1 : 0; }
    if (!io || nojump || begin < io || cur < begin || rend <= begin || cur > rend || rend - begin < 0x20000u) {
        if (rend > begin) vm_write32(ctx_ea + 0x4, rend);
        return 0;
    }
    u32 need = wp60_ovf_words;
    need = (need && need <= 0x10000u) ? need * 4u : 4u;
    const u32 seg = wp60_seg_bytes();
    const long lg0 = __atomic_load_n(&wp60_lg, __ATOMIC_ACQUIRE);
    u32 start;
    int wrapped = 0;
    if (cur + need + 8u > rend) {                  /* real ring end: wrap (the walker jumps at cur -> begin) */
        wp60_register_wrap(cur - io, begin - io);
        wp60_lap_end[lg0 & 3] = cur - io;
        wp59_store_be32(GCM_CTRL_EA + 0, cur - io);  /* put = end of the finished lap */
        __atomic_store_n(&wp60_lg, lg0 + 1, __ATOMIC_RELEASE);
        start = begin; wrapped = 1;
        __atomic_add_fetch(&wp60_wraps, 1ll, __ATOMIC_RELAXED);
    } else {                                        /* segment end inside the ring: keep writing at current */
        wp59_store_be32(GCM_CTRL_EA + 0, cur - io);
        start = cur;
    }
    gcm_fifo_kick(KS_OVERFLOW);                     /* async: bell */
    u32 span = seg < need + 64u ? need + 64u : seg;
    const u32 t_end = (rend - start > span) ? start + span : rend;
    const long lg = __atomic_load_n(&wp60_lg, __ATOMIC_ACQUIRE);
    static u64 stall_ns = 0;
    if (!stall_ns) { const char* e_ = getenv("PS3_GCM_OVF_STALL_MS"); long v_ = e_ ? atol(e_) : 0;
                     stall_ns = (u64)(v_ > 0 ? v_ : 15000) * 1000000ull; }
    u64 tp = get_timestamp_ns();
    long last_lr = __atomic_load_n(&wp60_lr, __ATOMIC_ACQUIRE);
    u32 last_get = vm_read32(GCM_CTRL_EA + 4);
    int waited = 0;
    while (!wp60_consumed(t_end - io, lg)) {
        waited = 1;
        {   const long lr_ = __atomic_load_n(&wp60_lr, __ATOMIC_ACQUIRE); const u32 g_ = vm_read32(GCM_CTRL_EA + 4);
            if (lr_ != last_lr || g_ != last_get) { last_lr = lr_; last_get = g_; tp = get_timestamp_ns(); } }
        if (get_timestamp_ns() - tp >= stall_ns) {
            const long long n = __atomic_add_fetch(&wp60_stalls, 1ll, __ATOMIC_RELAXED);
            if (n <= 20) fprintf(stderr, "[GCMSEG] RSX stalled (no progress for the stall limit) at get=0x%X lr=%ld lg=%ld seg [0x%X,0x%X) -- handing it out anyway\n",
                                 vm_read32(GCM_CTRL_EA + 4), __atomic_load_n(&wp60_lr, __ATOMIC_ACQUIRE), lg, start - io, t_end - io);
            break;
        }
        const int held = ppu_giant_lock_held_by_caller();
        if (held) ppu_giant_lock_release();
        gcm_overflow_wait();                        /* async: bell + wait for RSX progress */
        if (held) ppu_giant_lock_acquire();
        gcm_fifo_kick(KS_OVERFLOWWAIT);             /* async: bell */
    }
    vm_write32(ctx_ea + 0x8, start);                /* current */
    vm_write32(ctx_ea + 0x4, t_end);                /* end = this segment's end */
    const u64 dt = get_timestamp_ns() - t0;
    __atomic_add_fetch(&wp60_segs, 1ll, __ATOMIC_RELAXED);
    __atomic_add_fetch(&wp60_seg_wait_ns, (long long)dt, __ATOMIC_RELAXED);
    if (wp32_diag_on()) { WP32_ADD(wp32_ovf_n, 1); WP32_ADD(wp32_ovf_ns, dt); }   /* [GCMD] ovf stays comparable */
    {   static unsigned long n = 0; ++n;
        if (n <= 16 || (n & (n - 1)) == 0)
            fprintf(stderr, "[GCMSEG] #%lu cur=0x%X -> seg [0x%X,0x%X)%s need=%u lr=%ld lg=%ld get=0x%X put=0x%X wait=%.2fms%s\n",
                    n, cur - io, start - io, t_end - io, wrapped ? " WRAP" : "", need,
                    __atomic_load_n(&wp60_lr, __ATOMIC_ACQUIRE), lg, vm_read32(GCM_CTRL_EA + 4), vm_read32(GCM_CTRL_EA + 0),
                    (double)dt / 1e6, waited ? "" : " (no wait)"); }
    *rc = CELL_OK;
    return 1;
}
s32 ps3_gcm_ctx_overflow(u32 ctx_ea)
{
    if (wp60_seg_on()) { s32 rc60 = CELL_OK; if (wp60_ctx_overflow_seg(ctx_ea, &rc60)) return rc60; }   /* winport v60-async-seg */
    const u64 wp32_ot0 = get_timestamp_ns();   /* winport v32-gcmdiag */
    u32 begin   = vm_read32(ctx_ea + 0x0);
    u32 current = vm_read32(ctx_ea + 0x8);
    u32 io      = s_config.ioAddress;

    /* Wrap = the RSX continuing at `begin` once it reaches `current` (on the
     * console, a JUMP to begin written at current). Without it the next drain
     * resumed at the old get and read the rest of the ring -- the previous lap's
     * words -- as commands (E443: every desync sits at get=0x1FFxxx). Our
     * context has no reserved slot for that jump (E444: current == end ==
     * begin + 2 MB, so writing it touched the word after the buffer), so the
     * walker takes the wrap itself: s_fifo_wrap_at/_to, consumed when get
     * reaches it. PS3_GCM_CB_NOJUMP=1 restores the old wrap for A/B. */
    static int nojump = -1;
    if (nojump < 0) { const char* e = getenv("PS3_GCM_CB_NOJUMP"); nojump = (e && *e && *e != '0') ? 1 : 0; }
    if (io && current >= io && begin >= io && !nojump) {
        /* A lap with no flip in it (one frame > the 2 MB ring) publishes no put inside
         * the new lap, so the walker sits on get == put == the pending wrap point
         * without taking it (its loop runs only while get != put). Overwriting that
         * pending wrap here made it read the ring's leftovers as commands: measured
         * 2026-09-22 in gameplay -- a garbage CALL at io+0x914BC4, the RSX stuck there
         * for good, every flip timing out (3 fps), [DISPCHG] surface state full of
         * floats. Queue the new wrap instead; the walker takes both, in order. */
        if (__atomic_load_n(&s_fifo_wrap_pending, __ATOMIC_ACQUIRE)) {
            if (__atomic_load_n(&s_fifo_wrap_next_pending, __ATOMIC_ACQUIRE)) {
                static u32 n_ovw = 0;   /* a THIRD lap before the RSX took the first */
                if (n_ovw < 20u) {
                    n_ovw++;
                    fprintf(stderr, "[FIFOSKIP] wrap at 0x%X not reached yet (get=0x%X) when the next wrap at 0x%X came\n",
                            s_fifo_wrap_at, vm_read32(GCM_CTRL_EA + 4), current - io);
                }
            }
            s_fifo_wrap_next_at = current - io;
            s_fifo_wrap_next_to = begin - io;
            __atomic_store_n(&s_fifo_wrap_next_pending, 1, __ATOMIC_RELEASE);
            /* The walker may have taken the pending wrap between our test and the
             * queueing, and then nobody would promote this one: do it here. */
            int one = 1;
            if (!__atomic_load_n(&s_fifo_wrap_pending, __ATOMIC_ACQUIRE) &&
                __atomic_compare_exchange_n(&s_fifo_wrap_next_pending, &one, 0, 0,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                s_fifo_wrap_at = s_fifo_wrap_next_at;
                s_fifo_wrap_to = s_fifo_wrap_next_to;
                __atomic_store_n(&s_fifo_wrap_pending, 1, __ATOMIC_RELEASE);
            }
        } else {
            s_fifo_wrap_at = current - io;
            s_fifo_wrap_to = begin - io;
            if (wp59_async_on()) __atomic_store_n(&s_fifo_wrap_pending, 1, __ATOMIC_RELEASE);   /* winport v59-async-rsx */
            else s_fifo_wrap_pending = 1;
        }
    }
    if (io && current >= io)
        vm_write32(GCM_CTRL_EA + 0, current - io);   /* put = offset do current */
    gcm_fifo_kick(KS_OVERFLOW);                              /* avanca o get ate' ao put */
    if (wp32_diag_on() && io && current >= io) {   /* winport v32-gcmdiag: bytes of the lap still unwalked */
        const u32 g_ = vm_read32(GCM_CTRL_EA + 4), e_ = current - io, b_ = begin - io;
        WP32_ADD(wp32_ovf_left, (g_ <= e_ && g_ >= b_) ? (e_ - g_) : 0u);
    }
    /* The console callback waits for the RSX to reach the end of the lap before the title
     * writes over it again. The walker can still be behind here (wait points, a long
     * backlog), and recycling right away let the next lap overwrite commands it had not
     * run (2026-09-17: wraps still pending when the next one came, then frames without
     * their post chain). Wait with the giant lock released, kicking the walker; after
     * 250 ms recycle anyway so the title never hangs. */
    if (io && current >= io && begin >= io && !nojump) {
        extern int ppu_giant_lock_held_by_caller(void);
        extern void ppu_giant_lock_release(void);
        extern void ppu_giant_lock_acquire(void);
        static int unlock_fifo = -1;
        if (unlock_fifo < 0)
            unlock_fifo = ps3_gcm_overflow_fifo_unlocked_parse(getenv("PS3_GCM_OVF_UNLOCK_FIFO"));
        u64 t0 = get_timestamp_ns();
        int timed_out = 0;
        static u64 stall_ns = 0;   /* winport: mingw */ /* stall-based limit */
        if (!stall_ns) { const char* e_ = getenv("PS3_GCM_OVF_STALL_MS"); long v_ = e_ ? atol(e_) : 0;
                         stall_ns = (u64)(v_ > 0 ? v_ : 15000) * 1000000ull; }
        u32 last_get = vm_read32(GCM_CTRL_EA + 4);
        while (vm_read32(GCM_CTRL_EA + 4) != current - io) {
            { u32 g_ = vm_read32(GCM_CTRL_EA + 4);
              if (g_ != last_get) { last_get = g_; t0 = get_timestamp_ns(); } }
            if (get_timestamp_ns() - t0 >= stall_ns) { timed_out = 1; break; }
            const int held = ppu_giant_lock_held_by_caller();
            const int wd32 = wp32_diag_on(); u64 a32 = 0, b32 = 0, c32 = 0;   /* winport v32-gcmdiag */
            const int st32 = wd32 ? wp32_state() : 0;
            if (wd32) a32 = get_timestamp_ns();
            if (held) ppu_giant_lock_release();
            gcm_overflow_wait();
            if (wd32) b32 = get_timestamp_ns();
            /* Default (UNLOCK_FIFO off): reacquire then drain, fifo under lock.
             * PS3_GCM_OVF_UNLOCK_FIFO=1: drain unlocked, then reacquire. */
            if (held && !unlock_fifo) ppu_giant_lock_acquire();
            if (wd32) c32 = get_timestamp_ns();
            gcm_fifo_kick(KS_OVERFLOWWAIT);
            if (held && unlock_fifo) ppu_giant_lock_acquire();
            if (wd32) wp32_ovf_iter_note(st32, a32, b32, c32, get_timestamp_ns());
        }
        if (timed_out) {
            static u32 n_to = 0;
            if (n_to < 20u) {
                n_to++;
                fprintf(stderr, "[FIFOSKIP] overflow: RSX stalled at get=0x%X (no progress for the stall limit; wrap at 0x%X), recycling\n",
                        vm_read32(GCM_CTRL_EA + 4), current - io);
            }
        }
    }
    if (wp32_diag_on()) { WP32_ADD(wp32_ovf_n, 1); WP32_ADD(wp32_ovf_ns, get_timestamp_ns() - wp32_ot0); }   /* winport v32-gcmdiag */
    vm_write32(ctx_ea + 0x8, begin);                 /* recicla o buffer */

    {
        static unsigned long n = 0; ++n;
        if (n <= 16 || (n & (n - 1)) == 0)
            fprintf(stderr,
                    "[GCM-CB] #%lu overflow ctx=0x%08X current=0x%08X -> 0x%08X "
                    "(put=0x%X get=0x%X)\n",
                    n, ctx_ea, current, begin,
                    vm_read32(GCM_CTRL_EA + 0), vm_read32(GCM_CTRL_EA + 4));
    }
    return CELL_OK;
}

s32 cellGcmCallback(void* context, u32 count)
{
    (void)context;
    (void)count;
    return CELL_OK;
}

/* Map RSX local memory — returns the base address and size of local VRAM */
s32 cellGcmMapLocalMemory(u32* address, u32* size)
{
    if (!address || !size)
        return CELL_GCM_ERROR_INVALID_VALUE;

    /* address/size are GUEST EAs -> vm_write32 (BE), not raw host derefs. */
    vm_write32((u32)(size_t)address, s_config.localAddress);
    vm_write32((u32)(size_t)size,    s_config.localSize);

    printf("[cellGcmSys] MapLocalMemory(address=0x%08X, size=0x%X)\n",
           s_config.localAddress, s_config.localSize);
    return CELL_OK;
}

/* Shutdown RSX — clear all state */
void cellGcmTerminate(void)
{
    printf("[cellGcmSys] Terminate\n");

    s_gcm_initialized = 0;
    s_flip_mode   = CELL_GCM_DISPLAY_VSYNC;
    s_flip_status = CELL_GCM_FLIP_STATUS_DONE;
    s_debug_level = CELL_GCM_DEBUG_LEVEL0;
    s_vblank_count = 0;
    s_io_map_reserved = 0;
    s_default_fifo_mode = 0;
    s_user_command = 0;

    memset(s_display_buffers, 0, sizeof(s_display_buffers));
    memset(s_display_buffer_set, 0, sizeof(s_display_buffer_set));
    memset(&s_config, 0, sizeof(s_config));
    memset(&s_control, 0, sizeof(s_control));
    vm_write32(GCM_CTRL_EA + 0, 0);   /* guest control register: put=get=ref=0 */
    vm_write32(GCM_CTRL_EA + 4, 0);
    vm_write32(GCM_CTRL_EA + 8, 0);
    memset(s_io_mappings, 0, sizeof(s_io_mappings));
    memset(s_tiles, 0, sizeof(s_tiles));
    memset(s_zcull, 0, sizeof(s_zcull));
    memset(s_report_data, 0, sizeof(s_report_data));
    memset(s_labels, 0, sizeof(s_labels));
    memset(s_io_address_table, 0xFF, sizeof(s_io_address_table));
    memset(s_ea_address_table, 0xFF, sizeof(s_ea_address_table));

    s_flip_handler     = NULL;
    s_vblank_handler   = NULL;
    s_user_handler     = NULL;
    s_second_v_handler = NULL;
    s_graphics_handler = NULL;
    s_queue_handler    = NULL;
    s_local_mem_allocated = 0;
    s_io_mapping_count = 0;
    s_current_display_buffer_id = 0;
    s_last_flip_time = 0;
}

/* Return remaining IO map space (256MB total IO space minus already mapped) */
u32 cellGcmGetMaxIoMapSize(void)
{
    /* RSX has a 256MB IO address window */
    u32 total_io = 256 * 1024 * 1024;
    u32 used = 0;

    for (int i = 0; i < CELL_GCM_MAX_IO_MAPPINGS; i++) {
        if (s_io_mappings[i].active)
            used += s_io_mappings[i].size;
    }

    return (used < total_io) ? (total_io - used) : 0;
}

/* Reserve IO map space (pre-allocation for future mappings) */
s32 cellGcmReserveIoMapSize(u32 size)
{
    u32 max_size = cellGcmGetMaxIoMapSize();
    if (size > max_size - s_io_map_reserved)
        return CELL_GCM_ERROR_FAILURE;

    s_io_map_reserved += size;
    return CELL_OK;
}

/* Unreserve IO map space */
s32 cellGcmUnreserveIoMapSize(u32 size)
{
    if (size > s_io_map_reserved)
        return CELL_GCM_ERROR_FAILURE;

    s_io_map_reserved -= size;
    return CELL_OK;
}

/* Return incrementing VBlank counter */
u32 cellGcmGetVBlankCount(void)
{
    /* Approximate: increment each time queried (games use this for timing) */
    return s_vblank_count++;
}

/* Return tile info for a given index */
CellGcmTileInfo* cellGcmGetTileInfo(u8 index)
{
    if (index >= CELL_GCM_MAX_TILE_COUNT) {
        printf("[cellGcmSys] WARNING: GetTileInfo index %u out of range\n", index);
        return NULL;
    }
    return &s_tiles[index];
}

/* Return zcull info for a given index */
CellGcmZcullInfo* cellGcmGetZcullInfo(u8 index)
{
    if (index >= CELL_GCM_MAX_ZCULL_COUNT) {
        printf("[cellGcmSys] WARNING: GetZcullInfo index %u out of range\n", index);
        return NULL;
    }
    return &s_zcull[index];
}

/* Return display buffer info by index */
CellGcmDisplayInfo* cellGcmGetDisplayInfo(u32 index)
{
    if (index >= CELL_GCM_MAX_DISPLAY_BUFFER_NUM) {
        printf("[cellGcmSys] WARNING: GetDisplayInfo index %u out of range\n", index);
        return NULL;
    }
    return &s_display_buffers[index];
}

/* Set default FIFO mode (before init) */
void cellGcmInitDefaultFifoMode(s32 mode)
{
    printf("[cellGcmSys] InitDefaultFifoMode(mode=%d)\n", mode);
    s_default_fifo_mode = mode;
}

/* Set command buffer to defaults (reset put/get pointers) */
void cellGcmSetDefaultCommandBuffer(void)
{
    printf("[cellGcmSys] SetDefaultCommandBuffer\n");
    s_control.put = 0;
    s_control.get = 0;
    s_control.ref = 0;
}

/* Debug dump — no-op in recomp */
void cellGcmDumpGraphicsError(void)
{
    printf("[cellGcmSys] DumpGraphicsError (no-op)\n");
}

/* Default command word size: 0x400 (1024) words */
u32 cellGcmGetDefaultCommandWordSize(void)
{
    return 0x400;
}

/* Default segment word size: 0x100 (256) words */
u32 cellGcmGetDefaultSegmentWordSize(void)
{
    return 0x100;
}

/* Immediate flip — perform flip right now */
s32 cellGcmSetFlipImmediate(u32 bufferId)
{
    printf("[cellGcmSys] SetFlipImmediate(bufferId=%u)\n", bufferId);
    return gcm_set_flip_command(bufferId);   /* no vblank wait: that is what immediate means */
}

/* Set flip status directly */
void cellGcmSetFlipStatus(u32 status)
{
    s_flip_status = status;
}

/* Store graphics handler callback */
void cellGcmSetGraphicsHandler(CellGcmGraphicsHandler handler)
{
    printf("[cellGcmSys] SetGraphicsHandler(%p)\n", (void*)(size_t)handler);
    s_graphics_handler = handler;
}

/* Store queue handler callback */
void cellGcmSetQueueHandler(CellGcmQueueHandler handler)
{
    printf("[cellGcmSys] SetQueueHandler(%p)\n", (void*)(size_t)handler);
    s_queue_handler = handler;
}

/* Set second V frequency */
void cellGcmSetSecondVFrequency(u32 freq)
{
    printf("[cellGcmSys] SetSecondVFrequency(%u)\n", freq);
    s_second_v_frequency = freq;
}

/* Set VBlank frequency */
void cellGcmSetVBlankFrequency(u32 freq)
{
    printf("[cellGcmSys] SetVBlankFrequency(%u)\n", freq);
    s_vblank_frequency = freq;
}

/* Store user command value */
void cellGcmSetUserCommand(u32 cmd)
{
    s_user_command = cmd;
}

/* Invalidate a tile region (unbind + clear) */
s32 cellGcmSetInvalidateTile(u8 index)
{
    if (index >= CELL_GCM_MAX_TILE_COUNT)
        return CELL_GCM_ERROR_INVALID_VALUE;

    printf("[cellGcmSys] SetInvalidateTile(index=%u)\n", index);

    s_tiles[index].bound = 0;
    memset(&s_tiles[index], 0, sizeof(CellGcmTileInfo));
    return CELL_OK;
}

/* EA IO address remap — stub, sorting not needed in recomp */
void cellGcmSortRemapEaIoAddress(void)
{
    /* No-op: IO address remapping not required for recomp */
}

/* Report data with location parameter */
CellGcmReportData* cellGcmGetReportDataAddressLocation(u32 index, u32 location)
{
    (void)location;  /* We only have one report area */

    if (index >= CELL_GCM_MAX_REPORT_COUNT) {
        printf("[cellGcmSys] WARNING: GetReportDataAddressLocation index %u out of range\n", index);
        return NULL;
    }
    return &s_report_data[index];
}

/* Get report value at index with location */
u32 cellGcmGetReportDataLocation(u32 index, u32 location)
{
    (void)location;

    if (index >= CELL_GCM_MAX_REPORT_COUNT)
        return 0;

    return s_report_data[index].value;
}

/* Task 2 (bringup-instrumentacao-fase0): contador de flips bem sucedidos,
 * para o campo last_flip= do subsistema SNAPSHOT. Gated a SNAPSHOT -- ver
 * s_ps3_flip_count acima. Leitura atomica (review final de branch, achado
 * Minor #4): o escritor usa __sync_fetch_and_add; __atomic_load_n alinha
 * este accessor com o mesmo tratamento que ps3_ppu_thread_alive_count()
 * (runtime/syscalls/sys_ppu_thread.c) ja' recebe para o seu contador. */
u32 ps3_gcm_flip_count(void)
{
    return __atomic_load_n(&s_ps3_flip_count, __ATOMIC_RELAXED);
}

/* --- PS3_RSX_CAPTURE providers (rsx_capture.c recorder; called only while
 * the probe is armed/recording, never on the OFF path) --- */
const struct rsx_state* gcm_capture_rsx_state(void) { return &s_rsx_state; }

/* io_to_ea = s_ea_address_table: populate_offset_table writes
 * s_ea_address_table[io >> 20] = ea >> 20 (IO offset -> EA, 1 MiB pages). */
void gcm_capture_io_info(u32* io_address, u32* io_size, const u16** io_to_ea)
{
    *io_address = s_config.ioAddress;
    *io_size    = s_config.ioSize;
    *io_to_ea   = s_ea_address_table;
}

void gcm_capture_local_info(u32* local_address, u32* local_size)
{
    *local_address = s_config.localAddress;
    *local_size    = s_config.localSize;
}

void gcm_capture_display_table(void (*emit)(u32 id, u32 off, u32 pitch, u32 w, u32 h))
{
    for (u32 i = 0; i < CELL_GCM_MAX_DISPLAY_BUFFER_NUM; i++)
        if (s_display_buffer_set[i])
            emit(i, s_display_buffers[i].offset, s_display_buffers[i].pitch,
                 s_display_buffers[i].width, s_display_buffers[i].height);
}

/* winport: GCM helpers for the vendored upstream D3D12 renderer */
u32 cellGcmResolveIO(u32 offset)
{
    u32 ea = 0;
    return gcm_io_offset_to_ea(offset, &ea) ? ea : 0;
}
u32 cellGcmResolveOffset(u32 offset)
{
    u32 ea = 0;
    if (gcm_io_offset_to_ea(offset, &ea)) return ea;
    return s_config.localAddress + offset;
}
u32 cellGcmResolveLocated(int local, u32 offset)
{
    if (local) return s_config.localAddress + offset;
    return cellGcmResolveOffset(offset);
}
int cellGcmOffsetIsDisplay(u32 offset)
{
    for (int i = 0; i < CELL_GCM_MAX_DISPLAY_BUFFER_NUM; i++)
        if (s_display_buffer_set[i] && s_display_buffers[i].offset == offset)
            return 1;
    return 0;
}
u32 cellGcm_flip_request_count(void) { return wp59_async_on() ? (u32)wp59_flips_presented : s_winport_flip_requests; }   /* winport v59-async-rsx */
u32 rsx_find_vram_upload(u32 want) { (void)want; return 0; }
void rsx_reset_upload_claims(void) {}
