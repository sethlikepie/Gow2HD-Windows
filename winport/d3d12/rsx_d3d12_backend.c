/* winport: adapted to the gow2 fork's rsx_state (adapt_backend.py) */
/*
 * ps3recomp - D3D12 RSX Backend
 *
 * Translates RSX GPU state to D3D12 rendering commands.
 *
 * Phase 1 implementation:
 *   - Win32 window + D3D12 device + swap chain
 *   - Clear render target to RSX clear color
 *   - Present with vsync
 *   - Basic vertex-colored triangle rendering
 *
 * This file is C with COM calls (D3D12 is a COM API).
 * We use the C interface (__uuidof not available in C, so we
 * define GUIDs manually).
 */

#ifdef _WIN32

#include <time.h>
int wp_stats_on(void);   /* winport release: WINPORT_STATS=1 logs the per-second statistics (win_compat.c) */
#include "rsx_d3d12_backend.h"
#include "rsx_primitives.h"
#include "rsx_vertex_fetch.h"
#include "rsx_texture_layout.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* D3D12 headers */
#include <d3d12.h>
#include <d3d12sdklayers.h>   /* ID3D12Debug / ID3D12InfoQueue */
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <limits.h>

/* winport-fix: on-disk shader cache */
static HRESULT wp_D3DCompile(LPCVOID src, SIZE_T len, LPCSTR name, const D3D_SHADER_MACRO* defs,
                             ID3DInclude* inc, LPCSTR entry, LPCSTR target, UINT f1, UINT f2,
                             ID3DBlob** code, ID3DBlob** err)
{
    static int init = 0; static char dir[512];
    if (!init) {
        init = 1;
        const char* e = getenv("WINPORT_SHADER_CACHE");
        if (e && e[0] == '0' && !e[1]) dir[0] = 0;
        else {
            snprintf(dir, sizeof dir, "%s", (e && *e) ? e : "shader_cache");
            CreateDirectoryA(dir, NULL);
        }
    }
    if (!dir[0] || defs || inc || !src || !code)
        return D3DCompile(src, len, name, defs, inc, entry, target, f1, f2, code, err);
    unsigned long long h = 1469598103934665603ull;
    const unsigned char* b = (const unsigned char*)src;
    for (SIZE_T i = 0; i < len; i++) { h ^= b[i]; h *= 1099511628211ull; }
    const char* parts[2] = { entry ? entry : "", target ? target : "" };
    for (int k = 0; k < 2; k++)
        for (const char* c = parts[k]; ; c++) { h ^= (unsigned char)*c; h *= 1099511628211ull; if (!*c) break; }
    h ^= ((unsigned long long)f1 << 32) ^ f2 ^ 0x57500001ull; h *= 1099511628211ull;
    char path[600];
    snprintf(path, sizeof path, "%s/%016llx.dxbc", dir, h);
    FILE* f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        ID3DBlob* blob = NULL;
        if (n > 0 && SUCCEEDED(D3DCreateBlob((SIZE_T)n, &blob)) && blob &&
            fread(blob->lpVtbl->GetBufferPointer(blob), 1, (size_t)n, f) == (size_t)n) {
            fclose(f);
            *code = blob;
            if (err) *err = NULL;
            return S_OK;
        }
        if (blob) blob->lpVtbl->Release(blob);
        fclose(f);
    }
    HRESULT hr = D3DCompile(src, len, name, defs, inc, entry, target, f1, f2, code, err);
    if (SUCCEEDED(hr) && *code) {
        char tmp[620];
        snprintf(tmp, sizeof tmp, "%s.%lu.tmp", path, (unsigned long)GetCurrentThreadId());
        FILE* o = fopen(tmp, "wb");
        if (o) {
            size_t n = (*code)->lpVtbl->GetBufferSize(*code);
            int ok = fwrite((*code)->lpVtbl->GetBufferPointer(*code), 1, n, o) == n;
            fclose(o);
            if (!ok || !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) DeleteFileA(tmp);
        }
    }
    return hr;
}
#define D3DCompile wp_D3DCompile

/* We need these GUIDs — define them here to avoid uuid.lib dependency */
#include <initguid.h>

/* Link libraries */
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

/* ---------------------------------------------------------------------------
 * Constants
 * -----------------------------------------------------------------------*/

/* winport v58c-threebuf: 2 buffers (double buffering), 3 with WINPORT_FPS_CAP (fix_v58c_threebuf_renderer.py) */
#define FRAME_COUNT_MAX     3
static u32 wp58_nbuf(void);
#define FRAME_COUNT         (wp58_nbuf())

/* winport v58-fpscap: WINPORT_FPS_CAP=N -> tearing present + own N fps limiter (fix_v58_fpscap_renderer.py) */
static int s_wp58_tear;
static int wp58_nopresent(void)   /* winport v58d-nopresent */
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_BENCH_NOPRESENT"); v = (e && e[0] == '1') ? 1 : 0;
    if (v) fprintf(stderr, "[FPSCAP58] WINPORT_BENCH_NOPRESENT=1: frames rendered, never presented (bench pacing = own limiter only)\n"); }
  return v; }
static int s_wp58_skipped;   /* winport v58b-iconic: last frame was rendered without Present */

/* ---- winport v68-res-scale (fix_v68_res_scale_renderer.py) ----
 * WINPORT_RES_SCALE=S: RSX colour surfaces, the shared DSV, ZSNAP snapshots and the back buffer are created at S x their
 * guest size. All bookkeeping (OffRT.w/h, draw records, guest viewports, texture sizes) stays in GUEST pixels; sizes are
 * multiplied only at the D3D12 call sites (resource creation, viewports/scissors, copy boxes, readbacks).
 * WINPORT_OUT_W/H, WINPORT_WINDOW_SCALE, WINPORT_BORDERLESS: window client size (DXGI STRETCH scales the back buffer).
 * Nothing set = the old path with identical numbers. */
static float s_wp68_s = 1.0f;
static int   s_wp68_on = 0, s_wp68_bl = 0, s_wp68_dpi = 0;
static u32   s_wp68_bw = 0, s_wp68_bh = 0, s_wp68_dsv = 2048, s_wp68_outw = 0, s_wp68_outh = 0;
static u32   s_wp68_maxw = 0, s_wp68_maxh = 0;
static unsigned s_wp68_n_rt, s_wp68_n_seed, s_wp68_n_vp, s_wp68_n_blit, s_wp68_n_zs, s_wp68_n_sc, s_wp68_n_comp,
                s_wp68_n_wpos, s_wp68_n_subvp, s_wp68_n_gfb, s_wp68_n_fbc;
static u32 wp68_px(u32 g) { return s_wp68_s == 1.0f ? g : (u32)((float)g * s_wp68_s + 0.5f); }
#define WP68_BW (s_wp68_bw ? s_wp68_bw : s_d3d.width)    /* back-buffer pixels */
#define WP68_BH (s_wp68_bh ? s_wp68_bh : s_d3d.height)
static void wp68_init(u32 gw, u32 gh)
{
    const char* e = getenv("WINPORT_RES_SCALE");
    float s = e ? (float)atof(e) : 1.0f;
    if (!(s >= 1.0f)) s = 1.0f;
    if (s > 4.0f) s = 4.0f;
    s_wp68_s = s;
    {   extern int wp92b_edge_vs;   /* winport v92b-edge (fix_v92b_edge_renderer.py) */
        const char* e92 = getenv("WINPORT_EDGE92");
        wp92b_edge_vs = (s > 1.0f && e92 && e92[0] == '1');
        if (wp92b_edge_vs) { fprintf(stderr, "[EDGE92] full-screen passes cover the left/top host pixel at scale %.2f (vertex shaders recompile once)\n", s); fflush(stderr); } }
    {   extern int wp92b_edge_vs, wp95_edge_vs; extern float wp95_edge_d;   /* winport v95a-edge-exact (fix_v95a_edge_exact_renderer.py) */
        const char* e95 = getenv("WINPORT_EDGE95");
        const char* h95 = getenv("WINPORT_HALFPX");
        float hp95 = h95 ? (float)atof(h95) : 0.5f;
        if (s > 1.0f && !(e95 && e95[0] == '0') && hp95 != 0.0f) {
            wp95_edge_vs = 1; wp95_edge_d = hp95 * s; wp92b_edge_vs = 0;
            fprintf(stderr, "[EDGE95] full-screen quads drawn unshifted + attributes moved %.3f host px in the PS (exact half-pixel at scale %.2f; replaces EDGE92; WINPORT_EDGE95=0 = v94)\n", (double)wp95_edge_d, s); fflush(stderr);
        } else if (s > 1.0f) { fprintf(stderr, "[EDGE95] off (WINPORT_EDGE95=0 or HALFPX=0): EDGE92=%d\n", wp92b_edge_vs); fflush(stderr); } }
    e = getenv("WINPORT_OUT_W"); s_wp68_outw = (e && atoi(e) > 0) ? (u32)atoi(e) : 0;
    e = getenv("WINPORT_OUT_H"); s_wp68_outh = (e && atoi(e) > 0) ? (u32)atoi(e) : 0;
    e = getenv("WINPORT_WINDOW_SCALE");
    if (e && atof(e) > 0.0 && !s_wp68_outw && !s_wp68_outh) {
        s_wp68_outw = (u32)((double)gw * atof(e) + 0.5); s_wp68_outh = (u32)((double)gh * atof(e) + 0.5); }
    if (s_wp68_outw && !s_wp68_outh) s_wp68_outh = (u32)((double)s_wp68_outw * gh / gw + 0.5);
    if (s_wp68_outh && !s_wp68_outw) s_wp68_outw = (u32)((double)s_wp68_outh * gw / gh + 0.5);
    e = getenv("WINPORT_BORDERLESS"); s_wp68_bl = (e && e[0] == '1');
    s_wp68_on = (s != 1.0f) || s_wp68_outw || s_wp68_bl;
    if (s != 1.0f) { s_wp68_bw = wp68_px(gw); s_wp68_bh = wp68_px(gh); s_wp68_dsv = wp68_px(2048); }
    if (s_wp68_on) {
        e = getenv("WINPORT_DPI_AWARE");
        if (!(e && e[0] == '0')) {
            typedef BOOL (WINAPI *dpi_fn)(HANDLE);
            HMODULE u = GetModuleHandleA("user32.dll");
            dpi_fn f = u ? (dpi_fn)(void*)GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
            s_wp68_dpi = (f && f((HANDLE)(INT_PTR)-4)) ? 2 : (SetProcessDPIAware() ? 1 : 0);   /* -4 = PER_MONITOR_AWARE_V2 */
        }
        printf("[RES68] scale=%.3g guest=%ux%u backbuffer=%ux%u depth+ZSNAP=%ux%u out=%ux%u borderless=%d dpi_aware=%d"
               " (RTs, viewports, blits, copies and captures scaled; guest memory untouched)\n",
               (double)s, gw, gh, s_wp68_bw ? s_wp68_bw : gw, s_wp68_bh ? s_wp68_bh : gh, s_wp68_dsv, s_wp68_dsv,
               s_wp68_outw, s_wp68_outh, s_wp68_bl, s_wp68_dpi);
    } else
        printf("[RES68] off: native %ux%u (WINPORT_RES_SCALE / WINPORT_OUT_W / WINPORT_BORDERLESS unset)\n", gw, gh);
    fflush(stdout);
}
/* Window placement (called from create_window). 0 = not active -> caller keeps the old code. */
static int wp68_place(RECT* wr, DWORD* style, int* x, int* y)
{
    if (!s_wp68_on) return 0;
    const double gw = (double)(wr->right - wr->left), gh = (double)(wr->bottom - wr->top);
    const double bw = s_wp68_bw ? (double)s_wp68_bw : gw, bh = s_wp68_bh ? (double)s_wp68_bh : gh;
    const double asp = bw / bh;
    MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
    POINT p0 = {0, 0};
    if (!GetMonitorInfoA(MonitorFromPoint(p0, MONITOR_DEFAULTTOPRIMARY), &mi)) {
        mi.rcMonitor.left = mi.rcMonitor.top = 0;
        mi.rcMonitor.right = GetSystemMetrics(SM_CXSCREEN); mi.rcMonitor.bottom = GetSystemMetrics(SM_CYSCREEN);
        mi.rcWork = mi.rcMonitor;
    }
    const RECT area = s_wp68_bl ? mi.rcMonitor : mi.rcWork;
    const double aw = (double)(area.right - area.left), ah = (double)(area.bottom - area.top);
    double cw, ch;
    if (s_wp68_bl) { cw = aw; ch = ah; *style = WS_POPUP | WS_VISIBLE; }
    else { cw = s_wp68_outw ? (double)s_wp68_outw : bw; ch = s_wp68_outh ? (double)s_wp68_outh : bh; }
    if (cw / ch > asp + 1e-6) cw = ch * asp; else if (cw / ch < asp - 1e-6) ch = cw / asp;   /* keep 16:9 */
    int shrunk = 0;
    if (!s_wp68_bl) {
        RECT fr = {0, 0, 100, 100}; AdjustWindowRect(&fr, WS_OVERLAPPEDWINDOW, FALSE);
        const double mw = aw - (double)((fr.right - fr.left) - 100), mh = ah - (double)((fr.bottom - fr.top) - 100);
        if (cw > mw || ch > mh) { const double k = (mw / cw < mh / ch) ? mw / cw : mh / ch; cw *= k; ch *= k; shrunk = 1; }
    }
    RECT r = {0, 0, (LONG)(cw + 0.5), (LONG)(ch + 0.5)};
    const LONG ccw = r.right, cch = r.bottom;
    if (!s_wp68_bl) AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    const LONG ww = r.right - r.left, wh = r.bottom - r.top;
    *x = area.left + (LONG)((aw - (double)ww) / 2); *y = area.top + (LONG)((ah - (double)wh) / 2);
    if (*y < area.top) *y = area.top;
    wr->left = 0; wr->top = 0; wr->right = ww; wr->bottom = wh;
    printf("[RES68] window: client %ldx%ld (%s%s) at %d,%d, back buffer %.0fx%.0f -> %s\n", (long)ccw, (long)cch,
           s_wp68_bl ? "borderless on primary monitor" : "windowed", shrunk ? ", shrunk to fit the work area" : "",
           *x, *y, bw, bh, ((double)ccw == bw && (double)cch == bh) ? "1:1, no scaling" : "DXGI stretch (linear)");
    fflush(stdout);
    return 1;
}
/* ---- end winport v68-res-scale helpers ---- */
/* ---- winport v85d-walkprof-waits (fix_v85d_walkprof_waits_renderer.py): WINPORT_WALKPROF85=1 wait buckets ---- */
static int s_wk85d_on = -1;
static unsigned long long s_wk85d_c[3], s_wk85d_n[3];
static int wk85d_on(void) { if (s_wk85d_on < 0) { const char* e = getenv("WINPORT_WALKPROF85"); s_wk85d_on = (e && e[0] == '1') ? 1 : 0; } return s_wk85d_on; }
#define WK85D_T0() (wk85d_on() ? __builtin_ia32_rdtsc() : 0ull)
#define WK85D_ADD(b, t) do { if (t) { s_wk85d_c[b] += __builtin_ia32_rdtsc() - (t); s_wk85d_n[b]++; } } while (0)
static void wk85d_report(double tps, double s)
{
    fprintf(stderr, "[WALK85W] ms/s (calls/s): w77 %.1f (%.0f) wfg %.1f (%.0f) mtnf %.1f (%.0f)\n",
            s_wk85d_c[0] * 1000.0 / tps / s, s_wk85d_n[0] / s, s_wk85d_c[1] * 1000.0 / tps / s, s_wk85d_n[1] / s,
            s_wk85d_c[2] * 1000.0 / tps / s, s_wk85d_n[2] / s);
    memset(s_wk85d_c, 0, sizeof s_wk85d_c); memset(s_wk85d_n, 0, sizeof s_wk85d_n);
}
/* ---- end winport v85d-walkprof-waits ---- */
static volatile int s_wp58_capv = -2;   /* winport v80-port-settings: runtime-settable (was a function-local static) */
static int wp58_cap(void)
{
    int v = s_wp58_capv;
    if (v == -2) { const char* e = getenv("WINPORT_FPS_CAP"); v = e ? atoi(e) : 0; if (v < 0) v = 0; if (v > 1000) v = 1000; s_wp58_capv = v; }
    return v;
}
static void wp58_limit(void)
{
    const int cap = wp58_cap();
    if (!cap) return;
    static LARGE_INTEGER fq, next; static HANDLE tm; static int init;
    if (!init) { init = 1; QueryPerformanceFrequency(&fq);
                 tm = CreateWaitableTimerExW(NULL, NULL, 0x00000002u /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS); }
    const LONGLONG per = fq.QuadPart / cap;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!next.QuadPart || now.QuadPart - next.QuadPart > per * 4) next.QuadPart = now.QuadPart;   /* first frame / after a stall */
    else if (next.QuadPart > now.QuadPart) {
        const LONGLONG us = (next.QuadPart - now.QuadPart) * 1000000 / fq.QuadPart;
        if (us > 1500) {
            if (tm) { LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(us - 1000) * 10; SetWaitableTimer(tm, &due, 0, NULL, NULL, FALSE); WaitForSingleObject(tm, INFINITE); }
            else Sleep((DWORD)((us - 1000) / 1000));
        }
        do { YieldProcessor(); QueryPerformanceCounter(&now); } while (now.QuadPart < next.QuadPart);
    }
    next.QuadPart += per;
}
static u32 wp58_nbuf(void) { return wp58_cap() ? 3u : 2u; }   /* winport v58c-threebuf */
#define MAX_VERTICES    524288  /* winport-fix: bigger vertex batch */ /* per-frame vertex buffer (dbgfont submits
                                 * ~7.5k verts/frame; leave generous headroom) */
#define MAX_DRAWS         2048  /* per-frame draw records. Sized from the
                                 * measured worst case (~1841, FRAME_BUDGET);
                                 * the per-draw constant buffer is
                                 * VP_CB_STRIDE * MAX_DRAWS * 2 bytes, so 16384
                                 * asked for a 264 MB upload allocation. */
/* Per-draw VP constant-buffer slot: vp_c[512] + posscale + posoffset
 * (514 vec4 = 8224 B) rounded up to D3D12's 256-byte CBV alignment.
 * Constants are snapshotted at RECORD time -- wave's passes each set their
 * own texScale/offset uniforms, so one per-frame snapshot ran every pass
 * with the LAST pass's constants. */
#define VP_CB_STRIDE      8448
/* b1 per-draw FP slot: rsx_texscale[4] + rsx_alphatest + fp_k[64]
 * = 69 float4 = 1104 B, rounded to D3D12's 256-byte CBV alignment. */
#define VP_FPCB_STRIDE    1280
#define VERTEX_STRIDE       36  /* bytes per host vertex: pos3 + col4 + uv2 */

typedef struct {
    u32 vb_byte_offset; /* offset into vb_mapped where this draw's verts live */
    u32 vertex_count;
    u32 topology;       /* rsx_topology (NOT a D3D value; see topo_to_d3d) */
    int textured;       /* 1 = sample the bound font/atlas texture (dbgfont) */
    int is_vp;          /* 1 = real VP path: vb_byte_offset indexes vp_vb (float4) */
    /* VP path per-draw shader/texture state, captured at draw_arrays time. */
    u32 fp_addr;        /* SET_SHADER_PROGRAM value (guest FP ucode location)   */
    int fp_exp32;       /* SET_SHADER_CONTROL 32-bit-exports bit at draw time   */
    u32 alpha_ctl;      /* alpha test: enable<<16 | (func&0xFF)<<8 | ref */
    u32 begin_epoch;    /* SET_BEGIN_END generation, for batch concatenation */
    u32 cull;           /* packed face culling: bit0 enable, bit1 cull FRONT
                         * (else BACK), bit2 front face is CCW. RSX culls back
                         * faces on most solid geometry; rendering everything
                         * double-sided lets a shell's interior faces show
                         * through and shade unlit (black). */
    u32 cmask;          /* D3D write mask from SET_COLOR_MASK at draw time
                         * (wave's sim passes write single lanes of the height
                         * maps; ignoring the mask stomped persistent state) */
    /* Per-unit textures (t0-t3): decompiled FPs sample up to 4 units
     * (demosaic's interpolation passes read 3). Captured at draw time. */
    struct {
        u32 off;        /* resolved vm offset (guest upload source), 0 = none */
        u32 raw;        /* raw RSX offset (offscreen-RT matching) */
        u32 w, h, fmt;  /* dims + RSX base format */
        u32 ctrl1;      /* NV4097 TEXTURE_CONTROL1: component remap crossbar */
        u32 mips;       /* SET_TEXTURE_FORMAT bits 16..31: mipmap level count.
                         * Cube faces sit one whole mip pyramid apart, so this
                         * is what sets the face stride. */
        int cube;       /* SET_TEXTURE_FORMAT bit 2: a cube texture. Sampled
                         * with a 3-component direction, not a 2D uv -- treating
                         * one as 2D is what makes an environment-mapped chrome
                         * surface come out with black patches and banding. */
        int set;
        u32 samp;       /* winport-fix: guest sampler state */ /* packed filter/address, 0 = default */
    } tex[4];
    int tex_rt[4];      /* pre-pass: OffRT index sampled by unit, -1 = none */
    int tex_slot;       /* legacy single-slot path (atlas); -1 = none */
    int vs_idx;         /* VPVSEntry slot for this draw's vertex program (-1 = primary) */
    int blend;          /* guest blend enable at draw time */
    u32 blend_key;      /* packed guest blend state (factors+equation), PSO key */
    /* Render-to-texture: which colour surface this op targets. 0 = a display
     * buffer (the swap-chain backbuffer); else the resolved vm offset of an
     * offscreen surface (demosaic chains its effect passes through local-
     * memory buffers and composites from them). */
    u32 rt_off;
    u32 wp94_tgt, wp94_clr;   /* winport v94a-ffhaze: display target raw offset (0 = offscreen), cleared-buffer mask */
    /* Which per-draw constant slot this record's constants were written to.
     * vp_record_cb writes them keyed by the RECORD index, so any pass that
     * reorders or compacts s_d3d.draws (DRAW_KEEP_TEX, DRAW_LAST_TEX,
     * DRAW_LIMIT) would otherwise make every surviving draw read some other
     * object's MVP -- silently transforming it somewhere else entirely. */
    u32 cb_slot;
    u16 wp_merge_n, wp_merge_cbchg;   /* winport v38: batches merged into this record / of them after a vertex-constant upload */
    u32 wp_stk0, wp_stk1, wp_sref;    /* winport v39: guest stencil state (wp_stencil_pack), recorded always */
    u32 wp_vzero, wp_vcmp, wp_vmaxi;  /* winport v41: WINPORT_VTXCHK samples (of 64) */
    u32 rt_mrt[3];      /* colour targets B,C,D (MRT1/2/3), 0 = none. Deferred
                         * shading G-buffers write 3-4 targets in one pass. */
    u32 rt_w, rt_h;     /* surface clip dims at record time (offscreen RT size) */
    u32 rt_fmt;         /* RSX surface colour format (SET_SURFACE_FORMAT [4:0]) */
    u32 zeta_off, zeta_pitch;   /* winport-fix: guest depth surface */
    u32 fp_kn;          /* winport-fix: v22 scene depth: index of fp_alpha in this draw's b1 block */
    /* Guest viewport rect at draw time (target pixels). Sub-viewport layouts
     * (wave's debug tiles) position quads with this, not with constants --
     * forcing full-target viewports drew every tile window-sized. */
    u32 vp_x, vp_y, vp_w, vp_h;
    /* Ordered clear op (offscreen surfaces only; display clears stay the
     * frame-start backbuffer clear). is_clear records also set is_vp so the
     * legacy replay pass skips them. */
    int   is_clear;
    float cc[4];
    u32   clear_flags; float clear_z; u32 clear_s;   /* winport-fix: honour RSX clear flags */
    u32   fp_uhash;      /* winport-fix: shader caches */ /* FP code hash taken at record time (0 = none) */
    u32   wp85_ib;       /* winport v85e-idxdraw: index-buffer start + 1 (0 = expanded vertices) */
    /* winport-fix: NV3089 blit record (is_vp=1, is_blit=1) */
    int   is_blit;
    u32   blit_src, blit_dst, blit_spitch, blit_dpitch;
    float blit_uv[4];    /* source rect in texels: x0,y0,x1,y1 */
    u32   blit_rect[4];  /* destination rect in pixels: x,y,w,h */
} D3D12DrawRecord;

/* Offscreen render target (render-to-texture). Persistent across frames --
 * pass N's output is sampled by pass N+1 and possibly by later frames. */
typedef struct {
    ID3D12Resource*       res;
    ID3D12Resource*       up;          /* init-upload staging (kept alive) */
    u32                   off, w, h;   /* raw RSX offset + dims */
    u32                   dxgi;        /* DXGI_FORMAT of the resource */
    D3D12_RESOURCE_STATES st;          /* tracked resource state */
    int                   used;        /* referenced this frame */
} OffRT;
#define MAX_OFF_RTS  64   /* winport-fix: SRV heap layout */ /* was 16 */  /* demosaic double-buffers its 6-surface pass chain */
#define RT_SRV_BASE  5   /* SRV heap slots 5..20 hold offscreen-RT SRVs */
/* Per-draw SRV windows: each VP draw gets 4 consecutive descriptors (t0-t3)
 * so multi-unit fragment programs see all their textures (demosaic's
 * interpolation passes sample 3 units). */
#define WP_TEX_SRV_BASE  (RT_SRV_BASE + MAX_OFF_RTS)          /* winport-fix: SRV heap layout */ /* vp_tex slot SRVs */
#define DRAW_SRV_BASE0   (WP_TEX_SRV_BASE + VP_TEX_SLOTS + 16)   /* winport v77-fif: per-frame windows start here */
#define WP_BLIT_SRV_N    256
#define WP77_FRAME_SRVS  (MAX_DRAWS * 4 + WP_BLIT_SRV_N)          /* one frame: draw windows + blit ring */
static u32 s77_srv_off;                                         /* 0, or slot * WP77_FRAME_SRVS (v77) */
static int wp77_on(void); static u32 wp77_nslot(void);
#define DRAW_SRV_BASE    (DRAW_SRV_BASE0 + s77_srv_off)
#define WP_BLIT_SRV_BASE (DRAW_SRV_BASE + MAX_DRAWS * 4)
#define SRV_HEAP_SIZE    (DRAW_SRV_BASE0 + (wp77_on() ? 3u : 1u) * WP77_FRAME_SRVS)

/* Per-frame VP texture slot: a guest texture uploaded for this frame's VP
 * draws (re-uploaded every frame -- gcm/cube's plasma animates in guest
 * memory). SRV lives at heap index 1+slot. */
typedef struct {
    ID3D12Resource* res;
    ID3D12Resource* up;
    u32 off, w, h, fmt; /* current contents (resource reused when dims match) */
    u32 csum;           /* sparse checksum of the source bytes last uploaded */
    int cube;           /* resource is a 6-face cube, sampled as TextureCube */
    u32 key;            /* the ORIGINAL bound offset -- the cache lookup key.
                         * off is the RESOLVED source after TEX_OFF_BIAS/TEX_REMAP
                         * shift it, so keying on off never matches the raw offset
                         * a later draw arrives with, and every draw re-uploads. */
    int used;           /* referenced this frame */
    u32 lastf;   /* winport-fix: texture slot LRU */ /* frame stamp of last use (LRU) */
} VPTexSlot;
/* Distinct textures uploadable per frame. Four was enough for a UI/atlas-style
 * frame but not for a scene: this title binds ~20 distinct textures per frame,
 * so every draw past the fourth got slot -1 from vp_upload_tex_slot ("out of
 * slots") and drew UNTEXTURED. That reads as a correctly-lit but solid black
 * object, which is indistinguishable from a missing texture upload. */
#define VP_TEX_SLOTS 256   /* winport-fix: texture slot LRU */
static u32 s_wp_tx_frame = 1, s_wp_tx_hit = 0, s_wp_tx_csum = 0, s_wp_tx_stale = 0,
           s_wp_tx_new = 0, s_wp_tx_evict = 0, s_wp_tx_fail = 0; static u64 s_wp_tx_bytes = 0;

/* Decompiled-VS cache: one entry per distinct vertex-program ucode seen at
 * draw time (hashed). Apps switch VPs between draws (gcm/cube: its MVP cube VP
 * vs dbgfont-gcm's text VP); compiling only the first left later draws
 * transformed by the wrong program (text offscreen). */
typedef struct {
    u32 hash;               /* FNV-1a of the ucode */
    ID3DBlob* vs;
    int uses_c03;
} VPVSEntry;
#define VP_VS_CACHE 512 /* winport-fix: shader caches */  /* wave uses 5+ distinct VPs; at 4 the cache
                         * thrashed every frame and eviction shifted slots
                         * under recorded vs_idx values -- the display mesh
                         * drew with the WRONG vertex program (sub-rect +
                         * edge slivers instead of fullscreen) */

/* Compiled guest-FP pipeline cache: decompiled VS (by cache slot) + decompiled
 * PS (fragment ucode at fp_addr). */
typedef struct {
    u32 fp_addr;
    int vs_idx;             /* VPVSEntry slot this PSO's VS came from */
    u32 vs_hash;            /* validates the slot hasn't been evicted */
    u32 gen;
    u32 blend;              /* packed guest blend key (enable+factors+eq, PSO key) */
    int nrt;                /* bound colour target count (PSO key)       */
    u32 rtfmt;              /* DXGI format of the colour targets (PSO key) */
    int exp32;              /* 32-bit-exports control bit (PSO key)       */
    u32 ucode_hash;         /* FNV-1a of the FP ucode: apps re-patch inline
                             * constants per frame (wave's stamp position/
                             * amplitude) -- address-only keying served the
                             * stale compile forever. */
    u32 cmask;              /* colour write mask (PSO key) */
    u32 cull;               /* packed face culling (PSO key) */
    u32 cube_mask;          /* which units are cube textures (PSO key): the HLSL
                             * declares those samplers as TextureCube, so a cube
                             * and a 2D variant of the same program are different
                             * pipelines and must not share a cache entry. */
    u32 stk0, stk1;         /* winport v39: stencil state (PSO key; 0 = stencil off) */
    u32 wp_topo;            /* winport v42: topology class 0 tri / 1 line / 2 point (PSO key) */
    ID3D12PipelineState* pso;
} VPFPEntry;
static int wp_stencil_on(void);   /* winport v39 */
/* Guest-FP PSO cache. 16 entries thrashed badly: this title needs far more
 * distinct (fp, vs, blend, rt, cmask, ucode-hash) combinations than that in a
 * single frame, so the FIFO evicted entries that were needed again immediately
 * and ~17 shaders were recompiled EVERY frame -- about half the frame's CPU
 * time. Entries are small (a PSO pointer plus key fields). */
#define VP_FP_CACHE 4096   /* winport-fix: shader caches */

/* ---------------------------------------------------------------------------
 * Internal state
 * -----------------------------------------------------------------------*/

typedef struct {
    /* Window */
    HWND hwnd;
    u32  width;
    u32  height;
    int  window_closed;

    /* D3D12 core */
    ID3D12Device*              device;
    ID3D12CommandQueue*        cmd_queue;
    IDXGISwapChain3*           swap_chain;
    ID3D12DescriptorHeap*      rtv_heap;
    u32                        rtv_descriptor_size;
    ID3D12Resource*            render_targets[FRAME_COUNT_MAX];
    ID3D12CommandAllocator*    cmd_allocators[FRAME_COUNT_MAX];
    ID3D12GraphicsCommandList* cmd_list;

    /* Synchronization */
    ID3D12Fence* fence;
    HANDLE       fence_event;
    u64          fence_values[FRAME_COUNT_MAX];
    u32          frame_index;

    /* Pipeline */
    ID3D12RootSignature*  root_signature;
    ID3D12PipelineState*  pipeline_state;         /* triangle class — default */
    ID3D12PipelineState*  pipeline_state_lines;   /* line class */
    ID3D12PipelineState*  pipeline_state_points;  /* point class */

    /* Depth/stencil */
    ID3D12DescriptorHeap* dsv_heap;
    ID3D12Resource*       depth_buffer;

    /* Dynamic vertex buffer (upload heap) */
    ID3D12Resource*       vertex_buffer;
    D3D12_VERTEX_BUFFER_VIEW vb_view;
    void*                 vb_mapped;      /* persistently mapped */
    u32                   vb_offset;      /* current write position */

    int                   pipeline_ready; /* 1 if root sig + PSO created */

    /* Debug: copy the presented backbuffer to disk as BMP for the first N
     * frames (enabled by env CELLMARK_DUMP). Lets us verify what actually
     * rendered without racing the window/process lifetime. */
    ID3D12Resource*       readback_buf;
    u32                   readback_pitch;
    int                   dump_frames_left;
    int                   dump_skip_left;   /* CELLMARK_DUMP_SKIP: presents to ignore first */

    /* Textured pipeline (dbgfont / 2D atlas quads). The font atlas is an 8-bit
     * coverage texture uploaded as R8_UNORM and sampled in the pixel shader. */
    ID3D12PipelineState*  pipeline_state_tex;   /* triangle + texture + blend */
    ID3D12DescriptorHeap* srv_heap;             /* shader-visible, 1 SRV slot  */
    ID3D12Resource*       tex_resource;         /* current atlas texture       */
    ID3D12Resource*       tex_upload;           /* staging buffer for uploads  */
    u32                   tex_w, tex_h;         /* dims of tex_resource         */
    int                   tex_ready;            /* 1 once an atlas is uploaded  */
    u32                   tex_src_offset;       /* guest RSX offset of atlas    */
    int                   tex_bound;            /* a texture is bound for draws */
    int                   tex_dirty;            /* re-upload needed this frame  */

    /* Real RSX vertex-program path: the captured VP is decompiled to HLSL and
     * used as the vertex shader, fed the raw float4 attrib0 + the vp_c[]
     * constant bank. This produces exact position + texcoord (vs. the frac
     * approximation). Used for the 2D/dbgfont quad draws. */
    ID3D12RootSignature*  vp_root_sig;          /* CBV(b0) + SRV table + sampler */
    ID3D12PipelineState*  pipeline_state_vp;    /* decompiled VS + atlas PS      */
    ID3D12PipelineState*  pipeline_state_vp_color; /* decompiled VS + colour PS (untextured 3D) */
    ID3D12Resource*       vp_vb;                /* raw float4 attrib0, per-frame */
    void*                 vp_vb_mapped;
    u32                   vp_vb_offset;
    ID3D12Resource*       vp_cb;                /* per-draw VP constant slots    */
    void*                 vp_cb_mapped;
    ID3D12Resource*       vp_fpcb;              /* per-draw FP texscale (b1)     */
    void*                 vp_fpcb_mapped;
    int                   vp_ready;             /* VS+PSO compiled ok            */
    u32                   vp_compiled_bytes;    /* ucode size when last compiled */
    ID3DBlob*             vp_vs_blob;           /* kept for guest-FP PSO builds  */
    u32                   vp_gen;               /* bumped per VP recompile       */
    int                   vp_uses_c03;          /* VS references vp_c[0..3]      */
    VPTexSlot             vp_tex[VP_TEX_SLOTS]; /* per-frame VP texture slots    */
    VPVSEntry             vp_vs[VP_VS_CACHE];   /* per-draw decompiled VS cache  */
    int                   vp_vs_n;
    VPFPEntry             vp_fp[VP_FP_CACHE];   /* guest-FP PSO cache            */
    int                   vp_fp_n;
    u32                   srv_inc;              /* CBV_SRV_UAV descriptor size   */
    /* VP path: latest texture bound per unit (t0-t3). */
    struct { u32 off, raw, w, h, fmt, ctrl1, mips; int cube; int set; u32 samp; } cur_texs[4];   /* winport-fix: guest sampler state */

    /* Render-to-texture: offscreen RT pool + their RTV heap. */
    OffRT                 off_rt[MAX_OFF_RTS];
    /* DRAW_ARRAYS batch merging (see d3d12_draw_arrays): the vertex index one
     * past the last batch, and whether the current BEGIN/END may still be
     * extended. */
    u32                   merge_first_end;
    int                   merge_prev_draw;
    ID3D12DescriptorHeap* rt_rtv_heap;          /* MAX_OFF_RTS RTVs (CPU only)   */

    /* Frame-parity double buffering for the per-draw upload streams (vp_vb
     * vertices, vp_cb constants, vp_fpcb texscales): records for frame N+1
     * are written while frame N's GPU work may still be reading -- writes go
     * to the other half. Toggled at the end of render_frame. */
    int                   vp_parity;

    /* Per-frame draw recording */
    int                   frame_recording; /* 1 if cmd list is open for recording */
    u32                   draw_count;      /* draws this frame */
    D3D12DrawRecord       draws[MAX_DRAWS];

    /* Pointer to current RSX state (set before draw calls) */
    const rsx_state*      current_rsx_state;

    /* Current frame state */
    float clear_color[4];  /* RGBA float */

    /* Stats */
    u64 frame_count;
    u64 last_fps_time;
    u32 fps;

    int initialized;
} D3D12State;

static D3D12State s_d3d;
static int s_wp_cont = 0;      /* winport-fix: flush a full batch instead of dropping draws */ /* next batch continues the previous one */
static u32 s_wp_cont_rt = 0, s_wp_last_rt = 0;
static int wp_samp_ready(void);
static int wp_samp_on(void)   /* winport-fix: guest sampler state */ /* first called at root-signature creation (device exists) */
{ static int c = -1; if (c < 0) { const char* e = getenv("WINPORT_SAMPLERS"); c = (e && *e == '0') ? 0 : 1;
    if (c && !wp_samp_ready()) { c = 0; fprintf(stderr, "[WPSAMP] no sampler heap -> static samplers\n"); } } return c; }
#define WP_SAMP_TABLES 512
static ID3D12DescriptorHeap* s_wp_samp_heap = NULL;
static UINT s_wp_samp_inc = 0;
static unsigned long long s_wp_samp_key[WP_SAMP_TABLES];
static int s_wp_samp_n = 0;
static D3D12_TEXTURE_ADDRESS_MODE wp_samp_addr(u32 w)
{
    switch (w) {
    case 2: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case 3: case 5: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case 4: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case 6: case 7: case 8: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
    default: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
}
static void wp_samp_desc(u32 code, D3D12_SAMPLER_DESC* sd)
{
    memset(sd, 0, sizeof *sd);
    u32 minf = code & 7u, magf = (code >> 3) & 7u, ws = (code >> 6) & 0xFu, wt = (code >> 10) & 0xFu;
    if (!(code & 0x8000u)) { minf = 6; magf = 2; ws = wt = 1; }   /* unit unused: linear + wrap */
    int min_lin = !(minf == 1 || minf == 3 || minf == 5);
    int mip_lin = (minf == 5 || minf == 6);
    int has_mip = (minf >= 3 && minf <= 6);
    int mag_lin = (magf != 1);
    sd->Filter = (D3D12_FILTER)((min_lin ? 0x10 : 0) | (mag_lin ? 0x4 : 0) | (mip_lin ? 0x1 : 0));
    sd->AddressU = wp_samp_addr(ws); sd->AddressV = wp_samp_addr(wt); sd->AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sd->MaxAnisotropy = 1; sd->ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sd->MinLOD = 0.0f; sd->MaxLOD = has_mip ? D3D12_FLOAT32_MAX : 0.0f;
}
static int wp_samp_ready(void)
{
    if (s_wp_samp_heap) return 1;
    if (!s_d3d.device) return 0;
    D3D12_DESCRIPTOR_HEAP_DESC hd = {0};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER; hd.NumDescriptors = WP_SAMP_TABLES * 4;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(s_d3d.device->lpVtbl->CreateDescriptorHeap(s_d3d.device, &hd, &IID_ID3D12DescriptorHeap, (void**)&s_wp_samp_heap))) {
        s_wp_samp_heap = NULL; return 0; }
    s_wp_samp_inc = s_d3d.device->lpVtbl->GetDescriptorHandleIncrementSize(s_d3d.device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    return 1;
}
/* The table for one combination of four packed unit samplers. Written once, never changed, so
 * in-flight command lists that reference it stay valid. */
static D3D12_GPU_DESCRIPTOR_HANDLE wp_samp_table_key(unsigned long long key)
{
    D3D12_GPU_DESCRIPTOR_HANDLE g; g.ptr = 0;
    if (!wp_samp_ready()) return g;
    int idx = -1;
    for (int i = 0; i < s_wp_samp_n; i++) if (s_wp_samp_key[i] == key) { idx = i; break; }
    if (idx < 0) {
        if (s_wp_samp_n >= WP_SAMP_TABLES) {
            static int w = 0; if (!w++) fprintf(stderr, "[WPSAMP] sampler tables full, using the default table\n");
            key = 0; for (int i = 0; i < s_wp_samp_n; i++) if (s_wp_samp_key[i] == 0) { idx = i; break; }
            if (idx < 0) idx = 0;
        } else {
            idx = s_wp_samp_n++;
            s_wp_samp_key[idx] = key;
            D3D12_CPU_DESCRIPTOR_HANDLE c;
            s_wp_samp_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_wp_samp_heap, &c);
            c.ptr += (SIZE_T)idx * 4u * s_wp_samp_inc;
            for (int u = 0; u < 4; u++) {
                D3D12_SAMPLER_DESC sd; wp_samp_desc((u32)((key >> (16 * u)) & 0xFFFFu), &sd);
                s_d3d.device->lpVtbl->CreateSampler(s_d3d.device, &sd, c);
                c.ptr += s_wp_samp_inc;
            }
            static int nlog = 0; if (nlog++ < 12)
                fprintf(stderr, "[WPSAMP] table %d = %04X %04X %04X %04X\n", idx,
                        (unsigned)(key & 0xFFFF), (unsigned)((key >> 16) & 0xFFFF), (unsigned)((key >> 32) & 0xFFFF), (unsigned)(key >> 48));
        }
    }
    s_wp_samp_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_wp_samp_heap, &g);
    g.ptr += (UINT64)idx * 4u * s_wp_samp_inc;
    return g;
}
static D3D12_GPU_DESCRIPTOR_HANDLE wp_samp_table(const D3D12DrawRecord* dr)
{
    unsigned long long key = 0;
    for (int u = 0; u < 4; u++)
        if (dr->tex[u].set) key |= (unsigned long long)(dr->tex[u].samp & 0xFFFFu) << (16 * u);
    return wp_samp_table_key(key);
}
static void wp_set_heaps(ID3D12GraphicsCommandList* cl)
{
    if (wp_samp_on() && wp_samp_ready()) {
        ID3D12DescriptorHeap* h[] = { s_d3d.srv_heap, s_wp_samp_heap };
        cl->lpVtbl->SetDescriptorHeaps(cl, 2, h);
    } else {
        ID3D12DescriptorHeap* h[] = { s_d3d.srv_heap };
        cl->lpVtbl->SetDescriptorHeaps(cl, 1, h);
    }
}
char g_rsx_title_base[128] = "ps3recomp";
static u32 s_dbg_last_draws = 0;
static u64 s_req_verts = 0, s_req_draws = 0, s_drop_draws = 0;
/* Raw bind offset of the duck's texture, identified by CONTENT (duck.tga is
 * overwhelmingly yellow, avg 243,191,23). VRAM offsets move between runs, so a
 * hard-coded offset in a filter silently matches nothing and the run looks like
 * "renders nothing" -- which cost real time. DRAW_KEEP_TEX=duck resolves here. */
/* Copy of the last completed frame, bound wherever a draw samples a
 * DISPLAY-SIZED texture. On RSX this title renders its reflection into a corner
 * of the render surface and then samples that surface as a texture; our backend
 * renders into a D3D backbuffer, so the guest memory behind that sampler is
 * never written and it reads empty -- which is why the water had no reflection.
 * Feeding it the previous frame costs one frame of latency, which for a water
 * reflection is not visible. */
static ID3D12Resource* s_screen_copy = NULL;
static ID3D12Resource* s_wp92_prev = NULL;   /* winport v92b-prevframe: last finished frame (display-buffer textures) */
static int s_wp92_prev_ok = 0;
static ID3D12Resource* s_wp94_buf[8];   /* winport v94a-ffhaze: game frame last rendered into display buffer i */
static int s_wp94_ok[8];
static ID3D12Resource* s_wp94_last;     /* mode 2: previous game frame */
static int s_wp94_last_ok;
static u32 s_wp94_frame_tgt;            /* display target of the frame being rendered */
/* winport v50-boot-screens (fix_v50_boot_screens_renderer.py): the boot screens (Sony / Bluepoint / WARNING) are
 * 1280x720 DXT1 textures -- display-sized, but never a copy of the frame. 0 = substitute as before. */
static int wp50_screen_sub_ok(u32 fmt)
{
    static int on = -1;
    const u32 bf = fmt & 0x9Fu;
    if (on < 0) {
        const char* e = getenv("WINPORT_BOOT_SCREENS");
        on = !(e && e[0] == '0');
        fprintf(stderr, "[BOOT50] display-sized DXT textures drawn as themselves (boot logos / warning): %s%c", on ? "ON" : "off", 10);
    }
    if (on && bf >= 0x86u && bf <= 0x88u) {
        static int n = 0;
        if (n++ < 3) fprintf(stderr, "[BOOT50] display-sized DXT texture (fmt 0x%02X) sampled as itself, not as the frame copy%c", bf, 10);
        return 0;
    }
    return 1;
}
/* Sub-viewport render-to-texture. This title renders its reflection/refraction
 * pre-pass into a REGION of the same surface (vp 0,208 512x512 and 0,208
 * 1024x512, cmask=F) and then samples a texture of exactly that size. On
 * hardware the pass lands in the buffer that texture points at; our backend
 * renders into D3D resources, so that guest buffer is never written and every
 * fluid sampler reads zero. Capture each region into a matching resource and
 * bind it for samplers whose guest source is empty and whose size matches.
 *
 * SUBVP_RTT=1 to enable. OFF by default because it cannot be validated on this
 * title yet: the fluid draws that would consume these captures never rasterize
 * (see below), so binding them changes nothing observable. The capture and
 * binding themselves are confirmed working -- SUBVP_DBG shows the 512x512 and
 * 256x256 regions captured and handed to fp 0x5DB81/0x5EA81/0x5EF81/0x5E381. */
#define SUBVP_SLOTS 4
static struct { ID3D12Resource* res; u32 x, y, w, h; } s_subvp[SUBVP_SLOTS];
static int s_subvp_n = 0;

static void subvp_note(u32 x, u32 y, u32 w, u32 h)
{
    if (!w || !h || w > s_d3d.width || h > s_d3d.height) return;
    if (w == s_d3d.width && h == s_d3d.height) return;      /* full-screen pass */
    for (int i = 0; i < s_subvp_n; i++)
        if (s_subvp[i].w == w && s_subvp[i].h == h) { s_subvp[i].x = x; s_subvp[i].y = y; return; }
    if (s_subvp_n >= SUBVP_SLOTS) return;
    s_subvp[s_subvp_n].x = x; s_subvp[s_subvp_n].y = y;
    s_subvp[s_subvp_n].w = w; s_subvp[s_subvp_n].h = h;
    s_subvp[s_subvp_n].res = NULL;
    s_subvp_n++;
}

/* Copy each noted region out of the backbuffer. Called at the same moment as
 * screen_copy_capture -- after the reduced-viewport passes and before the
 * full-screen scene overwrites them. */
static void subvp_capture(u32 fi)
{
    static int en = -1;
    if (en < 0) { const char* e = getenv("SUBVP_RTT"); en = e ? atoi(e) : 0; }
    if (!en || !s_d3d.device || !s_subvp_n) return;
    for (int i = 0; i < s_subvp_n; i++) {
        if (!s_subvp[i].res) {
            D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC td = {0};
            td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            td.Width = wp68_px(s_subvp[i].w); td.Height = wp68_px(s_subvp[i].h);   /* winport v68 */
            td.DepthOrArraySize = 1; td.MipLevels = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
            td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
                    s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL,
                    &IID_ID3D12Resource, (void**)&s_subvp[i].res)))
                { s_subvp[i].res = NULL; continue; }
        }
        D3D12_RESOURCE_BARRIER bb[2] = {0};
        bb[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bb[0].Transition.pResource   = s_d3d.render_targets[fi];
        bb[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        bb[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        bb[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        bb[1] = bb[0];
        bb[1].Transition.pResource   = s_subvp[i].res;
        bb[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        bb[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, bb);

        D3D12_TEXTURE_COPY_LOCATION dstl = {0}, srcl = {0};
        dstl.pResource = s_subvp[i].res;
        dstl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dstl.SubresourceIndex = 0;
        srcl.pResource = s_d3d.render_targets[fi];
        srcl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; srcl.SubresourceIndex = 0;
        D3D12_BOX box; box.left = wp68_px(s_subvp[i].x); box.top = wp68_px(s_subvp[i].y);   /* winport v68 */
        box.front = 0; box.right = box.left + wp68_px(s_subvp[i].w);
        box.bottom = box.top + wp68_px(s_subvp[i].h); box.back = 1;
        if (box.right > WP68_BW)  box.right  = WP68_BW;
        if (box.bottom > WP68_BH) box.bottom = WP68_BH;
        if (s_wp68_s != 1.0f) s_wp68_n_subvp++;
        s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dstl, 0, 0, 0, &srcl, &box);

        bb[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        bb[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        bb[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        bb[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, bb);
        { static int _n = 0; if (getenv("SUBVP_DBG") && _n++ < 8)
            fprintf(stderr, "[SUBVP] captured %ux%u from (%u,%u)%c",
                    s_subvp[i].w, s_subvp[i].h, s_subvp[i].x, s_subvp[i].y, 10); }
    }
}
static void screen_copy_capture(u32 fi);   /* fwd */
static int  s_sc_dump_pending = 0;
static u32 s_duck_raw = 0;   /* as BOUND (what draw records carry) */
static u32 s_duck_off = 0;   /* as RESOLVED (what the uploader sees)   */
/* PERF=1: where a frame's CPU time actually goes. Guessing at this is how you
 * spend an afternoon optimising the wrong loop. */
static double s_perf_tex = 0.0, s_perf_frame = 0.0, s_perf_vtx = 0.0, s_perf_rf = 0.0;
static double s_perf_gpu = 0.0, s_perf_pre = 0.0, s_perf_srv = 0.0, s_perf_pso = 0.0;
static int s_perf_pso_calls = 0, s_perf_pso_miss = 0, s_perf_pso_hashbytes = 0;
static u64    s_perf_nverts = 0;
static double s_perf_vcache = 0.0; static u64 s_perf_vdraws = 0, s_perf_vattr = 0, s_perf_vfast = 0;   /* v24 vertex-fetch diagnostics */
static u64 s_wpv_draws, s_wpv_verts;   /* winport v27 fwd (tentative): parallel vertex stats */
static u64 s_vs_slow, s_vs_type[8], s_vs_nonlin, s_vs_stride0, s_vs_size, s_vs_disabled, s_vs_other, s_vs_vpread, s_vs_vpen;   /* winport v25: [VTXSLOW] counters */
static u64    s_perf_texbytes = 0;
static int    s_perf_ntex = 0;
static double perf_now(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
static int perf_on(void)
{
    static int v = -1;
    if (v < 0) { const char* e = getenv("PERF"); v = e ? atoi(e) : 0; }
    return v;
}
/* DBG_LOCK: running clip-space centroid of the tracked mesh (RSX_DBG_VTX's
 * texture), so the debug camera can follow it. The ducks are driven by the
 * physics sim and drift every frame, so a fixed DBG_CENTER loses them as soon
 * as the magnification is high enough to make one recognisable. */
static float s_lock_x = 0.0f, s_lock_y = 0.0f;
static int   s_lock_valid = 0;
/* Accumulated over one frame, published at the frame boundary. Averaging as the
 * frame is recorded makes the aim wander while it is being used, which at high
 * magnification walks the target off-screen entirely. */
static double s_lock_sx = 0.0, s_lock_sy = 0.0;
static u32    s_lock_n  = 0;

/* ---------------------------------------------------------------------------
 * Win32 window
 * -----------------------------------------------------------------------*/

/* ---- winport v69-fps-f11: window-thread side of the F11 borderless toggle ---- */
#define WP_WM_BORDER69 (WM_APP + 0x69)
static volatile LONG s_wp69_keys_on = 0;   /* set by wp69_fps_overlay once the keys are enabled (F10 swallow) */
static RECT s_wp69_saved; static int s_wp69_have_saved = 0;
static void wp69_apply_border(HWND hwnd)   /* ON THE WINDOW THREAD */
{
    LONG_PTR st = GetWindowLongPtrA(hwnd, GWL_STYLE);
    MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
    if (!GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
    const double bw = s_wp68_bw ? (double)s_wp68_bw : 1280.0, bh = s_wp68_bh ? (double)s_wp68_bh : 720.0, asp = bw / bh;
    if (IsZoomed(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    if (st & WS_CAPTION) {   /* -> borderless */
        GetWindowRect(hwnd, &s_wp69_saved); s_wp69_have_saved = 1;
        const double aw = (double)(mi.rcMonitor.right - mi.rcMonitor.left), ah = (double)(mi.rcMonitor.bottom - mi.rcMonitor.top);
        double cw = aw, ch = ah;
        if (cw / ch > asp + 1e-6) cw = ch * asp; else if (cw / ch < asp - 1e-6) ch = cw / asp;
        const int w = (int)(cw + 0.5), h = (int)(ch + 0.5);
        const int x = mi.rcMonitor.left + (int)((aw - w) / 2), y = mi.rcMonitor.top + (int)((ah - h) / 2);
        SetWindowLongPtrA(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP, x, y, w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        fprintf(stderr, "[UI69] F11: borderless %dx%d at %d,%d\n", w, h, x, y);
    } else {                 /* -> overlapped window */
        int x, y, w, h;
        if (s_wp69_have_saved) {
            x = s_wp69_saved.left; y = s_wp69_saved.top; w = s_wp69_saved.right - s_wp69_saved.left; h = s_wp69_saved.bottom - s_wp69_saved.top;
        } else {
            const double aw = (double)(mi.rcWork.right - mi.rcWork.left), ah = (double)(mi.rcWork.bottom - mi.rcWork.top);
            RECT fr = {0, 0, 100, 100}; AdjustWindowRect(&fr, WS_OVERLAPPEDWINDOW, FALSE);
            const double mw = aw - (double)((fr.right - fr.left) - 100), mh = ah - (double)((fr.bottom - fr.top) - 100);
            double cw = 1280.0, ch = 1280.0 / asp;
            if (cw > mw || ch > mh) { const double k = (mw / cw < mh / ch) ? mw / cw : mh / ch; cw *= k; ch *= k; }
            RECT r = {0, 0, (LONG)(cw + 0.5), (LONG)(ch + 0.5)}; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
            w = r.right - r.left; h = r.bottom - r.top;
            x = mi.rcWork.left + (int)((aw - w) / 2); y = mi.rcWork.top + (int)((ah - h) / 2);
        }
        SetWindowLongPtrA(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP, x, y, w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        fprintf(stderr, "[UI69] F11: windowed %dx%d at %d,%d\n", w, h, x, y);
    }
    fflush(stderr);
}
/* ---- end winport v69 window side ---- */

/* ---- winport v82c-present-wd: window-thread recovery steps for a stalled presentation (see wp82_wd_thread) ---- */
#define WP_WM_PWD82 (WM_APP + 0x82)
static void wp82_wnd_recover(HWND hwnd, int level)   /* ON THE WINDOW THREAD */
{
    const int act = level == 1 ? 2 : level == 2 ? 3 : 1;   /* winport v82c-present-wd-b: cloak first */
    if (act <= 1) {
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else if (act == 2) {
        typedef HRESULT (WINAPI *SetAttr)(HWND, DWORD, LPCVOID, DWORD);
        static SetAttr f = NULL; static int tried = 0;
        if (!tried) { tried = 1; HMODULE m = LoadLibraryA("dwmapi.dll"); if (m) f = (SetAttr)(void*)GetProcAddress(m, "DwmSetWindowAttribute"); }
        if (f) { BOOL on = TRUE; f(hwnd, 13 /* DWMWA_CLOAK */, &on, sizeof on); Sleep(20); on = FALSE; f(hwnd, 13, &on, sizeof on); }
    } else {
        RECT r; if (GetWindowRect(hwnd, &r)) {
            const int w = r.right - r.left, h = r.bottom - r.top;
            SetWindowPos(hwnd, NULL, r.left, r.top, w, h - 1, SWP_NOZORDER | SWP_NOACTIVATE);
            Sleep(30);
            SetWindowPos(hwnd, NULL, r.left, r.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    fprintf(stderr, "[PWD82] window thread: recovery L%d (%s) done\n", level,
            act == 2 ? "DWM cloak/uncloak" : act == 3 ? "1-px resize" : "frame change"); fflush(stderr);
}
/* ---- end winport v82c-present-wd (window side) ---- */
static char s_wp_title[256];   /* winport-fix: no cross-thread SetWindowTextA */
#define WP_WM_SETTITLE (WM_APP + 0x57)
static LRESULT CALLBACK d3d12_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WP_WM_SETTITLE:
        SetWindowTextA(hwnd, s_wp_title);   /* on the window's own thread */
        return 0;
    case WM_CLOSE:
        s_d3d.window_closed = 1;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        { const char* e_ = getenv("WINPORT_EXIT_ON_CLOSE");   /* winport-fix: window thread */
          if (!(e_ && *e_ == '0')) { printf("[WINPORT] window closed -> exit\n"); TerminateProcess(GetCurrentProcess(), 0); } }
        return 0;
    case WP_WM_PWD82:   /* winport v82c-present-wd */
        wp82_wnd_recover(hwnd, (int)wp);
        return 0;
    case WP_WM_BORDER69:   /* winport v69-fps-f11 */
        wp69_apply_border(hwnd);
        return 0;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:      /* winport v69: F10 is the Windows menu key; it toggles the FPS overlay instead */
        if (wp == VK_F10 && s_wp69_keys_on) return 0;
        break;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && getenv("WINPORT_ESC_QUIT")) {   /* winport-fix: window thread */
            s_d3d.window_closed = 1;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static HWND create_window(u32 width, u32 height, const char* title)
{
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = d3d12_wndproc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "ps3recomp_d3d12";
    RegisterClassExA(&wc);

    RECT wr = {0, 0, (LONG)width, (LONG)height};
    DWORD st68 = WS_OVERLAPPEDWINDOW | WS_VISIBLE; int x68 = CW_USEDEFAULT, y68 = CW_USEDEFAULT;   /* winport v68 */
    if (!wp68_place(&wr, &st68, &x68, &y68))
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    /* Keep the display awake (see rsx_null_backend.c). */
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    return CreateWindowExA(
        0, "ps3recomp_d3d12",
        title ? title : "ps3recomp (D3D12)",
        st68,   /* winport v68 */
        x68, y68,
        wr.right - wr.left, wr.bottom - wr.top,
        NULL, NULL, GetModuleHandle(NULL), NULL);
}

/* ---------------------------------------------------------------------------
 * D3D12 initialization
 * -----------------------------------------------------------------------*/

/* winport-fix: window thread */
static struct { u32 w, h; const char* t; HWND hwnd; HANDLE ready; } s_wp_wnd;
static DWORD WINAPI wp_window_thread(LPVOID a)
{
    (void)a;
    s_wp_wnd.hwnd = create_window(s_wp_wnd.w, s_wp_wnd.h, s_wp_wnd.t);
    SetEvent(s_wp_wnd.ready);
    MSG m;
    while (GetMessageA(&m, NULL, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageA(&m); }
    return 0;
}
static HWND wp_create_window_threaded(u32 w, u32 h, const char* t)
{
    const char* e = getenv("WINPORT_WINDOW_THREAD");
    { extern int wp59_async_rsx_on(void);   /* winport v59-async-rsx: async RSX needs the window thread */
      if (e && *e == '0' && wp59_async_rsx_on()) { fprintf(stderr, "[ASYNC59] WINPORT_WINDOW_THREAD=0 ignored (async RSX)\n"); e = NULL; } }
    if (e && *e == '0') return create_window(w, h, t);
    s_wp_wnd.w = w; s_wp_wnd.h = h; s_wp_wnd.t = t; s_wp_wnd.hwnd = NULL;
    s_wp_wnd.ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE th = s_wp_wnd.ready ? CreateThread(NULL, 0, wp_window_thread, NULL, 0, NULL) : NULL;
    if (!th) return create_window(w, h, t);
    WaitForSingleObject(s_wp_wnd.ready, 10000);
    CloseHandle(th);
    printf("[WINPORT] window on its own thread (hwnd=%p)\n", (void*)s_wp_wnd.hwnd);
    return s_wp_wnd.hwnd;
}

static int init_d3d12(u32 width, u32 height)
{
    HRESULT hr;

    /* Enable debug layer in debug builds */
    /* Debug layer: on in debug builds, or in any build when D3D12_DBG is set
     * (so a Release run can capture exact PSO/validation errors). */
    if (
#ifdef NDEBUG
        getenv("D3D12_DBG")
#else
        1
#endif
    ) {
        ID3D12Debug* debug_controller = NULL;
        hr = D3D12GetDebugInterface(&IID_ID3D12Debug, (void**)&debug_controller);
        if (SUCCEEDED(hr) && debug_controller) {
            debug_controller->lpVtbl->EnableDebugLayer(debug_controller);
            debug_controller->lpVtbl->Release(debug_controller);
            printf("[D3D12] Debug layer enabled\n");
        } else {
            printf("[D3D12] Debug layer requested but unavailable (0x%08lX) -- "
                   "install 'Graphics Tools' optional feature\n", hr);
        }
    }

    /* Create DXGI factory */
    IDXGIFactory4* factory = NULL;
    hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void**)&factory);
    if (FAILED(hr)) {
        printf("[D3D12] ERROR: CreateDXGIFactory1 failed (0x%08lX)\n", hr);
        return -1;
    }

    /* Create D3D12 device. On a dual-GPU laptop a NULL adapter usually lands on
     * the integrated GPU; explicitly pick the high-performance (discrete)
     * adapter via IDXGIFactory6. Set CELLMARK_IGPU to force the low-power one
     * (for A/B testing a suspected iGPU-driver stall). */
    {
        IDXGIFactory6* factory6 = NULL;
        if (SUCCEEDED(factory->lpVtbl->QueryInterface(
                factory, &IID_IDXGIFactory6, (void**)&factory6))) {
            DXGI_GPU_PREFERENCE pref = getenv("CELLMARK_IGPU")
                ? DXGI_GPU_PREFERENCE_MINIMUM_POWER
                : DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
            IDXGIAdapter1* adapter = NULL;
            for (UINT ai = 0; factory6->lpVtbl->EnumAdapterByGpuPreference(
                     factory6, ai, pref, &IID_IDXGIAdapter1, (void**)&adapter)
                     != DXGI_ERROR_NOT_FOUND; ai++) {
                DXGI_ADAPTER_DESC1 ad;
                adapter->lpVtbl->GetDesc1(adapter, &ad);
                if (!(ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                    SUCCEEDED(D3D12CreateDevice((IUnknown*)adapter,
                        D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device,
                        (void**)&s_d3d.device))) {
                    printf("[D3D12] adapter: %ls (VRAM %llu MB)\n", ad.Description,
                           (unsigned long long)(ad.DedicatedVideoMemory >> 20));
                    adapter->lpVtbl->Release(adapter);
                    break;
                }
                adapter->lpVtbl->Release(adapter);
                adapter = NULL;
            }
            factory6->lpVtbl->Release(factory6);
        }
    }
    if (!s_d3d.device) {
        hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0,
                               &IID_ID3D12Device, (void**)&s_d3d.device);
        if (FAILED(hr)) {
            printf("[D3D12] ERROR: D3D12CreateDevice failed (0x%08lX)\n", hr);
            factory->lpVtbl->Release(factory);
            return -1;
        }
        printf("[D3D12] Device created on default adapter (feature level 11.0)\n");
    }

    /* Create command queue */
    D3D12_COMMAND_QUEUE_DESC queue_desc = {0};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = s_d3d.device->lpVtbl->CreateCommandQueue(
        s_d3d.device, &queue_desc, &IID_ID3D12CommandQueue, (void**)&s_d3d.cmd_queue);
    if (FAILED(hr)) {
        printf("[D3D12] ERROR: CreateCommandQueue failed\n");
        factory->lpVtbl->Release(factory);
        return -1;
    }

    /* Create swap chain */
    DXGI_SWAP_CHAIN_DESC1 sc_desc = {0};
    sc_desc.Width = width;
    sc_desc.Height = height;
    sc_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc_desc.SampleDesc.Count = 1;
    sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc_desc.BufferCount = FRAME_COUNT;
    sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    if (wp58_cap() > 0) {   /* winport v58-fpscap */
        IDXGIFactory5* f5 = NULL; BOOL tear = FALSE;
        if (SUCCEEDED(factory->lpVtbl->QueryInterface(factory, &IID_IDXGIFactory5, (void**)&f5))) {
            if (FAILED(f5->lpVtbl->CheckFeatureSupport(f5, DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tear, sizeof tear))) tear = FALSE;
            f5->lpVtbl->Release(f5); }
        s_wp58_tear = tear ? 1 : 0;
        if (tear) sc_desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        fprintf(stderr, "[FPSCAP58] own frame limiter %d fps, tearing present %s\n", wp58_cap(), tear ? "ON" : "unsupported (sync 0 only)");
    }
    IDXGISwapChain1* swap_chain1 = NULL;
    hr = factory->lpVtbl->CreateSwapChainForHwnd(
        factory, (IUnknown*)s_d3d.cmd_queue,
        s_d3d.hwnd, &sc_desc, NULL, NULL, &swap_chain1);
    if (FAILED(hr)) {
        printf("[D3D12] ERROR: CreateSwapChainForHwnd failed (0x%08lX)\n", hr);
        factory->lpVtbl->Release(factory);
        return -1;
    }

    /* Disable Alt+Enter fullscreen toggle */
    factory->lpVtbl->MakeWindowAssociation(factory, s_d3d.hwnd, DXGI_MWA_NO_ALT_ENTER);
    factory->lpVtbl->Release(factory);

    /* Query SwapChain3 interface */
    hr = swap_chain1->lpVtbl->QueryInterface(
        swap_chain1, &IID_IDXGISwapChain3, (void**)&s_d3d.swap_chain);
    swap_chain1->lpVtbl->Release(swap_chain1);
    if (FAILED(hr)) {
        printf("[D3D12] ERROR: QueryInterface for SwapChain3 failed\n");
        return -1;
    }

    s_d3d.frame_index = s_d3d.swap_chain->lpVtbl->GetCurrentBackBufferIndex(s_d3d.swap_chain);

    /* Create RTV descriptor heap */
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {0};
    rtv_heap_desc.NumDescriptors = FRAME_COUNT;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hr = s_d3d.device->lpVtbl->CreateDescriptorHeap(
        s_d3d.device, &rtv_heap_desc, &IID_ID3D12DescriptorHeap, (void**)&s_d3d.rtv_heap);
    if (FAILED(hr)) return -1;

    s_d3d.rtv_descriptor_size = s_d3d.device->lpVtbl->GetDescriptorHandleIncrementSize(
        s_d3d.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    /* Create RTVs for each frame */
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle;
    s_d3d.rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.rtv_heap, &rtv_handle);

    for (u32 i = 0; i < FRAME_COUNT; i++) {
        if (wp58_cap() && wp58_nopresent()) {   /* winport v58e-ownbb: own render-target textures, never presented */
            D3D12_HEAP_PROPERTIES bhp = {0}; bhp.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC brd = {0};
            brd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; brd.Width = width; brd.Height = height;
            brd.DepthOrArraySize = 1; brd.MipLevels = 1; brd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            brd.SampleDesc.Count = 1; brd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; brd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            hr = s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &bhp, D3D12_HEAP_FLAG_NONE, &brd,
                     D3D12_RESOURCE_STATE_PRESENT, NULL, &IID_ID3D12Resource, (void**)&s_d3d.render_targets[i]);
            if (i == 0) fprintf(stderr, "[FPSCAP58] bench no-present: %u own %ux%u back-buffer textures (hr=0x%08lX)\n", FRAME_COUNT, (unsigned)width, (unsigned)height, (long)hr);
        } else
        hr = s_d3d.swap_chain->lpVtbl->GetBuffer(
            s_d3d.swap_chain, i, &IID_ID3D12Resource, (void**)&s_d3d.render_targets[i]);
        if (FAILED(hr)) return -1;

        s_d3d.device->lpVtbl->CreateRenderTargetView(
            s_d3d.device, s_d3d.render_targets[i], NULL, rtv_handle);
        rtv_handle.ptr += s_d3d.rtv_descriptor_size;
    }

    /* ---------------------------------------------------------------
     * Depth/stencil buffer
     * 24-bit depth + 8-bit stencil (DXGI_FORMAT_D24_UNORM_S8_UINT).
     * One shared depth texture across both frames — RSX games on PS3
     * typically use a single zeta surface.
     * ---------------------------------------------------------------*/
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {0};
        dsv_heap_desc.NumDescriptors = 1;
        dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        hr = s_d3d.device->lpVtbl->CreateDescriptorHeap(
            s_d3d.device, &dsv_heap_desc, &IID_ID3D12DescriptorHeap,
            (void**)&s_d3d.dsv_heap);
        if (FAILED(hr)) {
            printf("[D3D12] DSV heap creation failed\n");
            return -1;
        }

        D3D12_HEAP_PROPERTIES heap_props = {0};
        heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC depth_desc = {0};
        depth_desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        /* Sized to cover the LARGEST render target, not just the window:
         * D3D12 clips rasterization to the smallest bound attachment, and
         * this DSV is shared by every pass -- with a window-sized depth
         * buffer, draws into larger offscreen RTs (wave's 1920x1080 input
         * image) were silently cropped to the window rect. */
        depth_desc.Width              = s_wp68_dsv;   /* winport v68: 2048 x scale */
        depth_desc.Height             = s_wp68_dsv;
        depth_desc.DepthOrArraySize   = 1;
        depth_desc.MipLevels          = 1;
        depth_desc.Format             = DXGI_FORMAT_R24G8_TYPELESS;   /* winport-fix: v22 scene depth (DSV below is still D24_UNORM_S8_UINT) */
        depth_desc.SampleDesc.Count   = 1;
        depth_desc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depth_desc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE depth_clear = {0};
        depth_clear.Format                       = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depth_clear.DepthStencil.Depth           = 1.0f;
        depth_clear.DepthStencil.Stencil         = 0;

        hr = s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &heap_props, D3D12_HEAP_FLAG_NONE,
            &depth_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_clear,
            &IID_ID3D12Resource, (void**)&s_d3d.depth_buffer);
        if (FAILED(hr)) {
            printf("[D3D12] Depth buffer creation failed (0x%08lX)\n", hr);
            return -1;
        }

        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {0};
        dsv_desc.Format         = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsv_desc.ViewDimension  = D3D12_DSV_DIMENSION_TEXTURE2D;

        D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle;
        s_d3d.dsv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.dsv_heap, &dsv_handle);
        s_d3d.device->lpVtbl->CreateDepthStencilView(
            s_d3d.device, s_d3d.depth_buffer, &dsv_desc, dsv_handle);

        printf("[D3D12] Depth buffer created (%ux%u D24S8)\n", width, height);
    }

    /* Create command allocators and command list */
    for (u32 i = 0; i < FRAME_COUNT; i++) {
        hr = s_d3d.device->lpVtbl->CreateCommandAllocator(
            s_d3d.device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void**)&s_d3d.cmd_allocators[i]);
        if (FAILED(hr)) return -1;
    }

    hr = s_d3d.device->lpVtbl->CreateCommandList(
        s_d3d.device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        s_d3d.cmd_allocators[0], NULL,
        &IID_ID3D12GraphicsCommandList, (void**)&s_d3d.cmd_list);
    if (FAILED(hr)) return -1;

    /* Close the command list (it starts in recording state) */
    s_d3d.cmd_list->lpVtbl->Close(s_d3d.cmd_list);

    /* Create fence */
    hr = s_d3d.device->lpVtbl->CreateFence(
        s_d3d.device, 0, D3D12_FENCE_FLAG_NONE,
        &IID_ID3D12Fence, (void**)&s_d3d.fence);
    if (FAILED(hr)) return -1;

    s_d3d.fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    memset(s_d3d.fence_values, 0, sizeof(s_d3d.fence_values));

    /* ---------------------------------------------------------------
     * Create root signature with 16 root constants (one mat4 MVP at b0).
     * Visible only to the vertex shader — pixel shader doesn't need it.
     * ---------------------------------------------------------------*/
    {
        /* param 0: mat4 MVP as 16 root constants at b0 (vertex shader).
         * param 1: one SRV (t0) descriptor table for the atlas texture (pixel).
         * static sampler s0: linear clamp. The plain (untextured) PSO simply
         * never references t0/s0, so it ignores them. */
        D3D12_DESCRIPTOR_RANGE srv_range = {0};
        srv_range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors     = 1;
        srv_range.BaseShaderRegister = 0;   /* t0 */
        srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER root_params[2] = {0};
        root_params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_params[0].Constants.ShaderRegister = 0;   /* b0 */
        root_params[0].Constants.RegisterSpace  = 0;
        root_params[0].Constants.Num32BitValues = 16;  /* mat4 */
        root_params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;
        root_params[1].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_params[1].DescriptorTable.NumDescriptorRanges = 1;
        root_params[1].DescriptorTable.pDescriptorRanges   = &srv_range;
        root_params[1].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC samp = {0};
        samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.ShaderRegister   = 0;   /* s0 */
        samp.MaxLOD           = D3D12_FLOAT32_MAX;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rs_desc = {0};
        rs_desc.NumParameters     = 2;
        rs_desc.pParameters       = root_params;
        rs_desc.NumStaticSamplers = 1;
        rs_desc.pStaticSamplers   = &samp;
        rs_desc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
                              | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
                              | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
                              | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        ID3DBlob* signature_blob = NULL;
        ID3DBlob* error_blob = NULL;
        hr = D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                          &signature_blob, &error_blob);
        if (FAILED(hr)) {
            printf("[D3D12] Root signature serialization failed: %s\n",
                   error_blob ? (const char*)error_blob->lpVtbl->GetBufferPointer(error_blob) : "?");
            if (error_blob) error_blob->lpVtbl->Release(error_blob);
            return -1;
        }

        hr = s_d3d.device->lpVtbl->CreateRootSignature(
            s_d3d.device, 0,
            signature_blob->lpVtbl->GetBufferPointer(signature_blob),
            signature_blob->lpVtbl->GetBufferSize(signature_blob),
            &IID_ID3D12RootSignature, (void**)&s_d3d.root_signature);
        signature_blob->lpVtbl->Release(signature_blob);
        if (FAILED(hr)) {
            printf("[D3D12] Root signature creation failed\n");
            return -1;
        }
    }

    /* ---------------------------------------------------------------
     * Compile shaders and create PSO
     * ---------------------------------------------------------------*/
    {
        /* Basic vertex-colored shader.
         * The MVP matrix arrives via root constants as 4 vec4 columns
         * (PS3/OpenGL column-major convention). We multiply explicitly so
         * we don't depend on HLSL's matrix packing — matches PS3 semantics
         * `gl_Position = MVP * vec4(pos, 1.0)`. */
        static const char vs_hlsl[] =
            "cbuffer cb0 : register(b0) {\n"
            "    float4 mvp_col0;\n"
            "    float4 mvp_col1;\n"
            "    float4 mvp_col2;\n"
            "    float4 mvp_col3;\n"
            "};\n"
            "struct VSInput  { float3 pos : POSITION; float4 col : COLOR; };\n"
            "struct VSOutput { float4 pos : SV_POSITION; float4 col : COLOR; };\n"
            "VSOutput main(VSInput i) {\n"
            "    VSOutput o;\n"
            "    float4 p = float4(i.pos, 1.0);\n"
            "    o.pos = mvp_col0 * p.x + mvp_col1 * p.y + mvp_col2 * p.z + mvp_col3 * p.w;\n"
            "    o.col = i.col;\n"
            "    return o;\n"
            "}\n";
        static const char ps_hlsl[] =
            "struct PSInput { float4 pos : SV_POSITION; float4 col : COLOR; };\n"
            "float4 main(PSInput i) : SV_TARGET { return i.col; }\n";

        ID3DBlob* vs_blob = NULL;
        ID3DBlob* ps_blob = NULL;
        ID3DBlob* err = NULL;

        hr = D3DCompile(vs_hlsl, sizeof(vs_hlsl) - 1, "vs_basic", NULL, NULL,
                        "main", "vs_5_0", 0, 0, &vs_blob, &err);
        if (FAILED(hr)) {
            printf("[D3D12] VS compile failed: %s\n",
                   err ? (const char*)err->lpVtbl->GetBufferPointer(err) : "unknown");
            if (err) err->lpVtbl->Release(err);
        }

        hr = D3DCompile(ps_hlsl, sizeof(ps_hlsl) - 1, "ps_basic", NULL, NULL,
                        "main", "ps_5_0", 0, 0, &ps_blob, &err);
        if (FAILED(hr)) {
            printf("[D3D12] PS compile failed: %s\n",
                   err ? (const char*)err->lpVtbl->GetBufferPointer(err) : "unknown");
            if (err) err->lpVtbl->Release(err);
        }

        if (vs_blob && ps_blob) {
            D3D12_INPUT_ELEMENT_DESC input_layout[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
                 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28,
                 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
            pso_desc.pRootSignature = s_d3d.root_signature;
            pso_desc.VS.pShaderBytecode = vs_blob->lpVtbl->GetBufferPointer(vs_blob);
            pso_desc.VS.BytecodeLength = vs_blob->lpVtbl->GetBufferSize(vs_blob);
            pso_desc.PS.pShaderBytecode = ps_blob->lpVtbl->GetBufferPointer(ps_blob);
            pso_desc.PS.BytecodeLength = ps_blob->lpVtbl->GetBufferSize(ps_blob);
            pso_desc.InputLayout.pInputElementDescs = input_layout;
            pso_desc.InputLayout.NumElements = 3;
            pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
                D3D12_COLOR_WRITE_ENABLE_ALL;
            pso_desc.SampleMask = UINT_MAX;
            pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pso_desc.NumRenderTargets = 1;
            pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            pso_desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
            /* Depth test enabled, write enabled, LESS func.
             * Games with depth_test_enable=0 in RSX state can still render —
             * LESS just means new-z must be less than existing — but future
             * work should mirror RSX depth state into a PSO cache. */
            pso_desc.DepthStencilState.DepthEnable    = TRUE;
            pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            pso_desc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            pso_desc.DepthStencilState.StencilEnable  = FALSE;
            pso_desc.SampleDesc.Count = 1;

            hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
                s_d3d.device, &pso_desc,
                &IID_ID3D12PipelineState, (void**)&s_d3d.pipeline_state);
            if (SUCCEEDED(hr)) {
                s_d3d.pipeline_ready = 1;
                printf("[D3D12] Pipeline state created (triangle class)\n");
            } else {
                printf("[D3D12] PSO TRIANGLE creation failed (0x%08lX)\n", hr);
            }

            /* Line-class PSO — same shader, LINE topology type. */
            pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
                s_d3d.device, &pso_desc,
                &IID_ID3D12PipelineState, (void**)&s_d3d.pipeline_state_lines);
            if (SUCCEEDED(hr)) printf("[D3D12] Pipeline state created (line class)\n");
            else printf("[D3D12] PSO LINE creation failed (0x%08lX)\n", hr);

            /* Point-class PSO. */
            pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
            hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
                s_d3d.device, &pso_desc,
                &IID_ID3D12PipelineState, (void**)&s_d3d.pipeline_state_points);
            if (SUCCEEDED(hr)) printf("[D3D12] Pipeline state created (point class)\n");
            else printf("[D3D12] PSO POINT creation failed (0x%08lX)\n", hr);

            vs_blob->lpVtbl->Release(vs_blob);
            ps_blob->lpVtbl->Release(ps_blob);
        }

        /* -----------------------------------------------------------------
         * Textured triangle PSO (dbgfont / 2D atlas quads). Same MVP VS but
         * carrying UV; the PS samples the single-channel atlas as coverage
         * and modulates the vertex color, with straight alpha blending so
         * glyph edges composite over the framebuffer. Depth off (2D overlay).
         * ----------------------------------------------------------------*/
        {
            static const char vs_tex[] =
                "cbuffer cb0 : register(b0){float4 c0;float4 c1;float4 c2;float4 c3;};\n"
                "struct VSIn { float3 pos:POSITION; float4 col:COLOR; float2 uv:TEXCOORD; };\n"
                "struct VSOut{ float4 pos:SV_POSITION; float4 col:COLOR; float2 uv:TEXCOORD; };\n"
                "VSOut main(VSIn i){ VSOut o; float4 p=float4(i.pos,1.0);\n"
                "  o.pos=c0*p.x+c1*p.y+c2*p.z+c3*p.w; o.col=i.col; o.uv=i.uv; return o; }\n";
            static const char ps_tex[] =
                "Texture2D tex : register(t0);\n"
                "SamplerState smp : register(s0);\n"
                "struct PSIn{ float4 pos:SV_POSITION; float4 col:COLOR; float2 uv:TEXCOORD; };\n"
                "float4 main(PSIn i):SV_TARGET{\n"
                /* dbgfont texcoords are compressed ~10x (U) / ~8x (V) vs. the
                 * atlas; scale to recover glyph cells. Exact per-glyph offset
                 * still being calibrated. */
                "  float2 uv2 = float2(i.uv.x*10.0, i.uv.y*8.0 + 0.59);\n"
                "  float cov = tex.Sample(smp, uv2).r;\n"
                "  return float4(i.col.rgb, i.col.a * cov); }\n";

            ID3DBlob *vtb = NULL, *ptb = NULL, *e2 = NULL;
            hr = D3DCompile(vs_tex, sizeof(vs_tex) - 1, "vs_tex", NULL, NULL, "main", "vs_5_0", 0, 0, &vtb, &e2);
            if (FAILED(hr)) printf("[D3D12] VS(tex) compile failed: %s\n", e2 ? (const char*)e2->lpVtbl->GetBufferPointer(e2) : "?");
            hr = D3DCompile(ps_tex, sizeof(ps_tex) - 1, "ps_tex", NULL, NULL, "main", "ps_5_0", 0, 0, &ptb, &e2);
            if (FAILED(hr)) printf("[D3D12] PS(tex) compile failed: %s\n", e2 ? (const char*)e2->lpVtbl->GetBufferPointer(e2) : "?");

            if (vtb && ptb) {
                D3D12_INPUT_ELEMENT_DESC il[] = {
                    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                    {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                };
                D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {0};
                pd.pRootSignature = s_d3d.root_signature;
                pd.VS.pShaderBytecode = vtb->lpVtbl->GetBufferPointer(vtb);
                pd.VS.BytecodeLength  = vtb->lpVtbl->GetBufferSize(vtb);
                pd.PS.pShaderBytecode = ptb->lpVtbl->GetBufferPointer(ptb);
                pd.PS.BytecodeLength  = ptb->lpVtbl->GetBufferSize(ptb);
                pd.InputLayout.pInputElementDescs = il;
                pd.InputLayout.NumElements = 3;
                pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
                pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                pd.BlendState.RenderTarget[0].BlendEnable    = TRUE;
                pd.BlendState.RenderTarget[0].SrcBlend       = D3D12_BLEND_SRC_ALPHA;
                pd.BlendState.RenderTarget[0].DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
                pd.BlendState.RenderTarget[0].BlendOp        = D3D12_BLEND_OP_ADD;
                pd.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ONE;
                pd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                pd.BlendState.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;
                pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                pd.SampleMask = UINT_MAX;
                pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                pd.NumRenderTargets = 1;
                pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
                pd.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
                pd.DepthStencilState.DepthEnable = FALSE;
                pd.DepthStencilState.StencilEnable = FALSE;
                pd.SampleDesc.Count = 1;
                hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
                    s_d3d.device, &pd, &IID_ID3D12PipelineState, (void**)&s_d3d.pipeline_state_tex);
                if (SUCCEEDED(hr)) printf("[D3D12] Pipeline state created (textured class)\n");
                else printf("[D3D12] PSO TEX creation failed (0x%08lX)\n", hr);
            }
            if (vtb) vtb->lpVtbl->Release(vtb);
            if (ptb) ptb->lpVtbl->Release(ptb);
        }

        /* SRV descriptor heap (shader-visible). Layout: slot 0 = legacy atlas
         * (dbgfont / textured 2D path), slots 1-4 = per-draw VP textures,
         * 5-20 = offscreen render targets (render-to-texture), rest spare.
         * All slots start as null SRVs so any 4-wide table window (the
         * root signature binds t0-t3) is valid on tier-1 hardware. */
        {
            D3D12_DESCRIPTOR_HEAP_DESC hd = {0};
            hd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            hd.NumDescriptors = SRV_HEAP_SIZE;
            hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            hr = s_d3d.device->lpVtbl->CreateDescriptorHeap(
                s_d3d.device, &hd, &IID_ID3D12DescriptorHeap, (void**)&s_d3d.srv_heap);
            if (FAILED(hr)) printf("[D3D12] SRV heap creation failed (0x%08lX)\n", hr);
            else {
                s_d3d.srv_inc = s_d3d.device->lpVtbl->GetDescriptorHandleIncrementSize(
                    s_d3d.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                D3D12_SHADER_RESOURCE_VIEW_DESC nv = {0};
                nv.Format = DXGI_FORMAT_R8_UNORM;
                nv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                nv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                nv.Texture2D.MipLevels = 1;
                D3D12_CPU_DESCRIPTOR_HANDLE hh;
                s_d3d.srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &hh);
                for (int _i = 0; _i < SRV_HEAP_SIZE; _i++) {
                    s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, NULL, &nv, hh);
                    hh.ptr += s_d3d.srv_inc;
                }
            }
        }

        /* RTV heap for offscreen render targets (CPU-visible only). */
        {
            D3D12_DESCRIPTOR_HEAP_DESC hd = {0};
            hd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            hd.NumDescriptors = MAX_OFF_RTS;
            hr = s_d3d.device->lpVtbl->CreateDescriptorHeap(
                s_d3d.device, &hd, &IID_ID3D12DescriptorHeap, (void**)&s_d3d.rt_rtv_heap);
            if (FAILED(hr)) printf("[D3D12] offscreen RTV heap creation failed (0x%08lX)\n", hr);
        }
    }

    /* ---------------------------------------------------------------
     * Create dynamic vertex buffer (upload heap, 4MB)
     * ---------------------------------------------------------------*/
    {
        D3D12_HEAP_PROPERTIES heap_props = {0};
        heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC buf_desc = {0};
        buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buf_desc.Width = MAX_VERTICES * VERTEX_STRIDE;
        buf_desc.Height = 1;
        buf_desc.DepthOrArraySize = 1;
        buf_desc.MipLevels = 1;
        buf_desc.SampleDesc.Count = 1;
        buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        hr = s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &heap_props, D3D12_HEAP_FLAG_NONE,
            &buf_desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Resource, (void**)&s_d3d.vertex_buffer);
        if (SUCCEEDED(hr)) {
            D3D12_RANGE read_range = {0, 0};
            s_d3d.vertex_buffer->lpVtbl->Map(
                s_d3d.vertex_buffer, 0, &read_range, &s_d3d.vb_mapped);
            s_d3d.vb_view.BufferLocation =
                s_d3d.vertex_buffer->lpVtbl->GetGPUVirtualAddress(s_d3d.vertex_buffer);
            s_d3d.vb_view.StrideInBytes = VERTEX_STRIDE;
            s_d3d.vb_view.SizeInBytes = MAX_VERTICES * VERTEX_STRIDE;
            printf("[D3D12] Vertex buffer created (%u KB)\n",
                   (MAX_VERTICES * VERTEX_STRIDE) / 1024);
        }
    }

    /* ---------------------------------------------------------------
     * Real vertex-program path resources: root signature (CBV b0 for the
     * vp_c[] bank + SRV t0 + static sampler s0), a raw-float4 vertex buffer,
     * and the constant-bank buffer. The PSO itself is built lazily once the
     * game uploads its VP microcode (render_frame).
     * ---------------------------------------------------------------*/
    {
        /* 4-descriptor SRV table (t0-t3) so decompiled fragment programs can
         * sample up to 4 texture units; the hardcoded atlas/colour PSs use only
         * t0 and are unaffected. Matching 4 static samplers s0-s3. */
        D3D12_DESCRIPTOR_RANGE srv_range = {0};
        srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors = 4;
        srv_range.BaseShaderRegister = 0;
        srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER rp[4] = {0};   /* winport-fix: guest sampler state */
        D3D12_DESCRIPTOR_RANGE wp_sr = {0};
        wp_sr.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; wp_sr.NumDescriptors = 4;
        wp_sr.BaseShaderRegister = 0; wp_sr.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        rp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[3].DescriptorTable.NumDescriptorRanges = 1; rp[3].DescriptorTable.pDescriptorRanges = &wp_sr;
        rp[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;   /* b0 = vp_c bank */
        rp[0].Descriptor.ShaderRegister = 0;
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].DescriptorTable.NumDescriptorRanges = 1;
        rp[1].DescriptorTable.pDescriptorRanges = &srv_range;
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;   /* b1 = FP texscale */
        rp[2].Descriptor.ShaderRegister = 1;
        rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC samp[4] = {0};
        for (int _s = 0; _s < 4; _s++) {
            samp[_s].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            samp[_s].AddressU = samp[_s].AddressV = samp[_s].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            samp[_s].ShaderRegister = (UINT)_s;
            samp[_s].MaxLOD = D3D12_FLOAT32_MAX;
            samp[_s].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        /* s0 keeps point/clamp: the dbgfont atlas PS samples glyph cells and
         * linear filtering bleeds neighbouring glyphs. */
        samp[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samp[0].AddressU = samp[0].AddressV = samp[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

        D3D12_ROOT_SIGNATURE_DESC rd = {0};
        rd.NumParameters = wp_samp_on() ? 4 : 3; rd.pParameters = rp;   /* winport-fix: guest sampler state */
        rd.NumStaticSamplers = wp_samp_on() ? 0 : 4; rd.pStaticSamplers = samp;
        rd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* sig = NULL; ID3DBlob* err = NULL;
        hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
        if (SUCCEEDED(hr)) {
            s_d3d.device->lpVtbl->CreateRootSignature(s_d3d.device, 0,
                sig->lpVtbl->GetBufferPointer(sig), sig->lpVtbl->GetBufferSize(sig),
                &IID_ID3D12RootSignature, (void**)&s_d3d.vp_root_sig);
            sig->lpVtbl->Release(sig);
        } else if (err) { printf("[D3D12] VP root sig: %s\n", (const char*)err->lpVtbl->GetBufferPointer(err)); err->lpVtbl->Release(err); }

        /* raw float4 vertex buffer + constant bank (both UPLOAD, persistently mapped) */
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd = {0};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_RANGE nr = {0,0};

        bd.Width = (u64)MAX_VERTICES * 256 * wp77_nslot();   /* winport v77-fif */  /* generic VP vertex: 16 float4 slots,
                                          * DOUBLE-buffered by frame parity so a
                                          * new frame's upload never overwrites
                                          * vertices the in-flight GPU frame is
                                          * still reading (was: torn/missing
                                          * triangles mixing stale+new verts) */
        /* Failure here leaves vp_vb_mapped NULL and every VP draw silently
         * declines to upload -- the scene just does not render, with no error
         * anywhere. Report the size too: this buffer is
         * MAX_VERTICES * 256 * 2 bytes and grows fast. */
        fprintf(stderr, "[VPVB] per-frame vertex buffer: %llu MB (%u verts, x2 parity)%c",
                (unsigned long long)(bd.Width >> 20), (unsigned)MAX_VERTICES, 10);
        if (SUCCEEDED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp,
                D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void**)&s_d3d.vp_vb)))
            s_d3d.vp_vb->lpVtbl->Map(s_d3d.vp_vb, 0, &nr, &s_d3d.vp_vb_mapped);
        else
            fprintf(stderr, "[VPVB] ALLOCATION FAILED -- no VP geometry will render%c", 10);

        bd.Width = (u64)VP_CB_STRIDE * MAX_DRAWS * wp77_nslot();   /* winport v77-fif */   /* per-draw constant slots, x2 parity */
        /* A failure here is silent and catastrophic: vp_record_cb returns early,
         * every draw loses its constants, and the whole scene transforms by
         * zeros -- indistinguishable from a broken vertex program. Say so. */
        fprintf(stderr, "[VPCB] per-draw constant buffer: %llu MB (%d draws)%c",
                (unsigned long long)(bd.Width >> 20), MAX_DRAWS, 10);
        if (SUCCEEDED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp,
                D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void**)&s_d3d.vp_cb)))
            s_d3d.vp_cb->lpVtbl->Map(s_d3d.vp_cb, 0, &nr, &s_d3d.vp_cb_mapped);

        bd.Width = (u64)VP_FPCB_STRIDE * MAX_DRAWS * wp77_nslot();   /* winport v77-fif */  /* per-draw FP slots (b1), x2 parity */
        if (SUCCEEDED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp,
                D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void**)&s_d3d.vp_fpcb)))
            s_d3d.vp_fpcb->lpVtbl->Map(s_d3d.vp_fpcb, 0, &nr, &s_d3d.vp_fpcb_mapped);

        printf("[D3D12] VP path resources: rootsig=%p vb=%p cb=%p\n",
               (void*)s_d3d.vp_root_sig, (void*)s_d3d.vp_vb, (void*)s_d3d.vp_cb);
    }

    printf("[D3D12] Initialization complete (%ux%u, %u buffers, pipeline=%s)\n",
           width, height, FRAME_COUNT,
           s_d3d.pipeline_ready ? "ready" : "NOT ready");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Frame sync helpers
 * -----------------------------------------------------------------------*/


/* winport-fix: GPU pass timing */
static ID3D12QueryHeap* s_ts_heap = NULL; static ID3D12Resource* s_ts_rb = NULL; static u64 s_ts_freq = 0;
#define WP_TS_MAX 1024
static u32 s_ts_n = 0, s_ts_pend = 0;
static u32 s_ts_rt[WP_TS_MAX], s_ts_wh[WP_TS_MAX], s_ts_nd[WP_TS_MAX];
static u32 s_tsp_rt[WP_TS_MAX], s_tsp_wh[WP_TS_MAX], s_tsp_nd[WP_TS_MAX];
static int wp_gput_on(void) { static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_GPUT"); v = (e && atoi(e)) ? 1 : 0; } return v; }
static void wp_ts_init(void)
{
    static int tried = 0;
    if (tried || !wp_gput_on() || !s_d3d.device || !s_d3d.cmd_queue) return;
    tried = 1;
    static const GUID iid_qh = {0x0d9658ae, 0xed45, 0x469e, {0xa6, 0x1d, 0x97, 0x0e, 0xc5, 0x83, 0xca, 0xb4}};
    D3D12_QUERY_HEAP_DESC qd; memset(&qd, 0, sizeof qd);
    qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qd.Count = WP_TS_MAX;
    if (FAILED(s_d3d.device->lpVtbl->CreateQueryHeap(s_d3d.device, &qd, &iid_qh, (void**)&s_ts_heap))) { s_ts_heap = NULL; return; }
    D3D12_HEAP_PROPERTIES hp; memset(&hp, 0, sizeof hp); hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd; memset(&rd, 0, sizeof rd);
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = WP_TS_MAX * 8; rd.Height = 1; rd.DepthOrArraySize = 1;
    rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&s_ts_rb))) {
        s_ts_heap->lpVtbl->Release(s_ts_heap); s_ts_heap = NULL; s_ts_rb = NULL; return; }
    s_d3d.cmd_queue->lpVtbl->GetTimestampFrequency(s_d3d.cmd_queue, &s_ts_freq);
    fprintf(stderr, "[GPUT] timestamp queries on (freq %llu Hz)\n", (unsigned long long)s_ts_freq);
}
static void wp_ts_mark(u32 rt, u32 w, u32 h)
{
    if (!s_ts_heap || s_ts_n >= WP_TS_MAX - 1) return;
    s_d3d.cmd_list->lpVtbl->EndQuery(s_d3d.cmd_list, s_ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, s_ts_n);
    s_ts_rt[s_ts_n] = rt; s_ts_wh[s_ts_n] = (w << 16) | (h & 0xFFFFu); s_ts_nd[s_ts_n] = 0; s_ts_n++;
}
static void wp_ts_resolve(void)
{
    if (!s_ts_heap || s_ts_n < 1) { s_ts_pend = 0; return; }
    wp_ts_mark(0xFFFFFFFFu, 0, 0);
    s_d3d.cmd_list->lpVtbl->ResolveQueryData(s_d3d.cmd_list, s_ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, s_ts_n, s_ts_rb, 0);
    s_ts_pend = s_ts_n;
    memcpy(s_tsp_rt, s_ts_rt, s_ts_n * 4u); memcpy(s_tsp_wh, s_ts_wh, s_ts_n * 4u); memcpy(s_tsp_nd, s_ts_nd, s_ts_n * 4u);
    s_ts_n = 0;
}
static void wp_ts_collect(double cpu_wait_s)
{
    static double acc_ms = 0.0, acc_wait = 0.0; static u32 nfr = 0;
    static struct { u32 rt, wh; double ms; u32 nd, np; } tab[96]; static int nt = 0;
    if (!s_ts_heap) return;
    acc_wait += cpu_wait_s * 1000.0;
    if (s_ts_pend >= 2) {
        D3D12_RANGE rr; rr.Begin = 0; rr.End = (SIZE_T)s_ts_pend * 8u; u64* t = NULL;
        if (SUCCEEDED(s_ts_rb->lpVtbl->Map(s_ts_rb, 0, &rr, (void**)&t)) && t) {
            for (u32 i = 0; i + 1 < s_ts_pend; i++) {
                double ms = (double)(t[i + 1] - t[i]) * 1000.0 / (double)s_ts_freq;
                int k = -1; for (int j = 0; j < nt; j++) if (tab[j].rt == s_tsp_rt[i] && tab[j].wh == s_tsp_wh[i]) { k = j; break; }
                if (k < 0 && nt < 96) { k = nt++; tab[k].rt = s_tsp_rt[i]; tab[k].wh = s_tsp_wh[i]; tab[k].ms = 0; tab[k].nd = 0; tab[k].np = 0; }
                if (k >= 0) { tab[k].ms += ms; tab[k].nd += s_tsp_nd[i]; tab[k].np++; }
                acc_ms += ms;
            }
            D3D12_RANGE wr; wr.Begin = 0; wr.End = 0; s_ts_rb->lpVtbl->Unmap(s_ts_rb, 0, &wr);
            nfr++;
        }
        s_ts_pend = 0;
    }
    if (nfr >= 20) {
        fprintf(stderr, "[GPUT] per frame (avg of %u): GPU busy %.1f ms | CPU waited for GPU %.1f ms |", nfr, acc_ms / nfr, acc_wait / nfr);
        for (int r = 0; r < 8; r++) {
            int b = -1; for (int j = 0; j < nt; j++) if (tab[j].ms >= 0 && (b < 0 || tab[j].ms > tab[b].ms)) b = j;
            if (b < 0 || tab[b].ms <= 0.0) break;
            fprintf(stderr, " [rt=0x%X %ux%u %.1fms %ud %up]", tab[b].rt, tab[b].wh >> 16, tab[b].wh & 0xFFFFu,
                    tab[b].ms / nfr, tab[b].nd / nfr, tab[b].np / nfr);
            tab[b].ms = -1.0;
        }
        fprintf(stderr, "\n");
        nt = 0; nfr = 0; acc_ms = 0.0; acc_wait = 0.0;
    }
}

/* ---- winport v77-fif (fix_v77_frames_in_flight_renderer.py): two frames in flight ---- */
static ID3D12Fence* s77_fence; static HANDLE s77_ev;
static u64 s77_val;                 /* last serial signalled after a submission */
static u64 s77_par_use[3];          /* serial of the last submission that used each per-frame slot */
static u64 s77_tex_use[VP_TEX_SLOTS], s77_mv_use, s77_ui_use, s77_gfb_use;
static u32 s77_frame_par;           /* slot latched at frame start (SRV window offset follows it) */
static int s77_legacy;              /* this frame used the legacy (non-slotted) vertex buffer */
static double s77_wait_s;           /* waits since the last [GPUT] collect */
static unsigned long long s77_wn[7]; static double s77_wms[7]; static unsigned long long s77_frames, s77_ts_drop;
static int wp77_on(void)
{
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("WINPORT_FRAMES_IN_FLIGHT"); const int n = e ? atoi(e) : 1;
        v = 0;
        if (n >= 2) {
            if (wp58_nbuf() >= 3) v = 1;
            else fprintf(stderr, "[FIF77] WINPORT_FRAMES_IN_FLIGHT=%d needs the 3-buffer swap chain (WINPORT_FPS_CAP) -> off\n", n);
        }
        if (v) fprintf(stderr, "[FIF77] 2 frames in flight: 3 per-frame slots (vp_vb/vp_cb/vp_fpcb/SRV windows), "
                               "no full GPU drain at frame start, own fence per submission\n");
        fflush(stderr);
    }
    return v;
}
static u32 wp77_nslot(void) { return wp77_on() ? 3u : 2u; }
static u32 wp77_prev_par(void) { return (u32)((s_d3d.vp_parity + (int)wp77_nslot() - 1) % (int)wp77_nslot()); }
/* reason: 0 frame slot, 1 slot at parity advance, 2 texture slot, 3 movie, 4 UI / guest fb, 5 release / idle, 6 legacy vb */
static void wp77_wait_wk85d(u64 serial, int why);
static void wp77_wait(u64 serial, int why) { const unsigned long long t_ = WK85D_T0(); wp77_wait_wk85d(serial, why); WK85D_ADD(0, t_); }   /* winport v85d-walkprof-waits */
static void wp77_wait_wk85d(u64 serial, int why)
{
    if (!s77_fence || !serial) return;
    if (s77_fence->lpVtbl->GetCompletedValue(s77_fence) >= serial) return;
    const double t0 = perf_now();
    s77_fence->lpVtbl->SetEventOnCompletion(s77_fence, serial, s77_ev);
    if (WaitForSingleObject(s77_ev, 2000) == WAIT_TIMEOUT)
        fprintf(stderr, "[FIF77] wait STUCK 2s (reason %d): want %llu got %llu\n", why, (unsigned long long)serial,
                (unsigned long long)s77_fence->lpVtbl->GetCompletedValue(s77_fence));
    const double dt = perf_now() - t0;
    s77_wait_s += dt;
    if (why >= 0 && why < 7) { s77_wn[why]++; s77_wms[why] += dt * 1000.0; }
}
static void wp77_idle(int why) { if (wp77_on()) wp77_wait(s77_val, why); }   /* every submitted frame done */
static void wp77_after_submit(void)
{
    if (!wp77_on() || !s_d3d.device || !s_d3d.cmd_queue) return;
    if (!s77_fence) {
        if (FAILED(s_d3d.device->lpVtbl->CreateFence(s_d3d.device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&s77_fence)))
            { s77_fence = NULL; return; }
        s77_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    }
    s77_val++;
    s_d3d.cmd_queue->lpVtbl->Signal(s_d3d.cmd_queue, s77_fence, s77_val);
    s77_par_use[s77_frame_par % 3u] = s77_val;
}
/* Frame start (render_frame and the movie-only present): latch the slot, wait for its last user. */
static void wp77_frame_begin(void)
{
    s77_frame_par = (u32)s_d3d.vp_parity % 3u;
    s77_srv_off = wp77_on() ? s77_frame_par * (u32)WP77_FRAME_SRVS : 0u;
    s77_legacy = s_d3d.vb_offset != 0;
    wp77_wait(s77_par_use[s77_frame_par], 0);
}
static double wp77_take_wait(void) { const double w = s77_wait_s; s77_wait_s = 0.0; return w; }
static void wp77_report(void)
{
    static double last = 0.0;
    s77_frames++;
    const double now = perf_now();
    if (last == 0.0) { last = now; return; }
    if (now - last < 1.0) return;
    const double s = now - last; last = now;
    double tot = 0.0; for (int i = 0; i < 7; i++) tot += s77_wms[i];
    if (!wp_stats_on()) ; else fprintf(stderr, "[FIF77] frames=%.0f/s wait=%.2fms/frame | slot %llu/%.1fms adv %llu/%.1fms tex %llu/%.1fms movie %llu/%.1fms "
                    "ui %llu/%.1fms idle %llu/%.1fms legacy %llu/%.1fms | gput dropped %llu | serial %llu\n",
            s77_frames / s, s77_frames ? tot / (double)s77_frames : 0.0,
            s77_wn[0], s77_wms[0], s77_wn[1], s77_wms[1], s77_wn[2], s77_wms[2], s77_wn[3], s77_wms[3],
            s77_wn[4], s77_wms[4], s77_wn[5], s77_wms[5], s77_wn[6], s77_wms[6], s77_ts_drop, (unsigned long long)s77_val);
    for (int i = 0; i < 7; i++) { s77_wn[i] = 0; s77_wms[i] = 0.0; }
    s77_frames = 0; s77_ts_drop = 0;
}
/* ---- end winport v77-fif helpers ---- */
/* ---- winport v82c-fence-diag (fix_v82c_fence_diag_renderer.py): WINPORT_FENCE82=1 logs early presents ---- */
/* ---- winport v85a-walkprof (fix_v85a_walkprof_renderer.py): WINPORT_WALKPROF85=1 inclusive TSC buckets ---- */
enum { WK85_drawI, WK85_drawA, WK85_bind, WK85_vattr, WK85_rt, WK85_clear, WK85_shader, WK85_vp, WK85_blend, WK85_depth, WK85_blit, WK85_beginf, WK85_endf, WK85_present, WK85_vtx, WK85_vpf, WK85_defq, WK85_vs, WK85_cb, WK85_fpsnap, WK85_room, WK85_rf, WK85_join, WK85_texup, WK85_srvw, WK85_pso, WK85_dxgi, WK85_N };
static const char* const s_wk85_name[WK85_N] = { "drawI", "drawA", "bind", "vattr", "rt", "clear", "shader", "vp", "blend", "depth", "blit", "beginf", "endf", "present", "vtx", "vpf", "defq", "vs", "cb", "fpsnap", "room", "rf", "join", "texup", "srvw", "pso", "dxgi" };
static int s_wk85_on = -1;
static unsigned long long s_wk85_c[WK85_N], s_wk85_n[WK85_N];
static int wk85_on(void)
{
    if (s_wk85_on < 0) { const char* e = getenv("WINPORT_WALKPROF85"); s_wk85_on = (e && e[0] == '1') ? 1 : 0;
                         if (s_wk85_on) fprintf(stderr, "[WALK85] walker TSC buckets ON (inclusive ms/s, calls/s)\n"); }
    return s_wk85_on;
}
#define WK85_T0() (__builtin_expect(s_wk85_on > 0, 0) ? __builtin_ia32_rdtsc() : (s_wk85_on < 0 && wk85_on() ? __builtin_ia32_rdtsc() : 0ull))
#define WK85_ADD(b, t) do { if (t) { s_wk85_c[b] += __builtin_ia32_rdtsc() - (t); s_wk85_n[b]++; } } while (0)
static void wk85_report(void)
{
    static LARGE_INTEGER q0; static unsigned long long c0;
    if (s_wk85_on <= 0) return;
    LARGE_INTEGER q, fq; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&fq);
    const unsigned long long c = __builtin_ia32_rdtsc();
    if (!q0.QuadPart) { q0 = q; c0 = c; memset(s_wk85_c, 0, sizeof s_wk85_c); memset(s_wk85_n, 0, sizeof s_wk85_n); return; }
    const double s = (double)(q.QuadPart - q0.QuadPart) / (double)fq.QuadPart;
    if (s < 1.0) return;
    const double tps = (double)(c - c0) / s;   /* TSC ticks per second */
    char line[1600]; int o = snprintf(line, sizeof line, "[WALK85] ms/s (calls/s):");
    for (int b = 0; b < WK85_N; b++) {
        if (b == WK85_vtx || b == WK85_rf) o += snprintf(line + o, sizeof line - (size_t)o, " |");
        o += snprintf(line + o, sizeof line - (size_t)o, " %s %.1f (%.0f)", s_wk85_name[b], s_wk85_c[b] * 1000.0 / tps / s, s_wk85_n[b] / s);
        if (o > (int)sizeof line - 64) break;
    }
    fprintf(stderr, "%s\n", line);
    wk85d_report(tps, s);   /* winport v85d-walkprof-waits */
    memset(s_wk85_c, 0, sizeof s_wk85_c); memset(s_wk85_n, 0, sizeof s_wk85_n);
    q0 = q; c0 = c;
}
/* ---- end winport v85a-walkprof helpers ---- */
static HRESULT wp82_present_wk85(UINT sync, UINT flags, int site);
static HRESULT wp82_present(UINT sync, UINT flags, int site) { const unsigned long long t_ = WK85_T0(); HRESULT r_ = wp82_present_wk85(sync, flags, site); WK85_ADD(WK85_dxgi, t_); return r_; }   /* winport v85a-walkprof */
static HRESULT wp82_present_wk85(UINT sync, UINT flags, int site)
{
    static int on = -1; static long n = 0;
    if (on < 0) { const char* e = getenv("WINPORT_FENCE82"); on = (e && e[0] == '1') ? 1 : 0; }
    { extern void wp82_wd_start_fwd(void); wp82_wd_start_fwd(); }   /* winport v82c-present-wd */
    if (!on) return s_d3d.swap_chain->lpVtbl->Present(s_d3d.swap_chain, sync, flags);
    const UINT bb = s_d3d.swap_chain->lpVtbl->GetCurrentBackBufferIndex(s_d3d.swap_chain);
    const double t0 = perf_now();
    const HRESULT hr = s_d3d.swap_chain->lpVtbl->Present(s_d3d.swap_chain, sync, flags);
    const double ms = (perf_now() - t0) * 1000.0;
    n++;
    if (n <= 40 || FAILED(hr) || hr != S_OK || ms > 50.0) {
        fprintf(stderr, "[FENCE82] #%ld site=%d tid=%lu fi=%u bb=%u->%u sync=%u flags=0x%X hr=0x%08lX took=%.2fms "
                        "s77 sub=%llu done=%llu | fence want=%llu done=%llu | iconic=%d fg=%d\n",
                n, site, (unsigned long)GetCurrentThreadId(), s_d3d.frame_index, bb,
                s_d3d.swap_chain->lpVtbl->GetCurrentBackBufferIndex(s_d3d.swap_chain), sync, flags, (long)hr, ms,
                (unsigned long long)s77_val, s77_fence ? (unsigned long long)s77_fence->lpVtbl->GetCompletedValue(s77_fence) : 0ull,
                (unsigned long long)s_d3d.fence_values[s_d3d.frame_index],
                (unsigned long long)s_d3d.fence->lpVtbl->GetCompletedValue(s_d3d.fence),
                s_d3d.hwnd ? (int)IsIconic(s_d3d.hwnd) : -1, s_d3d.hwnd ? (int)(GetForegroundWindow() == s_d3d.hwnd) : -1);
        fflush(stderr);
    }
    return hr;
}
/* ---- end winport v82c-fence-diag ---- */
/* ---- winport v82c-present-wd (fix_v82c_present_wd_renderer.py): GPU-queue stall watchdog, WINPORT_PRESENT_WD82=1 ---- */
static volatile LONG s82_wd_started;
static DWORD WINAPI wp82_wd_thread(LPVOID arg)
{
    (void)arg;
    int ms = 500; { const char* e = getenv("WINPORT_PRESENT_WD82_MS"); if (e && atoi(e) >= 100) ms = atoi(e); }
    fprintf(stderr, "[PWD82] present watchdog on (stall = GPU busy and not advancing for %d ms)\n", ms); fflush(stderr);
    unsigned long long last_done = 0, stall_t0 = 0, t_stall_start = 0; int level = 0, events = 0;
    for (;;) {
        Sleep(100);
        if (!s77_fence) continue;
        const unsigned long long sub = (unsigned long long)s77_val;
        const unsigned long long done = (unsigned long long)s77_fence->lpVtbl->GetCompletedValue(s77_fence);
        const unsigned long long now = GetTickCount64();
        if (done != last_done || done >= sub) {
            if (level > 0) {
                fprintf(stderr, "[PWD82] GPU moving again after L%d (%llu ms stalled; done %llu -> %llu, sub %llu)\n",
                        level, now - t_stall_start, last_done, done, sub); fflush(stderr);
            }
            last_done = done; stall_t0 = now; level = 0;
            continue;
        }
        if (now - stall_t0 < (unsigned long long)ms) continue;
        HWND h = s_d3d.hwnd;
        if (level == 0) {
            t_stall_start = stall_t0; events++;
            DWORD_PTR res = 0; const DWORD t0 = GetTickCount();
            const LRESULT ok = h ? SendMessageTimeoutA(h, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 300, &res) : 0;
            int cloaked = -1;
            { typedef HRESULT (WINAPI *GetAttr)(HWND, DWORD, PVOID, DWORD); static GetAttr g = NULL; static int tried = 0;
              if (!tried) { tried = 1; HMODULE m = LoadLibraryA("dwmapi.dll"); if (m) g = (GetAttr)(void*)GetProcAddress(m, "DwmGetWindowAttribute"); }
              DWORD c = 0; if (g && h && SUCCEEDED(g(h, 14 /* DWMWA_CLOAKED */, &c, sizeof c))) cloaked = (int)c; }
            fprintf(stderr, "[PWD82] STALL #%d: GPU done %llu of %llu submissions, no progress for %llu ms | window thread %s (%lu ms) "
                            "visible=%d iconic=%d cloaked=%d fg=%d hung=%d\n",
                    events, done, sub, now - stall_t0, ok ? "responsive" : "NOT responding", (unsigned long)(GetTickCount() - t0),
                    h ? (int)IsWindowVisible(h) : -1, h ? (int)IsIconic(h) : -1, cloaked, h ? (int)(GetForegroundWindow() == h) : -1,
                    h ? (int)IsHungAppWindow(h) : -1);
            fflush(stderr);
        }
        if (level < 3 && h) {
            level++;
            fprintf(stderr, "[PWD82] recovery step L%d posted to the window thread\n", level); fflush(stderr);
            PostMessageA(h, WP_WM_PWD82, (WPARAM)level, 0);
        } else if (level == 3) {
            level = 4;
            fprintf(stderr, "[PWD82] all recovery steps tried; still stalled\n"); fflush(stderr);
        }
        stall_t0 = now;   /* next step after another stall period */
    }
    return 0;
}
static void wp82_wd_start(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("WINPORT_PRESENT_WD82"); on = (e && e[0] == '1') ? 1 : 0; }
    if (!on || s82_wd_started || InterlockedExchange(&s82_wd_started, 1)) return;
    HANDLE t = CreateThread(NULL, 0, wp82_wd_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
/* ---- end winport v82c-present-wd ---- */
void wp82_wd_start_fwd(void) { wp82_wd_start(); }   /* winport v82c-present-wd */
static void wait_for_gpu_wk85d(void);
static void wait_for_gpu(void) { const unsigned long long t_ = WK85D_T0(); wait_for_gpu_wk85d(); WK85D_ADD(1, t_); }   /* winport v85d-walkprof-waits */
static void wait_for_gpu_wk85d(void)
{
    u32 fi = s_d3d.frame_index;
    s_d3d.fence_values[fi]++;
    s_d3d.cmd_queue->lpVtbl->Signal(s_d3d.cmd_queue, s_d3d.fence, s_d3d.fence_values[fi]);

    if (s_d3d.fence->lpVtbl->GetCompletedValue(s_d3d.fence) < s_d3d.fence_values[fi]) {
        s_d3d.fence->lpVtbl->SetEventOnCompletion(
            s_d3d.fence, s_d3d.fence_values[fi], s_d3d.fence_event);
        /* NEVER wait unbounded here: this runs on the vblank ticker, which also
         * drives the guest's vblank handlers, the FIFO drain, and the fence
         * publication. A device removal (TDR) leaves the D3D12 fence unsignaled
         * forever and an INFINITE wait silently froze the ENTIRE emulation --
         * observed on LBP as a boot that died the moment the 2048x2048
         * offscreen RT was created (last log line), every guest thread parked.
         * Degrade loudly instead: bounded waits + the removal reason, then
         * carry on (subsequent D3D calls fail visibly but the game keeps
         * running headless). */
        for (int tries = 0; tries < 3; tries++) {
            if (WaitForSingleObject(s_d3d.fence_event, 2000) != WAIT_TIMEOUT)
                return;
            HRESULT rr = s_d3d.device->lpVtbl->GetDeviceRemovedReason(s_d3d.device);
            printf("[D3D12] wait_for_gpu STUCK %ds: want %llu got %llu removed=0x%08lX\n",
                   2 * (tries + 1),
                   (unsigned long long)s_d3d.fence_values[fi],
                   (unsigned long long)s_d3d.fence->lpVtbl->GetCompletedValue(s_d3d.fence),
                   (long)rr);
            fflush(stdout);
            if (rr != 0 /* S_OK: device still alive, keep waiting a bit */)
                break;
        }
    }
}

static void move_to_next_frame_wk85d(void);
static void move_to_next_frame(void) { const unsigned long long t_ = WK85D_T0(); move_to_next_frame_wk85d(); WK85D_ADD(2, t_); }   /* winport v85d-walkprof-waits */
static void move_to_next_frame_wk85d(void)
{
    u64 current_fence = s_d3d.fence_values[s_d3d.frame_index];
    s_d3d.cmd_queue->lpVtbl->Signal(s_d3d.cmd_queue, s_d3d.fence, current_fence);

    if (s_wp58_skipped) { s_wp58_skipped = 0; s_d3d.frame_index = (s_d3d.frame_index + 1) % FRAME_COUNT; }   /* winport v58b-iconic */
    else
    s_d3d.frame_index = s_d3d.swap_chain->lpVtbl->GetCurrentBackBufferIndex(s_d3d.swap_chain);

    if (s_d3d.fence->lpVtbl->GetCompletedValue(s_d3d.fence) < s_d3d.fence_values[s_d3d.frame_index]) {
        s_d3d.fence->lpVtbl->SetEventOnCompletion(
            s_d3d.fence, s_d3d.fence_values[s_d3d.frame_index], s_d3d.fence_event);
        if (WaitForSingleObject(s_d3d.fence_event, 2000) == WAIT_TIMEOUT) {
            HRESULT rr = s_d3d.device->lpVtbl->GetDeviceRemovedReason(s_d3d.device);
            printf("[D3D12] FENCE STUCK 2s: want %llu got %llu removed=0x%08lX\n",
                   (unsigned long long)s_d3d.fence_values[s_d3d.frame_index],
                   (unsigned long long)s_d3d.fence->lpVtbl->GetCompletedValue(s_d3d.fence),
                   (long)rr);
        }
    }

    if (FRAME_COUNT > 2 && !wp77_on()) {   /* winport v77-fif: off with 2 frames in flight | winport v58c-threebuf: keep at most ONE frame in flight (as with 2 buffers) */
        static u64 prev_sig;
        if (prev_sig && s_d3d.fence->lpVtbl->GetCompletedValue(s_d3d.fence) < prev_sig) {
            s_d3d.fence->lpVtbl->SetEventOnCompletion(s_d3d.fence, prev_sig, s_d3d.fence_event);
            WaitForSingleObject(s_d3d.fence_event, 2000);
        }
        prev_sig = current_fence;
    }
    s_d3d.fence_values[s_d3d.frame_index] = current_fence + 1;
}

/* ---------------------------------------------------------------------------
 * Render a frame (clear + present)
 * -----------------------------------------------------------------------*/

/* Write the mapped readback buffer (R8G8B8A8, row pitch = readback_pitch) out
 * as a 24-bit bottom-up BMP. Debug-only. */
/* Name prefix for the next readback dump: lets the same writer emit the
 * backbuffer and the screen-copy texture under different filenames. */
static const char* s_dump_name = NULL;

static void dump_backbuffer_bmp(void)
{
    if (!s_d3d.readback_buf) return;
    void* mapped = NULL;
    D3D12_RANGE rr = {0, (SIZE_T)s_d3d.readback_pitch * WP68_BH};
    if (FAILED(s_d3d.readback_buf->lpVtbl->Map(s_d3d.readback_buf, 0, &rr, &mapped)) || !mapped)
        return;

    static int idx = 0;
    char path[512];
    const char* dir = getenv("CELLMARK_DUMP_DIR");   /* default: current dir */
    snprintf(path, sizeof(path), "%s%s%s_%03d.bmp",
             dir ? dir : "", dir ? "/" : "", s_dump_name ? s_dump_name : "frame", idx++);
    FILE* f = fopen(path, "wb");
    if (f) {
        u32 w = WP68_BW, h = WP68_BH;
        u32 padded = (w * 3 + 3) & ~3u;
        u32 imgsz  = padded * h;
        u32 filesz = 54 + imgsz;
        unsigned char hdr[54] = {0};
        hdr[0] = 'B'; hdr[1] = 'M';
        hdr[2]=filesz&0xFF; hdr[3]=(filesz>>8)&0xFF; hdr[4]=(filesz>>16)&0xFF; hdr[5]=(filesz>>24)&0xFF;
        hdr[10]=54; hdr[14]=40;
        hdr[18]=w&0xFF; hdr[19]=(w>>8)&0xFF; hdr[20]=(w>>16)&0xFF; hdr[21]=(w>>24)&0xFF;
        hdr[22]=h&0xFF; hdr[23]=(h>>8)&0xFF; hdr[24]=(h>>16)&0xFF; hdr[25]=(h>>24)&0xFF;
        hdr[26]=1; hdr[28]=24;
        hdr[34]=imgsz&0xFF; hdr[35]=(imgsz>>8)&0xFF; hdr[36]=(imgsz>>16)&0xFF; hdr[37]=(imgsz>>24)&0xFF;
        fwrite(hdr, 1, 54, f);
        unsigned char* row = (unsigned char*)malloc(padded);
        if (row) {
            memset(row, 0, padded);
            for (int y = (int)h - 1; y >= 0; y--) {   /* BMP is bottom-up */
                unsigned char* src = (unsigned char*)mapped + (u32)y * s_d3d.readback_pitch;
                for (u32 x = 0; x < w; x++) {
                    row[x*3+0] = src[x*4+2]; /* B */
                    row[x*3+1] = src[x*4+1]; /* G */
                    row[x*3+2] = src[x*4+0]; /* R */
                }
                fwrite(row, 1, padded, f);
            }
            free(row);
        }
        fclose(f);
        printf("[D3D12] dumped %s (%ux%u)\n", path, WP68_BW, WP68_BH);
    }
    D3D12_RANGE wr = {0, 0};
    s_d3d.readback_buf->lpVtbl->Unmap(s_d3d.readback_buf, 0, &wr);
}

/* ---------------------------------------------------------------------------
 * RSX frame capture ("rsxcap") -- a purpose-built GPU debugger. RSX_CAP=start
 * [:count[:stride]] snapshots whole frames to <RSX_CAP_DIR>/frame_NN/: a text
 * manifest of every op (in submission order, with full surface/shader/texture
 * state) plus a BMP of the backbuffer and every offscreen colour RT used that
 * frame. One frame, captured atomically -- no cross-frame guessing.
 * -----------------------------------------------------------------------*/

/* Shared 24-bit BMP writer for readbacks. R8G8B8A8_UNORM straight; the half/
 * float RT formats are |v|-tonemapped so signed/HDR data is visible. */
static void wp_zs_dump_all(u32 fi, const char* dir);   /* winport-fix: v22b zeta dump */
static void cap_write_bmp(const char* path, const void* mapped, u32 w, u32 h,
                          u32 pitch, u32 dxgi)
{
    FILE* f = fopen(path, "wb");
    if (!f) return;
    u32 rowb = (w * 3 + 3) & ~3u, datasz = rowb * h;
    u8 hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    *(u32*)(hdr+2) = 54 + datasz; *(u32*)(hdr+10) = 54; *(u32*)(hdr+14) = 40;
    *(int*)(hdr+18) = (int)w; *(int*)(hdr+22) = (int)h;
    *(u16*)(hdr+26) = 1; *(u16*)(hdr+28) = 24; *(u32*)(hdr+34) = datasz;
    fwrite(hdr, 1, 54, f);
    u8* line = (u8*)malloc(rowb);
    if (line) for (int y = (int)h - 1; y >= 0; y--) {   /* BMP is bottom-up */
        const u8* srow = (const u8*)mapped + (u64)y * pitch;
        memset(line, 0, rowb);
        for (u32 x = 0; x < w; x++) {
            float rv, gv, bv;
            if (dxgi == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                const u16* hp = (const u16*)(srow + (u64)x * 8);
                float v[3];
                for (int c = 0; c < 3; c++) {
                    u16 hv = hp[c]; u32 s = (hv>>15)&1, e = (hv>>10)&0x1F, m = hv&0x3FF; float fv;
                    if (e == 0) fv = (float)m / 16777216.0f;
                    else { u32 fb = (s<<31)|((e-15+127)<<23)|(m<<13); memcpy(&fv,&fb,4); }
                    v[c] = fv;
                }
                rv = v[0]; gv = v[1]; bv = v[2];
            } else if (dxgi == DXGI_FORMAT_R32G32B32A32_FLOAT) {
                const float* fp = (const float*)(srow + (u64)x * 16);
                rv = fp[0]; gv = fp[1]; bv = fp[2];
            } else {
                const u8* p = srow + (u64)x * 4;
                rv = p[0]/255.0f; gv = p[1]/255.0f; bv = p[2]/255.0f;
            }
            float ar = rv<0?-rv:rv, ag = gv<0?-gv:gv, ab = bv<0?-bv:bv;
            if (ar>1) ar=1; if (ag>1) ag=1; if (ab>1) ab=1;
            line[x*3+0] = (u8)(ab*255.0f);
            line[x*3+1] = (u8)(ag*255.0f);
            line[x*3+2] = (u8)(ar*255.0f);
        }
        fwrite(line, 1, rowb, f);
    }
    free(line);
    fclose(f);
}

/* Copy one GPU resource into a fresh readback buffer and write it as a BMP.
 * Self-contained: waits, resets the frame command list, copies, executes,
 * waits, maps. `prior` is the resource's current tracked state (restored after
 * the copy so the caller's state tracking stays valid). */
static void cap_readback_write(ID3D12Resource* res, u32 w, u32 h, u32 dxgi,
                               D3D12_RESOURCE_STATES prior, u32 fi, const char* path)
{
    if (!res || !w || !h) return;
    u32 bpp = (dxgi == DXGI_FORMAT_R16G16B16A16_FLOAT) ? 8 :
              (dxgi == DXGI_FORMAT_R32G32B32A32_FLOAT) ? 16 : 4;
    u32 pitch = (w * bpp + 255) & ~255u;

    ID3D12Resource* rb = NULL;
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd = {0};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (u64)pitch * h; rd.Height = 1; rd.DepthOrArraySize = 1;
    rd.MipLevels = 1; rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void**)&rb)) || !rb)
        return;

    wait_for_gpu();
    s_d3d.cmd_allocators[fi]->lpVtbl->Reset(s_d3d.cmd_allocators[fi]);
    s_d3d.cmd_list->lpVtbl->Reset(s_d3d.cmd_list, s_d3d.cmd_allocators[fi], NULL);

    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (prior != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        b.Transition.StateBefore = prior;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    }

    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
    dst.pResource = rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = (DXGI_FORMAT)dxgi;
    dst.PlacedFootprint.Footprint.Width = w; dst.PlacedFootprint.Footprint.Height = h;
    dst.PlacedFootprint.Footprint.Depth = 1; dst.PlacedFootprint.Footprint.RowPitch = pitch;
    src.pResource = res; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);
        /* TEXUP_DBG=1: texture uploads per second. A cache that misses every
         * draw re-uploads megabytes per frame, which is the kind of GPU work
         * that trips a 2-second TDR while looking like nothing in a draw count. */
        { static int td = -1; if (td < 0) td = getenv("TEXUP_DBG") ? 1 : 0;
          if (td) { static long long n = 0, bytes = 0; static clock_t t0 = 0;
              n++; bytes += (long long)pitch * h;
              const clock_t now = clock();
              if (!t0) t0 = now;
              if (now - t0 >= CLOCKS_PER_SEC) {
                  fprintf(stderr, "[TEXUP] %lld uploads/s, %.1f MB/s\n",
                          n, bytes / 1048576.0);
                  n = 0; bytes = 0; t0 = now; } } }

    if (prior != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.StateAfter  = prior;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    }

    s_d3d.cmd_list->lpVtbl->Close(s_d3d.cmd_list);
    ID3D12CommandList* cl[] = { (ID3D12CommandList*)s_d3d.cmd_list };
    s_d3d.cmd_queue->lpVtbl->ExecuteCommandLists(s_d3d.cmd_queue, 1, cl);
    wait_for_gpu();

    void* mp = NULL; D3D12_RANGE rr = {0, (SIZE_T)pitch * h};
    if (SUCCEEDED(rb->lpVtbl->Map(rb, 0, &rr, &mp)) && mp) {
        cap_write_bmp(path, mp, w, h, pitch, dxgi);
        D3D12_RANGE wr = {0, 0};
        rb->lpVtbl->Unmap(rb, 0, &wr);
    }
    rb->lpVtbl->Release(rb);
}

static void rsx_capture_frame(u32 fi, u32 ndraws, u32 capidx)
{
    char dir[300]; const char* base = getenv("RSX_CAP_DIR");
    if (!base || !*base) base = "rsxcap";
    CreateDirectoryA(base, NULL);
    snprintf(dir, sizeof dir, "%s/frame_%02u", base, capidx);
    CreateDirectoryA(dir, NULL);
    char path[512];

    /* Manifest: every op in submission order with full state. */
    snprintf(path, sizeof path, "%s/manifest.txt", dir);
    FILE* mf = fopen(path, "w");
    if (mf) {
        fprintf(mf, "frame_count=%llu draws=%u backbuffer=%ux%u\n",
                (unsigned long long)s_d3d.frame_count, ndraws, s_d3d.width, s_d3d.height);
        for (int i = 0; i < MAX_OFF_RTS; i++)
            if (s_d3d.off_rt[i].res && s_d3d.off_rt[i].used)
                fprintf(mf, "offrt[%d] off=0x%08X %ux%u dxgi=%u\n", i, s_d3d.off_rt[i].off,
                        s_d3d.off_rt[i].w, s_d3d.off_rt[i].h, s_d3d.off_rt[i].dxgi);
        fprintf(mf, "--- ops ---\n");
        for (u32 d = 0; d < ndraws && d < MAX_DRAWS; d++) {
            const D3D12DrawRecord* r = &s_d3d.draws[d];
            if (r->is_clear)
                fprintf(mf, "op%03u CLEAR rt=0x%08X mrt=0x%X/0x%X/0x%X cc=(%.3f,%.3f,%.3f,%.3f) flags=0x%02X z=%.3f zo=0x%X\n",   /* winport-fix: honour RSX clear flags */
                        d, r->rt_off, r->rt_mrt[0], r->rt_mrt[1], r->rt_mrt[2],
                        r->cc[0], r->cc[1], r->cc[2], r->cc[3], r->clear_flags, r->clear_z, r->zeta_off);
            else {
                fprintf(mf, "op%03u DRAW  rt=0x%08X mrt=0x%X/0x%X/0x%X vp=%u,%u,%ux%u fp=0x%08X uh=0x%08X m=%d vs=%d "   /* winport-fix: FP source dump */
                            "n=%u cmask=%X blend=%d bk=0x%X zc=0x%X zo=0x%X t0=0x%08X t1=0x%08X t2=0x%08X t3=0x%08X\n",   /* winport-fix: guest depth state */
                        d, r->rt_off, r->rt_mrt[0], r->rt_mrt[1], r->rt_mrt[2],
                        r->vp_x, r->vp_y, r->vp_w, r->vp_h,
                        r->fp_addr, r->fp_uhash, (int)(r->fp_exp32 & 3), r->vs_idx, r->vertex_count, r->cmask, r->blend, r->blend_key, r->cull, r->zeta_off,
                        r->tex[0].raw, r->tex[1].raw, r->tex[2].raw, r->tex[3].raw);
                if (r->is_vp && (r->wp_vzero || r->wp_vcmp))   /* winport v41 */
                    fprintf(mf, "      VTXCHK: %u/64 sampled positions zero/non-finite, %u/64 CMP placeholder normals (1,1,1), max index %u\n",
                            r->wp_vzero, r->wp_vcmp, r->wp_vmaxi);
                if (r->is_vp && (r->wp_stk0 & 1u))   /* winport v39 */
                    fprintf(mf, "      stencil ON stk0=0x%08X masks=0x%08X ref=%u%s\n", r->wp_stk0, r->wp_stk1, r->wp_sref,
                            wp_stencil_on() ? "" : "  (IGNORED: WINPORT_STENCIL=0)");
                if (r->is_vp && r->wp_merge_n)   /* winport v38 */
                    fprintf(mf, "      merged %u more batch(es) into this draw, %u of them after a vertex-constant upload%s\n",
                            r->wp_merge_n, r->wp_merge_cbchg, r->wp_merge_cbchg ? "  <-- drawn with STALE constants" : "");
                for (int u = 0; u < 4; u++)
                    if (r->tex[u].set)
                        fprintf(mf, "      t%d off=0x%08X fmt=0x%02X (%s%s) %ux%u remap=0x%04X\n", u,
                                r->tex[u].off, r->tex[u].fmt,
                                (r->tex[u].fmt & 0x20) ? "LN" : "SZ",
                                (r->tex[u].fmt & 0x40) ? ",NR" : "",
                                r->tex[u].w, r->tex[u].h, r->tex[u].ctrl1 & 0xFFFF);
            }
        }
        /* Per-draw VP constant banks for the first few offscreen-RT geometry
         * draws: c[0..3] is the projection row-major for most CG VPs; also scan
         * for the largest non-zero slot so a mis-placed MVP is obvious. Records
         * live at slot==draw-index; parity is post-^1 here (capture is after the
         * frame's vp_parity flip), so read the other half. */
        if (s_d3d.vp_cb_mapped) {
            u32 par = wp77_prev_par() /* winport v77-fif */;
            fprintf(mf, "--- vp constants (first offscreen geometry draws) ---\n");
            int shown = 0;
            for (u32 d = 0; d < ndraws && d < MAX_DRAWS && shown < 4; d++) {
                const D3D12DrawRecord* r = &s_d3d.draws[d];
                if (r->is_clear || !r->rt_off) continue;
                const float* c = (const float*)((const char*)s_d3d.vp_cb_mapped
                    + ((u64)par * MAX_DRAWS + d) * VP_CB_STRIDE);
                int last_nz = -1; float maxabs = 0.0f;
                for (int i = 0; i < RSX_MAX_VERTEX_CONSTANTS * 4; i++) {
                    float a = c[i] < 0 ? -c[i] : c[i];
                    if (a > 1e-9f) last_nz = i;
                    if (a > maxabs) maxabs = a;
                }
                fprintf(mf, "op%03u vs=%d lastNZslot=%d maxabs=%.3f\n",
                        d, r->vs_idx, last_nz / 4, maxabs);
                if (shown == 0) {   /* full non-zero slot dump for the 1st draw */
                    for (int s = 0; s <= last_nz / 4; s++) {
                        const float* v = c + s * 4;
                        if ((v[0]||v[1]||v[2]||v[3]))
                            fprintf(mf, "   c[%3d] = %9.4f %9.4f %9.4f %9.4f\n",
                                    s, v[0], v[1], v[2], v[3]);
                    }
                }
                shown++;
            }
        }

        /* winport-fix: capture detail */
        if (s_d3d.vp_cb_mapped && s_d3d.vp_vb_mapped) {
            u32 par2 = wp77_prev_par() /* winport v77-fif */;
            fprintf(mf, "--- postproc draw detail (<=6-vertex draws; first 2 per vs/fp/viewport) ---\n");
            u32 seenk[192][2]; u32 nseen = 0, printed = 0;
            for (u32 d = 0; d < ndraws && d < MAX_DRAWS && printed < 110; d++) {
                const D3D12DrawRecord* r = &s_d3d.draws[d];
                if (r->is_clear || r->is_blit || !r->is_vp || r->vertex_count == 0 || r->vertex_count > 6) continue;
                u32 key0 = ((u32)r->vs_idx << 20) ^ r->fp_uhash, key1 = (r->vp_w << 16) | (r->vp_h & 0xFFFFu);
                u32 cnt = 0; for (u32 i = 0; i < nseen; i++) if (seenk[i][0] == key0 && seenk[i][1] == key1) cnt++;
                if (cnt >= 2 || nseen >= 192) continue;
                seenk[nseen][0] = key0; seenk[nseen][1] = key1; nseen++; printed++;
                fprintf(mf, "op%03u vs=%d uh=0x%08X rt=0x%X vp=%u,%u,%ux%u zo=0x%X zc=0x%X n=%u blend=%d bk=0x%X cmask=%X\n",
                        d, r->vs_idx, r->fp_uhash, r->rt_off, r->vp_x, r->vp_y, r->vp_w, r->vp_h, r->zeta_off, r->cull,
                        r->vertex_count, r->blend, r->blend_key, r->cmask);
                const float* c = (const float*)((const char*)s_d3d.vp_cb_mapped
                    + ((u64)par2 * MAX_DRAWS + r->cb_slot) * VP_CB_STRIDE);
                int shownc = 0;
                for (int sl = 0; sl < RSX_MAX_VERTEX_CONSTANTS && shownc < 28; sl++) {
                    const float* v = c + sl * 4;
                    if (v[0] || v[1] || v[2] || v[3]) { fprintf(mf, "   c[%3d] = %.5f %.5f %.5f %.5f\n", sl, v[0], v[1], v[2], v[3]); shownc++; }
                }
                const float* vx = c + RSX_MAX_VERTEX_CONSTANTS * 4;
                fprintf(mf, "   posscale=(%g %g %g %g) posoffset=(%g %g %g %g)\n", vx[0], vx[1], vx[2], vx[3], vx[4], vx[5], vx[6], vx[7]);
                if (s_d3d.vp_fpcb_mapped) {
                    const float* ts = (const float*)((const char*)s_d3d.vp_fpcb_mapped
                        + ((u64)par2 * MAX_DRAWS + r->cb_slot) * VP_FPCB_STRIDE);
                    for (int k = 0; k < 6; k++)
                        fprintf(mf, "   fp_k[%d] = %.5f %.5f %.5f %.5f\n", k, ts[20+k*4], ts[21+k*4], ts[22+k*4], ts[23+k*4]);
                }
                const float* vt = (const float*)((const char*)s_d3d.vp_vb_mapped
                    + (u64)par2 * MAX_VERTICES * 256u + (u64)r->vb_byte_offset);
                for (u32 v = 0; v < r->vertex_count && v < 6; v++) {
                    fprintf(mf, "   v%u:", v);
                    for (int a = 0; a < 16; a++) {
                        const float* f = vt + (size_t)v * 64u + (size_t)a * 4u;
                        if (f[0] || f[1] || f[2] || f[3]) fprintf(mf, " a%d=(%.4f %.4f %.4f %.4f)", a, f[0], f[1], f[2], f[3]);
                    }
                    fprintf(mf, "\n");
                }
            }
        }
        fclose(mf);
    }

    /* winport v43: WINPORT_CAPDRAWS=1|2 (default off) -- raw per-draw VS inputs for an offline
     * CPU-skinning audit (work/winport/tools/g4_skin_audit.py). For each VP draw: the vertex
     * constant snapshot exactly as the shader sees it (vp_cb slot) and the de-indexed float4 x16
     * vertices (vp_vb). 1 = only draws with non-zero a7 (bone indices) in their first vertices,
     * 2 = every VP draw. File: <capdir>/draws.bin, record = u32 hdr[12] + cb + n*256 bytes. */
    {
        static int capd = -1;
        if (capd < 0) { const char* e = getenv("WINPORT_CAPDRAWS"); capd = (e && e[0] >= '1' && e[0] <= '9') ? (e[0] - '0') : 0; }
        if (capd && s_d3d.vp_cb_mapped && s_d3d.vp_vb_mapped) {
            snprintf(path, sizeof path, "%s/draws.bin", dir);
            FILE* bf = fopen(path, "wb");
            if (bf) {
                const u32 par3 = wp77_prev_par() /* winport v77-fif */;
                const u32 magic[4] = { 0x34475744u /* 'DWG4' */, 1u, (u32)VP_CB_STRIDE, 256u };
                fwrite(magic, 4, 4, bf);
                u32 nw = 0;
                for (u32 d = 0; d < ndraws && d < MAX_DRAWS; d++) {
                    const D3D12DrawRecord* r = &s_d3d.draws[d];
                    if (r->is_clear || r->is_blit || !r->is_vp || r->vertex_count == 0) continue;
                    if ((u64)r->vb_byte_offset + (u64)r->vertex_count * 256u > (u64)MAX_VERTICES * 256u) continue;
                    const float* vt = (const float*)((const char*)s_d3d.vp_vb_mapped
                        + (u64)par3 * MAX_VERTICES * 256u + (u64)r->vb_byte_offset);
                    if (capd == 1) {
                        int sk = 0;
                        for (u32 v = 0; v < r->vertex_count && v < 32 && !sk; v++) {
                            const float* a7 = vt + (size_t)v * 64u + 28u;
                            if (a7[0] != 0.0f || a7[1] != 0.0f) sk = 1;
                        }
                        if (!sk) continue;
                    }
                    const u32 hdr[12] = { d, (u32)r->vs_idx, r->vertex_count, r->cull, r->cmask, r->fp_uhash,
                                          r->rt_off, r->blend_key, r->cb_slot, r->topology, r->zeta_off, 0x5245434Fu };
                    fwrite(hdr, 4, 12, bf);
                    fwrite((const char*)s_d3d.vp_cb_mapped + ((u64)par3 * MAX_DRAWS + r->cb_slot) * VP_CB_STRIDE,
                           1, VP_CB_STRIDE, bf);
                    fwrite(vt, 256u, r->vertex_count, bf);
                    nw++;
                }
                fclose(bf);
                fprintf(stderr, "[CAPDRAWS] %s: %u draw(s) dumped (mode %d)\n", path, nw, capd);
            }
        }
    }

    {   /* winport v81a-captex: WINPORT_CAPTEX=1 -> raw guest bytes of every bound texture */
        static int ctx = -1;
        if (ctx < 0) { const char* e = getenv("WINPORT_CAPTEX"); ctx = (e && e[0] == '1') ? 1 : 0; }
        if (ctx) {
            extern uint8_t* vm_base;
            u32 seen[256]; u32 ns = 0, nw = 0;
            for (u32 d = 0; d < ndraws && d < MAX_DRAWS; d++) {
                const D3D12DrawRecord* r = &s_d3d.draws[d];
                if (r->is_clear) continue;
                for (int u = 0; u < 4; u++) {
                    if (!r->tex[u].set || !r->tex[u].off || r->tex[u].w == 0 || r->tex[u].h == 0) continue;
                    u32 k = 0; while (k < ns && seen[k] != r->tex[u].off) k++;
                    if (k < ns || ns >= 256) continue;
                    seen[ns++] = r->tex[u].off;
                    const u32 f = r->tex[u].fmt & ~0x60u;
                    u64 sz = (u64)r->tex[u].w * r->tex[u].h;
                    if (f == 0x85 || f == 0x8F) sz *= 4; else if (f == 0x86) sz /= 2; else if (f == 0x87 || f == 0x88 || f == 0x81) sz *= 1; else sz *= 2;
                    if (sz == 0 || sz > (16u << 20)) continue;
                    snprintf(path, sizeof path, "%s/texraw_%08X_%ux%u_f%02X.bin", dir, r->tex[u].off, r->tex[u].w, r->tex[u].h, r->tex[u].fmt);
                    FILE* tf = fopen(path, "wb");
                    if (tf) { fwrite(vm_base + r->tex[u].off, 1, (size_t)sz, tf); fclose(tf); nw++; }
                }
            }
            fprintf(stderr, "[CAPTEX81] %s: %u texture(s) dumped\n", dir, nw); fflush(stderr);
        }
        static int cmem = -1;   /* WINPORT_CAPMEM=1: guest main memory 0..256 MB (committed pages) -> mem.bin, first capture only */
        if (cmem < 0) { const char* e = getenv("WINPORT_CAPMEM"); cmem = (e && e[0] == '1') ? 1 : 0; }
        if (cmem == 1) {
            extern uint8_t* vm_base;
            cmem = 2;
            snprintf(path, sizeof path, "%s/mem.bin", dir);
            FILE* mfp = fopen(path, "wb");
            unsigned long long ea = 0, wr = 0;
            while (mfp && ea < 0x10000000ull) {
                MEMORY_BASIC_INFORMATION mbi;
                if (!VirtualQuery(vm_base + ea, &mbi, sizeof mbi)) break;
                unsigned long long len = (unsigned long long)((uint8_t*)mbi.BaseAddress + mbi.RegionSize - (vm_base + ea));
                if (ea + len > 0x10000000ull) len = 0x10000000ull - ea;
                if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
                    const unsigned hd[2] = { (unsigned)ea, (unsigned)len };
                    fwrite(hd, sizeof hd, 1, mfp); fwrite(vm_base + ea, 1, (size_t)len, mfp); wr += len;
                }
                ea += len;
            }
            if (mfp) fclose(mfp);
            fprintf(stderr, "[CAPMEM81] %s: %llu MB\n", path, wr >> 20); fflush(stderr);
        }
    }
    /* Backbuffer (in PRESENT state at capture time) + every used offscreen RT. */
    snprintf(path, sizeof path, "%s/backbuffer.bmp", dir);
    cap_readback_write(s_d3d.render_targets[fi], WP68_BW, WP68_BH,   /* winport v68 */
                       DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_PRESENT, fi, path);
    for (int i = 0; i < MAX_OFF_RTS; i++) {
        OffRT* r = &s_d3d.off_rt[i];
        if (!r->res || !r->used) continue;
        snprintf(path, sizeof path, "%s/rt_%08X.bmp", dir, r->off);
        cap_readback_write(r->res, wp68_px(r->w), wp68_px(r->h), r->dxgi, r->st, fi, path);   /* winport v68 */
    }
    wp_zs_dump_all(fi, dir);   /* winport-fix: v22b zeta dump */
    printf("[RSXCAP] frame %llu -> %s (%u ops)\n",
           (unsigned long long)s_d3d.frame_count, dir, ndraws);
}

/* Decompile the captured RSX vertex program to HLSL and build the VP PSO
 * (decompiled VS + atlas alpha-test PS). One-shot per program. */
static void compile_vp(void)
{
    extern int rsx_vp_decompile(const uint8_t*, u32, char*, u32);
    const rsx_state* st = s_d3d.current_rsx_state;
    if (!st || (st->transform_program_words * 4u) < 16 || !s_d3d.vp_root_sig) return;

    static char hlsl[64 * 1024];
    int ni = rsx_vp_decompile(((const u8*)st->transform_program), (st->transform_program_words * 4u), hlsl, sizeof hlsl);
    /* Does this VP read vp_c[0..3]? Gates the garbage-projection fallback:
     * programs keeping their MVP elsewhere (gcm/cube: c[256..259]) must not
     * have c[0..3] stomped nor the viewport z-lane overridden. */
    s_d3d.vp_uses_c03 = (strstr(hlsl, "vp_c[0]") || strstr(hlsl, "vp_c[1]") ||
                         strstr(hlsl, "vp_c[2]") || strstr(hlsl, "vp_c[3]")) ? 1 : 0;
    if (ni <= 0) { printf("[VP] decompile failed (%d)\n", ni); s_d3d.vp_compiled_bytes = (st->transform_program_words * 4u); return; }
    if (getenv("VP_DUMP")) {
        FILE* vf = fopen("vp_dump.hlsl", "w");
        if (vf) { fwrite(hlsl, 1, strlen(hlsl), vf); fclose(vf);
                  printf("[VP] dumped vp_dump.hlsl (%d instrs)\n", ni); }
        printf("[VPRAW] first 3 instrs (%u ucode bytes):\n", (st->transform_program_words * 4u));
        for (u32 _q = 0; _q < 48 && _q < (st->transform_program_words * 4u); _q += 16)
            printf("[VPRAW]  d0=%02X%02X%02X%02X d1=%02X%02X%02X%02X d2=%02X%02X%02X%02X d3=%02X%02X%02X%02X\n",
                ((const u8*)st->transform_program)[_q+0],((const u8*)st->transform_program)[_q+1],((const u8*)st->transform_program)[_q+2],((const u8*)st->transform_program)[_q+3],
                ((const u8*)st->transform_program)[_q+4],((const u8*)st->transform_program)[_q+5],((const u8*)st->transform_program)[_q+6],((const u8*)st->transform_program)[_q+7],
                ((const u8*)st->transform_program)[_q+8],((const u8*)st->transform_program)[_q+9],((const u8*)st->transform_program)[_q+10],((const u8*)st->transform_program)[_q+11],
                ((const u8*)st->transform_program)[_q+12],((const u8*)st->transform_program)[_q+13],((const u8*)st->transform_program)[_q+14],((const u8*)st->transform_program)[_q+15]);
    }

    /* Pixel shader mirrors dbgfont's FP: sample the atlas coverage at TEXCOORD0
     * (VP output o[7]), alpha-test at 0.5, output the vertex color (o[1]). */
    static const char ps[] =
        "Texture2D tex : register(t0); SamplerState smp : register(s0);\n"
        "struct PSIn{ float4 pos:SV_Position; float4 col0:COLOR0; float4 col1:COLOR1; float4 fog:FOG;\n"
        "  float4 t0:TEXCOORD0; float4 t1:TEXCOORD1; float4 t2:TEXCOORD2; float4 t3:TEXCOORD3;\n"
        "  float4 t4:TEXCOORD4; float4 t5:TEXCOORD5; float4 t6:TEXCOORD6; float4 t7:TEXCOORD7; };\n"
        "float4 main(PSIn i):SV_TARGET{ float cov = tex.Sample(smp, i.t0.xy).r;\n"
        "  if (cov <= 0.5) discard; return float4(i.col0.rgb, 1); }\n";

    ID3DBlob *vb=NULL,*pb=NULL,*e=NULL;
    HRESULT hr = D3DCompile(hlsl, strlen(hlsl), "vp", NULL, NULL, "main", "vs_5_0", 0, 0, &vb, &e);
    if (FAILED(hr)) { printf("[VP] VS compile FAIL: %s\n", e?(const char*)e->lpVtbl->GetBufferPointer(e):"?"); if(e)e->lpVtbl->Release(e); s_d3d.vp_compiled_bytes=(st->transform_program_words * 4u); return; }
    hr = D3DCompile(ps, sizeof(ps)-1, "vpps", NULL, NULL, "main", "ps_5_0", 0, 0, &pb, &e);
    if (FAILED(hr)) { printf("[VP] PS compile FAIL: %s\n", e?(const char*)e->lpVtbl->GetBufferPointer(e):"?"); if(e)e->lpVtbl->Release(e); if(vb)vb->lpVtbl->Release(vb); s_d3d.vp_compiled_bytes=(st->transform_program_words * 4u); return; }

    /* The decompiled VS declares inputs a0:ATTR0 .. a15:ATTR15 and the HLSL
     * compiler keeps whichever the program body reads. D3D12 requires EVERY VS
     * input to have a matching input-layout element (by semantic name+index),
     * so declare all 16 ATTR slots. (The old single "POSITION" element matched
     * NO VS input once the decompiler switched to ATTR semantics -> PSO failed
     * with E_INVALIDARG for every VP, blanking cellmark's text and vkcube.)
     * The VP path uploads only attrib0 to vp_vb (one float4/vertex), so every
     * slot reads that same float4 at offset 0; attributes other than position
     * therefore alias attrib0 for now (colours/uv wrong until the VP path
     * uploads multiple attributes), but geometry is correct and the PSO is
     * valid. */
    D3D12_INPUT_ELEMENT_DESC il[16];
    for (int _e = 0; _e < 16; _e++) {
        il[_e].SemanticName = "ATTR";
        il[_e].SemanticIndex = _e;
        il[_e].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        il[_e].InputSlot = 0;
        /* 256-byte generic VP vertex: every ATTRi is a float4 slot at i*16
         * (read_vp_vertex converts each enabled RSX attrib by type; disabled
         * slots hold (0,0,0,1)). Covers tiny3d (a0/a3/a8), SDK gcm samples
         * (a0/a1/a2), dbgfont -- no aliasing. */
        il[_e].AlignedByteOffset = (UINT)(_e * 16);
        il[_e].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        il[_e].InstanceDataStepRate = 0;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {0};
    pd.pRootSignature = s_d3d.vp_root_sig;
    pd.VS.pShaderBytecode = vb->lpVtbl->GetBufferPointer(vb); pd.VS.BytecodeLength = vb->lpVtbl->GetBufferSize(vb);
    pd.PS.pShaderBytecode = pb->lpVtbl->GetBufferPointer(pb); pd.PS.BytecodeLength = pb->lpVtbl->GetBufferSize(pb);
    pd.InputLayout.pInputElementDescs = il; pd.InputLayout.NumElements = 16;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1; pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    /* Depth test ON (LESS_EQUAL, matching the guest's rsxtiny_DepthTestFunc 515 /
     * CELL_GCM_LEQUAL). Without it the cube's faces drew in submission order and
     * back faces overwrote the front -> you saw through the near face to the
     * darker interior. The depth buffer is bound + cleared to 1.0 each frame. */
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pd.DepthStencilState.StencilEnable = FALSE;
    pd.SampleDesc.Count = 1;
    hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(s_d3d.device, &pd, &IID_ID3D12PipelineState, (void**)&s_d3d.pipeline_state_vp);

    /* Second PSO: same VS but a COLOUR-only PS (no texture sample, no alpha
     * discard) for untextured 3D geometry (vkcube's cube). The atlas PS above
     * samples a texture and discards where coverage<=0.5, which blanks any draw
     * with no atlas bound. */
    {
        static const char ps_col[] =
            "struct PSIn{ float4 pos:SV_Position; float4 col0:COLOR0; float4 col1:COLOR1; float4 fog:FOG;\n"
            "  float4 t0:TEXCOORD0; float4 t1:TEXCOORD1; float4 t2:TEXCOORD2; float4 t3:TEXCOORD3;\n"
            "  float4 t4:TEXCOORD4; float4 t5:TEXCOORD5; float4 t6:TEXCOORD6; float4 t7:TEXCOORD7; };\n"
            "float4 main(PSIn i):SV_TARGET{ return float4(i.col0.rgb, 1); }\n";
        ID3DBlob *pcb=NULL,*ec=NULL;
        if (SUCCEEDED(D3DCompile(ps_col, sizeof(ps_col)-1, "vppsc", NULL, NULL,
                                 "main", "ps_5_0", 0, 0, &pcb, &ec)) && pcb) {
            pd.PS.pShaderBytecode = pcb->lpVtbl->GetBufferPointer(pcb);
            pd.PS.BytecodeLength  = pcb->lpVtbl->GetBufferSize(pcb);
            HRESULT hr2 = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
                s_d3d.device, &pd, &IID_ID3D12PipelineState,
                (void**)&s_d3d.pipeline_state_vp_color);
            printf(SUCCEEDED(hr2) ? "[VP] colour pipeline ready\n"
                                  : "[VP] colour PSO FAIL (0x%08lX)\n", hr2);
            pcb->lpVtbl->Release(pcb);
        }
        if (ec) ec->lpVtbl->Release(ec);
    }
    /* Keep the VS bytecode for guest-FP PSO builds (vp_get_fp_pso); bump the
     * generation so cached FP PSOs built against the old VS are rebuilt. */
    if (s_d3d.vp_vs_blob) s_d3d.vp_vs_blob->lpVtbl->Release(s_d3d.vp_vs_blob);
    s_d3d.vp_vs_blob = vb;
    s_d3d.vp_gen++;
    pb->lpVtbl->Release(pb);
    s_d3d.vp_compiled_bytes = (st->transform_program_words * 4u);
    if (SUCCEEDED(hr)) { s_d3d.vp_ready = 1; printf("[VP] pipeline ready (%d instrs)\n", ni); }
    else {
        printf("[VP] PSO creation FAIL (0x%08lX)\n", hr);
        /* Drain the debug layer's message queue to get the EXACT validation
         * reason (E_INVALIDARG is otherwise opaque). One-shot. */
        ID3D12InfoQueue* iq = NULL;
        if (SUCCEEDED(s_d3d.device->lpVtbl->QueryInterface(
                s_d3d.device, &IID_ID3D12InfoQueue, (void**)&iq)) && iq) {
            UINT64 n = iq->lpVtbl->GetNumStoredMessages(iq);
            for (UINT64 mi = 0; mi < n; mi++) {
                SIZE_T len = 0;
                iq->lpVtbl->GetMessage(iq, mi, NULL, &len);
                D3D12_MESSAGE* m = (D3D12_MESSAGE*)malloc(len);
                if (m && SUCCEEDED(iq->lpVtbl->GetMessage(iq, mi, m, &len)))
                    printf("[VP][DBG] %s\n", m->pDescription);
                free(m);
            }
            iq->lpVtbl->Release(iq);
        }
    }
}

/* Build (or fetch) a PSO pairing the current decompiled VS with the guest's
 * FRAGMENT program at fp_addr: read the FP ucode from guest memory, decompile
 * to HLSL (rsx_fp_decompiler), compile, and cache. This replaces the two
 * hardcoded pixel shaders for draws whose FP we can translate -- e.g.
 * gcm/cube's plasma FP `c = tex2D(t0, uv).x; out = (c, 0, c, 1)`. */
#include "rsx_fp_decompiler.h"
/* winport-fix: shader caches -- FP snapshots, content-keyed FP pipeline index, counters. */
static unsigned s_wp_fp_compiles = 0, s_wp_vs_compiles = 0, s_wp_fp_lookups = 0;
static unsigned s_wp_rt_recreates = 0, s_wp_rt_evicts = 0;
static int wp_fp_buffered(void);
static int wp_zdepth_on(void)   /* winport-fix: v22 scene depth */
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_ZDEPTH"); v = (e && *e == '0') ? 0 : 1; } return v && wp_fp_buffered(); }
#define WP_FP_CTRL_MODE(st) (wp_fp_ctrl_on() && (st)->shader_control_valid ? \
                                 (((((st)->shader_control & 0x40u) != 0) ? 2 : 1) | (wp_zdepth_on() && (((st)->shader_control & 0xEu) != 0) ? 4 : 0)) : 0)   /* winport-fix: real SHADER_CONTROL for FP exports */ /* winport-fix: v22 scene depth */
static int wp_fp_ctrl_on(void)
{ static int c = -1; if (c < 0) { const char* e = getenv("WINPORT_FP_CTRL"); c = (e && *e == '0') ? 0 : 1; } return c; }
static u32 s_wp_alpha = 0;   /* winport-fix: buffered FP constants */ /* 0x200|func when the draw alpha-tests */
#define WP_ALPHA_KEY(dr) ((((dr)->alpha_ctl >> 16) & 1u) ? (0x200u | (((dr)->alpha_ctl >> 8) & 7u)) : 0u)
/* winport v39 (G1 black blotches): guest STENCIL test. The upstream D3D12 renderer never enabled stencil
 * (StencilEnable = FALSE everywhere), but GoW2 draws stencil shadow/darkening volumes (colour-masked draws,
 * then a full-screen REV_SUBTRACT darkening quad that must only touch stencil-marked pixels): without the
 * test the darkening hits whole regions of the screen -> black boxes/blotches. The Metal backend implements
 * it (rsx_core_pipe.c rsx_core_pipe_ds_desc); this mirrors it. WINPORT_STENCIL=1 enables; 0 = old behaviour. */
static int wp_stencil_on(void)
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_STENCIL"); v = e ? (atoi(e) != 0) : 0; } return v; }
static u32 wp_sop(u32 op)   /* CELL_GCM stencil op -> D3D12_STENCIL_OP (4 bits) */
{
    switch (op) {
    case 0x0000: return D3D12_STENCIL_OP_ZERO;
    case 0x1E00: return D3D12_STENCIL_OP_KEEP;
    case 0x1E01: return D3D12_STENCIL_OP_REPLACE;
    case 0x1E02: return D3D12_STENCIL_OP_INCR_SAT;
    case 0x1E03: return D3D12_STENCIL_OP_DECR_SAT;
    case 0x150A: return D3D12_STENCIL_OP_INVERT;
    case 0x8507: return D3D12_STENCIL_OP_INCR;
    case 0x8508: return D3D12_STENCIL_OP_DECR;
    default:     return D3D12_STENCIL_OP_KEEP;
    }
}
/* stk0: b0 enable | front func&7 <<1 | fail<<4 | zfail<<8 | zpass<<12 | back func&7 <<16 | fail<<19 | zfail<<23 | zpass<<27
 * stk1: front read mask | front write mask<<8 | back read mask<<16 | back write mask<<24; ref = front ref. */
static void wp_stencil_pack(const rsx_state* st, u32* k0, u32* k1, u32* ref)
{
    *k0 = *k1 = *ref = 0;
    if (!st || !st->stencil_test_enable) return;
    int two = st->two_sided_stencil_enable;
    u32 bf = two ? st->back_stencil_func : st->stencil_func;
    u32 bfa = two ? st->back_stencil_op_fail : st->stencil_op_fail;
    u32 bzf = two ? st->back_stencil_op_zfail : st->stencil_op_zfail;
    u32 bzp = two ? st->back_stencil_op_zpass : st->stencil_op_zpass;
    u32 brm = two ? st->back_stencil_mask : st->stencil_mask;
    u32 bwm = two ? st->back_stencil_write_mask : st->stencil_write_mask;
    *k0 = 1u | ((st->stencil_func & 7u) << 1) | (wp_sop(st->stencil_op_fail) << 4) | (wp_sop(st->stencil_op_zfail) << 8)
        | (wp_sop(st->stencil_op_zpass) << 12) | ((bf & 7u) << 16) | (wp_sop(bfa) << 19) | (wp_sop(bzf) << 23) | (wp_sop(bzp) << 27);
    *k1 = (st->stencil_mask & 0xFFu) | ((st->stencil_write_mask & 0xFFu) << 8) | ((brm & 0xFFu) << 16) | ((bwm & 0xFFu) << 24);
    *ref = st->stencil_ref & 0xFFu;
}
static u32 s_wp_stk0 = 0, s_wp_stk1 = 0;   /* handed to vp_get_fp_pso like s_wp_alpha */
/* winport v42: WINPORT_LINE_FIX -- topology class handed to vp_get_fp_pso like s_wp_stk0 */
static u32 s_wp_topo = 0;
static int wp_line_fix(void) { static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_LINE_FIX"); v = e ? atoi(e) : 0; } return v; }
#define WP_TOPO_CLASS(dr) (((dr)->topology == RSX_TOPOLOGY_LINES || (dr)->topology == RSX_TOPOLOGY_LINE_STRIP) ? 1u : ((dr)->topology == RSX_TOPOLOGY_POINTS) ? 2u : 0u)
static u32 s_wp_vchk_zero = 0, s_wp_vchk_cmp = 0, s_wp_vchk_maxvi = 0;   /* winport v41: WINPORT_VTXCHK, last vpf_prepare */
/* winport v41 (white shadows): the darkening quad tests GREATER ref 128 (passes where stencil < 128), the volumes
 * DECR/INCR-wrap on z-fail. With the stencil cleared to 0 the lit floor (0) passes and the shadow (0-1 = 255) fails:
 * the inverse of the intent. WINPORT_STENCIL_BASE=128 clears stencil to 128 (lit 128 fails, shadow 127 passes);
 * WINPORT_STENCIL_REVCMP=1 alternatively swaps the compare operands (LESS<->GREATER, LEQUAL<->GEQUAL). Defaults 0. */
static int wp_stencil_base(void)
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_STENCIL_BASE"); v = e ? (atoi(e) & 0xFF) : 0; } return v; }
static int wp_stencil_revcmp(void)
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_STENCIL_REVCMP"); v = e ? (atoi(e) != 0) : 0; } return v; }
static u32 wp_scmp(u32 f)   /* GL func & 7 -> D3D12 comparison, optionally operand-swapped */
{
    static const u8 rev[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    f &= 7u; if (wp_stencil_revcmp()) f = rev[f];
    return f + 1u;
}
#define WP_STK_SET(dr) (s_wp_stk0 = wp_stencil_on() ? (dr)->wp_stk0 : 0u, s_wp_stk1 = wp_stencil_on() ? (dr)->wp_stk1 : 0u)
static int wp_fp_buffered(void)
{ static int b = -1; if (b < 0) { const char* e = getenv("WINPORT_FP_BUFFERED"); b = (e && *e == '0') ? 0 : 1; } return b; }
static u32 s_wp_uh = 0;          /* record-time code hash of the draw being resolved (0 = compute) */
typedef struct { u32 h; u8* bytes; } WpFpSnap;
#define WP_FPSNAP_N 8192
static WpFpSnap s_wp_fpsnap[WP_FPSNAP_N];
static int s_wp_fpsnap_n = 0;
static void winport_fp_snapshot_wk85(D3D12DrawRecord* dr);
static void winport_fp_snapshot(D3D12DrawRecord* dr) { const unsigned long long t_ = WK85_T0(); winport_fp_snapshot_wk85(dr); WK85_ADD(WK85_fpsnap, t_); }   /* winport v85a-walkprof */
static void winport_fp_snapshot_wk85(D3D12DrawRecord* dr)
{
    extern uint8_t* vm_base;
    extern u32 cellGcmResolveLocated(int local, u32 offset);
    dr->fp_uhash = 0;
    if (!dr->fp_addr || !vm_base) return;
    { static int off = -1;
      if (off < 0) { const char* e = getenv("WINPORT_FP_SNAPSHOT"); off = (e && *e == '0') ? 1 : 0; }
      if (off) return; }
    u32 o = cellGcmResolveLocated((dr->fp_addr & 0x3u) == 1, dr->fp_addr & ~0x3u);
    if (o == 0xFFFFFFFFu) return;
    const u8* src = vm_base + o;
    u32 h = rsx_fp_code_hash(src, 4096);
    if (!wp_fp_buffered()) {   /* winport-fix: buffered FP constants */ /* literals: constants are part of the code */
        u32 n_ = rsx_fp_program_size(src, 4096);
        for (u32 i_ = 0; i_ < n_; i_++) { h ^= src[i_]; h *= 16777619u; }
    }
    if (!h) h = 1;
    dr->fp_uhash = h;
    u32 i = (h * 2654435761u) & (WP_FPSNAP_N - 1);
    for (u32 k = 0; k < WP_FPSNAP_N; k++, i = (i + 1) & (WP_FPSNAP_N - 1)) {
        if (s_wp_fpsnap[i].h == h) return;
        if (s_wp_fpsnap[i].h) continue;
        if (s_wp_fpsnap_n >= WP_FPSNAP_N * 3 / 4) return;   /* full: compile from guest memory */
        u32 n = rsx_fp_program_size(src, 4096);
        if (!n || n > 4096) n = 4096;
        u8* b = (u8*)calloc(1, 4096 + 64);
        if (!b) return;
        memcpy(b, src, n);
        s_wp_fpsnap[i].bytes = b; s_wp_fpsnap[i].h = h; s_wp_fpsnap_n++;
        return;
    }
}
static const u8* wp_fp_src(u32 h, const u8* fallback)
{
    if (!h) return fallback;
    u32 i = (h * 2654435761u) & (WP_FPSNAP_N - 1);
    for (u32 k = 0; k < WP_FPSNAP_N; k++, i = (i + 1) & (WP_FPSNAP_N - 1)) {
        if (s_wp_fpsnap[i].h == h) return s_wp_fpsnap[i].bytes;
        if (!s_wp_fpsnap[i].h) break;
    }
    return fallback;
}
#define WP_FPIDX_N 16384
static int s_wp_fpidx[WP_FPIDX_N];      /* vp_fp[] index + 1; 0 = empty */
static int s_wp_fp_overflow = 0;
static ID3D12PipelineState* s_wp_fp_defer[256];
static int s_wp_fp_defer_n = 0;
static u32 wp_fpkey(u32 uh, u32 vsh, u32 blend, int nrt, u32 rtfmt, int exp32, u32 cmask,
                    u32 cull, u32 cube)
{
    u32 v[9] = { uh, vsh, blend, (u32)nrt, rtfmt, (u32)exp32, cmask, cull, cube };
    u32 h = 2166136261u;
    for (int i = 0; i < 9; i++) { h ^= v[i]; h *= 16777619u; h ^= h >> 15; }
    return h;
}
/* Frame start (GPU idle): release pipelines handed out uncached, flush a full cache. */
static void wp_fp_frame_start(void)
{
    for (int i = 0; i < s_wp_fp_defer_n; i++)
        if (s_wp_fp_defer[i]) s_wp_fp_defer[i]->lpVtbl->Release(s_wp_fp_defer[i]);
    s_wp_fp_defer_n = 0;
    if (s_wp_fp_overflow) {
        for (int i = 0; i < s_d3d.vp_fp_n; i++)
            if (s_d3d.vp_fp[i].pso) s_d3d.vp_fp[i].pso->lpVtbl->Release(s_d3d.vp_fp[i].pso);
        s_d3d.vp_fp_n = 0;
        memset(s_wp_fpidx, 0, sizeof s_wp_fpidx);
        s_wp_fp_overflow = 0;
        printf("[WINPORT] FP pipeline cache full -> flushed\n");
    }
}

/* Hash + compile the CURRENT rsx_state vertex program into the VS cache;
 * returns the cache slot (or -1). Called at draw-record time so each draw
 * carries the VP that was loaded when it was submitted. */
static u32 vp_hash_ucode(const u8* p, u32 n)
{
    u32 h = 2166136261u;
    for (u32 i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h ? h : 1u;
}
/* ---- winport v85c-walkfast (fix_v85c_walkfast_renderer.py): switches + VP hash memo ---- */
static int wp85_sw(const char* name)
{
    const char* e = getenv(name);
    if (e && *e) return e[0] == '1';
    const char* m = getenv("WINPORT_WALKFAST85");
    return m && m[0] == '1';
}
static int wp85_cb_on(void)  { static int v = -1; if (v < 0) { v = wp85_sw("WINPORT_CB85");  if (v) fprintf(stderr, "[FAST85] CB85 shadow constant blocks ON\n"); } return v; }
static int wp85_vs_on(void)  { static int v = -1; if (v < 0) { v = wp85_sw("WINPORT_VS85");  if (v) fprintf(stderr, "[FAST85] VS85 vertex-program hash memo ON\n"); } return v; }
static int wp85_idx_on(void) { static int v = -1; if (v < 0) { v = wp85_sw("WINPORT_IDX85"); if (v) fprintf(stderr, "[FAST85] IDX85 index max scan ON\n"); } return v; }
static int wp85_tex_on(void) { static int v = -1; if (v < 0) { v = wp85_sw("WINPORT_TEX85"); if (v) fprintf(stderr, "[FAST85] TEX85 texture slot memo ON\n"); } return v; }
/* WINPORT_FAST85_VERIFY=1: every memo answer is recomputed the old way and compared ([FAST85V] lines) */
static int wp85_verify(void) { static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_FAST85_VERIFY"); v = (e && e[0] == '1'); if (v) fprintf(stderr, "[FAST85V] verify mode ON\n"); } return v; }
static unsigned long long s85_v_n[3], s85_v_bad[3];   /* 0 vp hash, 1 index max, 2 tex slot */
static void wp85_vcount(int k, int bad)
{
    s85_v_n[k]++; if (bad) s85_v_bad[k]++;
    if (bad && (s85_v_bad[k] & (s85_v_bad[k] - 1)) == 0) fprintf(stderr, "[FAST85V] MISMATCH kind=%d (#%llu)\n", k, s85_v_bad[k]);
    if ((s85_v_n[k] & 0x3FFFFull) == 0) fprintf(stderr, "[FAST85V] kind=%d checked=%llu mismatches=%llu\n", k, s85_v_n[k], s85_v_bad[k]);
}
/* memo: program bytes -> FNV hash (exactly vp_hash_ucode of the same bytes) [+ VS cache index for the vp_get_vs table] */
#define WP85_MEMO_N 16
typedef struct { u32 len, hash; int idx; u8* bytes; } Wp85Memo;
typedef struct { Wp85Memo e[WP85_MEMO_N]; int next; Wp85Memo* last; unsigned long long hit, miss; } Wp85MemoTab;
static Wp85MemoTab s85_vsm, s85_stm;
static u32 wp85_memo_hash(Wp85MemoTab* t, const u8* p, u32 n)
{
    for (int i = 0; i < WP85_MEMO_N; i++) {
        Wp85Memo* e = &t->e[i];
        if (e->bytes && e->len == n && memcmp(e->bytes, p, n) == 0) { t->last = e; t->hit++;
            if (wp85_verify()) wp85_vcount(0, vp_hash_ucode(p, n) != e->hash);
            return e->hash; }
    }
    const u32 h = vp_hash_ucode(p, n);
    t->miss++; t->last = NULL;
    if (n <= 8192u) {
        Wp85Memo* e = &t->e[t->next]; t->next = (t->next + 1) % WP85_MEMO_N;
        if (!e->bytes) e->bytes = (u8*)malloc(8192);
        if (e->bytes) { memcpy(e->bytes, p, n); e->len = n; e->hash = h; e->idx = -1; t->last = e; }
    }
    return h;
}
/* index max over [first, first+count) of a big-endian 16/32-bit index array (same value as the vpi_get loop) */
static u32 wp85_maxidx(const u8* base, int is16, u32 first, u32 count)
{
    u32 m = 0;
    if (is16) { const u16* p = (const u16*)(base + (u64)first * 2);
                for (u32 k = 0; k < count; k++) { u16 v; memcpy(&v, p + k, 2); v = __builtin_bswap16(v); m = v > m ? v : m; } }
    else      { const u32* p = (const u32*)(base + (u64)first * 4);
                for (u32 k = 0; k < count; k++) { u32 v; memcpy(&v, p + k, 4); v = __builtin_bswap32(v); m = v > m ? v : m; } }
    return m;
}
/* ---- end winport v85c-walkfast helpers ---- */
/* ---- winport v85e-idxdraw (fix_v85e_idxdraw_renderer.py): WINPORT_IDXDRAW85=1 ---- */
#define WP85_IB_CAP (2u * 1024u * 1024u)   /* 16-bit indices per frame slot */
static ID3D12Resource* s85_ib = NULL;
static u16* s85_ib_mapped = NULL;
static u32 s85_ib_off = 0, s85_pending_ib = 0;
static int s85_ib_state = 0;   /* 0 not tried, 1 ok, -1 failed */
static unsigned long long s85_n_idx, s85_n_exp, s85_n_merge, s85_v_conv, s85_v_idx;
static int wp85_idxd_on(void)
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_IDXDRAW85"); v = (e && e[0] == '1') ? 1 : 0;
    if (v) fprintf(stderr, "[IDX85] indexed triangle lists drawn indexed (each vertex converted once)\n"); } return v; }
static void wp85_ib_reset(void)   /* wherever draw_count / vp_vb_offset go back to 0 */
{
    for (u32 i = 0; i < s_d3d.draw_count && i < MAX_DRAWS; i++) s_d3d.draws[i].wp85_ib = 0;
    s85_ib_off = 0; s85_pending_ib = 0;
}
static int wp85_ib_ensure(void)
{
    if (s85_ib_state) return s85_ib_state > 0;
    s85_ib_state = -1;
    if (!s_d3d.device) return 0;
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd = {0};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bd.Width = (u64)WP85_IB_CAP * 2u * 3u;
    D3D12_RANGE nr = {0, 0};
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&s85_ib)) || !s85_ib) {
        fprintf(stderr, "[IDX85] index buffer allocation FAILED -> expansion as before\n"); s85_ib = NULL; return 0; }
    void* m = NULL;
    if (FAILED(s85_ib->lpVtbl->Map(s85_ib, 0, &nr, &m)) || !m) { fprintf(stderr, "[IDX85] index buffer map FAILED\n"); return 0; }
    s85_ib_mapped = (u16*)m; s85_ib_state = 1;
    fprintf(stderr, "[IDX85] index buffer: 3 x %u indices (%llu MB)\n", WP85_IB_CAP, (unsigned long long)(bd.Width >> 20));
    return 1;
}
static void wp85_idx_report(void)
{
    static ULONGLONG t0; const ULONGLONG t = GetTickCount64();
    if (!t0) { t0 = t; return; }
    if (t - t0 < 1000) return;
    const double s = (t - t0) / 1000.0; t0 = t;
    if (!wp_stats_on()) ; else fprintf(stderr, "[IDX85] indexed=%.0f/s expanded=%.0f/s (merge-kept %.0f/s) | converted %.0fk verts/s for %.0fk indices/s\n",
            s85_n_idx / s, s85_n_exp / s, s85_n_merge / s, s85_v_conv / s / 1000.0, s85_v_idx / s / 1000.0);
    s85_n_idx = s85_n_exp = s85_n_merge = s85_v_conv = s85_v_idx = 0;
}
/* ---- end winport v85e-idxdraw globals ---- */

static int vp_get_vs_wk85(const rsx_state* st);
static int vp_get_vs(const rsx_state* st) { const unsigned long long t_ = WK85_T0(); int r_ = vp_get_vs_wk85(st); WK85_ADD(WK85_vs, t_); return r_; }   /* winport v85a-walkprof */
static int vp_get_vs_wk85(const rsx_state* st)
{
    extern int rsx_vp_decompile(const uint8_t*, u32, char*, u32);
    if (!st || (st->transform_program_words * 4u) < 16) return -1;
    /* Start at the instruction SET_TRANSFORM_PROGRAM_START selects, not at 0.
     * The microcode store holds every resident program -- this title keeps its
     * scene, fluid, caustics and droplet programs in it at once -- so
     * decompiling from 0 ran the scene's transform for every draw. That is why
     * the fluid's 114300 vertices rasterized nothing: they were being pushed
     * through the wrong program.
     * VP_START_OFF=1 restores the old always-from-zero behaviour. */
    u32 vstart = 0;
    { static int off = -1; if (off < 0) off = getenv("VP_START_OFF") ? 1 : 0;
      if (!off) vstart = st->transform_program_start * 16u; }
    if (vstart >= (st->transform_program_words * 4u)) vstart = 0;
    const u8* vuc = ((const u8*)st->transform_program) + vstart;
    u32 vlen = (st->transform_program_words * 4u) - vstart;
    { extern u32 rsx_vp_program_size_instrs(const u8* ucode, u32 max_bytes);   /* winport-fix: shader caches */
      static int whole = -1; if (whole < 0) whole = getenv("WINPORT_VP_HASH_WHOLE") ? 1 : 0;
      u32 ni_ = whole ? 0 : rsx_vp_program_size_instrs(vuc, vlen);
      if (ni_ && ni_ * 16u <= vlen) vlen = ni_ * 16u; }
    u32 hash;
    Wp85Memo* m85_ = NULL;   /* winport v85c-walkfast */
    if (wp85_vs_on()) {
        hash = wp85_memo_hash(&s85_vsm, vuc, vlen); m85_ = s85_vsm.last;
        if (m85_ && m85_->idx >= 0 && m85_->idx < s_d3d.vp_vs_n && s_d3d.vp_vs[m85_->idx].hash == hash) return m85_->idx;
    } else hash = vp_hash_ucode(vuc, vlen);
    for (int i = 0; i < s_d3d.vp_vs_n; i++)
        if (s_d3d.vp_vs[i].hash == hash) { if (m85_) m85_->idx = i; return i; }

    static char hlsl[262144];
    int ni = rsx_vp_decompile(vuc, vlen, hlsl, sizeof hlsl);
    if (ni <= 0) return -1;
    if (getenv("VP_DUMP")) { static int _d=0; if (_d++ < 4) {
        FILE* f = fopen("vp2_dump.hlsl", _d==1 ? "w" : "a");
        if (f) { fprintf(f, "/* per-draw VS hash pending, %d instrs */%s%s", ni, hlsl, "\n"); fclose(f); } } }
    /* RSX_DBG_VP=<hex tex0 offset>: write the vertex program belonging to the draws
     * that bind that texture. vp2_dump.hlsl holds whichever four programs were
     * compiled first, which need not include the one under investigation -- and
     * reading the wrong program sends you chasing the wrong constants. */
    { static const char* dp = (const char*)1; static u32 wantp = 0; static int done = 0;
      if (dp == (const char*)1) { dp = getenv("RSX_DBG_VP");
                                  wantp = dp ? (u32)strtoul(dp, NULL, 16) : 0; }
      if (wantp && !done && s_d3d.cur_texs[0].raw == wantp) {
          done = 1;
          FILE* f = fopen("duck_vp.hlsl", "w");
          if (f) { fprintf(f, "/* duck VP, %d instrs, tex0=0x%X */%s", ni, wantp, hlsl);
                   fclose(f); }
          fprintf(stderr, "[DUCKVP] wrote duck_vp.hlsl (%d instrs)%c", ni, 10);
      } }
    /* VP_BYPASS=1: replace the guest transform with a direct map of attribute 0
     * into clip space. If geometry appears with this on, everything from the
     * input layout through raster/depth/present is sound and the fault is the
     * transform (constants or the decompiled program); if it stays blank, the
     * fault is upstream of the shader -- the attribute binding itself. Patched
     * in place, space-padded to the original length so offsets stay valid. */
    /* VP_W_ONE=1: keep the guest transform but force the clip-space w to 1.
     * Different from VP_BYPASS, which replaces the whole position: this tests
     * only the hypothesis that w is wrong. This title's VP ends
     *   { float4 _v = (float4)((vp_c[467]).xxxx); o[0].w = _v.w; }
     * with c467 = (-1, 1, 0, 0), so w = -1 and the clipper discards every
     * vertex. Patched in place, space-padded so offsets stay valid. */
    if (getenv("VP_W_ONE")) {
        static const char anchor[] = "o[0].w = _v.w;";
        static const char repl[]   = "o[0].w = 1.0;";
        char* at = hlsl;
        while ((at = strstr(at, anchor)) != NULL) {
            size_t n = sizeof(anchor) - 1, m = sizeof(repl) - 1;
            memcpy(at, repl, m);
            memset(at + m, ' ', n - m);
            at += n;
        }
    }
    if (getenv("VP_BYPASS")) {
        static const char anchor[] =
            "Out.pos = float4(_p.xyz * vp_posscale.xyz + _p.w * vp_posoffset.xyz, _p.w);";
        static const char repl[] = "Out.pos = float4(v[0].xy * 0.5, 0.5, 1.0);";
        char* at = hlsl;
        while ((at = strstr(at, anchor)) != NULL) {
            size_t n = sizeof(anchor) - 1, m = sizeof(repl) - 1;
            memcpy(at, repl, m);
            memset(at + m, ' ', n - m);
            at += n;
        }
    }
    ID3DBlob* vb = NULL; ID3DBlob* e = NULL;
    HRESULT hr = D3DCompile(hlsl, strlen(hlsl), "guest_vp2", NULL, NULL,
                            "main", "vs_5_0", 0, 0, &vb, &e);
    if (e) e->lpVtbl->Release(e);
    if (FAILED(hr) || !vb) {
        static int _e=0; if (_e++<4) printf("[VP] per-draw VS compile FAIL (hash=0x%08X)\n", hash);
        return -1;
    }
    int slot;
    s_wp_vs_compiles++;
    if (s_d3d.vp_vs_n < VP_VS_CACHE) slot = s_d3d.vp_vs_n++;
    else {  /* winport-fix: shader caches */ /* replace in place: never shift slots under recorded vs_idx */
        static int victim = 0;
        slot = victim; victim = (victim + 1) % VP_VS_CACHE;
        if (s_d3d.vp_vs[slot].vs) s_d3d.vp_vs[slot].vs->lpVtbl->Release(s_d3d.vp_vs[slot].vs);
    }
    s_d3d.vp_vs[slot].hash = hash;
    s_d3d.vp_vs[slot].vs   = vb;
    { static const char* vd_ = (const char*)1;   /* winport-fix: capture detail */
      if (vd_ == (const char*)1) { vd_ = getenv("WINPORT_VPDUMP"); if (vd_ && !*vd_) vd_ = NULL; }
      if (vd_) { char pth_[600]; snprintf(pth_, sizeof pth_, "%s/vs_%03d_%08X.hlsl", vd_, slot, hash);
                 FILE* f_ = fopen(pth_, "w"); if (f_) { fputs(hlsl, f_); fclose(f_); } } }
    s_d3d.vp_vs[slot].uses_c03 =
        (strstr(hlsl, "vp_c[0]") || strstr(hlsl, "vp_c[1]") ||
         strstr(hlsl, "vp_c[2]") || strstr(hlsl, "vp_c[3]")) ? 1 : 0;
    { static int _n=0; if (_n++<6) printf("[VP] per-draw VS cached (hash=0x%08X, %d instrs, slot %d)\n", hash, ni, slot); }
    /* Build the base VP pipeline here if it does not exist yet. render_frame's
     * trigger reads s_d3d.current_rsx_state, which by frame end no longer has
     * the microcode -- so for a title that only ever compiles per-draw VS the
     * base pipeline was never built, s_d3d.vp_ready stayed 0, and the VP draw
     * pass it gates dropped every recorded is_vp draw. Here we are at RECORD
     * time with a live state that definitely has microcode. */
    if (!s_d3d.vp_ready && st && (st->transform_program_words * 4u) >= 16) {
        const rsx_state* prev = s_d3d.current_rsx_state;
        s_d3d.current_rsx_state = st;
        compile_vp();
        s_d3d.current_rsx_state = prev;
    }
    return slot;
}

/* --- Guest blend state -> packed PSO key ----------------------------------
 * RSX blend factors/equation carry GL enums; each SFACTOR/DFACTOR/EQUATION
 * word packs (alpha<<16)|colour. The key packs D3D12 enums so the PSO cache
 * distinguishes real blend modes: bit0 enable, [5:1] srcC, [10:6] dstC,
 * [15:11] srcA, [20:16] dstA, [23:21] opC, [26:24] opA. DeferredShading's
 * light accumulation is additive (ONE,ONE) -- the old hardcoded straight-
 * alpha PSO turned each light pass into a screen-blanking flash. */
static u8 gl_blend_factor_d3d(u32 f, int is_alpha)
{
    switch (f & 0xFFFF) {
    case 0x0000: return D3D12_BLEND_ZERO;
    case 0x0001: return D3D12_BLEND_ONE;
    case 0x0300: return is_alpha ? D3D12_BLEND_SRC_ALPHA     : D3D12_BLEND_SRC_COLOR;
    case 0x0301: return is_alpha ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_INV_SRC_COLOR;
    case 0x0302: return D3D12_BLEND_SRC_ALPHA;
    case 0x0303: return D3D12_BLEND_INV_SRC_ALPHA;
    case 0x0304: return D3D12_BLEND_DEST_ALPHA;
    case 0x0305: return D3D12_BLEND_INV_DEST_ALPHA;
    case 0x0306: return is_alpha ? D3D12_BLEND_DEST_ALPHA     : D3D12_BLEND_DEST_COLOR;
    case 0x0307: return is_alpha ? D3D12_BLEND_INV_DEST_ALPHA : D3D12_BLEND_INV_DEST_COLOR;
    case 0x0308: return D3D12_BLEND_SRC_ALPHA_SAT;
    case 0x8001: return D3D12_BLEND_BLEND_FACTOR;       /* CONSTANT_COLOR   */
    case 0x8002: return D3D12_BLEND_INV_BLEND_FACTOR;
    case 0x8003: return D3D12_BLEND_BLEND_FACTOR;       /* CONSTANT_ALPHA ~ */
    case 0x8004: return D3D12_BLEND_INV_BLEND_FACTOR;
    default:     return D3D12_BLEND_ONE;
    }
}
static u8 gl_blend_op_d3d(u32 e)
{
    switch (e & 0xFFFF) {
    case 0x8007: return D3D12_BLEND_OP_MIN;
    case 0x8008: return D3D12_BLEND_OP_MAX;
    case 0x800A: return D3D12_BLEND_OP_SUBTRACT;
    case 0x800B: return D3D12_BLEND_OP_REV_SUBTRACT;
    default:     return D3D12_BLEND_OP_ADD;   /* 0x8006 FUNC_ADD / unset */
    }
}
/* Pack the guest's face-culling state for the PSO key.
 * bit0 = cull enabled, bit1 = cull FRONT (else BACK), bit2 = front face is CCW.
 * RSX/GL enums: CULL_FACE FRONT=0x0404 BACK=0x0405 FRONT_AND_BACK=0x0408;
 * FRONT_FACE CW=0x0900 CCW=0x0901. rsx_commands seeds cull_face/front_face with
 * plain 1/0 before any register arrives, so treat those defaults as BACK/CW. */
static int wp_zstate_on(void)   /* winport-fix: guest depth state */
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_ZSTATE"); v = e ? (atoi(e) != 0) : 1; } return v; }
static u32 rsx_cull_key(const rsx_state* st)
{
    u32 k = 0;
    if (st && st->cull_face_enable) {
        k = 1u;
        if (st->cull_face == 0x0404u || st->cull_face == 0x0408u) k |= 2u;  /* FRONT */
        if (st->front_face == 0x0901u) k |= 4u;                             /* CCW */
    }
    if (st && wp_zstate_on()) {   /* bit3 depth test, bit4 depth write, bits5-7 func (GL 0x200.. & 7), bit8 valid */
        k |= 0x100u;
        if (st->depth_test_enable) k |= 8u;
        if (st->depth_mask) k |= 16u;
        k |= ((u32)st->depth_func & 7u) << 5;
    }
    return k;
}

static u32 rsx_blend_key(const rsx_state* st, int enable)
{
    { static int _bd = -1; if (_bd < 0) _bd = getenv("BLENDDBG") ? 1 : 0;
      if (_bd) { static u32 seen[32]; static int ns=0;
        u32 sk = (enable?0x80000000u:0u) | (st ? (st->blend_sfactor & 0xFFFFu) : 0u);
        int f=0; for (int k2=0;k2<ns;k2++) if (seen[k2]==sk) f=1;
        if (!f && ns<32) { seen[ns++]=sk;
            fprintf(stderr, "[BLEND] enable=%d sfactor=0x%X dfactor=0x%X%c", enable,
                    st?st->blend_sfactor:0, st?st->blend_dfactor:0, 10); } } }
    if (!enable) return 0;
    /* Factors never programmed: keep the legacy straight-alpha behaviour
     * (dbgfont-style text enables blending without setting factors). */
    if (!st || (st->blend_sfactor == 0 && st->blend_dfactor == 0))
        return 1u
             | ((u32)D3D12_BLEND_SRC_ALPHA     << 1)
             | ((u32)D3D12_BLEND_INV_SRC_ALPHA << 6)
             | ((u32)D3D12_BLEND_ONE           << 11)
             | ((u32)D3D12_BLEND_INV_SRC_ALPHA << 16)
             | ((u32)D3D12_BLEND_OP_ADD << 21) | ((u32)D3D12_BLEND_OP_ADD << 24);
    return 1u
         | ((u32)gl_blend_factor_d3d(st->blend_sfactor,       0) << 1)
         | ((u32)gl_blend_factor_d3d(st->blend_dfactor,       0) << 6)
         | ((u32)gl_blend_factor_d3d(st->blend_sfactor >> 16, 1) << 11)
         | ((u32)gl_blend_factor_d3d(st->blend_dfactor >> 16, 1) << 16)
         | ((u32)gl_blend_op_d3d(st->blend_equation)       << 21)
         | ((u32)gl_blend_op_d3d(st->blend_equation >> 16) << 24);
}

/* Bound colour target count for a draw record: 1 (target A) + the contiguous
 * prefix of MRT B/C/D offsets. */
static int dr_num_rts(const D3D12DrawRecord* dr)
{
    int n = 1;
    while (n < 4 && dr->rt_mrt[n - 1]) n++;
    return n;
}

/* Replace every occurrence of `find` with `repl` inside a NUL-terminated buffer,
 * shifting the tail. Used to retarget generated HLSL (2D sampler -> cube). */
static void hlsl_replace_all(char* buf, size_t cap, const char* find, const char* repl)
{
    size_t fl = strlen(find), rl = strlen(repl);
    if (!fl) return;
    char* at = buf;
    while ((at = strstr(at, find)) != NULL) {
        size_t used = strlen(buf) + 1;
        if (used + rl - fl > cap) return;
        memmove(at + rl, at + fl, used - (size_t)(at - buf) - fl);
        memcpy(at, repl, rl);
        at += rl;
    }
}

/* Which of a draw's texture units are cube textures, as a 4-bit mask. */
static u32 dr_cube_mask(const D3D12DrawRecord* dr)
{
    /* ON by default (RSX_CUBE_TEX=0 to disable). This was off while the cube path
     * cost 2262 PSO misses per 20 frames and 0.38 fps -- both caused by inline
     * fragment-program constants baking into the shader source. With those
     * constants hoisted into b1 the cube path costs 0 PSO misses, and with the
     * per-face stride fixed (each face is a whole mip pyramid, not one mip-0
     * image) all six faces decode as clean environment art. */
    static int en = -1;
    if (en < 0) { const char* e = getenv("RSX_CUBE_TEX"); en = e ? atoi(e) : 1; }
    if (!en) return 0;
    u32 m = 0;
    for (int u = 0; u < 4; u++) if (dr->tex[u].set && dr->tex[u].cube) m |= 1u << u;
    return m;
}

static ID3D12PipelineState* vp_get_fp_pso_wk85(int vs_idx, u32 fp_addr, u32 blend, int nrt, DXGI_FORMAT rtfmt, int exp32, u32 cmask, u32 cull, u32 cube_mask);
static ID3D12PipelineState* vp_get_fp_pso(int vs_idx, u32 fp_addr, u32 blend, int nrt, DXGI_FORMAT rtfmt, int exp32, u32 cmask, u32 cull, u32 cube_mask) { const unsigned long long t_ = WK85_T0(); ID3D12PipelineState* r_ = vp_get_fp_pso_wk85(vs_idx, fp_addr, blend, nrt, rtfmt, exp32, cmask, cull, cube_mask); WK85_ADD(WK85_pso, t_); return r_; }   /* winport v85a-walkprof */
static ID3D12PipelineState* vp_get_fp_pso_wk85(int vs_idx, u32 fp_addr, u32 blend, int nrt,
                                          DXGI_FORMAT rtfmt, int exp32, u32 cmask, u32 cull,
                                          u32 cube_mask)
{
    if (nrt < 1) nrt = 1; if (nrt > 4) nrt = 4;
    if (rtfmt == 0) rtfmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    u32 wp_alpha = s_wp_alpha; s_wp_alpha = 0;   /* winport-fix: buffered FP constants */
    const u32 stk0 = s_wp_stk0, stk1 = s_wp_stk0 ? s_wp_stk1 : 0u; s_wp_stk0 = s_wp_stk1 = 0;   /* winport v39: stencil */
    const u32 wtopo = s_wp_topo; s_wp_topo = 0;   /* winport v42 */
    const u32 cube_key = cube_mask ^ (stk0 * 0x9E3779B1u) ^ (stk1 * 0x85EBCA6Bu) ^ (wtopo * 0xC2B2AE35u);   /* hash input only */
    exp32 = (exp32 & 3) | (exp32 & 4) | (exp32 & 0xF0000) | (int)(wp_alpha << 4);   /* alpha test is part of the key */   /* winport-fix: v22 scene depth */
    const u32 wp_ctrl = (((exp32 & 3) == 2) ? 0x40u : ((exp32 & 3) == 1) ? 0u : RSX_FP_CTRL_AUTO)
                        | (((exp32 & 3) && (exp32 & 4)) ? 0x2u : 0u);   /* winport-fix: real SHADER_CONTROL for FP exports */ /* winport-fix: v22 scene depth */
    extern uint8_t* vm_base;
    extern u32 cellGcmResolveOffset(u32);
    if (!fp_addr || !vm_base) return NULL;
    { static int _off=-1; if(_off<0)_off=getenv("FP_OFF")?1:0; if(_off) return NULL; }
    /* Resolve the VS: the draw's cached per-VP blob, else the primary. */
    ID3DBlob* vsb = NULL; u32 vs_hash = 0;
    if (vs_idx >= 0 && vs_idx < s_d3d.vp_vs_n) {
        vsb = s_d3d.vp_vs[vs_idx].vs; vs_hash = s_d3d.vp_vs[vs_idx].hash;
    }
    if (!vsb) { vsb = s_d3d.vp_vs_blob; vs_idx = -1; }
    if (!vsb) return NULL;

    /* SET_SHADER_PROGRAM low bits = location+1 (same as textures): 1 = LOCAL
     * (VRAM), 2 = MAIN (gcm/cube emits 0x00B90001 for its VRAM-resident FP). */
    extern u32 cellGcmResolveLocated(int local, u32 offset);
    u32 off = cellGcmResolveLocated((fp_addr & 0x3u) == 1, fp_addr & ~0x3u);
    if (off == 0xFFFFFFFFu) return NULL;

    /* Content hash: inline constants are patched in place per frame (wave's
     * stamp), so identity is the BYTES, not the address. */
    /* Hash the CODE, not the constants: with fp_k[] hoisted into the per-draw
     * constant buffer the compiled shader is invariant under constant changes,
     * so hashing them made the pipeline key move every draw. */
    u32 uhash;
    if (s_wp_uh) { uhash = s_wp_uh; s_wp_uh = 0; } else   /* winport-fix: shader caches */
    {
        u32 usz = rsx_fp_program_size(vm_base + off, 4096);
        if (usz == 0) usz = 64;
        s_perf_pso_hashbytes += (int)usz;
        if (getenv("FP_CONSTBUF") && getenv("FP_CONSTBUF")[0] == '0') {
            uhash = 2166136261u;
            const u8* up = vm_base + off;
            for (u32 i = 0; i < usz; i++) { uhash ^= up[i]; uhash *= 16777619u; }
        } else {
            uhash = rsx_fp_code_hash(vm_base + off, 4096);
        }
    }

    s_perf_pso_calls++;
    s_wp_fp_lookups++;
    {   /* winport-fix: shader caches */ /* content-keyed index; fp_addr is not part of the key */
        u32 k_ = wp_fpkey(uhash, vs_hash, blend, nrt, (u32)rtfmt, exp32, cmask, cull, cube_key);   /* winport v39 */
        for (u32 j_ = 0, i_ = k_ & (WP_FPIDX_N - 1); j_ < WP_FPIDX_N; j_++, i_ = (i_ + 1) & (WP_FPIDX_N - 1)) {
            int e_ = s_wp_fpidx[i_] - 1;
            if (e_ < 0) break;
            const VPFPEntry* q_ = &s_d3d.vp_fp[e_];
            if (q_->ucode_hash == uhash && q_->vs_hash == vs_hash &&
                (vs_hash || q_->gen == s_d3d.vp_gen) &&
                q_->blend == blend && q_->nrt == nrt && q_->rtfmt == (u32)rtfmt &&
                q_->exp32 == exp32 && q_->cmask == cmask && q_->cull == cull &&
                q_->cube_mask == cube_mask && q_->stk0 == stk0 && q_->stk1 == stk1 && q_->wp_topo == wtopo)   /* winport v39, v42 */
                return q_->pso;
        }
    }
    s_perf_pso_miss++;      /* falls through to a full decompile + D3DCompile */
    /* PSOMISSDBG=1: on a miss, name the key field that differs from an existing
     * entry for the same program. "the cache misses" is not actionable; "it
     * misses on ucode_hash" is. */
    /* HASHDBG=1: how many DISTINCT ucode hashes each program produces. Inline
     * constants are patched into the shader bytes, so a program whose constants
     * animate yields a new hash -- and a new pipeline -- every time. */
    { static int hd = -1;
      if (hd < 0) { const char* e = getenv("HASHDBG"); hd = e ? atoi(e) : 0; }
      if (hd) { static u32 fps[32]; static u32 cnt[32]; static int nf = 0;
        int idx = -1;
        for (int i = 0; i < nf; i++) if (fps[i] == fp_addr) idx = i;
        if (idx < 0 && nf < 32) { idx = nf++; fps[idx] = fp_addr; cnt[idx] = 0; }
        if (idx >= 0) { cnt[idx]++;
          if ((cnt[idx] % 200) == 0)
            fprintf(stderr, "[HASHDBG] fp=0x%X has produced %u distinct compiles%c",
                    fp_addr, cnt[idx], 10); } } }
    { static int md = -1;
      if (md < 0) { const char* e = getenv("PSOMISSDBG"); md = e ? atoi(e) : 0; }
      if (md) { static int n = 0;
        for (int i = 0; i < s_d3d.vp_fp_n && n < 12; i++) {
            VPFPEntry* q = &s_d3d.vp_fp[i];
            if (q->fp_addr != fp_addr) continue;
            n++;
            fprintf(stderr, "[PSOMISS] fp=0x%X differs:%s%s%s%s%s%s%s%s%c", fp_addr,
                    q->vs_idx != vs_idx        ? " vs_idx"     : "",
                    q->vs_hash != vs_hash      ? " vs_hash"    : "",
                    q->gen != s_d3d.vp_gen     ? " gen"        : "",
                    q->blend != blend          ? " blend"      : "",
                    q->nrt != nrt              ? " nrt"        : "",
                    q->rtfmt != (u32)rtfmt     ? " rtfmt"      : "",
                    q->ucode_hash != uhash     ? " ucode_hash" : "",
                    q->cube_mask != cube_mask  ? " cube_mask"  : "", 10);
            break;
        } } }
    /* CUBEKEYDBG=1: which (program, cube mask) pairs are being compiled. If one
     * program shows several masks, the mask is unstable and is the reason the
     * pipeline cache thrashes with cube support on. */
    { static int ck = -1;
      if (ck < 0) { const char* e = getenv("CUBEKEYDBG"); ck = e ? atoi(e) : 0; }
      if (ck) { static u32 seen[64][2]; static int ns = 0; int known = 0;
        for (int i = 0; i < ns; i++)
            if (seen[i][0] == fp_addr && seen[i][1] == cube_mask) known = 1;
        if (!known && ns < 64) { seen[ns][0] = fp_addr; seen[ns][1] = cube_mask; ns++;
            fprintf(stderr, "[CUBEKEY] fp=0x%X cube_mask=0x%X (distinct pairs=%d)%c",
                    fp_addr, cube_mask, ns, 10); } } }
    static char hlsl[32768];
    /* Signature changed when the newer decompiler was adopted: the third
     * parameter is now SET_SHADER_CONTROL, not an exports-32 flag. This
     * backend does not track that register, so let the decompiler infer it. */
    (void)exp32;
    int n; u32 wp_nk = 0;   /* winport-fix: buffered FP constants */
    if (wp_fp_buffered()) {
        n = rsx_fp_decompile_buffered_ex(wp_fp_src(uhash, vm_base + off), 4096, wp_ctrl,
                                         cube_mask, hlsl, sizeof(hlsl), &wp_nk);
        if (n > 0 && (wp_alpha & 0x200u) &&
            rsx_fp_apply_alpha_test_buffered(hlsl, sizeof(hlsl), wp_alpha & 0x207u) < 0) {
            static int w_ = 0; if (w_++ < 8) printf("[FP] alpha test not applied (fp=0x%08X)\n", fp_addr);
        }
    } else
    n = rsx_fp_decompile(wp_fp_src(uhash, vm_base + off), 4096, wp_ctrl, hlsl, sizeof(hlsl));   /* winport-fix: shader caches */
    if (n <= 0) { static int _e=0; if(_e++<16) printf("[FP] decompile fail (fp=0x%08X)\n", fp_addr); return NULL; }
    if (s_wp68_s != 1.0f && strstr(hlsl, "input.position")) {   /* winport v68: WPOS is SV_Position (host px) -> guest px */
        char* m68 = strstr(hlsl, "main(PSInput input)");
        char* b68 = m68 ? strchr(m68, '{') : NULL;
        char ins68[80]; snprintf(ins68, sizeof ins68, "\n    input.position.xy *= %.9g;", 1.0 / (double)s_wp68_s);
        const size_t il68 = strlen(ins68), used68 = strlen(hlsl);
        if (b68 && used68 + il68 + 1 < sizeof(hlsl)) {
            memmove(b68 + 1 + il68, b68 + 1, used68 - (size_t)(b68 + 1 - hlsl) + 1);
            memcpy(b68 + 1, ins68, il68);
            s_wp68_n_wpos++;
            { static int n68 = 0; if (n68++ < 8) printf("[RES68] fp=0x%08X reads WPOS -> rescaled by 1/%.3g\n", fp_addr, (double)s_wp68_s); }
        } else { static int n68 = 0; if (n68++ < 8) printf("[RES68] fp=0x%08X reads WPOS but was NOT rescaled\n", fp_addr); }
    }
    if (n > 0 && ((exp32 & 0xF0000) || (exp32 & 4))) {   /* winport-fix: v22 scene depth */
        int zr_ = (exp32 & 0xF0000) ? rsx_fp_apply_depth_tex(hlsl, sizeof(hlsl), ((u32)exp32 >> 16) & 0xFu) : 0;
        { static int zl_ = 0; if (zl_++ < 40) printf("[ZDEPTH] fp=0x%08X uh=0x%08X depth_export=%d tex_units=0x%X sample_sites=%d %s\n", fp_addr, uhash, (exp32 & 4) ? 1 : 0, ((u32)exp32 >> 16) & 0xFu, zr_, strstr(hlsl, "SV_Depth") ? "SV_Depth emitted" : ""); }
    }
    /* FP_LIST=1: every program the title actually compiles, with its size. A
     * fragment program that hangs the GPU shows up here as an implausible
     * instruction count long before it shows up as a TDR. */
    { static const char* dd_ = (const char*)1; static int nd_ = 0;   /* winport-fix: FP source dump */
      static u32 seen_[512]; static int ns_ = 0;
      if (dd_ == (const char*)1) { dd_ = getenv("WINPORT_FPDUMP"); if (dd_ && !*dd_) dd_ = NULL;
                                   if (dd_) CreateDirectoryA(dd_, NULL); }
      u32 key_ = uhash ^ ((u32)(exp32 & 3) << 30) ^ (wp_alpha ? 0x5A5A0000u : 0u) ^ ((u32)(exp32 & 0xF0004) * 0x9E3779B1u);   /* winport-fix: v22 scene depth */
      int known_ = 0; for (int i_ = 0; i_ < ns_; i_++) if (seen_[i_] == key_) { known_ = 1; break; }
      if (dd_ && !known_ && nd_ < 400 && ns_ < 512) {
          seen_[ns_++] = key_; nd_++;
          char fn_[512]; snprintf(fn_, sizeof fn_, "%s/fp_%08X_m%d%s.hlsl", dd_, uhash, (int)(exp32 & 3), wp_alpha ? "_at" : "");
          FILE* f_ = fopen(fn_, "w");
          if (f_) {
              const u8* uc_ = wp_fp_src(uhash, vm_base + off);
              u32 usz_ = rsx_fp_program_size(uc_, 4096); if (!usz_ || usz_ > 4096) usz_ = 64;
              fprintf(f_, "/* fp_addr=0x%08X uhash=0x%08X mode=%d (0 auto,1 fp16,2 fp32) ctrl=0x%08X alpha=0x%X instrs=%d nrt=%d cmask=%X blend=0x%X\n   ucode (%u bytes):", fp_addr, uhash, (int)(exp32 & 3), wp_ctrl, wp_alpha, n, nrt, cmask, blend, usz_);
              for (u32 b_ = 0; b_ < usz_; b_++) fprintf(f_, "%s%02X", (b_ % 16) ? " " : "\n   ", uc_[b_]);
              fprintf(f_, "\n*/\n%s\n", hlsl);
              fclose(f_);
          }
      } }
    { static int fl = -1; if (fl < 0) fl = getenv("FP_LIST") ? 1 : 0;
      if (fl) fprintf(stderr, "[FPLIST] fp=0x%08X instrs=%d hlsl=%u bytes%c",
                      fp_addr, n, (unsigned)strlen(hlsl), 10); }
    if (getenv("FP_DUMP")) { static int _d=0; if (_d++ < 4) {
        FILE* f = fopen("fp_dump.hlsl", _d==1 ? "w" : "a");
        if (f) {
            const u8* uc = vm_base + off;
            fprintf(f, "/* fp_addr=0x%08X vmoff=0x%08X raw:", fp_addr, off);
            for (int _b = 0; _b < 32; _b++) fprintf(f, " %02X", uc[_b]);
            fprintf(f, " */\n%s\n", hlsl); fclose(f);
        } } }

    /* FP_PICK=<hex fp_addr>: write just that program, whatever order it compiles
     * in. fp_dump.hlsl keeps only the first four, same trap as vp2_dump.hlsl. */
    { static const char* fpk = (const char*)1; static u32 wantf = 0; static int done = 0;
      if (fpk == (const char*)1) { fpk = getenv("FP_PICK");
                                   wantf = fpk ? (u32)strtoul(fpk, NULL, 16) : 0; }
      if (wantf && !done && fp_addr == wantf) { done = 1;
          FILE* f = fopen("duck_fp.hlsl", "w");
          if (f) { fprintf(f, "/* fp_addr=0x%08X, %d instrs */%s", fp_addr, n, hlsl);
                   fclose(f); }
          fprintf(stderr, "[DUCKFP] wrote duck_fp.hlsl (fp=0x%08X, %d instrs)%c",
                  fp_addr, n, 10);
      } }

    /* Debug FP_ONE=<hex fp_addr>: force that program's colour output to
     * all-ones (e.g. wave's colour-detect -> full mask -> island borders
     * everywhere -> the water sim must visibly radiate if it works). */
    { const char* f1 = getenv("FP_ONE");
      if (f1 && (u32)strtoul(f1, NULL, 16) == fp_addr) {
          /* Match the assignment, not one exact spelling of it: the final
           * register is r[N] for some programs and h[N] for others, and the old
           * fixed "= r[0];" anchor silently did nothing for every h[] shader --
           * so this switch appeared to have no effect and was useless for
           * identifying which program draws what. Rewrite the expression with a
           * proper length-changing splice. */
          char* rp = strstr(hlsl, "_po.c0 = ");
          if (rp) {
              char* semi = strchr(rp, ';');
              if (semi) {
                  static const char rep[] = "_po.c0 = (1.0).xxxx";
                  size_t oldlen = (size_t)(semi - rp), newlen = sizeof(rep) - 1;
                  size_t tail = strlen(semi) + 1;
                  if ((rp - hlsl) + newlen + tail < sizeof(hlsl)) {
                      memmove(rp + newlen, semi, tail);
                      memcpy(rp, rep, newlen);
                  }
                  (void)oldlen;
              }
          }
      } }
    /* FP_TEX=1: make every program output its unit-0 texture sample verbatim.
     * Separates "the texture is wrong" from "the shading tints it" in one run,
     * without having to identify which program draws which surface first. */
    /* FP_SHOW=<hlsl expr>: replace a program's colour output with an arbitrary
     * expression (e.g. FP_SHOW=h[1]) so an intermediate register can be looked
     * at directly. Restrict with FP_TEX_FP=<hex>. Reading a 69-instruction
     * shader to guess which term carries a bad value is slower and less certain
     * than displaying the terms. */
    /* FP_KILL=<hex fp_addr>: make that program discard every fragment, so what
     * it covers becomes visible. Distinguishes "this surface shades black" from
     * "this surface is correct and something else is missing behind it". */
    { const char* kf = getenv("FP_KILL");
      if (kf && (u32)strtoul(kf, NULL, 16) == fp_addr) {
          char* rp = strstr(hlsl, "PSOut _po;");
          if (rp) {
              const char* ins = "discard; ";
              size_t nl = strlen(ins), tail = strlen(rp) + 1;
              if ((size_t)(rp - hlsl) + nl + tail < sizeof(hlsl)) {
                  memmove(rp + nl, rp, tail);
                  memcpy(rp, ins, nl);
                  fprintf(stderr, "[FPKILL] fp=0x%X discards%c", fp_addr, 10);
              }
          }
      } }
    { const char* sh = getenv("FP_SHOW");
      const char* only = getenv("FP_TEX_FP");
      if (sh && (!only || (u32)strtoul(only, NULL, 16) == fp_addr)) {
          char* rp = strstr(hlsl, "_po.c0 = ");
          if (rp) { char* semi = strchr(rp, ';');
            if (semi) {
                char rep[256];
                snprintf(rep, sizeof rep, "_po.c0 = float4((%s).xyz, 1.0)", sh);
                size_t newlen = strlen(rep), tail = strlen(semi) + 1;
                if ((size_t)(rp - hlsl) + newlen + tail < sizeof(hlsl)) {
                    memmove(rp + newlen, semi, tail);
                    memcpy(rp, rep, newlen);
                    fprintf(stderr, "[FPSHOW] fp=0x%X output replaced with %s%c",
                            fp_addr, sh, 10);
                }
            } }
      } }
    /* Retarget the samplers the guest bound as CUBE textures. The decompiler
     * always emits 2D samplers; a cube is sampled with a 3-component direction
     * and no texel-scale, and the emitted coordinate always ends in the fixed
     * tail ".xy * rsx_texscale[N].xy", so both edits are exact replacements. */
    if (cube_mask) {
        for (int _u = 0; _u < 4; _u++) {
            if (!(cube_mask & (1u << _u))) continue;
            char f1[64], r1[64], f2[64], r2[64];
            snprintf(f1, sizeof f1, "Texture2D    rsx_tex%d", _u);
            snprintf(r1, sizeof r1, "TextureCube  rsx_tex%d", _u);
            hlsl_replace_all(hlsl, sizeof hlsl, f1, r1);
            snprintf(f1, sizeof f1, "Texture2D rsx_tex%d", _u);
            snprintf(r1, sizeof r1, "TextureCube rsx_tex%d", _u);
            hlsl_replace_all(hlsl, sizeof hlsl, f1, r1);
            snprintf(f2, sizeof f2, ".xy * rsx_texscale[%d].xy", _u);
            snprintf(r2, sizeof r2, ".xyz");
            hlsl_replace_all(hlsl, sizeof hlsl, f2, r2);
        }
        { static int _n = 0; if (_n++ < 4)
            fprintf(stderr, "[CUBE] fp=0x%X compiled with cube units mask 0x%X%c",
                    fp_addr, cube_mask, 10); }
    }
    /* FP_IDCOLOR=1: give every fragment program a distinct flat colour derived
     * from its address. One frame then shows which program paints which surface,
     * instead of one run per candidate to test them by elimination. */
    if (getenv("FP_IDCOLOR")) {
        char* rp = strstr(hlsl, "_po.c0 = ");
        if (rp) {
            char* semi = strchr(rp, ';');
            if (semi) {
                /* Mix properly: every fp address in a title tends to share low
                 * bits (they all end 0x01/0x81 here), so a single multiply left
                 * one channel constant across all programs and made distinct
                 * shaders look like the same colour. */
                u32 hsh = fp_addr * 2654435761u;
                hsh ^= hsh >> 13; hsh *= 0x5BD1E995u; hsh ^= hsh >> 15;
                /* Force the top bit on in each channel: a hash that happens to
                 * land near black is indistinguishable from "this surface was
                 * never painted", which is the exact question the mode exists
                 * to answer. */
                float cr = (float)(((hsh >> 16) & 0x7F) | 0x80) / 255.0f;
                float cg = (float)(((hsh >>  8) & 0x7F) | 0x80) / 255.0f;
                float cb = (float)(((hsh      ) & 0x7F) | 0x80) / 255.0f;
                char rep[128];
                snprintf(rep, sizeof rep, "_po.c0 = float4(%.3f,%.3f,%.3f,1.0)", cr, cg, cb);
                size_t newlen = strlen(rep), tail = strlen(semi) + 1;
                if ((size_t)(rp - hlsl) + newlen + tail < sizeof(hlsl)) {
                    memmove(rp + newlen, semi, tail);
                    memcpy(rp, rep, newlen);
                    fprintf(stderr, "[FPID] fp=0x%X -> rgb(%.0f,%.0f,%.0f)%c",
                            fp_addr, cr*255, cg*255, cb*255, 10);
                }
            }
        }
    }
    /* FP_TEX=<unit>: output that unit's sample verbatim. FP_TEX_FP=<hex> limits
     * it to one program, so one surface can be inspected without repainting the
     * whole scene. FP_TEX_UV=1 samples with the same texcoord the program uses
     * for that unit rather than tc0. */
    if (getenv("FP_TEX") && (!getenv("FP_TEX_FP") ||
        (u32)strtoul(getenv("FP_TEX_FP"), NULL, 16) == fp_addr)) {
        int _un = atoi(getenv("FP_TEX"));
        if (_un < 0 || _un > 3) _un = 0;
        char* rp = strstr(hlsl, "_po.c0 = ");
        if (rp) {
            char* semi = strchr(rp, ';');
            if (semi) {
                char rep[160];
                /* FP_TEX_UV=1: sample at a fixed centre UV instead of the
                 * interpolated texcoord. If the texture is populated but the
                 * texcoords never arrive, the normal path samples texel (0,0)
                 * -- usually black -- and looks exactly like an empty texture.
                 * This tells the two apart. */
                if (getenv("FP_TEX_UV"))
                    snprintf(rep, sizeof rep,
                             "_po.c0 = rsx_tex%d.Sample(rsx_samp%d, float2(0.5, 0.5))",
                             _un, _un);
                else
                snprintf(rep, sizeof rep,
                         "_po.c0 = rsx_tex%d.Sample(rsx_samp%d, input.tc0.xy * rsx_texscale[%d].xy)",
                         _un, _un, _un);
                size_t newlen = strlen(rep), tail = strlen(semi) + 1;
                fprintf(stderr, "[FP_TEX] program 0x%X rewritten to sample unit %d%c",
                        fp_addr, _un, 10);
                if ((size_t)(rp - hlsl) + newlen + tail < sizeof(hlsl)) {
                    memmove(rp + newlen, semi, tail);
                    memcpy(rp, rep, newlen);
                }
            }
        }
    }
    /* FP_FORCE=1: replace the translated body with solid magenta -- isolates
     * geometry/transform problems from texture/blend problems. */
    if (getenv("FP_FORCE")) {
        char* rp = strstr(hlsl, "return r[0];");
        if (rp) memcpy(rp, "return float4(1,0,1,1)", 23);
    }
    ID3DBlob* pb = NULL; ID3DBlob* e = NULL;
    HRESULT hr = D3DCompile(hlsl, strlen(hlsl), "guest_fp", NULL, NULL,
                            "main", "ps_5_0", 0, 0, &pb, &e);
    if (FAILED(hr) || !pb) {
        static int _e2=0; if (_e2++<16)
            printf("[FP] PS compile FAIL (fp=0x%08X): %s\n", fp_addr,
                   e ? (const char*)e->lpVtbl->GetBufferPointer(e) : "?");
        if (e) e->lpVtbl->Release(e);
        return NULL;
    }
    if (e) e->lpVtbl->Release(e);

    D3D12_INPUT_ELEMENT_DESC il[16];
    for (int _i = 0; _i < 16; _i++) {
        il[_i].SemanticName = "ATTR"; il[_i].SemanticIndex = _i;
        il[_i].Format = DXGI_FORMAT_R32G32B32A32_FLOAT; il[_i].InputSlot = 0;
        il[_i].AlignedByteOffset = (UINT)(_i * 16);
        il[_i].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        il[_i].InstanceDataStepRate = 0;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {0};
    pd.pRootSignature = s_d3d.vp_root_sig;
    pd.VS.pShaderBytecode = vsb->lpVtbl->GetBufferPointer(vsb);
    pd.VS.BytecodeLength  = vsb->lpVtbl->GetBufferSize(vsb);
    pd.PS.pShaderBytecode = pb->lpVtbl->GetBufferPointer(pb);
    pd.PS.BytecodeLength  = pb->lpVtbl->GetBufferSize(pb);
    pd.InputLayout.pInputElementDescs = il; pd.InputLayout.NumElements = 16;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    /* Guest face culling. CULL_OFF=1 forces double-sided, the old
     * unconditional behaviour -- geometry that vanishes under culling is a
     * winding/front-face problem, not a missing draw. */
    { static int _co = -1; if (_co < 0) _co = getenv("CULL_OFF") ? 1 : 0;
      pd.RasterizerState.CullMode = (!_co && (cull & 1u))
          ? ((cull & 2u) ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_BACK)
          : D3D12_CULL_MODE_NONE;
      /* CULL_FLIP=1: invert the front-face convention. Our VP path may hand
       * D3D the opposite winding from the guest's, in which case honouring
       * CULL_FACE culls exactly the faces it should keep. */
      static int _cfl = -1; if (_cfl < 0) _cfl = getenv("CULL_FLIP") ? 1 : 0;
      pd.RasterizerState.FrontCounterClockwise =
          (((cull & 4u) ? 1 : 0) ^ _cfl) ? TRUE : FALSE; }
    /* Blend per the guest's packed key (see rsx_blend_key): dbgfont text needs
     * straight alpha; demosaic's effect passes blend OFF; DeferredShading's
     * light accumulation is additive ONE,ONE. */
    pd.BlendState.RenderTarget[0].BlendEnable    = (blend & 1) ? TRUE : FALSE;
    pd.BlendState.RenderTarget[0].SrcBlend       = (blend & 1) ? (D3D12_BLEND)((blend >> 1)  & 0x1F) : D3D12_BLEND_ONE;
    pd.BlendState.RenderTarget[0].DestBlend      = (blend & 1) ? (D3D12_BLEND)((blend >> 6)  & 0x1F) : D3D12_BLEND_ZERO;
    pd.BlendState.RenderTarget[0].BlendOp        = (blend & 1) ? (D3D12_BLEND_OP)((blend >> 21) & 0x7) : D3D12_BLEND_OP_ADD;
    pd.BlendState.RenderTarget[0].SrcBlendAlpha  = (blend & 1) ? (D3D12_BLEND)((blend >> 11) & 0x1F) : D3D12_BLEND_ONE;
    pd.BlendState.RenderTarget[0].DestBlendAlpha = (blend & 1) ? (D3D12_BLEND)((blend >> 16) & 0x1F) : D3D12_BLEND_ZERO;
    pd.BlendState.RenderTarget[0].BlendOpAlpha   = (blend & 1) ? (D3D12_BLEND_OP)((blend >> 24) & 0x7) : D3D12_BLEND_OP_ADD;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = (wtopo == 1u) ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : (wtopo == 2u) ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT
                                             : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;   /* winport v42 */
    pd.NumRenderTargets = (UINT)nrt;
    for (int _r = 0; _r < nrt; _r++) {
        pd.RTVFormats[_r] = rtfmt;
        /* Zero-init leaves RenderTargetWriteMask 0 on secondary targets --
         * every MRT-B write would be masked off. Mirror RT0's blend state. */
        pd.BlendState.RenderTarget[_r] = pd.BlendState.RenderTarget[0];
    }
    /* Guest colour write mask (RGBA nibble, already D3D-ordered). cmask is 0xF
     * by default (unset colour_mask register decodes to all-on); a literal 0
     * therefore means the guest EXPLICITLY masked every channel -- a depth-only
     * pass (e.g. DeferredShading's shadow-map generation). Honour it: forcing 0
     * back to 0xF splatters the depth pass's fragment colour onto the target. */
    /* CMASK_FORCE=1: ignore the guest colour mask (write RGBA on every draw).
     * A mis-decoded mask makes geometry rasterize correctly and write nothing,
     * which is indistinguishable from "the draw never happened". */
    { static int _cf = -1; if (_cf < 0) _cf = getenv("CMASK_FORCE") ? 1 : 0;
      if (_cf) cmask = 0xF; }
    for (int _r = 0; _r < nrt; _r++)
        pd.BlendState.RenderTarget[_r].RenderTargetWriteMask = (UINT8)(cmask & 0xF);
    pd.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pd.DepthStencilState.StencilEnable = FALSE;
    if (cull & 0x100u) {   /* winport-fix: guest depth state */
        pd.DepthStencilState.DepthEnable = (cull & 8u) ? TRUE : FALSE;
        pd.DepthStencilState.DepthWriteMask = ((cull & 8u) && (cull & 16u)) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        pd.DepthStencilState.DepthFunc = (D3D12_COMPARISON_FUNC)(((cull >> 5) & 7u) + 1u);
        if (pd.DepthStencilState.DepthFunc == D3D12_COMPARISON_FUNC_LESS) pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;   /* keep v16 behaviour for plain LESS: equal-z decals */
    }
    if (stk0 & 1u) {   /* winport v39: guest stencil test (see wp_stencil_on) */
        D3D12_DEPTH_STENCIL_DESC* ds_ = &pd.DepthStencilState;
        ds_->StencilEnable = TRUE;
        ds_->StencilReadMask  = (UINT8)(stk1 & 0xFFu);          /* D3D12 has one mask pair for both faces */
        ds_->StencilWriteMask = (UINT8)((stk1 >> 8) & 0xFFu);
        ds_->FrontFace.StencilFunc        = (D3D12_COMPARISON_FUNC)wp_scmp(stk0 >> 1);   /* winport v41 */
        ds_->FrontFace.StencilFailOp      = (D3D12_STENCIL_OP)((stk0 >> 4) & 0xFu);
        ds_->FrontFace.StencilDepthFailOp = (D3D12_STENCIL_OP)((stk0 >> 8) & 0xFu);
        ds_->FrontFace.StencilPassOp      = (D3D12_STENCIL_OP)((stk0 >> 12) & 0xFu);
        ds_->BackFace.StencilFunc         = (D3D12_COMPARISON_FUNC)wp_scmp(stk0 >> 16);   /* winport v41 */
        ds_->BackFace.StencilFailOp       = (D3D12_STENCIL_OP)((stk0 >> 19) & 0xFu);
        ds_->BackFace.StencilDepthFailOp  = (D3D12_STENCIL_OP)((stk0 >> 23) & 0xFu);
        ds_->BackFace.StencilPassOp       = (D3D12_STENCIL_OP)((stk0 >> 27) & 0xFu);
        { static int n_ = 0; if (n_++ < 24)
            fprintf(stderr, "[STENCIL] PSO fp=0x%08X stk0=0x%08X masks=0x%08X depth=%d/%d cmask=%X\n",
                    fp_addr, stk0, stk1, (int)ds_->DepthEnable, (int)ds_->DepthWriteMask, cmask); }
    }
    /* DEPTH_OFF=1: drop the depth test for guest-FP draws, so submission order
     * alone decides what is on top. Paired with DRAW_LAST_TEX this puts one
     * object in front of everything and answers "is it merely occluded?" --
     * which reordering alone cannot, because a relocated draw still fails the
     * depth test against whatever is already in the buffer. Diagnostic. */
    { static int doff = -1;
      if (doff < 0) { const char* e = getenv("DEPTH_OFF"); doff = e ? atoi(e) : 0; }
      if (doff) { pd.DepthStencilState.DepthEnable = FALSE;
                  pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; } }
    pd.SampleDesc.Count = 1;

    ID3D12PipelineState* pso = NULL;
    hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
        s_d3d.device, &pd, &IID_ID3D12PipelineState, (void**)&pso);
    pb->lpVtbl->Release(pb);
    if (FAILED(hr)) {
        static int _e3=0; if (_e3++<16) printf("[FP] PSO FAIL (fp=0x%08X, 0x%08lX)\n", fp_addr, hr);
        return NULL;
    }
    { static int _ok=0; if (_ok++<32) printf("[FP] guest FP pipeline ready (fp=0x%08X)\n", fp_addr); }

    s_wp_fp_compiles++;
    if (s_d3d.vp_fp_n >= VP_FP_CACHE) {   /* winport-fix: shader caches */ /* never evict mid-frame */
        s_wp_fp_overflow = 1;
        if (s_wp_fp_defer_n < 256) s_wp_fp_defer[s_wp_fp_defer_n++] = pso;
        return pso;
    }
    s_d3d.vp_fp[s_d3d.vp_fp_n].fp_addr = fp_addr;
    s_d3d.vp_fp[s_d3d.vp_fp_n].vs_idx  = vs_idx;
    s_d3d.vp_fp[s_d3d.vp_fp_n].vs_hash = vs_hash;
    s_d3d.vp_fp[s_d3d.vp_fp_n].gen     = s_d3d.vp_gen;
    s_d3d.vp_fp[s_d3d.vp_fp_n].blend   = blend;
    s_d3d.vp_fp[s_d3d.vp_fp_n].nrt     = nrt;
    s_d3d.vp_fp[s_d3d.vp_fp_n].rtfmt   = (u32)rtfmt;
    s_d3d.vp_fp[s_d3d.vp_fp_n].exp32   = exp32;
    s_d3d.vp_fp[s_d3d.vp_fp_n].ucode_hash = uhash;
    s_d3d.vp_fp[s_d3d.vp_fp_n].cmask   = cmask;
    s_d3d.vp_fp[s_d3d.vp_fp_n].cull    = cull;
    s_d3d.vp_fp[s_d3d.vp_fp_n].cube_mask = cube_mask;
    s_d3d.vp_fp[s_d3d.vp_fp_n].stk0 = stk0; s_d3d.vp_fp[s_d3d.vp_fp_n].stk1 = stk1;   /* winport v39 */
    s_d3d.vp_fp[s_d3d.vp_fp_n].wp_topo = wtopo;   /* winport v42 */
    s_d3d.vp_fp[s_d3d.vp_fp_n].pso     = pso;
    {   /* winport-fix: shader caches */ /* index it */
        u32 k_ = wp_fpkey(uhash, vs_hash, blend, nrt, (u32)rtfmt, exp32, cmask, cull, cube_key);   /* winport v39 */
        for (u32 j_ = 0, i_ = k_ & (WP_FPIDX_N - 1); j_ < WP_FPIDX_N; j_++, i_ = (i_ + 1) & (WP_FPIDX_N - 1))
            if (!s_wp_fpidx[i_]) { s_wp_fpidx[i_] = s_d3d.vp_fp_n + 1; break; }
    }
    s_d3d.vp_fp_n++;
    return pso;
}

/* Upload the guest texture for a VP draw into a per-frame texture slot
 * (re-uploaded every frame: gcm/cube's plasma animates in guest memory).
 * Returns the slot index (SRV at heap 1+slot) or -1. Must run while the
 * command list is open, before the draw passes. */
/* NV4097 TEXTURE_CONTROL1 component remap -> D3D12 Shader4ComponentMapping.
 * The crossbar decode itself is RSX semantics and lives in
 * rsx_texture_layout.c; all that is left here is packing the four selectors
 * into D3D12's word. Its force-zero/force-one encodings are 4 and 5, which is
 * what RSX_REMAP_ZERO/ONE already are, so the selectors pass straight through.
 *
 * out[] arrives in the crossbar's field order A,R,G,B; D3D12 wants
 * destR | destG<<3 | destB<<6 | destA<<9 | valid<<12. */
static u32 rsx_remap_to_d3d(u32 c1, u32 basef)
{
    u8 out[4];
    rsx_texture_component_remap(c1, basef, out);
    /* TEX_REMAP_ID=<n>: override the derived crossbar with a fixed mapping, to
     * test channel order directly. The resource holds the guest's A8R8G8B8
     * bytes straight through, so its components are (R=A, G=R, B=G, A=B) and
     * the shader needs destR=1, destG=2, destB=3, destA=0 to see real RGBA.
     *   1 = identity   2 = rotate (the ARGB fix)   3 = BGRA swap */
    { static int fixed = -1;
      if (fixed < 0) { const char* e = getenv("TEX_REMAP_ID"); fixed = e ? atoi(e) : 0; }
      if (fixed == 1) return 0u | (1u<<3) | (2u<<6) | (3u<<9) | (1u<<12);
      if (fixed == 2) return 1u | (2u<<3) | (3u<<6) | (0u<<9) | (1u<<12);
      if (fixed == 3) return 2u | (1u<<3) | (0u<<6) | (3u<<9) | (1u<<12); }
    /* D3D12 mapping: destR | destG<<3 | destB<<6 | destA<<9 | valid bit */
    { static int _rd = -1; if (_rd < 0) _rd = getenv("REMAPDBG") ? 1 : 0;
      if (_rd) { static u32 seen[32]; static int ns=0;
        u32 k = (basef << 16) | (c1 & 0xFFFFu); int f=0;
        for (int i2=0;i2<ns;i2++) if (seen[i2]==k) f=1;
        if (!f && ns<32) { seen[ns++]=k;
            fprintf(stderr, "[REMAP] basef=0x%02X c1=0x%04X -> destR=%u destG=%u destB=%u destA=%u%c",
                    basef, c1 & 0xFFFFu, out[1], out[2], out[3], out[0], 10); } } }
    return out[1] | (out[2] << 3) | (out[3] << 6) | (out[0] << 9) | (1u << 12);
}

/* Morton/Z-order texel offset for RSX swizzled textures (LN bit clear).
 * Interleaves x (even bit positions) and y (odd) until the smaller dimension's
 * bits run out, then the larger dimension's remaining bits ride above -- the
 * NV40 layout (matches RPCS3 convert_linear_swizzle). POT dims only, which is
 * what the hardware requires for swizzled textures. */

/* Sparse FNV-1a over a texture's source bytes -- enough to notice an animated
 * surface changing, cheap enough to run on every bind. Kept a function rather
 * than a statement-expression macro so the file still compiles under MSVC;
 * ps3recomp builds with clang-cl, but titles like Tokyo Jungle drive the build
 * through the Visual Studio generator. */
/* ---- winport v85c-walkfast: WINPORT_TEX85 texture slot memo ---- */
static unsigned s85_tex_gen = 1;
static int s85_tex_want = -1;   /* verify mode: the memo's answer, checked against the scan */
typedef struct { u32 key, w, h, fmt; int cube; unsigned gen; int slot; } Wp85TexMemo;
static Wp85TexMemo s85_texm[1024];
static inline Wp85TexMemo* wp85_texm(u32 key, u32 w, u32 h, u32 fmt, int cube)
{ return &s85_texm[((key >> 4) ^ (key >> 14) ^ (w * 31u) ^ (h * 131u) ^ (fmt * 7u) ^ (u32)cube) & 1023u]; }
static inline void wp85_tex_note(u32 key, u32 w, u32 h, u32 fmt, int cube, int slot)
{ if (!wp85_tex_on()) return; if (s85_tex_want >= 0) { wp85_vcount(2, s85_tex_want != slot); s85_tex_want = -1; }
  Wp85TexMemo* m = wp85_texm(key, w, h, fmt, cube);
  m->key = key; m->w = w; m->h = h; m->fmt = fmt; m->cube = cube; m->gen = s85_tex_gen; m->slot = slot; }
/* ---- end winport v85c-walkfast tex memo ---- */
static u32 tex_csum(const u8* base, u32 nbytes)
{
    u32 h = 2166136261u;
    u32 step = nbytes > 4096u ? nbytes / 1024u : 4u;
    for (u32 i = 0; i + 3 < nbytes; i += step) {
        u32 w32; memcpy(&w32, base + i, sizeof w32);
        h ^= w32; h *= 16777619u;
    }
    return h;
}

static int vp_upload_tex_slot_wk85(u32 off, u32 w, u32 h, u32 fmt, int cube, u32 mips);
static int vp_upload_tex_slot(u32 off, u32 w, u32 h, u32 fmt, int cube, u32 mips) { const unsigned long long t_ = WK85_T0(); int r_ = vp_upload_tex_slot_wk85(off, w, h, fmt, cube, mips); WK85_ADD(WK85_texup, t_); return r_; }   /* winport v85a-walkprof */
static int vp_upload_tex_slot_wk85(u32 off, u32 w, u32 h, u32 fmt, int cube, u32 mips)
{
    /* TEX_BUDGET=<n>: cap texture uploads per frame. Off by default -- it was
     * tried against the TDR and does not help, because PERF shows the upload
     * path costing 0.01s for 775 calls. Kept because it is the right lever if a
     * title ever does hit an upload-bound frame. */
    { static int budget = -1;
      if (budget < 0) { const char* e = getenv("TEX_BUDGET"); budget = e ? atoi(e) : 0; }
      if (budget > 0) {
          static u64 last_frame = ~0ull; static int used = 0;
          if (last_frame != (u64)s_d3d.frame_count) { last_frame = (u64)s_d3d.frame_count; used = 0; }
          if (++used > budget) return -1;   /* try again next frame */
      } }
    extern uint8_t* vm_base;

    if (!off || !w || !h || !vm_base || !s_d3d.srv_heap) return -1;
    s85_tex_want = -1;
    if (wp85_tex_on()) {   /* winport v85c-walkfast: same answer as the scan while no slot identity changed */
        const Wp85TexMemo* m85_ = wp85_texm(off, w, h, fmt, cube);
        if (m85_->gen == s85_tex_gen && m85_->key == off && m85_->w == w && m85_->h == h && m85_->fmt == fmt && m85_->cube == cube) {
            const VPTexSlot* c85_ = &s_d3d.vp_tex[m85_->slot];
            if (c85_->res && c85_->used && c85_->key == off && c85_->w == w && c85_->h == h && c85_->fmt == fmt && c85_->cube == cube) {
                if (!wp85_verify()) { s_wp_tx_hit++; return m85_->slot; }
                s85_tex_want = m85_->slot;   /* verify: let the scan answer and compare */
            }
        }
    }
    /* Format classes (base = fmt & 0x9F). The LBP loading screen uses:
     * 0x85 A8R8G8B8 (swizzled UI art), 0x8B G8B8 (the 1024x2048 linear FONT
     * atlas -- without it no text renders at all), 0x86/87/88 DXT1/23/45
     * (512x512 detail/LUT layers bound at t1/t3 on every draw). */
    /* Format class, row sizes and swizzling are RSX semantics: rsx_texture_
     * layout.c owns them so the Metal backend can read a guest texture without
     * re-deriving any of it. Only the DXGI mapping below is ours. */
    rsx_tex_layout tl;
    rsx_texture_layout(fmt, w, h, &tl);
    u32 basef = fmt & 0x9F;              /* still needed for the SRV remap */
    int argb = (tl.fmt == RSX_TEXFMT_R8G8B8A8);
    int dxt  = tl.compressed;
    /* Compressed rows are counted in blocks, so keep bpp at 1 for the byte
     * arithmetic the diagnostics below do. */
    u32 bpp = tl.compressed ? 1u : tl.bytes_per_texel;
    /* The DECODED image's format, which is not always the guest's: the packed
     * 16-bit colour formats arrive here unpacked to RGBA8, and DEPTH24_D8 as a
     * float. rsx_texture_layout.h says which class each format byte lands in. */
    DXGI_FORMAT dxfmt = (tl.fmt == RSX_TEXFMT_R8G8B8A8) ? DXGI_FORMAT_R8G8B8A8_UNORM
                      : (tl.fmt == RSX_TEXFMT_R8G8)     ? DXGI_FORMAT_R8G8_UNORM
                      : (tl.fmt == RSX_TEXFMT_BC1)      ? DXGI_FORMAT_BC1_UNORM
                      : (tl.fmt == RSX_TEXFMT_BC2)      ? DXGI_FORMAT_BC2_UNORM
                      : (tl.fmt == RSX_TEXFMT_BC3)      ? DXGI_FORMAT_BC3_UNORM
                      : (tl.fmt == RSX_TEXFMT_R16)      ? DXGI_FORMAT_R16_UNORM
                      : (tl.fmt == RSX_TEXFMT_R16G16)   ? DXGI_FORMAT_R16G16_UNORM
                      : (tl.fmt == RSX_TEXFMT_R16G16F)  ? DXGI_FORMAT_R16G16_FLOAT
                      : (tl.fmt == RSX_TEXFMT_R16G16B16A16F) ? DXGI_FORMAT_R16G16B16A16_FLOAT
                      : (tl.fmt == RSX_TEXFMT_R32F)     ? DXGI_FORMAT_R32_FLOAT
                      : (tl.fmt == RSX_TEXFMT_R32G32B32A32F) ? DXGI_FORMAT_R32G32B32A32_FLOAT
                      : DXGI_FORMAT_R8_UNORM;
    /* DXT data is stored as linear 4x4 block rows (compressed formats are
     * never Morton-swizzled on RSX). Row of blocks = (w/4)*blocksize. */
    u32 blkrow  = dxt ? tl.row_bytes : 0;
    u32 blkrows = dxt ? tl.rows      : 0;
    const u32 key_off = off;         /* lookup key: the offset as bound */
    /* Sparse checksum of a texture's source bytes -- enough to notice an
     * animated surface changing, cheap enough to run on every bind. */
    /* (portable helper tex_csum() -- see above; a statement-expression macro
     * here was a GCC/Clang extension MSVC rejects.) */
    #define TEX_CSUM(base, nbytes) tex_csum((base), (nbytes))
    int slot = -1, freeslot = -1, fcls_ = 3;   /* winport-fix: texture slot LRU */
    for (int i = 0; i < VP_TEX_SLOTS; i++) {
        VPTexSlot* c = &s_d3d.vp_tex[i];
        if (c->res && c->key == key_off && c->w == w && c->h == h && c->fmt == fmt
            && c->cube == cube) {
            if (c->used) { s_wp_tx_hit++; wp85_tex_note(key_off, w, h, fmt, cube, i); return i; }   /* already bound this frame */   /* winport-fix: texture slot LRU */
            { static int nocache = -1;            /* TEX_NOCACHE=1: always re-upload */
              if (nocache < 0) { const char* e = getenv("TEX_NOCACHE");
                                 nocache = e ? atoi(e) : 0; }
              if (nocache) { slot = i; break; } }
            /* Held from an earlier frame: re-upload only if the guest bytes
             * changed. Most of this scene's textures are static, and converting
             * every one of them every frame was ~70% of the frame's CPU time. */
            u32 nb = dxt ? (blkrow * blkrows) : (w * h * bpp);
            u32 cs = TEX_CSUM(vm_base + c->off, nb);
            if (cs == c->csum) { c->used = 1; c->lastf = s_wp_tx_frame; s_wp_tx_csum++; wp85_tex_note(key_off, w, h, fmt, cube, i); return i; }
            s_wp_tx_stale++;
            slot = i; break;                      /* stale: fall through and redo */
        }
        if (!c->used) {   /* winport-fix: texture slot LRU */ /* victim class: 0 empty, 1 same size (resources reused, no re-create), 2 other (destroy+create) */
            int cls_ = !c->res ? 0 : (c->w == w && c->h == h && c->fmt == fmt && c->cube == cube) ? 1 : 2;
            if (freeslot < 0 || cls_ < fcls_ || (cls_ == fcls_ && c->lastf < s_d3d.vp_tex[freeslot].lastf)) { freeslot = i; fcls_ = cls_; }
        }
    }
    /* TEX_OFF_BIAS=1: sample the image one whole level-0 BELOW the bound offset.
     * Measured on the Rubber Ducky demo: at the bind offset every texture reads
     * as all-zero, while [off - w*h*bpp, off) holds the image (3800-4160 of 4298
     * sampled bytes non-zero, for four different textures at three sizes). So
     * the offset the guest programs points PAST level 0 rather than at it. */
    /* TEX_OFF_BIAS: 1 = one level-0 below the bound offset, 2 = one full MIP
     * CHAIN above it. The measured upload-to-bind deltas on the Rubber Ducky
     * demo are 0x2AAB00 for a 2MB level 0 and 0x555580 for a 4MB one -- both
     * exactly the mipmap pyramid total (L0 * 4/3), which is what the guest
     * reserves per texture. */
    { static int bias = -1;
      if (bias < 0) { const char* e = getenv("TEX_OFF_BIAS"); bias = e ? atoi(e) : 0; }
      if (bias == 1) { u32 sz = w * h * bpp; if (off > sz) off -= sz; }
      else if (bias >= 2) {
          u32 chain = 0;
          for (u32 mw = w, mh = h; mw && mh; mw >>= 1, mh >>= 1) {
              chain += mw * mh * bpp;
              if (mw == 1 && mh == 1) break;
          }
          /* bias 3: only shift when the bound offset really is empty, so a
           * texture that IS bound correctly keeps its own data. Applying the
           * chain unconditionally fixes the mis-bound majority but breaks any
           * correctly-bound minority, which shows up as objects going black. */
          int shift = 1;
          if (bias >= 3) {
              u32 nz = 0, sz = w * h * bpp;
              for (u32 i = 0; i < sz && i < 0x20000u; i += 97) if (vm_base[off + i]) nz++;
              shift = (nz == 0);
          }
          if (shift) off += chain;
      } }
    /* TEX_REMAP=1: if the bound offset holds nothing, look the image up by size
     * in the VRAM upload registry (cellGcmSys). For a title whose upload address
     * and SET_TEXTURE_OFFSET disagree this puts the real bytes under the sampler
     * without guessing a delta. */
    { static int remap = -1;
      if (remap < 0) { const char* e = getenv("TEX_REMAP"); remap = e ? atoi(e) : 0; }
      if (remap) {
        u32 nz = 0, sz = w * h * bpp;
        for (u32 i = 0; i < sz && i < 0x20000u; i += 97) if (vm_base[off + i]) nz++;
        if (!nz) {
            extern u32 rsx_find_vram_upload(u32);
            u32 alt = rsx_find_vram_upload(sz);
            if (alt) {
                static int _n = 0;
                if (_n++ < 8) fprintf(stderr, "[TEXREMAP] 0x%08X (%ux%u, %u bytes) -> 0x%08X%c",
                                      off, w, h, sz, alt, 10);
                off = alt;
            }
        } } }
    /* A stale cache entry (slot >= 0) re-uses its own slot; otherwise take a
     * free one. Without this the stale path fell through to freeslot and either
     * duplicated the texture into a second slot or bailed when none was free. */
    s85_tex_gen++;   /* winport v85c-walkfast: a slot may change identity below */
    if (s85_tex_want >= 0) { wp85_vcount(2, 1); s85_tex_want = -1; }   /* verify: memo said hit, scan did not */
    if (slot < 0 && freeslot < 0) { s_wp_tx_fail++; return -1; }      /* out of slots this frame */
    if (slot < 0 && s_d3d.vp_tex[freeslot].res) s_wp_tx_evict++;   /* winport-fix: texture slot LRU */
    /* TEX_SRCDBG=<N>: is the guest memory this slot uploads FROM actually
     * populated? An empty source and a broken sampler both render flat. */
    { static int cap = -1, n = 0;
      if (cap < 0) { const char* e = getenv("TEX_SRCDBG"); cap = e ? atoi(e) : 0; }
      if (cap && n < cap) { n++;
        u32 nz = 0, tot = 0;
        for (u32 i = 0; i < w * h * bpp && i < 0x40000u; i += 61) { tot++; if (vm_base[off + i]) nz++; }
        /* Several of this title's textures are bound at EXACTLY the end address
         * of an upload, so also sample the block immediately BELOW the bind
         * offset: if that holds the image, the bind is one whole texture high. */
        u32 nzb = 0, totb = 0;
        { u32 sz = w * h * bpp; u32 base = (off > sz) ? off - sz : 0;
          for (u32 i = 0; i < sz && i < 0x40000u; i += 61) { totb++; if (vm_base[base + i]) nzb++; } }
        long nearest = 0; u32 found = 0;
        if (!nz) {   /* empty: where IS the data? scan out from the bind offset */
            for (long d = 0x1000; d <= 0x1000000 && !found; d += 0x1000) {
                for (int s = 0; s < 2 && !found; s++) {
                    long a = (long)off + (s ? -d : d);
                    if (a < 0x1000) continue;
                    u32 c = 0;
                    for (u32 i = 0; i < 0x1000u; i += 61) if (vm_base[a + i]) c++;
                    if (c > 8) { nearest = (s ? -d : d); found = c; }
                }
            }
        }
        fprintf(stderr, "[TEXSRC] off=0x%08X %ux%u fmt=0x%02X bpp=%u nonzero=%u/%u"
                        "%s%+ld (page nonzero=%u)\n",
                off, w, h, fmt, bpp, nz, tot,
                nz ? "" : "  nearest data at ", nz ? 0L : nearest, found);
        fprintf(stderr, "[TEXSRC]    block BELOW bind (off-%u): nonzero=%u/%u%c",
                w * h * bpp, nzb, totb, 10);
        /* Walk DOWN in 4KB steps to the first populated page whose predecessor
         * is empty: that is where this texture's data actually begins, and the
         * delta from the bound offset is the rule we are missing. */
        { u32 step = 0x1000, prev_pop = 0; long start_delta = 0;
          for (long d = 0; d >= -(long)(w * h * bpp) * 2; d -= step) {
              u32 a = (u32)((long)off + d); u32 pop = 0;
              if (a < 0x1000) break;
              for (u32 i = 0; i < step; i += 53) if (vm_base[a + i]) pop++;
              if (!pop && prev_pop) { start_delta = d + step; break; }
              prev_pop = pop;
          }
          fprintf(stderr, "[TEXSRC]    data starts at off%+ld (level0=%u bytes)%c",
                  start_delta, w * h * bpp, 10); } } }
    if (slot < 0) slot = freeslot;
    VPTexSlot* t = &s_d3d.vp_tex[slot];
    /* The upload buffer's rows hold the DECODED image, so they are sized from
     * dst_row_bytes -- w * bpp is the source row, which is narrower for any
     * format rsx_texture_decode unpacks. */
    u32 pitch = (tl.dst_row_bytes + 255) & ~255u;
    int fresh = 0;

    /* Also recreate when the CUBE-ness changes: a slot holding a 2D texture of
     * the same dimensions would otherwise be reused with 1 array slice while the
     * cube path copies 6 subresources and writes a 6x upload buffer -- which
     * crashes. */
    if (t->res && (t->w != w || t->h != h || t->fmt != fmt || t->cube != cube)) {
        wp77_idle(5);   /* winport v77-fif: in-flight frames may still sample it */
        t->res->lpVtbl->Release(t->res); t->res = NULL;
        if (t->up) { t->up->lpVtbl->Release(t->up); t->up = NULL; }
    }
    if (!t->res) {
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = {0};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = w; td.Height = h;
        td.DepthOrArraySize = cube ? 6 : 1;   /* cube = 6 array slices */
        td.MipLevels = 1;
        td.Format = dxfmt; td.SampleDesc.Count = 1;
        td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
                s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
                D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                &IID_ID3D12Resource, (void**)&t->res)))
            return -1;
        D3D12_HEAP_PROPERTIES hu = {0}; hu.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd = {0};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = (u64)pitch * (dxt ? blkrows : h) * (cube ? 6 : 1);
        bd.Height = 1; bd.DepthOrArraySize = 1;
        bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
                s_d3d.device, &hu, D3D12_HEAP_FLAG_NONE, &bd,
                D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void**)&t->up))) {
            t->res->lpVtbl->Release(t->res); t->res = NULL; return -1;
        }
        fresh = 1;
    }

    void* mapped = NULL; D3D12_RANGE nr = {0,0};
    if (wp77_on()) { wp77_wait(s77_tex_use[slot], 2); s77_tex_use[slot] = s77_val + 1; }   /* winport v77-fif: its last copy is done */
    if (FAILED(t->up->lpVtbl->Map(t->up, 0, &nr, &mapped)) || !mapped) return -1;
    { static int _tp=0; if (getenv("RTT_DUMP") && _tp++ < 6) {
        const u8* sp = vm_base + off;
        fprintf(stderr, "[TEXUP] off=0x%X fmt=0x%X row0:", off, fmt);
        for (int _b=0;_b<8;_b++) fprintf(stderr, " %02X", sp[_b]);
        fprintf(stderr, "  row240:");
        for (int _b=0;_b<8;_b++) fprintf(stderr, " %02X", sp[240*w*bpp+_b]);
        fprintf(stderr, "  mid:");
        for (int _b=0;_b<8;_b++) fprintf(stderr, " %02X", sp[240*w*bpp+320*bpp+_b]);
        fprintf(stderr, "%s", "\n");
    } }
    /* Swizzled (LN bit 0x20 clear): texels live in Morton/Z-order, not rows.
     * Uploading them as linear rows produced the diagonal-stripe garbage on
     * LBP's loading screen (every texture there is 0x85 swizzled). POT dims
     * only -- the hardware requires that for swizzled textures anyway. */
    int swz = tl.swizzled;
    /* Cube textures store their 6 faces consecutively. Convert each into its own
     * slice of the upload buffer; the conversion below is unchanged and simply
     * runs once per face with off/mapped rebased. */
    const u32 _face_rows  = dxt ? blkrows : h;
    /* The face stride is NOT one mip-0 image: RSX stores each cube face as its
     * own complete mip pyramid, so face f starts a whole pyramid (128-byte
     * aligned) in. Assuming mip-0-sized strides made face 1 land inside face 0's
     * mip chain -- which is exactly what the dumps showed, every face after the
     * first a progressively smaller copy of the first. */
    u32 _face_bytes = dxt ? (blkrow * blkrows) : (w * h * bpp);
    if (cube) {
        u32 total = 0;
        for (u32 lw = w, lh = h, l = 0; l < (mips ? mips : 1u); l++) {
            total += dxt ? (((lw + 3) / 4) * (bpp == 8 ? 16u : 8u) * ((lh + 3) / 4))
                         : (lw * lh * bpp);
            if (lw > 1) lw >>= 1;
            if (lh > 1) lh >>= 1;
        }
        _face_bytes = (total + 127u) & ~127u;
    }
    const u32 _nfaces     = cube ? 6u : 1u;
    const u32 _off0 = off; u8* const _map0 = (u8*)mapped;
    for (u32 _f = 0; _f < _nfaces; _f++) {
    off    = _off0 + _f * _face_bytes;
    if (cube && getenv("CUBEDBG")) { u32 _s=0; const u8* _p = vm_base + off;
        for (u32 _i = 0; _i < _face_bytes; _i += 97) _s = _s*131 + _p[_i];
        fprintf(stderr, "[CUBEFACE] f=%u off=0x%X bytes=%u bpp=%u swz=%d csum=%08X%c",
                _f, off, _face_bytes, bpp, swz, _s, 10); }
    mapped = _map0 + (u64)_f * pitch * _face_rows;
    /* Pixel conversion -- deswizzle, channel order, block copy -- is RSX
     * semantics and lives in rsx_texture_layout.c, so a second backend gets
     * it by calling this rather than porting the loops again. */
    rsx_texture_decode(mapped, pitch, vm_base + off, w, h, &tl,
                       rsx_texture_argb_is_rgba());

    /* The diagnostics below only apply to the linear 8-bit case (Bink video
     * planes); they used to sit inside that branch of the conversion. */
    if (!tl.compressed && tl.bytes_per_texel == 1 && !tl.swizzled) {
        /* MOVIE_FIND=1: when a 640x360 movie plane uploads ZERO, scan a wide
         * guest window for the REAL frame (a 640-wide region with content) so
         * we learn where the video decode actually wrote vs where the texture
         * points -- ends the frame-buffer-address guessing. */
        if (getenv("MOVIE_FIND") && w == 640 && h == 360) {
            extern uint8_t* vm_base;
            const u8* here = vm_base + off;
            u32 hz = 0; for (u32 i=0;i<w*h;i+=137) if (here[i]) { hz=1; break; }
            static int _mf = 0;
            if (!hz && _mf++ < 3) {
                fprintf(stderr, "[movie-find] bound plane ea=0x%X is ZERO; full scan...\n", off);
                /* Scan FULL VRAM + main heap in 0x8000 steps for a 640x360
                 * content block -- NO early cap (the previous found<6 stopped in
                 * the low-VRAM display buffers before ever reaching the movie
                 * region ~0x40E80000). Report the near-plane hits explicitly so
                 * ring-desync (frame present at a DIFFERENT slot) is separable
                 * from decode-never-ran (region entirely zero). */
                struct { u32 lo, hi; const char* tag; } rng[] = {
                    {0x40000000u, 0x41000000u, "VRAM"}, {0x00100000u, 0x10000000u, "MAIN"},
                    {0x48000000u, 0x4A000000u, "BINK"} };  /* Bink working set (handle ~0x4849E760, SPU I/O 0x4847/0x4945) */
                u32 plane_lo = (off > 0x400000u) ? off - 0x400000u : 0;
                u32 plane_hi = off + 0x400000u;
                int found = 0, nearc = 0;
                for (int r = 0; r < (int)(sizeof(rng)/sizeof(rng[0])); r++) {
                    u32 best_a = 0, best_nz = 0; u32 best_span = 0;
                    for (u32 a = rng[r].lo; a + w*h < rng[r].hi; a += 0x8000) {
                        const u8* p = vm_base + a;
                        u32 nz = 0; u8 mn2 = 255, mx2 = 0;
                        for (u32 i = 0; i < w*h; i += 257) { u8 v = p[i]; if (v) nz++; if (v<mn2) mn2=v; if (v>mx2) mx2=v; }
                        u32 span = mx2 - mn2;
                        if (nz > best_nz) { best_nz = nz; best_a = a; best_span = span; }
                        if (nz > 400 && span > 60) {  /* looks like image content */
                            int isnear = (a >= plane_lo && a <= plane_hi);
                            if (isnear) nearc++;
                            if (found < 40)
                                fprintf(stderr, "[movie-find]   CONTENT @0x%08X (%s) nz=%u span=%u%s\n",
                                        a, rng[r].tag, nz, span, isnear ? " <== NEAR bound plane" : "");
                            found++;
                        }
                    }
                    /* Always report the densest block in this range even below the
                     * content threshold -- the MM intro fades in from black, so the
                     * real first frames are low-span and would otherwise be invisible. */
                    fprintf(stderr, "[movie-find]   %s best @0x%08X nz=%u span=%u%s\n",
                            rng[r].tag, best_a, best_nz, best_span,
                            (best_a >= plane_lo && best_a <= plane_hi) ? " <== NEAR bound plane" : "");
                    /* Dump the densest block as a 640x360 grayscale BMP so the
                     * "is this the decoded movie frame" question is answered by
                     * eye. One dump per range per process. */
                    if (best_nz > 400 && best_span > 40) {
                        static int _bd[4] = {0,0,0,0};
                        if (r < 4 && !_bd[r]) { _bd[r] = 1;
                            const u8* sp = vm_base + best_a;
                            char pn[128];
                            snprintf(pn, sizeof(pn), "binkscan_%s_%08X.bmp", rng[r].tag, best_a);
                            FILE* f = fopen(pn, "wb");
                            if (f) {
                                u32 rowb = w*3, rowp = (rowb+3)&~3u, fsz = 54 + rowp*h;
                                u8 hd[54] = {'B','M'};
                                hd[2]=(u8)fsz; hd[3]=(u8)(fsz>>8); hd[4]=(u8)(fsz>>16); hd[5]=(u8)(fsz>>24);
                                hd[10]=54; hd[14]=40;
                                hd[18]=(u8)w; hd[19]=(u8)(w>>8); hd[22]=(u8)h; hd[23]=(u8)(h>>8);
                                hd[26]=1; hd[28]=24;
                                fwrite(hd,1,54,f);
                                for (int y=(int)h-1; y>=0; y--) {
                                    const u8* srow = sp + (u64)y*w;
                                    for (u32 x=0;x<w;x++){ u8 px[3]; px[0]=px[1]=px[2]=srow[x]; fwrite(px,1,3,f); }
                                    { u8 z[3]={0,0,0}; fwrite(z,1,rowp-rowb,f); }
                                }
                                fclose(f);
                                fprintf(stderr, "[movie-find]   dumped %s\n", pn);
                            }
                        }
                    }
                }
                fprintf(stderr, "[movie-find]   total=%d near-plane=%d (window 0x%X..0x%X)\n",
                        found, nearc, plane_lo, plane_hi);
                if (!found) fprintf(stderr, "[movie-find]   no 640x360 content block (>thresh) found anywhere\n");
            }
        }
        /* TEX_SAVE=1: also dump wide B8 uploads (Bink video planes are B8 --
         * the Y plane IS the movie frame in grayscale) + a content stat, so
         * "is the decoder producing pixels" is answerable by looking at a BMP. */
        if (getenv("TEX_SAVE") && w >= 256) {
            static int _bs = 0;
            const u8* sp = vm_base + off;
            u32 nz = 0; u8 mn = 255, mx = 0;
            for (u32 i = 0; i < w * h; i += 17) {
                u8 v = sp[i]; if (v) nz++; if (v < mn) mn = v; if (v > mx) mx = v;
            }
            fprintf(stderr, "[TEXB8] off=0x%X %ux%u nz=%u/%u min=%u max=%u\n",
                    off, w, h, nz, (w * h) / 17, mn, mx);
            if (_bs < 8 && mx > mn) { _bs++;
                char pn[128];
                snprintf(pn, sizeof(pn), "texb8_%08X_%ux%u_%d.bmp", off, w, h, _bs);
                FILE* f = fopen(pn, "wb");
                if (f) {
                    u32 rowb = w * 3, rowp = (rowb + 3) & ~3u;
                    u32 fsz = 54 + rowp * h;
                    u8 hd[54] = {'B','M'};
                    hd[2]=(u8)fsz; hd[3]=(u8)(fsz>>8); hd[4]=(u8)(fsz>>16); hd[5]=(u8)(fsz>>24);
                    hd[10]=54; hd[14]=40;
                    hd[18]=(u8)w; hd[19]=(u8)(w>>8); hd[22]=(u8)h; hd[23]=(u8)(h>>8);
                    hd[26]=1; hd[28]=24;
                    fwrite(hd, 1, 54, f);
                    for (int y = (int)h - 1; y >= 0; y--) {
                        const u8* srow = sp + (u64)y * w;
                        for (u32 x = 0; x < w; x++) {
                            u8 px[3]; px[0]=px[1]=px[2]=srow[x];
                            fwrite(px, 1, 3, f);
                        }
                        { u8 z[3] = {0,0,0}; fwrite(z, 1, rowp - rowb, f); }
                    }
                    fclose(f);
                    fprintf(stderr, "[TEX_SAVE] %s\n", pn);
                }
            }
        }
    }
    /* TEX_SAVE=1: dump the first few converted ARGB uploads as BMPs (rgb +
     * alpha channel separately) -- ground-truth for "is the guest texture
     * wrong or is the sampling wrong" questions (wave's hue palette). */
    if (argb && _f + 1 == _nfaces && getenv("TEX_SAVE")) { static int _ts = 0; if (_ts < 8) { _ts++;
        /* For a cube, write every face: the point of the dump is to check the
         * assumed 6-face layout against the game's cubemap art. */
        for (u32 _face = 0; _face < _nfaces; _face++)
        for (int pass = 0; pass < 2; pass++) {
            char pn[128];
            if (_nfaces > 1)
                snprintf(pn, sizeof(pn), "cube_%08X_%ux%u_f%u_%s.bmp", _off0, w, h,
                         _face, pass ? "a" : "rgb");
            else
                snprintf(pn, sizeof(pn), "tex_%08X_%ux%u_%s.bmp", off, w, h,
                     pass ? "a" : "rgb");
            FILE* f = fopen(pn, "wb");
            if (f) {
                u32 rowb = w * 3, rowp = (rowb + 3) & ~3u;
                u32 fsz = 54 + rowp * h;
                u8 hd[54] = {'B','M'};
                hd[2]=(u8)fsz; hd[3]=(u8)(fsz>>8); hd[4]=(u8)(fsz>>16); hd[5]=(u8)(fsz>>24);
                hd[10]=54; hd[14]=40;
                hd[18]=(u8)w; hd[19]=(u8)(w>>8); hd[22]=(u8)h; hd[23]=(u8)(h>>8);
                hd[26]=1; hd[28]=24;
                fwrite(hd, 1, 54, f);
                for (int y = (int)h - 1; y >= 0; y--) {
                    const u8* srow = _map0 + (u64)_face * pitch * _face_rows
                                     + (u64)y * pitch;
                    for (u32 x = 0; x < w; x++) {
                        u8 px[3];
                        if (pass) { px[0]=px[1]=px[2]=srow[x*4+3]; }
                        else { px[0]=srow[x*4+2]; px[1]=srow[x*4+1]; px[2]=srow[x*4+0]; }
                        fwrite(px, 1, 3, f);
                    }
                    { u8 z[3] = {0,0,0}; fwrite(z, 1, rowp - rowb, f); }
                }
                fclose(f);
                fprintf(stderr, "[TEX_SAVE] %s\n", pn);
            }
        }
    } }
    /* TEX_AVGDBG=1: one line per distinct bound texture offset with its average
     * source colour. duck.tga is overwhelmingly yellow (avg ~243,191,23), so this
     * says whether the duck's image is bound at all -- and if it is, the duck is
     * being drawn somewhere and the question is where, not whether. */
    { static int avgd = -1;
      if (avgd < 0) { const char* e = getenv("TEX_AVGDBG"); avgd = e ? atoi(e) : 0; }
      if (avgd && !dxt && bpp == 4) {
        static u32 seen[64]; static int nseen = 0;
        int known = 0;
        for (int i2 = 0; i2 < nseen; i2++) if (seen[i2] == off) { known = 1; break; }
        if (!known && nseen < 64) {
            seen[nseen++] = off;
            /* Report the source as R,G,B,A -- the layout TEX_RGBA uploads (PSGL
             * writes GL_RGBA/UNSIGNED_INT_8_8_8_8, so bytes land R,G,B,A). Alpha
             * matters as much as colour here: a blended surface whose alpha is a
             * flat 255 renders opaque no matter how correct the blend state is. */
            u64 sr = 0, sg = 0, sb = 0, sa = 0; u32 cnt = 0, amin = 255, amax = 0;
            for (u32 i2 = 0; i2 + 3 < w * h * 4u; i2 += 4 * 37) {
                const u8* q = vm_base + off + i2;
                sr += q[0]; sg += q[1]; sb += q[2]; sa += q[3]; cnt++;
                if (q[3] < amin) amin = q[3];
                if (q[3] > amax) amax = q[3];
            }
            if (cnt) fprintf(stderr, "[TEXAVG] raw=0x%08X %4ux%-4u fmt=0x%02X"
                                     " avgRGBA=(%3u,%3u,%3u,%3u) alpha[%u..%u]%c",
                             key_off, w, h, fmt, (u32)(sr/cnt), (u32)(sg/cnt),
                             (u32)(sb/cnt), (u32)(sa/cnt), amin, amax, 10);
        }
      } }
    /* Content-identify the duck's texture so filters can say "duck". */
    if (!dxt && bpp == 4 && w == 512 && h == 512 && s_duck_raw != key_off) {
        u64 ar = 0, ag = 0, ab = 0; u32 n = 0;
        for (u32 i2 = 0; i2 + 3 < w * h * 4u; i2 += 4 * 211) {
            const u8* q = vm_base + off + i2;
            ar += q[0]; ag += q[1]; ab += q[2]; n++;
        }
        if (n && ar/n > 200 && ag/n > 140 && ab/n < 90) {
            s_duck_off = key_off;
            { static u32 last = 0; if (last != key_off) { last = key_off;
                fprintf(stderr, "[DUCKTEX] resolved=0x%08X identified by content%c", key_off, 10); } }
        }
    }
    /* TEX_HILITE=1: paint the duck's texture pure red. duck.tga is overwhelmingly
     * yellow (avg 243,191,23); with the R,G,B,A source order that is bytes
     * s[0] high, s[1] mid, s[2] low. Flooding it with an unmistakable colour
     * answers "where on screen is the duck drawn" directly, which neither the
     * vertex data nor a pixel count can. Diagnostic only. */
    { static int hil = -1;
      if (hil < 0) { const char* e = getenv("TEX_HILITE"); hil = e ? atoi(e) : 0; }
      if (hil && !dxt && bpp == 4 && w == 512 && h == 512) {
        u64 sr = 0, sg = 0, sb = 0; u32 cnt = 0;
        for (u32 i2 = 0; i2 + 3 < w * h * 4u; i2 += 4 * 37) {
            const u8* q = vm_base + off + i2;
            sr += q[0]; sg += q[1]; sb += q[2]; cnt++;
        }
        if (cnt) {
            u32 ar = (u32)(sr/cnt), ag = (u32)(sg/cnt), ab = (u32)(sb/cnt);
            if (ar > 200 && ag > 140 && ab < 90) {
                for (u32 y = 0; y < h; y++) {
                    u8* drow = (u8*)mapped + (u64)y * pitch;
                    for (u32 x = 0; x < w; x++) {
                        drow[x*4+0] = 0xFF; drow[x*4+1] = 0x00;
                        drow[x*4+2] = 0x00; drow[x*4+3] = 0xFF;
                    }
                }
                { static int _n = 0; if (_n++ < 4)
                    fprintf(stderr, "[TEXHILITE] duck texture off=0x%08X avg=(%u,%u,%u) -> red%c",
                            off, ar, ag, ab, 10); }
            }
        }
      } }
    /* TEX_MARKEMPTY=1: paint any texture whose guest source is entirely zero a
     * flat magenta instead of uploading the empty bytes. An unresolved texture
     * and a legitimately black surface are indistinguishable on screen, so this
     * says WHICH geometry is missing its image -- diagnostic only. */
    { static int mark = -1;
      if (mark < 0) { const char* e = getenv("TEX_MARKEMPTY"); mark = e ? atoi(e) : 0; }
      if (mark && !dxt) {
        u32 nz = 0, sz = w * h * bpp;
        for (u32 i2 = 0; i2 < sz && i2 < 0x40000u; i2 += 61) if (vm_base[off + i2]) nz++;
        if (!nz) {
            for (u32 y = 0; y < h; y++) {
                u8* drow = (u8*)mapped + (u64)y * pitch;
                for (u32 x = 0; x < w; x++) {
                    /* Pure RED. Magenta collided with this title's wall tint,
                     * so the marker was indistinguishable from real geometry. */
                    drow[x*4+0] = 0xFF; drow[x*4+1] = 0x00;
                    drow[x*4+2] = 0x00; drow[x*4+3] = 0xFF;
                }
            }
            { static int _n = 0; if (_n++ < 8)
                fprintf(stderr, "[TEXEMPTY] off=0x%08X %ux%u fmt=0x%02X -> magenta%c",
                        off, w, h, fmt, 10); }
        }
      } }
    }   /* end per-face conversion */
    off = _off0; mapped = _map0;
    t->up->lpVtbl->Unmap(t->up, 0, NULL);

    if (!fresh) {   /* reused resource: PSR -> COPY_DEST first */
        D3D12_RESOURCE_BARRIER b = {0};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = t->res;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    }
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
    dst.pResource = t->res; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = t->up;  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format   = dxfmt;
    src.PlacedFootprint.Footprint.Width    = w;
    src.PlacedFootprint.Footprint.Height   = h;
    src.PlacedFootprint.Footprint.Depth    = 1;
    src.PlacedFootprint.Footprint.RowPitch = pitch;
    for (u32 _f = 0; _f < _nfaces; _f++) {
        dst.SubresourceIndex        = _f;      /* cube face = array slice */
        src.PlacedFootprint.Offset  = (u64)_f * pitch * _face_rows;
        s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);
    }
    {
        D3D12_RESOURCE_BARRIER b = {0};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = t->res;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    }
    /* SRV at heap slot 1+slot */
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {0};
    sv.Format = dxfmt;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    /* RSX B8 replicates the byte into all four channels (dbgfont's FP reads
     * coverage from .w); DXGI R8 defaults to (r,0,0,1), so swizzle (R,R,R,R)
     * = encoded 0x1000 (component 0 in all lanes + always-set bit). ARGB8
     * keeps the identity mapping. */
    sv.Shader4ComponentMapping = (basef == 0x81) ? 0x1000 : D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE sh;
    s_d3d.srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &sh);
    sh.ptr += (u64)(WP_TEX_SRV_BASE + slot) * s_d3d.srv_inc;   /* winport-fix: SRV heap layout */
    if (cube) {
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        sv.TextureCube.MipLevels = 1;
        sv.TextureCube.MostDetailedMip = 0;
        sv.TextureCube.ResourceMinLODClamp = 0.0f;
    }
    s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, t->res, &sv, sh);

    t->off = off; t->key = key_off; t->w = w; t->h = h; t->fmt = fmt;
    t->cube = cube; t->used = 1; t->lastf = s_wp_tx_frame; if (fresh) s_wp_tx_new++;   /* winport-fix: texture slot LRU */
    { u32 nb = dxt ? (blkrow * blkrows) : (w * h * bpp);
      s_wp_tx_bytes += nb;
      t->csum = TEX_CSUM(vm_base + off, nb); }
    #undef TEX_CSUM
    return slot;
}

/* ---------------------------------------------------------------------------
 * Render-to-texture: offscreen RT pool.
 *
 * Draws/clears targeting a non-display surface (SET_SURFACE_COLOR_xOFFSET
 * not registered via cellGcmSetDisplayBuffer) render into a pooled RGBA8
 * texture keyed by the surface's resolved vm offset; a later draw binding a
 * texture at that offset samples the RT directly instead of guest memory.
 * RGBA8 for every RT (even half-float guest surfaces) keeps the existing
 * PSO/RTV formats -- values clamp to [0,1], which demosaic's RGB data fits.
 * -----------------------------------------------------------------------*/

/* winport v44/v45 (G4 Colossus arm): WINPORT_BONE_REPAIR=1 (default 0 = old behaviour).
 * The v43 CAPDRAWS audit showed the Colossus depth pass (vs EB2FCFED, writes Z) and its colour
 * pass (vs C90A85CE, LEQUAL, no Z write) drawn over the SAME vertices with bone palettes that
 * differ in one or two lanes of one bone: lane [1].z and/or [3].x of the bone holds a small integer
 * stored as raw bits (0x00000001, 0x197 ...) instead of the rotation/translation value, so the depth
 * pass writes Z for a different shape than the colour pass draws ("arm overlaps itself wrong").
 * The integers come from the guest command stream (upstream data); this is a narrow workaround.
 * v45 (after the v44 bench: the v44 detector fired on stale, unused bones in ~every record):
 *  - only for a PASS PAIR: this record follows the previous VP record in the next slot, same frame,
 *    same vertex count, same RT, a DIFFERENT vertex program (depth pass + colour pass);
 *  - only bone blocks (c260+4i .. +3, rows .w = 0,0,0,1) whose 64 bytes DIFFER between the two
 *    passes, and only if every differing lane is lane [1].z or [3].x with the signature
 *    (raw bits 1..0xFFFF) on one side -- any other difference = not a pair, nothing is touched;
 *  - per such bone: each signature lane takes the partner's value when that is a normal float;
 *    a [1].z lane still carrying the signature is rebuilt from rows 2 x 0 (orthonormal check);
 *    then the merged bone is written to BOTH passes' constant slots (pass invariance).
 *  Cost: one 64-byte memcmp per bone (49) for pair candidates only. [BONEFIX] log lines. */
/* ---- winport v85c-walkfast: WINPORT_CB85 shadow vertex-constant blocks (cached copies of the upload-heap slots) ---- */
#define WP85_CB_FLOATS (512 * 4 + 8)   /* RSX_MAX_VERTEX_CONSTANTS vec4 + the 8-float viewport epilogue */
static float s85_sh[2][WP85_CB_FLOATS] __attribute__((aligned(64)));
static u32 s85_sh_slot[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu }, s85_sh_par[2];
static unsigned long long s85_sh_frame[2];
static char* s85_sh_wc[2];
static int s85_cur = 0;
static float (*s85_prv)[4] = NULL;     /* shadow of the previous record's slot, when that is slot-1 of this frame/parity */
static char* s85_prv_wc = NULL;        /* ... and its upload-heap address */
static char* wp85_cb_shadow(u32 slot, char* wc)
{
    const int prev = s85_cur; s85_cur ^= 1;
    if (s85_sh_slot[prev] + 1u == slot && s85_sh_frame[prev] == (unsigned long long)s_d3d.frame_count &&
        s85_sh_par[prev] == (u32)s_d3d.vp_parity) { s85_prv = (float (*)[4])s85_sh[prev]; s85_prv_wc = s85_sh_wc[prev]; }
    else { s85_prv = NULL; s85_prv_wc = NULL; }
    s85_sh_slot[s85_cur] = slot; s85_sh_frame[s85_cur] = (unsigned long long)s_d3d.frame_count;
    s85_sh_par[s85_cur] = (u32)s_d3d.vp_parity; s85_sh_wc[s85_cur] = wc;
    return (char*)s85_sh[s85_cur];
}
/* ---- end winport v85c-walkfast shadow ---- */
#define WP_BR_LO 260
#define WP_BR_HI 456
static int wp_bone_repair_on(void) {
    static int v = -1;
    if (v < 0) { const char* e = getenv("WINPORT_BONE_REPAIR"); v = (e && e[0] == '1') ? 1 : 0; }
    return v;
}
static u32 s_wp_br_slot = 0xFFFFFFFFu, s_wp_br_n = 0, s_wp_br_par = 0, s_wp_br_rt = 0;
static int s_wp_br_vs = -1, s_wp_br_mod = 0;
static unsigned long long s_wp_br_frame = ~0ull;
static u32 s_wp_br_calls = 0, s_wp_br_cand = 0, s_wp_br_pairs = 0, s_wp_br_lanes = 0, s_wp_br_rot = 0,
           s_wp_br_left = 0, s_wp_br_rej = 0, s_wp_br_log = 0;
static int wp_br_sig(float f) { u32 b; memcpy(&b, &f, 4); return b >= 1u && b <= 0xFFFFu; }
static int wp_br_norm(float f) { u32 b; memcpy(&b, &f, 4); const u32 e = b & 0x7F800000u;
                                 return e != 0x7F800000u && (e != 0u || (b & 0x7FFFFFFFu) == 0u); }
static int wp_br_shape(const float (*c)[4], int b) {
    return c[b][3] == 0.0f && c[b + 1][3] == 0.0f && c[b + 2][3] == 0.0f && c[b + 3][3] == 1.0f;
}
static void wp_bone_repair(u32 slot, float (*cur)[4], const D3D12DrawRecord* dr) {
    s_wp_br_calls++;
    const int partner = dr && !s_wp_br_mod && s_wp_br_slot + 1u == slot &&
                        s_wp_br_frame == (unsigned long long)s_d3d.frame_count &&
                        s_wp_br_par == (u32)s_d3d.vp_parity && s_wp_br_n == dr->vertex_count &&
                        s_wp_br_rt == dr->rt_off && s_wp_br_vs != dr->vs_idx;
    int modified = 0;
    if (partner) {
        float (*prv)[4] = (float (*)[4])((char*)s_d3d.vp_cb_mapped
            + ((u64)s_d3d.vp_parity * MAX_DRAWS + (slot - 1u)) * VP_CB_STRIDE);
        float (*prv_wc85)[4] = NULL;   /* winport v85c-walkfast: compare against the cached shadow, mirror writes */
        if (s85_prv && s85_prv_wc == (char*)prv && (char*)cur == (char*)s85_sh[s85_cur]) { prv_wc85 = prv; prv = s85_prv; }
        int blk[(WP_BR_HI - WP_BR_LO) / 4], nb = 0, ok = 1;
        for (int b = WP_BR_LO; b + 3 < WP_BR_HI && ok; b += 4) {
            if (memcmp(prv[b], cur[b], 64) == 0) continue;
            if (!wp_br_shape((const float (*)[4])cur, b) || !wp_br_shape((const float (*)[4])prv, b)) { ok = 0; break; }
            for (int i = 0; i < 4 && ok; i++)
                for (int l = 0; l < 4; l++) {
                    const float p = prv[b + i][l], q = cur[b + i][l];
                    if (p == q) continue;
                    if (!((i == 1 && l == 2) || (i == 3 && l == 0)) || !(wp_br_sig(p) || wp_br_sig(q))) { ok = 0; break; }
                }
            if (ok) blk[nb++] = b;
        }
        if (nb) s_wp_br_cand++;
        if (!ok) s_wp_br_rej++;
        else if (nb) {
            for (int k = 0; k < nb; k++) {
                const int b = blk[k];
                static const int li[2][2] = { { 1, 2 }, { 3, 0 } };
                for (int j = 0; j < 2; j++) {
                    float* q = &cur[b + li[j][0]][li[j][1]];
                    const float p = prv[b + li[j][0]][li[j][1]];
                    if (wp_br_sig(*q) && !wp_br_sig(p) && wp_br_norm(p)) {
                        if (s_wp_br_log < 24) { s_wp_br_log++; u32 bits; memcpy(&bits, q, 4);
                            fprintf(stderr, "[BONEFIX] slot=%u vs=%d c%d.%c raw=0x%08X -> %g (from the other pass)\n",
                                    slot, dr->vs_idx, b + li[j][0], "xyzw"[li[j][1]], bits, p); }
                        *q = p; s_wp_br_lanes++;
                    }
                }
                if (wp_br_sig(cur[b + 1][2])) {   /* row1.z on both passes: rebuild from rows 2 x 0 */
                    const float* a = cur[b + 2]; const float* d = cur[b];
                    const float la = sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]), ld = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
                    const float x0 = cur[b + 1][0], y0 = cur[b + 1][1];
                    if (la > 1e-6f && ld > 1e-6f && fabsf(la - ld) <= 0.05f * la &&
                        fabsf((a[0]*d[0] + a[1]*d[1] + a[2]*d[2]) / (la * ld)) <= 0.05f) {
                        const float s = sqrtf(la * ld);
                        const float cx = (a[1]*d[2] - a[2]*d[1]) / s, cy = (a[2]*d[0] - a[0]*d[2]) / s, cz = (a[0]*d[1] - a[1]*d[0]) / s;
                        if (fabsf(cx - x0) <= 0.02f * s && fabsf(cy - y0) <= 0.02f * s) {   /* row1.xy agree */
                            if (s_wp_br_log < 24) { s_wp_br_log++;
                                fprintf(stderr, "[BONEFIX] slot=%u vs=%d c%d.z -> %g (rebuilt, rows 2 x 0)\n", slot, dr->vs_idx, b + 1, cz); }
                            cur[b + 1][2] = cz; s_wp_br_rot++;
                        } else s_wp_br_left++;
                    } else s_wp_br_left++;
                }
                if (wp_br_sig(cur[b + 3][0])) s_wp_br_left++;
                memcpy(prv[b], cur[b], 64); if (prv_wc85) memcpy(prv_wc85[b], cur[b], 64);   /* winport v85c-walkfast */
            }
            s_wp_br_pairs++; modified = 1;
        }
    }
    s_wp_br_slot = slot; s_wp_br_frame = (unsigned long long)s_d3d.frame_count; s_wp_br_par = (u32)s_d3d.vp_parity;
    s_wp_br_n = dr ? dr->vertex_count : 0; s_wp_br_rt = dr ? dr->rt_off : 0; s_wp_br_vs = dr ? dr->vs_idx : -1;
    s_wp_br_mod = modified;
    if ((s_wp_br_calls & (s_wp_br_calls - 1)) == 0 && s_wp_br_calls >= 1024)
        fprintf(stderr, "[BONEFIX] records=%u pair-candidates=%u repaired-pairs=%u lanes-from-partner=%u rebuilt=%u left=%u rejected=%u\n",
                s_wp_br_calls, s_wp_br_cand, s_wp_br_pairs, s_wp_br_lanes, s_wp_br_rot, s_wp_br_left, s_wp_br_rej);
}

/* Snapshot the VP constant bank + viewport epilogue for one draw into its
 * vp_cb slot. Runs at record time so every draw keeps the constants that
 * were live when the guest issued it. */
static void vp_record_cb_wk85(u32 slot, int vs_idx, const D3D12DrawRecord* dr);
static void vp_record_cb(u32 slot, int vs_idx, const D3D12DrawRecord* dr) { const unsigned long long t_ = WK85_T0(); vp_record_cb_wk85(slot, vs_idx, dr); WK85_ADD(WK85_cb, t_); }   /* winport v85a-walkprof */
static void vp_record_cb_wk85(u32 slot, int vs_idx, const D3D12DrawRecord* dr)
{
    if (dr) { ((D3D12DrawRecord*)dr)->is_blit = 0;   /* winport-fix: shader caches */
              ((D3D12DrawRecord*)dr)->wp_merge_n = 0; ((D3D12DrawRecord*)dr)->wp_merge_cbchg = 0;   /* winport v38 */
              winport_fp_snapshot((D3D12DrawRecord*)dr); }
    const rsx_state* st = s_d3d.current_rsx_state;
    /* winport v38: this record snapshots the vertex constants below; from here on the engine's
     * vertex_constants_dirty flag means "a constant was uploaded after this snapshot" (see wp_merge_ok). */
    if (st) ((rsx_state*)st)->vertex_constants_dirty = 0;
    if (dr) wp_stencil_pack(st, &((D3D12DrawRecord*)dr)->wp_stk0, &((D3D12DrawRecord*)dr)->wp_stk1,
                            &((D3D12DrawRecord*)dr)->wp_sref);   /* winport v39 */
    if (dr) { D3D12DrawRecord* w_ = (D3D12DrawRecord*)dr;   /* winport v41 */
              w_->wp_vzero = s_wp_vchk_zero; w_->wp_vcmp = s_wp_vchk_cmp; w_->wp_vmaxi = s_wp_vchk_maxvi;
              s_wp_vchk_zero = s_wp_vchk_cmp = s_wp_vchk_maxvi = 0; }
    if (!s_d3d.vp_cb_mapped || !st || slot >= MAX_DRAWS) return;
    /* FP texcoord scale (b1): 1/size for UNnormalized textures (fmt bit
     * 0x40 -- wave samples everything in texel space), 1.0 otherwise. */
    if (s_d3d.vp_fpcb_mapped && !(wp85_cb_on() && dr && wp_fp_buffered())) {   /* winport v85c-walkfast: dead when the buffered block follows */
        float* ts = (float*)((char*)s_d3d.vp_fpcb_mapped
            + ((u64)s_d3d.vp_parity * MAX_DRAWS + slot) * VP_FPCB_STRIDE);
        for (int _u = 0; _u < 4; _u++) {
            float sx = 1.0f, sy = 1.0f;
            if (dr && dr->tex[_u].set && (dr->tex[_u].fmt & 0x40) &&
                dr->tex[_u].w && dr->tex[_u].h) {
                sx = 1.0f / (float)dr->tex[_u].w;
                sy = 1.0f / (float)dr->tex[_u].h;
            }
            ts[_u*4+0] = sx; ts[_u*4+1] = sy; ts[_u*4+2] = 0.0f; ts[_u*4+3] = 0.0f;
        }
        /* rsx_alphatest (b1[4]): x=enable, y=ref/255, z=func-0x200. D3D12
         * has no fixed alpha test; guest FPs discard on it dynamically
         * (wave's colour wheel is a disc via alpha test -- without it the
         * palette drew as an opaque square). */
        if (dr && !getenv("NO_ALPHATEST")) {
            { static int _at = 0;
              if (((dr->alpha_ctl >> 16) & 1u) && _at++ < 4)
                  printf("[ALPHATEST] enable func=%u ref=%u\n",
                         (dr->alpha_ctl >> 8) & 0xFFu, dr->alpha_ctl & 0xFFu); }
            ts[16] = (float)((dr->alpha_ctl >> 16) & 1u);
            ts[17] = (float)(dr->alpha_ctl & 0xFFu) / 255.0f;
            ts[18] = (float)((dr->alpha_ctl >> 8) & 0x07u);
            ts[19] = 0.0f;
        } else {
            ts[16] = 0.0f; ts[17] = 0.0f; ts[18] = 7.0f; ts[19] = 0.0f;
        }

        /* fp_k[]: the program's inline fragment constants, re-read from the
         * guest ucode every draw. The decompiler emits fp_k[i] lookups instead
         * of baking these in as literals, so the compiled shader no longer
         * changes when the title re-patches them -- which is what forced the
         * pipeline cache to key on a hash of the ucode bytes. */
        if (dr && dr->fp_addr) {
            extern uint8_t* vm_base;
            extern u32 cellGcmResolveLocated(int, u32);
            u32 foff = cellGcmResolveLocated((dr->fp_addr & 0x3u) == 1,
                                             dr->fp_addr & ~0x3u);
            if (vm_base && foff != 0xFFFFFFFFu)
                rsx_fp_extract_consts(vm_base + foff, 4096, &ts[20], 64);   /* fp_k[64] */
            /* FPK_DBG=1: the fragment constants as the shader will see them.
             * A program whose only output is fp_k[0].xxxx renders black if
             * those constants never arrive, which looks identical from the
             * outside to geometry that never rasterised. */
            { static int fk = -1; if (fk < 0) fk = getenv("FPK_DBG") ? 1 : 0;
              static int n = 0;
              if (fk && n < 12 && dr->fp_addr != 0x00EC2B02u) { n++;
                  fprintf(stderr, "[FPK] tex0=0x%08X fp=0x%08X k0=(%g %g %g %g)"
                                  " k1=(%g %g %g %g)%c",
                          dr->tex[0].raw, dr->fp_addr,
                          ts[20], ts[21], ts[22], ts[23],
                          ts[24], ts[25], ts[26], ts[27], 10);
                  const u32 raw = dr->fp_addr & ~0x3u;
                  const u32 ea_main  = cellGcmResolveLocated(0, raw);
                  const u32 ea_local = cellGcmResolveLocated(1, raw);
                  fprintf(stderr, "[FPK]   ucode main@0x%08X:", ea_main);
                  for (int b = 0; b < 48; b++) fprintf(stderr, "%s%02X", (b%16)?" ":"  |", vm_base[ea_main + b]);
                  fputc(10, stderr); }
              /* distinct fragment programs seen -- if every draw shares one,
               * SET_SHADER_PROGRAM is not being tracked per draw. */
              if (fk) { static u32 seen[16]; static int ns = 0; int f = 0;
                  for (int q = 0; q < ns; q++) if (seen[q] == dr->fp_addr) f = 1;
                  if (!f && ns < 16) { seen[ns++] = dr->fp_addr;
                      fprintf(stderr, "[FPK] distinct fp #%d = 0x%08X%c", ns, dr->fp_addr, 10); } } }
        }
    }
    if (s_d3d.vp_fpcb_mapped && dr && wp_fp_buffered()) {   /* winport-fix: buffered FP constants */
        /* b1 = fp_constants[N] (the program's inline constants, as the decompiler
         * numbers them), then fp_alpha.x = alpha-test reference */
        float* const kwc85_ = (float*)((char*)s_d3d.vp_fpcb_mapped
            + ((u64)s_d3d.vp_parity * MAX_DRAWS + slot) * VP_FPCB_STRIDE);
        static float kloc85_[VP_FPCB_STRIDE / 4] __attribute__((aligned(64)));   /* winport v85c-walkfast */
        float* kb_ = wp85_cb_on() ? kloc85_ : kwc85_;
        memset(kb_, 0, VP_FPCB_STRIDE);
        int nk_ = 0;
        if (dr->fp_addr) {
            extern uint8_t* vm_base;
            extern u32 cellGcmResolveLocated(int, u32);
            static rsx_fp_constant_block blk_;
            u32 fo_ = cellGcmResolveLocated((dr->fp_addr & 0x3u) == 1, dr->fp_addr & ~0x3u);
            if (vm_base && fo_ != 0xFFFFFFFFu) {
                nk_ = rsx_fp_collect_constants(vm_base + fo_, 4096, &blk_);
                if (nk_ < 0) nk_ = 0;
                if (nk_ > (int)(VP_FPCB_STRIDE / 16) - 3) nk_ = (int)(VP_FPCB_STRIDE / 16) - 3;   /* winport-fix: v22 scene depth */
                memcpy(kb_, blk_.values, (size_t)nk_ * 16u);
            }
        }
        kb_[(nk_ > 0 ? nk_ : 1) * 4] = (float)(dr->alpha_ctl & 0xFFu) / 255.0f;
        ((D3D12DrawRecord*)dr)->fp_kn = (u32)(nk_ > 0 ? nk_ : 1);   /* winport-fix: v22 scene depth */
        if (kb_ != kwc85_) memcpy(kwc85_, kb_, VP_FPCB_STRIDE);   /* winport v85c-walkfast */
    }
    char* const cwc85_ = (char*)s_d3d.vp_cb_mapped
        + ((u64)s_d3d.vp_parity * MAX_DRAWS + slot) * VP_CB_STRIDE;
    char* dst = (wp85_cb_on() && RSX_MAX_VERTEX_CONSTANTS == 512) ? wp85_cb_shadow(slot, cwc85_) : cwc85_;   /* winport v85c-walkfast */
    memcpy(dst, st->vertex_constants, RSX_MAX_VERTEX_CONSTANTS * 16);
    if (wp_bone_repair_on()) wp_bone_repair(slot, (float (*)[4])dst, dr);   /* winport v44/v45 (G4) */
    /* VP_MVP=<N>: the constant bank as the shader will see it, for the first N
     * draws. A snapshot taken from a stale rsx_state looks exactly like a broken
     * vertex program from the outside -- both give zero fragments. */
    { static int cap = -1, seen = 0;
      if (cap < 0) { const char* e = getenv("VP_MVP"); cap = e ? atoi(e) : 0; }
      if (cap && seen < cap) { seen++;
        const float* c = (const float*)dst;
        int last_nz = -1;
        for (int i = 0; i < RSX_MAX_VERTEX_CONSTANTS * 4; i++)
            if (c[i] > 1e-9f || c[i] < -1e-9f) last_nz = i / 4;
        fprintf(stderr, "[VPMVP] slot=%u lastNZ=c%d  c260=(%g %g %g %g) c467=(%g %g %g %g)\n",
                slot, last_nz,
                c[260*4+0], c[260*4+1], c[260*4+2], c[260*4+3],
                c[467*4+0], c[467*4+1], c[467*4+2], c[467*4+3]); } }
    /* RSX_DBG_CB=<hex tex0 offset>: the constants as they land in the per-draw
     * constant buffer for that texture's draws -- i.e. exactly what the shader
     * samples, not what the CPU-side rsx_state holds. Those two agreeing is an
     * assumption worth checking directly when a draw transforms to nothing. */
    const float* vpx_dbg = (const float*)(dst + RSX_MAX_VERTEX_CONSTANTS * 16);
    { static const char* dc = (const char*)1; static u32 wantc = 0; static int n = 0;
      if (dc == (const char*)1) { dc = getenv("RSX_DBG_CB");
                                  wantc = dc ? (u32)strtoul(dc, NULL, 16) : 0; }
      if (wantc && dr && dr->tex[0].raw == wantc && n < 4) { n++;
          const float* c = (const float*)dst;
          fprintf(stderr, "[DUCKCB] slot=%u vs_idx=%d posscale=(%g %g %g) posoffset=(%g %g %g)%c",
                  slot, vs_idx, vpx_dbg[0], vpx_dbg[1], vpx_dbg[2],
                  vpx_dbg[4], vpx_dbg[5], vpx_dbg[6], 10);
          for (int r = 256; r <= 259; r++)
              fprintf(stderr, "[DUCKCB]   c%d=(%g %g %g %g)%c", r,
                      c[r*4+0], c[r*4+1], c[r*4+2], c[r*4+3], 10);
      } }

    /* Viewport epilogue (see the render_frame notes this logic came from):
     * x/y identity, z lane remaps GL clip z when the guest programs one. */
    float* vpx = (float*)(dst + RSX_MAX_VERTEX_CONSTANTS * 16);
    const float* vs_ = st->viewport_scale;
    const float* vo_ = st->viewport_offset;
    vpx[0] = vpx[1] = vpx[3] = 1.0f;
    vpx[4] = vpx[5] = vpx[7] = 0.0f;
    if (vs_[2] != 0.0f) { vpx[2] = vs_[2]; vpx[6] = vo_[2]; }
    else                { vpx[2] = 1.0f;   vpx[6] = 0.0f;   }
    { static float hp_ = -999.0f; /* winport-fix: half-pixel */
      if (hp_ < -900.0f) { const char* e_ = getenv("WINPORT_HALFPX"); hp_ = e_ ? (float)atof(e_) : 0.5f; }
      if (hp_ != 0.0f && dr && dr->vp_w >= 2 && dr->vp_h >= 2) {
          vpx[4] += hp_ * 2.0f / (float)dr->vp_w;
          vpx[5] -= hp_ * 2.0f / (float)dr->vp_h; } }
    /* DBG_ZOOM=<k> with DBG_CENTER="cx,cy": a debug camera applied in CLIP space,
     * not a framebuffer crop -- the geometry is re-rasterized at full resolution.
     * The shader epilogue already computes
     *     pos.xyz = _p.xyz * vp_posscale + _p.w * vp_posoffset
     * so recentring NDC point c and magnifying by k is exactly
     *     pos.xy = _p.xy * k + _p.w * (-c * k).
     * Rubber Ducky frames its ducks near the bottom edge of the viewport and its
     * camera only moves from the analog sticks, so without this there is no way
     * to look at the subject of the demo. It changes the view, nothing else:
     * same draws, same transform, same textures. */
    { static int z = -1; static float k = 1.0f, cx = 0.0f, cy = 0.0f;
      if (z < 0) { const char* e = getenv("DBG_ZOOM");
                   k = e ? (float)atof(e) : 1.0f;
                   z = (e && k > 0.0f) ? 1 : 0;
                   const char* ce = getenv("DBG_CENTER");
                   if (ce) { double a = 0, b = 0; sscanf(ce, "%lf,%lf", &a, &b);
                             cx = (float)a; cy = (float)b; } }
      if (z) { float ux = cx, uy = cy;
               static int lock = -1;
               if (lock < 0) { const char* le = getenv("DBG_LOCK"); lock = le ? atoi(le) : 0; }
               if (lock && s_lock_valid) { ux = s_lock_x; uy = s_lock_y; }
               vpx[0] = k; vpx[1] = k; vpx[4] = -ux * k; vpx[5] = -uy * k; } }

    /* Garbage-projection fallback (vkcube; see the original comment). */
    int uses_c03 = (vs_idx >= 0 && vs_idx < s_d3d.vp_vs_n)
                       ? s_d3d.vp_vs[vs_idx].uses_c03 : s_d3d.vp_uses_c03;
    float* c = (float*)dst;
    float c00 = c[0];
    int garbage = !(c00 > 0.0f && c00 < 8.0f);
    static int nofix85_ = -1, fix85_ = -1;   /* winport v85c-walkfast: read once under CB85 */
    if (nofix85_ < 0 || !wp85_cb_on()) { nofix85_ = getenv("VP_NOFIXPROJ") ? 1 : 0; fix85_ = getenv("VP_FIXPROJ") ? 1 : 0; }
    if (nofix85_) garbage = 0;
    if ((garbage && uses_c03) || fix85_) {
        { static int _fb = 0; if (_fb++ < 6)
            printf("[VPFB] fallback proj on draw slot %u (c00=%g vs_idx=%d)\n",
                   slot, c00, vs_idx); }
        for (int _i = 0; _i < 16; _i++) c[_i] = 0.0f;
        c[0]=1.358f; c[5]=2.414f; c[11]=1.0f;
        c[10]=1.0f/99.0f; c[14]=-1.0f/99.0f;
        vpx[2] = 1.0f; vpx[6] = 0.0f;
    }
    if (dst != cwc85_) memcpy(cwc85_, dst, RSX_MAX_VERTEX_CONSTANTS * 16 + 8 * sizeof(float));   /* winport v85c-walkfast */
}

/* Which offscreen surface (if any) does the current RSX state render to?
 * Returns 0 for a display buffer (backbuffer), else the surface's RAW RSX
 * offset. RTs are keyed by raw offset -- a texture bound at the same raw
 * offset is the same buffer (surface and texture registers share the offset
 * space; location bits differ but one title doesn't alias local vs main at
 * one offset), which sidesteps guessing the surface's context DMA location. */
static int wp92_dispsize_skip(void);   /* winport v92b-dispsize */
static u32 current_rt_off(u32* out_w, u32* out_h, u32 out_mrt[3])
{
    extern int cellGcmOffsetIsDisplay(u32 offset);
    const rsx_state* st = s_d3d.current_rsx_state;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (out_mrt) out_mrt[0] = out_mrt[1] = out_mrt[2] = 0;
    if (!st) return 0;
    /* SET_SURFACE_COLOR_TARGET: 1 = A, 2 = B, 0x13 = MRT1 (A+B),
     * 0x17 = MRT2 (A+B+C), 0x1F = MRT3 (A+B+C+D). */
    int sel = (st->color_target == 2) ? 1 : 0;
    u32 raw = st->surface_color_offset[sel];
    if (out_mrt) {
        if (st->color_target >= 0x13) out_mrt[0] = st->surface_color_offset[1];
        if (st->color_target >= 0x17) out_mrt[1] = st->surface_color_offset[2];
        if (st->color_target >= 0x1F) out_mrt[2] = st->surface_color_offset[3];
    }
    { static int _rs = -1; if (_rs < 0) _rs = getenv("RT_SEQDBG") ? 1 : 0;
      static u32 _last = 0; static int _n2 = 0;
      if (_rs && st->surface_color_offset[0] != _last && _n2 < 400000) {
          _last = st->surface_color_offset[0]; _n2++;
          fprintf(stderr, "[RTSEQ] #%d -> 0x%X%c", _n2, _last, 10); } }
    { static int _ro = -1; if (_ro < 0) _ro = getenv("RT_OFFDBG") ? 1 : 0;
      static u32 _seen[32]; static int _ns = 0;
      if (_ro) { u32 _k = st->surface_color_offset[0] ^ (st->color_target << 24)
                        ^ (st->surface_clip_w << 8) ^ st->surface_format;
        int _f = 0; for (int _i = 0; _i < _ns; _i++) if (_seen[_i] == _k) _f = 1;
        if (!_f && _ns < 32) { _seen[_ns++] = _k;
        fprintf(stderr, "[RTOFF] fmt=0x%X tgt=0x%X off[0]=0x%X off[1]=0x%X zeta=0x%X clip=%ux%u disp=%d -> 0x%X\n",
                st->surface_format, st->color_target, st->surface_color_offset[0], st->surface_color_offset[1],
                st->surface_zeta_offset, st->surface_clip_w, st->surface_clip_h,
                cellGcmOffsetIsDisplay(raw), cellGcmOffsetIsDisplay(raw) ? 0 : raw); } } }
    if (cellGcmOffsetIsDisplay(raw)) return 0;
    /* RT_DISPLAY_BY_SIZE=1: also treat a single-target surface whose clip
     * exactly matches the display resolution as the backbuffer.
     *
     * cellGcmSetDisplayBuffer only registers the buffers the FLIP may point at;
     * a guest is free to render into a different surface of the same size and
     * flip to it later (PSGL does). Matching offsets alone then classifies every
     * draw as offscreen, has_display stays 0, and render_frame() -- which is
     * what draws ALL recorded geometry -- is never called. The backbuffer shows
     * only the clear, which looks exactly like "nothing rasterizes". */
    { static int _bs = -1; if (_bs < 0) _bs = getenv("RT_DISPLAY_BY_SIZE") ? 1 : 0;
      if (_bs && st->color_target < 0x13 && !wp92_dispsize_skip() &&   /* winport v92b-dispsize */
          st->surface_clip_w == s_d3d.width && st->surface_clip_h == s_d3d.height)
          return 0; }
    /* Surface clip dims when sane; else the window size. Any size works --
     * passes draw normalized full-surface quads -- this only picks resolution. */
    u32 w = st->surface_clip_w, h = st->surface_clip_h;
    if (w < 16 || w > 2048 || h < 16 || h > 2048) { w = 0; h = 0; }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return raw;
}

/* RSX surface colour format (SET_SURFACE_FORMAT bits [4:0]) -> DXGI. Float
 * targets matter: wave's water height maps store SIGNED values (F_W16..),
 * demosaic's differential planes likewise -- RGBA8 clamps them to zero. */
static DXGI_FORMAT rsx_surface_dxgi(u32 fmt)
{
    switch (fmt & 0x1F) {
    case 0x0B: return DXGI_FORMAT_R16G16B16A16_FLOAT; /* F_W16Z16Y16X16 */
    case 0x0C: return DXGI_FORMAT_R32G32B32A32_FLOAT; /* F_W32Z32Y32X32 */
    case 0x0D: return DXGI_FORMAT_R32_FLOAT;          /* F_X32          */
    default:   return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

static int off_rt_find(u32 off)
{
    if (!off) return -1;
    for (int i = 0; i < MAX_OFF_RTS; i++)
        if (s_d3d.off_rt[i].res && s_d3d.off_rt[i].off == off) return i;
    return -1;
}

/* Ensure an RT resource exists for this surface; (re)creates the RTV at
 * rt_rtv_heap[i] and the SRV at srv_heap[RT_SRV_BASE+i]. */
static int off_rt_get(u32 off, u32 w, u32 h, u32 rsx_fmt)
{
    if (!off || !s_d3d.rt_rtv_heap || !s_d3d.srv_heap) return -1;
    if (!w) w = s_d3d.width;
    if (!h) h = s_d3d.height;
    DXGI_FORMAT want_fmt = rsx_surface_dxgi(rsx_fmt);
    /* RT_FORCE_RGBA8=1: allocate every offscreen target as R8G8B8A8 rather than
     * its native format. A title whose final composite reads a half-float
     * surface cannot be shown with a plain CopyTextureRegion, and giving it an
     * 8-bit target costs HDR precision but makes the composite a same-format
     * copy. A stopgap for looking at the frame, not a rendering fix. */
    { static int f8 = -1;
      if (f8 < 0) { const char* e = getenv("RT_FORCE_RGBA8"); f8 = e ? atoi(e) : 0; }
      if (f8) want_fmt = DXGI_FORMAT_R8G8B8A8_UNORM; }
    int slot = off_rt_find(off);
    if (slot >= 0) {
        OffRT* r = &s_d3d.off_rt[slot];
        if (r->w == w && r->h == h && r->dxgi == (u32)want_fmt) { r->used = 1; return slot; }
        /* The frame in flight still references this resource -- releasing it here
         * is "was deleted prior to closing the command list" (D3D12 message 921),
         * and every draw recorded against it silently lands nowhere while the
         * replacement reads back empty. Let the GPU finish first. Recreations are
         * rare (a surface changing size), so the stall costs nothing. */
        s_wp_rt_recreates++;   /* winport-fix: RT diagnostics */
        { static int n_ = 0; if (n_++ < 200)
            printf("[RTRECREATE] off=0x%X %ux%u dxgi=%u -> %ux%u dxgi=%u\n",
                   off, r->w, r->h, r->dxgi, w, h, (u32)want_fmt); }
        wait_for_gpu();
        r->res->lpVtbl->Release(r->res); r->res = NULL;   /* dims/format changed */
        if (r->up) { r->up->lpVtbl->Release(r->up); r->up = NULL; }
    } else {
        for (int i = 0; i < MAX_OFF_RTS; i++)
            if (!s_d3d.off_rt[i].res) { slot = i; break; }
        if (slot < 0) {   /* evict an entry not used this frame */
            for (int i = 0; i < MAX_OFF_RTS; i++)
                if (!s_d3d.off_rt[i].used) { slot = i; break; }
            if (slot < 0) { static int n_ = 0; if (n_++ < 50)   /* winport-fix: RT diagnostics */
                printf("[RTFULL] no free offscreen RT for off=0x%X %ux%u\n", off, w, h);
                return -1; }
            s_wp_rt_evicts++;
            { static int n_ = 0; if (n_++ < 100)
                printf("[RTEVICT] slot %d off=0x%X %ux%u for off=0x%X %ux%u\n", slot,
                       s_d3d.off_rt[slot].off, s_d3d.off_rt[slot].w, s_d3d.off_rt[slot].h, off, w, h); }
            wait_for_gpu();   /* same hazard as above: the frame may still use it */
            s_d3d.off_rt[slot].res->lpVtbl->Release(s_d3d.off_rt[slot].res);
            s_d3d.off_rt[slot].res = NULL;
            if (s_d3d.off_rt[slot].up) {
                s_d3d.off_rt[slot].up->lpVtbl->Release(s_d3d.off_rt[slot].up);
                s_d3d.off_rt[slot].up = NULL;
            }
        }
    }
    OffRT* r = &s_d3d.off_rt[slot];
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = {0};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = wp68_px(w); td.Height = wp68_px(h); td.DepthOrArraySize = 1; td.MipLevels = 1;   /* winport v68 */
    td.Format = want_fmt; td.SampleDesc.Count = 1;
    td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv = {0};
    cv.Format = td.Format;
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_COPY_DEST, &cv,
            &IID_ID3D12Resource, (void**)&r->res)))
        return -1;
    r->off = off; r->w = w; r->h = h; r->dxgi = (u32)want_fmt;
    if (s_wp68_s != 1.0f) {   /* winport v68 */
        s_wp68_n_rt++;
        if (td.Width * td.Height > s_wp68_maxw * s_wp68_maxh) { s_wp68_maxw = (u32)td.Width; s_wp68_maxh = td.Height; }
        { static int n68 = 0; if (n68++ < 32) printf("[RES68] RT off=0x%X guest %ux%u -> %ux%u dxgi=%u\n", off, w, h, (u32)td.Width, td.Height, (u32)want_fmt); }
    }
    r->st = D3D12_RESOURCE_STATE_COPY_DEST;
    r->used = 1;

    /* Populate the new RT from guest memory: titles CPU-initialise their
     * render-target buffers (wave fills the height fields with texels of
     * (0,0,0, 1.0f) -- the .w is 'inverse of mass'; from an all-zero GPU
     * resource the water simulation can never boot). Guest data is
     * big-endian and tightly packed. */
    {
        extern uint8_t* vm_base;
        extern u32 cellGcmResolveOffset(u32);
        u32 bpp = (want_fmt == DXGI_FORMAT_R16G16B16A16_FLOAT) ? 8u :
                  (want_fmt == DXGI_FORMAT_R32G32B32A32_FLOAT) ? 16u :
                  (want_fmt == DXGI_FORMAT_R32_FLOAT) ? 4u : 4u;
        u32 pitch = (w * bpp + 255) & ~255u;
        const u32 pw68 = wp68_px(w), ph68 = wp68_px(h), pp68 = (pw68 * bpp + 255) & ~255u;   /* winport v68 */
        extern u32 cellGcmResolveLocated(int local, u32 offset);   /* winport-fix: safe RT seeding */
        extern int ppu_guest_range_committed(uint32_t addr, uint32_t n);
        static int seed_ = -1;
        if (seed_ < 0) { const char* e = getenv("RT_SEED"); seed_ = (e && *e == '0') ? 0 : 1; }
        u32 src = cellGcmResolveLocated(1, off);
        if (vm_base && seed_ && src != 0xFFFFFFFFu &&
            ppu_guest_range_committed(src, w * h * bpp)) {
            D3D12_HEAP_PROPERTIES hu = {0}; hu.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC bd = {0};
            bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bd.Width = (u64)pp68 * ph68; bd.Height = 1; bd.DepthOrArraySize = 1;   /* winport v68 */
            bd.MipLevels = 1; bd.SampleDesc.Count = 1;
            bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (SUCCEEDED(s_d3d.device->lpVtbl->CreateCommittedResource(
                    s_d3d.device, &hu, D3D12_HEAP_FLAG_NONE, &bd,
                    D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                    &IID_ID3D12Resource, (void**)&r->up))) {
                void* mp = NULL; D3D12_RANGE nr = {0,0};
                if (SUCCEEDED(r->up->lpVtbl->Map(r->up, 0, &nr, &mp)) && mp) {
                    void* const mp68 = mp;   /* winport v68: convert at 1x into a temp, then upscale */
                    if (pw68 != w || ph68 != h) { mp = malloc((size_t)pitch * h); if (!mp) mp = mp68; }
                    const u8* sp = vm_base + src;
                    for (u32 y = 0; y < h; y++) {
                        const u8* srow = sp + (u64)y * w * bpp;
                        u8* drow = (u8*)mp + (u64)y * pitch;
                        if (want_fmt == DXGI_FORMAT_R8G8B8A8_UNORM) {
                            /* guest A8R8G8B8 (bytes A,R,G,B) -> R,G,B,A */
                            for (u32 x = 0; x < w; x++) {
                                drow[x*4+0] = srow[x*4+1];
                                drow[x*4+1] = srow[x*4+2];
                                drow[x*4+2] = srow[x*4+3];
                                drow[x*4+3] = srow[x*4+0];
                            }
                        } else if (want_fmt == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                            for (u32 x = 0; x < w * 4; x++) {   /* u16 halves, BE */
                                drow[x*2+0] = srow[x*2+1];
                                drow[x*2+1] = srow[x*2+0];
                            }
                        } else {                                 /* u32 floats, BE */
                            u32 nw = (w * bpp) / 4;
                            for (u32 x = 0; x < nw; x++) {
                                drow[x*4+0] = srow[x*4+3];
                                drow[x*4+1] = srow[x*4+2];
                                drow[x*4+2] = srow[x*4+1];
                                drow[x*4+3] = srow[x*4+0];
                            }
                        }
                    }
                    if (mp != mp68) {   /* winport v68: nearest upscale of the guest seed */
                        for (u32 y = 0; y < ph68; y++) {
                            const u8* s68 = (const u8*)mp + (u64)((u64)y * h / ph68) * pitch;
                            u8* d68 = (u8*)mp68 + (u64)y * pp68;
                            for (u32 x = 0; x < pw68; x++)
                                memcpy(d68 + (u64)x * bpp, s68 + (u64)((u64)x * w / pw68) * bpp, bpp);
                        }
                        free(mp); mp = mp68; s_wp68_n_seed++;
                    } else if (pw68 != w || ph68 != h) memset(mp68, 0, (size_t)pp68 * ph68);   /* malloc failed: zero seed */
                    r->up->lpVtbl->Unmap(r->up, 0, NULL);
                    D3D12_TEXTURE_COPY_LOCATION cdst = {0}, csrc = {0};
                    cdst.pResource = r->res;
                    cdst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    csrc.pResource = r->up;
                    csrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    csrc.PlacedFootprint.Footprint.Format   = want_fmt;
                    csrc.PlacedFootprint.Footprint.Width    = pw68;   /* winport v68 */
                    csrc.PlacedFootprint.Footprint.Height   = ph68;
                    csrc.PlacedFootprint.Footprint.Depth    = 1;
                    csrc.PlacedFootprint.Footprint.RowPitch = pp68;
                    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &cdst, 0, 0, 0, &csrc, NULL);
                }
            }
        }
        {
            D3D12_RESOURCE_BARRIER b = {0};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource   = r->res;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
            r->st = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rh;
    s_d3d.rt_rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.rt_rtv_heap, &rh);
    rh.ptr += (u64)slot * s_d3d.device->lpVtbl->GetDescriptorHandleIncrementSize(
        s_d3d.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    s_d3d.device->lpVtbl->CreateRenderTargetView(s_d3d.device, r->res, NULL, rh);

    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {0};
    sv.Format = td.Format;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE sh;
    s_d3d.srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &sh);
    sh.ptr += (u64)(RT_SRV_BASE + slot) * s_d3d.srv_inc;
    s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, r->res, &sv, sh);

    static int _log = 0;
    if (_log++ < 64)   /* winport-fix: RT diagnostics */
        printf("[D3D12] offscreen RT %d: off=0x%X %ux%u (render-to-texture)\n",
               slot, off, w, h);
    return slot;
}

static D3D12_CPU_DESCRIPTOR_HANDLE off_rt_rtv(int slot)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rh;
    s_d3d.rt_rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.rt_rtv_heap, &rh);
    rh.ptr += (u64)slot * s_d3d.device->lpVtbl->GetDescriptorHandleIncrementSize(
        s_d3d.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    return rh;
}

/* Write a texture SRV (or a null SRV when res == NULL) at an absolute SRV
 * heap slot. Used to fill per-draw t0-t3 descriptor windows. */
static void srv_write_ex_wk85(u32 heap_slot, ID3D12Resource* res, DXGI_FORMAT fmt, UINT mapping, int cube);
static void srv_write_ex(u32 heap_slot, ID3D12Resource* res, DXGI_FORMAT fmt, UINT mapping, int cube) { const unsigned long long t_ = WK85_T0(); srv_write_ex_wk85(heap_slot, res, fmt, mapping, cube); WK85_ADD(WK85_srvw, t_); }   /* winport v85a-walkprof */
static void srv_write_ex_wk85(u32 heap_slot, ID3D12Resource* res, DXGI_FORMAT fmt,
                         UINT mapping, int cube)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {0};
    sv.Format = fmt;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = mapping;
    sv.Texture2D.MipLevels = 1;
    if (cube) {   /* must match the TextureCube declaration in the HLSL */
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        sv.TextureCube.MipLevels = 1;
        sv.TextureCube.MostDetailedMip = 0;
        sv.TextureCube.ResourceMinLODClamp = 0.0f;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    s_d3d.srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &h);
    h.ptr += (u64)heap_slot * s_d3d.srv_inc;
    s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, res, &sv, h);
}

static void srv_write(u32 heap_slot, ID3D12Resource* res, DXGI_FORMAT fmt, UINT mapping)
{
    srv_write_ex(heap_slot, res, fmt, mapping, 0);
}


/* winport-fix: v22 scene depth -- per-zeta depth snapshots.
 * The shared DSV is cleared on every render-target switch, so a later pass that samples a depth
 * surface as a texture (GoW2's scene-copy shader) would read nothing.  Before the clear, the DSV is
 * copied into a snapshot texture (2048x2048 R24G8_TYPELESS, same size/format family as the DSV) for
 * every zeta offset that some draw of this frame samples as an ARGB8 texture. */
#define WP_ZS_MAX 4
typedef struct { u32 zoff, w, h; ID3D12Resource* res; } WpZSnap;
static WpZSnap s_wp_zs[WP_ZS_MAX];
static u32 s_wp_zneed[WP_ZS_MAX], s_wp_zneed_w[WP_ZS_MAX], s_wp_zneed_h[WP_ZS_MAX];
static int s_wp_zneed_n = 0;
static u32 s_wp_zs_copies = 0, s_wp_zs_bound = 0;
static int wp_zneed_find(u32 zoff)
{
    if (!zoff) return -1;
    for (int i = 0; i < s_wp_zneed_n; i++) if (s_wp_zneed[i] == zoff) return i;
    return -1;
}
static WpZSnap* wp_zs_get(u32 zoff, u32 w, u32 h)
{
    WpZSnap* free_ = NULL;
    for (int i = 0; i < WP_ZS_MAX; i++) {
        if (s_wp_zs[i].res && s_wp_zs[i].zoff == zoff) { s_wp_zs[i].w = w; s_wp_zs[i].h = h; return &s_wp_zs[i]; }
        if (!s_wp_zs[i].res && !free_) free_ = &s_wp_zs[i];
    }
    if (!free_) return NULL;
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = {0};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = s_wp68_dsv; td.Height = s_wp68_dsv; td.DepthOrArraySize = 1; td.MipLevels = 1;   /* winport v68: = DSV size */
    td.Format = DXGI_FORMAT_R24G8_TYPELESS; td.SampleDesc.Count = 1;
    td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    HRESULT hr = s_d3d.device->lpVtbl->CreateCommittedResource(
        s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL,
        &IID_ID3D12Resource, (void**)&free_->res);
    if (FAILED(hr)) { free_->res = NULL; printf("[ZSNAP] create failed 0x%08lX\n", (unsigned long)hr); return NULL; }
    free_->zoff = zoff; free_->w = w; free_->h = h;
    printf("[ZSNAP] snapshot texture for zeta 0x%08X (%ux%u)\n", zoff, w, h);
    return free_;
}
/* Copy the DSV's current contents into the snapshot of zeta `zoff` (if this frame samples it). */
static void wp_zs_take(u32 zoff)
{
    int ni = wp_zneed_find(zoff);
    if (ni < 0) return;
    WpZSnap* zs = wp_zs_get(zoff, s_wp_zneed_w[ni], s_wp_zneed_h[ni]);
    if (!zs || !zs->res) return;
    D3D12_RESOURCE_BARRIER b[2]; memset(b, 0, sizeof b);
    b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition.pResource = s_d3d.depth_buffer;
    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[1].Transition.pResource = zs->res;
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    b[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, b);
    s_d3d.cmd_list->lpVtbl->CopyResource(s_d3d.cmd_list, zs->res, s_d3d.depth_buffer);
    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, b);
    s_wp_zs_copies++;
    { static int n_ = 0; if (n_++ < 6) printf("[ZSNAP] copied depth of zeta 0x%08X (%ux%u) before the render-target switch\n", zoff, zs->w, zs->h); }
}
/* Pre-pass: which zeta surfaces does this frame sample as ARGB8 textures? */
static void wp_zs_scan(void)
{
    s_wp_zneed_n = 0;
    if (!wp_zdepth_on()) return;
    u32 zo[8], zw[8], zh[8]; int nz = 0;
    for (u32 d = 0; d < s_d3d.draw_count && d < MAX_DRAWS; d++) {
        const D3D12DrawRecord* dr = &s_d3d.draws[d];
        if (!dr->is_vp || dr->is_blit || !dr->zeta_off) continue;
        int k = -1; for (int i = 0; i < nz; i++) if (zo[i] == dr->zeta_off) { k = i; break; }
        if (k < 0 && nz < 8) { zo[nz] = dr->zeta_off; zw[nz] = dr->rt_w; zh[nz] = dr->rt_h; nz++; }
        else if (k >= 0 && !zw[k] && dr->rt_w) { zw[k] = dr->rt_w; zh[k] = dr->rt_h; }
    }
    { static int zd_ = -1; if (zd_ < 0) { const char* e_ = getenv("WINPORT_ZDUMP"); zd_ = (e_ && atoi(e_)) ? 1 : 0; }   /* winport-fix: v22b zeta dump */
      for (int i = 0; zd_ && i < nz && s_wp_zneed_n < WP_ZS_MAX; i++) {
          s_wp_zneed[s_wp_zneed_n] = zo[i]; s_wp_zneed_w[s_wp_zneed_n] = zw[i] ? zw[i] : s_d3d.width; s_wp_zneed_h[s_wp_zneed_n] = zh[i] ? zh[i] : s_d3d.height; s_wp_zneed_n++; } }
    for (u32 d = 0; d < s_d3d.draw_count && d < MAX_DRAWS; d++) {
        const D3D12DrawRecord* dr = &s_d3d.draws[d];
        if (!dr->is_vp || dr->is_clear || dr->is_blit) continue;
        for (int u = 0; u < 4; u++) {
            if (!dr->tex[u].set || !dr->tex[u].raw || (dr->tex[u].fmt & 0x9F) != 0x85) continue;
            for (int i = 0; i < nz; i++) {
                if (zo[i] != dr->tex[u].raw) continue;
                if (wp_zneed_find(zo[i]) < 0 && s_wp_zneed_n < WP_ZS_MAX) {
                    s_wp_zneed[s_wp_zneed_n] = zo[i];
                    s_wp_zneed_w[s_wp_zneed_n] = zw[i] ? zw[i] : s_d3d.width;
                    s_wp_zneed_h[s_wp_zneed_n] = zh[i] ? zh[i] : s_d3d.height;
                    s_wp_zneed_n++;
                }
            }
        }
    }
}

static void wp_zs_dump_all(u32 fi, const char* dir)   /* winport-fix: v22b zeta dump */
{
    { static int zd_ = -1; if (zd_ < 0) { const char* e_ = getenv("WINPORT_ZDUMP_FILES"); zd_ = (e_ && atoi(e_)) ? 1 : 0; }   /* v22d: this readback removed the D3D12 device on Seth's GPU -> opt-in only */
      if (!zd_) return; }
    for (int i = 0; i < WP_ZS_MAX; i++) {
        if (!s_wp_zs[i].res) continue;
        char path[512];
        snprintf(path, sizeof path, "%s/zeta_%08X.bmp", dir, s_wp_zs[i].zoff);
        cap_readback_write(s_wp_zs[i].res, s_wp68_dsv, s_wp68_dsv, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, fi, path);
    }
}

static void off_rt_transition(int slot, D3D12_RESOURCE_STATES to)
{
    OffRT* r = &s_d3d.off_rt[slot];
    if (r->st == to) return;
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = r->res;
    b.Transition.StateBefore = r->st;
    b.Transition.StateAfter  = to;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    r->st = to;
}

/* Set by the present entry point: 1 = this batch ends with a swapchain
 * Present, 0 = execute the recorded draws only. An OFFSCREEN-only batch (a
 * render-to-texture pass) must still run -- it is what fills the texture a
 * later batch samples -- but presenting it would show a half-built frame. */
static int s_present_this_frame = 1;

/* Resolve a DRAW_*_TEX value: a hex raw offset, or the literal "duck" for the
 * content-identified duck texture. */
static u32 draw_filter_tex(const char* e)
{
    if (!e) return 0;
    if (e[0] == 'd' && e[1] == 'u') return s_duck_raw;
    return (u32)strtoul(e, NULL, 16);
}

/* Copy the current backbuffer into s_screen_copy. Called mid-frame the moment
 * the reduced-viewport passes finish -- this title renders its reflection into
 * the TOP-LEFT REGION of the render target and the main scene then overwrites
 * it, so a snapshot taken at end of frame contains the scene, not the
 * reflection. Also called at end of frame as a fallback for frames with no
 * reduced-viewport pass. */
static void screen_copy_capture(u32 fi)
{
    static int en = -1;
    if (en < 0) { const char* e = getenv("SCREEN_AS_TEX"); en = e ? atoi(e) : 1; }
    if (!en || !s_d3d.device) return;
            if (!s_screen_copy) {
                D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
                D3D12_RESOURCE_DESC td = {0};
                td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                td.Width = WP68_BW; td.Height = WP68_BH;   /* winport v68: CopyResource of the back buffer */
                td.DepthOrArraySize = 1; td.MipLevels = 1;
                td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
                td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
                if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
                        s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL,
                        &IID_ID3D12Resource, (void**)&s_screen_copy)))
                    s_screen_copy = NULL;
            }
            if (s_screen_copy) {
                D3D12_RESOURCE_BARRIER bb[2] = {0};
                bb[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bb[0].Transition.pResource   = s_d3d.render_targets[fi];
                bb[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                bb[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
                bb[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                bb[1] = bb[0];
                bb[1].Transition.pResource   = s_screen_copy;
                bb[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                bb[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
                s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, bb);

                s_d3d.cmd_list->lpVtbl->CopyResource(s_d3d.cmd_list,
                    s_screen_copy, s_d3d.render_targets[fi]);
                if (s_wp68_s != 1.0f) s_wp68_n_sc++;   /* winport v68 */

                bb[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
                bb[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
                bb[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                bb[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, bb);

                /* SCREENCOPY_DUMP=1: stage the captured image into the readback
                 * buffer so it can be written out and LOOKED AT. Claiming a
                 * capture happened is not the same as showing what is in it. */
                { static int sd = -1;
                  if (sd < 0) { const char* e = getenv("SCREENCOPY_DUMP"); sd = e ? atoi(e) : 0; }
                  if (sd && s_d3d.readback_buf) {
                      D3D12_RESOURCE_BARRIER t = {0};
                      t.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                      t.Transition.pResource   = s_screen_copy;
                      t.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                      t.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
                      t.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                      s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &t);

                      D3D12_TEXTURE_COPY_LOCATION cd = {0}, cs = {0};
                      cd.pResource = s_d3d.readback_buf;
                      cd.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                      cd.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
                      cd.PlacedFootprint.Footprint.Width    = WP68_BW;   /* winport v68 */
                      cd.PlacedFootprint.Footprint.Height   = WP68_BH;
                      cd.PlacedFootprint.Footprint.Depth    = 1;
                      cd.PlacedFootprint.Footprint.RowPitch = s_d3d.readback_pitch;
                      cs.pResource = s_screen_copy;
                      cs.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                      s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &cd, 0, 0, 0, &cs, NULL);

                      t.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
                      t.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                      s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &t);
                      s_sc_dump_pending = 1;
                  } }
            }

}

/* rsx_topology -> D3D_PRIMITIVE_TOPOLOGY. Spelled out rather than relying on
 * the two enums happening to agree: the draw record carries the neutral value
 * so a second backend can read it, and this is the one place it becomes a D3D
 * one. */
static D3D12_PRIMITIVE_TOPOLOGY topo_to_d3d(u32 t)
{
    switch (t) {
    case RSX_TOPOLOGY_POINTS:         return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case RSX_TOPOLOGY_LINES:          return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case RSX_TOPOLOGY_LINE_STRIP:     return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case RSX_TOPOLOGY_TRIANGLE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case RSX_TOPOLOGY_TRIANGLES:      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    default:                          return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    }
}

/* GCM_GUEST_FB=<off>,<w>,<h>,<pitch> -- present the guest display buffer.
 *
 * A title that composites its final image with the RSX 2D engine assembles it
 * in GUEST memory: nv3089_blit is a CPU copy and never touches D3D12, and the
 * draw pass below only runs when a recorded draw targets the display surface,
 * which for such a title never happens. The frame is finished, in the wrong
 * memory, and the swapchain shows only the clear. Copy it across.
 *
 * Off unless the offset and geometry are given, so nothing else changes. */
/* GPU-side composite of an offscreen RT onto the backbuffer.
 *
 * A title that finishes its frame with the RSX 2D engine blits a rendered
 * surface into the display buffer. That surface lives in guest RAM, which the
 * D3D12 path never writes -- the scene is on the GPU -- so the CPU copy in
 * nv3089_blit faithfully moves zeros. The backend already holds the same
 * surface as a texture, so do the composite here instead. Triggered from
 * cellGcmSys under GCM_COMPOSITE_RT. */
static u32 s_composite_src = 0;
static int s_last_drawn_slot = -1;
static u32 s_last_drawn_off  = 0;

void rsx_d3d12_composite_from_offscreen(u32 raw_off) { s_composite_src = raw_off; }

static void composite_present(u32 fi)
{
    u32 want = s_composite_src;
    s_composite_src = 0;
    if (!want) return;
    /* want == 2 means "whatever this frame's draws targeted" -- the one thing
     * guaranteed to hold geometry, and immune to the offset collision above. */
    int i;
    if (want == 2u) { i = s_last_drawn_slot; want = s_last_drawn_off; }
    else            { i = off_rt_find(want); }
    if (i < 0 || !s_d3d.off_rt[i].res) return;
    OffRT* r = &s_d3d.off_rt[i];
    /* Same-format copy only; a half-float source would need a converting draw. */
    if (r->dxgi != DXGI_FORMAT_R8G8B8A8_UNORM) {
        static int w1 = 0;
        if (w1++ < 2)
            fprintf(stderr, "[COMPOSITE] RT 0x%08X is dxgi=%u, not R8G8B8A8 -- needs a draw%c",
                    want, r->dxgi, 10);
        return;
    }
    u32 cw = r->w < s_d3d.width  ? r->w : s_d3d.width;
    u32 ch = r->h < s_d3d.height ? r->h : s_d3d.height;
    if (!cw || !ch) return;
    { static int n = 0;
      if (n++ < 3) fprintf(stderr, "[COMPOSITE] RT 0x%08X %ux%u -> backbuffer%c",
                           want, cw, ch, 10); }

    D3D12_RESOURCE_BARRIER b[2] = {0};
    b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition.pResource   = r->res;
    b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b[0].Transition.StateBefore = r->st;
    b[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[1] = b[0];
    b[1].Transition.pResource   = s_d3d.render_targets[fi];
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, b);

    D3D12_TEXTURE_COPY_LOCATION dl = {0}, sl = {0};
    dl.pResource = s_d3d.render_targets[fi];
    dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dl.SubresourceIndex = 0;
    sl.pResource = r->res;
    sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; sl.SubresourceIndex = 0;
    D3D12_BOX box = {0};
    box.left = 0; box.top = 0; box.front = 0;
    box.right = wp68_px(cw); box.bottom = wp68_px(ch); box.back = 1;   /* winport v68 */
    if (s_wp68_s != 1.0f) s_wp68_n_comp++;
    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dl, 0, 0, 0, &sl, &box);

    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[0].Transition.StateAfter  = r->st;
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, b);
}


static void guest_fb_present(u32 fi)
{
    static int inited = 0;
    static u32 off, gw, gh, gpitch, up_pitch;
    static ID3D12Resource* up = NULL;
    if (!inited) {
        inited = 1;
        const char* e = getenv("GCM_GUEST_FB");
        unsigned a = 0, b = 0, c = 0, d = 0;
        if (!e || sscanf(e, "%i,%u,%u,%u", &a, &b, &c, &d) != 4 || !b || !c) return;
        off = a; gw = b; gh = c; gpitch = d;
        up_pitch = (gw * 4u + 255u) & ~255u;      /* D3D12 wants 256-byte rows */
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd = {0};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width  = (u64)up_pitch * gh; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1;
        bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
                s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &bd,
                D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void**)&up)))
            up = NULL;
        fprintf(stderr, "[GUESTFB] presenting guest 0x%08X %ux%u pitch %u -> backbuffer%c",
                off, gw, gh, gpitch, 10);
    }
    if (!up) return;
    if (s_wp68_s != 1.0f) { s_wp68_n_gfb++;   /* winport v68: GCM_GUEST_FB copy is NOT scaled (debug path) */
        static int n68 = 0; if (!n68++) printf("[RES68] UNSCALED path: GCM_GUEST_FB guest framebuffer copied at 1x into the top-left\n"); }
    { extern u8* vm_base;
      if (!vm_base) return;
      /* RSX local memory is mapped at guest 0xC0000000. */
      const u8* srow = vm_base + 0xC0000000u + off;
      void* m = NULL; D3D12_RANGE nr = {0, 0};
      if (wp77_on()) { wp77_wait(s77_gfb_use, 4); s77_gfb_use = s77_val + 1; }   /* winport v77-fif */
      if (FAILED(up->lpVtbl->Map(up, 0, &nr, &m)) || !m) return;
      for (u32 y = 0; y < gh; y++) {
          const u8* s = srow + (size_t)y * gpitch;
          u8* dp = (u8*)m + (size_t)y * up_pitch;
          for (u32 x = 0; x < gw; x++) {   /* guest B,G,R,A -> backbuffer R,G,B,A */
              dp[x*4+0] = s[x*4+2]; dp[x*4+1] = s[x*4+1];
              dp[x*4+2] = s[x*4+0]; dp[x*4+3] = 0xFF;
          }
      }
      D3D12_RANGE wr = {0, (SIZE_T)up_pitch * gh};
      up->lpVtbl->Unmap(up, 0, &wr); }

    D3D12_RESOURCE_BARRIER gb = {0};
    gb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    gb.Transition.pResource   = s_d3d.render_targets[fi];
    gb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    gb.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    gb.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &gb);

    D3D12_TEXTURE_COPY_LOCATION dl = {0}, sl = {0};
    dl.pResource = s_d3d.render_targets[fi];
    dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dl.SubresourceIndex = 0;
    sl.pResource = up; sl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    sl.PlacedFootprint.Offset = 0;
    sl.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
    sl.PlacedFootprint.Footprint.Width    = gw;
    sl.PlacedFootprint.Footprint.Height   = gh;
    sl.PlacedFootprint.Footprint.Depth    = 1;
    sl.PlacedFootprint.Footprint.RowPitch = up_pitch;
    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dl, 0, 0, 0, &sl, NULL);

    gb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    gb.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &gb);
}


/* winport-fix: NV3089 blits on the GPU ------------------------------------------------- */
typedef struct { u32 offset, pitch, width, height; } WpDispInfo;   /* = CellGcmDisplayInfo */
extern WpDispInfo* cellGcmGetDisplayInfo(u32 index);
/* ---- winport v92b-prevframe (fix_v92b_prevframe_renderer.py) ---- */
static int wp92_prev_on(void)
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_PREVFRAME92"); v = (e && e[0] == '1');
    if (v) fprintf(stderr, "[PREV92] display-buffer textures sample the last finished frame%c", 10); } return v; }
static int wp92_is_dispbuf(u32 off)
{
    if (off < 0xC0000000u) return 0;
    for (u32 i = 0; i < 8; i++) {
        WpDispInfo* di = cellGcmGetDisplayInfo(i);
        if (di && di->width && di->offset == off - 0xC0000000u) return 1;
    }
    return 0;
}
static void wp92_prev_capture(u32 fi)
{
    if (!wp92_prev_on() || !s_d3d.device) return;
    if (!s_wp92_prev) {
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = {0};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = WP68_BW; td.Height = WP68_BH;
        td.DepthOrArraySize = 1; td.MipLevels = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL, &IID_ID3D12Resource, (void**)&s_wp92_prev))) {
            s_wp92_prev = NULL; return; }
    }
    D3D12_RESOURCE_BARRIER bb[2] = {0};
    bb[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bb[0].Transition.pResource   = s_d3d.render_targets[fi];
    bb[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bb[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    bb[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    bb[1] = bb[0];
    bb[1].Transition.pResource   = s_wp92_prev;
    bb[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    bb[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, bb);
    s_d3d.cmd_list->lpVtbl->CopyResource(s_d3d.cmd_list, s_wp92_prev, s_d3d.render_targets[fi]);
    bb[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    bb[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bb[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    bb[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, bb);
    s_wp92_prev_ok = 1;
}
/* ---- end winport v92b-prevframe ---- */
/* ---- winport v94a-ffhaze (fix_v94a_fullframe_haze_renderer.py) ---- */
extern uint8_t* vm_base;
extern unsigned cellGcm_flip_request_count(void);
static int wp94_prev_mode(void)
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_PREVFRAME94"); v = (e && e[0] >= '1' && e[0] <= '2') ? e[0] - '0' : 0;
    if (v) fprintf(stderr, "[PREV94] display-buffer textures: %s%c", v == 1 ? "frame last rendered into that buffer (no host overlays)"
                                                                             : "previous game frame (no host overlays)", 10); } return v; }
static int wp94_diag(void) { static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_PREV94_DIAG"); v = (e && e[0] == '1'); } return v; }
static int wp94_buf_index(u32 raw)   /* raw: local offset (0x00400000) or 0xC0400000 */
{
    raw &= 0x0FFFFFFFu;
    for (u32 i = 0; i < 8; i++) {
        WpDispInfo* di = cellGcmGetDisplayInfo(i);
        if (di && di->width && di->offset == raw) return (int)i;
    }
    return -1;
}
static u32 wp94_cur_disp_raw(void)
{
    const rsx_state* st = s_d3d.current_rsx_state;
    if (!st) return 0;
    return st->surface_color_offset[(st->color_target == 2) ? 1 : 0];
}
static unsigned s_wp94_fc; static u32 s_wp94_clrmask;
static void wp94_flip_sync(void)
{ unsigned fc = cellGcm_flip_request_count(); if (fc != s_wp94_fc) { s_wp94_fc = fc; s_wp94_clrmask = 0; } }
static void wp94_rec(D3D12DrawRecord* dr)   /* record time, every draw */
{
    dr->wp94_tgt = 0; dr->wp94_clr = 0;
    if (!wp94_prev_mode()) return;
    wp94_flip_sync();
    if (dr->rt_off == 0) dr->wp94_tgt = wp94_cur_disp_raw() | 0x80000000u;   /* bit 31: set (offset 0 is valid) */
    dr->wp94_clr = s_wp94_clrmask;
}
static void wp94_clear_note(u32 flags)   /* record time, a DISPLAY clear */
{
    if (!wp94_prev_mode() || !(flags & 0xF0u)) return;
    wp94_flip_sync();
    int i = wp94_buf_index(wp94_cur_disp_raw());
    if (i >= 0) s_wp94_clrmask |= 1u << i;
}
static ID3D12Resource* wp94_new_copy(void)
{
    ID3D12Resource* r = NULL;
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = {0};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = WP68_BW; td.Height = WP68_BH;
    td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL, &IID_ID3D12Resource, (void**)&r))) return NULL;
    return r;
}
/* end of frame, BEFORE the host overlays: keep the game's own frame for the buffer it was rendered into */
static void wp94_capture(u32 fi)
{
    const int m = wp94_prev_mode();
    if (!m || !s_d3d.device || !s_wp94_frame_tgt) return;
    ID3D12Resource** dst; int* ok;
    if (m == 1) {
        int i = wp94_buf_index(s_wp94_frame_tgt & 0x7FFFFFFFu);
        if (i < 0) { s_wp94_frame_tgt = 0; return; }
        dst = &s_wp94_buf[i]; ok = &s_wp94_ok[i];
    } else { dst = &s_wp94_last; ok = &s_wp94_last_ok; }
    s_wp94_frame_tgt = 0;
    if (!*dst && !(*dst = wp94_new_copy())) return;
    D3D12_RESOURCE_BARRIER bb[2] = {0};
    bb[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bb[0].Transition.pResource   = s_d3d.render_targets[fi];
    bb[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bb[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    bb[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    bb[1] = bb[0];
    bb[1].Transition.pResource   = *dst;
    bb[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    bb[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, bb);
    s_d3d.cmd_list->lpVtbl->CopyResource(s_d3d.cmd_list, *dst, s_d3d.render_targets[fi]);
    bb[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    bb[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bb[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    bb[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 2, bb);
    *ok = 1;
}
/* render time: the resource a display-buffer texture should sample (NULL = old path) */
static ID3D12Resource* wp94_pick(const D3D12DrawRecord* dr, int u)
{
    const int m = wp94_prev_mode();
    int bi = wp94_buf_index(dr->tex[u].off);
    if (!m || bi < 0) return NULL;
    int ti = dr->wp94_tgt ? wp94_buf_index(dr->wp94_tgt & 0x7FFFFFFFu) : -1;
    int cleared = (dr->wp94_clr >> bi) & 1;
    ID3D12Resource* r = NULL;
    if (m == 1) r = (!cleared && s_wp94_ok[bi]) ? s_wp94_buf[bi] : NULL;
    else r = s_wp94_last_ok ? s_wp94_last : NULL;
    { static int n = 0; if (wp94_diag() && n < 60) { n++;
        fprintf(stderr, "[PREV94] fp=0x%X uh=0x%08X unit %d reads buf%d (0x%08X) | draw target buf%d | colour-cleared mask 0x%X | flip#%u | -> %s%c",
                dr->fp_addr, dr->fp_uhash, u, bi, dr->tex[u].off, ti, dr->wp94_clr, cellGcm_flip_request_count(),
                r ? "copy" : (cleared ? "cleared: screen copy" : "no copy yet: old path"), 10); } }
    return r;
}
/* the game's Full Frame option (byte in the display-settings object, TOC-0x2584 -> +0x30760) */
static int wp94_ff_flag(void)
{
    if (!vm_base) return 0;
    const u8* p = vm_base + 0x0053EBF4u;
    u32 base = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
    u32 a = (base && base < 0x0F000000u) ? base + 0x30760u : 0x00731558u;
    return vm_base[a] == 1;
}
static void wp94_ff_fit(float bw, float bh, float w, float h, float* vw, float* vh)
{
    static int mode = -1, last = -1;
    if (mode < 0) { const char* e = getenv("WINPORT_FULLFRAME94"); mode = (e && e[0] >= '1' && e[0] <= '2') ? e[0] - '0' : 0;
        if (mode) fprintf(stderr, "[FF94] movies follow the game's Full Frame option (%s when on)%c", mode == 1 ? "fill, crop" : "stretch", 10); }
    if (!mode || w <= 0.0f || h <= 0.0f) return;
    int ff = wp94_ff_flag();
    if (ff) {
        if (mode == 2) { *vw = bw; *vh = bh; }
        else { float s = bw / w > bh / h ? bw / w : bh / h; *vw = w * s; *vh = h * s; }
    }
    if (ff != last) { last = ff;
        fprintf(stderr, "[FF94] Full Frame %s -> movie %.0fx%.0f picture drawn %.0fx%.0f in %.0fx%.0f%c", ff ? "ON" : "off", w, h, *vw, *vh, bw, bh, 10); }
}
/* ---- end winport v94a-ffhaze ---- */
/* ---- winport v92b-dispsize (fix_v92b_dispsize_renderer.py) ---- */
static int wp92_dispsize_skip(void)
{
    static int on = -1, said = 0;
    if (on < 0) { const char* e = getenv("WINPORT_DISPSIZE92"); on = (e && e[0] == '1'); }
    if (!on) return 0;
    for (u32 i = 0; i < 8; i++) {
        WpDispInfo* di = cellGcmGetDisplayInfo(i);
        if (di && di->width && di->height) {
            if (!said) { said = 1; fprintf(stderr, "[DISPSIZE92] display buffers registered -> display-sized surfaces elsewhere are render targets%c", 10); }
            return 1;
        }
    }
    return 0;
}
/* ---- end winport v92b-dispsize ---- */
static ID3D12RootSignature* s_wp_blit_rs;
static ID3DBlob* s_wp_blit_vs;
static ID3DBlob* s_wp_blit_ps;
static struct { u32 fmt; ID3D12PipelineState* pso; } s_wp_blit_pso[8];
static int s_wp_blit_pso_n, s_wp_blit_broken;
static u32 s_wp_blit_srv_next;                  /* per-frame ring position */
static unsigned s_wp_blits_rec, s_wp_blits_exec, s_wp_blits_skip;
static ID3D12Resource* s_wp_bbcopy;             /* backbuffer snapshot (display-sourced blits) */
static D3D12_RESOURCE_STATES s_wp_bbcopy_st;

static int wp_blit_trace(void)
{
    static int t = -1;
    if (t < 0) { const char* e = getenv("WINPORT_BLIT_TRACE"); t = e ? atoi(e) : 0; }
    return t;
}
static u32 wp_dxgi_bpp(u32 dxgi)
{
    return dxgi == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8u :
           dxgi == DXGI_FORMAT_R32G32B32A32_FLOAT ? 16u : 4u;
}
/* Which GPU surface holds this RSX local offset: an offscreen RT (>= 0; the pitch places an
 * offset inside it at x,y), a display buffer (-2, the backbuffer), or none (-1). */
static int wp_surface_at(u32 off, u32 pitch, u32* px, u32* py)
{
    *px = *py = 0;
    int i = off_rt_find(off);
    if (i >= 0) return i;
    for (u32 b = 0; b < 8; b++) {
        WpDispInfo* di = cellGcmGetDisplayInfo(b);
        if (!di || !di->pitch || !di->height) continue;
        if (off >= di->offset && (u64)off < (u64)di->offset + (u64)di->pitch * di->height) {
            u32 d = off - di->offset;
            *py = d / di->pitch; *px = (d % di->pitch) / 4u;
            return -2;
        }
    }
    if (!pitch) return -1;
    for (int k = 0; k < MAX_OFF_RTS; k++) {
        const OffRT* r = &s_d3d.off_rt[k];
        if (!r->res || off < r->off) continue;
        u32 d = off - r->off, y = d / pitch, x = (d % pitch) / wp_dxgi_bpp(r->dxgi);
        if (y < r->h && x < r->w) { *px = x; *py = y; return k; }
    }
    return -1;
}

static int wp_blit_init(void)
{
    if (s_wp_blit_rs) return 1;
    if (s_wp_blit_broken) return 0;
    s_wp_blit_broken = 1;
    static const char hlsl[] =
        "cbuffer C : register(b0) { float4 uvr; };\n"
        "Texture2D t0 : register(t0);\n"
        "SamplerState s0 : register(s0);\n"
        "struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
        "V vs(uint id : SV_VertexID) {\n"
        "  V o; float2 q = float2(id & 1, id >> 1);\n"
        "  o.p = float4(q.x * 2 - 1, 1 - q.y * 2, 0, 1);\n"
        "  o.uv = lerp(uvr.xy, uvr.zw, q); return o; }\n"
        "float4 ps(V i) : SV_Target { return t0.SampleLevel(s0, i.uv, 0); }\n";
    ID3DBlob* err = NULL;
    if (FAILED(D3DCompile(hlsl, sizeof(hlsl) - 1, "wp_blit", NULL, NULL, "vs", "vs_5_0", 0, 0,
                          &s_wp_blit_vs, &err)) ||
        FAILED(D3DCompile(hlsl, sizeof(hlsl) - 1, "wp_blit", NULL, NULL, "ps", "ps_5_0", 0, 0,
                          &s_wp_blit_ps, &err))) {
        printf("[WPBLIT] shader compile failed: %s\n",
               err ? (const char*)err->lpVtbl->GetBufferPointer(err) : "?");
        if (err) err->lpVtbl->Release(err);
        return 0;
    }
    if (err) { err->lpVtbl->Release(err); err = NULL; }
    D3D12_DESCRIPTOR_RANGE rg = {0};
    rg.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rg.NumDescriptors = 1;
    rg.BaseShaderRegister = 0;
    rg.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER rp[2];
    memset(rp, 0, sizeof rp);
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rp[0].Constants.ShaderRegister = 0;
    rp[0].Constants.Num32BitValues = 4;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[1].DescriptorTable.NumDescriptorRanges = 1;
    rp[1].DescriptorTable.pDescriptorRanges = &rg;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sd = {0};
    sd.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sd.MaxLOD = D3D12_FLOAT32_MAX;
    sd.ShaderRegister = 0;
    sd.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rd = {0};
    rd.NumParameters = 2; rd.pParameters = rp;
    rd.NumStaticSamplers = 1; rd.pStaticSamplers = &sd;
    ID3DBlob* sig = NULL;
    if (FAILED(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) || !sig) {
        printf("[WPBLIT] root signature: %s\n",
               err ? (const char*)err->lpVtbl->GetBufferPointer(err) : "?");
        if (err) err->lpVtbl->Release(err);
        return 0;
    }
    HRESULT hr = s_d3d.device->lpVtbl->CreateRootSignature(s_d3d.device, 0,
        sig->lpVtbl->GetBufferPointer(sig), sig->lpVtbl->GetBufferSize(sig),
        &IID_ID3D12RootSignature, (void**)&s_wp_blit_rs);
    sig->lpVtbl->Release(sig);
    if (FAILED(hr) || !s_wp_blit_rs) { s_wp_blit_rs = NULL; printf("[WPBLIT] CreateRootSignature 0x%08lX\n", hr); return 0; }
    s_wp_blit_broken = 0;
    printf("[WPBLIT] GPU blit pipeline ready\n");
    return 1;
}
static ID3D12PipelineState* wp_blit_pso(DXGI_FORMAT fmt)
{
    for (int i = 0; i < s_wp_blit_pso_n; i++)
        if (s_wp_blit_pso[i].fmt == (u32)fmt) return s_wp_blit_pso[i].pso;
    if (s_wp_blit_pso_n >= 8) return NULL;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd;
    memset(&pd, 0, sizeof pd);
    pd.pRootSignature = s_wp_blit_rs;
    pd.VS.pShaderBytecode = s_wp_blit_vs->lpVtbl->GetBufferPointer(s_wp_blit_vs);
    pd.VS.BytecodeLength  = s_wp_blit_vs->lpVtbl->GetBufferSize(s_wp_blit_vs);
    pd.PS.pShaderBytecode = s_wp_blit_ps->lpVtbl->GetBufferPointer(s_wp_blit_ps);
    pd.PS.BytecodeLength  = s_wp_blit_ps->lpVtbl->GetBufferSize(s_wp_blit_ps);
    pd.BlendState.RenderTarget[0].BlendEnable = FALSE;
    pd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    pd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    pd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    pd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pd.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.DepthStencilState.StencilEnable = FALSE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = fmt;
    pd.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pd.SampleDesc.Count = 1;
    ID3D12PipelineState* p = NULL;
    HRESULT hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(s_d3d.device, &pd,
        &IID_ID3D12PipelineState, (void**)&p);
    if (FAILED(hr) || !p) { printf("[WPBLIT] PSO for dxgi %u failed 0x%08lX\n", (u32)fmt, hr); return NULL; }
    s_wp_blit_pso[s_wp_blit_pso_n].fmt = (u32)fmt;
    s_wp_blit_pso[s_wp_blit_pso_n].pso = p;
    s_wp_blit_pso_n++;
    return p;
}

/* Record side (backend->blit_scaled, called by the engine's NV3089 handler in FIFO order). */
static int d3d12_blit_scaled_wk85(void* ud, const rsx_blit_desc* d);
static int d3d12_blit_scaled(void* ud, const rsx_blit_desc* d) { const unsigned long long t_ = WK85_T0(); int r_ = d3d12_blit_scaled_wk85(ud, d); WK85_ADD(WK85_blit, t_); return r_; }   /* winport v85a-walkprof */
static int d3d12_blit_scaled_wk85(void* ud, const rsx_blit_desc* d)
{
    (void)ud;
    static int en = -1;
    if (en < 0) { const char* e = getenv("WINPORT_BLIT"); en = e ? atoi(e) : 1; }
    const char* why = NULL;
    u32 sx = 0, sy = 0, dx = 0, dy = 0;
    int si = -1, di = -1;
    if (!en) why = "disabled";
    else if (!s_d3d.initialized) why = "no device";
    else if (!d->src_local || !d->dst_local) why = "main-memory surface";
    else if (!d->out_w || !d->out_h || !d->ds_dx || !d->dt_dy) why = "empty";
    else if (s_d3d.draw_count >= MAX_DRAWS) why = "draw list full";
    else if ((si = wp_surface_at(d->src_offset, d->src_pitch, &sx, &sy)) == -1) why = "source is not a GPU surface";
    else if ((di = wp_surface_at(d->dst_offset, d->dst_pitch, &dx, &dy)) == -1) why = "destination is not a GPU surface";
    else if (si == di && si != -2) why = "same surface";
    /* NV3089 DS_DX / DT_DY: source texels per output pixel, 12.20 fixed point (RPCS3 decodes
     * the scale as 1048576 / value). If that reading makes the source footprint implausibly
     * small against the declared input size, fall back to 20.12. */
    float fx = (float)d->ds_dx / 1048576.0f, fy = (float)d->dt_dy / 1048576.0f;
    int alt = 0;
    if (!why && d->in_w && d->in_h &&
        (float)d->out_w * fx * 16.0f < (float)d->in_w && (float)d->out_h * fy * 16.0f < (float)d->in_h &&
        (float)d->out_w * d->ds_dx / 4096.0f <= d->in_w * 1.05f + 1.0f) {
        fx = (float)d->ds_dx / 4096.0f; fy = (float)d->dt_dy / 4096.0f; alt = 1;
    }
    int trace = wp_blit_trace();
    if (trace) {
        static int n = 0;
        if (n++ < trace)
            printf("[WPBLIT] %ux%u src=0x%X pitch %u in %ux%u uv %u,%u d %08X,%08X -> dst=0x%X pitch %u at %u,%u"
                   " | src=%d(%u,%u) dst=%d(%u,%u) scale %.4f,%.4f%s : %s\n",
                   d->out_w, d->out_h, d->src_offset, d->src_pitch, d->in_w, d->in_h, d->u0, d->v0,
                   d->ds_dx, d->dt_dy, d->dst_offset, d->dst_pitch, d->out_x, d->out_y,
                   si, sx, sy, di, dx, dy, fx, fy, alt ? " (20.12)" : "", why ? why : "recorded");
    }
    if (!why && si == -2 && di == -2) {
        /* display -> display: every display buffer is the one backbuffer here. Same spot is a
         * no-op; anything else is not representable. */
        if (sx == dx + d->out_x && sy == dy + d->out_y) return 1;
        why = "display to display";
    }
    if (why) { s_wp_blits_skip++; return 0; }
    D3D12DrawRecord* dr = &s_d3d.draws[s_d3d.draw_count++];
    memset(dr, 0, sizeof(*dr));
    dr->is_vp = 1; dr->is_blit = 1; dr->tex_slot = -1; dr->vs_idx = -1;
    for (int u = 0; u < 4; u++) dr->tex_rt[u] = -1;
    dr->blit_src = d->src_offset;  dr->blit_spitch = d->src_pitch;
    dr->blit_dst = d->dst_offset;  dr->blit_dpitch = d->dst_pitch;
    float x0 = (float)d->u0 / 16.0f + (float)sx, y0 = (float)d->v0 / 16.0f + (float)sy;
    dr->blit_uv[0] = x0;  dr->blit_uv[1] = y0;
    dr->blit_uv[2] = x0 + (float)d->out_w * fx;
    dr->blit_uv[3] = y0 + (float)d->out_h * fy;
    dr->blit_rect[0] = dx + d->out_x;  dr->blit_rect[1] = dy + d->out_y;
    dr->blit_rect[2] = d->out_w;       dr->blit_rect[3] = d->out_h;
    dr->rt_off = (di == -2) ? 0 : s_d3d.off_rt[di].off;
    s_d3d.merge_prev_draw = 0;
    s_wp_blits_rec++;
    return 1;
}

/* Execute side: called from the VP pass loop at the record's position. */
static void winport_blit_exec(const D3D12DrawRecord* dr, u32 fi, D3D12_CPU_DESCRIPTOR_HANDLE bb_rtv)
{
    u32 sx, sy, dx, dy;
    int si = wp_surface_at(dr->blit_src, dr->blit_spitch, &sx, &sy);
    int di = wp_surface_at(dr->blit_dst, dr->blit_dpitch, &dx, &dy);
    if (si == -1 || di == -1 || si == di || !wp_blit_init() ||
        s_wp_blit_srv_next >= WP_BLIT_SRV_N) { s_wp_blits_skip++; return; }
    ID3D12GraphicsCommandList* cl = s_d3d.cmd_list;
    ID3D12Resource* src; DXGI_FORMAT sfmt; float sw, sh;
    if (si == -2) {
        if (!s_wp_bbcopy) {
            D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC td = {0};
            td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            td.Width = WP68_BW; td.Height = WP68_BH; td.DepthOrArraySize = 1;   /* winport v68 */
            td.MipLevels = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
            if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp,
                    D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                    &IID_ID3D12Resource, (void**)&s_wp_bbcopy))) { s_wp_bbcopy = NULL; s_wp_blits_skip++; return; }
            s_wp_bbcopy_st = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        D3D12_RESOURCE_BARRIER b[2];
        memset(b, 0, sizeof b);
        b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b[1] = b[0];
        b[0].Transition.pResource = s_d3d.render_targets[fi];
        b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        int nb = 1;
        if (s_wp_bbcopy_st != D3D12_RESOURCE_STATE_COPY_DEST) {
            b[1].Transition.pResource = s_wp_bbcopy;
            b[1].Transition.StateBefore = s_wp_bbcopy_st;
            b[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
            nb = 2;
        }
        cl->lpVtbl->ResourceBarrier(cl, nb, b);
        D3D12_TEXTURE_COPY_LOCATION cd = {0}, cs = {0};
        cd.pResource = s_wp_bbcopy;             cd.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cs.pResource = s_d3d.render_targets[fi]; cs.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cl->lpVtbl->CopyTextureRegion(cl, &cd, 0, 0, 0, &cs, NULL);
        b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b[1].Transition.pResource = s_wp_bbcopy;
        b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        cl->lpVtbl->ResourceBarrier(cl, 2, b);
        s_wp_bbcopy_st = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        src = s_wp_bbcopy; sfmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        sw = (float)s_d3d.width; sh = (float)s_d3d.height;
    } else {
        off_rt_transition(si, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        src = s_d3d.off_rt[si].res; sfmt = (DXGI_FORMAT)s_d3d.off_rt[si].dxgi;
        sw = (float)s_d3d.off_rt[si].w; sh = (float)s_d3d.off_rt[si].h;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv; DXGI_FORMAT dfmt;
    if (di == -2) { rtv = bb_rtv; dfmt = DXGI_FORMAT_R8G8B8A8_UNORM; }
    else {
        off_rt_transition(di, D3D12_RESOURCE_STATE_RENDER_TARGET);
        rtv = off_rt_rtv(di); dfmt = (DXGI_FORMAT)s_d3d.off_rt[di].dxgi;
    }
    ID3D12PipelineState* pso = wp_blit_pso(dfmt);
    if (!pso) { s_wp_blits_skip++; return; }
    u32 slot = WP_BLIT_SRV_BASE + s_wp_blit_srv_next++;
    srv_write(slot, src, sfmt, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    D3D12_GPU_DESCRIPTOR_HANDLE gh;
    s_d3d.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &gh);
    gh.ptr += (u64)slot * s_d3d.srv_inc;
    float uvr[4] = { dr->blit_uv[0] / sw, dr->blit_uv[1] / sh, dr->blit_uv[2] / sw, dr->blit_uv[3] / sh };
    cl->lpVtbl->SetGraphicsRootSignature(cl, s_wp_blit_rs);
    cl->lpVtbl->SetPipelineState(cl, pso);
    { ID3D12DescriptorHeap* hp[] = { s_d3d.srv_heap }; cl->lpVtbl->SetDescriptorHeaps(cl, 1, hp); }
    cl->lpVtbl->SetGraphicsRoot32BitConstants(cl, 0, 4, uvr, 0);
    cl->lpVtbl->SetGraphicsRootDescriptorTable(cl, 1, gh);
    cl->lpVtbl->OMSetRenderTargets(cl, 1, &rtv, FALSE, NULL);
    const u32 bx68 = wp68_px(dr->blit_rect[0]), by68 = wp68_px(dr->blit_rect[1]);   /* winport v68 */
    const u32 bw68 = wp68_px(dr->blit_rect[0] + dr->blit_rect[2]) - bx68, bh68 = wp68_px(dr->blit_rect[1] + dr->blit_rect[3]) - by68;
    if (s_wp68_s != 1.0f) s_wp68_n_blit++;
    D3D12_VIEWPORT vp = { (float)bx68, (float)by68,
                          (float)bw68, (float)bh68, 0.0f, 1.0f };
    D3D12_RECT sc = { (LONG)bx68, (LONG)by68,
                      (LONG)(bx68 + bw68), (LONG)(by68 + bh68) };
    cl->lpVtbl->RSSetViewports(cl, 1, &vp);
    cl->lpVtbl->RSSetScissorRects(cl, 1, &sc);
    cl->lpVtbl->IASetPrimitiveTopology(cl, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cl->lpVtbl->DrawInstanced(cl, 4, 1, 0, 0);
    s_wp_blits_exec++;
}

/* winport-fix: host movie overlay */
static SRWLOCK s_wp_mv_lock = SRWLOCK_INIT;
static u8* s_wp_mv_buf;
static unsigned s_wp_mv_w, s_wp_mv_h;
static volatile LONG s_wp_mv_new, s_wp_mv_active;
static ID3D12Resource* s_wp_mv_tex;
static ID3D12Resource* s_wp_mv_up;
static unsigned s_wp_mv_tw, s_wp_mv_th, s_wp_mv_pitch;
static D3D12_RESOURCE_STATES s_wp_mv_st;
/* winport v73-moviesolo: WINPORT_MOVIE_SOLO=1 -> wall-clock movies presented on arrival (fix_v73_movie_solo.py) */
static LARGE_INTEGER s_wp73_lp;   /* QPC of the last present */
static volatile ULONGLONG s_wp_last_render_ms;   /* tentative declaration; defined (same type) further down */
extern volatile int g_wp73_movie_wallclock;
static int wp73_solo_due(void)
{
    static int on = -1; static LONGLONG gap = 0; static LARGE_INTEGER fq;
    if (on < 0) { const char* e = getenv("WINPORT_MOVIE_SOLO"); on = (e && e[0] == '1') ? 1 : 0;
                  const char* m = getenv("WINPORT_MOVIE_SOLO_MS"); int ms = m ? atoi(m) : 8; if (ms < 1) ms = 1;
                  QueryPerformanceFrequency(&fq); gap = fq.QuadPart * ms / 1000;
                  if (on) { fprintf(stderr, "[SOLO73] wall-clock movies presented on arrival (min gap %d ms)\n", ms); fflush(stderr); } }
    if (!on || !g_wp73_movie_wallclock) return GetTickCount64() - s_wp_last_render_ms > 150;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    return now.QuadPart - s_wp73_lp.QuadPart > gap;
}
/* winport v72-moviepace: WINPORT_MOVIE_PACE=1 -> movie frames queued, one per presented frame (fix_v72_movie_pace_renderer.py) */
#define WP72_Q 8
static u8* s_wp72_q[WP72_Q]; static unsigned s_wp72_qw[WP72_Q], s_wp72_qh[WP72_Q];
static int s_wp72_head, s_wp72_n, s_wp72_pop_ok, s_wp72_qmax; static float s_wp72_ema; static double s_wp72_maxgap;
static long s_wp72_shown, s_wp72_rep, s_wp72_drop; static ULONGLONG s_wp72_t;
static int wp72_on(void)
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_MOVIE_PACE"); v = (e && e[0] == '1') ? 1 : 0;
    if (v) { fprintf(stderr, "[PACE72] movie frames advance one per presented frame (queue %d)\n", WP72_Q); fflush(stderr); } }
  return v; }
/* called with s_wp_mv_lock held, before the overlay reads s_wp_mv_buf */
static void wp72_take(void)
{
    if (s_wp72_pop_ok && s_wp72_n) {
        static float lag = -1.0f; if (lag < 0.0f) { const char* e = getenv("WINPORT_MOVIE_PACE_LAG"); lag = e ? (float)atof(e) : 1.5f; if (lag < 0.5f) lag = 0.5f; }
        if (s_wp72_ema > lag && s_wp72_n >= 2) { s_wp72_head = (s_wp72_head + 1) % WP72_Q; s_wp72_n--; s_wp72_drop++; s_wp72_ema -= 1.0f; }   /* sustained lag only: bursts are absorbed */
        const int i = s_wp72_head; const unsigned w = s_wp72_qw[i], h = s_wp72_qh[i];
        if (!s_wp_mv_buf || w != s_wp_mv_w || h != s_wp_mv_h) { free(s_wp_mv_buf); s_wp_mv_buf = (u8*)malloc((size_t)w * h * 4); s_wp_mv_w = w; s_wp_mv_h = h; }
        if (s_wp_mv_buf && s_wp72_q[i]) { memcpy(s_wp_mv_buf, s_wp72_q[i], (size_t)w * h * 4); s_wp_mv_new = 1; s_wp72_shown++;
            static LARGE_INTEGER fq, last; LARGE_INTEGER now; if (!fq.QuadPart) QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&now);
            if (last.QuadPart) { const double g = (double)(now.QuadPart - last.QuadPart) * 1000.0 / (double)fq.QuadPart; if (g > s_wp72_maxgap) s_wp72_maxgap = g; }
            last = now; }
        s_wp72_head = (s_wp72_head + 1) % WP72_Q; s_wp72_n--;
    } else if (s_wp72_pop_ok) s_wp72_rep++;
    if (s_wp72_pop_ok) {
        s_wp72_ema = s_wp72_ema * 0.97f + (float)s_wp72_n * 0.03f;
        const ULONGLONG now = GetTickCount64();
        if (!s_wp72_t) s_wp72_t = now;
        if (now - s_wp72_t >= 1000) {
            fprintf(stderr, "[PACE72] shown=%ld repeated=%ld dropped=%ld max_queue=%d avg_queue=%.2f max_gap=%.0fms (per %.1f s)\n",
                    s_wp72_shown, s_wp72_rep, s_wp72_drop, s_wp72_qmax, (double)s_wp72_ema, s_wp72_maxgap, (now - s_wp72_t) / 1000.0);
            s_wp72_shown = s_wp72_rep = s_wp72_drop = 0; s_wp72_qmax = 0; s_wp72_maxgap = 0.0; s_wp72_t = now;
        }
    }
}
static void winport_movie_present_only(void);   /* winport-fix: movie present without guest flips */
static volatile ULONGLONG s_wp_last_render_ms;
void rsx_d3d12_movie_frame(const void* rgba, unsigned w, unsigned h)
{
    static int en = -1;
    if (en < 0) { const char* e = getenv("WINPORT_MOVIE_OVERLAY"); en = (e && *e == '0') ? 0 : 1; }
    if (!en || !rgba || !w || !h || w > 4096 || h > 4096) return;
    AcquireSRWLockExclusive(&s_wp_mv_lock);
    if (wp72_on()) {   /* winport v72-moviepace: queue the frame; the overlay takes one per presented frame */
        if (s_wp72_n == WP72_Q) { s_wp72_head = (s_wp72_head + 1) % WP72_Q; s_wp72_n--; s_wp72_drop++; }
        const int i = (s_wp72_head + s_wp72_n) % WP72_Q;
        if (!s_wp72_q[i] || s_wp72_qw[i] != w || s_wp72_qh[i] != h) { free(s_wp72_q[i]); s_wp72_q[i] = (u8*)malloc((size_t)w * h * 4); s_wp72_qw[i] = w; s_wp72_qh[i] = h; }
        if (s_wp72_q[i]) { memcpy(s_wp72_q[i], rgba, (size_t)w * h * 4); s_wp72_n++; if (s_wp72_n > s_wp72_qmax) s_wp72_qmax = s_wp72_n; }
        if (!s_wp_mv_active) printf("[WPMOVIE] overlay on (%ux%u)\n", w, h);
        s_wp_mv_active = 1;
        if (!s_wp_mv_buf) { s_wp72_pop_ok = 1; wp72_take(); s_wp72_pop_ok = 0; }   /* first frame: show at once */
    } else {
    if (!s_wp_mv_buf || w != s_wp_mv_w || h != s_wp_mv_h) {
        free(s_wp_mv_buf);
        s_wp_mv_buf = (u8*)malloc((size_t)w * h * 4);
        s_wp_mv_w = w; s_wp_mv_h = h;
    }
    if (s_wp_mv_buf) {
        memcpy(s_wp_mv_buf, rgba, (size_t)w * h * 4);
        s_wp_mv_new = 1;
        if (!s_wp_mv_active) printf("[WPMOVIE] overlay on (%ux%u)\n", w, h);
        s_wp_mv_active = 1;
    }
    }   /* winport v72-moviepace */
    ReleaseSRWLockExclusive(&s_wp_mv_lock);
    if (s_d3d.initialized && wp73_solo_due()) {   /* winport-fix: movie present without guest flips (winport v73-moviesolo) */
        extern void ppu_giant_lock_acquire(void);
        extern void ppu_giant_lock_release(void);
        extern int wp59_async_rsx_on(void); extern int wp59_rsx_lock(void); extern void wp59_rsx_unlock(int);
        const int a59 = wp59_async_rsx_on();   /* winport v59-async-rsx: the RSX lock, not the giant lock, guards D3D12 */
        const int tk59 = a59 ? wp59_rsx_lock() : 0;
        if (!a59) ppu_giant_lock_acquire();
        if (wp73_solo_due()) winport_movie_present_only();   /* winport v73-moviesolo */
        if (!a59) ppu_giant_lock_release();
        wp59_rsx_unlock(tk59);
    }
}
void rsx_d3d12_movie_playback_begin(void) {}
void rsx_d3d12_movie_playback_end(void) { s_wp_mv_active = 0; }
void rsx_d3d12_movie_stop(void)
{
    if (s_wp_mv_active) printf("[WPMOVIE] overlay off\n");
    s_wp_mv_active = 0;
}
static void winport_movie_overlay(u32 fi, D3D12_CPU_DESCRIPTOR_HANDLE bb_rtv)
{
    if (!s_wp_mv_active || !wp_blit_init()) return;
    ID3D12GraphicsCommandList* cl = s_d3d.cmd_list;
    int upload = 0;
    AcquireSRWLockExclusive(&s_wp_mv_lock);
    if (wp72_on()) wp72_take();   /* winport v72-moviepace */
    unsigned w = s_wp_mv_w, h = s_wp_mv_h;
    if (!s_wp_mv_buf || !w || !h) { ReleaseSRWLockExclusive(&s_wp_mv_lock); return; }
    if (!s_wp_mv_tex || w != s_wp_mv_tw || h != s_wp_mv_th) {
        /* the GPU is idle here for anything older than this frame (wait at frame start) */
        wp77_idle(5);   /* winport v77-fif: not any more with 2 frames in flight */
        if (s_wp_mv_tex) { s_wp_mv_tex->lpVtbl->Release(s_wp_mv_tex); s_wp_mv_tex = NULL; }
        if (s_wp_mv_up)  { s_wp_mv_up->lpVtbl->Release(s_wp_mv_up);   s_wp_mv_up = NULL; }
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = {0};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = w; td.Height = h; td.DepthOrArraySize = 1; td.MipLevels = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        s_wp_mv_pitch = (w * 4u + 255u) & ~255u;
        D3D12_HEAP_PROPERTIES hu = {0}; hu.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd = {0};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = (u64)s_wp_mv_pitch * h;
        bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE,
                &td, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&s_wp_mv_tex)) ||
            FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hu, D3D12_HEAP_FLAG_NONE,
                &bd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&s_wp_mv_up))) {
            if (s_wp_mv_tex) { s_wp_mv_tex->lpVtbl->Release(s_wp_mv_tex); s_wp_mv_tex = NULL; }
            if (s_wp_mv_up)  { s_wp_mv_up->lpVtbl->Release(s_wp_mv_up);   s_wp_mv_up = NULL; }
            ReleaseSRWLockExclusive(&s_wp_mv_lock);
            return;
        }
        s_wp_mv_st = D3D12_RESOURCE_STATE_COPY_DEST;
        s_wp_mv_tw = w; s_wp_mv_th = h;
        s_wp_mv_new = 1;
    }
    if (s_wp_mv_new) {
        void* m = NULL; D3D12_RANGE nr = {0, 0};
        if (wp77_on()) { wp77_wait(s77_mv_use, 3); s77_mv_use = s77_val + 1; }   /* winport v77-fif */
        if (SUCCEEDED(s_wp_mv_up->lpVtbl->Map(s_wp_mv_up, 0, &nr, &m)) && m) {
            for (unsigned y = 0; y < h; y++)
                memcpy((u8*)m + (size_t)y * s_wp_mv_pitch, s_wp_mv_buf + (size_t)y * w * 4u, (size_t)w * 4u);
            s_wp_mv_up->lpVtbl->Unmap(s_wp_mv_up, 0, NULL);
            upload = 1;
        }
        s_wp_mv_new = 0;
    }
    ReleaseSRWLockExclusive(&s_wp_mv_lock);
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = s_wp_mv_tex;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (upload) {
        if (s_wp_mv_st != D3D12_RESOURCE_STATE_COPY_DEST) {
            b.Transition.StateBefore = s_wp_mv_st; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            cl->lpVtbl->ResourceBarrier(cl, 1, &b);
            s_wp_mv_st = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        D3D12_TEXTURE_COPY_LOCATION cd = {0}, cs = {0};
        cd.pResource = s_wp_mv_tex; cd.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cs.pResource = s_wp_mv_up;  cs.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        cs.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        cs.PlacedFootprint.Footprint.Width = w; cs.PlacedFootprint.Footprint.Height = h;
        cs.PlacedFootprint.Footprint.Depth = 1; cs.PlacedFootprint.Footprint.RowPitch = s_wp_mv_pitch;
        cl->lpVtbl->CopyTextureRegion(cl, &cd, 0, 0, 0, &cs, NULL);
    }
    if (s_wp_mv_st != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        b.Transition.StateBefore = s_wp_mv_st; b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        cl->lpVtbl->ResourceBarrier(cl, 1, &b);
        s_wp_mv_st = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    ID3D12PipelineState* pso = wp_blit_pso(DXGI_FORMAT_R8G8B8A8_UNORM);
    if (!pso || s_wp_blit_srv_next >= WP_BLIT_SRV_N) return;
    u32 slot = WP_BLIT_SRV_BASE + s_wp_blit_srv_next++;
    srv_write(slot, s_wp_mv_tex, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    D3D12_GPU_DESCRIPTOR_HANDLE gh;
    s_d3d.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &gh);
    gh.ptr += (u64)slot * s_d3d.srv_inc;
    /* fit the movie inside the backbuffer, keeping its aspect */
    float bw = (float)WP68_BW, bh = (float)WP68_BH;   /* winport v68: back-buffer pixels */
    float sc = bw / (float)w < bh / (float)h ? bw / (float)w : bh / (float)h;
    float vw = (float)w * sc, vh = (float)h * sc;
    wp94_ff_fit(bw, bh, (float)w, (float)h, &vw, &vh);   /* winport v94a-ffhaze: Full Frame -> fill */
    float uvr[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    cl->lpVtbl->SetGraphicsRootSignature(cl, s_wp_blit_rs);
    cl->lpVtbl->SetPipelineState(cl, pso);
    { ID3D12DescriptorHeap* hps[] = { s_d3d.srv_heap }; cl->lpVtbl->SetDescriptorHeaps(cl, 1, hps); }
    cl->lpVtbl->SetGraphicsRoot32BitConstants(cl, 0, 4, uvr, 0);
    cl->lpVtbl->SetGraphicsRootDescriptorTable(cl, 1, gh);
    cl->lpVtbl->OMSetRenderTargets(cl, 1, &bb_rtv, FALSE, NULL);
    const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    cl->lpVtbl->ClearRenderTargetView(cl, bb_rtv, black, 0, NULL);
    D3D12_VIEWPORT vp = { (bw - vw) * 0.5f, (bh - vh) * 0.5f, vw, vh, 0.0f, 1.0f };
    D3D12_RECT scr = { 0, 0, (LONG)bw, (LONG)bh };
    cl->lpVtbl->RSSetViewports(cl, 1, &vp);
    cl->lpVtbl->RSSetScissorRects(cl, 1, &scr);
    cl->lpVtbl->IASetPrimitiveTopology(cl, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cl->lpVtbl->DrawInstanced(cl, 4, 1, 0, 0);
    (void)fi;
}
/* ---- winport v49-cutscene-style: God-of-War styled cutscene UI + native-pause compositing (fix_v49_cutscene_style_renderer.py) ----
 * State from cellPad.c (engine_fixes_v47_cutscene_ui.py / _v49_native_pause.py). CPU-drawn premultiplied RGBA panels
 * -> one texture (region A = bottom-right stack, region B = centred PAUSED title) -> alpha blend. Paused: the frame is
 * dimmed with a blend-factor pass; native pause: the game's own frame (copied before the movie overlay) is laid over
 * the dimmed movie with a MAX blend so the game's pause menu is visible. (Old v47 names kept: wp47_ui_overlay.) */
extern int wp47_cutscene_ui_snapshot(float* prompt_a, float* progress, int* paused, float* note_a);
extern int wp49_cutscene_native_paused(void);
typedef struct { u8* m; int w, h; } Wp47Txt;
enum { T49_HOLD, T49_TOSKIP, T49_PAUSED, T49_HINT, T49_NOTE, T49_NHINT, T49_N };
static Wp47Txt s_wp47_txt[T49_N];
static int s_wp47_txt_key = 0;
static u8* s_wp47_cv; static int s_wp47_cv_cap;
static int s_wp47_cw, s_wp47_ch;
static ID3D12Resource* s_wp47_tex;
static ID3D12Resource* s_wp47_up;
static int s_wp47_tw, s_wp47_th;
static unsigned s_wp47_pitch;
static D3D12_RESOURCE_STATES s_wp47_st;
static ID3D12PipelineState* s_wp47_pso;     /* premultiplied alpha */
static ID3D12PipelineState* s_wp49_dim_pso; /* dest * blend factor */
static ID3D12PipelineState* s_wp49_max_pso; /* max(dest, src) */
static int s_wp47_broken;
static ID3D12Resource* s_wp49_gf;           /* copy of the game's frame (native pause) */
static D3D12_RESOURCE_STATES s_wp49_gf_st;
static u32 s_wp49_gfw, s_wp49_gfh;
static int s_wp49_grabbed;

static void wp47_txt_make(Wp47Txt* t, const char* str, int px, int weight, int spacing)
{
    free(t->m); t->m = NULL; t->w = t->h = 0;
    HDC dc = CreateCompatibleDC(NULL);
    if (!dc) return;
    HFONT f = CreateFontA(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                          CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_ROMAN, "Palatino Linotype");
    HGDIOBJ of = f ? SelectObject(dc, f) : NULL;
    SetTextCharacterExtra(dc, spacing);
    const int len = (int)strlen(str);
    SIZE sz; sz.cx = sz.cy = 0;
    GetTextExtentPoint32A(dc, str, len, &sz);
    const int w = (int)sz.cx + 6, h = (int)sz.cy + 4;
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bm = (w > 6 && h > 4 && w < 4096 && h < 1024) ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0) : NULL;
    if (bm && bits) {
        HGDIOBJ ob = SelectObject(dc, bm);
        memset(bits, 0, (size_t)w * h * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutA(dc, 3, 2, str, len);
        GdiFlush();
        t->m = (u8*)malloc((size_t)w * h);
        if (t->m) {
            const u8* p = (const u8*)bits;
            for (int i = 0; i < w * h; i++) {
                u8 a = p[i * 4]; if (p[i * 4 + 1] > a) a = p[i * 4 + 1]; if (p[i * 4 + 2] > a) a = p[i * 4 + 2];
                t->m[i] = a;
            }
            t->w = w; t->h = h;
        }
        SelectObject(dc, ob);
        DeleteObject(bm);
    }
    if (of) SelectObject(dc, of);
    if (f) DeleteObject(f);
    DeleteDC(dc);
}

static void wp47_px(int x, int y, float r, float g, float b, float a)   /* premultiplied "over" */
{
    if (x < 0 || y < 0 || x >= s_wp47_cw || y >= s_wp47_ch || a <= 0.0f) return;
    if (a > 1.0f) a = 1.0f;
    u8* p = s_wp47_cv + ((size_t)y * s_wp47_cw + x) * 4;
    const float k = 1.0f - a;
    float v;
    v = r * a * 255.0f + p[0] * k; p[0] = (u8)(v > 255.0f ? 255.0f : v + 0.5f);
    v = g * a * 255.0f + p[1] * k; p[1] = (u8)(v > 255.0f ? 255.0f : v + 0.5f);
    v = b * a * 255.0f + p[2] * k; p[2] = (u8)(v > 255.0f ? 255.0f : v + 0.5f);
    v = a * 255.0f + p[3] * k;     p[3] = (u8)(v > 255.0f ? 255.0f : v + 0.5f);
}

/* rounded rectangle, vertical colour gradient c0 (top) -> c1 (bottom) */
static void wp49_rrect(float x0, float y0, float x1, float y1, float rad, const float* c0, const float* c1, float a)
{
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, hx = (x1 - x0) * 0.5f - rad, hy = (y1 - y0) * 0.5f - rad;
    for (int y = (int)floorf(y0); y < (int)ceilf(y1); y++) {
        float t = (y + 0.5f - y0) / (y1 - y0); if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
        const float r = c0[0] + (c1[0] - c0[0]) * t, g = c0[1] + (c1[1] - c0[1]) * t, b = c0[2] + (c1[2] - c0[2]) * t;
        for (int x = (int)floorf(x0); x < (int)ceilf(x1); x++) {
            float qx = fabsf(x + 0.5f - cx) - hx, qy = fabsf(y + 0.5f - cy) - hy;
            const float ox = qx > 0.0f ? qx : 0.0f, oy = qy > 0.0f ? qy : 0.0f;
            const float mx = qx > qy ? qx : qy;
            const float d = sqrtf(ox * ox + oy * oy) + (mx < 0.0f ? mx : 0.0f) - rad;
            float c = 0.5f - d; if (c <= 0.0f) continue; if (c > 1.0f) c = 1.0f;
            wp47_px(x, y, r, g, b, a * c);
        }
    }
}

static const float WP49_GOLD_T[3] = { 0.99f, 0.89f, 0.60f }, WP49_GOLD_B[3] = { 0.76f, 0.53f, 0.20f };
static const float WP49_FRAME[3]  = { 0.80f, 0.62f, 0.27f };
static const float WP49_RED_T[3]  = { 0.20f, 0.035f, 0.03f }, WP49_RED_B[3] = { 0.055f, 0.0f, 0.0f };

/* God-of-War panel: gold frame + dark red gradient */
static void wp49_panel(float x0, float y0, float x1, float y1, float k, float a)
{
    const float bw = 1.5f * k, rad = 5.0f * k;
    wp49_rrect(x0, y0, x1, y1, rad, WP49_FRAME, WP49_GOLD_B, 0.95f * a);
    wp49_rrect(x0 + bw, y0 + bw, x1 - bw, y1 - bw, rad - bw > 1.0f ? rad - bw : 1.0f, WP49_RED_T, WP49_RED_B, 0.93f * a);
}

/* engraved gold text: dark shadow + vertical gold gradient (gain < 1 = paler/smaller emphasis) */
static void wp49_text(const Wp47Txt* t, int x0, int y0, float a, float k)
{
    if (!t->m) return;
    const int so = k >= 1.4f ? 2 : 1;
    for (int y = 0; y < t->h; y++)
        for (int x = 0; x < t->w; x++) {
            const u8 m = t->m[y * t->w + x];
            if (m) wp47_px(x0 + x + so, y0 + y + so, 0.0f, 0.0f, 0.0f, 0.85f * a * (float)m / 255.0f);
        }
    for (int y = 0; y < t->h; y++) {
        const float u = (float)y / (float)(t->h > 1 ? t->h - 1 : 1);
        const float r = WP49_GOLD_T[0] + (WP49_GOLD_B[0] - WP49_GOLD_T[0]) * u;
        const float g = WP49_GOLD_T[1] + (WP49_GOLD_B[1] - WP49_GOLD_T[1]) * u;
        const float b = WP49_GOLD_T[2] + (WP49_GOLD_B[2] - WP49_GOLD_T[2]) * u;
        for (int x = 0; x < t->w; x++) {
            const u8 m = t->m[y * t->w + x];
            if (m) wp47_px(x0 + x, y0 + y, r, g, b, a * (float)m / 255.0f);
        }
    }
}

/* horizontal gold rule fading towards its outer end, with a small diamond at the inner end */
static void wp49_rule(float x0, float x1, float y, float k, float a, int fade_left)
{
    for (int x = (int)x0; x < (int)x1; x++) {
        float t = (x - x0) / (x1 - x0); if (fade_left) t = t; else t = 1.0f - t;
        wp47_px(x, (int)y, WP49_FRAME[0], WP49_FRAME[1], WP49_FRAME[2], a * t);
    }
    {   const float dx = fade_left ? x1 + 3.0f * k : x0 - 3.0f * k, r = 3.0f * k;
        for (int yy = (int)(y - r - 1); yy <= (int)(y + r + 1); yy++)
            for (int xx = (int)(dx - r - 1); xx <= (int)(dx + r + 1); xx++) {
                float c = r + 0.5f - (fabsf(xx + 0.5f - dx) + fabsf(yy + 0.5f - y));
                if (c > 0.0f) wp47_px(xx, yy, WP49_GOLD_T[0], WP49_GOLD_T[1], WP49_GOLD_T[2], a * (c > 1.0f ? 1.0f : c));
            } }
}

/* round START button (dark disc, gold rim, pale play triangle) with a gold progress ring, 12 o'clock clockwise */
static void wp47_button_ring(float cx, float cy, float ro, float th, float rb, float prog, float a)
{
    const float rm = ro - th * 0.5f, two_pi = 6.28318531f;
    const float ax = cx - 0.32f * rb, ay0 = cy - 0.46f * rb, ay1 = cy + 0.46f * rb, bx = cx + 0.52f * rb;
    for (int y = (int)floorf(cy - ro - 1.0f); y <= (int)ceilf(cy + ro + 1.0f); y++)
        for (int x = (int)floorf(cx - ro - 1.0f); x <= (int)ceilf(cx + ro + 1.0f); x++) {
            const float px = x + 0.5f, py = y + 0.5f, dx = px - cx, dy = py - cy;
            const float d = sqrtf(dx * dx + dy * dy);
            float c = rb + 0.5f - d;   /* disc */
            if (c > 0.0f) wp47_px(x, y, 0.06f, 0.02f, 0.02f, a * 0.95f * (c > 1.0f ? 1.0f : c));
            { float rc2 = 1.0f + 0.5f - fabsf(d - (rb - 0.5f));   /* gold rim of the button */
              if (rc2 > 0.0f) wp47_px(x, y, WP49_FRAME[0], WP49_FRAME[1], WP49_FRAME[2], a * 0.9f * (rc2 > 1.0f ? 1.0f : rc2)); }
            {
                float e1 = px - ax;
                const float ux = bx - ax, uy = cy - ay0, ul = sqrtf(ux * ux + uy * uy);
                float e2 = -((px - ax) * uy - (py - ay0) * ux) / ul;
                const float vx = bx - ax, vy = cy - ay1, vl = sqrtf(vx * vx + vy * vy);
                float e3 = ((px - ax) * vy - (py - ay1) * vx) / vl;
                float m = e1 < e2 ? e1 : e2; if (e3 < m) m = e3;
                float tc = m + 0.5f;
                if (tc > 0.0f) wp47_px(x, y, 0.97f, 0.92f, 0.80f, a * (tc > 1.0f ? 1.0f : tc));
            }
            float rc = th * 0.5f + 0.5f - fabsf(d - rm);   /* ring */
            if (rc <= 0.0f) continue;
            if (rc > 1.0f) rc = 1.0f;
            float ang = atan2f(dx, -dy); if (ang < 0.0f) ang += two_pi;
            if (prog > 0.0f && ang / two_pi <= prog) {
                const float u = (dy + ro) / (2.0f * ro);
                wp47_px(x, y, WP49_GOLD_T[0] + (WP49_GOLD_B[0] - WP49_GOLD_T[0]) * u, WP49_GOLD_T[1] + (WP49_GOLD_B[1] - WP49_GOLD_T[1]) * u,
                        WP49_GOLD_T[2] + (WP49_GOLD_B[2] - WP49_GOLD_T[2]) * u, a * rc);
            } else wp47_px(x, y, 0.33f, 0.20f, 0.08f, a * rc * 0.85f);
        }
}

static ID3D12PipelineState* wp49_make_pso(int mode)   /* 0 premultiplied alpha, 1 dim (dest*factor), 2 max */
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd;
    ID3D12PipelineState* p = NULL;
    memset(&pd, 0, sizeof pd);
    pd.pRootSignature = s_wp_blit_rs;
    pd.VS.pShaderBytecode = s_wp_blit_vs->lpVtbl->GetBufferPointer(s_wp_blit_vs);
    pd.VS.BytecodeLength  = s_wp_blit_vs->lpVtbl->GetBufferSize(s_wp_blit_vs);
    pd.PS.pShaderBytecode = s_wp_blit_ps->lpVtbl->GetBufferPointer(s_wp_blit_ps);
    pd.PS.BytecodeLength  = s_wp_blit_ps->lpVtbl->GetBufferSize(s_wp_blit_ps);
    D3D12_RENDER_TARGET_BLEND_DESC* bd = &pd.BlendState.RenderTarget[0];
    bd->BlendEnable = TRUE;
    bd->LogicOp = D3D12_LOGIC_OP_NOOP;
    bd->RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (mode == 0) {
        bd->SrcBlend = D3D12_BLEND_ONE; bd->DestBlend = D3D12_BLEND_INV_SRC_ALPHA; bd->BlendOp = D3D12_BLEND_OP_ADD;
        bd->SrcBlendAlpha = D3D12_BLEND_ONE; bd->DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA; bd->BlendOpAlpha = D3D12_BLEND_OP_ADD;
    } else if (mode == 1) {
        bd->SrcBlend = D3D12_BLEND_ZERO; bd->DestBlend = D3D12_BLEND_BLEND_FACTOR; bd->BlendOp = D3D12_BLEND_OP_ADD;
        bd->SrcBlendAlpha = D3D12_BLEND_ZERO; bd->DestBlendAlpha = D3D12_BLEND_ONE; bd->BlendOpAlpha = D3D12_BLEND_OP_ADD;
    } else {
        bd->SrcBlend = D3D12_BLEND_ONE; bd->DestBlend = D3D12_BLEND_ONE; bd->BlendOp = D3D12_BLEND_OP_MAX;
        bd->SrcBlendAlpha = D3D12_BLEND_ONE; bd->DestBlendAlpha = D3D12_BLEND_ONE; bd->BlendOpAlpha = D3D12_BLEND_OP_MAX;
    }
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.DepthStencilState.StencilEnable = FALSE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;   /* swap chain format */
    pd.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pd.SampleDesc.Count = 1;
    if (FAILED(s_d3d.device->lpVtbl->CreateGraphicsPipelineState(s_d3d.device, &pd, &IID_ID3D12PipelineState, (void**)&p)) || !p) {
        fprintf(stderr, "[CSUI] renderer: PSO mode %d failed -> cutscene UI not drawn\n", mode); fflush(stderr);
        return NULL;
    }
    return p;
}
static int wp47_psos(void)
{
    if (s_wp47_broken) return 0;
    if (!s_wp47_pso) {
        s_wp47_pso = wp49_make_pso(0); s_wp49_dim_pso = wp49_make_pso(1); s_wp49_max_pso = wp49_make_pso(2);
        if (!s_wp47_pso || !s_wp49_dim_pso || !s_wp49_max_pso) { s_wp47_broken = 1; return 0; }
        fprintf(stderr, "[CSUI] renderer: overlay pipelines ready (v49 style)\n"); fflush(stderr);
    }
    return 1;
}

/* render_frame, BEFORE the movie overlay clears the backbuffer: keep the game's frame for native pause */
static void wp49_grab_game_frame(u32 fi)
{
    s_wp49_grabbed = 0;
    if (!s_wp_mv_active || s_wp47_broken || !s_d3d.cmd_list || !wp49_cutscene_native_paused()) return;
    ID3D12GraphicsCommandList* cl = s_d3d.cmd_list;
    ID3D12Resource* bb = s_d3d.render_targets[fi];
    if (!s_wp49_gf || s_wp49_gfw != WP68_BW || s_wp49_gfh != WP68_BH) {
        if (s_wp49_gf) { wp77_idle(5); s_wp49_gf->lpVtbl->Release(s_wp49_gf); s_wp49_gf = NULL; }   /* winport v77-fif */
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = {0};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = WP68_BW; td.Height = WP68_BH; td.DepthOrArraySize = 1; td.MipLevels = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
                D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&s_wp49_gf)) || !s_wp49_gf) {
            s_wp49_gf = NULL; return;
        }
        s_wp49_gf_st = D3D12_RESOURCE_STATE_COPY_DEST;
        s_wp49_gfw = WP68_BW; s_wp49_gfh = WP68_BH;
    }
    D3D12_RESOURCE_BARRIER b[2];
    memset(b, 0, sizeof b);
    b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b[0].Transition.pResource = bb;
    b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    int nb = 1;
    if (s_wp49_gf_st != D3D12_RESOURCE_STATE_COPY_DEST) {
        b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b[1].Transition.pResource = s_wp49_gf;
        b[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b[1].Transition.StateBefore = s_wp49_gf_st; b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        nb = 2;
    }
    cl->lpVtbl->ResourceBarrier(cl, (UINT)nb, b);
    cl->lpVtbl->CopyResource(cl, s_wp49_gf, bb);
    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE; b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b[1].Transition.pResource = s_wp49_gf;
    b[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cl->lpVtbl->ResourceBarrier(cl, 2, b);
    s_wp49_gf_st = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    s_wp49_grabbed = 1;
    { static int n_ = 0; if (n_++ < 2) { fprintf(stderr, "[CSUI49] renderer: game frame kept for the native pause composite\n"); fflush(stderr); } }
}

/* one full-screen or sub-rect textured quad with the blit root signature */
static void wp49_quad(D3D12_CPU_DESCRIPTOR_HANDLE bb_rtv, ID3D12PipelineState* pso, ID3D12Resource* tex,
                      const float uvr[4], float vx, float vy, float vw, float vh)
{
    ID3D12GraphicsCommandList* cl = s_d3d.cmd_list;
    if (s_wp_blit_srv_next >= WP_BLIT_SRV_N) return;
    u32 slot = WP_BLIT_SRV_BASE + s_wp_blit_srv_next++;
    srv_write(slot, tex, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    D3D12_GPU_DESCRIPTOR_HANDLE gh;
    s_d3d.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &gh);
    gh.ptr += (u64)slot * s_d3d.srv_inc;
    D3D12_VIEWPORT vp = { vx, vy, vw, vh, 0.0f, 1.0f };
    D3D12_RECT scr = { 0, 0, (LONG)WP68_BW, (LONG)WP68_BH };
    cl->lpVtbl->SetGraphicsRootSignature(cl, s_wp_blit_rs);
    cl->lpVtbl->SetPipelineState(cl, pso);
    { ID3D12DescriptorHeap* hps[] = { s_d3d.srv_heap }; cl->lpVtbl->SetDescriptorHeaps(cl, 1, hps); }
    cl->lpVtbl->SetGraphicsRoot32BitConstants(cl, 0, 4, uvr, 0);
    cl->lpVtbl->SetGraphicsRootDescriptorTable(cl, 1, gh);
    cl->lpVtbl->OMSetRenderTargets(cl, 1, &bb_rtv, FALSE, NULL);
    cl->lpVtbl->RSSetViewports(cl, 1, &vp);
    cl->lpVtbl->RSSetScissorRects(cl, 1, &scr);
    cl->lpVtbl->IASetPrimitiveTopology(cl, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cl->lpVtbl->DrawInstanced(cl, 4, 1, 0, 0);
}

/* ---- winport v81-ui (fix_v81_ui_renderer.py): prototypes (bodies before wp80_panel) ---- */
static int wp81_ui(void);
static void wp81_csui_draw(D3D12_CPU_DESCRIPTOR_HANDLE bb, float pa, float prog, int paused, int native, float na);
static void wp81_fps_draw(D3D12_CPU_DESCRIPTOR_HANDLE bb);
static void wp81_scan(void);
/* WINPORT_UI81_CSTEST=<ms>: visual test of the cutscene UI without a movie: from <ms> after the first frame, 4 s skip
 * prompt (ring filling), 4 s prompt + "not available" note, 4 s host pause screen */
static int wp81_cstest(float* pa, float* prog, int* paused, float* na)
{
    static int at = -1; static DWORD t0;
    if (at < 0) { const char* e = getenv("WINPORT_UI81_CSTEST"); at = (e && atoi(e) > 0) ? atoi(e) : 0; t0 = GetTickCount(); }
    if (!at) return 0;
    const DWORD t = GetTickCount() - t0;
    if (t < (DWORD)at || t >= (DWORD)at + 12000u) return 0;
    const DWORD r = t - (DWORD)at;
    *pa = r < 8000u ? 1.0f : 0.0f; *prog = r < 4000u ? (float)r / 4000.0f : 0.35f; *paused = r >= 8000u; *na = (r >= 4000u && r < 8000u) ? 1.0f : 0.0f;
    { static int n_ = 0; if (n_++ < 1) { fprintf(stderr, "[UI81] cutscene UI visual test running (WINPORT_UI81_CSTEST)\n"); fflush(stderr); } }
    return 1;
}
static void wp47_ui_overlay(D3D12_CPU_DESCRIPTOR_HANDLE bb_rtv)
{
    float pa = 0.0f, prog = 0.0f, na = 0.0f;
    int paused = 0;
    if (!wp47_cutscene_ui_snapshot(&pa, &prog, &paused, &na) && !wp81_cstest(&pa, &prog, &paused, &na)) { s_wp49_grabbed = 0; return; }   /* winport v81-ui */
    if (s_wp47_broken || !s_d3d.initialized || !s_d3d.cmd_list || !wp_blit_init() || !wp47_psos()) return;
    const int native = paused && wp49_cutscene_native_paused();
    ID3D12GraphicsCommandList* cl = s_d3d.cmd_list;
    float k = (float)WP68_BH / 720.0f;
    if (k < 0.75f) k = 0.75f;
    if (k > 4.0f) k = 4.0f;
    const int key = (int)(k * 100.0f);
    if (key != s_wp47_txt_key) {
        const int sp = (int)(1.6f * k + 0.5f);
        wp47_txt_make(&s_wp47_txt[T49_HOLD],   "HOLD",    (int)(22.0f * k), FW_BOLD, sp);
        wp47_txt_make(&s_wp47_txt[T49_TOSKIP], "TO SKIP", (int)(22.0f * k), FW_BOLD, sp);
        wp47_txt_make(&s_wp47_txt[T49_PAUSED], "PAUSED",  (int)(44.0f * k), FW_BOLD, (int)(5.0f * k + 0.5f));
        wp47_txt_make(&s_wp47_txt[T49_HINT],   "SELECT  RESUME     \xB7     HOLD START  SKIP", (int)(16.0f * k), FW_SEMIBOLD, sp);
        wp47_txt_make(&s_wp47_txt[T49_NOTE],   "PAUSE IS NOT AVAILABLE HERE", (int)(17.0f * k), FW_SEMIBOLD, sp);
        wp47_txt_make(&s_wp47_txt[T49_NHINT],  "SELECT  RESUME MOVIE", (int)(16.0f * k), FW_SEMIBOLD, sp);
        s_wp47_txt_key = key;
        fprintf(stderr, "[CSUI] renderer: v49 text made for scale %.2f\n", k); fflush(stderr);
    }
    const Wp47Txt* T = s_wp47_txt;
    /* ---- region A (bottom-right stack) layout ---- */
    const float pad = 16.0f * k, gap = 10.0f * k;
    const float ro = 19.0f * k, th = 4.0f * k, rb = 13.0f * k;
    const float pw0 = pad + (float)T[T49_HOLD].w + 10.0f * k + 2.0f * ro + 10.0f * k + (float)T[T49_TOSKIP].w + pad;
    const float ph0 = 58.0f * k;
    const float pw2 = 2.0f * pad + (float)T[T49_NOTE].w, ph2 = 44.0f * k;
    const float pw3 = 2.0f * pad + (float)T[T49_NHINT].w, ph3 = 40.0f * k;
    float WA = 0.0f, HA = 0.0f;
    if (pa > 0.0f) { WA = pw0; HA = ph0; }
    if (native) { if (pw3 > WA) WA = pw3; HA += (HA > 0.0f ? gap : 0.0f) + ph3; }
    if (na > 0.0f) { if (pw2 > WA) WA = pw2; HA += (HA > 0.0f ? gap : 0.0f) + ph2; }
    /* ---- region B (centred PAUSED title, host pause only) ---- */
    const int title = paused && !native;
    const float rule = 70.0f * k;
    const float WB = title ? 2.0f * pad + 2.0f * (rule + 14.0f * k) + (float)T[T49_PAUSED].w : 0.0f;
    const float WB2 = title ? 2.0f * pad + (float)T[T49_HINT].w : 0.0f;
    const float wB = WB > WB2 ? WB : WB2;
    const float HB = title ? pad + (float)T[T49_PAUSED].h + 6.0f * k + (float)T[T49_HINT].h + pad : 0.0f;
    const int wa = WA > 0.0f ? (int)ceilf(WA) + 2 : 0, ha = HA > 0.0f ? (int)ceilf(HA) + 2 : 0;
    const int wb = wB > 0.0f ? (int)ceilf(wB) + 2 : 0, hb = HB > 0.0f ? (int)ceilf(HB) + 2 : 0;
    const int cw = wa > wb ? wa : wb, ch = ha + hb;
    /* ---- dim + native composite (before our panels) ---- */
    if (paused && !(native && !s_wp_mv_active)) {   /* native pause over a game-decoded movie: the game's menu is already on screen */
        ID3D12Resource* any = s_wp49_grabbed ? s_wp49_gf : (s_wp_mv_tex ? s_wp_mv_tex : s_wp47_tex);
        const float full[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
        if (any) {
            const float f[4] = { 0.42f, 0.42f, 0.42f, 1.0f };
            cl->lpVtbl->OMSetBlendFactor(cl, f);
            wp49_quad(bb_rtv, s_wp49_dim_pso, any, full, 0.0f, 0.0f, (float)WP68_BW, (float)WP68_BH);
        }
        if (native && s_wp49_grabbed)
            wp49_quad(bb_rtv, s_wp49_max_pso, s_wp49_gf, full, 0.0f, 0.0f, (float)WP68_BW, (float)WP68_BH);
    }
    s_wp49_grabbed = 0;
    if (wp81_ui()) { wp81_csui_draw(bb_rtv, pa, prog, paused, native, na); return; }   /* winport v81-ui */
    if (cw <= 0 || ch <= 0 || cw > 2048 || ch > 1536) return;
    if (cw * ch * 4 > s_wp47_cv_cap) {
        free(s_wp47_cv);
        s_wp47_cv_cap = cw * ch * 4;
        s_wp47_cv = (u8*)malloc((size_t)s_wp47_cv_cap);
        if (!s_wp47_cv) { s_wp47_cv_cap = 0; return; }
    }
    s_wp47_cw = cw; s_wp47_ch = ch;
    memset(s_wp47_cv, 0, (size_t)cw * ch * 4);
    if (ha) {   /* region A, right-aligned, stacked from its bottom */
        float yb = (float)ha - 1.0f;
        if (pa > 0.0f) {
            const float x0 = (float)wa - 1.0f - pw0, y0 = yb - ph0;
            float x = x0 + pad;
            wp49_panel(x0, y0, x0 + pw0, yb, k, pa);
            wp49_text(&T[T49_HOLD], (int)x, (int)(y0 + (ph0 - (float)T[T49_HOLD].h) * 0.5f), pa, k);
            x += (float)T[T49_HOLD].w + 10.0f * k;
            wp47_button_ring(x + ro, y0 + ph0 * 0.5f, ro, th, rb, prog, pa);
            x += 2.0f * ro + 10.0f * k;
            wp49_text(&T[T49_TOSKIP], (int)x, (int)(y0 + (ph0 - (float)T[T49_TOSKIP].h) * 0.5f), pa, k);
            yb = y0 - gap;
        }
        if (native) {
            const float x0 = (float)wa - 1.0f - pw3, y0 = yb - ph3;
            wp49_panel(x0, y0, x0 + pw3, yb, k, 1.0f);
            wp49_text(&T[T49_NHINT], (int)(x0 + pad), (int)(y0 + (ph3 - (float)T[T49_NHINT].h) * 0.5f), 1.0f, k);
            yb = y0 - gap;
        }
        if (na > 0.0f) {
            const float x0 = (float)wa - 1.0f - pw2, y0 = yb - ph2;
            wp49_panel(x0, y0, x0 + pw2, yb, k, na);
            wp49_text(&T[T49_NOTE], (int)(x0 + pad), (int)(y0 + (ph2 - (float)T[T49_NOTE].h) * 0.5f), na, k);
        }
    }
    if (hb) {   /* region B, centred in its own rows */
        const float oy = (float)ha, x0 = ((float)cw - wB) * 0.5f;
        const float tx = x0 + (wB - (float)T[T49_PAUSED].w) * 0.5f, ty = oy + pad;
        const float ry = ty + (float)T[T49_PAUSED].h * 0.52f;
        wp49_rule(tx - 14.0f * k - rule, tx - 14.0f * k, ry, k, 0.95f, 1);
        wp49_rule(tx + (float)T[T49_PAUSED].w + 14.0f * k, tx + (float)T[T49_PAUSED].w + 14.0f * k + rule, ry, k, 0.95f, 0);
        wp49_text(&T[T49_PAUSED], (int)tx, (int)ty, 1.0f, k);
        wp49_text(&T[T49_HINT], (int)(x0 + (wB - (float)T[T49_HINT].w) * 0.5f), (int)(ty + (float)T[T49_PAUSED].h + 6.0f * k), 0.9f, k);
    }
    /* ---- texture upload (grown in 64-px steps) ---- */
    if (!s_wp47_tex || cw > s_wp47_tw || ch > s_wp47_th) {
        wp77_idle(5);   /* winport v77-fif */
        if (s_wp47_tex) { s_wp47_tex->lpVtbl->Release(s_wp47_tex); s_wp47_tex = NULL; }
        if (s_wp47_up)  { s_wp47_up->lpVtbl->Release(s_wp47_up);   s_wp47_up = NULL; }
        const int tw = (cw + 63) & ~63, th2 = (ch + 63) & ~63;
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = {0};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = (UINT64)tw; td.Height = (UINT)th2; td.DepthOrArraySize = 1; td.MipLevels = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        s_wp47_pitch = ((unsigned)tw * 4u + 255u) & ~255u;
        D3D12_HEAP_PROPERTIES hu = {0}; hu.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd = {0};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = (u64)s_wp47_pitch * (u64)th2;
        bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE,
                &td, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&s_wp47_tex)) ||
            FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hu, D3D12_HEAP_FLAG_NONE,
                &bd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&s_wp47_up))) {
            if (s_wp47_tex) { s_wp47_tex->lpVtbl->Release(s_wp47_tex); s_wp47_tex = NULL; }
            if (s_wp47_up)  { s_wp47_up->lpVtbl->Release(s_wp47_up);   s_wp47_up = NULL; }
            s_wp47_broken = 1;
            fprintf(stderr, "[CSUI] renderer: texture allocation failed -> cutscene UI not drawn\n"); fflush(stderr);
            return;
        }
        s_wp47_st = D3D12_RESOURCE_STATE_COPY_DEST;
        s_wp47_tw = tw; s_wp47_th = th2;
    }
    {
        void* m = NULL; D3D12_RANGE nr = {0, 0};
        if (wp77_on()) { wp77_wait(s77_ui_use, 4); s77_ui_use = s77_val + 1; }   /* winport v77-fif */
        if (FAILED(s_wp47_up->lpVtbl->Map(s_wp47_up, 0, &nr, &m)) || !m) return;
        for (int y = 0; y < ch; y++)
            memcpy((u8*)m + (size_t)y * s_wp47_pitch, s_wp47_cv + (size_t)y * cw * 4u, (size_t)cw * 4u);
        s_wp47_up->lpVtbl->Unmap(s_wp47_up, 0, NULL);
    }
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = s_wp47_tex;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (s_wp47_st != D3D12_RESOURCE_STATE_COPY_DEST) {
        b.Transition.StateBefore = s_wp47_st; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        cl->lpVtbl->ResourceBarrier(cl, 1, &b);
        s_wp47_st = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    {
        D3D12_TEXTURE_COPY_LOCATION cd = {0}, cs = {0};
        cd.pResource = s_wp47_tex; cd.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cs.pResource = s_wp47_up;  cs.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        cs.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        cs.PlacedFootprint.Footprint.Width = (UINT)cw; cs.PlacedFootprint.Footprint.Height = (UINT)ch;
        cs.PlacedFootprint.Footprint.Depth = 1; cs.PlacedFootprint.Footprint.RowPitch = s_wp47_pitch;
        cl->lpVtbl->CopyTextureRegion(cl, &cd, 0, 0, 0, &cs, NULL);
    }
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cl->lpVtbl->ResourceBarrier(cl, 1, &b);
    s_wp47_st = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const float margin = 28.0f * k, tw = (float)s_wp47_tw, tht = (float)s_wp47_th;
    if (ha) {
        const float uvr[4] = { 0.0f, 0.0f, (float)wa / tw, (float)ha / tht };
        float vx = (float)WP68_BW - margin - (float)wa, vy = (float)WP68_BH - margin - (float)ha;
        if (vx < 0.0f) vx = 0.0f;
        if (vy < 0.0f) vy = 0.0f;
        wp49_quad(bb_rtv, s_wp47_pso, s_wp47_tex, uvr, floorf(vx), floorf(vy), (float)wa, (float)ha);
    }
    if (hb) {
        const float uvr[4] = { 0.0f, (float)ha / tht, (float)wb / tw, (float)(ha + hb) / tht };
        const float vx = floorf(((float)WP68_BW - (float)wb) * 0.5f), vy = floorf(((float)WP68_BH - (float)hb) * 0.5f);
        wp49_quad(bb_rtv, s_wp47_pso, s_wp47_tex, uvr, vx, vy, (float)wb, (float)hb);
    }
    { static int n_ = 0; if (n_++ < 2) { fprintf(stderr, "[CSUI] renderer: drew the v49 cutscene UI (A %dx%d, B %dx%d, prompt=%.2f ring=%.2f paused=%d native=%d)\n", wa, ha, wb, hb, pa, prog, paused, native); fflush(stderr); } }
}
/* ---- end winport v49-cutscene-style ---- */

/* ---- winport v69-fps-f11 (fix_v69_fps_overlay_f11.py): F10 FPS counter, F11 borderless toggle (key side) ---- */
static const unsigned char* wp69_glyph(char c)   /* 5x7, one byte per row, bit4 = leftmost */
{
    static const unsigned char G[][7] = {
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
        {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}, {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
        {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
        {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
        {0x1F,0x10,0x1E,0x10,0x10,0x10,0x10},   /* 10 F */
        {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},   /* 11 P */
        {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},   /* 12 S */
        {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},   /* 13 M */
        {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},   /* 14 . */
    };
    static const unsigned char Z[7] = {0};
    if (c >= '0' && c <= '9') return G[c - '0'];
    switch (c) { case 'F': return G[10]; case 'P': return G[11]; case 'S': return G[12]; case 'M': return G[13]; case '.': return G[14]; }
    return Z;
}
#define WP69_TW 1536
#define WP69_TH 128
static ID3D12Resource* s_wp69_tex; static ID3D12Resource* s_wp69_up[3];
static D3D12_RESOURCE_STATES s_wp69_st; static int s_wp69_broken;
static int s_wp69_on = 0, s_wp69_init = 0, s_wp69_dis = 0;
static char s_wp69_txt[32] = "";
static LARGE_INTEGER s_wp69_t0; static unsigned s_wp69_n; static double s_wp69_fq;

static int wp69_fg_ok(void)   /* a window of THIS process is in the foreground */
{
    HWND f = GetForegroundWindow(); DWORD pid = 0;
    if (!f) return 0;
    GetWindowThreadProcessId(f, &pid);
    return pid == GetCurrentProcessId();
}
static void wp69_keys(void)
{
    if (!s_wp69_init) {
        s_wp69_init = 1;
        const char* e = getenv("WINPORT_CS_PROBE");
        if (e && e[0] == '1') {
            s_wp69_dis = 1;
            fprintf(stderr, "[UI69] WINPORT_CS_PROBE=1: F10/F11 fps-overlay/borderless keys disabled\n");
        } else {
            e = getenv("WINPORT_FPS_OVERLAY"); s_wp69_on = (e && e[0] == '1');
            InterlockedExchange(&s_wp69_keys_on, 1);
            fprintf(stderr, "[UI69] fps overlay key F10 (start %s), F11 borderless toggle\n", s_wp69_on ? "on" : "off");
        }
        fflush(stderr);
    }
    if (s_wp69_dis) return;
    static SHORT p10, p11;
    SHORT k10 = GetAsyncKeyState(VK_F10), k11 = GetAsyncKeyState(VK_F11);
    const int fg = wp69_fg_ok();
    if (fg && (k10 & 0x8000) && !(p10 & 0x8000)) {
        s_wp69_on = !s_wp69_on; s_wp69_n = 0; s_wp69_t0.QuadPart = 0;
        fprintf(stderr, "[UI69] F10: fps overlay %s\n", s_wp69_on ? "on" : "off"); fflush(stderr);
    }
    if (fg && (k11 & 0x8000) && !(p11 & 0x8000) && s_d3d.hwnd) PostMessageA(s_d3d.hwnd, WP_WM_BORDER69, 0, 0);
    p10 = k10; p11 = k11;
}
/* called once per presented frame, on the render_frame thread, after wp47_ui_overlay, same command list */
static void wp69_fps_overlay(D3D12_CPU_DESCRIPTOR_HANDLE bb_rtv)
{
    wp69_keys();
    if (!s_wp69_on || s_wp69_broken || !s_d3d.initialized || !s_d3d.cmd_list) return;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (s_wp69_fq == 0.0) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); s_wp69_fq = (double)f.QuadPart; }
    if (!s_wp69_t0.QuadPart) { s_wp69_t0 = now; s_wp69_n = 0; }
    else {
        s_wp69_n++;
        const double dt = (double)(now.QuadPart - s_wp69_t0.QuadPart) / s_wp69_fq;
        if (dt >= 0.5 && s_wp69_n) {
            const double fps = (double)s_wp69_n / dt;
            snprintf(s_wp69_txt, sizeof s_wp69_txt, "%.0f FPS  %.1f MS", fps, 1000.0 / fps);
            s_wp69_t0 = now; s_wp69_n = 0;
        }
    }
    if (!s_wp69_txt[0]) return;
    if (wp81_ui()) { wp81_fps_draw(bb_rtv); return; }   /* winport v81-ui */
    if (!wp_blit_init() || !wp47_psos()) return;
    ID3D12GraphicsCommandList* cl = s_d3d.cmd_list;
    const unsigned pitch = WP69_TW * 4u;   /* multiple of 256 */
    if (!s_wp69_tex) {
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = {0};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = WP69_TW; td.Height = WP69_TH;
        td.DepthOrArraySize = 1; td.MipLevels = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        D3D12_HEAP_PROPERTIES hu = {0}; hu.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd = {0};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = (u64)pitch * WP69_TH; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        int bad = FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
                D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&s_wp69_tex));
        for (int i = 0; i < 3 && !bad; i++)
            bad = FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hu, D3D12_HEAP_FLAG_NONE, &bd,
                    D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&s_wp69_up[i]));
        if (bad) { s_wp69_broken = 1; fprintf(stderr, "[UI69] overlay texture allocation failed -> no fps overlay\n"); return; }
        s_wp69_st = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    const int fi = (int)(s_d3d.frame_index % 3u);
    ID3D12Resource* up = s_wp69_up[fi];
    /* ---- rasterise the text (premultiplied RGBA): dark panel + white 5x7 glyphs ---- */
    float k = (float)WP68_BH / 720.0f;
    if (k < 0.75f) k = 0.75f;
    if (k > 4.0f) k = 4.0f;
    int sc = (int)(k * 2.0f + 0.5f); if (sc < 1) sc = 1;
    const int n = (int)strlen(s_wp69_txt);
    while (sc > 1 && (n * 6 + 3) * sc > WP69_TW) sc--;
    int cw = (n * 6 + 3) * sc, ch = 11 * sc;
    if (cw > WP69_TW) cw = WP69_TW;
    if (ch > WP69_TH) ch = WP69_TH;
    void* m = NULL; D3D12_RANGE nr = {0, 0};
    if (FAILED(up->lpVtbl->Map(up, 0, &nr, &m)) || !m) return;
    for (int y = 0; y < ch; y++) {
        u32* row = (u32*)((u8*)m + (size_t)y * pitch);
        for (int x = 0; x < cw; x++) row[x] = 0xA0000000u;   /* premult black, a=0.63 (A in the top byte) */
    }
    for (int i = 0; i < n; i++) {
        const unsigned char* g = wp69_glyph(s_wp69_txt[i]);
        for (int gy = 0; gy < 7; gy++)
            for (int gx = 0; gx < 5; gx++) {
                if (!(g[gy] & (0x10 >> gx))) continue;
                for (int yy = 0; yy < sc; yy++) {
                    const int py = (2 + gy) * sc + yy; if (py >= ch) continue;
                    u32* row = (u32*)((u8*)m + (size_t)py * pitch);
                    for (int xx = 0; xx < sc; xx++) { const int px = (2 + i * 6 + gx) * sc + xx; if (px < cw) row[px] = 0xFFFFFFFFu; }
                }
            }
    }
    up->lpVtbl->Unmap(up, 0, NULL);
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = s_wp69_tex;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (s_wp69_st != D3D12_RESOURCE_STATE_COPY_DEST) {
        b.Transition.StateBefore = s_wp69_st; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        cl->lpVtbl->ResourceBarrier(cl, 1, &b);
    }
    {
        D3D12_TEXTURE_COPY_LOCATION cd = {0}, cs = {0};
        cd.pResource = s_wp69_tex; cd.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cs.pResource = up; cs.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        cs.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        cs.PlacedFootprint.Footprint.Width = (UINT)cw; cs.PlacedFootprint.Footprint.Height = (UINT)ch;
        cs.PlacedFootprint.Footprint.Depth = 1; cs.PlacedFootprint.Footprint.RowPitch = pitch;
        cl->lpVtbl->CopyTextureRegion(cl, &cd, 0, 0, 0, &cs, NULL);
    }
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cl->lpVtbl->ResourceBarrier(cl, 1, &b);
    s_wp69_st = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const float uvr[4] = { 0.0f, 0.0f, (float)cw / (float)WP69_TW, (float)ch / (float)WP69_TH };
    const float mg = floorf(8.0f * k);
    wp49_quad(bb_rtv, s_wp47_pso, s_wp69_tex, uvr, mg, mg, (float)cw, (float)ch);
}
/* ---- end winport v69-fps-f11 ---- */

/* ---- winport v80-port-settings (fix_v80_port_settings_renderer.py): Port Settings panel ---- */
extern volatile LONG wp80_pset_open, wp80_pset_hint, wp80_pset_nav;   /* cellPad.c (engine_fixes_v80_port_settings.py) */
/* ---- winport v90-ckpt (fix_v90_ckpt_renderer.py): Save Checkpoint row + toast; engine half in cellPad.c ---- */
extern volatile LONG wp90_req, wp90_gen, wp90_have;
extern char wp90_msg[];
static int s90_rows = -1, s90_pending;
static LONG s90_pgen;
static int wp90_rows(void)
{
    if (s90_rows < 0) { char sw[8]; DWORD l = GetEnvironmentVariableA("WINPORT_CKPT90", sw, sizeof sw); s90_rows = (l && l < sizeof sw && sw[0] == '1') ? 6 : 4; }   /* winport v90b-ckpt-load: 6 */
    return s90_rows;
}
/* ---- end winport v90-ckpt decl ---- */
/* ---- winport v90b-ckpt-load (fix_v90b_ckpt_load_renderer.py): Load Checkpoint row ---- */
extern char wp90_load[];
extern const char* wp90_dirp(void);
#define S90_NF 48
static char s90_fn[S90_NF][96];   /* file names, newest first */
static int s90_nf, s90_fi, s90_lpending;
static void wp90_list(void)
{
    WIN32_FIND_DATAA fd; char pat[MAX_PATH + 16]; FILETIME ft[S90_NF];
    s90_nf = 0; s90_fi = 0;
    const char* d = wp90_dirp();
    if (!d || !d[0]) return;
    snprintf(pat, sizeof pat, "%s\\ckpt_*.bin", d);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (strlen(fd.cFileName) >= sizeof s90_fn[0]) continue;
        const int full = (s90_nf == S90_NF);
        int k = full ? S90_NF - 1 : s90_nf++;   /* full: the newer file replaces the oldest kept (last) */
        if (full && CompareFileTime(&fd.ftLastWriteTime, &ft[k]) <= 0) continue;
        while (k > 0 && CompareFileTime(&fd.ftLastWriteTime, &ft[k - 1]) > 0) { ft[k] = ft[k - 1]; memcpy(s90_fn[k], s90_fn[k - 1], sizeof s90_fn[0]); k--; }
        ft[k] = fd.ftLastWriteTime; snprintf(s90_fn[k], sizeof s90_fn[0], "%s", fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
/* "ckpt_20261010_025251_Rhod27_Rhod30.bin" -> "Rhod30  10/10 02:52" */
static void wp90_label(const char* f, char* out, int cap)
{
    char d[16] = "", t[16] = "", n[64] = "";
    if (sscanf(f, "ckpt_%8[0-9]_%6[0-9]_%63[^.]", d, t, n) < 3 || strlen(d) < 8 || strlen(t) < 4) { snprintf(out, (size_t)cap, "%.30s", f); return; }
    char* lv = strrchr(n, '_'); lv = lv ? lv + 1 : n;   /* the second level name (the current one), short enough for the row */
    snprintf(out, (size_t)cap, "%.12s  %.2s:%.2s", lv, t, t + 2);   /* date goes to the note line (row was too wide) */
}
static void wp90_do_load(void)
{
    if (s90_nf <= 0 || s90_lpending) return;
    snprintf(wp90_load, MAX_PATH, "%s\\%s", wp90_dirp(), s90_fn[s90_fi]);
    s90_lpending = 1;
    fprintf(stderr, "[CKPT90] panel: Load Checkpoint %s (restart)\n", wp90_load); fflush(stderr);
    InterlockedExchange(&wp90_req, 2);
}
/* ---- end winport v90b-ckpt-load decl ---- */
/* ---- winport v92a-ckpt-list (fix_v92a_ckpt_list_renderer.py): thumbnails + Load Checkpoint list; engine half cellPad.c ---- */
extern volatile LONG wp92_sub, wp92_close_req, wp92_snap_req, wp92_snap_now, wp92_thumb_hash;
extern unsigned char wp92_thumb[];
extern unsigned wp92_gstate(void);
#define S92_TW 320
#define S92_TH 180
static int s92_mode;              /* 0 settings rows, 1 list, 2 delete confirm */
static int s92_sel, s92_top;
static unsigned char* s92_th[S90_NF];   /* loaded thumbnails (RGB 320x180) per listed file, NULL = none */
static void wp92_list_open(void);
static void wp92_list_nav(LONG nav);
/* ---- end winport v92a-ckpt-list decl ---- */
static char s80_ini[MAX_PATH + 32];
static char s80_boot[600];
static void wp80_ini_path(void)
{
    { char t[MAX_PATH]; DWORD l = GetEnvironmentVariableA("WINPORT_PSET80_INI", t, sizeof t);   /* winport v80b-pset-test */
      if (l && l < sizeof t) { strcpy(s80_ini, t); return; } }
    DWORD n = GetModuleFileNameA(NULL, s80_ini, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { strcpy(s80_ini, "port_settings.ini"); return; }
    char* a = strrchr(s80_ini, '\\'); char* b = strrchr(s80_ini, '/');
    if (b > a) a = b;
    if (a) a[1] = 0; else s80_ini[0] = 0;
    strcat(s80_ini, "port_settings.ini");
}
/* runs before main(): the file's values replace the launcher's environment before anything (wp_getenv_cached snapshot,
 * wp68_init, wp58_cap, wp69_keys) reads it */
__attribute__((constructor)) static void wp80_ini_ctor(void)
{
    char sw[8]; DWORD l = GetEnvironmentVariableA("WINPORT_PORT_SETTINGS", sw, sizeof sw);
    if (!l || l >= sizeof sw || sw[0] != '1') return;
    wp80_ini_path();
    FILE* f = fopen(s80_ini, "rb");
    if (!f) {
        snprintf(s80_boot, sizeof s80_boot, "[PSET80] switch on; %s not found -> launcher environment used\n", s80_ini);
        fputs(s80_boot, stderr); fflush(stderr);
        return;
    }
    size_t bl = (size_t)snprintf(s80_boot, sizeof s80_boot, "[PSET80] switch on; read %s ->", s80_ini);
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char* k = line; while (*k == ' ' || *k == '\t') k++;
        if (*k == '#' || *k == ';' || !*k) continue;
        char* eq = strchr(k, '='); if (!eq) continue;
        *eq = 0;
        char* kt = eq; while (kt > k && (kt[-1] == ' ' || kt[-1] == '\t')) *--kt = 0;
        char* v = eq + 1; while (*v == ' ' || *v == '\t') v++;
        char* ve = v + strlen(v); while (ve > v && (ve[-1] == '\r' || ve[-1] == '\n' || ve[-1] == ' ' || ve[-1] == '\t')) *--ve = 0;
        const char* env = NULL; char val[24];
        if (!strcmp(k, "fps_cap")) { int c = atoi(v); if (c < 0) c = 0; if (c > 1000) c = 1000; snprintf(val, sizeof val, "%d", c); env = "WINPORT_FPS_CAP"; }
        else if (!strcmp(k, "borderless")) { snprintf(val, sizeof val, "%d", atoi(v) ? 1 : 0); env = "WINPORT_BORDERLESS"; }
        else if (!strcmp(k, "res_scale")) { double d = atof(v); if (d < 1.0) d = 1.0; if (d > 4.0) d = 4.0; snprintf(val, sizeof val, "%g", d); env = "WINPORT_RES_SCALE"; }
        else if (!strcmp(k, "fps_overlay")) { snprintf(val, sizeof val, "%d", atoi(v) ? 1 : 0); env = "WINPORT_FPS_OVERLAY"; }
        if (!env) continue;
        _putenv_s(env, val);
        if (bl < sizeof s80_boot - 48) bl += (size_t)snprintf(s80_boot + bl, sizeof s80_boot - bl, " %s=%s", env, val);
    }
    fclose(f);
    if (bl < sizeof s80_boot - 2) { s80_boot[bl++] = '\n'; s80_boot[bl] = 0; }
    fputs(s80_boot, stderr); fflush(stderr);
}

typedef struct { Wp47Txt t; char s[96]; int key, ok; } Wp80Txt;
enum { T80_TITLE, T80_FOOT, T80_HINT, T80_LAB, T80_VAL = T80_LAB + 4, T80_NOTE = T80_VAL + 4, T80_N = T80_NOTE + 4 };
static Wp80Txt s80_t[T80_N];
typedef struct { u8* cv; size_t cap; int cw, ch; ID3D12Resource *tex, *up; int tw, th; unsigned pitch;
                 D3D12_RESOURCE_STATES st; u64 use; int need_up; } Wp80Spr;
static Wp80Spr s80_sp[2];   /* 0 panel, 1 hint */
static int s80_init, s80_on, s80_broken, s80_was_open, s80_sel, s80_dirty, s80_redraw_p, s80_redraw_h;
static int s80_cap_start, s80_cap_cur, s80_res_sel, s80_res_touched;
static DWORD s80_bl_t;
static const int S80_CAPS[] = { 30, 60, 120, 144, 162, 165, 240, 300, 1000 };   /* winport v86g-cap162: + 162 (G-Sync cap for 165 Hz) */
#define S80_NCAPS ((int)(sizeof S80_CAPS / sizeof S80_CAPS[0]))

static int wp80_live_borderless(void)
{
    HWND h = s_d3d.hwnd;
    return h ? !(GetWindowLongPtrA(h, GWL_STYLE) & WS_CAPTION) : s_wp68_bl;
}
static void wp80_init(void)
{
    s80_init = 1;
    char sw[8]; DWORD l = GetEnvironmentVariableA("WINPORT_PORT_SETTINGS", sw, sizeof sw);
    s80_on = (l && l < sizeof sw && sw[0] == '1');
    if (!s80_on) return;
    if (!s80_ini[0]) wp80_ini_path();
    if (s80_boot[0]) { fputs(s80_boot, stderr); }
    s80_cap_start = wp58_cap(); s80_cap_cur = s80_cap_start;
    s80_res_sel = (int)(s_wp68_s + 0.5f); if (s80_res_sel < 1) s80_res_sel = 1; if (s80_res_sel > 4) s80_res_sel = 4;
    fprintf(stderr, "[PSET80] renderer ready: running fps cap %d%s, res scale %g, borderless %d, fps counter %d, file %s\n",
            s80_cap_start, s80_cap_start > 0 ? "" : " (none: cap rows need a restart)", (double)s_wp68_s,
            wp80_live_borderless(), s_wp69_on, s80_ini);
    fflush(stderr);
}
static void wp80_save(void)
{
    FILE* f = fopen(s80_ini, "wb");
    if (!f) { fprintf(stderr, "[PSET80] cannot write %s\n", s80_ini); fflush(stderr); return; }
    const int bl = wp80_live_borderless();
    fprintf(f, "# GoW2 HD port settings (written by the in-game Port Settings panel; used when WINPORT_PORT_SETTINGS=1)\r\n");
    if (s80_cap_cur > 0) fprintf(f, "fps_cap=%d\r\n", s80_cap_cur);
    fprintf(f, "borderless=%d\r\n", bl);
    if (s80_res_touched) fprintf(f, "res_scale=%d\r\n", s80_res_sel); else fprintf(f, "res_scale=%g\r\n", (double)s_wp68_s);
    fprintf(f, "fps_overlay=%d\r\n", s_wp69_on ? 1 : 0);
    fclose(f);
    fprintf(stderr, "[PSET80] wrote %s (fps_cap=%d borderless=%d res_scale=%g fps_overlay=%d)\n", s80_ini, s80_cap_cur, bl,
            s80_res_touched ? (double)s80_res_sel : (double)s_wp68_s, s_wp69_on ? 1 : 0);
    fflush(stderr);
}
static void wp80_step(int dir)   /* change the selected row's value: dir +1 / -1 */
{
    switch (s80_sel) {
    case 0: {
        const int c = s80_cap_cur; int n = dir > 0 ? S80_CAPS[0] : S80_CAPS[S80_NCAPS - 1];   /* winport v86g-cap162 */
        if (dir > 0) { for (int i = 0; i < S80_NCAPS; i++) if (S80_CAPS[i] > c) { n = S80_CAPS[i]; break; } }
        else { for (int i = S80_NCAPS - 1; i >= 0; i--) if (S80_CAPS[i] < c) { n = S80_CAPS[i]; break; } }
        s80_cap_cur = n; s80_dirty = 1;
        if (s80_cap_start > 0) s_wp58_capv = n;
        fprintf(stderr, "[PSET80] frame rate cap -> %d (%s)\n", n, s80_cap_start > 0 ? "applied now" : "saved; needs a restart (game started without WINPORT_FPS_CAP)");
        break; }
    case 1: {
        const DWORD now = GetTickCount();
        if (now - s80_bl_t < 400u || !s_d3d.hwnd) break;
        s80_bl_t = now; s80_dirty = 1;
        fprintf(stderr, "[PSET80] display mode -> %s (F11 path)\n", wp80_live_borderless() ? "WINDOWED" : "BORDERLESS");
        PostMessageA(s_d3d.hwnd, WP_WM_BORDER69, 0, 0);
        break; }
    case 2:
        s80_res_sel += dir; if (s80_res_sel > 4) s80_res_sel = 1; if (s80_res_sel < 1) s80_res_sel = 4;
        s80_res_touched = 1; s80_dirty = 1;
        fprintf(stderr, "[PSET80] resolution scale -> %dX (%s)\n", s80_res_sel, fabsf((float)s80_res_sel - s_wp68_s) > 0.01f ? "applies after restart" : "= running scale");
        break;
    case 5:   /* winport v90b-ckpt-load: choose the checkpoint file (X / Enter loads, see wp80_panel) */
        if (s90_nf > 0) { s90_fi = (s90_fi + (dir > 0 ? 1 : s90_nf - 1)) % s90_nf; }
        break;
    case 4:   /* winport v90-ckpt: Save Checkpoint */
        if (dir > 0 && wp90_rows() >= 5 && !s90_pending) { s90_pending = 1; s90_pgen = wp90_gen; InterlockedExchange(&wp90_req, 1);
            fprintf(stderr, "[CKPT90] panel: Save Checkpoint\n"); }
        break;
    case 3:
        s_wp69_on = !s_wp69_on; s_wp69_n = 0; s_wp69_t0.QuadPart = 0; s80_dirty = 1;
        fprintf(stderr, "[PSET80] fps counter -> %s\n", s_wp69_on ? "ON" : "OFF");
        break;
    }
    fflush(stderr);
    s80_redraw_p = 1;
}
static const Wp47Txt* wp80_txt(int i, const char* s, int px, int weight, int sp, int key)
{
    Wp80Txt* x = &s80_t[i];
    if (x->ok && x->key == key && strcmp(x->s, s) == 0) return &x->t;
    wp47_txt_make(&x->t, s, px, weight, sp);
    strncpy(x->s, s, sizeof x->s - 1); x->s[sizeof x->s - 1] = 0; x->key = key; x->ok = 1;
    if (i == T80_HINT) s80_redraw_h = 1; else s80_redraw_p = 1;
    return &x->t;
}
/* open a canvas of w x h for wp47_px (swaps the v47 canvas globals; wp80_cv_end restores them) */
static u8* s80_sv_cv; static int s80_sv_cw, s80_sv_ch;
static int wp80_cv_begin(Wp80Spr* sp, int w, int h)
{
    const size_t need = (size_t)w * (size_t)h * 4u;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return 0;
    if (need > sp->cap) { free(sp->cv); sp->cv = (u8*)malloc(need); sp->cap = sp->cv ? need : 0; if (!sp->cv) return 0; }
    memset(sp->cv, 0, need);
    s80_sv_cv = s_wp47_cv; s80_sv_cw = s_wp47_cw; s80_sv_ch = s_wp47_ch;
    s_wp47_cv = sp->cv; s_wp47_cw = w; s_wp47_ch = h;
    sp->cw = w; sp->ch = h;
    return 1;
}
static void wp80_cv_end(Wp80Spr* sp)
{
    s_wp47_cv = s80_sv_cv; s_wp47_cw = s80_sv_cw; s_wp47_ch = s80_sv_ch;
    sp->need_up = 1;
}
/* texture + upload buffer (grown on size change), copy the canvas when it changed; returns the texture or NULL */
static ID3D12Resource* wp80_spr_upload(Wp80Spr* sp)
{
    ID3D12GraphicsCommandList* cl = s_d3d.cmd_list;
    if (!sp->cv || sp->cw <= 0) return NULL;
    if (!sp->tex || sp->tw != sp->cw || sp->th != sp->ch) {
        wp77_idle(5);
        if (sp->tex) { sp->tex->lpVtbl->Release(sp->tex); sp->tex = NULL; }
        if (sp->up)  { sp->up->lpVtbl->Release(sp->up);   sp->up = NULL; }
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = {0};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = (UINT64)sp->cw; td.Height = (UINT)sp->ch;
        td.DepthOrArraySize = 1; td.MipLevels = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        sp->pitch = ((unsigned)sp->cw * 4u + 255u) & ~255u;
        D3D12_HEAP_PROPERTIES hu = {0}; hu.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd = {0};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = (u64)sp->pitch * (u64)sp->ch; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
                D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&sp->tex)) ||
            FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hu, D3D12_HEAP_FLAG_NONE, &bd,
                D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&sp->up))) {
            if (sp->tex) { sp->tex->lpVtbl->Release(sp->tex); sp->tex = NULL; }
            if (sp->up)  { sp->up->lpVtbl->Release(sp->up);   sp->up = NULL; }
            s80_broken = 1;
            fprintf(stderr, "[PSET80] texture allocation failed -> panel not drawn\n"); fflush(stderr);
            return NULL;
        }
        sp->st = D3D12_RESOURCE_STATE_COPY_DEST; sp->tw = sp->cw; sp->th = sp->ch; sp->need_up = 1;
    }
    if (sp->need_up) {
        void* m = NULL; D3D12_RANGE nr = {0, 0};
        if (wp77_on()) { wp77_wait(sp->use, 4); sp->use = s77_val + 1; }   /* winport v77-fif */
        if (FAILED(sp->up->lpVtbl->Map(sp->up, 0, &nr, &m)) || !m) return NULL;
        for (int y = 0; y < sp->ch; y++)
            memcpy((u8*)m + (size_t)y * sp->pitch, sp->cv + (size_t)y * (size_t)sp->cw * 4u, (size_t)sp->cw * 4u);
        sp->up->lpVtbl->Unmap(sp->up, 0, NULL);
        D3D12_RESOURCE_BARRIER b = {0};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = sp->tex;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        if (sp->st != D3D12_RESOURCE_STATE_COPY_DEST) {
            b.Transition.StateBefore = sp->st; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            cl->lpVtbl->ResourceBarrier(cl, 1, &b);
        }
        D3D12_TEXTURE_COPY_LOCATION cd = {0}, cs = {0};
        cd.pResource = sp->tex; cd.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cs.pResource = sp->up;  cs.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        cs.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        cs.PlacedFootprint.Footprint.Width = (UINT)sp->cw; cs.PlacedFootprint.Footprint.Height = (UINT)sp->ch;
        cs.PlacedFootprint.Footprint.Depth = 1; cs.PlacedFootprint.Footprint.RowPitch = sp->pitch;
        cl->lpVtbl->CopyTextureRegion(cl, &cd, 0, 0, 0, &cs, NULL);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        cl->lpVtbl->ResourceBarrier(cl, 1, &b);
        sp->st = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; sp->need_up = 0;
    }
    return sp->tex;
}
/* panel layout + drawing into sp's canvas; returns 1 when the canvas is valid */
static int wp80_build_panel(Wp80Spr* sp, float kr)
{
    char vs[4][64], ns[4][64];
    const int bl = wp80_live_borderless();
    if (s80_cap_cur == 0) snprintf(vs[0], 64, "NEEDS RESTART");
    else if (s80_cap_cur >= 1000) snprintf(vs[0], 64, "UNLIMITED");
    else snprintf(vs[0], 64, "%d", s80_cap_cur);
    ns[0][0] = 0;
    if (s80_cap_start <= 0 && s80_cap_cur > 0) snprintf(ns[0], 64, "NEEDS RESTART");
    snprintf(vs[1], 64, "%s", bl ? "BORDERLESS" : "WINDOWED"); ns[1][0] = 0;
    snprintf(vs[2], 64, "%dX", s80_res_sel);
    if (fabsf((float)s80_res_sel - s_wp68_s) > 0.01f) snprintf(ns[2], 64, "(APPLIES AFTER RESTART)"); else ns[2][0] = 0;
    snprintf(vs[3], 64, "%s", s_wp69_on ? "ON" : "OFF"); ns[3][0] = 0;
    static const char* const LAB[4] = { "FRAME RATE CAP", "DISPLAY MODE", "RESOLUTION SCALE", "FPS COUNTER" };
    const int key = (int)(kr * 100.0f), sp1 = (int)(1.6f * kr + 0.5f);
    const Wp47Txt* title = wp80_txt(T80_TITLE, "PORT SETTINGS", (int)(40.0f * kr), FW_BOLD, (int)(5.0f * kr + 0.5f), key);
    const Wp47Txt* foot = wp80_txt(T80_FOOT, "X/ENTER: CHANGE   O/ESC: BACK", (int)(16.0f * kr), FW_SEMIBOLD, sp1, key);
    const Wp47Txt *lab[4], *val[4], *note[4];
    for (int i = 0; i < 4; i++) {
        char v[80];
        if (i == s80_sel && !(i == 0 && s80_cap_cur == 0)) snprintf(v, sizeof v, "<  %s  >", vs[i]); else snprintf(v, sizeof v, "%s", vs[i]);
        lab[i] = wp80_txt(T80_LAB + i, LAB[i], (int)(24.0f * kr), FW_SEMIBOLD, sp1, key);
        val[i] = wp80_txt(T80_VAL + i, v, (int)(24.0f * kr), FW_BOLD, sp1, key);
        note[i] = wp80_txt(T80_NOTE + i, ns[i], (int)(13.0f * kr), FW_SEMIBOLD, sp1, key);
    }
    const float pad = 22.0f * kr, rowh = 62.0f * kr, W = 600.0f * kr;
    const float ys = pad + (float)title->h + 16.0f * kr;
    const float fy = ys + 4.0f * rowh + 8.0f * kr;
    const float H = fy + (float)foot->h + pad;
    const int cw = (int)ceilf(W), ch = (int)ceilf(H);
    if (!(s80_redraw_p || sp->cw != cw || sp->ch != ch || !sp->cv)) return 1;
    if (!wp80_cv_begin(sp, cw, ch)) return 0;
    wp49_panel(0.5f, 0.5f, (float)cw - 0.5f, (float)ch - 0.5f, kr, 1.0f);
    {   const float tx = ((float)cw - (float)title->w) * 0.5f, ry = pad + (float)title->h * 0.52f;
        float rule = 60.0f * kr; if (tx - 14.0f * kr - rule < pad * 0.6f) rule = tx - 14.0f * kr - pad * 0.6f;
        if (rule > 4.0f) {
            wp49_rule(tx - 14.0f * kr - rule, tx - 14.0f * kr, ry, kr, 0.95f, 1);
            wp49_rule(tx + (float)title->w + 14.0f * kr, tx + (float)title->w + 14.0f * kr + rule, ry, kr, 0.95f, 0);
        }
        wp49_text(title, (int)tx, (int)pad, 1.0f, kr); }
    static const float SEL_T[3] = { 0.62f, 0.12f, 0.07f }, SEL_B[3] = { 0.33f, 0.04f, 0.03f };
    for (int i = 0; i < 4; i++) {
        const float ry = ys + (float)i * rowh, x0 = pad * 0.7f, x1 = (float)cw - pad * 0.7f, y1 = ry + rowh - 6.0f * kr;
        const float a = (i == s80_sel) ? 1.0f : 0.72f;
        if (i == s80_sel) {
            wp49_rrect(x0, ry, x1, y1, 4.0f * kr, WP49_FRAME, WP49_GOLD_B, 0.95f);
            wp49_rrect(x0 + 1.5f * kr, ry + 1.5f * kr, x1 - 1.5f * kr, y1 - 1.5f * kr, 3.0f * kr, SEL_T, SEL_B, 0.96f);
        }
        const float ty = ry + 5.0f * kr, vx = (float)cw - pad - 12.0f * kr - (float)val[i]->w;
        wp49_text(lab[i], (int)(pad + 12.0f * kr), (int)ty, a, kr);
        wp49_text(val[i], (int)vx, (int)ty, a, kr);
        if (note[i]->m) wp49_text(note[i], (int)((float)cw - pad - 12.0f * kr - (float)note[i]->w), (int)(ty + (float)val[i]->h - 3.0f * kr), 0.85f, kr);
    }
    wp49_text(foot, (int)(((float)cw - (float)foot->w) * 0.5f), (int)fy, 0.9f, kr);
    wp80_cv_end(sp);
    s80_redraw_p = 0;
    return 1;
}
static int wp80_build_hint(Wp80Spr* sp, float kr)
{
    const int key = (int)(kr * 100.0f);
    const Wp47Txt* t = wp80_txt(T80_HINT, "L3 / F1:  PORT SETTINGS", (int)(16.0f * kr), FW_SEMIBOLD, (int)(1.6f * kr + 0.5f), key);
    const float px = 16.0f * kr, py = 8.0f * kr;
    const int cw = (int)ceilf((float)t->w + 2.0f * px), ch = (int)ceilf((float)t->h + 2.0f * py);
    if (!(s80_redraw_h || sp->cw != cw || sp->ch != ch || !sp->cv)) return 1;
    if (!wp80_cv_begin(sp, cw, ch)) return 0;
    wp49_panel(0.5f, 0.5f, (float)cw - 0.5f, (float)ch - 0.5f, kr, 0.92f);
    wp49_text(t, (int)px, (int)py, 0.95f, kr);
    wp80_cv_end(sp);
    s80_redraw_h = 0;
    return 1;
}
/* ---- winport v81-ui (fix_v81_ui_renderer.py): God of War II styled port UI drawn with the game's own font ---- */
extern volatile LONG wp81_pad_tick;   /* cellPad.c (engine_fixes_v81_port_settings_anywhere.py) */
static int s81_ui = -1;
static int wp81_ui(void)
{
    if (s81_ui < 0) {
        const char* e = getenv("WINPORT_UI81"); s81_ui = (e && e[0] == '0') ? 0 : 1;
        fprintf(stderr, "[UI81] port UI look: %s\n", s81_ui ? "v81 (God of War style, game font when available)" : "v80/v49/v69 (WINPORT_UI81=0)");
        fflush(stderr);
    }
    return s81_ui;
}
/* glyphs of the game's menu font, measured from its own text draws (research/U3_ui_redesign.md):
 * atlas rect u0,v0..u1,v1 in 512-px atlas texels; quad = font-unit box x0,y0..x1,y1 (baseline y=0, cap height 39.19)
 * covering the rect grown by 1 texel; adv = pen advance in font units. Lower case = the font's small capitals. */
typedef struct { char c; short u0, v0, u1, v1; float x0, y0, x1, y1, adv; } Wp81G;
/* The metrics of the game's menu font are NOT part of this source tree: they were measured from the
 * game's own text draws, so they are treated as game-derived data. When a file ui_font_metrics.txt sits
 * next to the exe (one glyph per line: <char> u0 v0 u1 v1 x0 y0 x1 y1 adv; '#' = comment), the port UI
 * draws its text with the game's font atlas; without it the port UI uses the fallback GDI font look
 * (same as WINPORT_UI81_GAMEFONT=0). */
static Wp81G S81_G[128];
static int s81_ng = -1;
static int wp81_metrics(void)
{
    if (s81_ng >= 0) return s81_ng;
    s81_ng = 0;
    char path[MAX_PATH]; DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return 0;
    char* sl = strrchr(path, '\\'); if (!sl) return 0;
    strcpy(sl + 1, "ui_font_metrics.txt");
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "[UI81] no ui_font_metrics.txt next to the exe: port UI uses the fallback font\n"); fflush(stderr); return 0; }
    char line[256];
    while (s81_ng < 128 && fgets(line, sizeof line, f)) {
        Wp81G g; int u0, v0, u1, v1;
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (sscanf(line + 1, "%d %d %d %d %f %f %f %f %f", &u0, &v0, &u1, &v1, &g.x0, &g.y0, &g.x1, &g.y1, &g.adv) != 9) continue;
        g.c = line[0]; g.u0 = (short)u0; g.v0 = (short)v0; g.u1 = (short)u1; g.v1 = (short)v1;
        S81_G[s81_ng++] = g;
    }
    fclose(f);
    fprintf(stderr, "[UI81] ui_font_metrics.txt: %d glyphs -> port UI uses the game's font\n", s81_ng); fflush(stderr);
    return s81_ng;
}
#define S81_NG (wp81_metrics())
#define S81_SPACE 20.62f
#define S81_CAP 39.1875f
static const Wp81G* s81_gi[128];
static int s81_gi_ok;
static const Wp81G* wp81_glyph(char c)
{
    if (!s81_gi_ok) { for (int i = 0; i < S81_NG; i++) s81_gi[(unsigned char)S81_G[i].c & 127] = &S81_G[i]; s81_gi_ok = 1; }
    const unsigned u = (unsigned char)c;
    if (u >= 128) return NULL;
    const Wp81G* g = s81_gi[u];
    if (!g && u >= 'a' && u <= 'z') g = s81_gi[u - 32u];
    return g;
}

/* ---- runtime cache of the game's own UI textures (found by fingerprint in what the game binds; memory only) ---- */
#define S81_H_FONT512 0xCFC27946u   /* alpha FNV-1a of the 512x512 in-game font atlas */
#define S81_H_FONT256 0x2A55DE97u   /* alpha FNV-1a of the 256x256 front-end copy */
#define S81_H_PLAQUE  0xAB8A43F1u   /* ARGB FNV-1a of the 256x128 skull plaque (half) */
static u8* s81_fa; static int s81_fs; static int s81_gen = 1;   /* font alpha s81_fs x s81_fs (512/256), 0 = none */
static u8* s81_pq; static int s81_pq_ok;                         /* plaque, premultiplied RGBA 256x128 */
typedef struct { u32 off; DWORD t; } Wp81Seen;
static Wp81Seen s81_seen[64]; static int s81_seen_n, s81_seen_rr, s81_nlog;
static u32 wp81_mort(u32 x, u32 y, int m)
{
    u32 r = 0;
    for (int i = 0; i < m; i++) r |= (((x >> i) & 1u) << (2 * i)) | (((y >> i) & 1u) << (2 * i + 1));
    return r | (((x >> m) | (y >> m)) << (2 * m));
}
static void wp81_check(u32 off, int w, int h)
{
    extern uint8_t* vm_base;
    if (!vm_base) return;
    const u8* src = vm_base + off;
    int lw = 0, lh = 0; while ((1 << lw) < w) lw++; while ((1 << lh) < h) lh++;
    const int m = lw < lh ? lw : lh, font = (w == h);
    u8* buf = (u8*)malloc((size_t)w * (size_t)h * (font ? 1u : 4u));
    if (!buf) return;
    u32 hs = 2166136261u;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const u8* p = src + (size_t)wp81_mort((u32)x, (u32)y, m) * 4u;
            if (font) { buf[y * w + x] = p[0]; hs = (hs ^ p[0]) * 16777619u; continue; }
            for (int c = 0; c < 4; c++) hs = (hs ^ p[c]) * 16777619u;
            u8* q = buf + ((size_t)y * w + x) * 4u; const float a = p[0] / 255.0f;
            q[0] = (u8)(p[1] * a + 0.5f); q[1] = (u8)(p[2] * a + 0.5f); q[2] = (u8)(p[3] * a + 0.5f); q[3] = p[0];
        }
    if (font && ((w == 512 && hs == S81_H_FONT512) || (w == 256 && hs == S81_H_FONT256))) {
        free(s81_fa); s81_fa = buf; s81_fs = w; s81_gen++;
        fprintf(stderr, "[UI81] the game's menu font found at 0x%08X (%dx%d) -> port UI drawn with it\n", off, w, h); fflush(stderr);
        return;
    }
    if (!font && hs == S81_H_PLAQUE) {
        free(s81_pq); s81_pq = buf; s81_pq_ok = 1; s81_gen++;
        fprintf(stderr, "[UI81] the game's menu plaque found at 0x%08X -> used as the Port Settings header\n", off); fflush(stderr);
        return;
    }
    if (s81_nlog < 4) { s81_nlog++; fprintf(stderr, "[UI81] candidate 0x%08X %dx%d: fingerprint 0x%08X (not ours)\n", off, w, h, hs); fflush(stderr); }
    free(buf);
}
/* ---- winport v82a-game-buttons (fix_v82a_game_buttons_renderer.py): the game's own prompt button meshes ---- */
#define S82_H_DISC 0xB6CF66A6u   /* ARGB FNV-1a of the 128x128 radial button disc */
typedef struct { float x, y, u, v; } Wp82V;
static Wp82V s82_sh[96], s82_dc[96], s82_sy[2][64];   /* disc-local units; sy: 0 cross, 1 triangle */
static int s82_nsh, s82_ndc, s82_nsy[2];
static u8* s82_dt; static int s82_dt_ok; static u32 s82_dt_off;
static int s82_on = -1;
typedef struct { u32 off; DWORD t; } Wp82Seen;
static Wp82Seen s82_seen[32]; static int s82_seen_n, s82_seen_rr;
static int wp82_on(void)
{
    if (s82_on < 0) {
        const char* e = getenv("WINPORT_UI82_GAMEBUTTONS"); const char* g = getenv("WINPORT_UI81_GAMEFONT");
        s82_on = ((e && e[0] == '0') || (g && g[0] == '0')) ? 0 : 1;
        fprintf(stderr, "[UI82] prompt buttons: %s\n", s82_on ? "the game's own meshes + disc texture once seen (procedural until then)" : "procedural (v81)");
        fflush(stderr);
    }
    return s82_on;
}
static void wp82_check_disc(u32 off)
{
    extern uint8_t* vm_base;
    if (!vm_base) return;
    const u8* src = vm_base + off;
    u8* buf = (u8*)malloc(128u * 128u * 4u); if (!buf) return;
    u32 hs = 2166136261u;
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            const u8* p = src + (size_t)wp81_mort((u32)x, (u32)y, 7) * 4u;
            for (int c = 0; c < 4; c++) hs = (hs ^ p[c]) * 16777619u;
            u8* q = buf + ((size_t)y * 128 + x) * 4u; const float a = p[0] / 255.0f;
            q[0] = (u8)(p[1] * a + 0.5f); q[1] = (u8)(p[2] * a + 0.5f); q[2] = (u8)(p[3] * a + 0.5f); q[3] = p[0];
        }
    if (hs == S82_H_DISC) {
        free(s82_dt); s82_dt = buf; s82_dt_ok = 1; s82_dt_off = off; s81_gen++;
        fprintf(stderr, "[UI82] the game's button disc texture found at 0x%08X\n", off); fflush(stderr);
        return;
    }
    free(buf);
}
static const float* wp82_cb(const D3D12DrawRecord* r)
{
    return (const float*)((const char*)s_d3d.vp_cb_mapped + ((u64)s_d3d.vp_parity * MAX_DRAWS + r->cb_slot) * VP_CB_STRIDE);
}
static const float* wp82_vt(const D3D12DrawRecord* r)
{
    return (const float*)((const char*)s_d3d.vp_vb_mapped + (u64)s_d3d.vp_parity * MAX_VERTICES * 256u + (u64)r->vb_byte_offset);
}
/* a draw's vertices -> the disc draw's local space (2D part of the row-vector transform in c260..c263) */
static int wp82_take(const D3D12DrawRecord* r, const float* Md, Wp82V* out, int max, int uv)
{
    const u32 n = r->vertex_count;
    if (!r->is_vp || n < 3 || n > (u32)max || n % 3u || (u64)r->vb_byte_offset + (u64)n * 256u > (u64)MAX_VERTICES * 256u) return 0;
    const float* M = wp82_cb(r) + 260 * 4; const float* vt = wp82_vt(r);
    if (fabsf(Md[0]) < 1e-4f || fabsf(Md[5]) < 1e-4f) return 0;
    for (u32 i = 0; i < n; i++) {
        const u32 vi85_ = (r->wp85_ib && s85_ib_mapped) ? s85_ib_mapped[(u64)s_d3d.vp_parity * WP85_IB_CAP + r->wp85_ib - 1u + i] : i;   /* winport v85e-idxdraw */
        const float x = vt[vi85_ * 64u + 0], y = vt[vi85_ * 64u + 1];
        const float wx = x * M[0] + y * M[4] + M[12], wy = x * M[1] + y * M[5] + M[13];
        out[i].x = (wx - Md[12]) / Md[0]; out[i].y = (wy - Md[13]) / Md[5];
        out[i].u = uv ? vt[vi85_ * 64u + 32] : 0.0f; out[i].v = uv ? vt[vi85_ * 64u + 33] : 0.0f;
        if (!(fabsf(out[i].x) < 60.0f && fabsf(out[i].y) < 60.0f) || (uv && !(out[i].u > -0.5f && out[i].u < 1.5f && out[i].v > -0.5f && out[i].v < 1.5f))) return 0;
    }
    return (int)n;
}
static void wp82_scan(void)
{
    if (!wp81_ui() || !wp82_on() || !s_d3d.vp_cb_mapped || !s_d3d.vp_vb_mapped) return;
    if (s82_dt_ok && s82_ndc && s82_nsy[0] && s82_nsy[1]) return;
    const DWORD now = GetTickCount();
    int budget = 2;
    for (u32 d = 0; d < s_d3d.draw_count && d < MAX_DRAWS; d++) {
        const D3D12DrawRecord* r = &s_d3d.draws[d];
        if (r->is_clear || !r->tex[0].set || r->tex[0].fmt != 0x85u || r->tex[0].w != 128 || r->tex[0].h != 128 || !r->tex[0].off) continue;
        if (!s82_dt_ok) {
            if (budget <= 0) continue;
            int k = 0;
            while (k < s82_seen_n && s82_seen[k].off != r->tex[0].off) k++;
            if (k < s82_seen_n) { if (now - s82_seen[k].t < 3000u) continue; }
            else if (s82_seen_n < 32) k = s82_seen_n++;
            else { k = s82_seen_rr; s82_seen_rr = (s82_seen_rr + 1) % 32; }
            s82_seen[k].off = r->tex[0].off; s82_seen[k].t = now; budget--;
            wp82_check_disc(r->tex[0].off);
            if (!s82_dt_ok) continue;
        }
        if (r->tex[0].off != s82_dt_off || d == 0 || d + 1 >= s_d3d.draw_count) continue;
        const float* Md = wp82_cb(r) + 260 * 4;
        const D3D12DrawRecord* p = &s_d3d.draws[d - 1]; const D3D12DrawRecord* q = &s_d3d.draws[d + 1];
        if (!s82_ndc) {
            Wp82V dc[96], sh[96];
            const int nd = wp82_take(r, Md, dc, 96, 1);
            const int ns = (!p->is_clear && p->tex[0].set && p->tex[0].w == 4) ? wp82_take(p, Md, sh, 96, 0) : 0;
            if (nd >= 30) {
                memcpy(s82_dc, dc, sizeof(Wp82V) * (size_t)nd); s82_ndc = nd;
                if (ns >= 30) { memcpy(s82_sh, sh, sizeof(Wp82V) * (size_t)ns); s82_nsh = ns; }
                s81_gen++;
                fprintf(stderr, "[UI82] the game's button disc mesh captured (%d vertices, shadow %d)\n", nd, ns); fflush(stderr);
            }
        }
        if (!q->is_clear && q->tex[0].set && q->tex[0].w == 4 && (q->vertex_count == 30u || q->vertex_count == 27u)) {
            const int k = q->vertex_count == 30u ? 0 : 1;
            if (!s82_nsy[k]) {
                Wp82V sy[64];
                const int n = wp82_take(q, Md, sy, 64, 0);
                float mx = 0.0f; for (int i = 0; i < n; i++) { if (fabsf(sy[i].x) > mx) mx = fabsf(sy[i].x); if (fabsf(sy[i].y) > mx) mx = fabsf(sy[i].y); }
                if (n && mx < 26.0f) {
                    memcpy(s82_sy[k], sy, sizeof(Wp82V) * (size_t)n); s82_nsy[k] = n; s81_gen++;
                    fprintf(stderr, "[UI82] the game's %s button symbol captured (%d vertices)\n", k ? "TRIANGLE" : "CROSS", n); fflush(stderr);
                }
            }
        }
    }
}
/* ---- end winport v82a-game-buttons ---- */

/* render_frame, while this frame's draw records still exist */
static void wp81_scan(void)
{
    wp82_scan();   /* winport v82a-game-buttons */
    if (!wp81_ui() || (s81_fs == 512 && s81_pq_ok)) return;
    static int gf = -1;   /* WINPORT_UI81_GAMEFONT=0: never use the game's textures (GDI fallback look only) */
    if (gf < 0) { const char* e = getenv("WINPORT_UI81_GAMEFONT"); gf = (e && e[0] == '0') ? 0 : 1;
                  if (gf && wp81_metrics() == 0) gf = 0;   /* no font metrics file: fallback look */
                  if (!gf && e && e[0] == '0') { fprintf(stderr, "[UI81] WINPORT_UI81_GAMEFONT=0: fallback font only\n"); fflush(stderr); } }
    if (!gf) return;
    const DWORD now = GetTickCount();
    int budget = 2;
    for (u32 d = 0; d < s_d3d.draw_count && d < MAX_DRAWS && budget > 0; d++) {
        const D3D12DrawRecord* r = &s_d3d.draws[d];
        if (r->is_clear) continue;
        for (int u = 0; u < 4 && budget > 0; u++) {
            if (!r->tex[u].set || r->tex[u].fmt != 0x85u || !r->tex[u].off || r->tex[u].cube) continue;
            const int w = (int)r->tex[u].w, h = (int)r->tex[u].h;
            const int want = (w == h && (w == 512 || (w == 256 && s81_fs == 0)) && s81_fs != 512) || (w == 256 && h == 128 && !s81_pq_ok);
            if (!want) continue;
            int k = 0;
            while (k < s81_seen_n && s81_seen[k].off != r->tex[u].off) k++;
            if (k < s81_seen_n) { if (now - s81_seen[k].t < 3000u) continue; }
            else if (s81_seen_n < 64) k = s81_seen_n++;
            else { k = s81_seen_rr; s81_seen_rr = (s81_seen_rr + 1) % 64; }
            s81_seen[k].off = r->tex[u].off; s81_seen[k].t = now;
            budget--;
            wp81_check(r->tex[u].off, w, h);
        }
    }
}

/* ---- text masks: the game's atlas, or GDI small caps until it has been seen ---- */
typedef struct { u8* m; int w, h; float x0, base, wpx; } Wp81T;   /* pen origin x0 / baseline in mask px, advance width */
static float wp81_bil(const u8* a, int n, float x, float y)
{
    if (x < 0.0f) x = 0.0f; if (y < 0.0f) y = 0.0f;
    if (x > (float)(n - 1)) x = (float)(n - 1); if (y > (float)(n - 1)) y = (float)(n - 1);
    const int x0 = (int)x, y0 = (int)y, x1 = x0 + 1 < n ? x0 + 1 : x0, y1 = y0 + 1 < n ? y0 + 1 : y0;
    const float fx = x - (float)x0, fy = y - (float)y0;
    const float t = a[y0 * n + x0] + (a[y0 * n + x1] - a[y0 * n + x0]) * fx;
    const float b = a[y1 * n + x0] + (a[y1 * n + x1] - a[y1 * n + x0]) * fx;
    return t + (b - t) * fy;
}
static void wp81_tmake_atlas(Wp81T* t, const char* s, float cap)
{
    const float sc = cap / S81_CAP, f = (float)s81_fs / 512.0f;
    float units = 0.0f;
    for (const char* p = s; *p; p++) { const Wp81G* g = wp81_glyph(*p); units += g ? g->adv : S81_SPACE; }
    const int pad = 2 + (int)ceilf(8.0f * sc);
    const int w = (int)ceilf(units * sc) + 2 * pad, h = (int)ceilf(62.0f * sc) + 4;
    t->m = (u8*)calloc((size_t)w * (size_t)h, 1); if (!t->m) return;
    t->w = w; t->h = h; t->x0 = (float)pad; t->base = 2.0f + 48.0f * sc; t->wpx = units * sc;
    const float mag = (36.0f * f) / (cap > 1.0f ? cap : 1.0f);   /* atlas texels per output px */
    const int ns = mag > 2.4f ? 3 : (mag > 1.3f ? 2 : 1);
    float pen = (float)pad;
    for (const char* p = s; *p; p++) {
        const Wp81G* g = wp81_glyph(*p);
        if (!g) { pen += S81_SPACE * sc; continue; }
        const float X0 = pen + g->x0 * sc, X1 = pen + g->x1 * sc, Y0 = t->base + g->y0 * sc, Y1 = t->base + g->y1 * sc;
        const float uw = (float)(g->u1 - g->u0 + 2), vh = (float)(g->v1 - g->v0 + 2);
        int ya = (int)floorf(Y0), yb = (int)ceilf(Y1), xa = (int)floorf(X0), xb = (int)ceilf(X1);
        if (ya < 0) ya = 0; if (yb > h) yb = h; if (xa < 0) xa = 0; if (xb > w) xb = w;
        for (int y = ya; y < yb; y++)
            for (int x = xa; x < xb; x++) {
                float acc = 0.0f;
                for (int sy = 0; sy < ns; sy++)
                    for (int sx = 0; sx < ns; sx++) {
                        const float ty = ((float)y + (sy + 0.5f) / ns - Y0) / (Y1 - Y0), tx = ((float)x + (sx + 0.5f) / ns - X0) / (X1 - X0);
                        if (tx < 0.0f || tx > 1.0f || ty < 0.0f || ty > 1.0f) continue;
                        acc += wp81_bil(s81_fa, s81_fs, ((float)(g->u0 - 1) + tx * uw) * f - 0.5f, ((float)(g->v0 - 1) + ty * vh) * f - 0.5f);
                    }
                acc /= (float)(ns * ns);
                u8* q = t->m + (size_t)y * w + x;
                const int v = (int)(acc + 0.5f);
                if (v > *q) *q = (u8)(v > 255 ? 255 : v);
            }
        pen += g->adv * sc;
    }
}
static void wp81_tmake_gdi(Wp81T* t, const char* s, float cap)
{
    const float em = cap / 0.69f, sc = cap / S81_CAP;
    const int n = (int)strlen(s), pad = 2 + (int)ceilf(8.0f * sc);
    const int w = (int)ceilf((float)n * em * 0.95f) + 2 * pad, h = (int)ceilf(62.0f * sc) + 4;
    t->w = w; t->h = h; t->x0 = (float)pad; t->base = 2.0f + 48.0f * sc; t->wpx = 0.0f;
    t->m = (u8*)calloc((size_t)w * (size_t)h, 1); if (!t->m) return;
    HDC dc = CreateCompatibleDC(NULL); if (!dc) return;
    HFONT fb = CreateFontA(-(int)(em + 0.5f), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                           CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_ROMAN, "Palatino Linotype");
    HFONT fsm = CreateFontA(-(int)(em * 0.79f + 0.5f), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                            CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_ROMAN, "Palatino Linotype");
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bm = (w < 8192 && h < 2048) ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0) : NULL;
    if (bm && bits && fb && fsm) {
        HGDIOBJ ob = SelectObject(dc, bm);
        memset(bits, 0, (size_t)w * h * 4);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255)); SetTextAlign(dc, TA_BASELINE | TA_LEFT);
        float x = (float)pad;
        for (int i = 0; i < n; i++) {
            char c = s[i]; const int lo = (c >= 'a' && c <= 'z');
            if (lo) c = (char)(c - 32);
            SelectObject(dc, lo ? fsm : fb);
            TextOutA(dc, (int)(x + 0.5f), (int)(t->base + 0.5f), &c, 1);
            SIZE z; GetTextExtentPoint32A(dc, &c, 1, &z);
            x += (float)z.cx + em * 0.05f;
        }
        GdiFlush();
        const u8* p = (const u8*)bits;
        for (int i = 0; i < w * h; i++) {
            u8 a = p[i * 4]; if (p[i * 4 + 1] > a) a = p[i * 4 + 1]; if (p[i * 4 + 2] > a) a = p[i * 4 + 2];
            t->m[i] = a;
        }
        t->wpx = x - (float)pad - em * 0.05f;
        SelectObject(dc, ob);
    }
    if (bm) DeleteObject(bm);
    if (fb) DeleteObject(fb);
    if (fsm) DeleteObject(fsm);
    DeleteDC(dc);
}
typedef struct { Wp81T t; char s[80]; int key, gen; DWORD use; } Wp81TC;
static Wp81TC s81_tc[48];
static const Wp81T* wp81_txt(const char* s, float cap)
{
    const int key = (int)(cap * 64.0f + 0.5f), gen = s81_fs ? s81_gen : -1;
    Wp81TC* lru = &s81_tc[0];
    for (int i = 0; i < 48; i++) {
        Wp81TC* c = &s81_tc[i];
        if (c->t.m && c->key == key && c->gen == gen && !strcmp(c->s, s)) { c->use = GetTickCount(); return &c->t; }
        if (!c->t.m) { lru = c; break; }
        if (c->use < lru->use) lru = c;
    }
    free(lru->t.m); memset(&lru->t, 0, sizeof lru->t);
    if (s81_fs && s81_fa) wp81_tmake_atlas(&lru->t, s, cap); else wp81_tmake_gdi(&lru->t, s, cap);
    strncpy(lru->s, s, sizeof lru->s - 1); lru->s[sizeof lru->s - 1] = 0;
    lru->key = key; lru->gen = gen; lru->use = GetTickCount();
    return &lru->t;
}
/* draw a mask: pen origin x, baseline by (canvas px). sd = drop-shadow offset (game: +3,+3 @720p), ol = outline px */
static void wp81_tdraw(const Wp81T* t, float x, float by, const float* rgb, float a, float sd, float sa, float ol)
{
    if (!t || !t->m || a <= 0.0f) return;
    const int ox = (int)floorf(x - t->x0 + 0.5f), oy = (int)floorf(by - t->base + 0.5f);
    if (ol > 0.0f) {
        const int o = ol < 1.5f ? 1 : 2;
        for (int y = 0; y < t->h; y++)
            for (int xx = 0; xx < t->w; xx++) {
                int mx = 0;
                for (int dy = -o; dy <= o; dy++)
                    for (int dx = -o; dx <= o; dx++) {
                        const int sx = xx + dx, sy = y + dy;
                        if (sx < 0 || sy < 0 || sx >= t->w || sy >= t->h) continue;
                        const int v = t->m[sy * t->w + sx]; if (v > mx) mx = v;
                    }
                if (mx) wp47_px(ox + xx, oy + y, 0.0f, 0.0f, 0.0f, a * 0.8f * (float)mx / 255.0f);
            }
    }
    if (sd > 0.0f) {
        const int s = (int)(sd + 0.5f);
        for (int y = 0; y < t->h; y++)
            for (int xx = 0; xx < t->w; xx++) {
                const u8 m = t->m[y * t->w + xx];
                if (m) wp47_px(ox + xx + s, oy + y + s, 0.0f, 0.0f, 0.0f, a * sa * (float)m / 255.0f);
            }
    }
    for (int y = 0; y < t->h; y++)
        for (int xx = 0; xx < t->w; xx++) {
            const u8 m = t->m[y * t->w + xx];
            if (m) wp47_px(ox + xx, oy + y, rgb[0], rgb[1], rgb[2], a * (float)m / 255.0f);
        }
}

/* ---- shapes ---- */
static float wp81_dseg(float px, float py, float ax, float ay, float bx, float by)
{
    const float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float t = (wx * vx + wy * vy) / (vx * vx + vy * vy + 1e-6f); if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
    const float dx = wx - t * vx, dy = wy - t * vy;
    return sqrtf(dx * dx + dy * dy);
}
static void wp81_disc(float cx, float cy, float rx, float ry, float a)   /* the game's dark radial button disc */
{
    for (int y = (int)floorf(cy - ry - 1.0f); y <= (int)ceilf(cy + ry + 1.0f); y++)
        for (int x = (int)floorf(cx - rx - 1.0f); x <= (int)ceilf(cx + rx + 1.0f); x++) {
            const float u = (x + 0.5f - cx) / rx, v = (y + 0.5f - cy) / ry, d = sqrtf(u * u + v * v);
            if (d >= 1.0f) continue;
            const float c = 0.30f - 0.15f * d - 0.05f * v, al = d < 0.82f ? 1.0f : (1.0f - d) / 0.18f;
            wp47_px(x, y, c, c, c, a * 0.96f * al);
        }
}
enum { B81_CROSS, B81_CIRCLE, B81_TRI, B81_SQUARE, B81_START, B81_SELECT, B81_L3 };
static const float B81_COL[4][3] = { { 0.60f, 0.60f, 1.00f }, { 1.00f, 0.40f, 0.40f }, { 0.00f, 0.60f, 0.33f }, { 1.00f, 0.60f, 0.80f } };
static void wp81_sym(int kind, float cx, float cy, float r, float a)
{
    const float lw = r * 0.15f < 1.0f ? 1.0f : r * 0.15f;
    const float* col = kind <= B81_SQUARE ? B81_COL[kind] : NULL;
    static const float GR[3] = { 0.80f, 0.80f, 0.80f };
    if (!col) col = GR;
    for (int y = (int)floorf(cy - r - 2.0f); y <= (int)ceilf(cy + r + 2.0f); y++)
        for (int x = (int)floorf(cx - r - 2.0f); x <= (int)ceilf(cx + r + 2.0f); x++) {
            const float px = x + 0.5f, py = y + 0.5f, dx = px - cx, dy = py - cy;
            float cov = 0.0f, d;
            switch (kind) {
            case B81_CROSS: { const float q = r * 0.60f;
                d = wp81_dseg(px, py, cx - q, cy - q, cx + q, cy + q);
                const float d2 = wp81_dseg(px, py, cx - q, cy + q, cx + q, cy - q); if (d2 < d) d = d2;
                cov = lw * 0.5f + 0.5f - d; break; }
            case B81_CIRCLE: d = fabsf(sqrtf(dx * dx + dy * dy) - r * 0.62f); cov = lw * 0.5f + 0.5f - d; break;
            case B81_TRI: { const float ax = cx, ay = cy - r * 0.66f, bx = cx + r * 0.70f, by = cy + r * 0.50f, qx = cx - r * 0.70f;
                d = wp81_dseg(px, py, ax, ay, bx, by); float d2 = wp81_dseg(px, py, bx, by, qx, by); if (d2 < d) d = d2;
                d2 = wp81_dseg(px, py, qx, by, ax, ay); if (d2 < d) d = d2;
                cov = lw * 0.5f + 0.5f - d; break; }
            case B81_SQUARE: { const float hq = r * 0.52f, m = fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy);
                d = fabsf(m - hq); if (fabsf(dx) > hq + lw || fabsf(dy) > hq + lw) d = 99.0f; cov = lw * 0.5f + 0.5f - d; break; }
            case B81_START: { /* filled triangle pointing right */
                const float e1 = dx + r * 0.42f, e2 = (r * 0.52f - dx) * 0.55f - dy, e3 = (r * 0.52f - dx) * 0.55f + dy;
                float m = e1 < e2 ? e1 : e2; if (e3 < m) m = e3; cov = m + 0.5f; break; }
            case B81_SELECT: { /* small filled rounded bar */
                const float qx = fabsf(dx) - r * 0.50f, qy = fabsf(dy) - r * 0.26f;
                const float m = qx > qy ? qx : qy; cov = 0.5f - m; break; }
            default: break;
            }
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            wp47_px(x, y, col[0], col[1], col[2], a * cov);
        }
}
/* ---- winport v82a-game-buttons: drawing the game's button meshes ---- */
/* rasterise a triangle list in disc-local units at (cx,cy), sx/sy px per unit; tex: sample the disc texture by UV */
static void wp82_tris(const Wp82V* v, int n, float cx, float cy, float sx, float sy, int tex, const float* col, float a)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) {
        const float px = cx + v[i].x * sx, py = cy + v[i].y * sy;
        if (px < x0) x0 = px; if (px > x1) x1 = px; if (py < y0) y0 = py; if (py > y1) y1 = py;
    }
    const int bx = (int)floorf(x0), by = (int)floorf(y0), bw = (int)ceilf(x1) - bx + 1, bh = (int)ceilf(y1) - by + 1;
    if (bw <= 0 || bh <= 0 || bw > 1024 || bh > 1024) return;
    u16* mk = (u16*)calloc((size_t)bw * bh, sizeof(u16)); float* uv = tex ? (float*)calloc((size_t)bw * bh * 2u, sizeof(float)) : NULL;
    if (!mk || (tex && !uv)) { free(mk); free(uv); return; }
    for (int t = 0; t + 2 < n; t += 3) {
        const float ax = cx + v[t].x * sx, ay = cy + v[t].y * sy, bxx = cx + v[t + 1].x * sx, byy = cy + v[t + 1].y * sy;
        const float qx = cx + v[t + 2].x * sx, qy = cy + v[t + 2].y * sy;
        const float area = (bxx - ax) * (qy - ay) - (byy - ay) * (qx - ax);
        if (fabsf(area) < 1e-6f) continue;
        const int tx0 = (int)floorf(fminf(ax, fminf(bxx, qx))), tx1 = (int)ceilf(fmaxf(ax, fmaxf(bxx, qx)));
        const int ty0 = (int)floorf(fminf(ay, fminf(byy, qy))), ty1 = (int)ceilf(fmaxf(ay, fmaxf(byy, qy)));
        for (int y = ty0; y <= ty1; y++)
            for (int x = tx0; x <= tx1; x++) {
                const int ix = x - bx, iy = y - by;
                if (ix < 0 || iy < 0 || ix >= bw || iy >= bh) continue;
                u16 bits = 0;
                for (int s = 0; s < 16; s++) {
                    const float px = (float)x + ((s & 3) + 0.5f) * 0.25f, py = (float)y + ((s >> 2) + 0.5f) * 0.25f;
                    const float w0 = ((bxx - px) * (qy - py) - (byy - py) * (qx - px)) / area;
                    const float w1 = ((qx - px) * (ay - py) - (qy - py) * (ax - px)) / area;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) bits |= (u16)(1u << s);
                }
                if (!bits) continue;
                if (tex && !mk[iy * bw + ix]) {
                    const float px = x + 0.5f, py = y + 0.5f;
                    const float w0 = ((bxx - px) * (qy - py) - (byy - py) * (qx - px)) / area;
                    const float w1 = ((qx - px) * (ay - py) - (qy - py) * (ax - px)) / area;
                    const float w2 = 1.0f - w0 - w1;
                    uv[(iy * bw + ix) * 2] = w0 * v[t].u + w1 * v[t + 1].u + w2 * v[t + 2].u;
                    uv[(iy * bw + ix) * 2 + 1] = w0 * v[t].v + w1 * v[t + 1].v + w2 * v[t + 2].v;
                }
                mk[iy * bw + ix] |= bits;
            }
    }
    for (int iy = 0; iy < bh; iy++)
        for (int ix = 0; ix < bw; ix++) {
            const u16 m = mk[iy * bw + ix];
            if (!m) continue;
            int c = 0; for (u16 b = m; b; b &= (u16)(b - 1)) c++;
            const float cov = (float)c / 16.0f;
            if (!tex) { wp47_px(bx + ix, by + iy, col[0], col[1], col[2], a * col[3] * cov); continue; }
            float u = uv[(iy * bw + ix) * 2] * 128.0f - 0.5f, w = uv[(iy * bw + ix) * 2 + 1] * 128.0f - 0.5f;
            if (u < 0.0f) u = 0.0f; if (u > 127.0f) u = 127.0f; if (w < 0.0f) w = 0.0f; if (w > 127.0f) w = 127.0f;
            const int u0 = (int)u, v0 = (int)w, u1 = u0 < 127 ? u0 + 1 : u0, v1 = v0 < 127 ? v0 + 1 : v0;
            const float fu = u - (float)u0, fv = w - (float)v0;
            float px[4];
            for (int k = 0; k < 4; k++) {
                const float t0 = s82_dt[(v0 * 128 + u0) * 4 + k] + (s82_dt[(v0 * 128 + u1) * 4 + k] - s82_dt[(v0 * 128 + u0) * 4 + k]) * fu;
                const float t1 = s82_dt[(v1 * 128 + u0) * 4 + k] + (s82_dt[(v1 * 128 + u1) * 4 + k] - s82_dt[(v1 * 128 + u0) * 4 + k]) * fu;
                px[k] = (t0 + (t1 - t0) * fv) / 255.0f;
            }
            if (px[3] <= 0.004f) continue;
            wp47_px(bx + ix, by + iy, px[0] / px[3], px[1] / px[3], px[2] / px[3], a * px[3] * cov);
        }
    free(mk); free(uv);
}
/* the game's button at disc centre (cx,cy): 0 = not available, 1 = disc drawn (caller adds a symbol), 2 = disc + symbol */
static int wp82_btn(int kind, float cx, float cy, float cap, float a)
{
    if (!wp82_on() || !s82_dt_ok || !s82_ndc) return 0;
    const float sx = cap * 0.70f / 26.875f, sy = sx * 0.735f;   /* the game's disc is 34 x 25 px at footer size (720p) */
    static const float SH[4] = { 0.0f, 0.0f, 0.0f, 0.55f };
    static const float CR[4] = { 0.60f, 0.60f, 1.00f, 1.0f }, TR[4] = { 0.00f, 0.60f, 0.33f, 1.0f };   /* the game's colours */
    if (s82_nsh) wp82_tris(s82_sh, s82_nsh, cx, cy, sx, sy, 0, SH, a);
    wp82_tris(s82_dc, s82_ndc, cx, cy, sx, sy, 1, NULL, a);
    const int k = kind == B81_CROSS ? 0 : kind == B81_TRI ? 1 : -1;
    if (k >= 0 && s82_nsy[k]) { wp82_tris(s82_sy[k], s82_nsy[k], cx, cy, sx, sy, 0, k ? TR : CR, a); return 2; }
    return 1;
}
/* ---- end winport v82a-game-buttons drawing ---- */
static float wp81_btn_w(float cap) { return cap * 1.40f; }
/* a PS button glyph like the game's prompts: dark disc + symbol; cx = left edge; returns its width */
static float wp81_btn(int kind, float x, float by, float cap, float a)
{
    const float rx = cap * 0.70f, ry = cap * 0.52f, cx = x + rx, cy = by - cap * 0.37f;
    const int g82 = wp82_btn(kind, cx, cy, cap, a);   /* winport v82a-game-buttons: the game's own button when seen */
    if (!g82) wp81_disc(cx, cy, rx, ry, a);
    if (kind == B81_L3) {
        const Wp81T* t = wp81_txt("L3", cap * 0.66f);
        static const float GR[3] = { 0.85f, 0.85f, 0.85f };
        wp81_tdraw(t, cx - t->wpx * 0.5f, cy + cap * 0.66f * 0.5f, GR, a, 0.0f, 0.0f, 0.0f);
    } else if (g82 < 2) wp81_sym(kind, cx, cy, ry * 0.95f, a);
    return wp81_btn_w(cap);
}
static void wp81_ring(float cx, float cy, float r, float th, float prog, float a)   /* skip-hold progress, 12 o'clock clockwise */
{
    const float two_pi = 6.28318531f;
    for (int y = (int)floorf(cy - r - th); y <= (int)ceilf(cy + r + th); y++)
        for (int x = (int)floorf(cx - r - th); x <= (int)ceilf(cx + r + th); x++) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
            float c = th * 0.5f + 0.5f - fabsf(d - r); if (c <= 0.0f) continue; if (c > 1.0f) c = 1.0f;
            float ang = atan2f(dx, -dy); if (ang < 0.0f) ang += two_pi;
            if (prog > 0.0f && ang / two_pi <= prog) wp47_px(x, y, 0.85f, 0.85f, 0.85f, a * c);
            else wp47_px(x, y, 0.10f, 0.10f, 0.10f, a * c * 0.55f);
        }
}
static void wp81_arrow(float cx, float cy, float s, int dir, float a)   /* the game's grey value arrows */
{
    for (int y = (int)floorf(cy - s); y <= (int)ceilf(cy + s); y++)
        for (int x = (int)floorf(cx - s); x <= (int)ceilf(cx + s); x++) {
            const float dx = (x + 0.5f - cx) * (float)dir, dy = y + 0.5f - cy;
            const float e1 = dx + s * 0.45f, e2 = (s * 0.45f - dx) * 0.80f - dy, e3 = (s * 0.45f - dx) * 0.80f + dy;
            float m = e1 < e2 ? e1 : e2; if (e3 < m) m = e3;
            float c = m + 0.5f; if (c <= 0.0f) continue; if (c > 1.0f) c = 1.0f;
            const float g = dy < 0.0f ? 0.66f : 0.56f;
            wp47_px(x + 2, y + 2, 0.0f, 0.0f, 0.0f, a * c * 0.6f);
            wp47_px(x, y, g, g, g, a * c);
        }
}
static void wp81_plaque(float x0, float y0, float hw, float h)   /* cached skull plaque: left half + mirrored right half */
{
    if (!s81_pq_ok || !s81_pq) return;
    for (int y = (int)floorf(y0); y < (int)ceilf(y0 + h); y++) {
        float v = ((float)y + 0.5f - y0) / h * 128.0f - 0.5f; if (v < 0.0f) v = 0.0f; if (v > 127.0f) v = 127.0f;
        const int v0 = (int)v, v1 = v0 < 127 ? v0 + 1 : v0; const float fv = v - (float)v0;
        for (int x = (int)floorf(x0); x < (int)ceilf(x0 + 2.0f * hw); x++) {
            float u = ((float)x + 0.5f - x0) / hw; if (u > 1.0f) u = 2.0f - u;
            u = u * 256.0f - 0.5f; if (u < 0.0f) u = 0.0f; if (u > 255.0f) u = 255.0f;
            const int u0 = (int)u, u1 = u0 < 255 ? u0 + 1 : u0; const float fu = u - (float)u0;
            float c[4];
            for (int k = 0; k < 4; k++) {
                const float t = s81_pq[(v0 * 256 + u0) * 4 + k] + (s81_pq[(v0 * 256 + u1) * 4 + k] - s81_pq[(v0 * 256 + u0) * 4 + k]) * fu;
                const float b = s81_pq[(v1 * 256 + u0) * 4 + k] + (s81_pq[(v1 * 256 + u1) * 4 + k] - s81_pq[(v1 * 256 + u0) * 4 + k]) * fu;
                c[k] = (t + (b - t) * fv) / 255.0f;
            }
            if (c[3] <= 0.004f) continue;
            wp47_px(x, y, c[0] / c[3], c[1] / c[3], c[2] / c[3], c[3]);
        }
    }
}
static void wp81_vignette(int w, int h, float centre, float edge)
{
    const float cx = w * 0.5f, cy = h * 0.46f;
    for (int y = 0; y < h; y++) {
        u8* row = s_wp47_cv + (size_t)y * w * 4u;
        const float v = ((float)y + 0.5f - cy) / (h * 0.62f);
        for (int x = 0; x < w; x++) {
            const float u = ((float)x + 0.5f - cx) / (w * 0.62f);
            float d = sqrtf(u * u + v * v) - 0.35f; if (d < 0.0f) d = 0.0f; d /= 0.65f; if (d > 1.0f) d = 1.0f;
            const float a = centre + (edge - centre) * d * d;
            row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = 0; row[x * 4 + 3] = (u8)(a * 255.0f + 0.5f);
        }
    }
}

/* ---- colours from the game's menus (sampled at 2560x1440) ---- */
static const float C81_SEL[3] = { 0.72f, 0.72f, 0.72f }, C81_SELV[3] = { 0.80f, 0.80f, 0.80f };
static const float C81_DIM[3] = { 0.40f, 0.30f, 0.25f }, C81_FOOT[3] = { 0.80f, 0.80f, 0.80f };
static const float C81_NOTE[3] = { 0.55f, 0.43f, 0.35f }, C81_TITLE[3] = { 0.34f, 0.09f, 0.04f };

static float wp81_k(float* kr, float* sc)
{
    float k = (float)WP68_BH / 720.0f;
    if (k < 0.75f) k = 0.75f;
    if (k > 4.0f) k = 4.0f;
    *kr = k < 2.0f ? k : 2.0f; *sc = k / *kr;
    return k;
}
/* "(btn) = Label" pairs, centred (align 0) or right-aligned (align 1) at x; returns the total width */
typedef struct { int btn; const char* pre; const char* txt; } Wp81Pr;
static float wp81_prompts(const Wp81Pr* it, int n, float x, float by, float cap, float a, int align, int draw)
{
    const float gap = cap * 0.38f, sep = cap * 1.6f;
    float w = 0.0f;
    for (int i = 0; i < n; i++) {
        if (i) w += sep;
        if (it[i].pre && it[i].pre[0]) w += wp81_txt(it[i].pre, cap)->wpx + gap;
        if (it[i].btn >= 0) w += wp81_btn_w(cap) + gap;
        w += wp81_txt(it[i].txt, cap)->wpx;
    }
    if (!draw) return w;
    float cx = align ? x - w : x - w * 0.5f;
    for (int i = 0; i < n; i++) {
        if (i) cx += sep;
        if (it[i].pre && it[i].pre[0]) {
            const Wp81T* t = wp81_txt(it[i].pre, cap);
            wp81_tdraw(t, cx, by, C81_FOOT, a, cap * 0.10f, 0.75f, 0.0f);
            cx += t->wpx + gap;
        }
        if (it[i].btn >= 0) { wp81_btn(it[i].btn, cx, by, cap, a); cx += wp81_btn_w(cap) + gap; }
        const Wp81T* t = wp81_txt(it[i].txt, cap);
        wp81_tdraw(t, cx, by, C81_FOOT, a, cap * 0.10f, 0.75f, 0.0f);
        cx += t->wpx;
    }
    return w;
}

/* ---- Port Settings panel (full-screen canvas, 720p units * kr) ---- */
static Wp80Spr s81_sp[5];   /* 0 panel, 1 hint, 2 cutscene stack (bottom right), 3 cutscene centre, 4 fps */
/* ---- winport v92a-ckpt-list: thumbnail capture + list page ---- */
static ID3D12Resource* s92_rb; static u32 s92_rbw, s92_rbh, s92_rbp;
static volatile LONG s92_busy;     /* 0 idle, 1 copy queued, 2 worker reading */
static u64 s92_serial; static unsigned s92_fr; static LONG s92_hash, s92_lastreq; static DWORD s92_t11, s92_treq;
static DWORD WINAPI wp92_thumb_worker(LPVOID u)
{
    (void)u;
    void* m = NULL; D3D12_RANGE rr = { 0, (SIZE_T)s92_rbp * s92_rbh };
    if (SUCCEEDED(s92_rb->lpVtbl->Map(s92_rb, 0, &rr, &m)) && m) {
        static unsigned char out[S92_TW * S92_TH * 3];
        const u32 W = s92_rbw, H = s92_rbh;
        for (int ty = 0; ty < S92_TH; ty++) {
            const u32 y0 = (u32)((u64)ty * H / S92_TH), y1 = (u32)((u64)(ty + 1) * H / S92_TH);
            for (int tx = 0; tx < S92_TW; tx++) {
                const u32 x0 = (u32)((u64)tx * W / S92_TW), x1 = (u32)((u64)(tx + 1) * W / S92_TW);
                unsigned r = 0, g = 0, b = 0, n = 0;
                for (u32 y = y0; y < y1; y += (y1 - y0 > 4 ? 2 : 1))   /* every 2nd row/column above 4x: plenty for a thumbnail */
                    for (u32 x = x0; x < x1; x += (x1 - x0 > 4 ? 2 : 1)) {
                        const u8* p = (const u8*)m + (size_t)y * s92_rbp + (size_t)x * 4u;
                        r += p[0]; g += p[1]; b += p[2]; n++;
                    }
                if (!n) n = 1;
                unsigned char* o = out + ((size_t)ty * S92_TW + tx) * 3u;
                o[0] = (unsigned char)(r / n); o[1] = (unsigned char)(g / n); o[2] = (unsigned char)(b / n);
            }
        }
        D3D12_RANGE wr = { 0, 0 };
        s92_rb->lpVtbl->Unmap(s92_rb, 0, &wr);
        memcpy(wp92_thumb, out, sizeof out);
        InterlockedExchange(&wp92_thumb_hash, s92_hash);
        InterlockedCompareExchange(&wp92_snap_req, 0, s92_hash);
        fprintf(stderr, "[CKPT92] thumbnail captured for blob %08X (%ux%u -> %dx%d)\n", (unsigned)s92_hash, W, H, S92_TW, S92_TH); fflush(stderr);
    }
    InterlockedExchange(&s92_busy, 0);
    return 0;
}
/* once per presented frame, after the game frame is composed and before the port overlays (render thread, list open) */
static void wp92_thumb_tick(u32 fi)
{
    if (wp90_rows() < 5 || !s_d3d.device || !s_d3d.cmd_list) return;
    const DWORD now = GetTickCount();
    const unsigned st = wp92_gstate();
    if (st == 11u) { if (!s92_t11) s92_t11 = now ? now : 1; } else s92_t11 = 0;
    if (s92_busy == 1) {
        const int done = wp77_on() ? (s77_fence && s77_fence->lpVtbl->GetCompletedValue(s77_fence) >= s92_serial) : (++s92_fr >= 3u);
        if (!done) return;
        InterlockedExchange(&s92_busy, 2);
        HANDLE th = CreateThread(NULL, 64 * 1024, wp92_thumb_worker, NULL, 0, NULL);
        if (th) CloseHandle(th); else wp92_thumb_worker(NULL);
        return;
    }
    if (s92_busy) return;
    const LONG req = wp92_snap_req;
    if (!req) return;
    if (req != s92_lastreq) { s92_lastreq = req; s92_treq = now; }
    const int now1 = wp92_snap_now != 0;
    if (!now1 && !(st == 11u && s92_t11 && now - s92_t11 >= 1000u && now - s92_treq >= 200u && !wp80_pset_open && !s_wp_mv_active)) return;
    const u32 W = WP68_BW, H = WP68_BH;
    if (!s92_rb || s92_rbw != W || s92_rbh != H) {
        if (s92_rb) { wp77_idle(5); s92_rb->lpVtbl->Release(s92_rb); s92_rb = NULL; }
        s92_rbp = (W * 4u + 255u) & ~255u;
        D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd = {0};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = (u64)s92_rbp * H; rd.Height = 1; rd.DepthOrArraySize = 1;
        rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&s92_rb)) || !s92_rb) {
            s92_rb = NULL; InterlockedExchange(&wp92_snap_req, 0);
            fprintf(stderr, "[CKPT92] thumbnail readback buffer FAILED -> no thumbnails\n"); fflush(stderr);
            return;
        }
        s92_rbw = W; s92_rbh = H;
    }
    ID3D12GraphicsCommandList* cl = s_d3d.cmd_list;
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = s_d3d.render_targets[fi];
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cl->lpVtbl->ResourceBarrier(cl, 1, &b);
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
    dst.pResource = s92_rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width = W; dst.PlacedFootprint.Footprint.Height = H;
    dst.PlacedFootprint.Footprint.Depth = 1; dst.PlacedFootprint.Footprint.RowPitch = s92_rbp;
    src.pResource = s_d3d.render_targets[fi]; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
    cl->lpVtbl->CopyTextureRegion(cl, &dst, 0, 0, 0, &src, NULL);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE; b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cl->lpVtbl->ResourceBarrier(cl, 1, &b);
    s92_hash = req; s92_serial = s77_val + 2; s92_fr = 0;
    InterlockedExchange(&s92_busy, 1);
    fprintf(stderr, "[CKPT92] thumbnail copy queued for blob %08X (%s, state %u)\n", (unsigned)req, now1 ? "save, current frame" : "checkpoint frame", st);
    fflush(stderr);
}
/* <dir>\<name>.bmp (24-bit, 320x180, written by cellPad.c) -> RGB top row first; NULL when missing */
static unsigned char* wp92_load_bmp(const char* bin)
{
    char p[MAX_PATH + 112]; snprintf(p, sizeof p, "%s\\%s", wp90_dirp(), bin);
    char* d = strrchr(p, '.'); if (!d) return NULL;
    strcpy(d, ".bmp");
    FILE* f = fopen(p, "rb"); if (!f) return NULL;
    unsigned char h[54]; unsigned char* out = NULL;
    if (fread(h, 1, 54, f) == 54 && h[0] == 'B' && h[1] == 'M' && h[28] == 24) {
        const int w = h[18] | h[19] << 8, hh = h[22] | h[23] << 8;
        const unsigned off = h[10] | h[11] << 8 | h[12] << 16, rs = ((unsigned)w * 3u + 3u) & ~3u;
        if (w == S92_TW && hh == S92_TH && (out = (unsigned char*)malloc(S92_TW * S92_TH * 3))) {
            unsigned char row[S92_TW * 3 + 4];
            fseek(f, (long)off, SEEK_SET);
            for (int y = S92_TH - 1; y >= 0; y--) {
                if (fread(row, 1, rs, f) != rs) { free(out); out = NULL; break; }
                for (int x = 0; x < S92_TW; x++) { unsigned char* o = out + ((size_t)y * S92_TW + x) * 3u; o[0] = row[x * 3 + 2]; o[1] = row[x * 3 + 1]; o[2] = row[x * 3]; }
            }
        }
    }
    fclose(f);
    return out;
}
static void wp92_list_load(void)
{
    for (int i = 0; i < S90_NF; i++) { free(s92_th[i]); s92_th[i] = NULL; }
    wp90_list();
    for (int i = 0; i < s90_nf; i++) s92_th[i] = wp92_load_bmp(s90_fn[i]);
    if (s92_sel >= s90_nf) s92_sel = s90_nf > 0 ? s90_nf - 1 : 0;
}
static void wp92_list_open(void)
{
    s92_mode = 1; s92_sel = 0; s92_top = 0;
    wp92_list_load();
    InterlockedExchange(&wp92_sub, 1);
    int nt = 0; for (int i = 0; i < s90_nf; i++) nt += s92_th[i] != NULL;
    fprintf(stderr, "[CKPT92] list opened: %d checkpoint(s), %d with a thumbnail\n", s90_nf, nt); fflush(stderr);
    s80_redraw_p = 1;
}
static void wp92_list_close(void)
{
    s92_mode = 0;
    InterlockedExchange(&wp92_sub, 0);
    s80_redraw_p = 1;
}
static void wp92_delete(void)
{
    if (s92_sel < 0 || s92_sel >= s90_nf) return;
    char dd[MAX_PATH + 16], a[MAX_PATH + 112], b[MAX_PATH + 128];
    snprintf(dd, sizeof dd, "%s\\deleted", wp90_dirp());
    CreateDirectoryA(dd, NULL);
    int ok = 1;
    for (int k = 0; k < 2; k++) {
        snprintf(a, sizeof a, "%s\\%s", wp90_dirp(), s90_fn[s92_sel]);
        snprintf(b, sizeof b, "%s\\%s", dd, s90_fn[s92_sel]);
        if (k) { char* x = strrchr(a, '.'); char* y = strrchr(b, '.'); if (!x || !y) break; strcpy(x, ".bmp"); strcpy(y, ".bmp");
                 if (GetFileAttributesA(a) == INVALID_FILE_ATTRIBUTES) break; }
        if (!MoveFileExA(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) { ok = 0; fprintf(stderr, "[CKPT92] delete: cannot move %s (%lu)\n", a, GetLastError()); }
    }
    fprintf(stderr, "[CKPT92] delete: %s moved to %s (%s)\n", s90_fn[s92_sel], dd, ok ? "ok" : "FAILED"); fflush(stderr);
    wp92_list_load();
}
static void wp92_list_nav(LONG nav)
{
    if (!nav) return;
    if (s92_mode == 2) {   /* delete confirm */
        if (nav & 16) { wp92_delete(); s92_mode = 1; }
        else if (nav & 64) s92_mode = 1;
        s80_redraw_p = 1;
        return;
    }
    if (nav & 64) { wp92_list_close(); return; }
    if (s90_nf <= 0) return;
    if (nav & 1) s92_sel = (s92_sel + s90_nf - 1) % s90_nf;
    if (nav & 2) s92_sel = (s92_sel + 1) % s90_nf;
    if (nav & 32) s92_mode = 2;
    if (nav & 16) {
        snprintf(wp90_load, MAX_PATH, "%s\\%s", wp90_dirp(), s90_fn[s92_sel]);
        fprintf(stderr, "[CKPT92] panel: load %s in game\n", wp90_load); fflush(stderr);
        InterlockedExchange(&wp90_req, 2);
        wp92_list_close();
        InterlockedExchange(&wp92_close_req, 1);
    }
    s80_redraw_p = 1;
}
/* "ckpt_20261010_042712_rhod15_Rhod10.bin" -> area "Rhodes  (Rhod15 / Rhod10)", when "Oct 10, 2026   04:27" */
static void wp92_entry_text(const char* f, char* area, int ac, char* when, int wc)
{
    char d[16] = "", t[16] = "", n[64] = "";
    if (sscanf(f, "ckpt_%8[0-9]_%6[0-9]_%63[^.]", d, t, n) < 3 || strlen(d) < 8 || strlen(t) < 4) {
        snprintf(area, (size_t)ac, "%.40s", f); when[0] = 0; return;
    }
    char l1[32] = "", l2[32] = ""; char* u = strchr(n, '_');
    if (u) { *u = 0; snprintf(l1, sizeof l1, "%s", n); snprintf(l2, sizeof l2, "%s", u + 1); } else snprintf(l1, sizeof l1, "%s", n);
    if (l1[0] >= 'a' && l1[0] <= 'z') l1[0] = (char)(l1[0] - 32);
    if (l2[0] >= 'a' && l2[0] <= 'z') l2[0] = (char)(l2[0] - 32);
    const char* reg = !_strnicmp(l2[0] ? l2 : l1, "rhod", 4) ? "Rhodes" : NULL;   /* only the region we know by its level prefix */
    if (reg) snprintf(area, (size_t)ac, "%s  (%s%s%s)", reg, l1, l2[0] ? " / " : "", l2);
    else snprintf(area, (size_t)ac, "%s%s%s", l1, l2[0] ? " / " : "", l2);
    static const char* const MON[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    const int mo = (d[4] - '0') * 10 + (d[5] - '0'), dy = (d[6] - '0') * 10 + (d[7] - '0');
    snprintf(when, (size_t)wc, "%s %d, %.4s     %.2s:%.2s:%.2s", (mo >= 1 && mo <= 12) ? MON[mo - 1] : "?", dy, d, t, t + 2, t + 4);
}
static void wp92_tri(float cx, float cy, float s, int up, float a)   /* small up / down scroll marker */
{
    for (int y = (int)floorf(cy - s); y <= (int)ceilf(cy + s); y++) {
        float t = (y + 0.5f - (cy - s * 0.5f)) / s;   /* 0 .. 1 from top to bottom */
        if (t < 0.0f || t > 1.0f) continue;
        if (!up) t = 1.0f - t;                        /* apex at the top (up) or bottom (down) */
        const float hw = t * s * 0.65f;
        for (int x = (int)floorf(cx - s); x <= (int)ceilf(cx + s); x++) {
            float c = hw - fabsf(x + 0.5f - cx) + 0.5f; if (c <= 0.0f) continue; if (c > 1.0f) c = 1.0f;
            wp47_px(x, y, 0.62f, 0.62f, 0.62f, a * c);
        }
    }
}
static void wp92_blit_thumb(const unsigned char* t, float x0, float y0, float w, float h)
{
    const int X0 = (int)floorf(x0), Y0 = (int)floorf(y0), X1 = (int)ceilf(x0 + w), Y1 = (int)ceilf(y0 + h);
    for (int y = Y0; y < Y1; y++) {
        float v = ((float)y + 0.5f - y0) / h * S92_TH - 0.5f; if (v < 0.0f) v = 0.0f; if (v > S92_TH - 1) v = S92_TH - 1;
        const int v0 = (int)v, v1 = v0 < S92_TH - 1 ? v0 + 1 : v0; const float fv = v - (float)v0;
        for (int x = X0; x < X1; x++) {
            float u = ((float)x + 0.5f - x0) / w * S92_TW - 0.5f; if (u < 0.0f) u = 0.0f; if (u > S92_TW - 1) u = S92_TW - 1;
            const int u0 = (int)u, u1 = u0 < S92_TW - 1 ? u0 + 1 : u0; const float fu = u - (float)u0;
            float c[3];
            for (int k = 0; k < 3; k++) {
                const float a = t[((size_t)v0 * S92_TW + u0) * 3 + k] + (t[((size_t)v0 * S92_TW + u1) * 3 + k] - t[((size_t)v0 * S92_TW + u0) * 3 + k]) * fu;
                const float b = t[((size_t)v1 * S92_TW + u0) * 3 + k] + (t[((size_t)v1 * S92_TW + u1) * 3 + k] - t[((size_t)v1 * S92_TW + u0) * 3 + k]) * fu;
                c[k] = (a + (b - a) * fv) / 255.0f;
            }
            wp47_px(x, y, c[0], c[1], c[2], 1.0f);
        }
    }
}
static void wp92_build_list(Wp80Spr* sp, float kr)
{
    const int cw = (int)(1280.0f * kr + 0.5f), ch = (int)(720.0f * kr + 0.5f);
    if (!wp80_cv_begin(sp, cw, ch)) return;
    wp81_vignette(cw, ch, 0.86f, 0.94f);   /* winport v92a2-ckpt-list: darker, the game's text showed through */
    if (s81_pq_ok) {
        wp81_plaque(384.0f * kr, 30.0f * kr, 256.0f * kr, 108.0f * kr);
        const Wp81T* t = wp81_txt("Load Checkpoint", 39.19f * 0.47f * kr);   /* winport v92a2b-ckpt-title */
        wp81_tdraw(t, 640.0f * kr - t->wpx * 0.5f, 99.0f * kr, C81_TITLE, 1.0f, 2.0f * kr, 0.35f, 0.0f);
    } else {
        const Wp81T* t = wp81_txt("Load Checkpoint", 39.19f * 0.95f * kr);
        wp81_tdraw(t, 640.0f * kr - t->wpx * 0.5f, 112.0f * kr, C81_SELV, 1.0f, 3.0f * kr, 0.8f, 0.0f);
    }
    static const float PT[3] = { 0.16f, 0.05f, 0.03f }, PB[3] = { 0.07f, 0.02f, 0.01f };
    static const float DT[3] = { 0.09f, 0.07f, 0.06f }, DB[3] = { 0.04f, 0.03f, 0.03f };
    if (s90_nf <= 0) {
        const Wp81T* t = wp81_txt("No checkpoints saved yet", 39.19f * 0.70f * kr);
        wp81_tdraw(t, 640.0f * kr - t->wpx * 0.5f, 340.0f * kr, C81_SELV, 1.0f, 3.0f * kr, 0.8f, 0.0f);
        const Wp81T* n = wp81_txt("Save one with F6 or Save Checkpoint", 39.19f * 0.45f * kr);
        wp81_tdraw(n, 640.0f * kr - n->wpx * 0.5f, 384.0f * kr, C81_NOTE, 1.0f, 2.0f * kr, 0.8f, 0.0f);
    } else {
        enum { VIS = 4 };
        if (s92_sel < s92_top) s92_top = s92_sel;
        if (s92_sel >= s92_top + VIS) s92_top = s92_sel - VIS + 1;
        if (s92_top > s90_nf - VIS) s92_top = s90_nf - VIS > 0 ? s90_nf - VIS : 0;
        for (int i = s92_top; i < s90_nf && i < s92_top + VIS; i++) {
            const int sel = (i == s92_sel);
            const float y0 = (156.0f + 116.0f * (float)(i - s92_top)) * kr, x0 = 220.0f * kr, x1 = 1060.0f * kr, h = 104.0f * kr;   /* winport v92a2-ckpt-list */
            if (sel) {
                wp49_rrect(x0, y0, x1, y0 + h, 6.0f * kr, WP49_FRAME, WP49_GOLD_B, 0.95f);
                wp49_rrect(x0 + 2.0f * kr, y0 + 2.0f * kr, x1 - 2.0f * kr, y0 + h - 2.0f * kr, 5.0f * kr, PT, PB, 0.94f);
            } else wp49_rrect(x0, y0, x1, y0 + h, 6.0f * kr, DT, DB, 0.80f);
            const float tx = x0 + 8.0f * kr, ty = y0 + 7.0f * kr, tw = 160.0f * kr, th = 90.0f * kr;
            if (s92_th[i]) wp92_blit_thumb(s92_th[i], tx, ty, tw, th);
            else {
                wp49_rrect(tx, ty, tx + tw, ty + th, 2.0f * kr, DB, DB, 1.0f);
                const Wp81T* n = wp81_txt("No picture", 39.19f * 0.36f * kr);
                wp81_tdraw(n, tx + tw * 0.5f - n->wpx * 0.5f, ty + th * 0.5f + 5.0f * kr, C81_NOTE, 1.0f, 1.0f * kr, 0.8f, 0.0f);
            }
            char area[80], when[64];
            wp92_entry_text(s90_fn[i], area, sizeof area, when, sizeof when);
            const Wp81T* a = wp81_txt(area, 39.19f * (sel ? 0.56f : 0.52f) * kr);   /* winport v92a2-ckpt-list */
            wp81_tdraw(a, tx + tw + 24.0f * kr, y0 + 46.0f * kr, sel ? C81_SELV : C81_DIM, 1.0f, 2.5f * kr, 0.8f, 0.0f);
            const Wp81T* w = wp81_txt(when, 39.19f * 0.44f * kr);
            wp81_tdraw(w, tx + tw + 24.0f * kr, y0 + 82.0f * kr, sel ? C81_FOOT : C81_NOTE, 1.0f, 2.0f * kr, 0.8f, 0.0f);
        }
        if (s92_top > 0) wp92_tri(640.0f * kr, 142.0f * kr, 9.0f * kr, 1, 1.0f);
        if (s92_top + VIS < s90_nf) wp92_tri(640.0f * kr, 630.0f * kr, 9.0f * kr, 0, 1.0f);
        char cnt[24]; snprintf(cnt, sizeof cnt, "%d / %d", s92_sel + 1, s90_nf);
        const Wp81T* c = wp81_txt(cnt, 39.19f * 0.40f * kr);
        wp81_tdraw(c, 1060.0f * kr - c->wpx, 144.0f * kr, C81_NOTE, 1.0f, 1.5f * kr, 0.8f, 0.0f);
    }
    if (s92_mode == 2 && s92_sel < s90_nf) {   /* delete confirm, over the list */
        const float bx0 = 340.0f * kr, bx1 = 940.0f * kr, by0 = 280.0f * kr, by1 = 450.0f * kr;
        wp49_rrect(bx0, by0, bx1, by1, 6.0f * kr, WP49_FRAME, WP49_GOLD_B, 0.97f);
        wp49_rrect(bx0 + 2.0f * kr, by0 + 2.0f * kr, bx1 - 2.0f * kr, by1 - 2.0f * kr, 5.0f * kr, PT, PB, 0.97f);
        const Wp81T* q = wp81_txt("Delete this checkpoint?", 39.19f * 0.66f * kr);
        wp81_tdraw(q, 640.0f * kr - q->wpx * 0.5f, 334.0f * kr, C81_SELV, 1.0f, 2.5f * kr, 0.8f, 0.0f);
        char area[80], when[64]; wp92_entry_text(s90_fn[s92_sel], area, sizeof area, when, sizeof when);
        const Wp81T* n = wp81_txt(when, 39.19f * 0.42f * kr);
        wp81_tdraw(n, 640.0f * kr - n->wpx * 0.5f, 372.0f * kr, C81_NOTE, 1.0f, 2.0f * kr, 0.8f, 0.0f);
        const Wp81Pr it[2] = { { B81_CROSS, NULL, "= Yes" }, { B81_CIRCLE, NULL, "= No" } };
        wp81_prompts(it, 2, 640.0f * kr, 422.0f * kr, 39.19f * 0.56f * kr, 1.0f, 0, 1);
    }
    {   const Wp81Pr it[3] = { { B81_CROSS, NULL, "= Load" }, { B81_SQUARE, NULL, "= Delete" }, { B81_CIRCLE, NULL, "= Back" } };
        if (s90_nf > 0) wp81_prompts(it, 3, 640.0f * kr, 668.0f * kr, 39.19f * 0.625f * kr, 1.0f, 0, 1);
        else wp81_prompts(it + 2, 1, 640.0f * kr, 668.0f * kr, 39.19f * 0.625f * kr, 1.0f, 0, 1);
        const Wp81T* t = wp81_txt("Keyboard:  Arrows  Enter  Del  Esc", 39.19f * 0.36f * kr);
        wp81_tdraw(t, 640.0f * kr - t->wpx * 0.5f, 701.0f * kr, C81_NOTE, 1.0f, 1.5f * kr, 0.8f, 0.0f); }
    wp80_cv_end(sp);
    { static int n_ = 0; if (n_++ < 6) { fprintf(stderr, "[CKPT92] list page drawn: mode %d, entry %d of %d\n", s92_mode, s92_sel + 1, s90_nf); fflush(stderr); } }
}
/* ---- end winport v92a-ckpt-list ---- */
static int s81_pkey = -1, s81_hkey = -1;
static void wp81_build_panel(Wp80Spr* sp, float kr)
{
    if (s92_mode) { wp92_build_list(sp, kr); return; }   /* winport v92a-ckpt-list */
    const int cw = (int)(1280.0f * kr + 0.5f), ch = (int)(720.0f * kr + 0.5f);
    if (!wp80_cv_begin(sp, cw, ch)) return;
    wp81_vignette(cw, ch, 0.70f, 0.88f);
    /* header: the game's plaque with the title carved in it; else a plain title */
    if (s81_pq_ok) {
        wp81_plaque(384.0f * kr, 30.0f * kr, 256.0f * kr, 108.0f * kr);
        const Wp81T* t = wp81_txt("Port Settings", 39.19f * 0.60f * kr);
        wp81_tdraw(t, 640.0f * kr - t->wpx * 0.5f, 99.0f * kr, C81_TITLE, 1.0f, 2.0f * kr, 0.35f, 0.0f);
    } else {
        const Wp81T* t = wp81_txt("Port Settings", 39.19f * 0.95f * kr);
        wp81_tdraw(t, 640.0f * kr - t->wpx * 0.5f, 112.0f * kr, C81_SELV, 1.0f, 3.0f * kr, 0.8f, 0.0f);
    }
    /* rows: label right-aligned at 700, value centred at 940 (the game's Options screen layout) */
    char vs[6][40], ns[6][48];
    const int bl = wp80_live_borderless();
    if (s80_cap_cur == 0) snprintf(vs[0], 40, "V-Sync");
    else if (s80_cap_cur >= 1000) snprintf(vs[0], 40, "Unlimited");
    else snprintf(vs[0], 40, "%d", s80_cap_cur);
    ns[0][0] = 0;
    if (s80_cap_start <= 0 && s80_cap_cur > 0) snprintf(ns[0], 48, "Applies after restart");
    snprintf(vs[1], 40, "%s", bl ? "Borderless" : "Windowed"); ns[1][0] = 0;
    snprintf(vs[2], 40, "%dx", s80_res_sel);
    if (fabsf((float)s80_res_sel - s_wp68_s) > 0.01f) snprintf(ns[2], 48, "Applies after restart"); else ns[2][0] = 0;
    snprintf(vs[3], 40, "%s", s_wp69_on ? "On" : "Off"); ns[3][0] = 0;
    /* winport v90-ckpt: row 4 */
    snprintf(vs[4], 40, "%s", s90_pending ? "Saving..." : (wp90_gen > 0 ? "Saved" : "Press X"));
    if (wp90_gen > 0 && !s90_pending) snprintf(ns[4], 48, "%.47s", wp90_msg); else snprintf(ns[4], 48, "%s", wp90_have ? "Keyboard: F6 anywhere" : "No checkpoint yet");
    /* winport v90b-ckpt-load: row 5 */
    if (s90_nf > 0) wp90_label(s90_fn[s90_fi], vs[5], 40); else snprintf(vs[5], 40, "None saved");
    if (s90_lpending) snprintf(ns[5], 48, "Restarting...");
    else if (s90_nf > 0) snprintf(ns[5], 48, "X = choose from %d saved", s90_nf);   /* winport v92a-ckpt-list */
    else snprintf(ns[5], 48, "Save one first (row above or F6)");
    static const char* const LAB[6] = { "Frame Rate Cap:", "Display Mode:", "Resolution Scale:", "FPS Counter:", "Save Checkpoint:", "Load Checkpoint:" };
    for (int i = 0; i < wp90_rows(); i++) {
        const int sel = (i == s80_sel);
        const float by = (wp90_rows() == 6 ? 226.0f + 72.0f * (float)i : 244.0f + 88.0f * (float)i) * kr;   /* winport v90b-ckpt-load */
        const Wp81T* l = wp81_txt(LAB[i], 39.19f * (sel ? 0.90f : 0.76f) * kr);
        wp81_tdraw(l, 700.0f * kr - l->wpx, by, sel ? C81_SEL : C81_DIM, 1.0f, 3.0f * kr, 0.8f, 0.0f);
        const Wp81T* v = wp81_txt(vs[i], 39.19f * (sel ? 0.82f : 0.70f) * kr);
        const float vx = 940.0f * kr - v->wpx * 0.5f;
        wp81_tdraw(v, vx, by, sel ? C81_SELV : C81_DIM, 1.0f, 3.0f * kr, 0.8f, 0.0f);
        if (sel && i != 4) {
            const float vc = 39.19f * 0.82f * kr, as = vc * 0.78f, ay = by - vc * 0.50f, off = v->wpx * 0.5f + 26.0f * kr + as;
            wp81_arrow(940.0f * kr - off, ay, as, -1, 1.0f);
            wp81_arrow(940.0f * kr + off, ay, as, +1, 1.0f);
        }
        if (ns[i][0]) {
            const Wp81T* n = wp81_txt(ns[i], 39.19f * 0.42f * kr);
            wp81_tdraw(n, 940.0f * kr - n->wpx * 0.5f, by + 27.0f * kr, C81_NOTE, 1.0f, 2.0f * kr, 0.8f, 0.0f);
        }
    }
    /* footer like the game's: (X) = Change   (O) = Back; keyboard keys underneath, quieter */
    {   const Wp81Pr it[2] = { { B81_CROSS, NULL, "= Change" }, { B81_TRI, NULL, "= Back" } };
        wp81_prompts(it, 2, 640.0f * kr, 668.0f * kr, 39.19f * 0.625f * kr, 1.0f, 0, 1);
        const Wp81T* t = wp81_txt("Keyboard:  Arrows  Enter  Esc / F1", 39.19f * 0.36f * kr);
        wp81_tdraw(t, 640.0f * kr - t->wpx * 0.5f, 701.0f * kr, C81_NOTE, 1.0f, 1.5f * kr, 0.8f, 0.0f); }
    wp80_cv_end(sp);
}
static void wp81_build_hint(Wp80Spr* sp, float kr)   /* canvas 520x70 units, content right-aligned, baseline 48 */
{
    const int cw = (int)(520.0f * kr + 0.5f), ch = (int)(70.0f * kr + 0.5f);
    if (!wp80_cv_begin(sp, cw, ch)) return;
    const Wp81Pr it[1] = { { B81_L3, NULL, "/ F1 = Port Settings" } };
    wp81_prompts(it, 1, (float)cw - 6.0f * kr, 48.0f * kr, 39.19f * 0.46f * kr, 0.78f, 1, 1);
    wp80_cv_end(sp);
}
/* once per presented frame (wp80_panel, after the v80 input handling) */
static void wp81_panel_draw(D3D12_CPU_DESCRIPTOR_HANDLE bb, int open)
{
    if (!open && !wp80_pset_hint) return;
    if (s_wp47_broken || !s_d3d.initialized || !s_d3d.cmd_list || !wp_blit_init() || !wp47_psos()) return;
    float kr, sc; const float k = wp81_k(&kr, &sc);
    const float full[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    if (open) {
        Wp80Spr* sp = &s81_sp[0];
        const int key = (int)(kr * 100.0f) * 4096 + s81_gen * 2 + s81_pq_ok;
        if (s80_redraw_p || key != s81_pkey || !sp->cv) { wp81_build_panel(sp, kr); s81_pkey = key; s80_redraw_p = 0; }
        ID3D12Resource* tex = wp80_spr_upload(sp);
        if (!tex) return;
        const float f[4] = { 0.14f, 0.14f, 0.14f, 1.0f };   /* head: 0.36 -> 0.14, the game's pause menu text showed through */
        s_d3d.cmd_list->lpVtbl->OMSetBlendFactor(s_d3d.cmd_list, f);
        wp49_quad(bb, s_wp49_dim_pso, tex, full, 0.0f, 0.0f, (float)WP68_BW, (float)WP68_BH);
        wp49_quad(bb, s_wp47_pso, tex, full, 0.0f, 0.0f, (float)WP68_BW, (float)WP68_BH);
        { static int n_ = 0; if (n_++ < 2) { fprintf(stderr, "[UI81] drew panel %dx%d (k %.2f, %s font, plaque %d)\n", sp->cw, sp->ch, k,
                                                      s81_fs ? "game" : "fallback", s81_pq_ok); fflush(stderr); } }
    } else {
        Wp80Spr* sp = &s81_sp[1];
        const int key = (int)(kr * 100.0f) * 4096 + s81_gen * 2;
        if (key != s81_hkey || !sp->cv) { wp81_build_hint(sp, kr); s81_hkey = key; }
        ID3D12Resource* tex = wp80_spr_upload(sp);
        if (!tex) return;
        const float w = (float)sp->cw * sc, h = (float)sp->ch * sc;
        wp49_quad(bb, s_wp47_pso, tex, full, floorf(1256.0f * k - w), floorf(22.0f * k), w, h);   /* top right: clear of the game's footer */
        { static int n_ = 0; if (n_++ < 2) { fprintf(stderr, "[UI81] drew hint (%s font)\n", s81_fs ? "game" : "fallback"); fflush(stderr); } }
    }
}

/* ---- cutscene UI (v47/v49 state) ---- */
static int s81_akey = -1, s81_bkey = -1;
static void wp81_csui_draw(D3D12_CPU_DESCRIPTOR_HANDLE bb, float pa, float prog, int paused, int native, float na)
{
    float kr, sc; const float k = wp81_k(&kr, &sc);
    const float full[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    const float cap = 39.19f * 0.625f * kr;
    /* A: bottom-right stack, canvas 620x190 units, prompt baseline 168 */
    if (pa > 0.0f || native || na > 0.0f) {
        Wp80Spr* sp = &s81_sp[2];
        const int key = ((((int)(pa * 32.0f) * 70 + (int)(prog * 64.0f)) * 2 + native) * 40 + (int)(na * 32.0f)) * 64 + s81_gen % 64 + (int)(kr * 100.0f) * 10000000;
        if (key != s81_akey || !sp->cv) {
            const int cw = (int)(620.0f * kr + 0.5f), ch = (int)(190.0f * kr + 0.5f);
            if (wp80_cv_begin(sp, cw, ch)) {
                const float R = (float)cw - 6.0f * kr;
                float by = 168.0f * kr;
                if (pa > 0.0f) {
                    const Wp81T* t1 = wp81_txt("to Skip", cap); const Wp81T* t0 = wp81_txt("Hold", cap);
                    const float gap = cap * 0.62f, bw = wp81_btn_w(cap);   /* wider: the ring is larger than the disc */
                    float x = R - t1->wpx; wp81_tdraw(t1, x, by, C81_FOOT, pa, cap * 0.10f, 0.75f, 0.0f);
                    x -= gap + bw;
                    wp81_btn(B81_START, x, by, cap, pa);
                    wp81_ring(x + cap * 0.70f, by - cap * 0.37f, cap * 0.86f, cap * 0.12f, prog, pa);
                    x -= gap + t0->wpx; wp81_tdraw(t0, x, by, C81_FOOT, pa, cap * 0.10f, 0.75f, 0.0f);
                    by -= 46.0f * kr;
                }
                if (native) {
                    const Wp81Pr it[1] = { { B81_SELECT, NULL, "= Resume Movie" } };
                    wp81_prompts(it, 1, R, by, cap, 1.0f, 1, 1);
                    by -= 46.0f * kr;
                }
                if (na > 0.0f) {
                    const Wp81T* t = wp81_txt("Pause is not available here", cap * 0.75f);
                    wp81_tdraw(t, R - t->wpx, by, C81_FOOT, na, cap * 0.10f, 0.75f, 0.0f);
                }
                wp80_cv_end(sp); s81_akey = key;
            }
        }
        ID3D12Resource* tex = wp80_spr_upload(sp);
        if (tex) {
            const float w = (float)sp->cw * sc, h = (float)sp->ch * sc;
            wp49_quad(bb, s_wp47_pso, tex, full, floorf(1256.0f * k - w), floorf(690.0f * k - h), w, h);
        }
    }
    /* B: host pause -- centred "Paused" + footer (full-screen canvas, redrawn only when it changes) */
    if (paused && !native) {
        Wp80Spr* sp = &s81_sp[3];
        const int key = (int)(kr * 100.0f) * 4096 + s81_gen;
        if (key != s81_bkey || !sp->cv) {
            const int cw = (int)(1280.0f * kr + 0.5f), ch = (int)(720.0f * kr + 0.5f);
            if (wp80_cv_begin(sp, cw, ch)) {
                const Wp81T* t = wp81_txt("Paused", 39.19f * 1.125f * kr);
                wp81_tdraw(t, 640.0f * kr - t->wpx * 0.5f, 372.0f * kr, C81_SEL, 1.0f, 3.0f * kr, 0.8f, 0.0f);
                const Wp81Pr it[2] = { { B81_SELECT, NULL, "= Resume" }, { B81_START, "Hold", "= Skip" } };
                wp81_prompts(it, 2, 640.0f * kr, 668.0f * kr, cap, 1.0f, 0, 1);
                wp80_cv_end(sp); s81_bkey = key;
            }
        }
        ID3D12Resource* tex = wp80_spr_upload(sp);
        if (tex) wp49_quad(bb, s_wp47_pso, tex, full, 0.0f, 0.0f, (float)WP68_BW, (float)WP68_BH);
    }
    { static int n_ = 0; if (n_++ < 2) { fprintf(stderr, "[UI81] drew cutscene UI (prompt %.2f ring %.2f paused %d native %d note %.2f, %s font)\n",
                                                  pa, prog, paused, native, na, s81_fs ? "game" : "fallback"); fflush(stderr); } }
}

/* ---- fps counter: "165 fps  6.0 ms", game font, outlined, top left ---- */
static char s81_ftxt[40]; static int s81_fkey = -1;
static void wp81_fps_draw(D3D12_CPU_DESCRIPTOR_HANDLE bb)
{
    if (s_wp47_broken || !s_d3d.initialized || !s_d3d.cmd_list || !wp_blit_init() || !wp47_psos()) return;
    float kr, sc; const float k = wp81_k(&kr, &sc);
    int fps = 0; float ms = 0.0f;
    if (sscanf(s_wp69_txt, "%d FPS %f MS", &fps, &ms) != 2) return;
    char t[40]; snprintf(t, sizeof t, "%d fps   %.1f ms", fps, ms);
    Wp80Spr* sp = &s81_sp[4];
    const int key = (int)(kr * 100.0f) * 4096 + s81_gen;
    if (strcmp(t, s81_ftxt) || key != s81_fkey || !sp->cv) {
        const int cw = (int)(300.0f * kr + 0.5f), ch = (int)(40.0f * kr + 0.5f);
        if (wp80_cv_begin(sp, cw, ch)) {
            const Wp81T* m = wp81_txt(t, 39.19f * 0.40f * kr);
            wp81_tdraw(m, 6.0f * kr, 28.0f * kr, C81_SELV, 1.0f, 1.5f * kr, 0.8f, kr >= 1.4f ? 2.0f : 1.0f);
            wp80_cv_end(sp);
        }
        strcpy(s81_ftxt, t); s81_fkey = key;
    }
    ID3D12Resource* tex = wp80_spr_upload(sp);
    if (!tex) return;
    const float full[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    wp49_quad(bb, s_wp47_pso, tex, full, floorf(8.0f * k), floorf(6.0f * k), (float)sp->cw * sc, (float)sp->ch * sc);
}

/* ---- panel keys handled here while the game does not read the pad (boot screens) ---- */
static void wp81_keys_fallback(void)
{
    if (!wp81_ui()) return;
    { const char* e = getenv("WINPORT_PSET81_ANYWHERE"); if (e && e[0] == '0') return; }
    const DWORD now = GetTickCount(), pt = (DWORD)wp81_pad_tick;
    static DWORD t_first, t_bt; static int bt_ms = -1, bt_step, logged;
    if (!t_first) t_first = now;
    if (pt && now - pt < 300u) return;
    unsigned keys = 0;   /* 1 F1, 2 Esc, 4 Up, 8 Down, 16 Left, 32 Right, 64 Enter */
    if (wp69_fg_ok()) {
        if (GetAsyncKeyState(VK_F1) & 0x8000) keys |= 1; if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) keys |= 2;
        if (GetAsyncKeyState(VK_UP) & 0x8000) keys |= 4; if (GetAsyncKeyState(VK_DOWN) & 0x8000) keys |= 8;
        if (GetAsyncKeyState(VK_LEFT) & 0x8000) keys |= 16; if (GetAsyncKeyState(VK_RIGHT) & 0x8000) keys |= 32;
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) keys |= 64;
    }
    if (bt_ms < 0) { const char* e = getenv("WINPORT_PSET81_BOOTTEST"); bt_ms = (e && atoi(e) > 0) ? atoi(e) : 0; }
    unsigned syn = 0;
    if (bt_ms && bt_step < 3 && now - t_first >= (DWORD)bt_ms + 2500u * (DWORD)bt_step && now - t_bt > 400u) {
        syn = bt_step == 0 ? 1u : bt_step == 1 ? 8u : 2u; bt_step++; t_bt = now;
        fprintf(stderr, "[UI81] boot test key %s (pad silent)\n", syn == 1 ? "F1" : syn == 8 ? "Down" : "Esc"); fflush(stderr);
    }
    static unsigned prev;
    const unsigned e = (keys & ~prev) | syn;
    prev = keys;
    if (!e) return;
    if (!logged) { logged = 1; fprintf(stderr, "[UI81] the game is not reading the pad -> Port Settings keys handled by the renderer\n"); fflush(stderr); }
    if (!wp80_pset_open) {
        if (e & 1) { InterlockedExchange(&wp80_pset_nav, 0); InterlockedExchange(&wp80_pset_open, 1); fprintf(stderr, "[PSET80] panel OPEN (F1, renderer: pad silent)\n"); fflush(stderr); }
        return;
    }
    if (e & 3) { InterlockedExchange(&wp80_pset_open, 0); fprintf(stderr, "[PSET80] panel CLOSE (F1/Esc, renderer)\n"); fflush(stderr); return; }
    LONG nav = 0;
    if (e & 4) nav |= 1; if (e & 8) nav |= 2; if (e & 16) nav |= 4; if (e & 32) nav |= 8; if (e & 64) nav |= 16;
    if (nav) InterlockedOr(&wp80_pset_nav, nav);
}
/* ---- end winport v81-ui ---- */

/* ---- winport v90-ckpt: toast after a checkpoint save (panel closed) ---- */
static Wp80Spr s90_sp;
static LONG s90_tgen;
static DWORD s90_tt;
static int s90_tkey = -1;
static char s90_tmsg[200];
static void wp90_toast(D3D12_CPU_DESCRIPTOR_HANDLE bb)
{
    if (wp90_rows() < 5) return;
    const LONG g = wp90_gen;
    if (g != s90_tgen) { s90_tgen = g; s90_tt = GetTickCount(); if (!s90_tt) s90_tt = 1; snprintf(s90_tmsg, sizeof s90_tmsg, "%s", wp90_msg); s90_tkey = -1; }
    if (!s90_tt || GetTickCount() - s90_tt > 3500u) return;
    if (s_wp47_broken || !s_d3d.initialized || !s_d3d.cmd_list || !wp_blit_init() || !wp47_psos()) return;
    float kr, sc; const float k = wp81_k(&kr, &sc);
    Wp80Spr* sp = &s90_sp;
    const int key = ((int)(kr * 100.0f) * 4096 + s81_gen * 2) ^ (int)(g * 7919);
    if (key != s90_tkey || !sp->cv) {
        const Wp81T* t = wp81_txt(s90_tmsg, 39.19f * 0.52f * kr);
        const int cw = (int)(t->wpx + 56.0f * kr + 0.5f), ch = (int)(58.0f * kr + 0.5f);
        if (!wp80_cv_begin(sp, cw, ch)) return;
        static const float PT[3] = { 0.16f, 0.05f, 0.03f }, PB[3] = { 0.07f, 0.02f, 0.01f };
        wp49_rrect(0.5f, 0.5f, (float)cw - 0.5f, (float)ch - 0.5f, 5.0f * kr, WP49_FRAME, WP49_GOLD_B, 0.95f);
        wp49_rrect(2.0f * kr, 2.0f * kr, (float)cw - 2.0f * kr, (float)ch - 2.0f * kr, 4.0f * kr, PT, PB, 0.92f);
        wp81_tdraw(t, 28.0f * kr, 39.0f * kr, C81_SELV, 1.0f, 2.0f * kr, 0.8f, 0.0f);
        wp80_cv_end(sp);
        s90_tkey = key;
    }
    ID3D12Resource* tex = wp80_spr_upload(sp);
    if (!tex) return;
    const float full[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    const float w = (float)sp->cw * sc, h = (float)sp->ch * sc;
    wp49_quad(bb, s_wp47_pso, tex, full, floorf(640.0f * k - w * 0.5f), floorf(36.0f * k), w, h);
    { static int n_ = 0; if (n_++ < 2) { fprintf(stderr, "[CKPT90] toast drawn: %s\n", s90_tmsg); fflush(stderr); } }
}
/* ---- end winport v90-ckpt toast ---- */
/* once per presented frame on the render thread, after wp69_fps_overlay */
static void wp80_panel(D3D12_CPU_DESCRIPTOR_HANDLE bb_rtv)
{
    if (!s80_init) wp80_init();
    if (!s80_on || s80_broken) return;
    wp81_keys_fallback();   /* winport v81-ui */
    const int open = wp80_pset_open != 0;
    if (open && !s80_was_open) {
        s80_sel = 0; s80_dirty = 0; s80_redraw_p = 1;
        if (wp90_rows() == 6) { wp90_list(); s90_lpending = 0; }   /* winport v90b-ckpt-load */
        if (s92_mode) wp92_list_close();   /* winport v92a-ckpt-list */
        fprintf(stderr, "[PSET80] panel visible (cap %d%s, res %dX, borderless %d, fps counter %d)\n", s80_cap_cur,
                s80_cap_start > 0 ? "" : " NEEDS RESTART", s80_res_sel, wp80_live_borderless(), s_wp69_on); fflush(stderr);
    }
    if (open) {
        const LONG nav = InterlockedExchange(&wp80_pset_nav, 0);
        if (s92_mode) wp92_list_nav(nav); else {   /* winport v92a-ckpt-list */
        { const int nr = wp90_rows();   /* winport v90-ckpt */
          if (nav & 1) { s80_sel = (s80_sel + nr - 1) % nr; s80_redraw_p = 1; }
          if (nav & 2) { s80_sel = (s80_sel + 1) % nr; s80_redraw_p = 1; }
          if (s90_pending && wp90_gen != s90_pgen) { s90_pending = 0; s80_redraw_p = 1; if (nr == 6) wp90_list(); } }
        if (nav & 4) wp80_step(-1);
        if (s80_sel == 5 && wp90_rows() == 6 && (nav & 16)) wp92_list_open();   /* winport v92a-ckpt-list: list page (was: restart) */
        else if (nav & (8 | 16)) wp80_step(+1);
        }   /* winport v92a-ckpt-list */
    } else if (s80_was_open) {
        if (s92_mode) wp92_list_close();   /* winport v92a-ckpt-list */
        fprintf(stderr, "[PSET80] panel closed (%s)\n", s80_dirty ? "settings changed" : "no change"); fflush(stderr);
        if (s80_dirty) wp80_save();
        s80_dirty = 0;
    }
    s80_was_open = open;
    if (wp81_ui()) { wp81_panel_draw(bb_rtv, open); if (!open) wp90_toast(bb_rtv); return; }   /* winport v81-ui + winport v90-ckpt */
    if (!open && !wp80_pset_hint) return;
    if (s_wp47_broken || !s_d3d.initialized || !s_d3d.cmd_list || !wp_blit_init() || !wp47_psos()) return;
    float k = (float)WP68_BH / 720.0f;
    if (k < 0.75f) k = 0.75f;
    if (k > 4.0f) k = 4.0f;
    const float kr = k < 2.0f ? k : 2.0f, sc = k / kr;
    const float full[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    Wp80Spr* sp = &s80_sp[open ? 0 : 1];
    if (!(open ? wp80_build_panel(sp, kr) : wp80_build_hint(sp, kr))) return;
    ID3D12Resource* tex = wp80_spr_upload(sp);
    if (!tex) return;
    const float w = (float)sp->cw * sc, h = (float)sp->ch * sc;
    float vx, vy;
    if (open) {
        const float f[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
        s_d3d.cmd_list->lpVtbl->OMSetBlendFactor(s_d3d.cmd_list, f);
        wp49_quad(bb_rtv, s_wp49_dim_pso, tex, full, 0.0f, 0.0f, (float)WP68_BW, (float)WP68_BH);
        vx = floorf(((float)WP68_BW - w) * 0.5f); vy = floorf(((float)WP68_BH - h) * 0.5f);
    } else {
        vx = floorf((float)WP68_BW - w - 40.0f * k); vy = floorf((float)WP68_BH - h - 26.0f * k);   /* bottom-right: clear of the game's X/Triangle prompts */
    }
    if (vx < 0.0f) vx = 0.0f;
    if (vy < 0.0f) vy = 0.0f;
    wp49_quad(bb_rtv, s_wp47_pso, tex, full, vx, vy, w, h);
    { static int n_ = 0; if (n_++ < 3) { fprintf(stderr, "[PSET80] drew %s %dx%d at %.0f,%.0f (scale %.2f)\n", open ? "panel" : "hint", sp->cw, sp->ch, vx, vy, sc); fflush(stderr); } }
}
/* ---- end winport v80-port-settings ---- */

static void winport_movie_present_only(void)   /* winport-fix: movie present without guest flips */
{
    { extern void wp59_d3d_check(const char*); wp59_d3d_check("movie_present_only"); }   /* winport v59-async-rsx */
    if (!s_d3d.initialized || !s_d3d.cmd_list || !s_wp_mv_active) return;
    wait_for_gpu();
    if (wp77_on()) wp77_frame_begin();   /* winport v77-fif: latch the SRV window of this slot */
    s_wp_blit_srv_next = 0;
    u32 fi = s_d3d.frame_index;
    s_d3d.cmd_allocators[fi]->lpVtbl->Reset(s_d3d.cmd_allocators[fi]);
    s_d3d.cmd_list->lpVtbl->Reset(s_d3d.cmd_list, s_d3d.cmd_allocators[fi], NULL);
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = s_d3d.render_targets[fi];
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    s_d3d.rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.rtv_heap, &rtv);
    rtv.ptr += fi * s_d3d.rtv_descriptor_size;
    s_wp72_pop_ok = 1;   /* winport v72-moviepace: movie-only present */
    winport_movie_overlay(fi, rtv);
    s_wp72_pop_ok = 0;
    wp47_ui_overlay(rtv);   /* winport v47-cutscene-ui */
    wp69_fps_overlay(rtv);   /* winport v69-fps-f11 */
    wp80_panel(rtv);   /* winport v80-port-settings */
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    s_d3d.cmd_list->lpVtbl->Close(s_d3d.cmd_list);
    ID3D12CommandList* cls[] = { (ID3D12CommandList*)s_d3d.cmd_list };
    s_d3d.cmd_queue->lpVtbl->ExecuteCommandLists(s_d3d.cmd_queue, 1, cls);
    wp77_after_submit();   /* winport v77-fif */
    wp82_present(1, 0, 3);   /* winport v82c-fence-diag */
    move_to_next_frame();
    QueryPerformanceCounter(&s_wp73_lp);   /* winport v73-moviesolo */
    { static int n_ = 0; if (n_++ < 3) printf("[WPMOVIE] presented a movie frame on its own (game not flipping)\n"); }
}

static unsigned s_winport_renders = 0;   /* winport-fix: no empty re-render on flip (blink) */
/* winport v32-present-sync: WINPORT_PRESENT_SYNC=0 -> Present without the vsync wait (fix_v32_present_sync.py) */
extern int wp51_ff_active(void);   /* winport v51-rt-cutscene (cellPad.c): 1 while a real-time cutscene is fast-forwarded */
static UINT wp32_present_sync(void)
{
    static int v = -1;
    if (v < 0) { const char* e = getenv("WINPORT_PRESENT_SYNC"); v = (e && e[0] == '0') ? 0 : 1;
                 if (!v) fprintf(stderr, "[PRESENT] WINPORT_PRESENT_SYNC=0: no vsync wait in render_frame%c", 10); }
    return (UINT)(v && !wp51_ff_active());   /* winport v51-rt-cutscene: no vsync wait while skipping */
}
/* winport v40: orbs -- framebuffer feedback loops (fix_v40_orbs_sky.py, research/G2_orbs_sky.md).
 * GoW2's orb/fire refraction sprites (FP 0x75102F33) and the alpha-marker quad sample the colour target they
 * are drawing into (t0 == rt 0x01E60000). Binding the RT resource itself as the SRV while it is bound as RTV
 * (state RENDER_TARGET) is undefined in D3D12 and renders as 16x16 tile blocks -> orb checkerboard.
 * WINPORT_ORB_FIX=1: copy the target into a per-slot texture right before such a draw and sample the copy
 * (PS3 semantics: the texture unit sees the surface as it was before the draw). =2: skip those draws
 * (diagnostic). Unset/0: old behaviour. */
static int wp40_orb_mode(void)
{
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("WINPORT_ORB_FIX");
        v = e ? atoi(e) : 0;
        if (v) fprintf(stderr, "[WP40] WINPORT_ORB_FIX=%d (%s)%c", v,
                       v == 1 ? "copy self-sampled render targets before the draw" : "SKIP self-sampling draws (diagnostic)", 10);
    }
    return v;
}
static unsigned char   s_wp40_fbl[MAX_DRAWS];     /* per draw: 1 + off_rt slot it samples while drawing into it */
static ID3D12Resource* s_wp40_fbc[MAX_OFF_RTS];   /* per off_rt slot: copy texture (PIXEL_SHADER_RESOURCE) */
static u32             s_wp40_fbc_w[MAX_OFF_RTS], s_wp40_fbc_h[MAX_OFF_RTS], s_wp40_fbc_f[MAX_OFF_RTS];
static u32             s_wp40_ncopy;
static ID3D12Resource* wp40_fbc_get(int slot)
{
    if (slot < 0 || slot >= MAX_OFF_RTS || !s_d3d.device) return NULL;
    OffRT* r = &s_d3d.off_rt[slot];
    if (!r->res) return NULL;
    if (s_wp40_fbc[slot] && s_wp40_fbc_w[slot] == r->w && s_wp40_fbc_h[slot] == r->h && s_wp40_fbc_f[slot] == r->dxgi)
        return s_wp40_fbc[slot];
    /* Size/format changed (rare): the old copy may still be referenced by a command list in flight, so it is
     * not released here (bounded leak, one texture per change). */
    D3D12_HEAP_PROPERTIES hp = {0};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = {0};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = wp68_px(r->w); td.Height = wp68_px(r->h); td.DepthOrArraySize = 1; td.MipLevels = 1;   /* winport v68 */
    td.Format = (DXGI_FORMAT)r->dxgi; td.SampleDesc.Count = 1;
    td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    ID3D12Resource* c = NULL;
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL, &IID_ID3D12Resource, (void**)&c)) || !c) {
        static int _e = 0; if (_e++ < 4) fprintf(stderr, "[WP40] feedback copy texture FAILED for slot %d (%ux%u)%c", slot, r->w, r->h, 10);
        return NULL;
    }
    s_wp40_fbc[slot] = c; s_wp40_fbc_w[slot] = r->w; s_wp40_fbc_h[slot] = r->h; s_wp40_fbc_f[slot] = r->dxgi;
    fprintf(stderr, "[WP40] feedback copy texture for RT slot %d off=0x%08X %ux%u dxgi=%u%c", slot, r->off, r->w, r->h, r->dxgi, 10);
    return c;
}
/* Called in the replay loop with the target already bound (state RENDER_TARGET). */
static void wp40_fbc_copy(int slot)
{
    if (slot < 0 || slot >= MAX_OFF_RTS) return;
    ID3D12Resource* c = s_wp40_fbc[slot];
    OffRT* r = &s_d3d.off_rt[slot];
    if (!c || !r->res || s_wp40_fbc_w[slot] != r->w || s_wp40_fbc_h[slot] != r->h) return;
    const D3D12_RESOURCE_STATES prev = r->st;
    off_rt_transition(slot, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = c;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    s_d3d.cmd_list->lpVtbl->CopyResource(s_d3d.cmd_list, c, r->res);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    off_rt_transition(slot, prev);
    if (s_wp40_ncopy++ < 8)
        fprintf(stderr, "[WP40] feedback copy #%u: RT slot %d off=0x%08X copied before a self-sampling draw%c",
                s_wp40_ncopy, slot, r->off, 10);
}

static void wp_vc2_recheck(void);   /* winport v42 */
static int s64_n;   /* winport v64-defer-vtx (defined with the vertex pool below) */
static void wp64_vtx_join(void); static void wp64_report(void);
static void render_frame_wk85(void);
static void render_frame(void) { const unsigned long long t_ = WK85_T0(); render_frame_wk85(); WK85_ADD(WK85_rf, t_); wk85_report(); }   /* winport v85a-walkprof */
static void render_frame_wk85(void)
{
    { extern void wp59_d3d_check(const char*); wp59_d3d_check("render_frame"); }   /* winport v59-async-rsx */
    if (s64_n > 0) { wp64_vtx_join(); wp64_report(); }   /* winport v64-defer-vtx: every deferred vertex is in the upload heap */
    s_winport_renders++;
    wp_vc2_recheck();   /* winport v42 */
    s_wp_last_render_ms = GetTickCount64();   /* winport-fix: movie present without guest flips */
    QueryPerformanceCounter(&s_wp73_lp);   /* winport v73-moviesolo */
    { static int fd = -1; static unsigned nfr = 0; static DWORD t_last = 0;   /* winport-fix: WINPORT_FRAMEDBG */
      if (fd < 0) { const char* e = getenv("WINPORT_FRAMEDBG"); fd = e ? atoi(e) : 0; }
      nfr++;
      if (fd && (nfr <= (unsigned)fd || nfr % 50 == 0)) {
          DWORD now = GetTickCount();
          u32 disp = 0, clr = 0, blit = 0, off = 0, offs[12], cnt[12]; int nd = 0;
          for (u32 i = 0; i < s_d3d.draw_count && i < MAX_DRAWS; i++) {
              const D3D12DrawRecord* r = &s_d3d.draws[i];
              if (!r->is_vp) continue;
              if (r->is_clear) { clr++; continue; }
              if (r->is_blit) blit++;
              if (r->rt_off == 0) { disp++; continue; }
              off++;
              int f = -1;
              for (int j = 0; j < nd; j++) if (offs[j] == r->rt_off) f = j;
              if (f >= 0) cnt[f]++; else if (nd < 12) { offs[nd] = r->rt_off; cnt[nd++] = 1; }
          }
          char tb[512]; int tl = 0; tb[0] = 0;
          for (int j = 0; j < nd && tl < (int)sizeof tb - 32; j++)
              tl += snprintf(tb + tl, sizeof tb - tl, " %X:%u", offs[j], cnt[j]);
          printf("[FRAME] #%u dt=%lums recs=%u disp=%u off=%u clr=%u blit=%u | +vs %u +fp %u (lookups %u)"
                 " +rt_recreate %u +rt_evict %u | blits exec %u skip %u | bb=%u parity=%u | targets%s\n",
                 nfr, t_last ? (unsigned long)(now - t_last) : 0ul, s_d3d.draw_count, disp, off, clr, blit,
                 s_wp_vs_compiles, s_wp_fp_compiles, s_wp_fp_lookups, s_wp_rt_recreates, s_wp_rt_evicts,
                 s_wp_blits_exec, s_wp_blits_skip, s_d3d.frame_index, s_d3d.vp_parity, tb);
          s_wp_vs_compiles = s_wp_fp_compiles = s_wp_fp_lookups = 0;
          s_wp_rt_recreates = s_wp_rt_evicts = 0; s_wp_blits_exec = s_wp_blits_skip = 0;
          t_last = now;
      } }
    double _rf0 = perf_on() ? perf_now() : 0.0;
    u32 fi = s_d3d.frame_index;

    /* Drain the GPU before touching shared upload resources (vp_vb vertices,
     * vp_cb constants, per-frame texture staging): the previous frame's draws
     * may still be reading them, and overwriting mid-flight tears geometry
     * (gcm/cube: triangles mixing stale and new vertices -> missing/sliver
     * polygons). These workloads are a few draws/frame, so full serialisation
     * costs little. */
    if (wp77_on()) {   /* winport v77-fif: no full drain; wait only for this slot's last user */
        wp77_frame_begin();
        if (s_ts_pend && (!s77_fence || s77_fence->lpVtbl->GetCompletedValue(s77_fence) < s77_val)) { s_ts_pend = 0; s77_ts_drop++; }
        wp_ts_collect(wp77_take_wait());
        wp77_report();
    } else
    { double w0_ = perf_now(); wait_for_gpu(); wp_ts_collect(perf_now() - w0_); }   /* winport-fix: GPU pass timing */
    wp_fp_frame_start();   /* winport-fix: NV3089 blits on the GPU */
    s_wp_blit_srv_next = 0;

    /* Compile the real vertex program once its microcode is captured, and keep
     * the constant bank uploaded for the VS. */
    /* The base VP pipeline gates the ENTIRE vertex-program draw pass below
     * (`if (s_d3d.vp_ready ...)`). The old trigger also required the microcode
     * size to differ from the last compile, so a title whose first captured
     * program has the same byte count as vp_compiled_bytes never built the base
     * pipeline -- and then every is_vp draw record was silently dropped: draws
     * recorded, constants uploaded, per-draw VS compiled and cached, and not one
     * of them submitted. While !vp_ready there is nothing to be stale against,
     * so only the "have microcode" test belongs here. */
    if (s_d3d.current_rsx_state && !s_d3d.vp_ready &&
        (s_d3d.current_rsx_state->transform_program_words * 4u) >= 16)
        compile_vp();
    /* Per-draw VP constants are snapshotted at record time (vp_record_cb). */

    /* Lazily create the readback buffer the first time a dump is requested. */
    if (s_d3d.dump_frames_left > 0 && !s_d3d.readback_buf) {
        s_d3d.readback_pitch = (WP68_BW * 4 + 255) & ~255u;   /* winport v68 */
        D3D12_HEAP_PROPERTIES hp = {0};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd = {0};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = (u64)s_d3d.readback_pitch * WP68_BH;
        rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void**)&s_d3d.readback_buf);
    }

    /* Reset command allocator and list */
    s_d3d.cmd_allocators[fi]->lpVtbl->Reset(s_d3d.cmd_allocators[fi]);
    s_d3d.cmd_list->lpVtbl->Reset(s_d3d.cmd_list, s_d3d.cmd_allocators[fi], NULL);
    wp_ts_init(); s_ts_n = 0; wp_ts_mark(0xFFFFFFFEu, 0, 0);   /* winport-fix: GPU pass timing */

    /* Upload the bound font atlas into an R8_UNORM texture and create its SRV.
     * The atlas is a linear 8-bit coverage map, so a straight row copy (no
     * deswizzle) suffices. It is a DYNAMIC glyph cache -- the game rasterizes
     * new glyphs into it over time -- so re-upload whenever it is dirty (set on
     * every bind), not just once. Uploading only on first bind left every glyph
     * added after frame 1 sampling stale texels -> torn text. The backend is
     * synchronous (wait_for_gpu each frame), so recreating the resource per
     * dirty frame is safe. */
    if (s_d3d.tex_bound && (!s_d3d.tex_ready || s_d3d.tex_dirty) && s_d3d.srv_heap && s_d3d.pipeline_state_tex) {
        extern uint8_t* vm_base;
        u32 w = s_d3d.tex_w, h = s_d3d.tex_h;
        u32 pitch = (w + 255) & ~255u;   /* D3D12 requires 256-byte row pitch */

        wp77_idle(5);   /* winport v77-fif: legacy atlas recreated while frames may be in flight */
        if (s_d3d.tex_resource) { s_d3d.tex_resource->lpVtbl->Release(s_d3d.tex_resource); s_d3d.tex_resource = NULL; }
        if (s_d3d.tex_upload)   { s_d3d.tex_upload->lpVtbl->Release(s_d3d.tex_upload);     s_d3d.tex_upload = NULL; }

        D3D12_HEAP_PROPERTIES hp_def = {0}; hp_def.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = {0};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = w; td.Height = h; td.DepthOrArraySize = 1; td.MipLevels = 1;
        td.Format = DXGI_FORMAT_R8_UNORM; td.SampleDesc.Count = 1;
        td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        HRESULT thr = s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &hp_def, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void**)&s_d3d.tex_resource);

        D3D12_HEAP_PROPERTIES hp_up = {0}; hp_up.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC ud = {0};
        ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ud.Width = (u64)pitch * h; ud.Height = 1; ud.DepthOrArraySize = 1; ud.MipLevels = 1;
        ud.SampleDesc.Count = 1; ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (SUCCEEDED(thr))
            thr = s_d3d.device->lpVtbl->CreateCommittedResource(
                s_d3d.device, &hp_up, D3D12_HEAP_FLAG_NONE, &ud,
                D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void**)&s_d3d.tex_upload);

        if (SUCCEEDED(thr) && vm_base) {
            void* mapped = NULL;
            D3D12_RANGE nr = {0, 0};
            if (SUCCEEDED(s_d3d.tex_upload->lpVtbl->Map(s_d3d.tex_upload, 0, &nr, &mapped)) && mapped) {
                const u8* srcbase = vm_base + s_d3d.tex_src_offset;
                for (u32 y = 0; y < h; y++)
                    memcpy((u8*)mapped + (u64)y * pitch, srcbase + (u64)y * w, w);
                s_d3d.tex_upload->lpVtbl->Unmap(s_d3d.tex_upload, 0, NULL);

                D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
                dst.pResource = s_d3d.tex_resource;
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dst.SubresourceIndex = 0;
                src.pResource = s_d3d.tex_upload;
                src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint.Offset = 0;
                src.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8_UNORM;
                src.PlacedFootprint.Footprint.Width    = w;
                src.PlacedFootprint.Footprint.Height   = h;
                src.PlacedFootprint.Footprint.Depth    = 1;
                src.PlacedFootprint.Footprint.RowPitch = pitch;
                s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);

                D3D12_RESOURCE_BARRIER tb = {0};
                tb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                tb.Transition.pResource   = s_d3d.tex_resource;
                tb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                tb.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                tb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &tb);

                D3D12_SHADER_RESOURCE_VIEW_DESC sv = {0};
                sv.Format = DXGI_FORMAT_R8_UNORM;
                sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                sv.Texture2D.MipLevels = 1;
                D3D12_CPU_DESCRIPTOR_HANDLE sh;
                s_d3d.srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &sh);
                s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, s_d3d.tex_resource, &sv, sh);

                s_d3d.tex_ready = 1;
                s_d3d.tex_dirty = 0;   /* content now in sync with guest atlas */
                { static int _au = 0; if (_au++ < 3)
                    printf("[D3D12] atlas uploaded (%ux%u R8) -> textured\n", w, h); }
            }
        }
    }

    /* Frames with no ops (init/boot presents) don't count against the cap --
     * PNG-decode-heavy titles burn hundreds of empty presents before the first
     * real draw. */
    if (getenv("RTT_DUMP") && s_d3d.draw_count > 0) { static int _f=0; int _cap = atoi(getenv("RTT_DUMP")); if (_cap < 2) _cap = 14; if (_f++ < _cap) {
        fprintf(stderr, "[RTT] frame %d: %u ops\n", _f, s_d3d.draw_count);
        for (u32 _d = 0; _d < s_d3d.draw_count && _d < MAX_DRAWS; _d++) {
            D3D12DrawRecord* r = &s_d3d.draws[_d];
            fprintf(stderr, "[RTT]  op%02u %s rt=0x%X mrt=0x%X/0x%X/0x%X t0=0x%X fp=0x%X n=%u cmask=%X vp=%u,%u %ux%u blend=%d\n",
                _d, r->is_clear?"CLR ":(r->is_vp?"draw":"leg "), r->rt_off,
                r->rt_mrt[0], r->rt_mrt[1], r->rt_mrt[2],
                r->tex[0].raw, r->fp_addr, r->vertex_count,
                r->cmask, r->vp_x, r->vp_y, r->vp_w, r->vp_h, r->blend);
        }
    } }

    /* Debug: RTT_PASS=N shows pass N's output directly on screen (drops later
     * ops, retargets pass N to the backbuffer). */
    { const char* rp = getenv("RTT_PASS");
      if (rp) {
        int keep = atoi(rp), seen = 0; u32 cut = s_d3d.draw_count;
        int viewrt = getenv("RTT_VIEWRT") != NULL;
        for (u32 _d = 0; _d < s_d3d.draw_count && _d < MAX_DRAWS; _d++) {
            D3D12DrawRecord* r = &s_d3d.draws[_d];
            if (r->is_clear || !r->is_vp) continue;
            if (seen == keep) {
                /* Alone: retarget op N to the backbuffer. With RTT_VIEWRT:
                 * keep op N intact and pull the display composite forward so
                 * the chosen RT is shown as of this point in the chain. */
                if (!viewrt) { r->rt_off = 0; r->rt_mrt[0] = r->rt_mrt[1] = r->rt_mrt[2] = 0; }
                cut = _d + 1;
                break;
            }
            seen++;
        }
        if (viewrt) {
            for (u32 _j = cut; _j < s_d3d.draw_count && _j < MAX_DRAWS; _j++) {
                D3D12DrawRecord* r = &s_d3d.draws[_j];
                if (r->is_vp && !r->is_clear && r->rt_off == 0) {
                    if (cut < MAX_DRAWS) s_d3d.draws[cut++] = *r;
                    break;
                }
            }
        }
        s_d3d.draw_count = (cut < s_d3d.draw_count) ? cut : s_d3d.draw_count;
      } }

    /* DRAW_LIMIT=<N>: keep only the first N recorded ops this frame. The duck's
     * draws are ops 00..~03 of every frame, so a small limit shows the ducks
     * alone -- separating "the ducks never rasterize" from "later geometry is
     * painted over them". Diagnostic only. */
    { static int lim = -1;
      if (lim < 0) { const char* e = getenv("DRAW_LIMIT"); lim = e ? atoi(e) : 0; }
      if (lim > 0 && s_d3d.draw_count > (u32)lim) s_d3d.draw_count = (u32)lim; }

    /* DRAW_SKIP_TEX=<hex raw offset>: drop every op binding that texture on
     * unit 0. The ducks sit behind the water's slab geometry, so removing the
     * occluder is the way to see whether the duck itself renders. Diagnostic. */
    { static const char* sk = (const char*)1; static u32 want = 0;
      if (sk == (const char*)1) sk = getenv("DRAW_SKIP_TEX");
      want = draw_filter_tex(sk);
      if (want) {
        u32 keep = 0;
        for (u32 _d = 0; _d < s_d3d.draw_count && _d < MAX_DRAWS; _d++) {
            if (!s_d3d.draws[_d].is_clear && s_d3d.draws[_d].tex[0].raw == want) continue;
            if (keep != _d) s_d3d.draws[keep] = s_d3d.draws[_d];
            keep++;
        }
        s_d3d.draw_count = keep;
      } }

    /* DRAW_KEEP_TEX=<hex raw offset>: the inverse -- keep only the ops binding
     * that texture (plus clears). Isolates one object from everything drawn over
     * it, which is what finally shows the duck on its own. Diagnostic. */
    { static const char* kp = (const char*)1; static u32 want = 0;
      if (kp == (const char*)1) kp = getenv("DRAW_KEEP_TEX");
      want = draw_filter_tex(kp);
      /* DRAW_KEEP_NOCLEAR=1: also drop the clears. Keeping them preserves their
       * ORIGINAL position in the batch, so a clear that originally ran after the
       * kept draws still runs after them -- and wipes the very geometry being
       * isolated. That makes an object that renders fine look like it renders
       * nothing. */
      static int noclr = -1;
      if (noclr < 0) { const char* e = getenv("DRAW_KEEP_NOCLEAR"); noclr = e ? atoi(e) : 0; }
      /* DRAW_KEEP_BLEND=0|1: further restrict DRAW_KEEP_TEX to draws with that
       * blend flag. A mesh drawn twice -- once opaque, once blended -- is two
       * different passes (body vs reflection shell) that a texture filter alone
       * cannot separate. */
      static int kblend = -2;
      if (kblend == -2) { const char* e = getenv("DRAW_KEEP_BLEND");
                          kblend = e ? atoi(e) : -1; }
      if (want) {
        /* Emit the CLEARS FIRST, then the kept draws. Preserving the original
         * order means a clear that ran after them still runs after them and
         * wipes the isolated geometry; dropping the clears entirely (noclr)
         * leaves stale DEPTH that rejects most of its fragments. Neither shows
         * the object properly -- clear, then draw, does. */
        static D3D12DrawRecord kept[MAX_DRAWS];
        u32 nk = 0;
        if (!noclr)
            for (u32 _d = 0; _d < s_d3d.draw_count && _d < MAX_DRAWS; _d++)
                if (s_d3d.draws[_d].is_clear && nk < MAX_DRAWS) kept[nk++] = s_d3d.draws[_d];
        for (u32 _d = 0; _d < s_d3d.draw_count && _d < MAX_DRAWS; _d++)
            if (!s_d3d.draws[_d].is_clear && s_d3d.draws[_d].tex[0].raw == want
                && (kblend < 0 || s_d3d.draws[_d].blend == kblend)
                && nk < MAX_DRAWS)
                kept[nk++] = s_d3d.draws[_d];
        for (u32 _k = 0; _k < nk; _k++) s_d3d.draws[_k] = kept[_k];
        s_d3d.draw_count = nk;
      } }

    /* DRAW_LAST_TEX=<hex raw offset>: move the ops binding that texture to the
     * END of the batch so nothing is drawn over them. The duck's draws are ops
     * 00..03 of every frame and the tub wall is drawn afterwards across the same
     * screen area, so anything that defeats the depth sort hides the duck
     * completely. Reordering separates "occluded" from "not rendered". */
    { static const char* lt = (const char*)1; static u32 want = 0;
      if (lt == (const char*)1) lt = getenv("DRAW_LAST_TEX");
      want = draw_filter_tex(lt);
      if (want && s_d3d.draw_count > 1) {
        static D3D12DrawRecord moved[MAX_DRAWS];
        u32 nm = 0, keep = 0;
        for (u32 _d = 0; _d < s_d3d.draw_count && _d < MAX_DRAWS; _d++) {
            if (!s_d3d.draws[_d].is_clear && s_d3d.draws[_d].tex[0].raw == want) {
                if (nm < MAX_DRAWS) moved[nm++] = s_d3d.draws[_d];
                continue;
            }
            if (keep != _d) s_d3d.draws[keep] = s_d3d.draws[_d];
            keep++;
        }
        for (u32 _m = 0; _m < nm && keep < MAX_DRAWS; _m++)
            s_d3d.draws[keep++] = moved[_m];
        s_d3d.draw_count = keep;
      } }

    /* Render-to-texture pre-pass: make sure an offscreen RT resource exists for
     * every non-display surface targeted this frame (so draws binding it as a
     * texture can resolve to it below, whatever the op order). */
    for (int _i = 0; _i < MAX_OFF_RTS; _i++) s_d3d.off_rt[_i].used = 0;
    for (u32 _d = 0; _d < s_d3d.draw_count && _d < MAX_DRAWS; _d++) {
        D3D12DrawRecord* dr = &s_d3d.draws[_d];
        if (dr->is_vp && dr->is_blit) continue;   /* winport-fix: NV3089 blits on the GPU */
        if (dr->is_vp && dr->rt_off)
            off_rt_get(dr->rt_off, dr->rt_w, dr->rt_h, dr->rt_fmt);
        for (int _m = 0; _m < 3; _m++)
            if (dr->is_vp && dr->rt_mrt[_m])
                off_rt_get(dr->rt_mrt[_m], dr->rt_w, dr->rt_h, dr->rt_fmt);
    }

    /* Per-frame VP textures + guest-FP pipelines: for each VP draw, upload the
     * texture it had bound at submit time into a slot (SRV heap 1+slot; plasma
     * animates so contents re-upload every frame) and pre-build its FP PSO.
     * A texture whose offset matches an offscreen RT samples the RT directly
     * (tex_slot 1000+idx) -- no guest-memory upload. */
    double _pre0 = perf_on() ? perf_now() : 0.0;
    for (int _i = 0; _i < VP_TEX_SLOTS; _i++) s_d3d.vp_tex[_i].used = 0; s85_tex_gen++;   /* winport v85c-walkfast */
    s_wp_tx_frame++;   /* winport-fix: texture slot LRU */
    wp_zs_scan();   /* winport-fix: v22 scene depth */
    for (u32 _d = 0; _d < s_d3d.draw_count && _d < MAX_DRAWS; _d++) {
        D3D12DrawRecord* dr = &s_d3d.draws[_d];
        s_wp40_fbl[_d] = 0;   /* winport v40: orbs -- per-draw feedback flag, rebuilt every frame */
        if (!dr->is_vp || dr->is_clear || dr->is_blit) continue;   /* winport-fix: NV3089 blits on the GPU */
        /* Debug: RTT_VIEWRT=<hex raw offset> makes display draws sample that
         * offscreen RT at t0 (the composite blit then shows it fullscreen). */
        { static const char* vr = (const char*)1;     /* hoisted: this runs per DRAW */
          if (vr == (const char*)1) vr = getenv("RTT_VIEWRT");
          if (vr && dr->rt_off == 0) {
              dr->tex[0].raw = (u32)strtoul(vr, NULL, 16);
              dr->tex[0].off = 0;
              dr->tex[0].set = 1;
          } }
        /* SCREENTEX=1: report draws that sample a DISPLAY-SIZED texture. On RSX
         * a title renders a reflection/refraction into a surface and then
         * samples it; our backend renders into a D3D backbuffer, so the guest
         * memory that sampler reads is never written and the texture comes back
         * empty. Finding which geometry does it is the first step to feeding it
         * the rendered image instead. */
        /* EMPTYTEX=1: which geometry binds a texture whose source is all zero.
         * An unresolved texture renders untextured, so this names the surfaces
         * affected instead of leaving it to guesswork. */
        { static int ed = -1;
          if (ed < 0) { const char* e = getenv("EMPTYTEX"); ed = e ? atoi(e) : 0; }
          extern uint8_t* vm_base;
          if (ed && vm_base) for (int _u = 0; _u < 4; _u++) {
              if (!dr->tex[_u].set || !dr->tex[_u].off) continue;
              u32 nz = 0, sz = dr->tex[_u].w * dr->tex[_u].h * 4u;
              for (u32 i2 = 0; i2 < sz && i2 < 0x20000u; i2 += 997)
                  if (vm_base[dr->tex[_u].off + i2]) { nz = 1; break; }
              if (!nz) {
                  static u32 seen[24]; static int ns = 0; int known = 0;
                  for (int k = 0; k < ns; k++) if (seen[k] == dr->fp_addr) known = 1;
                  if (!known && ns < 24) { seen[ns++] = dr->fp_addr;
                      /* Scan a window around the resolved address: if the pass
                       * that fills this surface ran but landed elsewhere, the
                       * data is nearby; if the whole window is zero it was never
                       * produced. */
                      /* Same offset through BOTH context DMAs: a texture the
                       * guest put in main memory but that we resolve as LOCAL
                       * reads as untouched VRAM, i.e. all zeros. */
                      { extern u32 cellGcmResolveLocated(int, u32);
                        u32 la = cellGcmResolveLocated(1, dr->tex[_u].raw);
                        u32 ma = cellGcmResolveLocated(0, dr->tex[_u].raw);
                        u32 ln = 0, mn = 0;
                        for (u32 i4 = 0; i4 < 0x20000u; i4 += 997) {
                            if (vm_base[la + i4]) ln++;
                            if (vm_base[ma + i4]) mn++;
                        }
                        fprintf(stderr, "[TEXLOC] raw=0x%08X LOCAL@0x%08X nz=%u  MAIN@0x%08X nz=%u%c",
                                dr->tex[_u].raw, la, ln, ma, mn, 10); }
                      { u32 base = dr->tex[_u].off & ~0xFFFFFu;
                        for (u32 w = 0; w < 0x400000u; w += 0x100000u) {
                            u32 nzc = 0;
                            for (u32 i3 = 0; i3 < 0x100000u; i3 += 1021)
                                if (vm_base[base + w + i3]) nzc++;
                            fprintf(stderr, "[EMPTYSCAN] 0x%08X..+1MB nonzero %u/1027%c",
                                    base + w, nzc, 10);
                        } }
                      fprintf(stderr, "[EMPTYTEX] fp=0x%X unit=%d raw=0x%08X res=0x%08X %ux%u"
                                      " fmt=0x%02X verts=%u vp=%u,%u %ux%u%c",
                              dr->fp_addr, _u, dr->tex[_u].raw, dr->tex[_u].off, dr->tex[_u].w,
                              dr->tex[_u].h, dr->tex[_u].fmt, dr->vertex_count,
                              dr->vp_x, dr->vp_y, dr->vp_w, dr->vp_h, 10); }
              }
          } }
        { static int sd = -1;
          if (sd < 0) { const char* e = getenv("SCREENTEX"); sd = e ? atoi(e) : 0; }
          if (sd) for (int _u = 0; _u < 4; _u++)
              if (dr->tex[_u].set && dr->tex[_u].w == s_d3d.width &&
                  dr->tex[_u].h == s_d3d.height) {
                  static u32 seen[16]; static int ns = 0; int known = 0;
                  for (int k = 0; k < ns; k++) if (seen[k] == dr->fp_addr) known = 1;
                  if (!known && ns < 16) { seen[ns++] = dr->fp_addr;
                      fprintf(stderr, "[SCREENTEX] unit=%d raw=0x%08X %ux%u fmt=0x%02X"
                                      " fp=0x%X verts=%u vp=%u,%u %ux%u%c",
                              _u, dr->tex[_u].raw, dr->tex[_u].w, dr->tex[_u].h,
                              dr->tex[_u].fmt, dr->fp_addr, dr->vertex_count,
                              dr->vp_x, dr->vp_y, dr->vp_w, dr->vp_h, 10); }
              } }
        /* Fill this draw's t0-t3 SRV window (DRAW_SRV_BASE + d*4): each unit
         * resolves to an offscreen RT (sampled directly), an uploaded guest
         * texture, or a null SRV. */
        double _sv0 = perf_on() ? perf_now() : 0.0;
        u32 zm_ = 0;   /* winport-fix: v22 scene depth */
        for (int _u = 0; _u < 4; _u++) {
            u32 wslot = DRAW_SRV_BASE + _d * 4 + (u32)_u;
            if (_u == 0 && dr->wp94_tgt && !dr->is_clear) s_wp94_frame_tgt = dr->wp94_tgt;   /* winport v94a-ffhaze */
            dr->tex_rt[_u] = -1;
            if (s_wp_zneed_n && dr->tex[_u].set && (dr->tex[_u].fmt & 0x9F) == 0x85 && s_d3d.vp_fpcb_mapped) {   /* winport-fix: v22 scene depth */
                int zi_ = wp_zneed_find(dr->tex[_u].raw);
                WpZSnap* zs_ = (zi_ >= 0) ? wp_zs_get(s_wp_zneed[zi_], s_wp_zneed_w[zi_], s_wp_zneed_h[zi_]) : NULL;
                if (zs_ && zs_->res) {
                    srv_write(wslot, zs_->res, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
                    zm_ |= 1u << _u; s_wp_zs_bound++;
                    float* kz_ = (float*)((char*)s_d3d.vp_fpcb_mapped + ((u64)s_d3d.vp_parity * MAX_DRAWS + dr->cb_slot) * VP_FPCB_STRIDE);
                    kz_[(dr->fp_kn + 1) * 4 + (_u >> 1) * 4 + (_u & 1) * 2 + 0] = (float)wp68_px(zs_->w);   /* winport v68: wp_zld Load() texel dims */
                    kz_[(dr->fp_kn + 1) * 4 + (_u >> 1) * 4 + (_u & 1) * 2 + 1] = (float)wp68_px(zs_->h);
                    s_wp68_n_zs++;
                    continue;
                }
            }
            if (dr->tex[_u].set && wp94_prev_mode() &&   /* winport v94a-ffhaze */
                dr->tex[_u].w == s_d3d.width && dr->tex[_u].h == s_d3d.height) {
                ID3D12Resource* r94 = wp94_pick(dr, _u);
                if (r94) { srv_write(wslot, r94, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING); continue; }
            }
            if (dr->tex[_u].set && s_wp92_prev && s_wp92_prev_ok && wp92_prev_on() &&   /* winport v92b-prevframe */
                dr->tex[_u].w == s_d3d.width && dr->tex[_u].h == s_d3d.height && wp92_is_dispbuf(dr->tex[_u].off)) {
                { static int _n = 0; if (_n++ < 4)
                    fprintf(stderr, "[PREV92] unit %d of fp=0x%X samples display buffer 0x%08X -> last finished frame%c",
                            _u, dr->fp_addr, dr->tex[_u].off, 10); }
                srv_write(wslot, s_wp92_prev, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
                continue;
            }
            /* Display-sized sampler source -> the rendered frame. */
            if (dr->tex[_u].set && s_screen_copy &&
                !(wp92_dispsize_skip() && off_rt_find(dr->tex[_u].raw) >= 0) &&   /* winport v92b-dispsize2: our RT, not the screen copy */
                dr->tex[_u].w == s_d3d.width && dr->tex[_u].h == s_d3d.height &&
                wp50_screen_sub_ok(dr->tex[_u].fmt)) {   /* winport v50-boot-screens */
                static int en = -1;
                if (en < 0) { const char* e = getenv("SCREEN_AS_TEX"); en = e ? atoi(e) : 1; }
                if (en) {
                    { static int _n = 0; if (_n++ < 3)
                        fprintf(stderr, "[SCREENTEX] bound frame copy at unit %d for"
                                        " fp=0x%X (%ux%u)%c", _u, dr->fp_addr,
                                dr->tex[_u].w, dr->tex[_u].h, 10); }
                    srv_write(wslot, s_screen_copy, DXGI_FORMAT_R8G8B8A8_UNORM,
                              D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
                    continue;
                }
            }
            /* A sampler whose guest buffer was never written, at the size of a
             * reduced-viewport pass we captured: that pass IS its producer. */
            if (dr->tex[_u].set && dr->tex[_u].off && s_subvp_n) {
                int hit = -1;
                for (int i = 0; i < s_subvp_n; i++)
                    if (s_subvp[i].res && s_subvp[i].w == dr->tex[_u].w &&
                        s_subvp[i].h == dr->tex[_u].h) { hit = i; break; }
                if (hit >= 0) {
                    extern uint8_t* vm_base;
                    u32 nz = 0, sz = dr->tex[_u].w * dr->tex[_u].h * 4u;
                    for (u32 i5 = 0; i5 < sz && i5 < 0x20000u; i5 += 997)
                        if (vm_base[dr->tex[_u].off + i5]) { nz = 1; break; }
                    if (!nz) {
                        { static int _n = 0; if (getenv("SUBVP_DBG") && _n++ < 8)
                            fprintf(stderr, "[SUBVP] unit %d fp=0x%X <- captured %ux%u%c",
                                    _u, dr->fp_addr, dr->tex[_u].w, dr->tex[_u].h, 10); }
                        srv_write(wslot, s_subvp[hit].res, DXGI_FORMAT_R8G8B8A8_UNORM,
                                  D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
                        continue;
                    }
                }
            }
            if (dr->tex[_u].set) {
                int rt = off_rt_find(dr->tex[_u].raw);
                if (rt >= 0) {
                    dr->tex_rt[_u] = rt;
                    /* winport v40: orbs -- this unit samples the colour target the draw renders into
                     * (feedback loop, e.g. refraction FP 0x75102F33 on 0x01E60000): sample a copy instead. */
                    if (wp40_orb_mode() && dr->rt_off &&
                        (rt == off_rt_find(dr->rt_off) || rt == off_rt_find(dr->rt_mrt[0]) ||
                         rt == off_rt_find(dr->rt_mrt[1]) || rt == off_rt_find(dr->rt_mrt[2]))) {
                        s_wp40_fbl[_d] = (unsigned char)(rt + 1);
                        if (wp40_orb_mode() == 1) {
                            ID3D12Resource* c_ = wp40_fbc_get(rt);
                            if (c_) {
                                dr->tex_rt[_u] = -1;   /* the copy is always in PIXEL_SHADER_RESOURCE */
                                srv_write(wslot, c_, (DXGI_FORMAT)s_d3d.off_rt[rt].dxgi,
                                          D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
                                continue;
                            }
                            s_wp40_fbl[_d] = 0;   /* no copy texture: fall back to the old binding */
                        }
                    }
                    srv_write(wslot, s_d3d.off_rt[rt].res, (DXGI_FORMAT)s_d3d.off_rt[rt].dxgi,
                              D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
                    continue;
                }
                u32 _bf = dr->tex[_u].fmt & 0x9F;
                if (dr->tex[_u].off &&
                    (_bf == 0x81 || _bf == 0x85 || _bf == 0x8B ||
                     (_bf >= 0x86 && _bf <= 0x88) || _bf == 0x9E || _bf == 0x82 || _bf == 0x83 || _bf == 0x84 || _bf == 0x97 || _bf == 0x9D /* winport-fix: RGBA8-class texture formats */)) {
                    double _tt = perf_on() ? perf_now() : 0.0;
                    int _cube = dr->tex[_u].cube && (dr_cube_mask(dr) & (1u << _u));
                    int ts = vp_upload_tex_slot(dr->tex[_u].off, dr->tex[_u].w,
                                                dr->tex[_u].h, dr->tex[_u].fmt,
                                                _cube, dr->tex[_u].mips);
                    if (perf_on()) { s_perf_tex += perf_now() - _tt; s_perf_ntex++;
                        s_perf_texbytes += (u64)dr->tex[_u].w * dr->tex[_u].h * 4u; }
                    /* Both forms of the offset are in hand here; the filters
                     * compare the BOUND one. */
                    if (s_duck_off && dr->tex[_u].off == s_duck_off)
                        s_duck_raw = dr->tex[_u].raw;
                    if (ts >= 0) {
                        DXGI_FORMAT sf =
                            (_bf == 0x85 || _bf == 0x9E || _bf == 0x82 || _bf == 0x83 || _bf == 0x84 || _bf == 0x97 || _bf == 0x9D) ? DXGI_FORMAT_R8G8B8A8_UNORM :
                            (_bf == 0x8B) ? DXGI_FORMAT_R8G8_UNORM :
                            (_bf == 0x86) ? DXGI_FORMAT_BC1_UNORM :
                            (_bf == 0x87) ? DXGI_FORMAT_BC2_UNORM :
                            (_bf == 0x88) ? DXGI_FORMAT_BC3_UNORM :
                                            DXGI_FORMAT_R8_UNORM;
                        srv_write_ex(wslot, s_d3d.vp_tex[ts].res, sf,
                                  (_bf == 0x81) ? 0x1000
                                                : rsx_remap_to_d3d(dr->tex[_u].ctrl1, _bf),
                                  _cube);
                        continue;
                    }
                }
            }
            /* TEX_BINDDBG=<N>: a unit that ends up with a NULL SRV. The draw is
             * still recorded, lit and transformed -- it just samples nothing,
             * which renders as a solid black object. */
            { static int cap = -1, n = 0;
              if (cap < 0) { const char* e = getenv("TEX_BINDDBG"); cap = e ? atoi(e) : 0; }
              if (cap && n < cap && dr->tex[_u].set) { n++;
                fprintf(stderr, "[TEXBIND] null SRV unit=%d raw=0x%X off=0x%X %ux%u fmt=0x%02X%c",
                        _u, dr->tex[_u].raw, dr->tex[_u].off, dr->tex[_u].w,
                        dr->tex[_u].h, dr->tex[_u].fmt, 10); } }
            srv_write(wslot, NULL, DXGI_FORMAT_R8G8B8A8_UNORM,
                      D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
        }
        dr->fp_exp32 = (dr->fp_exp32 & ~0xF0000) | (int)(zm_ << 16);   /* winport-fix: v22 scene depth */
        if (perf_on()) s_perf_srv += perf_now() - _sv0;
        double _ps0 = perf_on() ? perf_now() : 0.0;
        if (dr->fp_addr) vp_get_fp_pso((s_wp_uh = dr->fp_uhash, s_wp_alpha = WP_ALPHA_KEY(dr), WP_STK_SET(dr), s_wp_topo = WP_TOPO_CLASS(dr), dr->vs_idx), dr->fp_addr, dr->blend_key,
                                       dr_num_rts(dr),
                                       dr->rt_off ? rsx_surface_dxgi(dr->rt_fmt)
                                                  : DXGI_FORMAT_R8G8B8A8_UNORM,
                                       dr->fp_exp32, dr->cmask, dr->cull, dr_cube_mask(dr));
        if (perf_on()) s_perf_pso += perf_now() - _ps0;
    }

    /* Transition render target to RENDER_TARGET state */
    D3D12_RESOURCE_BARRIER barrier = {0};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = s_d3d.render_targets[fi];
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &barrier);

    /* Get RTV handle for current frame */
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle;
    s_d3d.rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.rtv_heap, &rtv_handle);
    rtv_handle.ptr += fi * s_d3d.rtv_descriptor_size;

    /* Get DSV handle (single depth buffer shared across frames) */
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle;
    s_d3d.dsv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.dsv_heap, &dsv_handle);

    if (perf_on()) s_perf_pre += perf_now() - _pre0;
    /* Set render target + depth */
    s_d3d.cmd_list->lpVtbl->OMSetRenderTargets(s_d3d.cmd_list, 1, &rtv_handle, FALSE, &dsv_handle);

    /* Clear color and depth */
    s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(
        s_d3d.cmd_list, rtv_handle, s_d3d.clear_color, 0, NULL);
    if (!s_wp_cont)   /* winport-fix: flush a full batch instead of dropping draws */
    s_d3d.cmd_list->lpVtbl->ClearDepthStencilView(
        s_d3d.cmd_list, dsv_handle,
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f, (UINT8)wp_stencil_base(), 0, NULL);   /* winport v41 */

    /* Set viewport and scissor */
    D3D12_VIEWPORT viewport = {0, 0, (float)WP68_BW, (float)WP68_BH, 0.0f, 1.0f};   /* winport v68 */
    D3D12_RECT scissor = {0, 0, (LONG)WP68_BW, (LONG)WP68_BH};
    s_d3d.cmd_list->lpVtbl->RSSetViewports(s_d3d.cmd_list, 1, &viewport);
    s_d3d.cmd_list->lpVtbl->RSSetScissorRects(s_d3d.cmd_list, 1, &scissor);

    /* Bind pipeline state and push MVP if anything to draw */
    if (s_d3d.pipeline_ready && s_d3d.draw_count > 0) {
        s_d3d.cmd_list->lpVtbl->SetGraphicsRootSignature(s_d3d.cmd_list, s_d3d.root_signature);
        s_d3d.cmd_list->lpVtbl->IASetVertexBuffers(s_d3d.cmd_list, 0, 1, &s_d3d.vb_view);

        /* Make the atlas SRV heap current and bind its table (t0) for textured
         * draws. Safe to set even if no draw is textured. */
        if (s_d3d.tex_ready && s_d3d.srv_heap) {
            ID3D12DescriptorHeap* heaps[] = { s_d3d.srv_heap };
            s_d3d.cmd_list->lpVtbl->SetDescriptorHeaps(s_d3d.cmd_list, 1, heaps);
            D3D12_GPU_DESCRIPTOR_HANDLE gh;
            s_d3d.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &gh);
            s_d3d.cmd_list->lpVtbl->SetGraphicsRootDescriptorTable(s_d3d.cmd_list, 1, gh);
        }

        /* Push the MVP matrix from RSX vertex constants slots 0..3.
         * If the game hasn't written any constants (e.g. placeholder data
         * already in clip space), fall back to identity. */
        float mvp[16];
        const rsx_state* st = s_d3d.current_rsx_state;
        int have_mvp = 0;
        if (st) {
            for (u32 r = 0; r < 4; r++) {
                for (u32 c = 0; c < 4; c++) {
                    float v = st->vertex_constants[r][c];
                    mvp[r * 4 + c] = v;
                    if (v != 0.0f) have_mvp = 1;
                }
            }
        }
        if (!have_mvp) {
            memset(mvp, 0, sizeof(mvp));
            mvp[0] = mvp[5] = mvp[10] = mvp[15] = 1.0f; /* identity */
        }
        s_d3d.cmd_list->lpVtbl->SetGraphicsRoot32BitConstants(
            s_d3d.cmd_list, 0 /*root param 0*/, 16, mvp, 0);

        /* D3D12_IQ=1: dump exactly what the GPU is about to see -- the MVP root
         * constants and the first uploaded host vertices. The legacy path draws
         * with no validation errors yet produces zero pixels, so the geometry
         * must be degenerate/off-screen post-VS. */
        { static int _vd = -1;
          if (_vd < 0) { const char* e = getenv("D3D12_IQ"); _vd = e ? 1 : 0; }
          static int _n = 0;
          if (_vd && _n < 3) {
            _n++;
            fprintf(stderr, "[D3D12-DBG] have_mvp=%d draw_count=%u vb_offset=%u\n",
                    have_mvp, s_d3d.draw_count, s_d3d.vb_offset);
            fprintf(stderr, "[D3D12-DBG] mvp rows: [%.2f %.2f %.2f %.2f][%.2f %.2f %.2f %.2f]"
                            "[%.2f %.2f %.2f %.2f][%.2f %.2f %.2f %.2f]\n",
                    mvp[0],mvp[1],mvp[2],mvp[3], mvp[4],mvp[5],mvp[6],mvp[7],
                    mvp[8],mvp[9],mvp[10],mvp[11], mvp[12],mvp[13],mvp[14],mvp[15]);
            if (s_d3d.vb_mapped) {
                /* host vertex = 9 floats (pos3 + col4 + uv2), VERTEX_STRIDE=36 */
                const float* bv = (const float*)s_d3d.vb_mapped;
                for (int k = 0; k < 3; k++) {
                    const float* v = bv + k * 9;
                    fprintf(stderr, "[D3D12-DBG]  hostvert[%d] pos=(%.3f,%.3f,%.3f) col=(%.2f,%.2f,%.2f,%.2f)\n",
                            k, v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
                }
            }
            for (u32 d = 0; d < s_d3d.draw_count && d < 3; d++) {
                const D3D12DrawRecord* dr = &s_d3d.draws[d];
                fprintf(stderr, "[D3D12-DBG]  draw[%u] is_vp=%d is_clear=%d topo=%u cnt=%u vbofs=%u startv=%u rt=%u\n",
                        d, dr->is_vp, dr->is_clear, dr->topology, dr->vertex_count,
                        dr->vb_byte_offset, dr->vb_byte_offset / VERTEX_STRIDE, dr->rt_off);
            }
            fflush(stderr);
          } }

        /* Replay each recorded draw with its own primitive topology and
         * the matching PSO class (triangle / line / point). The PSO class
         * must match the topology or D3D12 rejects the draw. */
        u32 last_topo = 0xFFFFFFFFu;
        ID3D12PipelineState* last_pso = NULL;
        u32 draws = s_d3d.draw_count;
        if (draws > MAX_DRAWS) draws = MAX_DRAWS;
        for (u32 d = 0; d < draws; d++) {
            const D3D12DrawRecord* dr = &s_d3d.draws[d];
            if (dr->is_vp) continue; /* drawn by the VP pass below */

            /* Select PSO: textured triangles (dbgfont) use the atlas PSO;
             * otherwise pick by topology class. */
            ID3D12PipelineState* target_pso = s_d3d.pipeline_state; /* default triangle */
            if (dr->textured && s_d3d.tex_ready && s_d3d.pipeline_state_tex) {
                target_pso = s_d3d.pipeline_state_tex;
            } else if (dr->topology == RSX_TOPOLOGY_POINTS) {
                target_pso = s_d3d.pipeline_state_points
                             ? s_d3d.pipeline_state_points : s_d3d.pipeline_state;
            } else if (dr->topology == RSX_TOPOLOGY_LINES ||
                       dr->topology == RSX_TOPOLOGY_LINE_STRIP) {
                target_pso = s_d3d.pipeline_state_lines
                             ? s_d3d.pipeline_state_lines : s_d3d.pipeline_state;
            }
            if (target_pso != last_pso) {
                s_d3d.cmd_list->lpVtbl->SetPipelineState(s_d3d.cmd_list, target_pso);
                last_pso = target_pso;
            }
            if (dr->topology != last_topo) {
                s_d3d.cmd_list->lpVtbl->IASetPrimitiveTopology(s_d3d.cmd_list, topo_to_d3d(dr->topology));
                last_topo = dr->topology;
            }
            u32 start_vert = dr->vb_byte_offset / VERTEX_STRIDE;
            s_d3d.cmd_list->lpVtbl->DrawInstanced(
                s_d3d.cmd_list, dr->vertex_count, 1, start_vert, 0);
        }
    }

    /* VP pass: real decompiled vertex program + atlas alpha-test PS. Feeds raw
     * float4 attrib0 from vp_vb and the vp_c[] constant bank. */
    if (s_d3d.vp_ready && s_d3d.draw_count > 0) {
        int any = 0;
        for (u32 d = 0; d < s_d3d.draw_count && d < MAX_DRAWS; d++)
            if (s_d3d.draws[d].is_vp) { any = 1; break; }
        /* Textured geometry (dbgfont atlas) uses the sampling PS; untextured 3D
         * (vkcube) uses the colour-only PS. Fall back to whichever exists. */
        ID3D12PipelineState* vpso =
            (s_d3d.tex_ready && s_d3d.pipeline_state_vp) ? s_d3d.pipeline_state_vp
                                                         : s_d3d.pipeline_state_vp_color;
        if (!vpso) vpso = s_d3d.pipeline_state_vp;
        { static int cap = -1, n = 0;
          if (cap < 0) { const char* e = getenv("VP_SUBMIT"); cap = e ? atoi(e) : 0; }
          if (cap && n < cap) { n++;
            u32 nvp = 0, nclr = 0;
            for (u32 d2 = 0; d2 < s_d3d.draw_count && d2 < MAX_DRAWS; d2++) {
                if (s_d3d.draws[d2].is_vp)    nvp++;
                if (s_d3d.draws[d2].is_clear) nclr++;
            }
            fprintf(stderr, "[VPPASS] records=%u is_vp=%u clears=%u any=%d vpso=%p rootsig=%p\n",
                    s_d3d.draw_count, nvp, nclr, any,
                    (void*)vpso, (void*)s_d3d.vp_root_sig); } }
        if (any && vpso) {
            s_d3d.cmd_list->lpVtbl->SetGraphicsRootSignature(s_d3d.cmd_list, s_d3d.vp_root_sig);
            s_d3d.cmd_list->lpVtbl->SetPipelineState(s_d3d.cmd_list, vpso);
            s_d3d.cmd_list->lpVtbl->IASetPrimitiveTopology(s_d3d.cmd_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            s_d3d.cmd_list->lpVtbl->SetGraphicsRootConstantBufferView(s_d3d.cmd_list, 0,
                s_d3d.vp_cb->lpVtbl->GetGPUVirtualAddress(s_d3d.vp_cb));
            wp_set_heaps(s_d3d.cmd_list);   /* winport-fix: guest sampler state */
            D3D12_GPU_DESCRIPTOR_HANDLE gh;
            s_d3d.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &gh);
            s_d3d.cmd_list->lpVtbl->SetGraphicsRootDescriptorTable(s_d3d.cmd_list, 1, gh);
            if (wp_samp_on()) s_d3d.cmd_list->lpVtbl->SetGraphicsRootDescriptorTable(s_d3d.cmd_list, 3, wp_samp_table_key(0));
            D3D12_VERTEX_BUFFER_VIEW vbv;
            vbv.BufferLocation = s_d3d.vp_vb->lpVtbl->GetGPUVirtualAddress(s_d3d.vp_vb)
                               + (u64)s_d3d.vp_parity * MAX_VERTICES * 256;
            vbv.SizeInBytes    = MAX_VERTICES * 256;
            vbv.StrideInBytes  = 256;   /* 16 float4 attrib slots */
            s_d3d.cmd_list->lpVtbl->IASetVertexBuffers(s_d3d.cmd_list, 0, 1, &vbv);
            if (s85_ib && s85_ib_state > 0) {   /* winport v85e-idxdraw */
                D3D12_INDEX_BUFFER_VIEW ibv85_;
                ibv85_.BufferLocation = s85_ib->lpVtbl->GetGPUVirtualAddress(s85_ib) + (u64)s_d3d.vp_parity * WP85_IB_CAP * 2u;
                ibv85_.SizeInBytes = WP85_IB_CAP * 2u; ibv85_.Format = DXGI_FORMAT_R16_UINT;
                s_d3d.cmd_list->lpVtbl->IASetIndexBuffer(s_d3d.cmd_list, &ibv85_);
            }
            wp85_idx_report();
            D3D12_GPU_DESCRIPTOR_HANDLE gh_base;
            s_d3d.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &gh_base);
            int cur_rt = -1;                       /* target A: -1 = backbuffer */
            u32 cur_zeta_keep = 0;                 /* winport-fix: guest depth surface */
            float vp_last_w = 0, vp_last_h = 0;    /* for DRAWARGS */
            float dvp_x = 0, dvp_y = 0, dvp_w = 0, dvp_h = 0;  /* per-draw, for DRAWARGS */
            int cur_m[3] = {-1, -1, -1};           /* MRT B/C/D: -1 = unbound   */
            double _rec0 = perf_on() ? perf_now() : 0.0;
            int seen_small_vp = 0, captured = 0;
            for (u32 d = 0; d < s_d3d.draw_count && d < MAX_DRAWS; d++) {
                const D3D12DrawRecord* dr = &s_d3d.draws[d];
                if (!dr->is_vp) continue;
                if (dr->is_blit) {   /* winport-fix: NV3089 blits on the GPU */
                    winport_blit_exec(dr, fi, rtv_handle);
                    ID3D12GraphicsCommandList* cl_ = s_d3d.cmd_list;
                    cl_->lpVtbl->SetGraphicsRootSignature(cl_, s_d3d.vp_root_sig);
                    cl_->lpVtbl->SetPipelineState(cl_, vpso);
                    cl_->lpVtbl->IASetPrimitiveTopology(cl_, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    cl_->lpVtbl->SetGraphicsRootConstantBufferView(cl_, 0,
                        s_d3d.vp_cb->lpVtbl->GetGPUVirtualAddress(s_d3d.vp_cb));
                    wp_set_heaps(cl_);   /* winport-fix: guest sampler state */ /* the blit bound only the SRV heap */
                    cl_->lpVtbl->SetGraphicsRootDescriptorTable(cl_, 1, gh_base);
                    if (wp_samp_on()) cl_->lpVtbl->SetGraphicsRootDescriptorTable(cl_, 3, wp_samp_table_key(0));
                    {
                        D3D12_CPU_DESCRIPTOR_HANDLE rh_[4]; UINT n_ = 1;
                        rh_[0] = rtv_handle;
                        if (cur_rt >= 0) {
                            off_rt_transition(cur_rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
                            rh_[0] = off_rt_rtv(cur_rt);
                        }
                        for (int m_ = 0; m_ < 3 && cur_m[m_] >= 0; m_++) {
                            off_rt_transition(cur_m[m_], D3D12_RESOURCE_STATE_RENDER_TARGET);
                            rh_[n_++] = off_rt_rtv(cur_m[m_]);
                        }
                        cl_->lpVtbl->OMSetRenderTargets(cl_, n_, rh_, FALSE, &dsv_handle);
                        float w_ = cur_rt >= 0 ? (float)s_d3d.off_rt[cur_rt].w : (float)s_d3d.width;
                        float h_ = cur_rt >= 0 ? (float)s_d3d.off_rt[cur_rt].h : (float)s_d3d.height;
                        w_ = (float)wp68_px((u32)w_); h_ = (float)wp68_px((u32)h_);   /* winport v68 */
                        D3D12_VIEWPORT v_ = {0, 0, w_, h_, 0.0f, 1.0f};
                        D3D12_RECT r_ = {0, 0, (LONG)w_, (LONG)h_};
                        cl_->lpVtbl->RSSetViewports(cl_, 1, &v_);
                        cl_->lpVtbl->RSSetScissorRects(cl_, 1, &r_);
                    }
                    continue;
                }
                /* The reduced-viewport passes (this title's reflection, drawn
                 * into a region of the same target) finish here -- snapshot
                 * before the full-screen scene overwrites them. */
                if (!dr->is_clear) {
                    if (dr->vp_w && dr->vp_w < s_d3d.width) {
                        seen_small_vp = 1;
                        subvp_note(dr->vp_x, dr->vp_y, dr->vp_w, dr->vp_h);
                    }
                    else if (seen_small_vp && !captured &&
                             dr->vp_w == s_d3d.width && dr->rt_off == 0) {
                        captured = 1;
                        screen_copy_capture(fi);
                        subvp_capture(fi);
                        s_d3d.cmd_list->lpVtbl->OMSetRenderTargets(
                            s_d3d.cmd_list, 1, &rtv_handle, FALSE, &dsv_handle);
                    }
                }
                /* Render-to-texture: retarget when this op's surfaces differ.
                 * Depth is a single shared buffer, so clear it per switch. */
                int want  = dr->rt_off  ? off_rt_find(dr->rt_off)  : -1;
                /* Remember where geometry actually went: off_rt_find() matches
                 * on offset alone and slots share offsets at different sizes,
                 * so the composite cannot just look the source up by address. */
                if (want >= 0) { s_last_drawn_slot = want; s_last_drawn_off = dr->rt_off; }
                int wantm[3];
                for (int _m = 0; _m < 3; _m++)
                    wantm[_m] = dr->rt_mrt[_m] ? off_rt_find(dr->rt_mrt[_m]) : -1;
                if (want != cur_rt || wantm[0] != cur_m[0] ||
                    wantm[1] != cur_m[1] || wantm[2] != cur_m[2]) {
                    const int keepz_ = (want == cur_rt && dr->zeta_off && dr->zeta_off == cur_zeta_keep);   /* winport-fix: v22c keep depth across MRT-only switches */
                    if (cur_zeta_keep && s_wp_zneed_n && !keepz_) wp_zs_take(cur_zeta_keep);   /* winport-fix: v22 scene depth */
                    wp_ts_mark(dr->rt_off, want >= 0 ? s_d3d.off_rt[want].w : s_d3d.width, want >= 0 ? s_d3d.off_rt[want].h : s_d3d.height);   /* winport-fix: GPU pass timing */
                    D3D12_CPU_DESCRIPTOR_HANDLE rh[4];
                    UINT nrt = 1;
                    rh[0] = rtv_handle;
                    D3D12_VIEWPORT vp = {0, 0, (float)s_d3d.width, (float)s_d3d.height, 0.0f, 1.0f};
                    if (want >= 0) {
                        off_rt_transition(want, D3D12_RESOURCE_STATE_RENDER_TARGET);
                        rh[0] = off_rt_rtv(want);
                        vp.Width  = (float)s_d3d.off_rt[want].w;
                        vp.Height = (float)s_d3d.off_rt[want].h;
                    }
                    for (int _m = 0; _m < 3 && wantm[_m] >= 0; _m++) {
                        off_rt_transition(wantm[_m], D3D12_RESOURCE_STATE_RENDER_TARGET);
                        rh[nrt++] = off_rt_rtv(wantm[_m]);
                    }
                    vp.Width = (float)wp68_px((u32)vp.Width); vp.Height = (float)wp68_px((u32)vp.Height);   /* winport v68 */
                    D3D12_RECT sc = {0, 0, (LONG)vp.Width, (LONG)vp.Height};
                    vp_last_w = vp.Width; vp_last_h = vp.Height;
                    s_d3d.cmd_list->lpVtbl->OMSetRenderTargets(s_d3d.cmd_list, nrt, rh, FALSE, &dsv_handle);
                    s_d3d.cmd_list->lpVtbl->RSSetViewports(s_d3d.cmd_list, 1, &vp);
                    s_d3d.cmd_list->lpVtbl->RSSetScissorRects(s_d3d.cmd_list, 1, &sc);
                    if (s_wp_cont && dr->rt_off == s_wp_cont_rt) s_wp_cont = 0;   /* winport-fix: flush a full batch instead of dropping draws */ /* resume: keep depth */
                    else { s_wp_cont = 0;
                    { static int zk_ = -1; if (zk_ < 0) { const char* e_ = getenv("WINPORT_ZKEEP"); zk_ = (e_ && atoi(e_)) ? 1 : 0; }
                      if (!(keepz_ || (zk_ && dr->zeta_off && dr->zeta_off == cur_zeta_keep)))   /* winport-fix: v22c keep depth across MRT-only switches */
                    s_d3d.cmd_list->lpVtbl->ClearDepthStencilView(s_d3d.cmd_list, dsv_handle,
                        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, (UINT8)wp_stencil_base(), 0, NULL); } }   /* winport v41 */
                    /* RT_CLEARDBG=2: magenta-clear the surface the DRAWS bind,
                     * at the moment they bind it. The guest's own clears may go
                     * to a different surface entirely (they do here: clears to
                     * 0xAB0000, draws to 0x1BE0000), so clearing on the clear
                     * path tests the wrong target. Anything not magenta
                     * afterwards was rasterised. */
                    { static int cd2 = -1;
                      if (cd2 < 0) { const char* e = getenv("RT_CLEARDBG"); cd2 = e ? atoi(e) : 0; }
                      if (cd2 >= 2 && want >= 0) {
                          const float mag[4] = {1.0f, 0.0f, 1.0f, 1.0f};
                          s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(
                              s_d3d.cmd_list, off_rt_rtv(want), mag, 0, NULL);
                      } }
                    /* VP_TESTTRI=1: our own triangle, through the game's exact
                     * pipeline -- same vpso, same root signature, same vertex
                     * buffer, same RTV just bound. Written at vertex 1000 so it
                     * cannot collide with the guest's data. Pair with
                     * VP_BYPASS=1 so the shader is a trivial passthrough.
                     *
                     * If this renders, the GPU does read vp_vb and the fault is
                     * in the guest draws' own state. If it does not, nothing
                     * this backend draws reaches a render target at all. */
                    { static int tt = -1;
                      if (tt < 0) { const char* e = getenv("VP_TESTTRI"); tt = e ? atoi(e) : 0; }
                      if (tt && want >= 0 && s_d3d.vp_vb_mapped) {
                          float* vtx = (float*)((char*)s_d3d.vp_vb_mapped
                              + (u64)s_d3d.vp_parity * MAX_VERTICES * 256u
                              + (u64)1000 * 256u);
                          static const float tri[3][4] = {
                              {-2.0f, -2.0f, 0.0f, 1.0f},
                              { 2.0f, -2.0f, 0.0f, 1.0f},
                              { 0.0f,  2.0f, 0.0f, 1.0f} };
                          for (int _v = 0; _v < 3; _v++) {
                              float* dstv = vtx + (size_t)_v * (256u / 4u);
                              dstv[0] = tri[_v][0]; dstv[1] = tri[_v][1];
                              dstv[2] = tri[_v][2]; dstv[3] = tri[_v][3];
                          }
                          s_d3d.cmd_list->lpVtbl->DrawInstanced(s_d3d.cmd_list, 3, 1, 1000, 0);
                          { static int _n = 0;
                            if (_n++ < 3) fprintf(stderr, "[TESTTRI] issued on rt=%d off=0x%08X%c",
                                                  want, s_d3d.off_rt[want].off, 10); }
                      } }
                    cur_rt = want; cur_zeta_keep = dr->zeta_off;
                    s_wp_last_rt = dr->rt_off;   /* winport-fix: flush a full batch instead of dropping draws */
                    for (int _m = 0; _m < 3; _m++) cur_m[_m] = wantm[_m];
                }
                if (dr->is_clear && (dr->clear_flags & 3u)) {   /* winport-fix: honour RSX clear flags */
                    D3D12_CLEAR_FLAGS zf_ = (D3D12_CLEAR_FLAGS)(((dr->clear_flags & 1u) ? D3D12_CLEAR_FLAG_DEPTH : 0) |
                                                              ((dr->clear_flags & 2u) ? D3D12_CLEAR_FLAG_STENCIL : 0));
                    s_d3d.cmd_list->lpVtbl->ClearDepthStencilView(s_d3d.cmd_list, dsv_handle, zf_,
                        dr->clear_z, (UINT8)dr->clear_s, 0, NULL);
                }
                if (dr->is_clear && !(dr->clear_flags & 0xF0u)) continue;   /* winport-fix: honour RSX clear flags */
                if (dr->is_clear) {
                    D3D12_CPU_DESCRIPTOR_HANDLE rh =
                        (cur_rt >= 0) ? off_rt_rtv(cur_rt) : rtv_handle;
                    /* RT_CLEARDBG=1: clear offscreen targets to magenta rather
                     * than the guest colour. "0.000% nonzero" cannot tell an
                     * untouched surface from one written black, and that
                     * ambiguity invalidates every read of an offscreen target.
                     * If the surface comes back magenta the clears land and the
                     * draws are writing black; if it stays black, nothing
                     * reaches it at all. */
                    { static int cd = -1;
                      if (cd < 0) { const char* e = getenv("RT_CLEARDBG"); cd = e ? atoi(e) : 0; }
                      if (cd && cur_rt >= 0) {
                          const float mag[4] = {1.0f, 0.0f, 1.0f, 1.0f};
                          s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(s_d3d.cmd_list, rh, mag, 0, NULL);
                      } else
                          s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(s_d3d.cmd_list, rh, dr->cc, 0, NULL); }
                    for (int _m = 0; _m < 3; _m++)
                        if (cur_m[_m] >= 0) {
                            D3D12_CPU_DESCRIPTOR_HANDLE rhm = off_rt_rtv(cur_m[_m]);
                            s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(s_d3d.cmd_list, rhm, dr->cc, 0, NULL);
                        }
                    continue;
                }
                /* Per-draw pipeline: prefer the guest's own compiled FP; fall
                 * back to the hardcoded atlas/colour PS pair. */
                ID3D12PipelineState* dpso =
                    dr->fp_addr ? vp_get_fp_pso((s_wp_uh = dr->fp_uhash, s_wp_alpha = WP_ALPHA_KEY(dr), WP_STK_SET(dr), s_wp_topo = WP_TOPO_CLASS(dr), dr->vs_idx), dr->fp_addr, dr->blend_key,
                                                dr_num_rts(dr),
                                                dr->rt_off ? rsx_surface_dxgi(dr->rt_fmt)
                                                           : DXGI_FORMAT_R8G8B8A8_UNORM,
                                                dr->fp_exp32, dr->cmask, dr->cull,
                                                dr_cube_mask(dr)) : NULL;
                s_d3d.cmd_list->lpVtbl->SetPipelineState(s_d3d.cmd_list,
                                                         dpso ? dpso : vpso);
                /* Per-draw viewport: the guest rect when sane, else the
                 * full target. Scissor tracks the same rect. */
                {
                    float tw = (cur_rt >= 0) ? (float)s_d3d.off_rt[cur_rt].w : (float)s_d3d.width;
                    float th = (cur_rt >= 0) ? (float)s_d3d.off_rt[cur_rt].h : (float)s_d3d.height;
                    D3D12_VIEWPORT dvp = {0, 0, tw, th, 0.0f, 1.0f};
                    if (dr->vp_w >= 2 && dr->vp_h >= 2 &&
                        (float)(dr->vp_x + dr->vp_w) <= tw + 0.5f &&
                        (float)(dr->vp_y + dr->vp_h) <= th + 0.5f) {
                        dvp.TopLeftX = (float)dr->vp_x;
                        dvp.TopLeftY = (float)dr->vp_y;
                        dvp.Width    = (float)dr->vp_w;
                        dvp.Height   = (float)dr->vp_h;
                    }
                    if (s_wp68_s != 1.0f) {   /* winport v68: guest rect (validated above in guest px) -> target px */
                        dvp.TopLeftX *= s_wp68_s; dvp.TopLeftY *= s_wp68_s;
                        dvp.Width *= s_wp68_s; dvp.Height *= s_wp68_s; s_wp68_n_vp++;
                    }
                    D3D12_RECT dsc = {(LONG)dvp.TopLeftX, (LONG)dvp.TopLeftY,
                                      (LONG)(dvp.TopLeftX + dvp.Width),
                                      (LONG)(dvp.TopLeftY + dvp.Height)};
                    dvp_x = dvp.TopLeftX; dvp_y = dvp.TopLeftY;
                    dvp_w = dvp.Width;    dvp_h = dvp.Height;
                    s_d3d.cmd_list->lpVtbl->RSSetViewports(s_d3d.cmd_list, 1, &dvp);
                    s_d3d.cmd_list->lpVtbl->RSSetScissorRects(s_d3d.cmd_list, 1, &dsc);
                }
                if (s_wp40_fbl[d]) {   /* winport v40: orbs -- feedback loop: refresh the copy (or skip, =2) */
                    if (wp40_orb_mode() == 2) continue;
                    wp40_fbc_copy((int)s_wp40_fbl[d] - 1);
                }
                /* Per-draw textures: bind this draw's t0-t3 SRV window.
                 * Any sampled offscreen RT transitions to PSR first (never
                 * one of the currently-bound colour targets). */
                for (int _u = 0; _u < 4; _u++) {
                    int rt = dr->tex_rt[_u];
                    if (rt >= 0 && rt != cur_rt &&
                        rt != cur_m[0] && rt != cur_m[1] && rt != cur_m[2])
                        off_rt_transition(rt, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                }
                D3D12_GPU_DESCRIPTOR_HANDLE gh = gh_base;
                gh.ptr += (u64)(DRAW_SRV_BASE + d * 4) * s_d3d.srv_inc;
                s_d3d.cmd_list->lpVtbl->SetGraphicsRootDescriptorTable(s_d3d.cmd_list, 1, gh);
                if (wp_samp_on()) s_d3d.cmd_list->lpVtbl->SetGraphicsRootDescriptorTable(s_d3d.cmd_list, 3, wp_samp_table(dr));   /* winport-fix: guest sampler state */
                /* Per-draw constants: this draw's vp_cb + FP texscale slots. */
                s_d3d.cmd_list->lpVtbl->SetGraphicsRootConstantBufferView(s_d3d.cmd_list, 0,
                    s_d3d.vp_cb->lpVtbl->GetGPUVirtualAddress(s_d3d.vp_cb)
                    + ((u64)s_d3d.vp_parity * MAX_DRAWS + dr->cb_slot) * VP_CB_STRIDE);
                if (s_d3d.vp_fpcb)
                    s_d3d.cmd_list->lpVtbl->SetGraphicsRootConstantBufferView(s_d3d.cmd_list, 2,
                        s_d3d.vp_fpcb->lpVtbl->GetGPUVirtualAddress(s_d3d.vp_fpcb)
                        + ((u64)s_d3d.vp_parity * MAX_DRAWS + dr->cb_slot) * VP_FPCB_STRIDE);
                /* DRAWARGS=<N>: the arguments actually handed to the GPU, plus
                 * the target they are aimed at. A zero vertex count, or a start
                 * vertex past the buffer, rasterises nothing while every other
                 * check upstream still says the draw is fine. */
                { static int dcap = -1, dn = 0;
                  if (dcap < 0) { const char* e = getenv("DRAWARGS"); dcap = e ? atoi(e) : 0; }
                  if (dcap && dn < dcap) { dn++;
                      D3D12_CPU_DESCRIPTOR_HANDLE _rh =
                          (cur_rt >= 0) ? off_rt_rtv(cur_rt) : rtv_handle;
                      fprintf(stderr, "[DRAWARGS] verts=%u start=%u rt=%d rec=0x%08X bound=0x%08X"
                              " tex0=0x%08X texrt=%d dvp=%.0f,%.0f %.0fx%.0f%c",
                              dr->vertex_count, dr->vb_byte_offset / 256, cur_rt,
                              dr->rt_off,
                              (cur_rt >= 0) ? s_d3d.off_rt[cur_rt].off : 0u,
                              dr->tex[0].raw, dr->tex_rt[0],
                              (double)dvp_x, (double)dvp_y,
                              (double)dvp_w, (double)dvp_h, 10); } }
                /* RT_REBIND=1: re-issue OMSetRenderTargets immediately before
                 * every draw instead of only when the target changes. A clear
                 * proves the RTV descriptor is good but not that the binding is
                 * still live at draw time -- ClearRenderTargetView writes
                 * through the descriptor and never consults the OM. If anything
                 * between the retarget and here drops the binding, this restores
                 * it. */
                { static int rb = -1;
                  if (rb < 0) { const char* e = getenv("RT_REBIND"); rb = e ? atoi(e) : 0; }
                  if (rb) {
                      D3D12_CPU_DESCRIPTOR_HANDLE brh[4];
                      UINT bn = 1;
                      brh[0] = (cur_rt >= 0) ? off_rt_rtv(cur_rt) : rtv_handle;
                      for (int _m = 0; _m < 3; _m++)
                          if (cur_m[_m] >= 0) brh[bn++] = off_rt_rtv(cur_m[_m]);
                      s_d3d.cmd_list->lpVtbl->OMSetRenderTargets(
                          s_d3d.cmd_list, bn, brh, FALSE, &dsv_handle);
                  } }
                if (wp_stencil_on() && (dr->wp_stk0 & 1u))   /* winport v39: per-draw stencil reference */
                    s_d3d.cmd_list->lpVtbl->OMSetStencilRef(s_d3d.cmd_list, (UINT)(dr->wp_sref & 0xFFu));
                if (WP_TOPO_CLASS(dr)) {   /* winport v42: WINPORT_LINE_FIX line/point records */
                    s_d3d.cmd_list->lpVtbl->IASetPrimitiveTopology(s_d3d.cmd_list, topo_to_d3d(dr->topology));
                    s_d3d.cmd_list->lpVtbl->DrawInstanced(s_d3d.cmd_list, dr->vertex_count, 1, dr->vb_byte_offset / 256, 0);
                    s_d3d.cmd_list->lpVtbl->IASetPrimitiveTopology(s_d3d.cmd_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                } else if (dr->wp85_ib)   /* winport v85e-idxdraw */
                s_d3d.cmd_list->lpVtbl->DrawIndexedInstanced(s_d3d.cmd_list, dr->vertex_count, 1, dr->wp85_ib - 1u,
                    (INT)(dr->vb_byte_offset / 256), 0);
                else
                s_d3d.cmd_list->lpVtbl->DrawInstanced(s_d3d.cmd_list,
                    dr->vertex_count, 1, dr->vb_byte_offset / 256, 0);
                if (s_ts_n) s_ts_nd[s_ts_n - 1]++;   /* winport-fix: GPU pass timing */
                /* VP_SUBMIT=<N>: prove the VP pass actually reaches the GPU.
                 * "records exist" and "draws were submitted" are different
                 * claims, and every blank-output investigation conflates them. */
                { static int cap = -1, n = 0;
                  if (cap < 0) { const char* e = getenv("VP_SUBMIT"); cap = e ? atoi(e) : 0; }
                  if (cap && n < cap) { n++;
                    fprintf(stderr, "[VPSUBMIT] draw[%u] verts=%u pso=%s vp=%ux%u tex0=0x%X(set=%d) tex1=0x%X slot=%d fp=0x%X%c",
                            d, dr->vertex_count, dpso ? "guest-fp" : "fallback",
                            dr->vp_w, dr->vp_h, dr->tex[0].raw, dr->tex[0].set,
                            dr->tex[1].raw, dr->tex_slot, dr->fp_addr, 10);
                    fprintf(stderr, "            vs_idx=%d cb_slot=%u vbofs=%u cull=%u blend=%d%c",
                            dr->vs_idx, dr->cb_slot, dr->vb_byte_offset / 256,
                            dr->cull, dr->blend, 10); } }
            }
            if (perf_on()) s_perf_gpu += perf_now() - _rec0;   /* reuse: record time */
            /* Leave the backbuffer bound for the dump/present epilogue. */
            if (cur_rt >= 0) {
                s_d3d.cmd_list->lpVtbl->OMSetRenderTargets(s_d3d.cmd_list, 1, &rtv_handle, FALSE, &dsv_handle);
                D3D12_VIEWPORT vp = {0, 0, (float)WP68_BW, (float)WP68_BH, 0.0f, 1.0f};   /* winport v68 */
                D3D12_RECT sc = {0, 0, (LONG)WP68_BW, (LONG)WP68_BH};
                s_d3d.cmd_list->lpVtbl->RSSetViewports(s_d3d.cmd_list, 1, &vp);
                s_d3d.cmd_list->lpVtbl->RSSetScissorRects(s_d3d.cmd_list, 1, &sc);
            }
        }
    }

    if (perf_on()) s_perf_rf += perf_now() - _rf0;
    /* MEMPEEK=<hex addr>:<n>: n big-endian floats at a guest address each frame,
     * with a min/max so a buffer of constants is distinguishable from real data. */
    { const char* mp = getenv("MEMPEEK");
      if (mp) { extern uint8_t* vm_base; static int _n = 0;
        u32 addr = 0; int cnt = 8; sscanf(mp, "%x:%d", &addr, &cnt);
        if (vm_base && addr && _n++ < 6) {
            float lo = 1e30f, hi = -1e30f; u32 nz = 0;
            for (int i = 0; i < cnt; i++) {
                const u8* q = vm_base + addr + i * 4;
                u32 bits = ((u32)q[0]<<24)|((u32)q[1]<<16)|((u32)q[2]<<8)|q[3];
                float f; memcpy(&f, &bits, 4);
                if (f == f && f < 1e30f && f > -1e30f) { if (f < lo) lo = f; if (f > hi) hi = f; }
                if (bits) nz++;
            }
            fprintf(stderr, "[MEMPEEK] 0x%08X n=%d nonzero=%u range[%g..%g]%c",
                    addr, cnt, nz, lo, hi, 10);
        } } }
    /* VRAMSCAN=1: which 64 KB blocks of guest memory change between frames.
     * "nothing writes the buffer we read" is only half an answer -- this says
     * where the producer IS writing, so the two can be matched up. */
    { static int vs = -1; if (vs < 0) vs = getenv("VRAMSCAN") ? 1 : 0;
      if (vs) { extern uint8_t* vm_base;
        static u32 prev[512]; static int have = 0; static int reported = 0;
        u32 base = 0xC6000000u; u32 nblk = 512;          /* 32 MB window */
        const char* bs = getenv("VRAMSCAN_BASE");
        if (bs) base = (u32)strtoul(bs, NULL, 0);
        if (vm_base && reported < 6) {
            u32 changed = 0; char list[512]; int ln = 0;
            for (u32 b = 0; b < nblk; b++) {
                const u8* p = vm_base + base + b * 0x10000u;
                u32 h = 2166136261u;
                for (u32 i = 0; i < 0x10000u; i += 61) h = (h ^ p[i]) * 16777619u;
                if (have && h != prev[b]) { changed++;
                    if (ln < 400) ln += snprintf(list + ln, sizeof(list) - ln,
                                                 " +0x%X", b * 0x10000u); }
                prev[b] = h;
            }
            if (have && changed) { reported++;
                fprintf(stderr, "[VRAMSCAN] base=0x%08X %u/512 blocks changed:%s%c",
                        base, changed, list, 10); }
            have = 1;
        } } }
    /* FRAME_BUDGET=1: geometry a frame asked for vs what the batch can hold.
     * Reported from render_frame so it works under RSX_ACCUM_FRAME too -- it
     * used to live in the clear-boundary present path, which accum bypasses, so
     * it silently produced nothing in exactly the configuration being used. */
    { static int fb = -1;
      if (fb < 0) { const char* e = getenv("FRAME_BUDGET"); fb = e ? atoi(e) : 0; }
      if (fb && s_req_draws) { static int n = 0; if (n++ < 40)
        fprintf(stderr, "[BUDGET] frame: requested %llu verts / %llu draws, "
                        "buffer holds %u verts, %llu draws truncated%c",
                (unsigned long long)s_req_verts, (unsigned long long)s_req_draws,
                MAX_VERTICES, (unsigned long long)s_drop_draws, 10); }
      s_req_verts = 0; s_req_draws = 0; s_drop_draws = 0; }
    if (perf_on()) {
        static double s_t_prev = 0.0; static int s_n = 0;
        double now = perf_now();
        if (s_t_prev > 0.0) s_perf_frame += now - s_t_prev;
        s_t_prev = now;
        if (++s_n % 20 == 0) {
            double fr = s_perf_frame > 0 ? s_perf_frame : 1;
            fprintf(stderr, "[PERF] %.2f fps | tex %.2fs (%.0f%%, %d calls) | vtx %.2fs"
                            " (%.0f%%, %.0fk verts) | render_frame %.2fs (%.0f%%)"
                            " | prepass %.2fs (%.0f%%) [srv %.2fs %.0f%% | pso %.2fs %.0f%% %d calls %d MISS %dKB hashed] | guest %.2fs (%.0f%%)%c",
                    20.0 / fr,
                    s_perf_tex, 100.0 * s_perf_tex / fr, s_perf_ntex,
                    s_perf_vtx, 100.0 * s_perf_vtx / fr,
                    (double)s_perf_nverts / 1000.0,
                    s_perf_rf, 100.0 * s_perf_rf / fr,
                    s_perf_pre, 100.0 * s_perf_pre / fr,
                    s_perf_srv, 100.0 * s_perf_srv / fr,
                    s_perf_pso, 100.0 * s_perf_pso / fr,
                    s_perf_pso_calls, s_perf_pso_miss, s_perf_pso_hashbytes / 1024,
                    fr - s_perf_rf, 100.0 * (fr - s_perf_rf) / fr, 10);
            fprintf(stderr, "[VTXSTAT] %llu vertex-fetch draws (%llu on the fast path) | %.0f verts/draw | %.1f enabled attributes/vertex (both paths, v25) but every vertex is written as 256 bytes (%.0f MB/s written vs ~%.0f MB/s needed) | fetch into cached scratch (WINPORT_VTXBENCH=1) %.2fs vs real %.2fs%c",
                    (unsigned long long)s_perf_vdraws, (unsigned long long)s_perf_vfast,
                    s_perf_vdraws ? (double)s_perf_nverts / (double)s_perf_vdraws : 0.0,
                    s_perf_nverts ? (double)s_perf_vattr / (double)s_perf_nverts : 0.0,
                    (double)s_perf_nverts * 256.0 / 1048576.0 / fr,
                    (double)s_perf_vattr * 16.0 / 1048576.0 / fr,
                    s_perf_vcache, s_perf_vtx, 10);
            fprintf(stderr, "[VTXSLOW] draws slow=%llu | type6(CMP)=%llu type0=%llu type1=%llu type2=%llu type3=%llu type4=%llu type5=%llu type7=%llu | nonlinear=%llu | stride0=%llu | size=%llu | other=%llu (disabled=%llu) | vp reads %.2f of %.2f enabled attrs/draw (avg)%c",   /* winport v25: first miss reason per draw; stride0/size = draws that HAVE such an attr (informational, they do not force the slow path) */
                    (unsigned long long)s_vs_slow, (unsigned long long)s_vs_type[6], (unsigned long long)s_vs_type[0], (unsigned long long)s_vs_type[1], (unsigned long long)s_vs_type[2],
                    (unsigned long long)s_vs_type[3], (unsigned long long)s_vs_type[4], (unsigned long long)s_vs_type[5], (unsigned long long)s_vs_type[7],
                    (unsigned long long)s_vs_nonlin, (unsigned long long)s_vs_stride0, (unsigned long long)s_vs_size, (unsigned long long)s_vs_other, (unsigned long long)s_vs_disabled,
                    s_perf_vdraws ? (double)s_vs_vpread / (double)s_perf_vdraws : 0.0, s_perf_vdraws ? (double)s_vs_vpen / (double)s_perf_vdraws : 0.0, 10);
            if (s_wpv_draws) fprintf(stderr, "[VTXPAR] parallel draws=%llu verts=%llu%c", (unsigned long long)s_wpv_draws, (unsigned long long)s_wpv_verts, 10);   /* winport v27 */
            s_wpv_draws = s_wpv_verts = 0;
            s_vs_slow = s_vs_nonlin = s_vs_stride0 = s_vs_size = s_vs_disabled = s_vs_other = s_vs_vpread = s_vs_vpen = 0; memset(s_vs_type, 0, sizeof s_vs_type);
            s_perf_vcache = 0.0; s_perf_vdraws = s_perf_vattr = s_perf_vfast = 0;
            fprintf(stderr, "[TEXSTAT] per 20 frames: same-frame hit %u | unchanged %u | changed-reupload %u | new slot %u | evicted %u | no slot %u | %.1f MB decoded%c",   /* winport-fix: texture slot LRU */
                    s_wp_tx_hit, s_wp_tx_csum, s_wp_tx_stale, s_wp_tx_new, s_wp_tx_evict, s_wp_tx_fail, (double)s_wp_tx_bytes / 1048576.0, 10);
            s_wp_tx_hit = s_wp_tx_csum = s_wp_tx_stale = s_wp_tx_new = s_wp_tx_evict = s_wp_tx_fail = 0; s_wp_tx_bytes = 0;
            s_perf_frame = 0.0; s_perf_tex = 0.0; s_perf_vtx = 0.0; s_perf_rf = 0.0;
            s_perf_gpu = 0.0; s_perf_pre = 0.0; s_perf_srv = 0.0; s_perf_pso = 0.0;
            s_perf_pso_calls = 0; s_perf_pso_miss = 0; s_perf_pso_hashbytes = 0;
            s_perf_ntex = 0; s_perf_texbytes = 0; s_perf_nverts = 0;
        }
    }
    { static int dt = -1;
      if (dt < 0) { const char* e = getenv("RSX_DBG_TRACK"); dt = e ? atoi(e) : 0; }
      if (dt) { static int fr = 0; fr++;
        if (s_lock_n)
            fprintf(stderr, "[RSX_DBG_TRACK] frame %d: %u duck draws, ndc=(%.3f,%.3f)%c",
                    fr, s_lock_n, (float)(s_lock_sx/s_lock_n), (float)(s_lock_sy/s_lock_n), 10);
        else if (fr % 20 == 0)
            fprintf(stderr, "[RSX_DBG_TRACK] frame %d: NO duck draws%c", fr, 10);
      } }
    if (s_lock_n) {                       /* publish this frame's centroid */
        s_lock_x = (float)(s_lock_sx / s_lock_n);
        s_lock_y = (float)(s_lock_sy / s_lock_n);
        s_lock_valid = 1;
        s_lock_sx = s_lock_sy = 0.0; s_lock_n = 0;
    }
    wp81_scan();   /* winport v81-ui: find the game's font atlas / plaque among this frame's textures */
    s_dbg_last_draws = s_d3d.draw_count;
    /* winport v26: the PPU memory fast path (ppu_vm_fast_policy.c: plain load/store + bswap, no debug/trace checks)
     * is armed by the FIRST frame with >= 400 draws -- that is how the engine knows gameplay has started. Only the
     * Metal backend ever called this (rsx_metal_backend.m ~5670), so on Windows every guest access stayed on the
     * slow debug path (v25 profile: vm_* helpers ~31% of the main thread). WINPORT_VM_GATE=1 does what macOS does;
     * default 0 = old behaviour. The engine prints "[vm] gameplay fast path ..." once when it arms. */
    { static int g = -1, th = 400; if (g < 0) { const char* e = getenv("WINPORT_VM_GATE"); g = e ? (atoi(e) != 0) : 0;
                                                 e = getenv("WINPORT_VM_GATE_DRAWS"); if (e && atoi(e) > 0) th = atoi(e); }
      /* WINPORT_VM_GATE_DRAWS=N: arm at N draws/frame instead of the engine's 400 (low-draw scenes, benchmarks) */
      if (g && s_d3d.draw_count >= (u32)th) { extern void ps3_vm_note_gameplay_draws(unsigned draws); ps3_vm_note_gameplay_draws(400u > s_d3d.draw_count ? 400u : s_d3d.draw_count); } }
    /* winport v30: same "gameplay started" signal for PS3_PAD_AUTOSTART's combat rotation (E465: SQUARE/TRIANGLE/
     * CROSS/CIRCLE), as rsx_metal_backend.m does -- without it the autoplay never fights on Windows. Only sets a flag. */
    { static int armed; if (!armed && s_d3d.draw_count >= 400u) { extern void pad_autoplay_note_gameplay(void); armed = 1; pad_autoplay_note_gameplay(); } }
    wp85_ib_reset();   /* winport v85e-idxdraw */
    s_d3d.vb_offset  = 0; /* reset for next frame */
    s_d3d.vp_vb_offset = 0;
    s_d3d.draw_count = 0;
    s_wp_cont = 0;   /* winport-fix: flush a full batch instead of dropping draws */ /* only the batch right after a split resumes */
    if (wp77_on()) {   /* winport v77-fif: 3 slots; the next one is free once its last user finished */
        s_d3d.vp_parity = (s_d3d.vp_parity + 1) % 3;
        wp77_wait(s77_par_use[s_d3d.vp_parity], 1);
    } else
    s_d3d.vp_parity ^= 1;  /* next frame's records go to the other half */

    /* Debug: RTT_SAVERT=<hex raw offset>[:frame] copies that offscreen RT
     * into a readback buffer this frame and writes rt_save.bmp (half-float
     * RTs are tonemapped |v| -> byte). */
    static ID3D12Resource* s_rtsave_buf = NULL;
    static u32 s_rtsave_state = 0;   /* 1 = copy queued this frame */
    static u32 s_rtsave_w, s_rtsave_h, s_rtsave_pitch, s_rtsave_dxgi;
    { const char* sv = getenv("RTT_SAVERT");
      static int _done = 0;
      static u32 _skip = 0xFFFFFFFFu;
      if (_skip == 0xFFFFFFFFu) { const char* sk = getenv("RTT_SAVERT_SKIP");
          _skip = sk ? (u32)strtoul(sk, NULL, 0) : 0; }   /* frame 60 is far too
          early for a title that takes a minute to reach its menu */
      /* RTT_SAVERT_AFTER=<seconds> triggers on wall time instead of a frame
       * count. This title runs its menu at a few frames a second, so a frame
       * number is not a usable way to say "once it has settled". */
      static u32 _after = 0xFFFFFFFFu;
      if (_after == 0xFFFFFFFFu) { const char* sa = getenv("RTT_SAVERT_AFTER");
          _after = sa ? (u32)strtoul(sa, NULL, 0) : 0; }
      static clock_t _t0 = 0;
      if (!_t0) _t0 = clock();
      const int _due = _after
          ? ((u32)((clock() - _t0) / CLOCKS_PER_SEC) >= _after)
          : (s_d3d.frame_count > 60 + _skip);
      { static int _p1 = 0;
        if (sv && _due && !_p1) { _p1 = 1;
            printf("[RTT_SAVERT] due: want=0x%lX found_rt=%d frame=%u\n",
                   strtoul(sv, NULL, 16), off_rt_find((u32)strtoul(sv, NULL, 16)),
                   (unsigned)s_d3d.frame_count); fflush(stdout); } }
      if (sv && !_done && _due) {
        int rt = off_rt_find((u32)strtoul(sv, NULL, 16));
        fprintf(stderr, "[RTT_SAVERT] rt=%d res=%p\n", rt, rt >= 0 ? (void*)s_d3d.off_rt[rt].res : NULL);
        if (rt >= 0 && s_d3d.off_rt[rt].res) {
            OffRT* r = &s_d3d.off_rt[rt];
            u32 bpp = (r->dxgi == DXGI_FORMAT_R16G16B16A16_FLOAT) ? 8 :
                      (r->dxgi == DXGI_FORMAT_R32G32B32A32_FLOAT) ? 16 : 4;
            const u32 rw68 = wp68_px(r->w), rh68 = wp68_px(r->h);   /* winport v68 */
            u32 pitch = (rw68 * bpp + 255) & ~255u;
            fprintf(stderr, "[RTT_SAVERT] rt=%d w=%u h=%u bpp=%u pitch=%u dxgi=%d bytes=%llu\n",
                    rt, rw68, rh68, bpp, pitch, (int)r->dxgi,
                    (unsigned long long)((u64)pitch * rh68));
            if (!s_rtsave_buf) {
                D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_READBACK;
                D3D12_RESOURCE_DESC rd = {0};
                rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                rd.Width = (u64)pitch * rh68; rd.Height = 1; rd.DepthOrArraySize = 1;
                rd.MipLevels = 1; rd.SampleDesc.Count = 1;
                rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                s_d3d.device->lpVtbl->CreateCommittedResource(
                    s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &rd,
                    D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                    &IID_ID3D12Resource, (void**)&s_rtsave_buf);
            }
            if (!s_rtsave_buf) fprintf(stderr, "[RTT_SAVERT] readback buffer alloc FAILED\n");
            if (s_rtsave_buf) {
                off_rt_transition(rt, D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3D12_TEXTURE_COPY_LOCATION cdst = {0}, csrc = {0};
                cdst.pResource = s_rtsave_buf;
                cdst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                cdst.PlacedFootprint.Footprint.Format   = (DXGI_FORMAT)r->dxgi;
                cdst.PlacedFootprint.Footprint.Width    = rw68;
                cdst.PlacedFootprint.Footprint.Height   = rh68;
                cdst.PlacedFootprint.Footprint.Depth    = 1;
                cdst.PlacedFootprint.Footprint.RowPitch = pitch;
                csrc.pResource = r->res;
                csrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &cdst, 0, 0, 0, &csrc, NULL);
                off_rt_transition(rt, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                s_rtsave_state = 1;
                s_rtsave_w = rw68; s_rtsave_h = rh68;
                s_rtsave_pitch = pitch; s_rtsave_dxgi = r->dxgi;
                fprintf(stderr, "[RTT_SAVERT] copy queued: rt=%d %ux%u pitch=%u\n",
                        rt, rw68, rh68, pitch);
                _done = 1;
            }
        }
      } }

    /* CELLMARK_DUMP_MINDRAWS=<N>: only consider frames carrying at least N draw
     * records. Most render_frame calls come from the guest flip with a batch the
     * clear boundary already drained, so they present an empty backbuffer -- and
     * sampling those wastes every dump on a black image while the frames that do
     * hold the scene go uncaptured. */
    { static int mind = -1;
      if (mind < 0) { const char* e = getenv("CELLMARK_DUMP_MINDRAWS");
                      mind = e ? atoi(e) : 0; }
      /* draw_count is already reset by here; s_dbg_last_draws holds the
       * count this frame actually rendered. */
      if (mind > 0 && s_dbg_last_draws < (u32)mind) goto skip_dump_consider; }
    if (s_d3d.dump_skip_left > 0 && s_d3d.dump_frames_left > 0) s_d3d.dump_skip_left--;
skip_dump_consider: ;
    guest_fb_present(fi);
    composite_present(fi);
    if (s_present_this_frame) wp49_grab_game_frame(fi);   /* winport v49-cutscene-style */
    if (s_present_this_frame) wp92_thumb_tick(fi);   /* winport v92a-ckpt-list */
    wp94_capture(fi);   /* winport v94a-ffhaze: the game's own frame, before movie/UI overlays */
    s_wp72_pop_ok = s_present_this_frame;   /* winport v72-moviepace: only presented frames take a movie picture */
    winport_movie_overlay(fi, rtv_handle);   /* winport-fix: host movie overlay */
    s_wp72_pop_ok = 0;
    if (s_present_this_frame) wp47_ui_overlay(rtv_handle);   /* winport v47-cutscene-ui */
    if (s_present_this_frame) wp69_fps_overlay(rtv_handle);   /* winport v69-fps-f11 */
    if (s_present_this_frame) wp80_panel(rtv_handle);   /* winport v80-port-settings */
    int dumping = (s_d3d.dump_frames_left > 0 && s_d3d.readback_buf
                   && s_d3d.dump_skip_left == 0);
    { static int mind2 = -1;
      if (mind2 < 0) { const char* e = getenv("CELLMARK_DUMP_MINDRAWS");
                       mind2 = e ? atoi(e) : 0; }
      if (mind2 > 0 && s_dbg_last_draws < (u32)mind2) dumping = 0; }
    { static int secs = -1; static DWORD last = 0;   /* winport-fix: CELLMARK_DUMP_SECS */
      if (secs < 0) { const char* e = getenv("CELLMARK_DUMP_SECS"); secs = e ? atoi(e) : 0; }
      if (secs > 0) {
          dumping = 0;
          if (s_d3d.dump_frames_left > 0 && s_d3d.readback_buf && s_dbg_last_draws > 0) {
              DWORD now = GetTickCount();
              if (!last || now - last >= (DWORD)secs * 1000u) { dumping = 1; last = now; }
          }
      } }
    /* CELLMARK_DUMP_EVERY=N: after each dump, skip N-1 frames -- samples the whole
     * run instead of one contiguous window, so a short burst of real content
     * can't fall between the dumped frames. */
    if (dumping) {
        static int s_every = -1;
        if (s_every < 0) { const char* e = getenv("CELLMARK_DUMP_EVERY");
                           s_every = e ? atoi(e) : 0; }
        if (s_every > 1) s_d3d.dump_skip_left = s_every - 1;
    }
    if (!wp94_prev_mode()) wp92_prev_capture(fi);   /* winport v94a-ffhaze (v92b capture only without 94) -- winport v92b-prevframe: the finished frame, for next frame's display-buffer textures */
    if (dumping) {
        /* RT -> COPY_SOURCE, copy into the readback buffer, then -> PRESENT. */
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &barrier);

        D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
        dst.pResource = s_d3d.readback_buf;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Offset = 0;
        dst.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width    = WP68_BW;   /* winport v68 */
        dst.PlacedFootprint.Footprint.Height   = WP68_BH;
        dst.PlacedFootprint.Footprint.Depth    = 1;
        dst.PlacedFootprint.Footprint.RowPitch = s_d3d.readback_pitch;
        src.pResource = s_d3d.render_targets[fi];
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &barrier);
    } else {
        /* End-of-frame fallback snapshot: used when the frame never contained a
         * reduced-viewport pass to capture mid-frame. */
        screen_copy_capture(fi);
        /* Transition render target to PRESENT state */
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &barrier);
    }

    /* Close and execute */
    wp_ts_resolve();   /* winport-fix: GPU pass timing */
    s_d3d.cmd_list->lpVtbl->Close(s_d3d.cmd_list);
    ID3D12CommandList* cmd_lists[] = {(ID3D12CommandList*)s_d3d.cmd_list};
    s_d3d.cmd_queue->lpVtbl->ExecuteCommandLists(s_d3d.cmd_queue, 1, cmd_lists);
    wp77_after_submit();   /* winport v77-fif */
    if (wp77_on() && s77_legacy) wp77_wait(s77_val, 6);   /* legacy vertex buffer is not slotted: drain */

    /* D3D12_IQ=1: drain the debug layer's message queue after submitting the
     * frame. The legacy (is_vp=0) DrawInstanced path silently produced ZERO
     * pixels in flOw's render injection while ClearRenderTargetView worked, and
     * every input (verts, offsets, layout, MVP, PSO, root sig, depth, cull) was
     * verified correct -- so the only remaining explanation is a validation
     * error the debug layer is swallowing. Off unless the env var is set. */
    { static int _iq = -1;
      if (_iq < 0) { const char* e = getenv("D3D12_IQ"); _iq = e ? 1 : 0; }
      if (_iq && s_d3d.device) {
        ID3D12InfoQueue* iq = NULL;
        if (SUCCEEDED(s_d3d.device->lpVtbl->QueryInterface(
                s_d3d.device, &IID_ID3D12InfoQueue, (void**)&iq)) && iq) {
            UINT64 n = iq->lpVtbl->GetNumStoredMessages(iq);
            static int _printed = 0;
            for (UINT64 mi = 0; mi < n && _printed < 80; mi++) {
                SIZE_T len = 0;
                iq->lpVtbl->GetMessage(iq, mi, NULL, &len);
                D3D12_MESSAGE* m = (D3D12_MESSAGE*)malloc(len);
                if (m && SUCCEEDED(iq->lpVtbl->GetMessage(iq, mi, m, &len))) {
                    fprintf(stderr, "[D3D12-IQ][sev=%d id=%d] %s\n",
                            (int)m->Severity, (int)m->ID, m->pDescription);
                    _printed++;
                }
                free(m);
            }
            if (n) iq->lpVtbl->ClearStoredMessages(iq);
            iq->lpVtbl->Release(iq);
            fflush(stderr);
        }
      } }

    if (s_sc_dump_pending) {
        s_sc_dump_pending = 0;
        wait_for_gpu();
        s_dump_name = "screencopy";
        dump_backbuffer_bmp();
        s_dump_name = NULL;
    }
    if (dumping) {
        wait_for_gpu();            /* ensure the copy finished before mapping */
        dump_backbuffer_bmp();
        s_d3d.dump_frames_left--;
    }

    if (s_rtsave_state) {
        fprintf(stderr, "[RTT_SAVERT] writing rt_save.bmp %ux%u\n", s_rtsave_w, s_rtsave_h);
        if (!dumping) { double _g = perf_on() ? perf_now() : 0.0;
                        wait_for_gpu();
                        if (perf_on()) s_perf_gpu += perf_now() - _g; }
        void* mp = NULL; D3D12_RANGE rr = {0, (SIZE_T)s_rtsave_pitch * s_rtsave_h};
        if (SUCCEEDED(s_rtsave_buf->lpVtbl->Map(s_rtsave_buf, 0, &rr, &mp)) && mp) {
            FILE* f = fopen("rt_save.bmp", "wb");
            if (f) {
                u32 w = s_rtsave_w, h = s_rtsave_h;
                u32 rowb = (w * 3 + 3) & ~3u;
                u32 datasz = rowb * h;
                u8 hdr[54] = {0};
                hdr[0]='B'; hdr[1]='M';
                *(u32*)(hdr+2) = 54 + datasz; *(u32*)(hdr+10) = 54;
                *(u32*)(hdr+14) = 40; *(int*)(hdr+18) = (int)w; *(int*)(hdr+22) = (int)h;
                *(u16*)(hdr+26) = 1; *(u16*)(hdr+28) = 24; *(u32*)(hdr+34) = datasz;
                fwrite(hdr, 1, 54, f);
                u8* line = (u8*)malloc(rowb);
                for (int y = (int)h - 1; y >= 0; y--) {
                    const u8* srow = (const u8*)mp + (u64)y * s_rtsave_pitch;
                    memset(line, 0, rowb);
                    for (u32 x = 0; x < w; x++) {
                        float rv, gv, bv;
                        if (s_rtsave_dxgi == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                            const u16* hp16 = (const u16*)(srow + (u64)x * 8);
                            /* crude half->float: sign|exp|mant */
                            float v[3];
                            for (int c2 = 0; c2 < 3; c2++) {
                                u16 hv = hp16[c2];
                                u32 sign = (hv >> 15) & 1, exp = (hv >> 10) & 0x1F, man = hv & 0x3FF;
                                float fv;
                                if (exp == 0) fv = (float)man / 16777216.0f;
                                else { u32 fb = (sign << 31) | ((exp - 15 + 127) << 23) | (man << 13);
                                       memcpy(&fv, &fb, 4); }
                                v[c2] = fv;
                            }
                            rv = v[0]; gv = v[1]; bv = v[2];
                        } else if (s_rtsave_dxgi == DXGI_FORMAT_R32G32B32A32_FLOAT) {
                            const float* fp32 = (const float*)(srow + (u64)x * 16);
                            rv = fp32[0]; gv = fp32[1]; bv = fp32[2];
                        } else {
                            const u8* p8 = srow + (u64)x * 4;
                            rv = p8[0] / 255.0f; gv = p8[1] / 255.0f; bv = p8[2] / 255.0f;
                        }
                        /* RTT_SAVEA=1: show the alpha lane in RED (gates
                         * like wave's mask.w live there). */
                        if (getenv("RTT_SAVEA")) {
                            float av;
                            if (s_rtsave_dxgi == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                                const u16* hp16 = (const u16*)(srow + (u64)x * 8);
                                u16 hv = hp16[3];
                                u32 sign = (hv >> 15) & 1, exp = (hv >> 10) & 0x1F, man = hv & 0x3FF;
                                if (exp == 0) av = (float)man / 16777216.0f;
                                else { u32 fb = (sign << 31) | ((exp - 15 + 127) << 23) | (man << 13);
                                       memcpy(&av, &fb, 4); }
                            } else if (s_rtsave_dxgi == DXGI_FORMAT_R32G32B32A32_FLOAT) {
                                av = ((const float*)(srow + (u64)x * 16))[3];
                            } else {
                                av = (srow + (u64)x * 4)[3] / 255.0f;
                            }
                            rv = av;
                        }
                        /* |v| tonemap so signed heights are visible */
                        float ar = rv < 0 ? -rv : rv, ag = gv < 0 ? -gv : gv, ab = bv < 0 ? -bv : bv;
                        if (ar > 1) ar = 1; if (ag > 1) ag = 1; if (ab > 1) ab = 1;
                        line[x*3+0] = (u8)(ab * 255.0f);
                        line[x*3+1] = (u8)(ag * 255.0f);
                        line[x*3+2] = (u8)(ar * 255.0f);
                    }
                    fwrite(line, 1, rowb, f);
                }
                free(line);
                fclose(f);
                printf("[D3D12] wrote rt_save.bmp (%ux%u dxgi=%u)\n", w, h, s_rtsave_dxgi);
            }
            s_rtsave_buf->lpVtbl->Unmap(s_rtsave_buf, 0, NULL);
        }
        s_rtsave_state = 0;
    }

    /* rsxcap: snapshot whole frames (backbuffer + every offscreen RT + a draw
     * manifest) at RSX_CAP=start[:count[:stride]] frame_counts. Runs before the
     * present transition would matter -- the backbuffer is in PRESENT here and
     * is restored to PRESENT after each readback. */
    { const char* cap = getenv("RSX_CAP");
      if (cap) {
        static int s_cap_parsed = 0;
        static u32 s_cap_start = 0, s_cap_count = 1, s_cap_stride = 1, s_cap_done = 0;
        if (!s_cap_parsed) {
            s_cap_parsed = 1;
            u32 a = 0, b = 1, c = 1;
            int n = sscanf(cap, "%u:%u:%u", &a, &b, &c);
            s_cap_start  = (n >= 1 && a) ? a : 200;
            s_cap_count  = (n >= 2 && b) ? b : 1;
            s_cap_stride = (n >= 3 && c) ? c : 1;
        }
        if (s_cap_done < s_cap_count) {
            u32 target = s_cap_start + s_cap_done * s_cap_stride;
            if (s_d3d.frame_count >= target && s_dbg_last_draws > 0) {
                rsx_capture_frame(fi, s_dbg_last_draws, s_cap_done);
                s_cap_done++;
            }
        }
      } }

    { static int cap = -1, done = 0; static ULONGLONG last = 0;   /* winport-fix: WINPORT_CAP */
      if (cap < 0) { const char* e = getenv("WINPORT_CAP"); cap = e ? atoi(e) : 0; }
      static int capp = -1; if (capp < 0) { const char* e = getenv("WINPORT_CAP_PRESENTED"); capp = (e && e[0] == '1'); }   /* winport v59b-cappresented */
      static ULONGLONG capa = ~0ull, cap_t0 = 0; if (capa == ~0ull) { const char* e = getenv("WINPORT_CAP_AFTER_MS"); capa = (e && atoi(e) > 0) ? (ULONGLONG)atoi(e) : 0; cap_t0 = GetTickCount64(); }   /* winport v67b-capafter */
      if (cap && done < cap && s_dbg_last_draws >= 100 && (!capp || s_present_this_frame) && GetTickCount64() - cap_t0 >= capa) {
          ULONGLONG now = GetTickCount64();
          static ULONGLONG every = 0;   /* winport-fix: capture spacing */
          if (!every) { const char* e = getenv("WINPORT_CAP_EVERY_MS"); every = (e && atoi(e) > 0) ? (ULONGLONG)atoi(e) : 8000; }
          if (!last || now - last >= every) {
              rsx_capture_frame(fi, s_dbg_last_draws, 60u + (u32)done);
              done++; last = now;
          } } }
    /* winport v36: WINPORT_CAP_KEY=1 -> F9 captures the next frame (every draw + render target, like WINPORT_CAP) so a
     * player can grab the exact moment a visual bug shows. Up to 40 captures (each tens of MB) into RSX_CAP_DIR. */
    { static int on = -1, n = 0; static SHORT prev = 0;
      static int mx = 40;
      if (on < 0) { const char* e = getenv("WINPORT_CAP_KEY"); on = (e && e[0] == '1');
                    if ((e = getenv("WINPORT_CAP_KEY_MAX")) && atoi(e) > 0) mx = atoi(e); }
      if (on && n < mx) { SHORT k = GetAsyncKeyState(VK_F9);
          if ((k & 0x8000) && !(prev & 0x8000) && s_dbg_last_draws > 0) {
              rsx_capture_frame(fi, s_dbg_last_draws, 200u + (u32)n);
              fprintf(stderr, "[CAPKEY] F9 capture #%d (frame %llu, %u draws)%c", n + 1, (unsigned long long)s_d3d.frame_count, s_dbg_last_draws, 10);
              n++; MessageBeep(MB_OK); }
          prev = k; } }
    /* Present */
    if (s_present_this_frame)
    { if (wp58_cap()) { wp58_limit();
          if (wp58_nopresent() || IsIconic(s_d3d.hwnd)) {   /* winport v58b-iconic + winport v58d-nopresent */
              static int said; if (!said) { said = 1; fprintf(stderr, "[FPSCAP58] window minimized: frames rendered, not presented (no vblank pacing)\n"); }
              s_wp58_skipped = 1; }
          else wp82_present(0, s_wp58_tear ? DXGI_PRESENT_ALLOW_TEARING : 0, 1); }   /* winport v58-fpscap | winport v82c-fence-diag */
      else wp82_present(wp32_present_sync(), 0, 2); }   /* winport v82c-fence-diag */ /* vsync unless WINPORT_PRESENT_SYNC=0 (winport v32-present-sync) */   /* skipped for an offscreen-only batch */

    move_to_next_frame();

    s_d3d.frame_count++;

    /* RPCS3-style titlebar stats, refreshed once a second: presented FPS,
     * draw count of the last frame, and the backbuffer size. */
    {
        static ULONGLONG s_tt0 = 0;
        static u64 s_tframes = 0;
        s_tframes++;
        ULONGLONG tnow = GetTickCount64();
        if (s_tt0 == 0) s_tt0 = tnow;
        if (tnow - s_tt0 >= 1000 && s_d3d.hwnd) {
            extern char g_rsx_title_base[128];
            char tb[256];
            snprintf(tb, sizeof(tb), "%s | FPS: %.2f | draws: %u | %ux%u",
                     g_rsx_title_base,
                     s_tframes * 1000.0 / (double)(tnow - s_tt0),
                     s_dbg_last_draws, s_d3d.width, s_d3d.height);
            memcpy(s_wp_title, tb, sizeof s_wp_title); s_wp_title[sizeof s_wp_title - 1] = 0;   /* winport-fix: no cross-thread SetWindowTextA */
            PostMessageA(s_d3d.hwnd, WP_WM_SETTITLE, 0, 0);
            if (s_wp68_on) {   /* winport v68-res-scale */
                if (!wp_stats_on()) ; else printf("[RES68] 1s: scale=%.3g bb=%ux%u | RTs created %u (max %ux%u) seeds upscaled %u | viewports scaled %u blits %u composites %u screen copies %u subvp %u | ZSNAP binds %u orb copies %u | WPOS FPs %u | UNSCALED hits: guest_fb %u\n",
                       (double)s_wp68_s, WP68_BW, WP68_BH, s_wp68_n_rt, s_wp68_maxw, s_wp68_maxh, s_wp68_n_seed, s_wp68_n_vp,
                       s_wp68_n_blit, s_wp68_n_comp, s_wp68_n_sc, s_wp68_n_subvp, s_wp68_n_zs, s_wp40_ncopy - s_wp68_n_fbc,
                       s_wp68_n_wpos, s_wp68_n_gfb);
                s_wp68_n_fbc = s_wp40_ncopy;
                s_wp68_n_rt = s_wp68_n_seed = s_wp68_n_vp = s_wp68_n_blit = s_wp68_n_comp = s_wp68_n_sc = s_wp68_n_subvp = s_wp68_n_zs = s_wp68_n_gfb = 0;
            }
            s_tframes = 0;
            s_tt0 = tnow;
        }
    }
}

/* ---------------------------------------------------------------------------
 * RSX backend callbacks
 * -----------------------------------------------------------------------*/

static int d3d12_init(void* ud, u32 width, u32 height)
{
    (void)ud;
    printf("[D3D12] Backend init(%ux%u)\n", width, height);
    return 0;
}

static void d3d12_shutdown(void* ud)
{
    (void)ud;
    printf("[D3D12] Backend shutdown\n");
}

static void d3d12_begin_frame_wk85(void* ud);
static void d3d12_begin_frame(void* ud) { const unsigned long long t_ = WK85_T0(); d3d12_begin_frame_wk85(ud); WK85_ADD(WK85_beginf, t_); }   /* winport v85a-walkprof */
static void d3d12_begin_frame_wk85(void* ud)
{
    (void)ud;
}

static void d3d12_end_frame_wk85(void* ud);
static void d3d12_end_frame(void* ud) { const unsigned long long t_ = WK85_T0(); d3d12_end_frame_wk85(ud); WK85_ADD(WK85_endf, t_); }   /* winport v85a-walkprof */
static void d3d12_end_frame_wk85(void* ud)
{
    (void)ud;
}

static u32 s_dbg_clears_since_present = 0;   /* CELLMARK_BLINKDBG */
static u32 s_clear_presents = 0;   /* presents issued at clear (frame boundary) */

/* FRAME_BUDGET=1: how much geometry a guest frame actually asks for, versus
 * what the per-frame vertex buffer can hold. A frame that overflows is silently
 * truncated -- the tail of the scene never renders -- which reads as "the object
 * is missing" rather than "the batch is too small". */

static int blink_dbg(void)
{
    static int v = -1;
    if (v < 0) v = getenv("CELLMARK_BLINKDBG") ? 1 : 0;
    return v;
}

static void d3d12_present_wk85(void* ud, u32 buffer_id);
static void d3d12_present(void* ud, u32 buffer_id) { const unsigned long long t_ = WK85_T0(); d3d12_present_wk85(ud, buffer_id); WK85_ADD(WK85_present, t_); }   /* winport v85a-walkprof */
static void d3d12_present_wk85(void* ud, u32 buffer_id)
{
    (void)ud;
    (void)buffer_id;

    if (blink_dbg())
        printf("[PRESENT] draws=%u clears_since_last=%u\n",
               s_d3d.draw_count, s_dbg_clears_since_present);
    s_dbg_clears_since_present = 0;

    /* An accumulated batch with draws but NONE targeting a display buffer is
     * offscreen pass work only (demosaic flips once per effect pass): showing
     * it would strobe the bare backbuffer clear. Keep accumulating -- the
     * composite draw that targets the display presents the whole chain, in
     * order, in one command list. Empty batches still present (boot/idle). */
    static unsigned s_renders_at_flip = 0;   /* winport-fix: no empty re-render on flip (blink) */
    static int s_old_empty = -1;
    if (s_old_empty < 0) s_old_empty = getenv("WINPORT_EMPTY_FLIP_RENDER") ? 1 : 0;
    int flip_has_display = (s_d3d.draw_count == 0) &&
                           (s_old_empty || s_winport_renders == s_renders_at_flip);
    for (u32 _i = 0; _i < s_d3d.draw_count && _i < MAX_DRAWS; _i++)
        if (!s_d3d.draws[_i].is_clear && s_d3d.draws[_i].rt_off == 0) {
            flip_has_display = 1;
            break;
        }

    if (s_d3d.initialized && flip_has_display)
        render_frame();
    s_renders_at_flip = s_winport_renders;

    /* FPS tracking */
    ULONGLONG now = GetTickCount64();
    if (now - s_d3d.last_fps_time >= 1000) {
        s_d3d.fps = (u32)s_d3d.frame_count; /* rough estimate */
        s_d3d.last_fps_time = now;
        s_d3d.frame_count = 0;
    }
}

static void render_frame(void);
static void wp_room_for_wk85(u32 verts, u32 recs);
static void wp_room_for(u32 verts, u32 recs) { const unsigned long long t_ = WK85_T0(); wp_room_for_wk85(verts, recs); WK85_ADD(WK85_room, t_); }   /* winport v85a-walkprof */
static void wp_room_for_wk85(u32 verts, u32 recs)   /* winport-fix: flush a full batch instead of dropping draws */
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("WINPORT_BATCH_FLUSH"); on = (e && *e == '0') ? 0 : 1; }
    if (!on || !s_d3d.initialized || !s_d3d.draw_count) return;
    const u32 used_v = (u32)(s_d3d.vp_vb_offset / 256u);   /* VP_VERT_STRIDE, defined further down */
    if (s_d3d.draw_count + recs + 8u < MAX_DRAWS && used_v + verts + 64u < MAX_VERTICES) return;
    { static unsigned n = 0; if (n++ < 20) fprintf(stderr, "[WPFLUSH] batch full (records=%u vertices=%u, next op needs %u) -> executing it now\n", s_d3d.draw_count, used_v, verts); }
    s_present_this_frame = 0;
    render_frame();
    s_present_this_frame = 1;
    s_wp_cont = 1; s_wp_cont_rt = s_wp_last_rt;
}
static int wp_clear_flags_on(void)   /* winport-fix: honour RSX clear flags */
{ static int c = -1; if (c < 0) { const char* e = getenv("WINPORT_CLEAR_FLAGS"); c = (e && *e == '0') ? 0 : 1; } return c; }
static void d3d12_clear_wk85(void* ud, u32 flags, u32 color, float depth, u8 stencil);
static void d3d12_clear(void* ud, u32 flags, u32 color, float depth, u8 stencil) { const unsigned long long t_ = WK85_T0(); d3d12_clear_wk85(ud, flags, color, depth, stencil); WK85_ADD(WK85_clear, t_); }   /* winport v85a-walkprof */
static void d3d12_clear_wk85(void* ud, u32 flags, u32 color, float depth, u8 stencil)
{
    (void)ud;
    (void)flags;
    (void)depth;
    (void)stencil;

    /* Convert RSX ARGB u32 to float[4] RGBA */
    float cc[4];
    cc[0] = ((color >> 16) & 0xFF) / 255.0f; /* R */
    cc[1] = ((color >> 8) & 0xFF) / 255.0f;  /* G */
    cc[2] = (color & 0xFF) / 255.0f;          /* B */
    cc[3] = ((color >> 24) & 0xFF) / 255.0f;  /* A */
    /* CLEAR_RGB=r,g,b: override the guest clear colour. Black holes (geometry
     * that never rasterized) and black pixels a shader really wrote look
     * identical against a black clear; this separates them in one run. */
    { static int _cinit = 0; static float _co[3]; static int _con = 0;
      if (!_cinit) { _cinit = 1; const char* e = getenv("CLEAR_RGB");
          if (e && sscanf(e, "%f,%f,%f", &_co[0], &_co[1], &_co[2]) == 3) _con = 1; }
      if (_con) { cc[0] = _co[0]; cc[1] = _co[1]; cc[2] = _co[2]; } }

    u32 rt_w = 0, rt_h = 0, mrt[3] = {0, 0, 0};
    u32 rt = current_rt_off(&rt_w, &rt_h, mrt);

    /* An OFFSCREEN clear is just an ordered op in the current frame's pass
     * chain (demosaic clears each effect pass's surface) -- record it, don't
     * touch the frame boundary. */
    if (rt != 0) {
        if (wp_clear_flags_on() && !(flags & 0xF3u)) return;   /* winport-fix: honour RSX clear flags */
        wp_room_for(0, 1);   /* winport-fix: flush a full batch instead of dropping draws */
        if (s_d3d.draw_count < MAX_DRAWS) {
            D3D12DrawRecord* dr = &s_d3d.draws[s_d3d.draw_count++];
            memset(dr, 0, sizeof(*dr));
            dr->is_vp = 1; dr->is_clear = 1; dr->tex_slot = -1;
            dr->rt_off = rt; memcpy(dr->rt_mrt, mrt, sizeof(mrt));
            dr->rt_w = rt_w; dr->rt_h = rt_h;
            dr->rt_fmt = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->surface_format : 0; dr->zeta_off = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->surface_zeta_offset : 0; dr->zeta_pitch = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->surface_zeta_pitch : 0;
            memcpy(dr->cc, cc, sizeof(cc));
            dr->clear_flags = wp_clear_flags_on() ? flags : 0xF0u;   /* winport-fix: honour RSX clear flags */
            dr->clear_z = depth; dr->clear_s = stencil;
            { static int n41_ = 0; if (n41_ < 12 && (flags & 3u)) { n41_++;   /* winport v41: what the game clears depth/stencil to */
                fprintf(stderr, "[CLEARZS] rt=0x%08X zeta=0x%08X flags=0x%02X z=%.3f stencil=%u (zstencil_clear_value=0x%08X)\n", rt,
                        dr->zeta_off, flags, depth, (unsigned)stencil, s_d3d.current_rsx_state ? s_d3d.current_rsx_state->zstencil_clear_value : 0u); } }
            if (s_d3d.current_rsx_state && ((s_d3d.current_rsx_state->surface_format >> 5) & 7u) == 1u)   /* Z16 */
                dr->clear_z = (float)(s_d3d.current_rsx_state->zstencil_clear_value & 0xFFFFu) / 65535.0f;
            { static int n_ = 0; if (n_ < 24 && getenv("WINPORT_CLEARDBG")) { n_++;
                fprintf(stderr, "[CLEAR] rt=0x%08X flags=0x%02X color=0x%08X z=%.3f s=%u\n", rt, flags, color, depth, (unsigned)stencil); } }
        }
        return;
    }

    wp94_clear_note(flags);   /* winport v94a-ffhaze */
    if (!wp_clear_flags_on() || (flags & 0xF0u))   /* winport-fix: honour RSX clear flags */
    memcpy(s_d3d.clear_color, cc, sizeof(cc));

    /* A DISPLAY clear marks the start of a new visible frame. If a completed
     * frame is still accumulated (the drain gulped across a frame boundary --
     * guaranteed at the FIFO ring wrap, where rest-of-frame-N + clear-N+1
     * arrive in one batch), PRESENT it now instead of discarding it. Only a
     * batch that actually contains DISPLAY draws is a completed frame: a
     * batch of offscreen pass work (render-to-texture) must keep accumulating
     * until its composite draw arrives, or the screen strobes intermediates. */
    int have_display_draws = 0;
    for (u32 i = 0; i < s_d3d.draw_count && i < MAX_DRAWS; i++)
        if (!s_d3d.draws[i].is_clear && s_d3d.draws[i].rt_off == 0) {
            have_display_draws = 1;
            break;
        }
    if (!have_display_draws)
        return;   /* keep accumulating the in-progress frame */

    /* A title that DOUBLE-BUFFERS the display (DeferredShading clears both
     * 0x0 and 0x440000 per frame, plus a HUD pass) issues several display
     * clears per real frame -- treating each as a boundary presented the
     * 1-2-draw intermediates, strobing black between the full 146-draw
     * frames. Anchor the boundary to the FLIP instead: only present once a
     * cellGcmSetFlip has landed since the last present. Titles that flip once
     * per frame (wave/cellmark/gcmcube) are unaffected -- their single
     * display clear still follows their single flip. Fall back to the old
     * clear-only heuristic for titles that never flip (fc stays 0). */
    static u32 s_frame_draws_max = 0;   /* running max frame size (typical full frame) */
    if (s_d3d.draw_count > s_frame_draws_max) s_frame_draws_max = s_d3d.draw_count;
    {
        extern unsigned cellGcm_flip_request_count(void);
        static unsigned s_last_present_flip = 0;
        unsigned fc = cellGcm_flip_request_count();
        if (fc != 0) {
            if (fc == s_last_present_flip)
                return;   /* no flip since last present -> not a real boundary */
            /* A double-buffered title issues several display clears per flip
             * (DeferredShading: clear back-buffer, HUD pass, ...). They can
             * arrive in either order, so a small batch here may be an
             * intermediate that precedes the full frame's clear. Don't present
             * (or consume the flip) until the batch is a substantial fraction
             * of a full frame -- keep accumulating so the complete frame lands
             * in one present. Gauged against the running MAX frame size (not
             * the last, which a leaked tiny batch would poison), so it
             * self-scales per title with no fixed threshold. */
            if (s_frame_draws_max > 16 && s_d3d.draw_count < s_frame_draws_max / 4)
                return;   /* intermediate: keep accumulating, flip stays pending */
            s_last_present_flip = fc;
        }
    }

    /* RSX_ACCUM_FRAME=1: never present at a clear boundary, so every draw in a
     * guest frame accumulates into one image. A title that clears several times
     * per frame (render-to-texture passes) otherwise gets presented mid-scene,
     * and each captured frame holds only a slice of the geometry -- the floor in
     * one, a wall in the next -- which makes a single model impossible to see. */
    { static int accum = -1;
      if (accum < 0) { const char* e = getenv("RSX_ACCUM_FRAME"); accum = e ? atoi(e) : 0; }
      if (accum) return; }
    if (s_d3d.initialized) {
        if (blink_dbg())
            printf("[CLEAR] presenting %u accumulated draws at frame boundary\n",
                   s_d3d.draw_count);
        render_frame();
        s_clear_presents++;
    }
    s_dbg_clears_since_present++;
    wp85_ib_reset();   /* winport v85e-idxdraw */
    s_d3d.draw_count   = 0;
    s_d3d.vb_offset    = 0;
    s_d3d.vp_vb_offset = 0;
}

static void d3d12_set_render_target_wk85(void* ud, const rsx_state* state);
static void d3d12_set_render_target(void* ud, const rsx_state* state) { const unsigned long long t_ = WK85_T0(); d3d12_set_render_target_wk85(ud, state); WK85_ADD(WK85_rt, t_); }   /* winport v85a-walkprof */
static void d3d12_set_render_target_wk85(void* ud, const rsx_state* state)
{
    (void)ud;
    s_d3d.current_rsx_state = state;
    /* Log only the first few; set_render_target is called every frame and
     * floods the log otherwise. */
    static int s_count = 0;
    if (s_count < 5) {
        printf("[D3D12] set_render_target(%ux%u)\n",
               state->surface_clip_w, state->surface_clip_h);
        s_count++;
    }
}

static void d3d12_set_viewport_wk85(void* ud, const rsx_state* state);
static void d3d12_set_viewport(void* ud, const rsx_state* state) { const unsigned long long t_ = WK85_T0(); d3d12_set_viewport_wk85(ud, state); WK85_ADD(WK85_vp, t_); }   /* winport v85a-walkprof */
static void d3d12_set_viewport_wk85(void* ud, const rsx_state* state)
{
    (void)ud;
    /* TODO: update D3D12 viewport from RSX state */
    (void)state;
}

/* Our host vertex layout for the fallback path: position (xyz) + color (rgba)
 * + texcoord (uv), 36 bytes. (The real VP path feeds raw float4 attrib0.) */
typedef struct { float x, y, z; float r, g, b, a; float u, v; } BasicVertex;


/* Read one RSX vertex (by absolute vertex index) from guest memory into our
 * host layout. Position is attrib 0 (float3+), color is attrib 3 (ubyte4 or
 * float4); missing attribs default to opaque white. RSX stores each 32-bit
 * component big-endian, so every lane is byte-swapped. */
static void read_rsx_vertex(const rsx_state* state, u32 vindex, BasicVertex* out)
{
    extern uint8_t* vm_base;
    extern u32 cellGcmResolveOffset(u32);

    out->x = out->y = out->z = 0.0f;
    out->r = out->g = out->b = out->a = 1.0f;
    out->u = out->v = 0.0f;
    if (!state || !vm_base) return;

    const rsx_vertex_attrib* pos = &state->vertex_attribs[0];
    if (pos->enabled && pos->type == 2 /* float */ && pos->size >= 2) {
        u8* src = vm_base + cellGcmResolveOffset(pos->offset + vindex * pos->stride);
        out->x = rsx_rd_bef(src);
        out->y = rsx_rd_bef(src + 4);
        if (pos->size >= 3) out->z = rsx_rd_bef(src + 8);

        /* dbgfont (and similar 2D overlays) store screen positions biased far
         * outside clip space; their vertex program folds them back to clip
         * space using transform constants we don't execute. When a position is
         * well outside NDC, recover the fractional part as a normalized [0,1]
         * screen coord and map it to NDC (screen Y-down -> clip Y-up). In this
         * layout the attribute is [posX, posY, U, V] (size 4), so the last two
         * components are the atlas texcoords, not depth.
         * TODO: execute the real vertex-program / RSX viewport transform so
         * this is not needed. */
        if (out->x > 2.0f || out->x < -2.0f || out->y > 2.0f || out->y < -2.0f) {
            float sx = out->x - floorf(out->x);
            float sy = out->y - floorf(out->y);
            out->x = sx * 2.0f - 1.0f;
            out->y = 1.0f - sy * 2.0f;
            out->z = 0.0f;
            if (pos->size >= 4) {
                out->u = rsx_rd_bef(src + 8);   /* U */
                out->v = rsx_rd_bef(src + 12);  /* V */
            }
        }
    }

    const rsx_vertex_attrib* col = &state->vertex_attribs[3];
    if (col->enabled && col->size >= 3) {
        u8* src = vm_base + cellGcmResolveOffset(col->offset + vindex * col->stride);
        if (col->type == 4 /* ubyte */) {
            out->r = src[0] / 255.0f;
            out->g = src[1] / 255.0f;
            out->b = src[2] / 255.0f;
            out->a = (col->size >= 4) ? src[3] / 255.0f : 1.0f;
        } else if (col->type == 2 /* float */) {
            u32 fr, fg, fb, fa;
            memcpy(&fr, src,     4); fr = ((fr>>24)&0xFF)|((fr>>8)&0xFF00)|((fr<<8)&0xFF0000)|((fr<<24)&0xFF000000);
            memcpy(&fg, src + 4, 4); fg = ((fg>>24)&0xFF)|((fg>>8)&0xFF00)|((fg<<8)&0xFF0000)|((fg<<24)&0xFF000000);
            memcpy(&fb, src + 8, 4); fb = ((fb>>24)&0xFF)|((fb>>8)&0xFF00)|((fb<<8)&0xFF0000)|((fb<<24)&0xFF000000);
            memcpy(&out->r, &fr, 4); memcpy(&out->g, &fg, 4); memcpy(&out->b, &fb, 4);
            if (col->size >= 4) {
                memcpy(&fa, src + 12, 4); fa = ((fa>>24)&0xFF)|((fa>>8)&0xFF00)|((fa<<8)&0xFF0000)|((fa<<24)&0xFF000000);
                memcpy(&out->a, &fa, 4);
            }
        }
    }
}

/* Upload `count` sequential vertices [first, first+count). Returns the count
 * actually written (clamped to the remaining per-frame buffer). */
static u32 upload_vertices_from_rsx(u32 first, u32 count)
{
    BasicVertex* verts = (BasicVertex*)((u8*)s_d3d.vb_mapped + s_d3d.vb_offset);
    u32 max_verts = (MAX_VERTICES * VERTEX_STRIDE - s_d3d.vb_offset) / sizeof(BasicVertex);
    if (count > max_verts) count = max_verts;
    const rsx_state* state = s_d3d.current_rsx_state;
    for (u32 i = 0; i < count; i++)
        read_rsx_vertex(state, first + i, &verts[i]);
    s_d3d.vb_offset += count * sizeof(BasicVertex);
    return count;
}

/* Upload RSX QUADS (prim 8) as a triangle list: each 4-vertex quad v0..v3
 * (perimeter winding) splits into triangles (v0,v1,v2) and (v0,v2,v3).
 * D3D12 has no quad topology, so this expansion is how quads render at all.
 * Returns the number of triangle-list vertices emitted (6 per quad). */
static u32 upload_quads_from_rsx(u32 first, u32 count)
{
    const rsx_state* state = s_d3d.current_rsx_state;
    u32 quads = count / 4;
    u32 max_verts = (MAX_VERTICES * VERTEX_STRIDE - s_d3d.vb_offset) / sizeof(BasicVertex);
    if (quads * 6 > max_verts) quads = max_verts / 6;
    BasicVertex* verts = (BasicVertex*)((u8*)s_d3d.vb_mapped + s_d3d.vb_offset);
    u32 o = 0;
    for (u32 q = 0; q < quads; q++) {
        BasicVertex c[4];
        for (u32 k = 0; k < 4; k++)
            read_rsx_vertex(state, first + q * 4 + k, &c[k]);
        verts[o++] = c[0]; verts[o++] = c[1]; verts[o++] = c[2];
        verts[o++] = c[0]; verts[o++] = c[2]; verts[o++] = c[3];
    }
    s_d3d.vb_offset += o * sizeof(BasicVertex);
    return o;
}

/* Upload RSX QUADS as raw float4 attrib0 (byte-swapped) into vp_vb, expanded
 * to a triangle list (6 verts/quad). The decompiled vertex shader does the
 * transform. Returns emitted vertex count. */
/* Generic VP vertex: all 16 RSX vertex attributes, each converted to a float4
 * slot -- 256 bytes/vertex, input layout ATTRi @ i*16. Apps place attributes at
 * arbitrary indices (tiny3d: pos=a0 colour=a3 tex=a8; SDK gcm samples: pos=a0
 * colour=a1 tex=a2; dbgfont: a0..a2), so a hardcoded pos+colour pair can't
 * cover them: every enabled attrib is fetched from guest memory and converted
 * by its RSX type; disabled slots read (0,0,0,1). */
typedef struct { float v[4]; } VPSlot;
#define VP_VERT_STRIDE (16 * sizeof(VPSlot))   /* 256 */


void rsx_vtx_pos_dbg(const rsx_state* state, const float* v, u32 n);

/* Fill all 16 attribute slots for one vertex. The per-attribute work lives in
 * rsx_vertex_fetch.c so the Metal and null backends read guest vertices the
 * same way this one does, rather than each porting the logic again. */
static void read_vp_vertex(const rsx_state* state, u32 vi, VPSlot* out16)
{
    for (int i = 0; i < 16; i++) {
        rsx_fetch_attrib(state, i, vi, out16[i].v);
        if (i == 0) {
            const rsx_vertex_attrib* a = &state->vertex_attribs[0];
            u32 n = a->size ? a->size : 4; if (n > 4) n = 4;
            rsx_vtx_pos_dbg(state, out16[0].v, n);
        }
    }
}

/* winport-fix: fast vertex fetch */
typedef struct { int n; struct { u32 stride, type, size, idx; const u8* base; } a[16]; float cst[16][4]; int en; } VPFetch;   /* winport v25: en = enabled attrs with stride!=0 (perf only) */
/* winport v25: [VTXSLOW] -- FIRST reason a draw missed the fast path (PERF=1 only). */
static void vpf_vp_stats(const rsx_state* st, u32 enmask)   /* how many of the enabled attrs does the bound VP actually read */
{
    extern int rsx_vp_analyze_inputs(const u8*, u32, void*);
    struct { u32 input_mask; int exact; } an; static struct { u32 h, m; } cache[64];
    if (!st || (st->transform_program_words * 4u) < 16) return;
    u32 vstart = st->transform_program_start * 16u, wb = st->transform_program_words * 4u; if (vstart >= wb) vstart = 0;
    const u8* uc = (const u8*)st->transform_program + vstart; u32 len = wb - vstart;
    u32 h = (wp85_vs_on() ? wp85_memo_hash(&s85_stm, uc, len < 4096 ? len : 4096)
                         : vp_hash_ucode(uc, len < 4096 ? len : 4096)) | 1u, m = 0xFFFFu, k = h & 63;   /* winport v85c-walkfast */
    if (cache[k].h == h) m = cache[k].m;
    else { if (rsx_vp_analyze_inputs(uc, len, &an) >= 0 && an.exact) m = an.input_mask & 0xFFFFu; cache[k].h = h; cache[k].m = m; }
    s_vs_vpread += (u64)__builtin_popcount(m & enmask); s_vs_vpen += (u64)__builtin_popcount(enmask);
}
typedef struct { const u8* p; int is16; } VPIdx;
static int wp_vfetch_on(void)
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_FASTFETCH"); v = e ? (atoi(e) != 0) : 1; } return v; }
/* winport v26: CMP (RSX vertex type 6) = one big-endian u32 packing signed normalized x:11 | y:11 | z:10 bits (low to
 * high), w = 1 -- the decode RPCS3 does in its vertex shader. Before v26 type 6 was rejected by the fast path and then
 * read by rsx_fetch_attrib's default case as a float32, i.e. every CMP attribute (likely normals) was garbage.
 * WINPORT_VTX_CMP=1 enables the decode (default 0 = old behaviour) on both paths; WINPORT_VTX_CMPCHK=1 logs the mean
 * |xyz| of the first 200k decoded vertices once (~1.0 for unit normals). */
int wp_vtx_cmp_on(void)
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_VTX_CMP"); v = e ? (atoi(e) != 0) : 0; } return v; }
void wp_vtx_cmp_decode(const u8* p, float o[4])
{
    u32 w; memcpy(&w, p, 4); w = __builtin_bswap32(w);
    o[0] = (float)(((s32)(w << 21)) >> 21) / 1023.0f;
    o[1] = (float)(((s32)(w << 10)) >> 21) / 1023.0f;
    o[2] = (float)(((s32)w) >> 22) / 511.0f;
    o[3] = 1.0f;
    static int chk = -1; static u32 nchk; static double acc;
    if (chk < 0) chk = getenv("WINPORT_VTX_CMPCHK") ? 1 : 0;
    if (chk && nchk < 200000u) {
        acc += sqrt((double)o[0] * o[0] + (double)o[1] * o[1] + (double)o[2] * o[2]);
        if (++nchk == 200000u) fprintf(stderr, "[VTXCMP] mean |xyz| of %u decoded CMP vertices = %.3f (unit normals ~1.0)\n", nchk, acc / nchk);
    }
}
static int vpf_prepare_wk85(const rsx_state* st, VPFetch* f, u32 max_vi);
static int vpf_prepare(const rsx_state* st, VPFetch* f, u32 max_vi) { const unsigned long long t_ = WK85_T0(); int r_ = vpf_prepare_wk85(st, f, max_vi); WK85_ADD(WK85_vpf, t_); return r_; }   /* winport v85a-walkprof */
static int vpf_prepare_wk85(const rsx_state* st, VPFetch* f, u32 max_vi)
{
    extern uint8_t* vm_base;
    extern u32 cellGcmResolveLocated(int local, u32 offset);
    f->n = 0; f->en = 0;
    int pf_ = st ? perf_on() : 0;   /* winport v25: count the FIRST miss reason per draw */
    if (pf_) { u32 em = 0, s0 = 0, sz = 0; for (int i = 0; i < 16; i++) { const rsx_vertex_attrib* a = &st->vertex_attribs[i];
                 if (!a->enabled) continue; em |= 1u << i; if (a->stride == 0) s0 = 1; else f->en++; if (a->size > 4) sz = 1; }
               s_vs_stride0 += s0; s_vs_size += sz; vpf_vp_stats(st, em); }
    { static int vf = -1; if (vf < 0) vf = getenv("WINPORT_VTXFMT") ? 1 : 0;   /* winport v38: vertex layouts per VP (type 6 = CMP) */
      if (vf && st) { static u32 seen[96]; static int ns = 0; u32 h = 2166136261u ^ st->transform_program_start;
        for (int i = 0; i < 16; i++) { const rsx_vertex_attrib* a = &st->vertex_attribs[i];
            if (a->enabled) h = (h ^ ((u32)i << 24 | a->type << 16 | a->size << 8 | (a->stride & 0xFF))) * 16777619u; }
        int f_ = 0; for (int q = 0; q < ns; q++) if (seen[q] == h) f_ = 1;
        if (!f_ && ns < 96) { seen[ns++] = h; char b_[512]; int o_ = 0;
            for (int i = 0; i < 16; i++) { const rsx_vertex_attrib* a = &st->vertex_attribs[i];
                if (a->enabled && o_ < 480) o_ += snprintf(b_ + o_, sizeof b_ - o_, " a%d:t%u/s%u/str%u", i, a->type, a->size, a->stride); }
            b_[o_] = 0;
            fprintf(stderr, "[VTXFMT] vpstart=%u fp=0x%08X%s\n", st->transform_program_start, st->shader_program, b_);
            for (int i = 0; i < 16 && vm_base; i++) { const rsx_vertex_attrib* a = &st->vertex_attribs[i];   /* winport v39: raw CMP words */
                if (!a->enabled || a->type != 6 || !a->stride) continue;
                u32 ro_ = cellGcmResolveLocated((a->offset & 0x80000000u) ? 0 : 1, a->offset & 0x7FFFFFFFu);
                if (ro_ == 0xFFFFFFFFu) continue;
                for (u32 v_ = 0; v_ < 4; v_++) { const u8* p_ = vm_base + ro_ + v_ * a->stride; float d_[4];
                    u32 w_ = ((u32)p_[0] << 24) | ((u32)p_[1] << 16) | ((u32)p_[2] << 8) | p_[3];
                    wp_vtx_cmp_decode(p_, d_);
                    fprintf(stderr, "[VTXFMT]   a%d v%u raw=0x%08X -> (%.3f %.3f %.3f) |n|=%.3f\n", i, v_, w_, d_[0], d_[1], d_[2],
                            sqrt((double)d_[0]*d_[0] + (double)d_[1]*d_[1] + (double)d_[2]*d_[2])); } } } } }
    if (!wp_vfetch_on() || !vm_base || !st) { if (pf_) { s_vs_slow++; if (!wp_vfetch_on()) s_vs_disabled++; else s_vs_other++; } return 0; }
    for (int i = 0; i < 16; i++) {
        memcpy(f->cst[i], st->vertex_data4f[i], 16);
        const rsx_vertex_attrib* a = &st->vertex_attribs[i];
        if (!a->enabled || a->stride == 0) continue;
        if (!(a->type == 1 || a->type == 2 || a->type == 3 || a->type == 4 || a->type == 5 || a->type == 7 || (a->type == 6 && wp_vtx_cmp_on()))) {   /* winport v26: CMP */ if (pf_) { s_vs_slow++; s_vs_type[a->type & 7]++; } return 0; }
        u32 o = a->offset & 0x7FFFFFFFu;
        int loc = (a->offset & 0x80000000u) ? 0 : 1;
        u32 r0 = cellGcmResolveLocated(loc, o);
        for (u32 q = 1; q <= 4; q++) {
            u32 vq = (u32)(((u64)max_vi * q) / 4u);
            if (cellGcmResolveLocated(loc, o + vq * a->stride) - r0 != vq * a->stride) { if (pf_) { s_vs_slow++; s_vs_nonlin++; } return 0; }
        }
        u32 n = a->size ? a->size : 4; if (n > 4) n = 4;
        f->a[f->n].stride = a->stride; f->a[f->n].type = a->type; f->a[f->n].size = n;
        f->a[f->n].idx = (u32)i; f->a[f->n].base = vm_base + r0; f->n++;
    }
    /* winport v41: WINPORT_VTXCHK=1 -- sample up to 64 vertices of this draw: input positions (attr 0, float32) that
     * are exactly (0,0,0) or non-finite/huge, and CMP attributes holding 0x7FDFFBFF (= (1,1,1), a placeholder normal,
     * not a unit vector). Copied into the draw record (manifest line) and logged. */
    s_wp_vchk_zero = s_wp_vchk_cmp = 0; s_wp_vchk_maxvi = max_vi;
    { static int vc = -1; if (vc < 0) vc = getenv("WINPORT_VTXCHK") ? 1 : 0;
      if (vc) {
        for (int k = 0; k < f->n; k++) {
            const u8* b_ = f->a[k].base; u32 s_ = f->a[k].stride;
            for (u32 q = 0; q < 64; q++) {
                u32 vi = (u32)(((u64)max_vi * q) / 63u);
                const u8* p = b_ + (u64)vi * s_;
                if (f->a[k].idx == 0 && f->a[k].type == 2 && f->a[k].size >= 3) {
                    float x_[3]; for (int c = 0; c < 3; c++) { u32 w; memcpy(&w, p + c * 4, 4); w = __builtin_bswap32(w); memcpy(&x_[c], &w, 4); }
                    int bad = (x_[0] == 0.0f && x_[1] == 0.0f && x_[2] == 0.0f);
                    for (int c = 0; c < 3; c++) if (!(x_[c] == x_[c]) || x_[c] > 1e7f || x_[c] < -1e7f) bad = 1;
                    s_wp_vchk_zero += (u32)bad;
                }
                if (f->a[k].type == 6) { u32 w = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
                    s_wp_vchk_cmp += (w == 0x7FDFFBFFu); }
            }
        }
        if (s_wp_vchk_zero) { static unsigned long n = 0; ++n;
            if (n <= 16 || (n & (n - 1)) == 0)
                fprintf(stderr, "[VTXCHK] #%lu draw fp=0x%08X: %u of 64 sampled positions are (0,0,0)/non-finite (max index %u, a0 off=0x%08X stride %u)\n",
                        n, st->shader_program, s_wp_vchk_zero, max_vi, st->vertex_attribs[0].offset, st->vertex_attribs[0].stride); }
      } }
    return 1;
}
static inline float wp62_half_be(const u8* p)   /* winport v62-half-inline: exact copy of rsx_rd_half_be (rsx_vertex_fetch.c) */
{
    u16 h = (u16)((p[0] << 8) | p[1]);
    u32 sgn = (h >> 15) & 1u, ex = (h >> 10) & 0x1Fu, man = h & 0x3FFu, f;
    if (ex == 0)        f = sgn << 31;
    else if (ex == 31)  f = (sgn << 31) | 0x7F800000u | (man << 13);
    else                f = (sgn << 31) | ((ex - 15u + 127u) << 23) | (man << 13);
    float o; memcpy(&o, &f, 4); return o;
}
static void vpf_fetch(const VPFetch* f, u32 vi, VPSlot* out16)
{
    float t[16][4];
    memcpy(t, f->cst, sizeof t);
    for (int k = 0; k < f->n; k++) {
        const u8* p = f->a[k].base + (u64)vi * f->a[k].stride;
        float* o = t[f->a[k].idx]; u32 n = f->a[k].size;
        switch (f->a[k].type) {
        case 2: for (u32 c = 0; c < n; c++) { u32 w; memcpy(&w, p + c * 4, 4); w = __builtin_bswap32(w); memcpy(&o[c], &w, 4); } break;
        case 3: for (u32 c = 0; c < n; c++) o[c] = wp62_half_be(p + c * 2); break;   /* winport v62-half-inline */
        case 4: for (u32 c = 0; c < n; c++) o[c] = p[c] / 255.0f; break;
        case 1: for (u32 c = 0; c < n; c++) { s16 sv = (s16)((p[c*2] << 8) | p[c*2+1]); o[c] = (float)sv / 32767.0f; } break;
        case 5: for (u32 c = 0; c < n; c++) o[c] = (float)(s16)((p[c*2] << 8) | p[c*2+1]); break;
        case 7: for (u32 c = 0; c < n; c++) o[c] = (float)p[c]; break;
        case 6: wp_vtx_cmp_decode(p, o); break;   /* winport v26: CMP */
        }
    }
    memcpy(out16, t, sizeof t);
}
static void vpi_prepare(const rsx_state* st, VPIdx* x)
{
    extern uint8_t* vm_base;
    extern u32 cellGcmResolveLocated(int local, u32 offset);
    u32 dma = (st->index_location | ((u32)st->index_is_16bit << 4));
    x->p = vm_base + cellGcmResolveLocated(((dma & 0xF) == 0), st->index_array_addr);
    x->is16 = (((dma >> 4) & 0xF) == 1);
}
static inline u32 vpi_get(const VPIdx* x, u32 k)
{
    if (x->is16) { const u8* p = x->p + (u64)k * 2; return ((u32)p[0] << 8) | p[1]; }
    const u8* p = x->p + (u64)k * 4;
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
/* v24: WINPORT_VTXBENCH=1 re-runs each draw's vertex fetch into a small CACHED scratch buffer and times it: the gap between
 * that and the real "vtx" time is the cost of writing the 256-byte vertices into the (write-combined) GPU upload heap. */
static int wp_vbench_on(void)
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_VTXBENCH"); v = (e && *e && *e != '0') ? 1 : 0; } return v; }
/* winport v27: parallel vertex conversion (WINPORT_VTX_THREADS=N, default 0 = off = single-threaded as before).
 * v26 profile: the RSX thread converts every vertex on ONE core while it holds the PPU giant lock, and the main PPU
 * thread waits for it (lock wait ~20%, or ring/flip waits with PS3_GCM_FIFO_NOLOCK). The PC has idle cores. Fast-path
 * draws with >= WINPORT_VTX_PAR_MIN vertices (default 256) are cut into chunks of disjoint output ranges; N-1 pool
 * threads + the caller convert them, the caller returns only when all chunks are done, so the result is byte-
 * identical. Workers spin ~WINPORT_VTX_SPIN_US (default 50 us since v29) for the next job before sleeping, because one draw
 * is only ~45 us of work and a cold thread wake costs 5-20 us. Guest memory is only read. */
typedef struct { const VPFetch* f; const VPIdx* x; u32 first, count, chunk, nchunks; VPSlot* out; volatile LONG done; } WpVJob;
/* v28 race fix (Seth's v27 crash in wpv_run_chunks): a worker that lost the last claim of draw N could read draw N+1's
 * half-written job fields, miscount `done`, let the caller return early and then touch a dead VPFetch. Now ONE 64-bit
 * atomic carries gen:24 | nchunks:20 | next:20, so a claim is valid only against the job it was made for, and job
 * fields live in two slots (gen & 1); a slot is rewritten only after every valid claim on it has completed. */
static WpVJob s_wpv_jobs[2];
static volatile LONG64 s_wpv_claim;
static volatile LONG s_wpv_gen;
static HANDLE s_wpv_sem;
/* winport v55-vtxwait: WINPORT_VTX_WAIT=1 -> WaitOnAddress/WakeByAddressAll on s_wpv_gen (fix_v55_vtxwait_renderer.py) */
typedef BOOL (WINAPI *WpWoaFn)(volatile VOID*, PVOID, SIZE_T, DWORD);
typedef void (WINAPI *WpWbaFn)(PVOID);
static WpWoaFn s_wpv_woa; static WpWbaFn s_wpv_wba;
static int s_wpv_n = -1, s_wpv_min = 256, s_wpv_spin_us = 50;   /* v29: 300 -> 50 us (v27 combat: 3 workers at 100% CPU, may share a core with the main thread) */
static void wpv_run_chunks(void)
{
    for (;;) {
        LONG64 w = InterlockedIncrement64(&s_wpv_claim) - 1;
        u32 c = (u32)(w & 0xFFFFF), nch = (u32)((w >> 20) & 0xFFFFF);
        if (c >= nch) break;
        WpVJob* j = &s_wpv_jobs[(w >> 40) & 1];
        u32 b = c * j->chunk, e = b + j->chunk; if (e > j->count) e = j->count;
        if (j->x) for (u32 k = b; k < e; k++) vpf_fetch(j->f, vpi_get(j->x, j->first + k), &j->out[k * 16]);
        else      for (u32 k = b; k < e; k++) vpf_fetch(j->f, j->first + k, &j->out[k * 16]);
        InterlockedIncrement(&j->done);
    }
}
static DWORD WINAPI wpv_worker(LPVOID arg)
{
    (void)arg; LONG seen = 0;
    LARGE_INTEGER fq; QueryPerformanceFrequency(&fq);
    for (;;) {
        LARGE_INTEGER t0, t; QueryPerformanceCounter(&t0);
        LONG g;
        while ((g = s_wpv_gen) == seen) {          /* hot spin while draws keep coming */
            YieldProcessor();
            QueryPerformanceCounter(&t);
            if ((t.QuadPart - t0.QuadPart) * 1000000 / fq.QuadPart > s_wpv_spin_us) {
                if (s_wpv_woa) { LONG cmp = seen; s_wpv_woa(&s_wpv_gen, &cmp, sizeof(LONG), INFINITE); QueryPerformanceCounter(&t); }   /* winport v55-vtxwait */
                else WaitForSingleObject(s_wpv_sem, INFINITE);
                t0 = t; }
        }
        seen = g;
        wpv_run_chunks();
    }
    return 0;
}
static int wpv_on(void)
{
    if (s_wpv_n < 0) {
        const char* e = getenv("WINPORT_VTX_THREADS"); int n = e ? atoi(e) : 0; if (n > 16) n = 16;
        if ((e = getenv("WINPORT_VTX_PAR_MIN")) && atoi(e) > 0) s_wpv_min = atoi(e);
        if ((e = getenv("WINPORT_VTX_SPIN_US")) && atoi(e) >= 0) s_wpv_spin_us = atoi(e);
        if (n > 1 && (e = getenv("WINPORT_VTX_WAIT")) && e[0] == '1') {   /* winport v55-vtxwait */
            HMODULE kb = GetModuleHandleA("kernelbase.dll");
            if (kb) { s_wpv_woa = (WpWoaFn)GetProcAddress(kb, "WaitOnAddress"); s_wpv_wba = (WpWbaFn)GetProcAddress(kb, "WakeByAddressAll"); }
            if (!s_wpv_woa || !s_wpv_wba) { s_wpv_woa = NULL; s_wpv_wba = NULL; }
            fprintf(stderr, "[VTXWAIT55] workers sleep on WaitOnAddress: %s\n", s_wpv_woa ? "on" : "unavailable (old semaphore)");
        }
        if (n > 1) {
            s_wpv_sem = CreateSemaphoreA(NULL, 0, 1 << 20, NULL);
            for (int i = 0; i < n - 1; i++) { HANDLE h = CreateThread(NULL, 0, wpv_worker, NULL, 0, NULL); if (h) CloseHandle(h); }
            fprintf(stderr, "[VTXPAR] %d threads (caller + %d workers), min %u verts/draw, spin %d us\n", n, n - 1, s_wpv_min, s_wpv_spin_us);
        }
        s_wpv_n = n > 1 ? n : 0;
    }
    return s_wpv_n;
}
/* Returns 1 when it converted the draw in parallel (fast path only), 0 when the caller must do it serially. */
static int wpv_fetch_par(const VPFetch* f, const VPIdx* x, u32 first, u32 count, VPSlot* out)
{
    int n = wpv_on();
    if (!n || count < (u32)s_wpv_min) return 0;
    u32 nch = (u32)n * 2u, chunk = (count + nch - 1) / nch;
    if (chunk < 64) { chunk = 64; nch = (count + chunk - 1) / chunk; }
    static u32 gen; gen = (gen + 1) & 0xFFFFFF;
    WpVJob* j = &s_wpv_jobs[gen & 1];
    j->f = f; j->x = x; j->first = first; j->count = count; j->chunk = chunk; j->nchunks = nch; j->out = out; j->done = 0;
    MemoryBarrier();
    InterlockedExchange64(&s_wpv_claim, ((LONG64)gen << 40) | ((LONG64)nch << 20));
    InterlockedIncrement(&s_wpv_gen);
    if (s_wpv_wba) s_wpv_wba((PVOID)&s_wpv_gen);   /* winport v55-vtxwait */
    else ReleaseSemaphore(s_wpv_sem, s_wpv_n - 1, NULL);   /* wakes sleepers; spinning workers see the new gen first */
    wpv_run_chunks();
    while (j->done < (LONG)nch) YieldProcessor();
    MemoryBarrier();
    if (perf_on()) { s_wpv_draws++; s_wpv_verts += count; }
    return 1;
}
/* ---- winport v64-defer-vtx (fix_v64_defer_vtx_renderer.py): deferred vertex conversion ----
 * Producer = the thread holding the RSX lock (rsx-async). Chunks are claimed with a CAS on s64_head; s64_done counts
 * finished chunks. Ring slots are reused only after a join (desc: s64_dseq - s64_join_dseq < DCAP, chunks:
 * s64_tail - s64_join_tail <= ECAP), so a slot is never rewritten while a worker can still read it. */
#define WP64_DCAP 2048u
#define WP64_ECAP 16384u
typedef struct { VPFetch f; VPIdx x; int has_x; u32 first; VPSlot* out; } Wp64Desc;
typedef struct { u32 d, b, e; } Wp64Ent;
static Wp64Desc s64_desc[WP64_DCAP];
static Wp64Ent s64_ent[WP64_ECAP];
static volatile LONG64 s64_head, s64_tail, s64_done;
static LONG64 s64_join_tail;
static unsigned long long s64_dseq, s64_join_dseq;
static volatile LONG s64_gen, s64_sleepers;
static int s64_n = -1, s64_spin_us = 50;
static u32 s64_chunk = 256;
static WpWoaFn s64_woa; static WpWbaFn s64_wba;
static volatile long long s64_draws, s64_verts, s64_chunks, s64_joins, s64_join_ns, s64_full;
static void wp64_run(LONG64 i)
{
    const Wp64Ent* en = &s64_ent[(u64)i % WP64_ECAP];
    const Wp64Desc* d = &s64_desc[en->d];
    if (d->has_x) for (u32 k = en->b; k < en->e; k++) vpf_fetch(&d->f, vpi_get(&d->x, d->first + k), &d->out[(u64)k * 16]);
    else          for (u32 k = en->b; k < en->e; k++) vpf_fetch(&d->f, d->first + k, &d->out[(u64)k * 16]);
    InterlockedIncrement64(&s64_done);
}
static int wp64_claim_run(void)   /* 1 = ran or lost a race (try again), 0 = nothing left to claim */
{
    const LONG64 h = s64_head;
    if (h >= s64_tail) return 0;
    if (InterlockedCompareExchange64(&s64_head, h + 1, h) == h) wp64_run(h);
    return 1;
}
static DWORD WINAPI wp64_worker(LPVOID arg)
{
    (void)arg;
    {   typedef HRESULT (WINAPI *set_desc_fn)(HANDLE, PCWSTR);
        set_desc_fn f = (set_desc_fn)(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetThreadDescription");
        if (f) f(GetCurrentThread(), L"vtx-defer"); }
    LARGE_INTEGER fq; QueryPerformanceFrequency(&fq);
    for (;;) {
        if (wp64_claim_run()) continue;
        LARGE_INTEGER t0, t; QueryPerformanceCounter(&t0);
        for (;;) {                                   /* short spin: draws arrive every few microseconds */
            if (s64_head < s64_tail) break;
            YieldProcessor();
            QueryPerformanceCounter(&t);
            if ((t.QuadPart - t0.QuadPart) * 1000000 / fq.QuadPart > s64_spin_us) {
                LONG g = s64_gen;
                InterlockedIncrement(&s64_sleepers);
                if (s64_head >= s64_tail) {
                    if (s64_woa) s64_woa(&s64_gen, &g, sizeof(LONG), 5); else Sleep(1);
                }
                InterlockedDecrement(&s64_sleepers);
                QueryPerformanceCounter(&t0);
            }
        }
    }
    return 0;
}
static int wp64_vtx_pending(void) { return s64_n > 0 && s64_done != s64_tail; }
static void wp64_vtx_join_wk85(void);
static void wp64_vtx_join(void) { const unsigned long long t_ = WK85_T0(); wp64_vtx_join_wk85(); WK85_ADD(WK85_join, t_); }   /* winport v85a-walkprof */
static void wp64_vtx_join_wk85(void)
{
    if (s64_n <= 0) return;
    if (s64_done != s64_tail) {
        const double t0 = perf_now();
        while (wp64_claim_run()) { }
        while (s64_done < s64_tail) YieldProcessor();
        MemoryBarrier();
        s64_joins++; s64_join_ns += (long long)((perf_now() - t0) * 1e9);
    }
    s64_join_tail = s64_tail; s64_join_dseq = s64_dseq;
}
static int wp64_on(void)
{
    if (s64_n < 0) {
        extern int wp59_async_rsx_on(void);
        extern void wp64_register(int (*)(void), void (*)(void));
        const char* e = getenv("WINPORT_DEFER_VTX"); int n = e ? atoi(e) : 0;
        if (n > 16) n = 16;
        if (n > 0 && !wp59_async_rsx_on()) { fprintf(stderr, "[DEFER64] WINPORT_DEFER_VTX needs WINPORT_ASYNC_RSX=1 -> off\n"); n = 0; }
        {   static const char* const dbg[] = { "RSX_DBG_VTX", "RSX_DBG_TRACK", "RSX_DBG_PICK", "RSX_DBG_SCALE",
                                               "RSX_DBG_RECENTER", "RSX_DBG_SHIFT", "VTX_DUMP" };
            for (int i = 0; n > 0 && i < (int)(sizeof dbg / sizeof dbg[0]); i++)
                if (getenv(dbg[i])) { fprintf(stderr, "[DEFER64] %s reads converted vertices at record time -> off\n", dbg[i]); n = 0; } }
        if ((e = getenv("WINPORT_DEFER_VTX_CHUNK")) && atoi(e) >= 32 && atoi(e) <= 65536) s64_chunk = (u32)atoi(e);
        if ((e = getenv("WINPORT_DEFER_VTX_SPIN_US")) && atoi(e) >= 0 && atoi(e) <= 100000) s64_spin_us = atoi(e);
        if (n > 0) {
            HMODULE kb = GetModuleHandleA("kernelbase.dll");
            if (kb) { s64_woa = (WpWoaFn)GetProcAddress(kb, "WaitOnAddress"); s64_wba = (WpWbaFn)GetProcAddress(kb, "WakeByAddressAll"); }
            if (!s64_woa || !s64_wba) { s64_woa = NULL; s64_wba = NULL; }
            int made = 0;
            for (int i = 0; i < n; i++) { HANDLE h = CreateThread(NULL, 0, wp64_worker, NULL, 0, NULL); if (h) { CloseHandle(h); made++; } }
            if (!made) n = 0;
            else {
                s64_n = n;   /* before the engine can call back */
                wp64_register(wp64_vtx_pending, wp64_vtx_join);
                fprintf(stderr, "[DEFER64] deferred vertex conversion ON: %d workers, chunk %u verts, spin %d us, wait=%s\n",
                        made, s64_chunk, s64_spin_us, s64_woa ? "WaitOnAddress" : "Sleep(1)");
            }
        }
        s64_n = n > 0 ? n : 0;
    }
    return s64_n > 0;
}
/* Queue the conversion of [first, first+count) into out. 1 = queued (the caller must not convert), 0 = do it now. */
static int wp64_defer_wk85(const VPFetch* f, const VPIdx* x, u32 first, u32 count, VPSlot* out);
static int wp64_defer(const VPFetch* f, const VPIdx* x, u32 first, u32 count, VPSlot* out) { const unsigned long long t_ = WK85_T0(); int r_ = wp64_defer_wk85(f, x, first, count, out); WK85_ADD(WK85_defq, t_); return r_; }   /* winport v85a-walkprof */
static int wp64_defer_wk85(const VPFetch* f, const VPIdx* x, u32 first, u32 count, VPSlot* out)
{
    if (!count || !wp64_on()) return 0;
    const u32 nch = (count + s64_chunk - 1u) / s64_chunk;
    if (nch > WP64_ECAP / 2u) return 0;
    if (s64_dseq + 1ull - s64_join_dseq > WP64_DCAP || (u64)(s64_tail + (LONG64)nch - s64_join_tail) > WP64_ECAP) {
        s64_full++; wp64_vtx_join();
    }
    const u32 di = (u32)(s64_dseq++ % WP64_DCAP);
    Wp64Desc* d = &s64_desc[di];
    d->f = *f; d->has_x = x != NULL; if (x) d->x = *x; d->first = first; d->out = out;
    const LONG64 t = s64_tail;
    for (u32 c = 0; c < nch; c++) {
        Wp64Ent* en = &s64_ent[(u64)(t + (LONG64)c) % WP64_ECAP];
        en->d = di; en->b = c * s64_chunk; en->e = (c + 1u) * s64_chunk < count ? (c + 1u) * s64_chunk : count;
    }
    InterlockedExchange64(&s64_tail, t + (LONG64)nch);   /* full barrier: entries before the new tail */
    InterlockedIncrement(&s64_gen);
    if (s64_wba && s64_sleepers) s64_wba((PVOID)&s64_gen);
    s64_draws++; s64_verts += count; s64_chunks += nch;
    return 1;
}
static void wp64_report(void)   /* once a second, from render_frame */
{
    static double last = 0.0;
    if (s64_n <= 0) return;
    const double now = perf_now();
    if (last == 0.0) { last = now; return; }
    const double s = now - last;
    if (s < 1.0) return;
    last = now;
    if (!wp_stats_on()) ; else fprintf(stderr, "[DEFER64] draws=%.0f/s verts=%.0fk/s chunks=%.0f/s | joins=%.0f/s wait=%.1fms/s | ring-full=%.0f/s\n",
            s64_draws / s, s64_verts / s / 1000.0, s64_chunks / s, s64_joins / s, s64_join_ns / 1e6 / s, s64_full / s);
    s64_draws = s64_verts = s64_chunks = s64_joins = s64_join_ns = s64_full = 0;
}
/* ---- end winport v64-defer-vtx pool ---- */
/* winport v42: WINPORT_VTXCHK2=1 (G3 geometry timing) -- see research/G3_geometry_timing.md */
typedef struct { const u8* base; u32 stride, fp, nvi, bad0, mixed, ntri, lo, hi, a0off, idxoff; double t; u32 vi[32]; } WpVc2;
static WpVc2 s_vc2[128]; static int s_vc2_n;
static unsigned long s_vc2_draws, s_vc2_mixdraws, s_vc2_rechk, s_vc2_fixed, s_vc2_still;
static int wp_vc2_on(void) { static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_VTXCHK2"); v = e ? atoi(e) : 0; } return v; }
static int wp_vc2_bad(const u8* p)
{
    float x[3]; for (int c = 0; c < 3; c++) { u32 w; memcpy(&w, p + c * 4, 4); w = __builtin_bswap32(w); memcpy(&x[c], &w, 4); }
    if (x[0] == 0.0f && x[1] == 0.0f && x[2] == 0.0f) return 1;
    for (int c = 0; c < 3; c++) if (!(x[c] == x[c]) || x[c] > 1e7f || x[c] < -1e7f) return 1;
    return 0;
}
static void wp_vc2_draw(const rsx_state* st, const VPFetch* f, int fast, const VPIdx* x, u32 first, u32 count)
{
    if (!wp_vc2_on() || !fast || !st) return;
    int k = -1; for (int i = 0; i < f->n; i++) if (f->a[i].idx == 0 && f->a[i].type == 2 && f->a[i].size >= 3) k = i;
    if (k < 0) return;
    const u8* b = f->a[k].base; u32 s = f->a[k].stride;
    u32 ntri = count / 3, mixed = 0, badv = 0, lo = 0xFFFFFFFFu, hi = 0, nvi = 0, vi_[32];
    if (ntri > 8192) ntri = 8192;
    for (u32 t = 0; t < ntri; t++) {
        int nb = 0; u32 bi[3];
        for (int c = 0; c < 3; c++) { u32 v = x ? vpi_get(x, first + t * 3 + c) : first + t * 3 + c;
            if (v < lo) lo = v; if (v > hi) hi = v;
            if (wp_vc2_bad(b + (u64)v * s)) { bi[nb++] = v; } }
        badv += (u32)nb;
        if (nb == 1 || nb == 2) { mixed++; for (int c = 0; c < nb && nvi < 32; c++) vi_[nvi++] = bi[c]; }
    }
    s_vc2_draws++;
    if (!badv) return;
    if (mixed) s_vc2_mixdraws++;
    if (!mixed && wp_vc2_on() < 2) return;   /* =2 also tracks draws whose zero vertices form only degenerate triangles */
    if (!nvi) { for (u32 t = 0; t < ntri && nvi < 32; t++) for (int c = 0; c < 3 && nvi < 32; c++) {
        u32 v = x ? vpi_get(x, first + t * 3 + c) : first + t * 3 + c; if (wp_vc2_bad(b + (u64)v * s)) vi_[nvi++] = v; } }
    static unsigned long nlog = 0; ++nlog;
    if (nlog <= 40 || (nlog & (nlog - 1)) == 0)
        fprintf(stderr, "[VCHK2] #%lu fp=0x%08X vpstart=%u a0off=0x%08X str=%u idx=%s off=0x%08X range=[%u..%u] tris=%u zero-verts=%u mixed-tris=%u\n",
                nlog, st->shader_program, st->transform_program_start, st->vertex_attribs[0].offset, s,
                x ? (x->is16 ? "u16" : "u32") : "none", x ? st->index_array_addr : 0u, lo, hi, ntri, badv, mixed);
    if (nlog <= 12) {   /* raw look: first 18 indices, then vertices lo..lo+7 as big-endian floats */
        char ln[1024]; int o = 0;
        for (u32 q = 0; q < 18 && q < count && o < 900; q++) o += snprintf(ln + o, sizeof ln - o, " %u", x ? vpi_get(x, first + q) : first + q);
        fprintf(stderr, "[VCHK2]   idx:%s\n", ln);
        for (u32 v = lo; v < lo + 8 && v <= hi; v++) { o = 0; const u8* p = b + (u64)v * s;
            for (u32 w = 0; w < s / 4 && w < 16; w++) { u32 u; memcpy(&u, p + w * 4, 4); u = __builtin_bswap32(u); float fv; memcpy(&fv, &u, 4);
                o += snprintf(ln + o, sizeof ln - o, " %08X(%g)", u, fv); }
            fprintf(stderr, "[VCHK2]   v%u:%s\n", v, ln); }
    }
    if (s_vc2_n < (int)(sizeof s_vc2 / sizeof s_vc2[0])) {
        WpVc2* r = &s_vc2[s_vc2_n++];
        r->base = b; r->stride = s; r->fp = st->shader_program; r->nvi = nvi; r->bad0 = badv; r->mixed = mixed; r->ntri = ntri;
        r->lo = lo; r->hi = hi; r->a0off = st->vertex_attribs[0].offset; r->idxoff = x ? st->index_array_addr : 0u; r->t = perf_now();
        memcpy(r->vi, vi_, nvi * sizeof(u32));
    }
}
/* called at the start of render_frame: are the vertices that were zero at record time still zero now? */
static void wp_vc2_recheck(void)
{
    if (!wp_vc2_on()) return;
    double now = perf_now();
    for (int i = 0; i < s_vc2_n; i++) {
        WpVc2* r = &s_vc2[i]; u32 still = 0;
        for (u32 q = 0; q < r->nvi; q++) still += (u32)wp_vc2_bad(r->base + (u64)r->vi[q] * r->stride);
        s_vc2_rechk++; if (still == r->nvi) s_vc2_still++; else s_vc2_fixed++;
        static unsigned long nlog = 0; ++nlog;
        if (nlog <= 60 || (nlog & (nlog - 1)) == 0)
            fprintf(stderr, "[VCHK2R] fp=0x%08X a0off=0x%08X range=[%u..%u] mixed-tris=%u: %u sampled zero verts -> %u still zero at render_frame (+%.2f ms)\n",
                    r->fp, r->a0off, r->lo, r->hi, r->mixed, r->nvi, still, (now - r->t) * 1e3);
    }
    /* second look one render_frame later (stage-1 copies) */
    static WpVc2 s_old[128]; static int n_old = 0;
    for (int i = 0; i < n_old; i++) { WpVc2* r = &s_old[i]; u32 still = 0;
        for (u32 q = 0; q < r->nvi; q++) still += (u32)wp_vc2_bad(r->base + (u64)r->vi[q] * r->stride);
        static unsigned long nl2 = 0; ++nl2;
        if (nl2 <= 30 || (nl2 & (nl2 - 1)) == 0)
            fprintf(stderr, "[VCHK2R2] fp=0x%08X a0off=0x%08X range=[%u..%u]: %u zero verts -> %u still zero one frame later (+%.2f ms)\n",
                    r->fp, r->a0off, r->lo, r->hi, r->nvi, still, (now - r->t) * 1e3); }
    memcpy(s_old, s_vc2, (size_t)s_vc2_n * sizeof s_vc2[0]); n_old = s_vc2_n;
    s_vc2_n = 0;
    static double tl = 0; if (now - tl > 5.0) { tl = now;
        fprintf(stderr, "[VCHK2S] draws=%lu with-mixed-tris=%lu rechecked=%lu filled-later=%lu still-zero=%lu\n",
                s_vc2_draws, s_vc2_mixdraws, s_vc2_rechk, s_vc2_fixed, s_vc2_still); }
}
#define VPF_READ(F, OK, ST, IX, DST) do { if (OK) vpf_fetch(&(F), (IX), (DST)); else read_vp_vertex((ST), (IX), (DST)); } while (0)

/* VTX_POS=<N>: print the first fetched position of the first N draws. A guest
 * vertex array that resolves to the wrong memory reads as garbage/denormals, and
 * that is indistinguishable from "the shader is wrong" without seeing the input. */
void rsx_vtx_pos_dbg(const rsx_state* state, const float* v, u32 n)
{
    static int cap = -1, seen = 0;
    if (cap < 0) { const char* e = getenv("VTX_POS"); cap = e ? atoi(e) : 0; }
    if (!cap || seen >= cap) return;
    seen++;
    const rsx_vertex_attrib* a = &state->vertex_attribs[0];
    fprintf(stderr, "[VTXPOS] a0 off=0x%X stride=%u size=%u type=%u -> (%g, %g, %g, %g)\n",
            a->offset, a->stride, a->size, a->type,
            v[0], v[1], v[2], v[3]);   /* all four: masking by `size` hid whether w survived */
}

static void vp_attrs_dbg(const rsx_state* state)
{
    if (!getenv("VP_ATTRS")) return;
    /* VP_ATTRS_FP=<hex shader_program>: dump only the draws that use one
     * fragment program. Without it the first six draws are whatever the frame
     * happens to start with, which is never the mesh being investigated. */
    { static const char* e = (const char*)1; static u32 want = 0;
      if (e == (const char*)1) { e = getenv("VP_ATTRS_FP");
          want = e ? (u32)strtoul(e, NULL, 0) : 0; }
      if (want && (!state || state->shader_program != want)) return; }
    /* Dedupe by the set of enabled attributes rather than printing the first
     * few draws: one fragment program can be used by meshes with different
     * vertex layouts, and a mesh missing an attribute takes the constant
     * register instead -- which is exactly the case worth seeing. */
    { u32 mask = 0;
      for (int i = 0; i < 16; i++) if (state->vertex_attribs[i].enabled) mask |= 1u << i;
      static u32 seen[16]; static int ns = 0;
      for (int i = 0; i < ns; i++) if (seen[i] == mask) return;
      if (ns >= 16) return;
      seen[ns++] = mask; }
    fprintf(stderr, "[VPATTR] --- fp=0x%X ---%c", state->shader_program, 10);
    fprintf(stderr, "[VPATTR] divider_op=0x%08X\n", 0u);
    for (int i = 0; i < 16; i++) {
        const rsx_vertex_attrib* a = &state->vertex_attribs[i];
        if (a->enabled) fprintf(stderr, "[VPATTR] a%d off=0x%X stride=%u size=%u type=%u freq=%u fmt=0x%08X\n",
                                i, a->offset, a->stride, a->size, a->type, 0u, a->format);
    }
    /* VP_ATTRS_DUMP=<attr>:<n>: read n entries of that attribute straight out of
     * guest memory. Answers "did the guest write these values, or did our fetch
     * mangle them" without inferring it from the rendered image. */
    { const char* e = getenv("VP_ATTRS_DUMP"); if (!e) return;
      int ai = 0, cnt = 8; sscanf(e, "%d:%d", &ai, &cnt);
      const rsx_vertex_attrib* a = &state->vertex_attribs[ai];
      if (!a->enabled || !a->stride) return;
      extern uint8_t* vm_base;
      extern u32 cellGcmResolveLocated(int, u32);
      u32 nbad = 0, nzero = 0;
      for (int v = 0; v < cnt; v++) {
          u32 aoff = (a->offset & 0x7FFFFFFFu) + (u32)v * a->stride;
          const u8* q = vm_base + ((a->offset & 0x80000000u)
                        ? cellGcmResolveLocated(0, aoff) : cellGcmResolveLocated(1, aoff));
          /* Also read the same offset through the OTHER context-DMA. An array
           * the guest put in main memory but whose offset we resolve as LOCAL
           * reads as untouched VRAM, i.e. all zeros -- indistinguishable from
           * "the producer never ran". */
          if (v == 0) {
              u32 la = cellGcmResolveLocated(1, aoff), ma = cellGcmResolveLocated(0, aoff);
              const u8* lp = vm_base + la; const u8* mp = vm_base + ma;
              const u8* rp = vm_base + (a->offset & 0x7FFFFFFFu);   /* raw, unresolved */
              fprintf(stderr, "[VPDUMP] a%d off=0x%X  LOCAL@0x%X=(%g,%g,%g)  MAIN@0x%X=(%g,%g,%g)"
                              "  RAW@0x%X=(%g,%g,%g)%c",
                      ai, aoff, la, rsx_rd_bef(lp), rsx_rd_bef(lp+4), rsx_rd_bef(lp+8),
                      ma, rsx_rd_bef(mp), rsx_rd_bef(mp+4), rsx_rd_bef(mp+8),
                      (a->offset & 0x7FFFFFFFu), rsx_rd_bef(rp), rsx_rd_bef(rp+4), rsx_rd_bef(rp+8), 10);
          }
          u32 nc = a->size ? a->size : 3; if (nc > 3) nc = 3;
          float f[3] = {0,0,0};
          for (u32 k = 0; k < nc; k++) f[k] = rsx_rd_bef(q + k * 4);
          if (v < 12)
              fprintf(stderr, "[VPDUMP] a%d[%d] = (%g, %g, %g)%c", ai, v, f[0], f[1], f[2], 10);
          int allz = 1; for (u32 k = 0; k < nc; k++) if (f[k] != 0.f) allz = 0;
          if (allz) nzero++;
          else if (f[2] < -0.9f && f[0] > -0.1f && f[0] < 0.1f) nbad++;
      }
      fprintf(stderr, "[VPDUMP] a%d over %d verts: %u zero, %u near (0,0,-1)%c",
              ai, cnt, nzero, nbad, 10);
      /* How many entries differ from the first? An all-identical position array
       * is a degenerate mesh: it rasterizes nothing, which looks the same as
       * "the draw never happened". */
      { float f0[3]; u32 o0 = (a->offset & 0x7FFFFFFFu);
        const u8* q0 = vm_base + ((a->offset & 0x80000000u)
                       ? cellGcmResolveLocated(0, o0) : cellGcmResolveLocated(1, o0));
        for (int k = 0; k < 3; k++) f0[k] = rsx_rd_bef(q0 + k * 4);
        u32 diff = 0;
        for (int v = 1; v < cnt; v++) {
            u32 ao = o0 + (u32)v * a->stride;
            const u8* q = vm_base + ((a->offset & 0x80000000u)
                          ? cellGcmResolveLocated(0, ao) : cellGcmResolveLocated(1, ao));
            for (int k = 0; k < 3; k++)
                if (rsx_rd_bef(q + k * 4) != f0[k]) { diff++; break; }
        }
        fprintf(stderr, "[VPDUMP] a%d: %u/%d entries differ from entry 0 (%g,%g,%g)%c",
                ai, diff, cnt, f0[0], f0[1], f0[2], 10);
        /* Extent: an isosurface with little fluid is a speck, and a speck that
         * rasterizes nothing is correct, not a bug. */
        { float lo[3] = {1e30f,1e30f,1e30f}, hi[3] = {-1e30f,-1e30f,-1e30f};
          for (int v = 0; v < cnt; v++) {
              u32 ao = o0 + (u32)v * a->stride;
              const u8* q = vm_base + ((a->offset & 0x80000000u)
                            ? cellGcmResolveLocated(0, ao) : cellGcmResolveLocated(1, ao));
              for (int k = 0; k < 3; k++) { float f = rsx_rd_bef(q + k * 4);
                  if (f < lo[k]) lo[k] = f; if (f > hi[k]) hi[k] = f; } }
          u32 nnan = 0, nfin = 0;
          for (int v = 0; v < cnt; v++) {
              u32 ao = o0 + (u32)v * a->stride;
              const u8* q = vm_base + ((a->offset & 0x80000000u)
                            ? cellGcmResolveLocated(0, ao) : cellGcmResolveLocated(1, ao));
              int bad = 0;
              for (int k = 0; k < 3; k++) { float f = rsx_rd_bef(q + k * 4);
                  if (!(f == f) || f > 1e30f || f < -1e30f) bad = 1; }
              if (bad) nnan++; else nfin++;
          }
          fprintf(stderr, "[VPDUMP] a%d extent x[%g..%g] y[%g..%g] z[%g..%g]"
                          "  finite=%u nan/inf=%u%c",
                  ai, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], nfin, nnan, 10); } } }
}

static u32 upload_quads_vp_wk85(const rsx_state* state, u32 first, u32 count);
static u32 upload_quads_vp(const rsx_state* state, u32 first, u32 count) { const unsigned long long t_ = WK85_T0(); u32 r_ = upload_quads_vp_wk85(state, first, count); WK85_ADD(WK85_vtx, t_); return r_; }   /* winport v85a-walkprof */
static u32 upload_quads_vp_wk85(const rsx_state* state, u32 first, u32 count)
{
    extern uint8_t* vm_base;
    if (!state || !vm_base || !s_d3d.vp_vb_mapped) return 0;
    if (!state->vertex_attribs[0].enabled) return 0;
    vp_attrs_dbg(state);
    u32 quads = count / 4;
    u32 maxv = (MAX_VERTICES * VP_VERT_STRIDE - s_d3d.vp_vb_offset) / VP_VERT_STRIDE;
    if (quads * 6 > maxv) quads = maxv / 6;
    VPSlot* out = (VPSlot*)((u8*)s_d3d.vp_vb_mapped
        + (u64)s_d3d.vp_parity * MAX_VERTICES * VP_VERT_STRIDE + s_d3d.vp_vb_offset);
    u32 o = 0;
    for (u32 q = 0; q < quads; q++) {
        VPSlot c[4][16];
        for (u32 k = 0; k < 4; k++)
            read_vp_vertex(state, first + q*4 + k, c[k]);
        /* quad -> two triangles (perimeter winding) */
        static const int idx[6] = {0,1,2, 0,2,3};
        for (int t = 0; t < 6; t++) { memcpy(&out[o*16], c[idx[t]], sizeof(c[0])); o++; }
        if (getenv("VTX_DUMP")) { static int _n=0; if (_n++ < 4) {
            FILE* f = fopen("vtx_dump.txt", _n==1 ? "w" : "a");
            if (f) { for (u32 k = 0; k < 4; k++)
                fprintf(f, "q%02d v%u a0=(%.3f,%.3f,%.3f,%.3f) a8=(%.3f,%.3f)\n", _n, k,
                    c[k][0].v[0],c[k][0].v[1],c[k][0].v[2],c[k][0].v[3],
                    c[k][8].v[0],c[k][8].v[1]);
              fclose(f); } } }
    }
    s_d3d.vp_vb_offset += o * VP_VERT_STRIDE;
    return o;
}

/* Straight triangle-list upload through the VP path (gcm/cube's cube draws
 * TRIANGLES, prim 5 -- no expansion needed). */
static u32 upload_tris_vp_wk85(const rsx_state* state, u32 first, u32 count);
static u32 upload_tris_vp(const rsx_state* state, u32 first, u32 count) { const unsigned long long t_ = WK85_T0(); u32 r_ = upload_tris_vp_wk85(state, first, count); WK85_ADD(WK85_vtx, t_); return r_; }   /* winport v85a-walkprof */
static u32 upload_tris_vp_wk85(const rsx_state* state, u32 first, u32 count)
{
    extern uint8_t* vm_base;
    s_req_verts += count; s_req_draws++;
    if (!state || !vm_base || !s_d3d.vp_vb_mapped) return 0;
    if (!state->vertex_attribs[0].enabled) return 0;
    vp_attrs_dbg(state);
    u32 maxv = (MAX_VERTICES * VP_VERT_STRIDE - s_d3d.vp_vb_offset) / VP_VERT_STRIDE;
    if (count > maxv) { s_drop_draws++;
        { static int _n = 0; if (getenv("VBFULL") && _n++ < 12)
            fprintf(stderr, "[VBFULL] fp=0x%X wanted %u verts, room for %u%c",
                    state->shader_program, count, maxv, 10); }
        count = maxv - (maxv % 3); }
    VPSlot* out = (VPSlot*)((u8*)s_d3d.vp_vb_mapped
        + (u64)s_d3d.vp_parity * MAX_VERTICES * VP_VERT_STRIDE + s_d3d.vp_vb_offset);
    { double _tv = perf_on() ? perf_now() : 0.0;
      VPFetch vpf_; int fast_ = vpf_prepare(state, &vpf_, first + count - 1u);   /* winport-fix: fast vertex fetch */
      wp_vc2_draw(state, &vpf_, fast_, NULL, first, count);   /* winport v42 */
      if (!(fast_ && (wp64_defer(&vpf_, NULL, first, count, out) || wpv_fetch_par(&vpf_, NULL, first, count, out))))   /* winport v27 + winport v64-defer-vtx */
      for (u32 k = 0; k < count; k++)
          VPF_READ(vpf_, fast_, state, first + k, &out[k*16]);
      if (perf_on()) { s_perf_vtx += perf_now() - _tv; s_perf_nverts += count; }
    if (perf_on()) { s_perf_vdraws++; s_perf_vfast += fast_ ? 1 : 0; s_perf_vattr += (u64)vpf_.en * count;   /* winport v25: count enabled attrs on BOTH paths */
      if (fast_ && wp_vbench_on()) { static VPSlot scr_[512 * 16]; double t0_ = perf_now(); for (u32 k = 0; k < count; k++) vpf_fetch(&vpf_, first + k, &scr_[(k & 511) * 16]); s_perf_vcache += perf_now() - t0_; } } }
    /* IDXDBG=<hex shader_program>: vertex range for that program's draws. */
    { static const char* ie = (const char*)1; static u32 iw = 0;
      if (ie == (const char*)1) { ie = getenv("IDXDBG");
          iw = ie ? (u32)strtoul(ie, NULL, 16) : 0; }
      if (iw && state->shader_program == iw) { static int _n = 0; if (_n++ < 12)
          fprintf(stderr, "[IDXDBG] tris first=%u count=%u a0off=0x%X stride=%u%c",
                  first, count, state->vertex_attribs[0].offset,
                  state->vertex_attribs[0].stride, 10); } }
    /* RSX_DBG_VTX=<hex tex0 offset>: dump attribute-0 positions for the draws that
     * bind that texture. The duck's texture resolves and its 9960 draws all
     * target the backbuffer, yet none of its texels reach the screen -- so the
     * question is whether its vertices are where they should be. */
    { static const char* dv = (const char*)1; static u32 want = 0; static int n = 0;
      if (dv == (const char*)1) { dv = getenv("RSX_DBG_VTX");
                                  want = dv ? (u32)strtoul(dv, NULL, 16) : 0; }
      if (want && s_d3d.cur_texs[0].raw == want && n < 6) { n++;
        fprintf(stderr, "[DUCKVTX] first=%u count=%u", first, count);
        for (u32 k = 0; k < count && k < 4; k++)
            fprintf(stderr, "  v%u=(%.2f,%.2f,%.2f)", k,
                    out[k*16].v[0], out[k*16].v[1], out[k*16].v[2]);
        { int lo = -1, hi = -1, nz = 0;
          for (int r = 0; r < RSX_MAX_VERTEX_CONSTANTS; r++) {
              const float* m = state->vertex_constants[r];
              if (m[0] || m[1] || m[2] || m[3]) { if (lo < 0) lo = r; hi = r; nz++; }
          }
          fprintf(stderr, "  | constants: %d non-zero, slots %d..%d", nz, lo, hi); }
        fprintf(stderr, "%c", 10);
      } }
    if (getenv("VTX_DUMP") && state->surface_color_offset[0] == 0xCC0000) {
        static int _n=0; if (_n++ < 1) {
        FILE* f = fopen("vtx_dump.txt", "w");
        if (f) { fprintf(f, "G-buffer draw surf0=0x%X count=%u first=%u\n",
                         state->surface_color_offset[0], count, first);
          for (u32 k = 0; k < count && k < 30; k++)
            fprintf(f, "v%02u a0=(%.3f,%.3f,%.3f) a2=(%.3f,%.3f,%.3f) a9=(%.4f,%.4f,%.4f,%.4f)\n", k,
                out[k*16].v[0],out[k*16].v[1],out[k*16].v[2],
                out[k*16+2].v[0],out[k*16+2].v[1],out[k*16+2].v[2],
                out[k*16+9].v[0],out[k*16+9].v[1],out[k*16+9].v[2],out[k*16+9].v[3]);
          fclose(f); } } }
    s_d3d.vp_vb_offset += count * VP_VERT_STRIDE;
    return count;
}

/* Non-indexed TRIANGLE_STRIP (fan=0) / TRIANGLE_FAN (fan=1) through the VP
 * path, expanded to a triangle list. LBP's Bink movie draws its YUV quad as a
 * non-indexed 4-vertex strip (prim 6); without this it fell to the fixed-
 * function fallback with is_vp=0 (no vertex-program transform -> the quad
 * collapsed to a line) and textured=0 (the Y/U/V planes never sampled). */
static u32 upload_strip_vp_wk85(const rsx_state* state, u32 first, u32 count, int fan);
static u32 upload_strip_vp(const rsx_state* state, u32 first, u32 count, int fan) { const unsigned long long t_ = WK85_T0(); u32 r_ = upload_strip_vp_wk85(state, first, count, fan); WK85_ADD(WK85_vtx, t_); return r_; }   /* winport v85a-walkprof */
static u32 upload_strip_vp_wk85(const rsx_state* state, u32 first, u32 count, int fan)
{
    extern uint8_t* vm_base;
    if (!state || !vm_base || !s_d3d.vp_vb_mapped) return 0;
    if (!state->vertex_attribs[0].enabled) return 0;
    if (count < 3) return 0;
    u32 tris = count - 2;
    u32 maxv = (MAX_VERTICES * VP_VERT_STRIDE - s_d3d.vp_vb_offset) / VP_VERT_STRIDE;
    if (tris * 3 > maxv) tris = maxv / 3;
    VPSlot* out = (VPSlot*)((u8*)s_d3d.vp_vb_mapped
        + (u64)s_d3d.vp_parity * MAX_VERTICES * VP_VERT_STRIDE + s_d3d.vp_vb_offset);
    u32 o = 0;
    VPFetch vpf_; int fast_ = vpf_prepare(state, &vpf_, first + count - 1u);   /* winport-fix: fast vertex fetch */
    for (u32 t = 0; t < tris; t++) {
        u32 i0 = fan ? 0 : t;
        /* strip winding alternates per triangle to keep facing consistent */
        u32 i1 = t + 1, i2 = t + 2;
        if (!fan && (t & 1)) { u32 tmp = i1; i1 = i2; i2 = tmp; }
        VPF_READ(vpf_, fast_, state, first + i0, &out[o*16]); o++;
        VPF_READ(vpf_, fast_, state, first + i1, &out[o*16]); o++;
        VPF_READ(vpf_, fast_, state, first + i2, &out[o*16]); o++;
    }
    s_d3d.vp_vb_offset += o * VP_VERT_STRIDE;
    return o;
}

/* Fetch index k from the guest index array (SET_INDEX_ARRAY_ADDRESS/_DMA:
 * dma [3:0] = location (0 local, 1 main), [7:4] = type (0 u32, 1 u16)).
 * Indices are big-endian in guest memory. */
static u32 read_guest_index(const rsx_state* st, u32 k)
{
    extern uint8_t* vm_base;
    extern u32 cellGcmResolveLocated(int local, u32 offset);
    int local = (((st->index_location | ((u32)st->index_is_16bit << 4)) & 0xF) == 0);
    u32 base = cellGcmResolveLocated(local, st->index_array_addr);
    const u8* p = vm_base + base;
    if ((((st->index_location | ((u32)st->index_is_16bit << 4)) >> 4) & 0xF) == 1) {
        p += (u64)k * 2;
        return ((u32)p[0] << 8) | p[1];
    }
    p += (u64)k * 4;
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

/* Indexed variants of the VP-path uploads. */
/* winport v85e-idxdraw: convert [min,max] once + rebased 16-bit indices. Returns R (vertices written) or 0 = expand. */
static u32 wp85_idxd(const VPFetch* f, const VPIdx* x, u32 first, u32 count, u32 maxi, VPSlot* out, u32 maxv)
{
    if (count < 3 || count % 3u) { s85_n_exp++; return 0; }
    if (s_d3d.merge_prev_draw && s_d3d.draw_count > 0 && s_d3d.merge_first_end == first) { s85_n_merge++; s85_n_exp++; return 0; }
    if (s_d3d.draw_count >= MAX_DRAWS || !x->p) { s85_n_exp++; return 0; }
    u32 mn = 0xFFFFFFFFu;
    if (x->is16) { const u8* p = x->p + (u64)first * 2; for (u32 k = 0; k < count; k++) { u16 v; memcpy(&v, p + (u64)k * 2, 2); v = __builtin_bswap16(v); mn = v < mn ? v : mn; } }
    else         { const u8* p = x->p + (u64)first * 4; for (u32 k = 0; k < count; k++) { u32 v; memcpy(&v, p + (u64)k * 4, 4); v = __builtin_bswap32(v); mn = v < mn ? v : mn; } }
    if (mn > maxi) { s85_n_exp++; return 0; }
    const u32 R = maxi - mn + 1u;
    if (R > count || R > 65536u || R > maxv || s85_ib_off + count > WP85_IB_CAP || !wp85_ib_ensure()) { s85_n_exp++; return 0; }
    u16* ib = s85_ib_mapped + (u64)s_d3d.vp_parity * WP85_IB_CAP + s85_ib_off;
    if (x->is16) { const u8* p = x->p + (u64)first * 2; for (u32 k = 0; k < count; k++) { u16 v; memcpy(&v, p + (u64)k * 2, 2); ib[k] = (u16)(__builtin_bswap16(v) - mn); } }
    else         { const u8* p = x->p + (u64)first * 4; for (u32 k = 0; k < count; k++) { u32 v; memcpy(&v, p + (u64)k * 4, 4); ib[k] = (u16)(__builtin_bswap32(v) - mn); } }
    if (!(wp64_defer(f, NULL, mn, R, out) || wpv_fetch_par(f, NULL, mn, R, out)))
        for (u32 k = 0; k < R; k++) vpf_fetch(f, mn + k, &out[(u64)k * 16]);
    s85_pending_ib = s85_ib_off + 1u; s85_ib_off += count;
    s85_n_idx++; s85_v_conv += R; s85_v_idx += count;
    return R;
}
static u32 upload_quads_vp_indexed_wk85(const rsx_state* state, u32 first, u32 count);
static u32 upload_quads_vp_indexed(const rsx_state* state, u32 first, u32 count) { const unsigned long long t_ = WK85_T0(); u32 r_ = upload_quads_vp_indexed_wk85(state, first, count); WK85_ADD(WK85_vtx, t_); return r_; }   /* winport v85a-walkprof */
static u32 upload_quads_vp_indexed_wk85(const rsx_state* state, u32 first, u32 count)
{
    extern uint8_t* vm_base;
    if (!state || !vm_base || !s_d3d.vp_vb_mapped) return 0;
    if (!state->vertex_attribs[0].enabled) return 0;
    u32 quads = count / 4;
    u32 maxv = (MAX_VERTICES * VP_VERT_STRIDE - s_d3d.vp_vb_offset) / VP_VERT_STRIDE;
    if (quads * 6 > maxv) quads = maxv / 6;
    VPSlot* out = (VPSlot*)((u8*)s_d3d.vp_vb_mapped
        + (u64)s_d3d.vp_parity * MAX_VERTICES * VP_VERT_STRIDE + s_d3d.vp_vb_offset);
    u32 o = 0;
    for (u32 q = 0; q < quads; q++) {
        VPSlot c[4][16];
        for (u32 k = 0; k < 4; k++)
            read_vp_vertex(state, read_guest_index(state, first + q*4 + k), c[k]);
        static const int idx[6] = {0,1,2, 0,2,3};
        for (int t = 0; t < 6; t++) { memcpy(&out[o*16], c[idx[t]], sizeof(c[0])); o++; }
        if (getenv("VTX_DUMP")) { static int _n=0; if (_n++ < 6) {
            FILE* f = fopen("vtx_dump.txt", _n==1 ? "w" : "a");
            if (f) {
                const float* vs_ = state->viewport_scale;
                const float* vo_ = state->viewport_offset;
                fprintf(f, "iq%02d vp_scale=(%.1f,%.1f,%.3f) vp_off=(%.1f,%.1f,%.3f)\n", _n,
                        vs_[0], vs_[1], vs_[2], vo_[0], vo_[1], vo_[2]);
                for (int _c = 464; _c <= 467; _c++)
                    fprintf(f, "  c[%d]=(%.3f,%.3f,%.3f,%.3f)\n", _c,
                            state->vertex_constants[_c][0], state->vertex_constants[_c][1],
                            state->vertex_constants[_c][2], state->vertex_constants[_c][3]);
                for (u32 k = 0; k < 4; k++)
                    fprintf(f, "  v%u i%u a0=(%.3f,%.3f,%.3f,%.3f) a8=(%.3f,%.3f)\n", k,
                        read_guest_index(state, first + q*4 + k),
                        c[k][0].v[0],c[k][0].v[1],c[k][0].v[2],c[k][0].v[3],
                        c[k][8].v[0],c[k][8].v[1]);
                fclose(f);
            } } }
    }
    s_d3d.vp_vb_offset += o * VP_VERT_STRIDE;
    return o;
}

static u32 upload_tris_vp_indexed_wk85(const rsx_state* state, u32 first, u32 count);
static u32 upload_tris_vp_indexed(const rsx_state* state, u32 first, u32 count) { const unsigned long long t_ = WK85_T0(); u32 r_ = upload_tris_vp_indexed_wk85(state, first, count); WK85_ADD(WK85_vtx, t_); return r_; }   /* winport v85a-walkprof */
static u32 upload_tris_vp_indexed_wk85(const rsx_state* state, u32 first, u32 count)
{
    extern uint8_t* vm_base;
    s_req_verts += count; s_req_draws++;
    if (!state || !vm_base || !s_d3d.vp_vb_mapped) return 0;
    if (!state->vertex_attribs[0].enabled) return 0;
    vp_attrs_dbg(state);
    u32 maxv = (MAX_VERTICES * VP_VERT_STRIDE - s_d3d.vp_vb_offset) / VP_VERT_STRIDE;
    if (count > maxv) { s_drop_draws++;
        { static int _n = 0; if (getenv("VBFULL") && _n++ < 12)
            fprintf(stderr, "[VBFULL] fp=0x%X wanted %u verts, room for %u%c",
                    state->shader_program, count, maxv, 10); }
        count = maxv - (maxv % 3); }
    VPSlot* out = (VPSlot*)((u8*)s_d3d.vp_vb_mapped
        + (u64)s_d3d.vp_parity * MAX_VERTICES * VP_VERT_STRIDE + s_d3d.vp_vb_offset);
    { double _tv = perf_on() ? perf_now() : 0.0;
      VPIdx vpx_; vpi_prepare(state, &vpx_);   /* winport-fix: fast vertex fetch */
      u32 maxi_ = 0; if (wp85_idx_on()) maxi_ = wp85_maxidx(vpx_.p, vpx_.is16, first, count); else   /* winport v85c-walkfast */
      for (u32 k = 0; k < count; k++) { u32 ix_ = vpi_get(&vpx_, first + k); if (ix_ > maxi_) maxi_ = ix_; }
      if (wp85_idx_on() && wp85_verify()) { u32 m2_ = 0; for (u32 k = 0; k < count; k++) { u32 ix_ = vpi_get(&vpx_, first + k); if (ix_ > m2_) m2_ = ix_; } wp85_vcount(1, m2_ != maxi_); }
      VPFetch vpf_; int fast_ = vpf_prepare(state, &vpf_, maxi_);
      wp_vc2_draw(state, &vpf_, fast_, &vpx_, first, count);   /* winport v42 */
      s85_pending_ib = 0;   /* winport v85e-idxdraw */
      if (fast_ && wp85_idxd_on()) { const u32 r85_ = wp85_idxd(&vpf_, &vpx_, first, count, maxi_, out, maxv);
          if (r85_) { if (perf_on()) { s_perf_vtx += perf_now() - _tv; s_perf_nverts += r85_; s_perf_vdraws++; s_perf_vfast++; }
                      s_d3d.vp_vb_offset += r85_ * VP_VERT_STRIDE; return count; } }
      if (!(fast_ && (wp64_defer(&vpf_, &vpx_, first, count, out) || wpv_fetch_par(&vpf_, &vpx_, first, count, out))))   /* winport v27 + winport v64-defer-vtx */
      for (u32 k = 0; k < count; k++)
          VPF_READ(vpf_, fast_, state, vpi_get(&vpx_, first + k), &out[k*16]);
      if (perf_on()) { s_perf_vtx += perf_now() - _tv; s_perf_nverts += count; }
    if (perf_on()) { s_perf_vdraws++; s_perf_vfast += fast_ ? 1 : 0; s_perf_vattr += (u64)vpf_.en * count;   /* winport v25: count enabled attrs on BOTH paths */
      if (fast_ && wp_vbench_on()) { static VPSlot scr_[512 * 16]; double t0_ = perf_now(); for (u32 k = 0; k < count; k++) vpf_fetch(&vpf_, vpi_get(&vpx_, first + k), &scr_[(k & 511) * 16]); s_perf_vcache += perf_now() - t0_; } } }
    /* IDXDBG=<hex shader_program>: index range for that program's draws. An
     * index past the vertex array reads unmapped guest memory as zero, which
     * shows up as a spike triangle with a zero texcoord, not as a missing draw. */
    { static const char* ie = (const char*)1; static u32 iw = 0;
      if (ie == (const char*)1) { ie = getenv("IDXDBG");
          iw = ie ? (u32)strtoul(ie, NULL, 16) : 0; }
      if (iw && state->shader_program == iw) { static int _n = 0; if (_n++ < 12) {
          u32 lo = 0xFFFFFFFFu, hi = 0;
          for (u32 k = 0; k < count; k++) { u32 ix = read_guest_index(state, first + k);
              if (ix < lo) lo = ix; if (ix > hi) hi = ix; }
          fprintf(stderr, "[IDXDBG] first=%u count=%u idx=[%u..%u] dma=0x%X a0off=0x%X stride=%u%c",
                  first, count, lo, hi, (state->index_location | ((u32)state->index_is_16bit << 4)),
                  state->vertex_attribs[0].offset, state->vertex_attribs[0].stride, 10); } } }
    /* DBG_LOCK tracking: projected centroid of this mesh, updated every draw. */
    { static const char* dl = (const char*)1; static u32 wantl = 0; static int mvpb = 256;
      if (dl == (const char*)1) { dl = getenv("RSX_DBG_VTX");
                                  wantl = dl ? (u32)strtoul(dl, NULL, 16) : 0;
                                  const char* mb = getenv("MVP_BASE");
                                  if (mb) mvpb = atoi(mb); }
      /* RSX_DBG_TRACK=1 tracks the content-identified duck without needing its
       * offset passed in (VRAM offsets move between runs). Re-read every call:
       * s_duck_raw is filled by the texture upload, which happens AFTER the
       * first draws, so caching it once left the tracker watching offset 0. */
      { static int dtrk = -1;
        if (dtrk < 0) { const char* e = getenv("RSX_DBG_TRACK"); dtrk = e ? atoi(e) : 0; }
        if (dtrk && s_duck_raw) wantl = s_duck_raw; }
      /* RSX_DBG_PICK=<first>: track ONLY the draw with that starting index. The
       * mesh is one big vertex buffer sliced into 256-index chunks, so averaging
       * across all of them converges on the centre of the whole field -- which is
       * where there is nothing in particular. A single slice is a stable target
       * and can be magnified without drifting off it. */
      static int pick = -1;
      if (pick < 0) { const char* e = getenv("RSX_DBG_PICK"); pick = e ? atoi(e) : -2; }
      if (wantl && s_d3d.cur_texs[0].raw == wantl && count
          && (pick == -2 || (int)first == pick)) {
          double sx = 0, sy = 0, sz = 0;
          for (u32 k = 0; k < count; k++) {
              sx += out[k*16].v[0]; sy += out[k*16].v[1]; sz += out[k*16].v[2];
          }
          float v[4] = { (float)(sx/count), (float)(sy/count), (float)(sz/count), 1.0f };
          float clip[4];
          for (int r = 0; r < 4; r++) {
              const float* m = state->vertex_constants[mvpb + r];
              clip[r] = m[0]*v[0] + m[1]*v[1] + m[2]*v[2] + m[3]*v[3];
          }
          if (clip[3] > 1e-6f) {
              float nx = clip[0]/clip[3], ny = clip[1]/clip[3];
              if (nx > -2.0f && nx < 2.0f && ny > -2.0f && ny < 2.0f) {
                  s_lock_sx += nx; s_lock_sy += ny; s_lock_n++;
                  if (!s_lock_valid) {        /* seed so frame 1 is usable */
                      s_lock_x = nx; s_lock_y = ny; s_lock_valid = 1;
                  }
              }
          }
      } }
    /* RSX_DBG_SCALE=<f>: multiply attribute-0 positions for the draws binding
     * RSX_DBG_VTX's texture. The duck's object-space bbox is ~0.2 units and its
     * draws rasterize only a few dozen pixels per frame -- sub-pixel. Growing it
     * about its own origin tests the rest of the chain (index buffer, texture,
     * per-draw transform): if a duck-shaped, duck-coloured object appears, only
     * the scale is wrong. Diagnostic. */
    { static const char* ds = (const char*)1; static float sc = 0.0f;
      if (ds == (const char*)1) { ds = getenv("RSX_DBG_SCALE");
                                  sc = ds ? (float)atof(ds) : 0.0f; }
      static const char* dv2 = (const char*)1; static u32 want2 = 0;
      if (dv2 == (const char*)1) { dv2 = getenv("RSX_DBG_VTX");
                                   want2 = dv2 ? (u32)strtoul(dv2, NULL, 16) : 0; }
      if (sc > 0.0f && want2 && s_d3d.cur_texs[0].raw == want2 && count) {
        /* Magnify about a FIXED point captured from the first tracked draw, not
         * about each chunk's own centre -- recentring per chunk would stack every
         * chunk of the mesh on top of the others. Combined with VP_BYPASS (which
         * maps attribute 0 straight to clip space) this renders the mesh's real
         * geometry and texture at a readable size. */
        /* RSX_DBG_RECENTER=1: recentre EVERY chunk on its own centroid, overlaying
         * them. If each chunk is one instance of the same mesh they align into a
         * single silhouette; if they are arbitrary slices of one big mesh they
         * smear. Either way the answer is visible at a glance. */
        static int recen = -1;
        if (recen < 0) { const char* e = getenv("RSX_DBG_RECENTER"); recen = e ? atoi(e) : 0; }
        static int have_c = 0; static float ox = 0, oy = 0, oz = 0;
        if (!have_c || recen) {
            double sx = 0, sy = 0, sz = 0;
            for (u32 k = 0; k < count; k++) {
                sx += out[k*16].v[0]; sy += out[k*16].v[1]; sz += out[k*16].v[2];
            }
            ox = (float)(sx/count); oy = (float)(sy/count); oz = (float)(sz/count);
            have_c = 1;
        }
        for (u32 k = 0; k < count; k++) {
            /* RSX_DBG_SHIFT="dx,dy": nudge after scaling. VP_BYPASS applies the
             * guest's posoffset, so a recentred mesh does not land at screen
             * centre -- without this the magnified geometry sits clipped against
             * the top edge. */
            static int shf = -1; static float dx = 0.0f, dy = 0.0f;
            if (shf < 0) { const char* e = getenv("RSX_DBG_SHIFT");
                           if (e) { double a=0,b=0; sscanf(e, "%lf,%lf", &a, &b);
                                    dx = (float)a; dy = (float)b; }
                           shf = 1; }
            out[k*16].v[0] = (out[k*16].v[0] - ox) * sc + dx;
            out[k*16].v[1] = (out[k*16].v[1] - oy) * sc + dy;
            /* Leave z alone: under VP_BYPASS the object z maps straight to clip
             * z, so scaling it walks the geometry out of the depth range and
             * everything clips away. */
            (void)oz;
        }
      } }
    /* RSX_DBG_VTX=<hex tex0 offset>: attribute-0 positions for the draws binding
     * that texture (see upload_tris_vp). The duck's meshes are indexed, so this
     * is the copy that actually fires for it. */
    { static const char* dv = (const char*)1; static u32 want = 0; static int n = 0;
      if (dv == (const char*)1) { dv = getenv("RSX_DBG_VTX");
                                  want = dv ? (u32)strtoul(dv, NULL, 16) : 0; }
      if (want && s_d3d.cur_texs[0].raw == want && n < 8) { n++;
        float mnx=1e30f,mny=1e30f,mnz=1e30f,mxx=-1e30f,mxy=-1e30f,mxz=-1e30f;
        for (u32 k = 0; k < count; k++) {
            float x=out[k*16].v[0], y=out[k*16].v[1], z=out[k*16].v[2];
            if (x<mnx)mnx=x; if (y<mny)mny=y; if (z<mnz)mnz=z;
            if (x>mxx)mxx=x; if (y>mxy)mxy=y; if (z>mxz)mxz=z;
        }
        const rsx_vertex_attrib* _a3 = &state->vertex_attribs[3];
        fprintf(stderr, "[DUCKVTX] a3(en=%d type=%u size=%u stride=%u off=0x%08X freq=%u)"
                        " -> col0 v0=(%.3f %.3f %.3f %.3f)%c",
                _a3->enabled, _a3->type, _a3->size, _a3->stride, _a3->offset,
                0u,
                out[3].v[0], out[3].v[1], out[3].v[2], out[3].v[3], 10);
        const rsx_vertex_attrib* _a0 = &state->vertex_attribs[0];
        fprintf(stderr, "[DUCKVTX] first=%u count=%u a0(type=%u size=%u stride=%u off=0x%08X)"
                        " obj-bbox x[%.3f,%.3f] y[%.3f,%.3f] z[%.3f,%.3f]%c",
                first, count, _a0->type, _a0->size, _a0->stride, _a0->offset,
                mnx,mxx, mny,mxy, mnz,mxz, 10);
        /* Project the object-space bbox corners through the first four transform
         * constants (the conventional MVP rows) and report NDC. Sub-pixel output
         * with correct vertices means the placement transform is the problem, and
         * the NDC extent says by how much. */
        { float cx=(mnx+mxx)*0.5f, cy=(mny+mxy)*0.5f, cz=(mnz+mxz)*0.5f;
          /* This title's Cg programs keep their MVP at c[256..259], not c[0..3]
           * (see the vp_uses_c03 note); projecting through c0 reported zeros. */
          static int MVP = -1;
          if (MVP < 0) { const char* e = getenv("MVP_BASE"); MVP = e ? atoi(e) : 256; }
          float v[4] = { cx, cy, cz, 1.0f };
          float clip[4];
          for (int r = 0; r < 4; r++) {
              const float* m = state->vertex_constants[MVP + r];
              clip[r] = m[0]*v[0] + m[1]*v[1] + m[2]*v[2] + m[3]*v[3];
          }
          float ex[4];
          { float e[4] = { mxx-cx, mxy-cy, mxz-cz, 0.0f };
            for (int r = 0; r < 4; r++) {
                const float* m = state->vertex_constants[MVP + r];
                ex[r] = m[0]*e[0] + m[1]*e[1] + m[2]*e[2];
            } }
          { int lo = -1, hi = -1, nz = 0;
            for (int r = 0; r < RSX_MAX_VERTEX_CONSTANTS; r++) {
                const float* m = state->vertex_constants[r];
                if (m[0] || m[1] || m[2] || m[3]) { if (lo < 0) lo = r; hi = r; nz++; }
            }
            fprintf(stderr, "[DUCKVTX]   constants: %d non-zero, slots %d..%d;", nz, lo, hi);
            for (int r = (lo < 0 ? 0 : lo); r < (lo < 0 ? 0 : lo) + 4 && r < RSX_MAX_VERTEX_CONSTANTS; r++)
                fprintf(stderr, " c%d=(%.3f %.3f %.3f %.3f)", r,
                        state->vertex_constants[r][0], state->vertex_constants[r][1],
                        state->vertex_constants[r][2], state->vertex_constants[r][3]);
            fprintf(stderr, "%c", 10); }
          fprintf(stderr, "[DUCKVTX]   mvp0=(%.3f %.3f %.3f %.3f) clip=(%.3f %.3f %.3f w=%.3f)",
                  state->vertex_constants[MVP][0], state->vertex_constants[MVP][1],
                  state->vertex_constants[MVP][2], state->vertex_constants[MVP][3],
                  clip[0], clip[1], clip[2], clip[3]);
          if (clip[3] > 1e-6f)
              fprintf(stderr, " ndc=(%.4f %.4f) half-extent-px=(%.2f %.2f)",
                      clip[0]/clip[3], clip[1]/clip[3],
                      ex[0]/clip[3]*640.0f, ex[1]/clip[3]*360.0f);
          fprintf(stderr, "%c", 10); }
      } }
    /* IDX_DBG=<N>: the first indices and the position they resolve to. An index
     * buffer read with the wrong element size or base yields huge indices and
     * degenerate triangles -- an INDEXED mesh vanishes while non-indexed
     * geometry in the same scene renders fine. */
    { static int cap = -1, n = 0;
      if (cap < 0) { const char* e = getenv("IDX_DBG"); cap = e ? atoi(e) : 0; }
      if (cap && n < cap && count >= 3) { n++;
        extern u32 cellGcmResolveOffset(u32);
        const rsx_vertex_attrib* a0 = &state->vertex_attribs[0];
        u32 vbase = cellGcmResolveOffset(a0->offset & 0x7FFFFFFFu);
        u32 nz = 0; for (u32 q = 0; q < 0x1000u; q += 29) if (vm_base[vbase + q]) nz++;
        fprintf(stderr, "[IDX] idxoff=0x%X idx: %u %u %u | a0 off=0x%08X(->0x%08X nz=%u/142)"
                        " stride=%u size=%u type=%u  pos0=(%g %g %g)%c",
                state->index_array_addr,
                read_guest_index(state, first), read_guest_index(state, first+1),
                read_guest_index(state, first+2),
                a0->offset, vbase, nz, a0->stride, a0->size, a0->type,
                out[0].v[0], out[0].v[1], out[0].v[2], 10); } }
    s_d3d.vp_vb_offset += count * VP_VERT_STRIDE;
    return count;
}

/* Indexed TRIANGLE_STRIP / TRIANGLE_FAN, expanded CPU-side into a triangle
 * list (the whole VP path draws lists; CullMode is NONE so strip parity
 * winding doesn't matter). DeferredShading's spline tube meshes -- the
 * octopus tentacles -- are indexed strips. */
static u32 upload_strip_vp_indexed_wk85(const rsx_state* state, u32 first, u32 count, int fan);
static u32 upload_strip_vp_indexed(const rsx_state* state, u32 first, u32 count, int fan) { const unsigned long long t_ = WK85_T0(); u32 r_ = upload_strip_vp_indexed_wk85(state, first, count, fan); WK85_ADD(WK85_vtx, t_); return r_; }   /* winport v85a-walkprof */
static u32 upload_strip_vp_indexed_wk85(const rsx_state* state, u32 first, u32 count, int fan)
{
    extern uint8_t* vm_base;
    if (!state || !vm_base || !s_d3d.vp_vb_mapped) return 0;
    if (!state->vertex_attribs[0].enabled) return 0;
    if (count < 3) return 0;
    u32 tris = count - 2;
    u32 maxv = (MAX_VERTICES * VP_VERT_STRIDE - s_d3d.vp_vb_offset) / VP_VERT_STRIDE;
    if (tris * 3 > maxv) tris = maxv / 3;
    VPSlot* out = (VPSlot*)((u8*)s_d3d.vp_vb_mapped
        + (u64)s_d3d.vp_parity * MAX_VERTICES * VP_VERT_STRIDE + s_d3d.vp_vb_offset);
    u32 o = 0;
    VPIdx vpx_; vpi_prepare(state, &vpx_);   /* winport-fix: fast vertex fetch */
    u32 maxi_ = 0; if (wp85_idx_on()) maxi_ = wp85_maxidx(vpx_.p, vpx_.is16, first, count); else   /* winport v85c-walkfast */
      for (u32 k = 0; k < count; k++) { u32 ix_ = vpi_get(&vpx_, first + k); if (ix_ > maxi_) maxi_ = ix_; }
      if (wp85_idx_on() && wp85_verify()) { u32 m2_ = 0; for (u32 k = 0; k < count; k++) { u32 ix_ = vpi_get(&vpx_, first + k); if (ix_ > m2_) m2_ = ix_; } wp85_vcount(1, m2_ != maxi_); }
    VPFetch vpf_; int fast_ = vpf_prepare(state, &vpf_, maxi_);
    for (u32 t = 0; t < tris; t++) {
        u32 i0 = fan ? 0 : t;
        u32 i1 = t + 1, i2 = t + 2;   /* winport-fix: indexed strip winding parity */
        if (!fan && (t & 1)) { u32 tmp = i1; i1 = i2; i2 = tmp; }
        VPF_READ(vpf_, fast_, state, vpi_get(&vpx_, first + i0), &out[o*16]); o++;
        VPF_READ(vpf_, fast_, state, vpi_get(&vpx_, first + i1), &out[o*16]); o++;
        VPF_READ(vpf_, fast_, state, vpi_get(&vpx_, first + i2), &out[o*16]); o++;
    }
    s_d3d.vp_vb_offset += o * VP_VERT_STRIDE;
    return o;
}

/* winport v38: may a new DRAW batch be appended to the previous record pv (the 256-vertex batch merge below)?
 * The record's vertex constants were snapshotted when it was created (vp_record_cb). The engine already flushes
 * a pending batch when a vertex constant is written inside BEGIN/END (rsx_commands.c PS3_RSX_CONST_BARRIER: GoW2
 * uploads each skinned sub-mesh's bone palette between DRAW_INDEX_ARRAYs of one BEGIN/END), but the next batch is
 * contiguous and in the same BEGIN, so this merge glued it back onto the old record and the second sub-mesh was
 * skinned with the PREVIOUS palette (wrong bone matrices -> vertices thrown across the screen).
 * WINPORT_MERGE_CB=1: do not merge across a vertex-constant upload (when the previous record ends on a whole
 * triangle); 0 (default) = old behaviour. Both modes log [MERGECB] so a run shows whether it happens at all. */
static int wp_merge_ok(D3D12DrawRecord* pv, u32 primitive, u32 first, u32 count)
{
    static int mode = -1;
    if (mode < 0) { const char* e = getenv("WINPORT_MERGE_CB"); mode = e ? (atoi(e) != 0) : 0; }
    const rsx_state* st = s_d3d.current_rsx_state;
    int chg = st ? (st->vertex_constants_dirty != 0) : 0;
    if (pv->wp_merge_n < 0xFFFFu) pv->wp_merge_n++;
    if (!chg) return 1;
    int block = mode && (pv->vertex_count % 3u) == 0u;
    static unsigned long n = 0; ++n;
    if (n <= 16 || (n & (n - 1)) == 0)
        fprintf(stderr, "[MERGECB] #%lu batch appended after a vertex-constant upload: prim=%u first=%u count=%u "
                        "rec_verts=%u vs=%d fp=0x%08X consts c%u..c%u -> %s\n",
                n, primitive, first, count, pv->vertex_count, pv->vs_idx, pv->fp_addr,
                st->vertex_constants_lo, st->vertex_constants_hi,
                block ? "NEW RECORD (WINPORT_MERGE_CB=1)" : "merged with the OLD constants (old behaviour)");
    if (block) { pv->wp_merge_n--; return 0; }
    if (pv->wp_merge_cbchg < 0xFFFFu) pv->wp_merge_cbchg++;
    return 1;
}

/* winport v42: WINPORT_RT0DBG=1 -- why did a non-display-size draw get rt_off 0 (= backbuffer)? */
static void wp_rt0_dbg(u32 rt_off, u32 prim, u32 count, const char* where)
{
    static int on = -1; if (on < 0) on = getenv("WINPORT_RT0DBG") ? 1 : 0;
    const rsx_state* st = s_d3d.current_rsx_state;
    if (!on || rt_off != 0 || !st || st->viewport_w == s_d3d.width) return;
    extern int cellGcmOffsetIsDisplay(u32 offset);
    static unsigned long n = 0; ++n;
    if (n <= 60 || (n & (n - 1)) == 0)
        fprintf(stderr, "[RT0] #%lu %s prim=%u n=%u fp=0x%08X vp=%ux%u tgt=0x%X off=%08X/%08X/%08X/%08X disp=%d clip=%ux%u zeta=0x%X fmt=0x%X in_be=%d be=%u\n",
                n, where, prim, count, st->shader_program, st->viewport_w, st->viewport_h, st->color_target,
                st->surface_color_offset[0], st->surface_color_offset[1], st->surface_color_offset[2], st->surface_color_offset[3],
                cellGcmOffsetIsDisplay(st->surface_color_offset[(st->color_target == 2) ? 1 : 0]),
                st->surface_clip_w, st->surface_clip_h, st->surface_zeta_offset, st->surface_format, st->in_begin_end, st->begin_end_id);
}
static void d3d12_draw_arrays_wk85(void* ud, u32 primitive, u32 first, u32 count);
static void d3d12_draw_arrays(void* ud, u32 primitive, u32 first, u32 count) { const unsigned long long t_ = WK85_T0(); d3d12_draw_arrays_wk85(ud, primitive, first, count); WK85_ADD(WK85_drawA, t_); }   /* winport v85a-walkprof */
static void d3d12_draw_arrays_wk85(void* ud, u32 primitive, u32 first, u32 count)
{
    if (count && count <= MAX_VERTICES) wp_room_for(count * 3u + 6u, 1);   /* winport-fix: flush a full batch instead of dropping draws */
    /* RSX_LIVE_FEED_DBG=1: pair with the [feed] method counts so "the FIFO
     * carries no draw batches" can be checked against "the backend is told
     * to draw" in the same run. */
    { static int dbg = -1; if (dbg < 0) dbg = getenv("RSX_LIVE_FEED_DBG") ? 1 : 0;
      if (dbg) { static long long n = 0;
          if (((++n) % 20000) == 0)
              printf("[backend] arrays callbacks=%lld (prim=%u count=%u)\n", n, primitive, count), fflush(stdout); } }
    (void)ud;
    /* Log the first 20 calls in detail, then every 1000th to show liveness
     * without flooding. */
    static u64 s_total = 0;
    if (s_total < 20 || (s_total % 1000) == 0) {
        if (!wp_stats_on()) ; else printf("[D3D12] draw_arrays #%llu prim=%u first=%u count=%u\n",
               (unsigned long long)s_total, primitive, first, count);
    }
    s_total++;

    if (!s_d3d.pipeline_ready || !s_d3d.vb_mapped) return;
    if (count == 0 || count > MAX_VERTICES) return;

    /* QUADS (prim 8) have no D3D12 topology; expand to a triangle list.
     * dbgfont draws all text as quads. Prefer the real vertex-program path
     * (exact position + texcoord): upload raw float4 attrib0 into vp_vb and
     * mark the draw is_vp; render_frame compiles the VP and draws it. Fall
     * back to the frac-approximation textured path if VP resources are absent. */
    if (primitive == RSX_PRIMITIVE_QUADS) {
        /* Prefer the real vertex-program path whenever VP resources exist --
         * NOT only when a texture is bound. Requiring tex_bound routed vkcube's
         * UNtextured cube to the fixed-function fallback, which never applies the
         * VP's MVP transform, so the cube was drawn in object space and clipped
         * off-screen. Untextured VP draws now render via the colour PSO. */
        if (s_d3d.vp_vb_mapped && s_d3d.vp_root_sig) {
            u32 rec = s_d3d.vp_vb_offset;
            u32 emitted = upload_quads_vp(s_d3d.current_rsx_state, first, count);
            if (emitted && s_d3d.draw_count < MAX_DRAWS) {
                D3D12DrawRecord* dr = &s_d3d.draws[s_d3d.draw_count];
                dr->vb_byte_offset = rec;
                dr->vertex_count   = emitted;
                dr->topology       = RSX_TOPOLOGY_TRIANGLES;
                dr->textured       = s_d3d.tex_bound;
                dr->is_vp          = 1;
                dr->fp_addr = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->shader_program : 0;
                dr->fp_exp32 = s_d3d.current_rsx_state ?
                    WP_FP_CTRL_MODE(s_d3d.current_rsx_state) : 0;   /* winport-fix: real SHADER_CONTROL for FP exports */
                dr->cull = rsx_cull_key(s_d3d.current_rsx_state);
        dr->begin_epoch = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->begin_end_id : 0;
            dr->begin_epoch = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->begin_end_id : 0;
                dr->cmask = 0xF;
                if (s_d3d.current_rsx_state) {
                    u32 _cm = s_d3d.current_rsx_state->color_mask;
                    dr->cmask = ((_cm & 0x00010000) ? 1u : 0u)   /* R */
                              | ((_cm & 0x00000100) ? 2u : 0u)   /* G */
                              | ((_cm & 0x00000001) ? 4u : 0u)   /* B */
                              | ((_cm & 0x01000000) ? 8u : 0u);  /* A */
                }
                dr->alpha_ctl = 0;
                if (s_d3d.current_rsx_state)
                    dr->alpha_ctl = ((s_d3d.current_rsx_state->alpha_test_enable ? 1u : 0u) << 16)
                                  | ((s_d3d.current_rsx_state->alpha_func & 0xFFu) << 8)
                                  | (s_d3d.current_rsx_state->alpha_ref & 0xFFu);
                for (int _u = 0; _u < 4; _u++) {
                    dr->tex[_u].off = s_d3d.cur_texs[_u].off;
                    dr->tex[_u].raw = s_d3d.cur_texs[_u].raw;
                    dr->tex[_u].w   = s_d3d.cur_texs[_u].w;
                    dr->tex[_u].h   = s_d3d.cur_texs[_u].h;
                    dr->tex[_u].fmt = s_d3d.cur_texs[_u].fmt;
                    dr->tex[_u].ctrl1 = s_d3d.cur_texs[_u].ctrl1;
                    dr->tex[_u].samp = s_d3d.cur_texs[_u].samp;   /* winport-fix: guest sampler state */
                    dr->tex[_u].cube  = s_d3d.cur_texs[_u].cube;
                    dr->tex[_u].mips  = s_d3d.cur_texs[_u].mips;
                    dr->tex[_u].set = s_d3d.cur_texs[_u].set;
                    dr->tex_rt[_u]  = -1;
                }
                dr->tex_slot = -1;
                dr->vs_idx = vp_get_vs(s_d3d.current_rsx_state);
                dr->is_clear = 0;
                dr->blend = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->blend_enable : 1;
                dr->blend_key = rsx_blend_key(s_d3d.current_rsx_state, dr->blend);
                if (getenv("BLENDDBG")) { static u32 seen[32]; static int ns=0;
                    u32 k = (dr->blend ? 0x80000000u : 0u) | dr->blend_key;
                    int f=0; for (int i=0;i<ns;i++) if (seen[i]==k) f=1;
                    if (!f && ns<32) { seen[ns++]=k;
                        fprintf(stderr, "[BLEND] enable=%d key=0x%X%c",
                                dr->blend, dr->blend_key, 10); } }
                dr->rt_off = current_rt_off(&dr->rt_w, &dr->rt_h, dr->rt_mrt); wp_rt0_dbg(dr->rt_off, primitive, count, "arrays"); wp94_rec(dr);   /* winport v94a-ffhaze */   /* winport v42 */
                dr->rt_fmt = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->surface_format : 0; dr->zeta_off = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->surface_zeta_offset : 0; dr->zeta_pitch = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->surface_zeta_pitch : 0;
                if (s_d3d.current_rsx_state) {
                    dr->vp_x = s_d3d.current_rsx_state->viewport_x;
                    dr->vp_y = s_d3d.current_rsx_state->viewport_y;
                    dr->vp_w = s_d3d.current_rsx_state->viewport_w;
                    dr->vp_h = s_d3d.current_rsx_state->viewport_h;
                } else { dr->vp_x = dr->vp_y = dr->vp_w = dr->vp_h = 0; }
                dr->cb_slot = s_d3d.draw_count;
                vp_record_cb(s_d3d.draw_count, dr->vs_idx, dr);
                s_d3d.draw_count++;
            }
            return;
        }
        u32 record_offset = s_d3d.vb_offset;
        u32 emitted = upload_quads_from_rsx(first, count);
        if (emitted == 0) return;
        if (s_d3d.draw_count < MAX_DRAWS) {
            s_d3d.draws[s_d3d.draw_count].vb_byte_offset = record_offset;
            s_d3d.draws[s_d3d.draw_count].vertex_count   = emitted;
            s_d3d.draws[s_d3d.draw_count].topology       = RSX_TOPOLOGY_TRIANGLES;
            s_d3d.draws[s_d3d.draw_count].textured       = s_d3d.tex_bound;
            s_d3d.draws[s_d3d.draw_count].is_vp          = 0;
            s_d3d.draws[s_d3d.draw_count].is_clear       = 0;
            s_d3d.draws[s_d3d.draw_count].rt_off         = 0;
            s_d3d.draw_count++;
        }
        return;
    }

    /* TRIANGLES (prim 5): same VP-path preference as quads -- the guest's
     * vertex program does the MVP transform (gcm/cube draws its cube this way);
     * the fixed-function fallback below applies no transform, so 3D geometry
     * ends up in object space (invisible/garbage). */
    /* Route TRIANGLES/STRIP/FAN through the VP path (sagemono: LBP draws
     * non-indexed strips/fans), but ONLY when the guest ACTUALLY HAS a vertex
     * program (flOw). The VP path transforms via the guest's VP microcode; with
     * no microcode loaded it transforms nothing and the draw silently produces
     * zero pixels (it also uploads to vp_vb, leaving the fixed-function vb empty).
     * Geometry already in clip space with no VP -- flOw's injected scene -- must
     * go down the fixed-function passthrough below instead. */
    u32 wp_ln = 0;   /* winport v42: WINPORT_LINE_FIX -- RSX topology of a line/point draw sent down the VP path */
    if ((primitive == RSX_PRIMITIVE_LINES || primitive == RSX_PRIMITIVE_LINE_STRIP || primitive == RSX_PRIMITIVE_POINTS) &&
        wp_line_fix() && s_d3d.vp_vb_mapped && s_d3d.vp_root_sig &&
        s_d3d.current_rsx_state && (s_d3d.current_rsx_state->transform_program_words * 4u) >= 16) {
        { static unsigned long n_ = 0; ++n_; if (n_ <= 8 || (n_ & (n_ - 1)) == 0)
            fprintf(stderr, "[LINEFIX] #%lu prim=%u first=%u count=%u fp=0x%08X mode=%d\n", n_, primitive, first, count,
                    s_d3d.current_rsx_state->shader_program, wp_line_fix()); }
        if (wp_line_fix() == 2) { s_d3d.merge_prev_draw = 0; return; }
        wp_ln = (u32)rsx_primitive_topology(primitive);
    }
    if ((primitive == RSX_PRIMITIVE_TRIANGLES || primitive == RSX_PRIMITIVE_TRIANGLE_STRIP
         || primitive == RSX_PRIMITIVE_TRIANGLE_FAN || wp_ln) &&
        s_d3d.vp_vb_mapped && s_d3d.vp_root_sig &&
        s_d3d.current_rsx_state && (s_d3d.current_rsx_state->transform_program_words * 4u) >= 16) {
        u32 rec = s_d3d.vp_vb_offset;
        u32 emitted = (primitive == RSX_PRIMITIVE_TRIANGLES || wp_ln)   /* winport v42: lines/points = plain vertex list */
            ? upload_tris_vp(s_d3d.current_rsx_state, first, count)
            : upload_strip_vp(s_d3d.current_rsx_state, first, count, primitive == RSX_PRIMITIVE_TRIANGLE_FAN);
        /* RSX splits one primitive stream across several DRAW_ARRAYS entries
         * inside a single BEGIN/END (each carries at most 256 vertices), and the
         * hardware concatenates them before assembling primitives. Recording a
         * draw per batch instead regroups the triangles: this title's tub mesh
         * arrives as 128 then 256,256,..., neither a multiple of 3, so every
         * batch after the first was assembled one vertex out of phase -- which
         * rendered as spike triangles with a zero texcoord along the tub rim.
         * The batches land contiguously in the vertex buffer, so extending the
         * previous record is all that is needed to restore the stream. */
        if (emitted && s_d3d.merge_prev_draw && s_d3d.draw_count > 0 &&
            s_d3d.merge_first_end == first) {
            D3D12DrawRecord* pv = &s_d3d.draws[s_d3d.draw_count - 1];
            u32 fpnow = s_d3d.current_rsx_state
                        ? s_d3d.current_rsx_state->shader_program : 0;
            if (pv->is_vp && pv->topology == RSX_TOPOLOGY_TRIANGLES &&
                pv->fp_addr == fpnow && primitive == RSX_PRIMITIVE_TRIANGLES &&
                pv->begin_epoch == (s_d3d.current_rsx_state
                                    ? s_d3d.current_rsx_state->begin_end_id : 0) &&
                pv->vb_byte_offset + pv->vertex_count * VP_VERT_STRIDE == rec &&
                wp_merge_ok(pv, primitive, first, count)) {   /* winport v38 */
                pv->vertex_count += emitted;
                s_d3d.merge_first_end = first + count;
                return;
            }
        }
        if (emitted && s_d3d.draw_count < MAX_DRAWS) {
            D3D12DrawRecord* dr = &s_d3d.draws[s_d3d.draw_count];
            dr->vb_byte_offset = rec;
            dr->vertex_count   = emitted;
            dr->topology       = wp_ln ? wp_ln : RSX_TOPOLOGY_TRIANGLES;   /* winport v42 */
            dr->textured       = s_d3d.tex_bound;
            dr->is_vp          = 1;
            dr->fp_addr = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->shader_program : 0;
            dr->fp_exp32 = s_d3d.current_rsx_state ?
                WP_FP_CTRL_MODE(s_d3d.current_rsx_state) : 0;   /* winport-fix: real SHADER_CONTROL for FP exports */
            dr->cull = rsx_cull_key(s_d3d.current_rsx_state);
        dr->begin_epoch = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->begin_end_id : 0;
            dr->begin_epoch = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->begin_end_id : 0;
            dr->cmask = 0xF;
            if (s_d3d.current_rsx_state) {
                u32 _cm = s_d3d.current_rsx_state->color_mask;
                dr->cmask = ((_cm & 0x00010000) ? 1u : 0u)   /* R */
                          | ((_cm & 0x00000100) ? 2u : 0u)   /* G */
                          | ((_cm & 0x00000001) ? 4u : 0u)   /* B */
                          | ((_cm & 0x01000000) ? 8u : 0u);  /* A */
            }
            dr->alpha_ctl = 0;
            if (s_d3d.current_rsx_state)
                dr->alpha_ctl = ((s_d3d.current_rsx_state->alpha_test_enable ? 1u : 0u) << 16)
                              | ((s_d3d.current_rsx_state->alpha_func & 0xFFu) << 8)
                              | (s_d3d.current_rsx_state->alpha_ref & 0xFFu);
            for (int _u = 0; _u < 4; _u++) {
                dr->tex[_u].off = s_d3d.cur_texs[_u].off;
                dr->tex[_u].raw = s_d3d.cur_texs[_u].raw;
                dr->tex[_u].w   = s_d3d.cur_texs[_u].w;
                dr->tex[_u].h   = s_d3d.cur_texs[_u].h;
                dr->tex[_u].fmt = s_d3d.cur_texs[_u].fmt;
                    dr->tex[_u].ctrl1 = s_d3d.cur_texs[_u].ctrl1;
                    dr->tex[_u].samp = s_d3d.cur_texs[_u].samp;   /* winport-fix: guest sampler state */
                    dr->tex[_u].cube  = s_d3d.cur_texs[_u].cube;
                    dr->tex[_u].mips  = s_d3d.cur_texs[_u].mips;
                dr->tex[_u].set = s_d3d.cur_texs[_u].set;
                dr->tex_rt[_u]  = -1;
            }
            dr->tex_slot = -1;
            dr->vs_idx = vp_get_vs(s_d3d.current_rsx_state);
            dr->is_clear = 0;
            dr->blend = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->blend_enable : 1;
            dr->blend_key = rsx_blend_key(s_d3d.current_rsx_state, dr->blend);
            dr->rt_off = current_rt_off(&dr->rt_w, &dr->rt_h, dr->rt_mrt); wp_rt0_dbg(dr->rt_off, primitive, count, "arrays2"); wp94_rec(dr);   /* winport v94a-ffhaze */   /* winport v42 */
            dr->rt_fmt = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->surface_format : 0; dr->zeta_off = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->surface_zeta_offset : 0; dr->zeta_pitch = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->surface_zeta_pitch : 0;
            if (s_d3d.current_rsx_state) {
                dr->vp_x = s_d3d.current_rsx_state->viewport_x;
                dr->vp_y = s_d3d.current_rsx_state->viewport_y;
                dr->vp_w = s_d3d.current_rsx_state->viewport_w;
                dr->vp_h = s_d3d.current_rsx_state->viewport_h;
            } else { dr->vp_x = dr->vp_y = dr->vp_w = dr->vp_h = 0; }
            dr->cb_slot = s_d3d.draw_count;
                vp_record_cb(s_d3d.draw_count, dr->vs_idx, dr);
            s_d3d.draw_count++;
            /* Anchor for merging the next DRAW_ARRAYS batch of this stream. */
            s_d3d.merge_prev_draw  = (primitive == RSX_PRIMITIVE_TRIANGLES);
            s_d3d.merge_first_end  = first + count;
        }
        return;
    }
    s_d3d.merge_prev_draw = 0;   /* any other primitive breaks the stream */

    u32 topo = (u32)rsx_primitive_topology(primitive);
    if (topo == RSX_TOPOLOGY_UNSUPPORTED) {
        /* Other primitives still needing index-buffer conversion
         * (line loops, triangle fans) — skip rather than draw wrong. */
        static int s_skipped_nontri = 0;
        if (s_skipped_nontri < 3) {
            printf("[D3D12] draw_arrays: skipping prim=%u (needs index conversion)\n",
                   primitive);
            s_skipped_nontri++;
        }
        return;
    }

    u32 record_offset = s_d3d.vb_offset;
    u32 actual_count  = upload_vertices_from_rsx(first, count);
    if (actual_count == 0) return;

    if (s_d3d.draw_count < MAX_DRAWS) {
        s_d3d.draws[s_d3d.draw_count].vb_byte_offset = record_offset;
        s_d3d.draws[s_d3d.draw_count].vertex_count   = actual_count;
        s_d3d.draws[s_d3d.draw_count].topology       = topo;
        s_d3d.draws[s_d3d.draw_count].textured       = 0;
        s_d3d.draws[s_d3d.draw_count].is_vp          = 0;
        s_d3d.draws[s_d3d.draw_count].is_clear       = 0;
        s_d3d.draws[s_d3d.draw_count].rt_off         = 0;
        s_d3d.draw_count++;
    }
}

static void d3d12_draw_indexed_wk85(void* ud, u32 primitive, u32 first, u32 count);
static void d3d12_draw_indexed(void* ud, u32 primitive, u32 first, u32 count) { const unsigned long long t_ = WK85_T0(); d3d12_draw_indexed_wk85(ud, primitive, first, count); WK85_ADD(WK85_drawI, t_); }   /* winport v85a-walkprof */
static void d3d12_draw_indexed_wk85(void* ud, u32 primitive, u32 first, u32 count)
{
    s85_pending_ib = 0;   /* winport v85e-idxdraw */
    if (count && count <= MAX_VERTICES) wp_room_for(count * 3u + 6u, 1);   /* winport-fix: flush a full batch instead of dropping draws */
    /* RSX_LIVE_FEED_DBG=1: pair with the [feed] method counts so "the FIFO
     * carries no draw batches" can be checked against "the backend is told
     * to draw" in the same run. */
    { static int dbg = -1; if (dbg < 0) dbg = getenv("RSX_LIVE_FEED_DBG") ? 1 : 0;
      if (dbg) { static long long n = 0;
          if (((++n) % 20000) == 0)
              printf("[backend] indexed callbacks=%lld (prim=%u count=%u)\n", n, primitive, count), fflush(stdout); } }
    (void)ud;
    static int log_count = 0;
    if (log_count < 8) {
        printf("[D3D12] draw_indexed(prim=%u, first=%u, count=%u)\n",
               primitive, first, count);
        log_count++;
    }
    if (!s_d3d.pipeline_ready) return;
    if (count == 0 || count > MAX_VERTICES) return;
    if (!s_d3d.vp_vb_mapped || !s_d3d.vp_root_sig) return;

    /* Expand through the VP path (indices resolved CPU-side): QUADS -> two
     * triangles per quad, TRIANGLES straight through, STRIP/FAN -> triangle
     * list. Other primitives are skipped rather than drawn wrong. */
    u32 emitted = 0;
    u32 rec = s_d3d.vp_vb_offset;
    if (primitive == RSX_PRIMITIVE_QUADS)             emitted = upload_quads_vp_indexed(s_d3d.current_rsx_state, first, count);
    else if (primitive == RSX_PRIMITIVE_TRIANGLES)    emitted = upload_tris_vp_indexed(s_d3d.current_rsx_state, first, count);
    else if (primitive == RSX_PRIMITIVE_TRIANGLE_STRIP) emitted = upload_strip_vp_indexed(s_d3d.current_rsx_state, first, count, 0);
    else if (primitive == RSX_PRIMITIVE_TRIANGLE_FAN)   emitted = upload_strip_vp_indexed(s_d3d.current_rsx_state, first, count, 1);
    else {
        static int _skip = 0;
        if (_skip++ < 3)
            printf("[D3D12] draw_indexed: skipping prim=%u (not wired)\n", primitive);
        return;
    }
    /* Same BEGIN/END batch concatenation as the non-indexed path: this title's
     * fluid arrives as ~3000 DRAW_INDEX_ARRAY batches of 256, and 256 is not a
     * multiple of 3, so every batch after the first assembled out of phase. */
    if (emitted && s_d3d.merge_prev_draw && s_d3d.draw_count > 0 &&
        s_d3d.merge_first_end == first && primitive == RSX_PRIMITIVE_TRIANGLES) {
        D3D12DrawRecord* pv = &s_d3d.draws[s_d3d.draw_count - 1];
        u32 fpnow = s_d3d.current_rsx_state
                    ? s_d3d.current_rsx_state->shader_program : 0;
        if (pv->is_vp && pv->topology == RSX_TOPOLOGY_TRIANGLES &&
            pv->fp_addr == fpnow &&
            pv->begin_epoch == (s_d3d.current_rsx_state
                                ? s_d3d.current_rsx_state->begin_end_id : 0) &&
            pv->vb_byte_offset + pv->vertex_count * VP_VERT_STRIDE == rec &&
            wp_merge_ok(pv, primitive, first, count)) {   /* winport v38 */
            pv->vertex_count += emitted;
            s_d3d.merge_first_end = first + count;
            return;
        }
    }
    if (emitted && s_d3d.draw_count < MAX_DRAWS) {
        D3D12DrawRecord* dr = &s_d3d.draws[s_d3d.draw_count];
        dr->vb_byte_offset = rec;
        dr->vertex_count   = emitted;
        dr->topology       = RSX_TOPOLOGY_TRIANGLES;
        dr->textured       = s_d3d.tex_bound;
        dr->is_vp          = 1;
        dr->fp_addr = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->shader_program : 0;
        dr->fp_exp32 = s_d3d.current_rsx_state ?
            WP_FP_CTRL_MODE(s_d3d.current_rsx_state) : 0;   /* winport-fix: real SHADER_CONTROL for FP exports */
        dr->cull = rsx_cull_key(s_d3d.current_rsx_state);
        dr->begin_epoch = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->begin_end_id : 0;
        dr->cmask = 0xF;
        if (s_d3d.current_rsx_state) {
            u32 _cm = s_d3d.current_rsx_state->color_mask;
            dr->cmask = ((_cm & 0x00010000) ? 1u : 0u)   /* R */
                      | ((_cm & 0x00000100) ? 2u : 0u)   /* G */
                      | ((_cm & 0x00000001) ? 4u : 0u)   /* B */
                      | ((_cm & 0x01000000) ? 8u : 0u);  /* A */
        }
        dr->alpha_ctl = 0;
        if (s_d3d.current_rsx_state)
            dr->alpha_ctl = ((s_d3d.current_rsx_state->alpha_test_enable ? 1u : 0u) << 16)
                          | ((s_d3d.current_rsx_state->alpha_func & 0xFFu) << 8)
                          | (s_d3d.current_rsx_state->alpha_ref & 0xFFu);
        for (int _u = 0; _u < 4; _u++) {
            dr->tex[_u].off = s_d3d.cur_texs[_u].off;
            dr->tex[_u].raw = s_d3d.cur_texs[_u].raw;
            dr->tex[_u].w   = s_d3d.cur_texs[_u].w;
            dr->tex[_u].h   = s_d3d.cur_texs[_u].h;
            dr->tex[_u].fmt = s_d3d.cur_texs[_u].fmt;
                    dr->tex[_u].ctrl1 = s_d3d.cur_texs[_u].ctrl1;
                    dr->tex[_u].samp = s_d3d.cur_texs[_u].samp;   /* winport-fix: guest sampler state */
                    dr->tex[_u].cube  = s_d3d.cur_texs[_u].cube;
                    dr->tex[_u].mips  = s_d3d.cur_texs[_u].mips;
            dr->tex[_u].set = s_d3d.cur_texs[_u].set;
            dr->tex_rt[_u]  = -1;
        }
        dr->tex_slot = -1;
        dr->vs_idx = vp_get_vs(s_d3d.current_rsx_state);
        dr->is_clear = 0;
        dr->blend = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->blend_enable : 1;
        dr->blend_key = rsx_blend_key(s_d3d.current_rsx_state, dr->blend);
        dr->rt_off = current_rt_off(&dr->rt_w, &dr->rt_h, dr->rt_mrt); wp_rt0_dbg(dr->rt_off, primitive, count, "indexed"); wp94_rec(dr);   /* winport v94a-ffhaze */   /* winport v42 */
        dr->rt_fmt = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->surface_format : 0; dr->zeta_off = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->surface_zeta_offset : 0; dr->zeta_pitch = s_d3d.current_rsx_state ? s_d3d.current_rsx_state->surface_zeta_pitch : 0;
        if (s_d3d.current_rsx_state) {
            dr->vp_x = s_d3d.current_rsx_state->viewport_x;
            dr->vp_y = s_d3d.current_rsx_state->viewport_y;
            dr->vp_w = s_d3d.current_rsx_state->viewport_w;
            dr->vp_h = s_d3d.current_rsx_state->viewport_h;
        } else { dr->vp_x = dr->vp_y = dr->vp_w = dr->vp_h = 0; }
        dr->cb_slot = s_d3d.draw_count;
        dr->wp85_ib = s85_pending_ib; s85_pending_ib = 0;   /* winport v85e-idxdraw */
                vp_record_cb(s_d3d.draw_count, dr->vs_idx, dr);
        s_d3d.draw_count++;
        /* Anchor for merging the next batch of this indexed stream. */
        s_d3d.merge_prev_draw = (primitive == RSX_PRIMITIVE_TRIANGLES) && !dr->wp85_ib;   /* winport v85e-idxdraw: never merge into an indexed record */
        s_d3d.merge_first_end = first + count;
    }
}

static void d3d12_bind_texture_wk85(void* ud, u32 unit, const rsx_texture_state* tex);
static void d3d12_bind_texture(void* ud, u32 unit, const rsx_texture_state* tex) { const unsigned long long t_ = WK85_T0(); d3d12_bind_texture_wk85(ud, unit, tex); WK85_ADD(WK85_bind, t_); }   /* winport v85a-walkprof */
static void d3d12_bind_texture_wk85(void* ud, u32 unit, const rsx_texture_state* tex)
{
    (void)ud;
    extern uint8_t* vm_base;
    extern u32 cellGcmResolveOffset(u32);

    u32 width  = (tex->image_rect >> 16) & 0xFFFF;
    u32 height = tex->image_rect & 0xFFFF;
    u32 format = (tex->format >> 8) & 0xFF;
    u32 offset = tex->offset;
    { static int td = -1; static u32 seen[128][2]; static int ns = 0;   /* winport-fix: WINPORT_TEXDBG */
      if (td < 0) { const char* e = getenv("WINPORT_TEXDBG"); td = e ? atoi(e) : 0; }
      if (td && ns < td && ns < 128) {
          u32 k0 = (unit << 28) ^ (format << 16) ^ (tex->format & 3u), k1 = (width << 16) | height;
          int f = 0; for (int i = 0; i < ns; i++) if (seen[i][0] == k0 && seen[i][1] == k1) f = 1;
          if (!f) { seen[ns][0] = k0; seen[ns][1] = k1; ns++;
              u32 bf = format & 0x9F;
              int ok = (bf == 0x81 || bf == 0x85 || bf == 0x8B || (bf >= 0x86 && bf <= 0x88) || bf == 0x9A || bf == 0x9E || bf == 0x82 || bf == 0x83 || bf == 0x84 || bf == 0x97 || bf == 0x9D);
              printf("[TEXBIND] unit=%u fmt=0x%02X %ux%u loc=%u off=0x%X ctrl1=0x%08X %s\n",
                     unit, format, width, height, tex->format & 3u, offset, tex->control1,
                     ok ? "ok" : "UNSUPPORTED (unit keeps previous texture)"); } } }
    /* CUBEDBG=1: NV4097_SET_TEXTURE_FORMAT bit 2 is the cubemap flag, and the
     * line above throws it away with the rest of the low byte. Nothing in this
     * backend handles cube textures, so a cubemap bound for an environment
     * reflection is sampled as a plain 2D image -- which is what the chrome
     * faucet's black patches and banded escutcheons look like. */
    { static int cd = -1;
      if (cd < 0) { const char* e = getenv("CUBEDBG"); cd = e ? atoi(e) : 0; }
      if (cd && (tex->format & 4)) {
          static u32 seen[8]; static int ns = 0; int known = 0;
          for (int k = 0; k < ns; k++) if (seen[k] == offset) known = 1;
          if (!known && ns < 8) { seen[ns++] = offset;
              fprintf(stderr, "[CUBE] unit=%u offset=0x%08X %ux%u fmt=0x%02X"
                              " raw_format=0x%08X (cubemap bit SET)%c",
                      unit, offset, width, height, format, tex->format, 10); }
      } }

    static int log_count = 0;
    if (log_count < 10) {
        printf("[D3D12] bind_texture(unit=%u, offset=0x%X, fmt=0x%02X, %ux%u)\n",
               unit, offset, format, width, height);
        log_count++;
    }
    /* MOVIE_BIND=1: trace movie-plane binds (640x360 Y / 320x180 U/V) with the
     * resolved EA + a content probe -- used to diagnose the Bink frame-buffer
     * ring mismatch (the draw binds a cleared buffer 0x4D80 before the one the
     * decoder actually fills). */
    if (getenv("MOVIE_BIND") &&
        ((width == 640 && height == 360) || (width == 320 && height == 180))) {
        extern u32 cellGcmResolveLocated(int local, u32 offset);
        static int _mv = 0; if (_mv++ < 24) {
            /* Resolve the SAME raw offset both ways -- as LOCAL (VRAM) and as MAIN
             * (IO-mapped) -- and count nonzero bytes over the whole plane for each.
             * If the plane the game declares (loc bits fmt&3) is zero but the OTHER
             * pool has content, the decoder wrote to a different memory space than
             * our texture resolve picked (the local-vs-main mismatch). */
            u32 loc = tex->format & 3;               /* 1=LOCAL, 2=MAIN */
            u32 ea_loc  = cellGcmResolveLocated(1, offset);
            u32 ea_main = cellGcmResolveLocated(0, offset);
            u32 plane = width * height;
            u32 nz_loc = 0, nz_main = 0;
            if (vm_base) for (u32 i = 0; i < plane; i += 137) {
                if (vm_base[ea_loc  + i]) nz_loc++;
                if (vm_base[ea_main + i]) nz_main++;
            }
            fprintf(stderr, "[mv-bind] unit=%u fmt=0x%02X loc=%u %ux%u off=0x%X | LOCAL ea=0x%08X nz=%u | MAIN ea=0x%08X nz=%u\n",
                    unit, format, loc, width, height, offset,
                    ea_loc, nz_loc, ea_main, nz_main);
        }
    }

    if (!vm_base || width == 0 || height == 0) return;

    /* Record the currently-bound atlas so subsequent quad draws sample it. The
     * actual GPU upload happens in render_frame (we have no open command list
     * here). Only the 8-bit single-channel font atlas (B8, RSX fmt base 0x81 /
     * as-seen 0xA1 with the LN flag) is wired up so far; other formats fall
     * back to untextured. */
    u32 base_fmt = format & 0x9F;   /* strip LN(0x20)/UN(0x40) flags */
    /* VP path: record the latest bound texture (any supported format) so draws
     * can carry it per-draw. Location bits (format[1:0]): 1 = LOCAL, 2 = MAIN. */
    extern u32 cellGcmResolveLocated(int local, u32 offset);
    if (unit < 4 &&
        (base_fmt == 0x81 /* B8 */ || base_fmt == 0x85 /* A8R8G8B8 */ ||
         base_fmt == 0x8B /* G8B8: LBP's font atlas */ ||
         (base_fmt >= 0x86 && base_fmt <= 0x88) /* DXT1/23/45 */ ||
         base_fmt == 0x9A /* W16Z16Y16X16 half-float: RTT intermediates */ || base_fmt == 0x9E || base_fmt == 0x82 || base_fmt == 0x83 || base_fmt == 0x84 || base_fmt == 0x97 || base_fmt == 0x9D /* winport-fix: RGBA8-class texture formats */)) {
        /* TEX_RESOLVE_AUTO=1: resolve through the page tables (local-page map
         * then IO table) instead of trusting the format's location bits. A
         * texture the guest built in main memory but tagged local resolves to
         * untouched VRAM and samples as all-zero -- geometry renders, flat. */
        { static int _ra = -1; if (_ra < 0) _ra = getenv("TEX_RESOLVE_AUTO") ? 1 : 0;
          extern u32 cellGcmResolveIO(u32);
          u32 _r = 0;
          if (_ra) _r = cellGcmResolveIO(offset);          /* IO table first */
          if (!_r) _r = cellGcmResolveLocated((tex->format & 3) == 1, offset);
          /* TEX_PROBE=1: the location bits are not always right. This title
           * binds its menu textures with a location that resolves into main
           * memory, where the bytes are all zero, while the same raw offset
           * resolved as LOCAL lands on real data -- so 100k menu draws sample
           * black and the menu never appears. When the chosen resolution is
           * empty over a sample and the other one is not, take the other. */
          { static int _tp = -1;
            if (_tp < 0) _tp = getenv("TEX_PROBE") ? 1 : 0;
            if (_tp && vm_base && _r) {
                u32 alt = cellGcmResolveLocated((tex->format & 3) != 1, offset);
                if (alt && alt != _r && alt < 0xE0000000u && _r < 0xE0000000u) {
                    u32 span = width * height / 2;
                    if (span > 4096) span = 4096;
                    u32 nz_r = 0, nz_a = 0;
                    for (u32 i = 0; i < span; i += 7) {
                        if (vm_base[_r  + i]) nz_r++;
                        if (vm_base[alt + i]) nz_a++;
                    }
                    if (nz_r == 0 && nz_a > 0) {
                        static u32 seen[32]; static int ns = 0; int known = 0;
                        for (int k = 0; k < ns; k++) if (seen[k] == offset) known = 1;
                        if (!known && ns < 32) { seen[ns++] = offset;
                            fprintf(stderr, "[TEX_PROBE] 0x%08X: %s empty, using %s"
                                            " (%u non-zero)%c", offset,
                                    ((tex->format & 3) == 1) ? "local" : "main",
                                    ((tex->format & 3) == 1) ? "main" : "local",
                                    nz_a, 10); }
                        _r = alt;
                    }
                }
            } }
          s_d3d.cur_texs[unit].off = _r; }
        s_d3d.cur_texs[unit].raw = offset;
        s_d3d.cur_texs[unit].w = width; s_d3d.cur_texs[unit].h = height;
        s_d3d.cur_texs[unit].fmt = format;   /* full byte: LN(0x20)/UN(0x40) kept */
        s_d3d.cur_texs[unit].ctrl1 = tex->control1;
        s_d3d.cur_texs[unit].samp = 0x8000u | ((tex->filter >> 16) & 7u) | (((tex->filter >> 24) & 7u) << 3)
                                  | ((tex->address & 0xFu) << 6) | (((tex->address >> 8) & 0xFu) << 10);   /* winport-fix: guest sampler state */
        s_d3d.cur_texs[unit].cube  = (tex->format & 4) ? 1 : 0;
        s_d3d.cur_texs[unit].mips  = (tex->format >> 16) & 0xFFFFu;
        if (getenv("TEXFMTDBG")) { static u32 seen[64]; static int ns=0;
            u32 k = (unit<<24) | (((tex->format>>8) & 0xFFu)<<16) | (width & 0xFFFFu);
            int f=0; for (int i=0;i<ns;i++) if (seen[i]==k) f=1;
            if (!f && ns < 64) { seen[ns++]=k;
                fprintf(stderr, "[TEXFMT] unit=%u fmt=0x%02X %ux%u mips=%u cube=%d off=0x%X%c",
                        unit, (tex->format >> 8) & 0xFFu, width, height,
                        (tex->format>>16)&0xFFFFu, (tex->format&4)?1:0, offset, 10); } }
        s_d3d.cur_texs[unit].set = 1;
    }
    if (base_fmt == 0x81 /* B8 */) {
        s_d3d.tex_src_offset = cellGcmResolveLocated((tex->format & 3) == 1, offset);
        if (s_d3d.tex_w != width || s_d3d.tex_h != height) {
            /* dims changed -> resource must be (re)created in render_frame */
            s_d3d.tex_ready = 0;
        }
        s_d3d.tex_w     = width;
        s_d3d.tex_h     = height;
        s_d3d.tex_bound = 1;
        s_d3d.tex_dirty = 1;
    } else {
        s_d3d.tex_bound = 0;
    }
}

static void d3d12_set_vertex_attribs_wk85(void* ud, const rsx_state* state);
static void d3d12_set_vertex_attribs(void* ud, const rsx_state* state) { const unsigned long long t_ = WK85_T0(); d3d12_set_vertex_attribs_wk85(ud, state); WK85_ADD(WK85_vattr, t_); }   /* winport v85a-walkprof */
static void d3d12_set_vertex_attribs_wk85(void* ud, const rsx_state* state)
{
    (void)ud;
    s_d3d.current_rsx_state = state;

    /* Log enabled vertex attributes for debugging. RSX_VTXDBG=<N> raises the
     * cap: the fixed 5 were all consumed by boot-time setup, so the layouts the
     * real draws use were never printed. */
    static int log_count = 0, log_cap = -1;
    if (log_cap < 0) { const char* e = getenv("RSX_VTXDBG"); log_cap = e ? atoi(e) : 5; }
    if (log_count < log_cap) {
        printf("[D3D12] set_vertex_attribs:\n");
        for (int i = 0; i < 16; i++) {
            const rsx_vertex_attrib* a = &state->vertex_attribs[i];
            if (a->enabled) {
                const char* type_name = "?";
                switch (a->type) {
                case 1: type_name = "snorm16"; break;
                case 2: type_name = "float32"; break;
                case 3: type_name = "float16"; break;
                case 4: type_name = "ubyte"; break;
                case 5: type_name = "s16"; break;
                case 7: type_name = "ubyte256"; break;
                }
                printf("  attrib[%d]: %s x%u, stride=%u, offset=0x%X\n",
                       i, type_name, a->size, a->stride, a->offset);
            }
        }
        log_count++;
    }
}

static void d3d12_set_shader_wk85(void* ud, const rsx_state* state);
static void d3d12_set_shader(void* ud, const rsx_state* state) { const unsigned long long t_ = WK85_T0(); d3d12_set_shader_wk85(ud, state); WK85_ADD(WK85_shader, t_); }   /* winport v85a-walkprof */
static void d3d12_set_shader_wk85(void* ud, const rsx_state* state)
{
    (void)ud;
    s_d3d.current_rsx_state = state;

    static int log_count = 0;
    if (log_count < 5) {
        printf("[D3D12] set_shader: fp_addr=0x%08X, vp_load=%u, output_mask=0x%08X\n",
               state->fragment_program_addr, state->transform_program_load,
               state->vertex_attrib_output_mask);
        log_count++;
    }

    /* TODO: Look up or compile a PSO matching this shader combination.
     * For now we use the basic vertex-colored PSO for everything. */
}

static void d3d12_set_blend_wk85(void* ud, const rsx_state* state);
static void d3d12_set_blend(void* ud, const rsx_state* state) { const unsigned long long t_ = WK85_T0(); d3d12_set_blend_wk85(ud, state); WK85_ADD(WK85_blend, t_); }   /* winport v85a-walkprof */
static void d3d12_set_blend_wk85(void* ud, const rsx_state* state)
{
    (void)ud;
    /* TODO: modify PSO blend state or use dynamic state.
     * D3D12 requires PSO recreation for blend state changes,
     * so we'd need a PSO cache keyed by blend configuration. */
    static int log_count = 0;
    if (log_count < 5) {
        printf("[D3D12] set_blend(enable=%d, sfactor=0x%X, dfactor=0x%X)\n",
               state->blend_enable, state->blend_sfactor, state->blend_dfactor);
        log_count++;
    }
}

static void d3d12_set_depth_stencil_wk85(void* ud, const rsx_state* state);
static void d3d12_set_depth_stencil(void* ud, const rsx_state* state) { const unsigned long long t_ = WK85_T0(); d3d12_set_depth_stencil_wk85(ud, state); WK85_ADD(WK85_depth, t_); }   /* winport v85a-walkprof */
static void d3d12_set_depth_stencil_wk85(void* ud, const rsx_state* state)
{
    (void)ud;
    static int log_count = 0;
    if (log_count < 5) {
        printf("[D3D12] set_depth_stencil(depth=%d, stencil=%d, func=0x%X)\n",
               state->depth_test_enable, state->stencil_test_enable,
               state->depth_func);
        log_count++;
    }
}

/* ---------------------------------------------------------------------------
 * Backend registration
 * -----------------------------------------------------------------------*/

static rsx_backend s_d3d12_backend = {0};

/* ---------------------------------------------------------------------------
 * Public API
 * -----------------------------------------------------------------------*/

int rsx_d3d12_backend_init(u32 width, u32 height, const char* title)
{
    memset(&s_d3d, 0, sizeof(s_d3d));
    s_d3d.width = width;
    s_d3d.height = height;
    s_d3d.clear_color[0] = 0.0f;
    s_d3d.clear_color[1] = 0.0f;
    s_d3d.clear_color[2] = 0.1f;
    s_d3d.clear_color[3] = 1.0f;

    /* Debug: dump the first N presented frames to BMP if CELLMARK_DUMP is set
     * (its numeric value when > 1, else 24). */
    {
        const char* dv = getenv("CELLMARK_DUMP");
        int n = dv ? atoi(dv) : 0;
        s_d3d.dump_frames_left = dv ? (n > 1 ? n : 24) : 0;
        /* CELLMARK_DUMP_SKIP=N: ignore the first N presents before dumping.
         * The opening frames are the loading screen; the interesting content
         * only appears once the sim SPUs have advanced the scene. */
        { const char* sv = getenv("CELLMARK_DUMP_SKIP");
          s_d3d.dump_skip_left = sv ? atoi(sv) : 0; }
    }

    /* Create window */
    {
        extern char g_rsx_title_base[128];
        snprintf(g_rsx_title_base, sizeof(g_rsx_title_base), "%s",
                 title ? title : "ps3recomp");
    }
    wp68_init(width, height);   /* winport v68 */
    s_d3d.hwnd = wp_create_window_threaded(width, height, title);   /* winport-fix: window thread */
    if (!s_d3d.hwnd) {
        printf("[D3D12] ERROR: Window creation failed\n");
        return -1;
    }

    /* Initialize D3D12 */
    if (init_d3d12(WP68_BW, WP68_BH) != 0) {   /* winport v68: back buffer = guest x scale */
        printf("[D3D12] ERROR: D3D12 initialization failed\n");
        return -1;
    }

    /* Set up backend callbacks */
    s_d3d12_backend.userdata          = &s_d3d;
    s_d3d12_backend.init              = d3d12_init;
    s_d3d12_backend.shutdown          = d3d12_shutdown;
    s_d3d12_backend.begin_frame       = d3d12_begin_frame;
    s_d3d12_backend.end_frame         = d3d12_end_frame;
    s_d3d12_backend.present           = d3d12_present;
    s_d3d12_backend.clear             = d3d12_clear;
    s_d3d12_backend.set_render_target = d3d12_set_render_target;
    s_d3d12_backend.set_viewport      = d3d12_set_viewport;
    s_d3d12_backend.set_blend         = d3d12_set_blend;
    s_d3d12_backend.set_depth_stencil = d3d12_set_depth_stencil;
    s_d3d12_backend.set_shader        = d3d12_set_shader;
    s_d3d12_backend.set_vertex_attribs = d3d12_set_vertex_attribs;
    s_d3d12_backend.draw_arrays       = d3d12_draw_arrays;
    s_d3d12_backend.draw_indexed      = d3d12_draw_indexed;
    s_d3d12_backend.bind_texture      = d3d12_bind_texture;
    s_d3d12_backend.blit_scaled       = d3d12_blit_scaled;   /* winport-fix: NV3089 blits on the GPU */

    rsx_set_backend(&s_d3d12_backend);

    s_d3d.initialized = 1;
    s_d3d.last_fps_time = GetTickCount64();

    printf("[D3D12] Backend ready: %ux%u\n", width, height);
    return 0;
}

void rsx_d3d12_backend_shutdown(void)
{
    if (!s_d3d.initialized) return;

    wait_for_gpu();

    /* Release D3D12 resources */
    if (s_d3d.vertex_buffer) {
        s_d3d.vertex_buffer->lpVtbl->Unmap(s_d3d.vertex_buffer, 0, NULL);
        s_d3d.vertex_buffer->lpVtbl->Release(s_d3d.vertex_buffer);
    }
    if (s_d3d.pipeline_state)        s_d3d.pipeline_state->lpVtbl->Release(s_d3d.pipeline_state);
    if (s_d3d.pipeline_state_lines)  s_d3d.pipeline_state_lines->lpVtbl->Release(s_d3d.pipeline_state_lines);
    if (s_d3d.pipeline_state_points) s_d3d.pipeline_state_points->lpVtbl->Release(s_d3d.pipeline_state_points);
    if (s_d3d.depth_buffer) s_d3d.depth_buffer->lpVtbl->Release(s_d3d.depth_buffer);
    if (s_d3d.dsv_heap)     s_d3d.dsv_heap->lpVtbl->Release(s_d3d.dsv_heap);
    if (s_d3d.root_signature) s_d3d.root_signature->lpVtbl->Release(s_d3d.root_signature);
    if (s_d3d.fence) s_d3d.fence->lpVtbl->Release(s_d3d.fence);
    if (s_d3d.fence_event) CloseHandle(s_d3d.fence_event);
    if (s_d3d.cmd_list) s_d3d.cmd_list->lpVtbl->Release(s_d3d.cmd_list);
    for (u32 i = 0; i < FRAME_COUNT; i++) {
        if (s_d3d.cmd_allocators[i]) s_d3d.cmd_allocators[i]->lpVtbl->Release(s_d3d.cmd_allocators[i]);
        if (s_d3d.render_targets[i]) s_d3d.render_targets[i]->lpVtbl->Release(s_d3d.render_targets[i]);
    }
    if (s_d3d.rtv_heap) s_d3d.rtv_heap->lpVtbl->Release(s_d3d.rtv_heap);
    if (s_d3d.swap_chain) s_d3d.swap_chain->lpVtbl->Release(s_d3d.swap_chain);
    if (s_d3d.cmd_queue) s_d3d.cmd_queue->lpVtbl->Release(s_d3d.cmd_queue);
    if (s_d3d.device) s_d3d.device->lpVtbl->Release(s_d3d.device);

    if (s_d3d.hwnd) DestroyWindow(s_d3d.hwnd);

    rsx_set_backend(NULL);
    s_d3d.initialized = 0;

    printf("[D3D12] Backend shut down\n");
}

int rsx_d3d12_backend_pump_messages(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return -1;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return s_d3d.window_closed ? -1 : 0;
}

void rsx_d3d12_backend_present(void)
{
    if (blink_dbg())
        printf("[PRESENT] draws=%u clears_since_last=%u clear_presents=%u\n",
               s_d3d.draw_count, s_dbg_clears_since_present, s_clear_presents);
    s_dbg_clears_since_present = 0;

    /* Once frame-boundary presents are active (d3d12_clear presents each
     * completed frame as the drain crosses into the next one), the ticker
     * present would only ever show the partially-accumulated NEXT frame --
     * that partial present right after the FIFO ring recycle was the visible
     * blink. Keep the ticker present solely as the boot-time fallback (before
     * the first framed clear arrives). */
    if (s_clear_presents > 0)
        return;

    /* Same display gate as d3d12_present: a batch of offscreen pass work only
     * (render-to-texture) keeps accumulating until its composite arrives.
     * Empty batches present only until the first real frame -- after that an
     * empty present is a flip/drain race and wipes the screen for a frame
     * (wave: black flashes and layout flicker between frames). */
    static int s_seen_content = 0;
    int has_display = (s_d3d.draw_count == 0 && !s_seen_content);
    for (u32 _i = 0; _i < s_d3d.draw_count && _i < MAX_DRAWS; _i++)
        if (!s_d3d.draws[_i].is_clear && s_d3d.draws[_i].rt_off == 0) {
            has_display = 1;
            break;
        }
    /* The guest-framebuffer present needs the pass to run at all. */
    { static int gfb = -1;
      if (gfb < 0) { const char* e = getenv("GCM_GUEST_FB"); gfb = e ? 1 : 0; }
      if (gfb) has_display = 1; }
    if (s_composite_src) has_display = 1;
    if (has_display && s_d3d.draw_count > 0) s_seen_content = 1;

    /* VP_SUBMIT=<N>: has_display gates render_frame() entirely, so a batch whose
     * records all target an OFFSCREEN rt (rt_off != 0) presents without ever
     * running the draw pass -- the backbuffer then shows only the clear, which
     * reads as "nothing rasterizes" from every downstream check. */
    { static int cap = -1, n = 0;
      if (cap < 0) { const char* e = getenv("VP_SUBMIT"); cap = e ? atoi(e) : 0; }
      if (cap && n < cap && s_d3d.draw_count) { n++;
        u32 onscreen = 0, offscreen = 0, clears = 0;
        for (u32 _i = 0; _i < s_d3d.draw_count && _i < MAX_DRAWS; _i++) {
            if (s_d3d.draws[_i].is_clear) { clears++; continue; }
            if (s_d3d.draws[_i].rt_off) offscreen++; else onscreen++;
        }
        { /* Which surfaces the batch's draws actually target -- "offscreen=57"
           * does not say whether they all go to one RT or scatter. */
          u32 offs[8]; u32 cnt[8]; int nd = 0;
          for (u32 _i = 0; _i < s_d3d.draw_count && _i < MAX_DRAWS; _i++) {
              if (s_d3d.draws[_i].is_clear) continue;
              u32 o = s_d3d.draws[_i].rt_off; int f = -1;
              for (int _j = 0; _j < nd; _j++) if (offs[_j] == o) { f = _j; break; }
              if (f < 0) { if (nd < 8) { offs[nd] = o; cnt[nd] = 1; nd++; } }
              else cnt[f]++;
          }
          { u32 vp = 0, novp = 0;
            for (u32 _i = 0; _i < s_d3d.draw_count && _i < MAX_DRAWS; _i++) {
                if (s_d3d.draws[_i].is_clear) continue;
                if (s_d3d.draws[_i].is_vp) vp++; else novp++;
            }
            /* The execution loop skips anything without is_vp, so a batch full
             * of !is_vp records is counted, gated and then never drawn. */
            fprintf(stderr, "[PRESENTGATE] is_vp=%u not_vp=%u%c", vp, novp, 10); }
          fprintf(stderr, "[PRESENTGATE] targets:");
          for (int _j = 0; _j < nd; _j++)
              fprintf(stderr, " 0x%08X x%u", offs[_j], cnt[_j]);
          fprintf(stderr, "%c", 10); }
        fprintf(stderr, "[PRESENTGATE] records=%u onscreen=%u offscreen=%u clears=%u"
                        " has_display=%d seen_content=%d -> render_frame=%s\n",
                s_d3d.draw_count, onscreen, offscreen, clears, has_display,
                s_seen_content, (s_d3d.initialized && has_display) ? "YES" : "SKIPPED"); } }

    /* Execute the batch whenever it has draws. Gating the whole call on
     * has_display meant a render-to-texture pass -- every draw targeting an
     * offscreen surface -- was DISCARDED rather than deferred, so the texture
     * it produces was never written and whatever sampled it later read an
     * empty resource. Only the Present needs onscreen content. */
    if (s_d3d.initialized && (has_display || s_d3d.draw_count > 0)) {
        { extern void rsx_reset_upload_claims(void);
          static int remap = -1;
          if (remap < 0) { const char* e = getenv("TEX_REMAP"); remap = e ? atoi(e) : 0; }
          if (remap) rsx_reset_upload_claims(); }
        s_present_this_frame = has_display;
        render_frame();
        s_present_this_frame = 1;
    }
}

#else /* !_WIN32 */

#include <ps3emu/ps3types.h>   /* u32 (header includes above are inside the _WIN32 guard) */
#include <stdio.h>

/* Stub for non-Windows — D3D12 is Windows-only */
int rsx_d3d12_backend_init(u32 w, u32 h, const char* t)
{
    (void)w; (void)h; (void)t;
    printf("[D3D12] Not available on this platform (use Vulkan backend)\n");
    return -1;
}
void rsx_d3d12_backend_shutdown(void) {}
int rsx_d3d12_backend_pump_messages(void) { return 0; }
void rsx_d3d12_backend_present(void) {}

#endif /* _WIN32 */
