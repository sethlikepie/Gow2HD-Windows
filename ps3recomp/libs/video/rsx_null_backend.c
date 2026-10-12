/*
 * ps3recomp - Null RSX Backend (Win32 window + color clear)
 *
 * Implements rsx_backend callbacks using a simple Win32 GDI window.
 * No GPU rendering — just clears to the RSX clear color and presents.
 * Used for debugging command buffer flow before a real backend exists.
 */

#include "rsx_null_backend.h"
#include "rsx_frame_dump.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* ---------------------------------------------------------------------------
 * Internal state
 * -----------------------------------------------------------------------*/

typedef struct {
    HWND     hwnd;
    HDC      hdc;
    u32      width;
    u32      height;

    /* Current clear color (ARGB -> COLORREF) */
    COLORREF clear_color;

    /* Frame counter */
    u64      frame_count;
    u64      last_fps_time;
    u32      fps;

    int      window_closed;
} NullBackendState;

static NullBackendState s_state;

/* ---------------------------------------------------------------------------
 * Win32 window procedure
 * -----------------------------------------------------------------------*/

static LRESULT CALLBACK null_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
        s_state.window_closed = 1;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        /* Fill with current clear color */
        HBRUSH brush = CreateSolidBrush(s_state.clear_color);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, brush);
        DeleteObject(brush);

        /* Draw debug overlay text */
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        char buf[128];
        snprintf(buf, sizeof(buf), "ps3recomp null backend | %u FPS | frame %llu",
                 s_state.fps, (unsigned long long)s_state.frame_count);
        TextOutA(hdc, 10, 10, buf, (int)strlen(buf));

        char buf2[64];
        snprintf(buf2, sizeof(buf2), "Clear: #%06X",
                 (unsigned)(s_state.clear_color & 0xFFFFFF));
        TextOutA(hdc, 10, 30, buf2, (int)strlen(buf2));

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            s_state.window_closed = 1;
            DestroyWindow(hwnd);
        }
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* ---------------------------------------------------------------------------
 * Backend callbacks
 * -----------------------------------------------------------------------*/

static int null_init(void* ud, u32 width, u32 height)
{
    (void)ud;
    printf("[RSX null] init(%ux%u)\n", width, height);
    return 0;
}

static void null_shutdown(void* ud)
{
    (void)ud;
    printf("[RSX null] shutdown\n");
}

static void null_begin_frame(void* ud)
{
    (void)ud;
}

static void null_end_frame(void* ud)
{
    (void)ud;
    s_state.frame_count++;

    /* Compute FPS every second */
    ULONGLONG now = GetTickCount64();
    if (now - s_state.last_fps_time >= 1000) {
        /* Simple: just use frame_count delta. For first second, estimate. */
        s_state.fps = (u32)(s_state.frame_count -
                           (s_state.frame_count > 60 ? s_state.frame_count - 60 : 0));
        s_state.last_fps_time = now;
    }
}

static void null_present(void* ud, u32 buffer_id)
{
    (void)ud;
    (void)buffer_id;

    /* Trigger a repaint */
    if (s_state.hwnd)
        InvalidateRect(s_state.hwnd, NULL, FALSE);
}

static void null_clear(void* ud, u32 flags, u32 color, float depth, u8 stencil)
{
    (void)ud;
    (void)flags;
    (void)depth;
    (void)stencil;

    /* Convert RSX ARGB to Win32 COLORREF (BGR) */
    u8 r = (color >> 16) & 0xFF;
    u8 g = (color >> 8) & 0xFF;
    u8 b = color & 0xFF;
    s_state.clear_color = RGB(r, g, b);
}

static void null_set_render_target(void* ud, const rsx_state* state)
{
    (void)ud;
    printf("[RSX null] set_render_target(format=0x%X, %ux%u)\n",
           state->surface_format, state->surface_clip_w, state->surface_clip_h);
}

static void null_set_viewport(void* ud, const rsx_state* state)
{
    (void)ud;
    printf("[RSX null] set_viewport(%u,%u %ux%u)\n",
           state->viewport_x, state->viewport_y,
           state->viewport_w, state->viewport_h);
}

static void null_draw_arrays(void* ud, u32 primitive, u32 first, u32 count)
{
    (void)ud;
    static int s_draw_log_count = 0;
    if (s_draw_log_count < 20) {
        printf("[RSX null] draw_arrays(prim=%u, first=%u, count=%u)\n",
               primitive, first, count);
        s_draw_log_count++;
    }
}

static void null_draw_indexed(void* ud, u32 primitive, u32 offset, u32 count)
{
    (void)ud;
    static int s_draw_log_count = 0;
    if (s_draw_log_count < 20) {
        printf("[RSX null] draw_indexed(prim=%u, offset=%u, count=%u)\n",
               primitive, offset, count);
        s_draw_log_count++;
    }
}

/* ---------------------------------------------------------------------------
 * Backend registration
 * -----------------------------------------------------------------------*/

static rsx_backend s_null_backend = {
    .userdata          = &s_state,
    .init              = null_init,
    .shutdown          = null_shutdown,
    .begin_frame       = null_begin_frame,
    .end_frame         = null_end_frame,
    .present           = null_present,
    .set_render_target = null_set_render_target,
    .set_viewport      = null_set_viewport,
    .set_blend         = NULL,
    .set_depth_stencil = NULL,
    .clear             = null_clear,
    .draw_arrays       = null_draw_arrays,
    .draw_indexed      = null_draw_indexed,
    .bind_texture      = NULL,
};

/* ---------------------------------------------------------------------------
 * Public API
 * -----------------------------------------------------------------------*/

int rsx_null_backend_init(u32 width, u32 height, const char* title)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.width = width;
    s_state.height = height;
    s_state.clear_color = RGB(0, 0, 64); /* dark blue default */

    /* Register window class */
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = null_wndproc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "ps3recomp_null";
    RegisterClassExA(&wc);

    /* Compute window size from client area */
    RECT wr = {0, 0, (LONG)width, (LONG)height};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    s_state.hwnd = CreateWindowExA(
        0, "ps3recomp_null",
        title ? title : "ps3recomp",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!s_state.hwnd) {
        printf("[RSX null] ERROR: CreateWindow failed (%lu)\n", GetLastError());
        return -1;
    }

    s_state.hdc = GetDC(s_state.hwnd);
    s_state.last_fps_time = GetTickCount64();

    /* Register as the active RSX backend */
    rsx_set_backend(&s_null_backend);

    printf("[RSX null] Window created: %ux%u\n", width, height);
    return 0;
}

void rsx_null_backend_shutdown(void)
{
    if (s_state.hwnd) {
        ReleaseDC(s_state.hwnd, s_state.hdc);
        DestroyWindow(s_state.hwnd);
        s_state.hwnd = NULL;
    }
    rsx_set_backend(NULL);
    printf("[RSX null] Backend shut down after %llu frames\n",
           (unsigned long long)s_state.frame_count);
}

int rsx_null_backend_pump_messages(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT)
            return -1;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return s_state.window_closed ? -1 : 0;
}

#else /* !_WIN32 */

/* ---------------------------------------------------------------------------
 * SDL2 backend (macOS, Linux, BSD)
 *
 * Same contract as the Win32 GDI path above: open a window, track the RSX
 * clear colour, repaint on present. No GPU work -- this exists to prove the
 * command processor is running and the flip path is reached, before a real
 * Metal or Vulkan backend lands.
 *
 * Set PS3_FRAME_DUMP=1 to write frame_<n>.bmp on every present, which makes
 * the backend usable over SSH or in CI where no window server is available.
 * -----------------------------------------------------------------------*/

#include <SDL2/SDL.h>
#include <stdlib.h>

typedef struct {
    SDL_Window*   window;
    SDL_Renderer* renderer;
    u32           width;
    u32           height;

    u8            clear_r, clear_g, clear_b;

    u64           frame_count;
    u64           last_fps_ticks;
    u64           last_fps_frame;
    u32           fps;

    int           window_closed;
    int           dump_frames;

    /* Window title base, "<game title> — SDL"; the per-second FPS title keeps
     * it as its prefix so the graphics path in use is always visible. */
    char base_title[160];
} SdlBackendState;

static SdlBackendState s_state;

static int sdl_init_cb(void* ud, u32 width, u32 height)
{
    (void)ud;
    printf("[RSX sdl] init(%ux%u)\n", width, height);
    return 0;
}

static void sdl_shutdown_cb(void* ud)
{
    (void)ud;
    printf("[RSX sdl] shutdown\n");
}

static void sdl_begin_frame(void* ud)
{
    (void)ud;
}

static void sdl_end_frame(void* ud)
{
    (void)ud;
    s_state.frame_count++;

    u64 now = SDL_GetTicks64();
    if (now - s_state.last_fps_ticks >= 1000) {
        u64 delta_frames = s_state.frame_count - s_state.last_fps_frame;
        u64 delta_ticks  = now - s_state.last_fps_ticks;
        s_state.fps = (u32)((delta_frames * 1000) / (delta_ticks ? delta_ticks : 1));
        s_state.last_fps_ticks = now;
        s_state.last_fps_frame = s_state.frame_count;

        SDL_SetWindowTitle(s_state.window, NULL);
        char title[224];
        snprintf(title, sizeof(title),
                 "%s | %u FPS | frame %llu | clear #%02X%02X%02X",
                 s_state.base_title, s_state.fps, (unsigned long long)s_state.frame_count,
                 s_state.clear_r, s_state.clear_g, s_state.clear_b);
        SDL_SetWindowTitle(s_state.window, title);
    }
}

/*
 * Read the frame back and write it out.
 *
 * The buffer must be sized from SDL_GetRendererOutputSize, not from the
 * window size: on a HiDPI display the drawable is a multiple of the logical
 * window (320x240 window -> 640x480 output on a 2x Retina panel), and
 * SDL_RenderReadPixels with a NULL rect fills the whole drawable. Sizing this
 * from s_state.width/height overflows the heap on every Retina Mac.
 *
 * ARGB8888 is byte order B,G,R,A on a little-endian host, which is what
 * rsx_write_bmp_bgra32 expects.
 */
static void sdl_dump_frame(void)
{
    int out_w = 0, out_h = 0;
    if (SDL_GetRendererOutputSize(s_state.renderer, &out_w, &out_h) != 0 ||
        out_w <= 0 || out_h <= 0) {
        printf("[RSX sdl] frame dump: cannot query output size: %s\n",
               SDL_GetError());
        return;
    }

    const u32 w      = (u32)out_w;
    const u32 h      = (u32)out_h;
    const u32 stride = w * 4u;

    u8* pixels = (u8*)malloc((size_t)stride * h);
    if (!pixels) {
        return;
    }

    if (SDL_RenderReadPixels(s_state.renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                             pixels, (int)stride) != 0) {
        printf("[RSX sdl] frame dump: readback failed: %s\n", SDL_GetError());
        free(pixels);
        return;
    }

    char path[64];
    snprintf(path, sizeof(path), "frame_%llu.bmp",
             (unsigned long long)s_state.frame_count);
    rsx_write_bmp_bgra32(path, pixels, w, h);
    free(pixels);
}

static void sdl_present(void* ud, u32 buffer_id)
{
    (void)ud;
    (void)buffer_id;

    if (!s_state.renderer) {
        return;
    }

    SDL_SetRenderDrawColor(s_state.renderer,
                           s_state.clear_r, s_state.clear_g, s_state.clear_b, 255);
    SDL_RenderClear(s_state.renderer);

    if (s_state.dump_frames) {
        sdl_dump_frame();
    }

    SDL_RenderPresent(s_state.renderer);
}

static void sdl_clear(void* ud, u32 flags, u32 color, float depth, u8 stencil)
{
    (void)ud;
    (void)flags;
    (void)depth;
    (void)stencil;

    /* RSX clear colour arrives as ARGB. */
    s_state.clear_r = (color >> 16) & 0xFF;
    s_state.clear_g = (color >> 8)  & 0xFF;
    s_state.clear_b =  color        & 0xFF;
}

static void sdl_set_render_target(void* ud, const rsx_state* state)
{
    (void)ud;
    printf("[RSX sdl] set_render_target(format=0x%X, %ux%u)\n",
           state->surface_format, state->surface_clip_w, state->surface_clip_h);
}

static void sdl_set_viewport(void* ud, const rsx_state* state)
{
    (void)ud;
    printf("[RSX sdl] set_viewport(%u,%u %ux%u)\n",
           state->viewport_x, state->viewport_y,
           state->viewport_w, state->viewport_h);
}

static void sdl_draw_arrays(void* ud, u32 primitive, u32 first, u32 count)
{
    (void)ud;
    static int s_draw_log_count = 0;
    if (s_draw_log_count < 20) {
        printf("[RSX sdl] draw_arrays(prim=%u, first=%u, count=%u)\n",
               primitive, first, count);
        s_draw_log_count++;
    }
}

static void sdl_draw_indexed(void* ud, u32 primitive, u32 offset, u32 count)
{
    (void)ud;
    static int s_draw_log_count = 0;
    if (s_draw_log_count < 20) {
        printf("[RSX sdl] draw_indexed(prim=%u, offset=%u, count=%u)\n",
               primitive, offset, count);
        s_draw_log_count++;
    }
}

static rsx_backend s_sdl_backend = {
    .userdata          = &s_state,
    .init              = sdl_init_cb,
    .shutdown          = sdl_shutdown_cb,
    .begin_frame       = sdl_begin_frame,
    .end_frame         = sdl_end_frame,
    .present           = sdl_present,
    .set_render_target = sdl_set_render_target,
    .set_viewport      = sdl_set_viewport,
    .set_blend         = NULL,
    .set_depth_stencil = NULL,
    .clear             = sdl_clear,
    .draw_arrays       = sdl_draw_arrays,
    .draw_indexed      = sdl_draw_indexed,
    .bind_texture      = NULL,
};

int rsx_null_backend_init(u32 width, u32 height, const char* title)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.width  = width;
    s_state.height = height;
    s_state.clear_b = 64; /* dark blue default, matching the Win32 path */

    s_state.dump_frames = rsx_frame_dump_enabled();

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        printf("[RSX sdl] ERROR: SDL_InitSubSystem(VIDEO) failed: %s\n",
               SDL_GetError());
        return -1;
    }

    snprintf(s_state.base_title, sizeof(s_state.base_title), "%s \xE2\x80\x94 SDL",
             (title && title[0]) ? title : "ps3recomp");
    s_state.window = SDL_CreateWindow(
        s_state.base_title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        (int)width, (int)height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);

    if (!s_state.window) {
        printf("[RSX sdl] ERROR: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return -1;
    }

    /* Let SDL pick the driver: Metal on macOS, GL elsewhere. Fall back to the
     * software renderer so headless/CI hosts still get a valid target. */
    s_state.renderer = SDL_CreateRenderer(s_state.window, -1,
                                          SDL_RENDERER_ACCELERATED);
    if (!s_state.renderer) {
        printf("[RSX sdl] accelerated renderer unavailable (%s), trying software\n",
               SDL_GetError());
        s_state.renderer = SDL_CreateRenderer(s_state.window, -1,
                                              SDL_RENDERER_SOFTWARE);
    }
    if (!s_state.renderer) {
        printf("[RSX sdl] ERROR: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(s_state.window);
        s_state.window = NULL;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return -1;
    }

    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(s_state.renderer, &info) == 0) {
        printf("[RSX sdl] renderer: %s\n", info.name);
    }

    s_state.last_fps_ticks = SDL_GetTicks64();

    rsx_set_backend(&s_sdl_backend);

    printf("[RSX sdl] Window created: %ux%u%s\n", width, height,
           s_state.dump_frames ? " (frame dump on)" : "");
    return 0;
}

void rsx_null_backend_shutdown(void)
{
    if (s_state.renderer) {
        SDL_DestroyRenderer(s_state.renderer);
        s_state.renderer = NULL;
    }
    if (s_state.window) {
        SDL_DestroyWindow(s_state.window);
        s_state.window = NULL;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    rsx_set_backend(NULL);
    printf("[RSX sdl] Backend shut down after %llu frames\n",
           (unsigned long long)s_state.frame_count);
}

int rsx_null_backend_pump_messages(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            s_state.window_closed = 1;
            break;

        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_CLOSE) {
                s_state.window_closed = 1;
            }
            break;

        case SDL_KEYDOWN:
            if (ev.key.keysym.sym == SDLK_ESCAPE) {
                s_state.window_closed = 1;
            }
            break;

        default:
            break;
        }
    }

    return s_state.window_closed ? -1 : 0;
}

#endif /* _WIN32 */
