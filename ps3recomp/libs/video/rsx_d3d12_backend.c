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

#include "rsx_d3d12_backend.h"
#include "rsx_primitives.h"
#include "rsx_fp_decompiler.h"
#include "rsx_vp_decompiler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* We need these GUIDs — define them here to avoid uuid.lib dependency.
 * Must precede the D3D12/DXGI headers: initguid.h works by redefining the
 * DEFINE_GUID macro, so it only takes effect on headers included after it. */
#include <initguid.h>

/* D3D12 headers */
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>

/* Runtime overlay: shared core API, the D3D12 UI renderer and the pure
 * Win32-input / frame-timing helpers. Included before rsx_texture_formats.h
 * for the same DXGI_FORMAT reason as the SDK headers above. */
#include "rsx_overlay.h"
#include "rsx_overlay_d3d12.h"
#include "rsx_overlay_frame_timer.h"
#include "rsx_overlay_win32_input.h"
#include "rsx_overlay_log.h"
#include <stdarg.h>

/* rsx_texture_formats.h re-#defines DXGI_FORMAT_* names as plain integer
 * macros (it's shared with .c files that don't pull in the real SDK headers)
 * — must come AFTER <dxgi1_4.h> so those macros don't clobber the identifiers
 * the real DXGI_FORMAT enum declaration needs while it's being parsed. */
#include "rsx_texture_formats.h"
#include "rsx_texture_deswizzle.h"

/* Link libraries */
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

/* ---------------------------------------------------------------------------
 * Constants
 * -----------------------------------------------------------------------*/

#define FRAME_COUNT         2   /* double buffering */
#define MAX_VERTICES      4096  /* per-frame vertex buffer */
#define MAX_INDICES       8192  /* per-frame index buffer */
#define MAX_DRAWS          256  /* per-frame draw records */
#define MAX_SHADER_CACHE    32  /* cached fragment-program PSO triples */

#if FRAME_COUNT > RSX_OVERLAY_D3D12_FRAME_SLOTS
#error "overlay renderer needs one upload slot per swap-chain frame"
#endif

/* One compiled fragment program, paired with the shared passthrough VS, as
 * three PSOs (one per primitive-topology class — D3D12 requires the PSO's
 * PrimitiveTopologyType to match what IASetPrimitiveTopology will use). */
typedef struct {
    u32 fp_hash;          /* FNV-1a over the raw NV40 fragment-program bytecode */
    u32 vp_hash;          /* FNV-1a over the raw NV40 vertex-program words; 0 = passthrough VS */
    ID3DBlob* vs_owned;   /* owner of the decompiled VS blob; NULL = uses shared passthrough */
    ID3D12PipelineState* tri;
    ID3D12PipelineState* line;
    ID3D12PipelineState* point;
} ShaderPsoSet;

typedef struct {
    u32 vb_byte_offset; /* offset into vb_mapped where this draw's verts live */
    u32 vertex_count;   /* for indexed draws, this is the INDEX count */
    u32 topology;       /* D3D_PRIMITIVE_TOPOLOGY_* */
    int is_indexed;
    u32 ib_byte_offset; /* offset into ib_mapped, valid when is_indexed */
    u32 index_format;   /* DXGI_FORMAT_R16_UINT or DXGI_FORMAT_R32_UINT, valid when is_indexed */
    ShaderPsoSet* shader; /* NULL = use the fixed hardcoded PSO (no shader decompiled yet) */
} D3D12DrawRecord;

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
    ID3D12Resource*            render_targets[FRAME_COUNT];
    ID3D12CommandAllocator*    cmd_allocators[FRAME_COUNT];
    ID3D12GraphicsCommandList* cmd_list;

    /* Synchronization */
    ID3D12Fence* fence;
    HANDLE       fence_event;
    u64          fence_values[FRAME_COUNT];
    u32          frame_index;

    /* Pipeline */
    ID3D12RootSignature*  root_signature;
    ID3D12PipelineState*  pipeline_state;         /* triangle class — default */
    ID3D12PipelineState*  pipeline_state_lines;   /* line class */
    ID3D12PipelineState*  pipeline_state_points;  /* point class */
    ID3DBlob*             vs_passthrough_blob;    /* compiled once, shared by every fragment-shader PSO */

    /* Depth/stencil */
    ID3D12DescriptorHeap* dsv_heap;
    ID3D12Resource*       depth_buffer;

    /* Dynamic vertex buffer (upload heap) */
    ID3D12Resource*       vertex_buffer;
    D3D12_VERTEX_BUFFER_VIEW vb_view;
    void*                 vb_mapped;      /* persistently mapped */
    u32                   vb_offset;      /* current write position */

    /* Dynamic index buffer (upload heap) — filled per draw_indexed call. */
    ID3D12Resource*       index_buffer;
    void*                 ib_mapped;
    u32                   ib_capacity_bytes;
    u32                   ib_offset;      /* current write position, reset each frame */

    /* Texture descriptor tables: one shared SRV heap (16 slots, t0..t15) and
     * one shared SAMPLER heap (16 slots, s0..s15). Slots not yet bound by the
     * game point at a 1x1 white dummy texture / default sampler so any
     * decompiled shader that indexes rsx_tex[N] is always well-defined. */
    ID3D12DescriptorHeap* srv_heap;
    ID3D12DescriptorHeap* sampler_heap;
    u32                   srv_descriptor_size;
    u32                   sampler_descriptor_size;
    ID3D12Resource*       dummy_texture;
    ID3D12Resource*       textures[RSX_MAX_TEXTURES]; /* NULL = still using the dummy */

    /* Fragment-shader PSO cache, keyed by a hash of the raw NV40 bytecode. */
    ShaderPsoSet          shader_cache[MAX_SHADER_CACHE];
    u32                   shader_cache_count;
    ShaderPsoSet*         current_shader; /* set by d3d12_set_shader, read by draw_arrays/draw_indexed */

    int                   pipeline_ready; /* 1 if root sig + PSO created */

    /* Per-frame draw recording */
    int                   frame_recording; /* 1 if cmd list is open for recording */
    u32                   draw_count;      /* draws this frame */
    D3D12DrawRecord       draws[MAX_DRAWS];

    /* Pointer to current RSX state (set before draw calls) */
    const rsx_state*      current_rsx_state;

    /* Current frame state */
    float clear_color[4];  /* RGBA float */

    /* Stats. fps comes from the elapsed-time frame timer; last_fps_time
     * only paces the once-per-second PS3_TRACE_RSX_SHADERS summary. */
    u64 frame_count;
    u64 last_fps_time;
    u32 fps;
    rsx_overlay_frame_timer frame_timer;
    u64 guest_draws_total; /* recorded guest draws replayed, overlay excluded */

    /* Runtime overlay UI pass (NULL = unavailable; the game renders as before). */
    rsx_overlay_d3d12_renderer* overlay;

    int initialized;
} D3D12State;

static D3D12State s_d3d;

/* ---------------------------------------------------------------------------
 * Content-pipeline health counters (Fase A observability).
 * Track how many fragment programs actually flow through the FP shader
 * cache: cache hits/misses, and decompile+compile outcomes. Printed as a
 * one-line summary at shutdown when PS3_TRACE_RSX_SHADERS=1, so we can prove
 * real game shaders (not just the hardcoded fallback PSO) reach the cache.
 * -----------------------------------------------------------------------*/
static u32 s_fp_cache_hits;
static u32 s_fp_cache_misses;
static u32 s_fp_decompile_ok;
static u32 s_fp_decompile_fail;
/* Vertex-program decompile counters (parallel to the FP ones above). ok = a
 * real NV40 VP was translated + D3DCompiled to a vs_5_0 blob; fail = decompile
 * or vs compile failed and we fell back to the shared passthrough VS. */
static u32 s_vp_decompile_ok;
static u32 s_vp_decompile_fail;

/* Shader failures also reach the overlay Logs tab. The ring never blocks
 * (a contended message is dropped); callers keep their own rate limits. */
static void d3d12_overlay_shader_log(const char* fmt, ...)
{
    char line[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, line);
}

u32 rsx_d3d12_fp_cache_hits(void)    { return s_fp_cache_hits; }
u32 rsx_d3d12_fp_cache_misses(void)  { return s_fp_cache_misses; }
u32 rsx_d3d12_fp_decompile_ok(void)  { return s_fp_decompile_ok; }
u32 rsx_d3d12_fp_decompile_fail(void){ return s_fp_decompile_fail; }

/* ---------------------------------------------------------------------------
 * Win32 window
 * -----------------------------------------------------------------------*/

static LRESULT CALLBACK d3d12_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
        s_d3d.window_closed = 1;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
    case WM_KEYUP: {
        /* The overlay sees only the keys it owns and consumes them only while
         * open (plus its toggle key); everything else keeps the old path. */
        int overlay_key = rsx_overlay_win32_key((unsigned)wp);
        if (overlay_key != 0 &&
            rsx_overlay_handle_key(overlay_key, msg == WM_KEYDOWN,
                rsx_overlay_win32_modifiers(GetKeyState(VK_SHIFT) < 0,
                                            GetKeyState(VK_CONTROL) < 0,
                                            GetKeyState(VK_MENU) < 0)))
            return 0;
        if (msg == WM_KEYUP)
            break;
        if (wp == VK_ESCAPE) {
            s_d3d.window_closed = 1;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_KILLFOCUS:
        /* Held keys/buttons must not stay captured across a focus change. */
        rsx_overlay_focus_lost();
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static HWND create_window(u32 width, u32 height, const char* title, int fullscreen)
{
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = d3d12_wndproc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "ps3recomp_d3d12";
    RegisterClassExA(&wc);

    if (fullscreen) {
        /* Borderless window covering the monitor; the caller sized the swap
         * chain to it (width/height are that monitor's size). */
        return CreateWindowExA(
            0, "ps3recomp_d3d12",
            title ? title : "ps3recomp (D3D12)",
            WS_POPUP | WS_VISIBLE,
            0, 0, (int)width, (int)height,
            NULL, NULL, GetModuleHandle(NULL), NULL);
    }

    RECT wr = {0, 0, (LONG)width, (LONG)height};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    return CreateWindowExA(
        0, "ps3recomp_d3d12",
        title ? title : "ps3recomp (D3D12)",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        NULL, NULL, GetModuleHandle(NULL), NULL);
}

/* ---------------------------------------------------------------------------
 * D3D12 initialization
 * -----------------------------------------------------------------------*/

static int init_d3d12(u32 width, u32 height)
{
    HRESULT hr;

    /* Enable debug layer in debug builds */
#ifndef NDEBUG
    {
        ID3D12Debug* debug_controller = NULL;
        hr = D3D12GetDebugInterface(&IID_ID3D12Debug, (void**)&debug_controller);
        if (SUCCEEDED(hr) && debug_controller) {
            debug_controller->lpVtbl->EnableDebugLayer(debug_controller);
            debug_controller->lpVtbl->Release(debug_controller);
            printf("[D3D12] Debug layer enabled\n");
        }
    }
#endif

    /* Create DXGI factory */
    IDXGIFactory4* factory = NULL;
    hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void**)&factory);
    if (FAILED(hr)) {
        printf("[D3D12] ERROR: CreateDXGIFactory1 failed (0x%08lX)\n", hr);
        return -1;
    }

    /* Create D3D12 device */
    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0,
                           &IID_ID3D12Device, (void**)&s_d3d.device);
    if (FAILED(hr)) {
        printf("[D3D12] ERROR: D3D12CreateDevice failed (0x%08lX)\n", hr);
        factory->lpVtbl->Release(factory);
        return -1;
    }
    printf("[D3D12] Device created (feature level 11.0)\n");

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
        depth_desc.Width              = width;
        depth_desc.Height             = height;
        depth_desc.DepthOrArraySize   = 1;
        depth_desc.MipLevels          = 1;
        depth_desc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
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
     * Root signature: 16 root constants (mat4 MVP at b0, VS-only) plus two
     * descriptor tables for the decompiled fragment shaders' `Texture2D
     * rsx_tex[16] : register(t0)` / `SamplerState rsx_samp[16] : register(s0)`
     * (see docs/RSX_FRAGMENT_PROGRAM.md) — both pixel-shader-only.
     * ---------------------------------------------------------------*/
    {
        D3D12_DESCRIPTOR_RANGE srv_range = {0};
        srv_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors                    = RSX_MAX_TEXTURES;
        srv_range.BaseShaderRegister                = 0; /* t0 */
        srv_range.RegisterSpace                     = 0;
        srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE sampler_range = {0};
        sampler_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        sampler_range.NumDescriptors                    = RSX_MAX_TEXTURES;
        sampler_range.BaseShaderRegister                = 0; /* s0 */
        sampler_range.RegisterSpace                     = 0;
        sampler_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER root_params[3] = {0};
        root_params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_params[0].Constants.ShaderRegister = 0;   /* b0 */
        root_params[0].Constants.RegisterSpace  = 0;
        root_params[0].Constants.Num32BitValues = 16;  /* mat4 */
        root_params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;

        root_params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_params[1].DescriptorTable.NumDescriptorRanges  = 1;
        root_params[1].DescriptorTable.pDescriptorRanges    = &srv_range;
        root_params[1].ShaderVisibility                     = D3D12_SHADER_VISIBILITY_PIXEL;

        root_params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_params[2].DescriptorTable.NumDescriptorRanges  = 1;
        root_params[2].DescriptorTable.pDescriptorRanges    = &sampler_range;
        root_params[2].ShaderVisibility                     = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rs_desc = {0};
        rs_desc.NumParameters = 3;
        rs_desc.pParameters   = root_params;
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
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
            pso_desc.pRootSignature = s_d3d.root_signature;
            pso_desc.VS.pShaderBytecode = vs_blob->lpVtbl->GetBufferPointer(vs_blob);
            pso_desc.VS.BytecodeLength = vs_blob->lpVtbl->GetBufferSize(vs_blob);
            pso_desc.PS.pShaderBytecode = ps_blob->lpVtbl->GetBufferPointer(ps_blob);
            pso_desc.PS.BytecodeLength = ps_blob->lpVtbl->GetBufferSize(ps_blob);
            pso_desc.InputLayout.pInputElementDescs = input_layout;
            pso_desc.InputLayout.NumElements = 2;
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
    }

    /* ---------------------------------------------------------------
     * Create dynamic vertex buffer (upload heap, 4MB)
     * ---------------------------------------------------------------*/
    {
        D3D12_HEAP_PROPERTIES heap_props = {0};
        heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC buf_desc = {0};
        buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buf_desc.Width = MAX_VERTICES * 28; /* 28 bytes per vertex (pos3+col4) */
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
            s_d3d.vb_view.StrideInBytes = 28;
            s_d3d.vb_view.SizeInBytes = MAX_VERTICES * 28;
            printf("[D3D12] Vertex buffer created (%u KB)\n",
                   (MAX_VERTICES * 28) / 1024);
        }
    }

    /* ---------------------------------------------------------------
     * Create dynamic index buffer (upload heap, 4-byte indices worst case)
     * ---------------------------------------------------------------*/
    {
        D3D12_HEAP_PROPERTIES heap_props = {0};
        heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC buf_desc = {0};
        buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buf_desc.Width = MAX_INDICES * 4;
        buf_desc.Height = 1;
        buf_desc.DepthOrArraySize = 1;
        buf_desc.MipLevels = 1;
        buf_desc.SampleDesc.Count = 1;
        buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        hr = s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &heap_props, D3D12_HEAP_FLAG_NONE,
            &buf_desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Resource, (void**)&s_d3d.index_buffer);
        if (SUCCEEDED(hr)) {
            D3D12_RANGE read_range = {0, 0};
            s_d3d.index_buffer->lpVtbl->Map(s_d3d.index_buffer, 0, &read_range, &s_d3d.ib_mapped);
            s_d3d.ib_capacity_bytes = MAX_INDICES * 4;
            printf("[D3D12] Index buffer created (%u KB)\n", (MAX_INDICES * 4) / 1024);
        }
    }

    printf("[D3D12] Initialization complete (%ux%u, %u buffers, pipeline=%s)\n",
           width, height, FRAME_COUNT,
           s_d3d.pipeline_ready ? "ready" : "NOT ready");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Frame sync helpers
 * -----------------------------------------------------------------------*/

static void wait_for_gpu(void)
{
    u32 fi = s_d3d.frame_index;
    s_d3d.fence_values[fi]++;
    s_d3d.cmd_queue->lpVtbl->Signal(s_d3d.cmd_queue, s_d3d.fence, s_d3d.fence_values[fi]);

    if (s_d3d.fence->lpVtbl->GetCompletedValue(s_d3d.fence) < s_d3d.fence_values[fi]) {
        s_d3d.fence->lpVtbl->SetEventOnCompletion(
            s_d3d.fence, s_d3d.fence_values[fi], s_d3d.fence_event);
        WaitForSingleObject(s_d3d.fence_event, INFINITE);
    }
}

static void move_to_next_frame(void)
{
    u64 current_fence = s_d3d.fence_values[s_d3d.frame_index];
    s_d3d.cmd_queue->lpVtbl->Signal(s_d3d.cmd_queue, s_d3d.fence, current_fence);

    s_d3d.frame_index = s_d3d.swap_chain->lpVtbl->GetCurrentBackBufferIndex(s_d3d.swap_chain);

    if (s_d3d.fence->lpVtbl->GetCompletedValue(s_d3d.fence) < s_d3d.fence_values[s_d3d.frame_index]) {
        s_d3d.fence->lpVtbl->SetEventOnCompletion(
            s_d3d.fence, s_d3d.fence_values[s_d3d.frame_index], s_d3d.fence_event);
        WaitForSingleObject(s_d3d.fence_event, INFINITE);
    }

    s_d3d.fence_values[s_d3d.frame_index] = current_fence + 1;
}

/* ---------------------------------------------------------------------------
 * Texture descriptor tables: shared SRV heap (16 slots) + sampler heap (16
 * slots), plus a 1x1 white dummy texture so every slot is well-defined even
 * before the game binds a real texture to that unit (Task: decompiled
 * fragment shaders declare Texture2D rsx_tex[16] unconditionally).
 * -----------------------------------------------------------------------*/
static int create_texture_resources(void)
{
    HRESULT hr;

    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {0};
    srv_heap_desc.NumDescriptors = RSX_MAX_TEXTURES;
    srv_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = s_d3d.device->lpVtbl->CreateDescriptorHeap(
        s_d3d.device, &srv_heap_desc, &IID_ID3D12DescriptorHeap, (void**)&s_d3d.srv_heap);
    if (FAILED(hr)) { printf("[D3D12] SRV heap creation failed\n"); return -1; }
    s_d3d.srv_descriptor_size = s_d3d.device->lpVtbl->GetDescriptorHandleIncrementSize(
        s_d3d.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {0};
    sampler_heap_desc.NumDescriptors = RSX_MAX_TEXTURES;
    sampler_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampler_heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = s_d3d.device->lpVtbl->CreateDescriptorHeap(
        s_d3d.device, &sampler_heap_desc, &IID_ID3D12DescriptorHeap, (void**)&s_d3d.sampler_heap);
    if (FAILED(hr)) { printf("[D3D12] Sampler heap creation failed\n"); return -1; }
    s_d3d.sampler_descriptor_size = s_d3d.device->lpVtbl->GetDescriptorHandleIncrementSize(
        s_d3d.device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    /* 1x1 white RGBA8 dummy texture. */
    D3D12_HEAP_PROPERTIES default_heap = {0};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC tex_desc = {0};
    tex_desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Width            = 1;
    tex_desc.Height           = 1;
    tex_desc.DepthOrArraySize = 1;
    tex_desc.MipLevels        = 1;
    tex_desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    hr = s_d3d.device->lpVtbl->CreateCommittedResource(
        s_d3d.device, &default_heap, D3D12_HEAP_FLAG_NONE, &tex_desc,
        D3D12_RESOURCE_STATE_COPY_DEST, NULL,
        &IID_ID3D12Resource, (void**)&s_d3d.dummy_texture);
    if (FAILED(hr)) { printf("[D3D12] Dummy texture creation failed\n"); return -1; }

    D3D12_HEAP_PROPERTIES upload_heap = {0};
    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC upload_desc = {0};
    upload_desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    upload_desc.Width            = 256; /* room for a row-pitch-aligned 1x1 RGBA8 copy */
    upload_desc.Height           = 1;
    upload_desc.DepthOrArraySize = 1;
    upload_desc.MipLevels        = 1;
    upload_desc.SampleDesc.Count = 1;
    upload_desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* upload_buf = NULL;
    hr = s_d3d.device->lpVtbl->CreateCommittedResource(
        s_d3d.device, &upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
        &IID_ID3D12Resource, (void**)&upload_buf);
    if (FAILED(hr)) { printf("[D3D12] Dummy texture upload buffer failed\n"); return -1; }

    void* mapped = NULL;
    D3D12_RANGE no_read = {0, 0};
    upload_buf->lpVtbl->Map(upload_buf, 0, &no_read, &mapped);
    *(u32*)mapped = 0xFFFFFFFFu; /* opaque white, byte order irrelevant (R=G=B=A=0xFF) */
    upload_buf->lpVtbl->Unmap(upload_buf, 0, NULL);

    /* One-shot copy + transition, recorded/executed here (mirrors render_frame's
     * pattern but as a single blocking submission during setup). */
    s_d3d.cmd_allocators[0]->lpVtbl->Reset(s_d3d.cmd_allocators[0]);
    s_d3d.cmd_list->lpVtbl->Reset(s_d3d.cmd_list, s_d3d.cmd_allocators[0], NULL);

    D3D12_TEXTURE_COPY_LOCATION dst = {0};
    dst.pResource        = s_d3d.dummy_texture;
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {0};
    src.pResource                          = upload_buf;
    src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width    = 1;
    src.PlacedFootprint.Footprint.Height   = 1;
    src.PlacedFootprint.Footprint.Depth    = 1;
    src.PlacedFootprint.Footprint.RowPitch = 256; /* D3D12_TEXTURE_DATA_PITCH_ALIGNMENT */

    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);

    D3D12_RESOURCE_BARRIER barrier = {0};
    barrier.Type                        = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource        = s_d3d.dummy_texture;
    barrier.Transition.StateBefore      = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter       = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource      = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &barrier);

    s_d3d.cmd_list->lpVtbl->Close(s_d3d.cmd_list);
    ID3D12CommandList* lists[] = {(ID3D12CommandList*)s_d3d.cmd_list};
    s_d3d.cmd_queue->lpVtbl->ExecuteCommandLists(s_d3d.cmd_queue, 1, lists);
    wait_for_gpu();
    upload_buf->lpVtbl->Release(upload_buf);

    /* Every SRV slot starts pointing at the dummy texture; d3d12_bind_texture
     * overwrites the specific unit's slot once the game binds something real. */
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {0};
    srv_desc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension              = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels        = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE srv_handle;
    s_d3d.srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &srv_handle);
    for (u32 i = 0; i < RSX_MAX_TEXTURES; i++) {
        s_d3d.device->lpVtbl->CreateShaderResourceView(
            s_d3d.device, s_d3d.dummy_texture, &srv_desc, srv_handle);
        srv_handle.ptr += s_d3d.srv_descriptor_size;
    }

    D3D12_SAMPLER_DESC sampler_desc = {0};
    sampler_desc.Filter         = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU       = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.AddressV       = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.AddressW       = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler_desc.MaxLOD         = D3D12_FLOAT32_MAX;

    D3D12_CPU_DESCRIPTOR_HANDLE sampler_handle;
    s_d3d.sampler_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.sampler_heap, &sampler_handle);
    for (u32 i = 0; i < RSX_MAX_TEXTURES; i++) {
        s_d3d.device->lpVtbl->CreateSampler(s_d3d.device, &sampler_desc, sampler_handle);
        sampler_handle.ptr += s_d3d.sampler_descriptor_size;
    }

    printf("[D3D12] Texture descriptor tables ready (%u SRV + %u sampler slots)\n",
           (u32)RSX_MAX_TEXTURES, (u32)RSX_MAX_TEXTURES);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Render a frame (clear + present)
 * -----------------------------------------------------------------------*/

/* ---------------------------------------------------------------------------
 * Movie overlay (movie_hle.c): host-decoded RGBA frames drawn as a fullscreen
 * quad on top of the game's output while a movie is playing.
 * -----------------------------------------------------------------------*/
typedef struct {
    CRITICAL_SECTION     cs;
    int                  cs_init;
    volatile int         active;        /* a stream is playing            */
    int                  pending;       /* staging holds an unconsumed frame */
    u8*                  staging;       /* host copy of the latest frame  */
    u32                  w, h;
    ID3D12Resource*      tex;
    ID3D12Resource*      upload;
    u32                  tex_w, tex_h;  /* allocated texture geometry     */
    ID3D12DescriptorHeap* srv_heap;
    ID3D12RootSignature* root_sig;
    ID3D12PipelineState* pso;
    int                  tex_in_srv_state;
} MovieOverlay;
static MovieOverlay s_movie;

void rsx_d3d12_movie_frame(const void* rgba, unsigned w, unsigned h)
{
    if (!s_movie.cs_init) { InitializeCriticalSection(&s_movie.cs); s_movie.cs_init = 1; }
    EnterCriticalSection(&s_movie.cs);
    if (!s_movie.staging || s_movie.w != w || s_movie.h != h) {
        free(s_movie.staging);
        s_movie.staging = (u8*)malloc((size_t)w * h * 4);
        s_movie.w = w; s_movie.h = h;
    }
    if (s_movie.staging) {
        memcpy(s_movie.staging, rgba, (size_t)w * h * 4);
        s_movie.pending = 1;
        s_movie.active  = 1;
    }
    LeaveCriticalSection(&s_movie.cs);
}

/* Sticky content overlay (HOSTRES legalscreen720.ctxr etc.).
 * MUST be suspended while movie HLE is playing — otherwise the static legal
 * texture freezes the screen while PlaySound keeps the WAV going. */
static u8*  s_content_rgba;
static u32  s_content_w, s_content_h;
static int  s_content_hold;
static int  s_content_hold_paused; /* 1 while movie HLE owns the overlay */
static int  s_content_logged;
static int  s_movie_hle_playing;   /* set by movie_start/stop bridge */

void rsx_d3d12_movie_playback_begin(void)
{
    s_movie_hle_playing = 1;
    s_content_hold_paused = 1;
    /* Do not clear staging — next movie_frame will overwrite. Keep active so
     * the first decoded frame can present immediately. */
    fprintf(stderr, "[CONTENT] pause hold for movie HLE\n");
    fflush(stderr);
}

void rsx_d3d12_movie_playback_end(void)
{
    s_movie_hle_playing = 0;
    s_content_hold_paused = 0;
    /* After intro, only re-arm sticky content if explicitly requested.
     * Default off: re-showing legal over the logo end looked like a freeze. */
    {
        const char* v = getenv("PS3_CONTENT_HOLD_AFTER_MOVIE");
        if (v && v[0] && v[0] != '0' && s_content_hold && s_content_rgba) {
            rsx_d3d12_movie_frame(s_content_rgba, s_content_w, s_content_h);
            fprintf(stderr, "[CONTENT] re-arm hold after movie %ux%u\n",
                    s_content_w, s_content_h);
        } else {
            /* Leave last movie frame on screen until game draws something else. */
            fprintf(stderr, "[CONTENT] movie end — hold stays off (last movie frame)\n");
        }
        fflush(stderr);
    }
}

/* -----------------------------------------------------------------------
 * Force-bind host RGBA as a native RSX texture (≠ 1280×720 content path).
 * Writes guest main-memory A8R8G8B8 (BE) and calls d3d12_bind_texture so the
 * existing upload/SRV path logs bind_texture with real dimensions.
 * Deferred until the D3D device/SRV heap is ready.
 * -----------------------------------------------------------------------*/
#define FORCE_TEX_BASE   0x4F800000u
#define FORCE_TEX_STRIDE 0x00400000u /* 4 MB per unit slot */
#define FORCE_TEX_UNITS  8

typedef struct {
    u8*  rgba;
    u32  w, h;
    int  pending;
} ForceTexSlot;
static ForceTexSlot s_force_tex[FORCE_TEX_UNITS];
static int s_force_tex_bound;

extern void ppu_register_committed_range(uint32_t lo, uint32_t hi);
static void d3d12_bind_texture(void* ud, u32 unit, const rsx_texture_state* tex);

static void force_tex_try_bind_one(unsigned unit, const u8* rgba, u32 w, u32 h)
{
    extern uint8_t* vm_base;
    if (!rgba || !w || !h || unit >= FORCE_TEX_UNITS) return;
    if (!s_d3d.device || !s_d3d.srv_heap || !vm_base) return;

    u32 ea = FORCE_TEX_BASE + unit * FORCE_TEX_STRIDE;
    u32 bytes = w * h * 4u;
    if (bytes > FORCE_TEX_STRIDE - 0x1000u) return;

    /* Commit guest pages and register for the committed-range guard. */
    u32 pg = ea & ~0xFFFu;
    u32 span = ((bytes + 0xFFFu) & ~0xFFFu);
#ifdef _WIN32
    if (!VirtualAlloc(vm_base + pg, span, MEM_COMMIT, PAGE_READWRITE)) {
        fprintf(stderr, "[FORCE-TEX] VirtualAlloc failed ea=0x%08X span=0x%X\n", ea, span);
        fflush(stderr);
        return;
    }
#else
    if (mprotect(vm_base + pg, span, PROT_READ | PROT_WRITE) != 0) return;
#endif
    ppu_register_committed_range(pg, pg + span);

    /* Pack host RGBA → guest A8R8G8B8 byte order (A,R,G,B). Upload path
     * bswap32s to BGRA for DXGI B8G8R8A8_UNORM. */
    u8* dst = vm_base + ea;
    for (u32 i = 0; i < w * h; i++) {
        u8 r = rgba[i * 4 + 0], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2], a = rgba[i * 4 + 3];
        dst[i * 4 + 0] = a;
        dst[i * 4 + 1] = r;
        dst[i * 4 + 2] = g;
        dst[i * 4 + 3] = b;
    }

    /* format: bits8-15 = A8R8G8B8|LN (0xA5), dim 2D in bits4-7, loc=main(0) */
    rsx_texture_state tex;
    memset(&tex, 0, sizeof(tex));
    tex.offset     = ea;
    tex.format     = (0xA5u << 8) | (2u << 4) | 0u;
    tex.image_rect = (w << 16) | h;
    tex.dirty      = 1;

    fprintf(stderr,
            "[FORCE-TEX] bind unit=%u ea=0x%08X %ux%u fmt=0x%08X (native path)\n",
            unit, ea, w, h, tex.format);
    fflush(stderr);

    d3d12_bind_texture(NULL, unit, &tex);
    s_force_tex_bound++;
}

static void force_tex_flush_pending(void)
{
    if (!s_d3d.device || !s_d3d.srv_heap) return;
    for (unsigned u = 0; u < FORCE_TEX_UNITS; u++) {
        ForceTexSlot* s = &s_force_tex[u];
        if (!s->pending || !s->rgba) continue;
        force_tex_try_bind_one(u, s->rgba, s->w, s->h);
        s->pending = 0;
    }
}

void rsx_d3d12_force_bind_rgba(unsigned unit, const void* rgba, unsigned w, unsigned h)
{
    if (!rgba || !w || !h || unit >= FORCE_TEX_UNITS) return;
    ForceTexSlot* s = &s_force_tex[unit];
    free(s->rgba);
    s->rgba = (u8*)malloc((size_t)w * h * 4);
    if (!s->rgba) { s->pending = 0; return; }
    memcpy(s->rgba, rgba, (size_t)w * h * 4);
    s->w = w; s->h = h;
    s->pending = 1;

    /* Try immediately; if D3D not ready, flush on next present.
 * Clear pending after a successful try so present does not re-bind every
 * frame (that was Resetting the live command list and starving the movie). */
    if (s_d3d.device && s_d3d.srv_heap) {
        force_tex_try_bind_one(unit, s->rgba, w, h);
        s->pending = 0;
    } else {
        fprintf(stderr, "[FORCE-TEX] queued unit=%u %ux%u (D3D not ready)\n", unit, w, h);
    }
    fflush(stderr);
}

void rsx_d3d12_content_hold(const void* rgba, unsigned w, unsigned h)
{
    if (!rgba || !w || !h) return;
    free(s_content_rgba);
    s_content_rgba = (u8*)malloc((size_t)w * h * 4);
    if (!s_content_rgba) { s_content_hold = 0; return; }
    memcpy(s_content_rgba, rgba, (size_t)w * h * 4);
    s_content_w = w;
    s_content_h = h;
    s_content_hold = 1;
    if (!s_content_logged) {
        s_content_logged = 1;
        fprintf(stderr, "[CONTENT] hold stored %ux%u (HOSTRES texture)\n", w, h);
        fflush(stderr);
        /* Dump one proof frame immediately. */
        FILE* f = fopen("frame_content_00.ppm", "wb");
        if (f) {
            fprintf(f, "P6\n%u %u\n255\n", w, h);
            for (u32 y = 0; y < h; y++) {
                const u8* row = (const u8*)rgba + (size_t)y * w * 4;
                for (u32 x = 0; x < w; x++)
                    fwrite(row + x * 4, 1, 3, f);
            }
            fclose(f);
            fprintf(stderr, "[PIXELS] wrote frame_content_00.ppm %ux%u\n", w, h);
            fflush(stderr);
        }
    }
    /* Never steal the overlay while movie HLE is playing — that freezes the
     * logo on the first/legal frame while the WAV keeps playing. */
    if (s_movie_hle_playing || s_content_hold_paused) {
        fprintf(stderr, "[CONTENT] hold stored but paused (movie HLE active)\n");
        fflush(stderr);
        return;
    }
    rsx_d3d12_movie_frame(rgba, w, h);
}

void rsx_d3d12_movie_stop(void)
{
    /* End-of-movie bookkeeping (may re-arm content hold if opted in). */
    rsx_d3d12_movie_playback_end();
    /* Keep active=1 with last staged frame so the final movie picture stays
     * visible until the game or content-hold replaces it. Only drop active
     * if we never had a frame. */
    if (!s_movie.staging)
        s_movie.active = 0;
}

static const char s_movie_hlsl[] =
    "Texture2D tex0 : register(t0);\n"
    "SamplerState samp0 : register(s0);\n"
    "struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vsmain(uint id : SV_VertexID) {\n"
    "    V o; float2 uv = float2((id << 1) & 2, id & 2);\n"
    "    o.uv = uv; o.p = float4(uv * float2(2,-2) + float2(-1,1), 0, 1); return o;\n"
    "}\n"
    "float4 psmain(V i) : SV_TARGET { return float4(tex0.Sample(samp0, i.uv).rgb, 1); }\n";

static int movie_overlay_create(void)
{
    HRESULT hr;

    /* Root signature: one SRV table + one static sampler. */
    D3D12_DESCRIPTOR_RANGE range = {0};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;

    D3D12_ROOT_PARAMETER param = {0};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges = &range;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samp = {0};
    samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.MaxLOD = 1000.0f;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs = {0};
    rs.NumParameters = 1;
    rs.pParameters = &param;
    rs.NumStaticSamplers = 1;
    rs.pStaticSamplers = &samp;

    ID3DBlob* sig = NULL; ID3DBlob* err = NULL;
    hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    if (FAILED(hr)) { if (err) err->lpVtbl->Release(err); return -1; }
    hr = s_d3d.device->lpVtbl->CreateRootSignature(
        s_d3d.device, 0, sig->lpVtbl->GetBufferPointer(sig), sig->lpVtbl->GetBufferSize(sig),
        &IID_ID3D12RootSignature, (void**)&s_movie.root_sig);
    sig->lpVtbl->Release(sig);
    if (FAILED(hr)) return -1;

    ID3DBlob* vs = NULL; ID3DBlob* ps = NULL;
    if (FAILED(D3DCompile(s_movie_hlsl, sizeof(s_movie_hlsl) - 1, "movie_vs", NULL, NULL,
                          "vsmain", "vs_5_0", 0, 0, &vs, NULL))) return -1;
    if (FAILED(D3DCompile(s_movie_hlsl, sizeof(s_movie_hlsl) - 1, "movie_ps", NULL, NULL,
                          "psmain", "ps_5_0", 0, 0, &ps, NULL))) { vs->lpVtbl->Release(vs); return -1; }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {0};
    pd.pRootSignature = s_movie.root_sig;
    pd.VS.pShaderBytecode = vs->lpVtbl->GetBufferPointer(vs);
    pd.VS.BytecodeLength  = vs->lpVtbl->GetBufferSize(vs);
    pd.PS.pShaderBytecode = ps->lpVtbl->GetBufferPointer(ps);
    pd.PS.BytecodeLength  = ps->lpVtbl->GetBufferSize(ps);
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    for (int i = 0; i < 8; i++)
        pd.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = 0xFFFFFFFFu;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.SampleDesc.Count = 1;
    /* No depth: overlay always wins. */
    hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
        s_d3d.device, &pd, &IID_ID3D12PipelineState, (void**)&s_movie.pso);
    vs->lpVtbl->Release(vs); ps->lpVtbl->Release(ps);
    if (FAILED(hr)) return -1;

    D3D12_DESCRIPTOR_HEAP_DESC hd = {0};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = 1;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = s_d3d.device->lpVtbl->CreateDescriptorHeap(
        s_d3d.device, &hd, &IID_ID3D12DescriptorHeap, (void**)&s_movie.srv_heap);
    if (FAILED(hr)) return -1;

    printf("[D3D12] Movie overlay pipeline created\n");
    return 0;
}

static int movie_overlay_realloc_tex(u32 w, u32 h)
{
    if (s_movie.tex && s_movie.tex_w == w && s_movie.tex_h == h) return 0;
    wait_for_gpu();
    if (s_movie.tex)    { s_movie.tex->lpVtbl->Release(s_movie.tex);       s_movie.tex = NULL; }
    if (s_movie.upload) { s_movie.upload->lpVtbl->Release(s_movie.upload); s_movie.upload = NULL; }

    D3D12_HEAP_PROPERTIES hp = {0};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = {0};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = w; td.Height = h;
    td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void**)&s_movie.tex)))
        return -1;
    s_movie.tex_in_srv_state = 0;

    const u32 pitch = (w * 4 + 255u) & ~255u;      /* row-pitch aligned */
    D3D12_HEAP_PROPERTIES up = {0};
    up.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd = {0};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = (u64)pitch * h;
    bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &up, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Resource, (void**)&s_movie.upload)))
        return -1;

    D3D12_SHADER_RESOURCE_VIEW_DESC sd = {0};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu;
    s_movie.srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_movie.srv_heap, &hcpu);
    s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, s_movie.tex, &sd, hcpu);

    s_movie.tex_w = w; s_movie.tex_h = h;
    return 0;
}

/* Record the overlay upload+draw into the open command list (end of frame). */
static void movie_overlay_render(void)
{
    /* Do NOT flush force-tex binds here: force_tex_try_bind_one Reset()s the
     * live command list and would destroy the in-flight present. Deferred
     * binds run from d3d12_present after the frame is executed. */

    /* Sticky content only when movie HLE is not playing. */
    if (s_content_hold && s_content_rgba && !s_content_hold_paused
        && !s_movie_hle_playing && !s_movie.active)
        rsx_d3d12_movie_frame(s_content_rgba, s_content_w, s_content_h);

    if (!s_movie.active || !s_movie.cs_init) return;

    if (!s_movie.pso && movie_overlay_create() != 0) { s_movie.active = 0; return; }

    EnterCriticalSection(&s_movie.cs);
    u32 w = s_movie.w, h = s_movie.h;
    int have_frame = (s_movie.staging != NULL);
    if (have_frame && movie_overlay_realloc_tex(w, h) != 0) {
        LeaveCriticalSection(&s_movie.cs);
        s_movie.active = 0;
        return;
    }

    if (have_frame && s_movie.pending) {
        /* CPU -> upload buffer (row pitch aligned) */
        const u32 pitch = (w * 4 + 255u) & ~255u;
        void* mapped = NULL;
        D3D12_RANGE no_read = {0, 0};
        if (SUCCEEDED(s_movie.upload->lpVtbl->Map(s_movie.upload, 0, &no_read, &mapped))) {
            for (u32 y = 0; y < h; y++)
                memcpy((u8*)mapped + (size_t)y * pitch,
                       s_movie.staging + (size_t)y * w * 4, (size_t)w * 4);
            s_movie.upload->lpVtbl->Unmap(s_movie.upload, 0, NULL);
        }
        /* Optional: dump host-decoded movie frames as PPM for pixel proof.
         * Skip pure-black frames (logo fade-in); keep first 6 non-black. */
        {
            static int dump_on = -1, dump_n = 0, seen = 0;
            if (dump_on < 0) dump_on = getenv("PS3_DUMP_FRAMES") ? 1 : 0;
            if (dump_on && dump_n < 6 && s_movie.staging && w && h) {
                u64 nz = 0, tot = (u64)w * h;
                for (u64 i = 0; i < tot; i += 16) { /* subsample for speed */
                    const u8* p = s_movie.staging + i * 4;
                    if (p[0] | p[1] | p[2]) nz++;
                }
                u64 samples = (tot + 15) / 16;
                double pct = samples ? (100.0 * (double)nz / (double)samples) : 0.0;
                seen++;
                if (pct < 0.5 && seen < 300) {
                    /* still black — wait for real content */
                } else {
                    char path[128];
                    snprintf(path, sizeof(path), "frame_movie_%02d.ppm", dump_n);
                    FILE* f = fopen(path, "wb");
                    if (f) {
                        fprintf(f, "P6\n%u %u\n255\n", w, h);
                        for (u32 y = 0; y < h; y++) {
                            const u8* row = s_movie.staging + (size_t)y * w * 4;
                            for (u32 x = 0; x < w; x++)
                                fwrite(row + x * 4, 1, 3, f); /* RGBA->RGB */
                        }
                        fclose(f);
                        fprintf(stderr, "[PIXELS] wrote %s %ux%u nonzero~%.1f%% seen=%d\n",
                                path, w, h, pct, seen);
                        fflush(stderr);
                        dump_n++;
                    }
                }
            }
        }
        s_movie.pending = 0;

        /* upload -> texture */
        if (s_movie.tex_in_srv_state) {
            D3D12_RESOURCE_BARRIER b = {0};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = s_movie.tex;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
            s_movie.tex_in_srv_state = 0;
        }
        D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
        dst.pResource = s_movie.tex;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource = s_movie.upload;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        src.PlacedFootprint.Footprint.Width = w;
        src.PlacedFootprint.Footprint.Height = h;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = pitch;
        s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);
    }
    LeaveCriticalSection(&s_movie.cs);

    if (!s_movie.tex) return;

    if (!s_movie.tex_in_srv_state) {
        D3D12_RESOURCE_BARRIER b = {0};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = s_movie.tex;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
        s_movie.tex_in_srv_state = 1;
    }

    s_d3d.cmd_list->lpVtbl->SetGraphicsRootSignature(s_d3d.cmd_list, s_movie.root_sig);
    s_d3d.cmd_list->lpVtbl->SetPipelineState(s_d3d.cmd_list, s_movie.pso);
    ID3D12DescriptorHeap* heaps[] = { s_movie.srv_heap };
    s_d3d.cmd_list->lpVtbl->SetDescriptorHeaps(s_d3d.cmd_list, 1, heaps);
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu;
    s_movie.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_movie.srv_heap, &hgpu);
    s_d3d.cmd_list->lpVtbl->SetGraphicsRootDescriptorTable(s_d3d.cmd_list, 0, hgpu);
    s_d3d.cmd_list->lpVtbl->IASetPrimitiveTopology(s_d3d.cmd_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    s_d3d.cmd_list->lpVtbl->DrawInstanced(s_d3d.cmd_list, 3, 1, 0, 0);
}

static u64 d3d12_now_ns(void)
{
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (frequency.QuadPart == 0)
        QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (u64)(counter.QuadPart / frequency.QuadPart) * 1000000000ull +
           (u64)(counter.QuadPart % frequency.QuadPart) * 1000000000ull /
               (u64)frequency.QuadPart;
}

/* VSync is the one graphics setting D3D12 applies live: Present's sync
 * interval is chosen per call. Without an initialized overlay it stays on. */
static UINT d3d12_sync_interval(void)
{
    rsx_overlay_graphics_settings graphics;
    if (rsx_overlay_get_graphics_settings(&graphics))
        return graphics.vsync ? 1u : 0u;
    return 1u;
}

static void render_frame(void)
{
    u32 fi = s_d3d.frame_index;
    /* Guest draws of this frame, sampled before any overlay submission so UI
     * draws can never be counted as guest work. */
    const u32 guest_draws = s_d3d.draw_count > MAX_DRAWS ? MAX_DRAWS : s_d3d.draw_count;

    /* Reset command allocator and list */
    s_d3d.cmd_allocators[fi]->lpVtbl->Reset(s_d3d.cmd_allocators[fi]);
    s_d3d.cmd_list->lpVtbl->Reset(s_d3d.cmd_list, s_d3d.cmd_allocators[fi], NULL);

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

    /* Set render target + depth */
    s_d3d.cmd_list->lpVtbl->OMSetRenderTargets(s_d3d.cmd_list, 1, &rtv_handle, FALSE, &dsv_handle);

    /* Clear color and depth */
    s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(
        s_d3d.cmd_list, rtv_handle, s_d3d.clear_color, 0, NULL);
    s_d3d.cmd_list->lpVtbl->ClearDepthStencilView(
        s_d3d.cmd_list, dsv_handle,
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f, 0, 0, NULL);

    /* Set viewport and scissor */
    D3D12_VIEWPORT viewport = {0, 0, (float)s_d3d.width, (float)s_d3d.height, 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, (LONG)s_d3d.width, (LONG)s_d3d.height};
    s_d3d.cmd_list->lpVtbl->RSSetViewports(s_d3d.cmd_list, 1, &viewport);
    s_d3d.cmd_list->lpVtbl->RSSetScissorRects(s_d3d.cmd_list, 1, &scissor);

    /* Bind pipeline state and push MVP if anything to draw */
    if (s_d3d.pipeline_ready && s_d3d.draw_count > 0) {
        s_d3d.cmd_list->lpVtbl->SetGraphicsRootSignature(s_d3d.cmd_list, s_d3d.root_signature);
        s_d3d.cmd_list->lpVtbl->IASetVertexBuffers(s_d3d.cmd_list, 0, 1, &s_d3d.vb_view);

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

        /* Bind the shared texture/sampler descriptor tables once per frame —
         * every PSO shares the same root signature (task: root sig expansion),
         * so this satisfies D3D12's binding requirements uniformly whether or
         * not the active shader actually samples a texture. */
        ID3D12DescriptorHeap* heaps[] = { s_d3d.srv_heap, s_d3d.sampler_heap };
        if (heaps[0] && heaps[1]) {
            s_d3d.cmd_list->lpVtbl->SetDescriptorHeaps(s_d3d.cmd_list, 2, heaps);
            D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu, sampler_gpu;
            s_d3d.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &srv_gpu);
            s_d3d.sampler_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.sampler_heap, &sampler_gpu);
            s_d3d.cmd_list->lpVtbl->SetGraphicsRootDescriptorTable(s_d3d.cmd_list, 1, srv_gpu);
            s_d3d.cmd_list->lpVtbl->SetGraphicsRootDescriptorTable(s_d3d.cmd_list, 2, sampler_gpu);
        }

        /* Replay each recorded draw with its own primitive topology and the
         * matching PSO class (triangle / line / point) from whichever shader
         * (decompiled or the fixed fallback) was active when it was queued.
         * The PSO class must match the topology or D3D12 rejects the draw. */
        u32 last_topo = 0xFFFFFFFFu;
        ID3D12PipelineState* last_pso = NULL;
        u32 draws = s_d3d.draw_count;
        if (draws > MAX_DRAWS) draws = MAX_DRAWS;
        for (u32 d = 0; d < draws; d++) {
            const D3D12DrawRecord* dr = &s_d3d.draws[d];

            ID3D12PipelineState* tri_pso   = dr->shader && dr->shader->tri   ? dr->shader->tri   : s_d3d.pipeline_state;
            ID3D12PipelineState* line_pso  = dr->shader && dr->shader->line  ? dr->shader->line  : s_d3d.pipeline_state_lines;
            ID3D12PipelineState* point_pso = dr->shader && dr->shader->point ? dr->shader->point : s_d3d.pipeline_state_points;

            ID3D12PipelineState* target_pso = tri_pso; /* default triangle */
            if (dr->topology == D3D_TOPOLOGY_POINTLIST) {
                target_pso = point_pso ? point_pso : tri_pso;
            } else if (dr->topology == D3D_TOPOLOGY_LINELIST ||
                       dr->topology == D3D_TOPOLOGY_LINESTRIP) {
                target_pso = line_pso ? line_pso : tri_pso;
            }
            if (target_pso != last_pso) {
                s_d3d.cmd_list->lpVtbl->SetPipelineState(s_d3d.cmd_list, target_pso);
                last_pso = target_pso;
            }
            if (dr->topology != last_topo) {
                s_d3d.cmd_list->lpVtbl->IASetPrimitiveTopology(s_d3d.cmd_list, dr->topology);
                last_topo = dr->topology;
            }
            if (dr->is_indexed) {
                D3D12_INDEX_BUFFER_VIEW ibv = {0};
                ibv.BufferLocation = s_d3d.index_buffer->lpVtbl->GetGPUVirtualAddress(s_d3d.index_buffer) + dr->ib_byte_offset;
                u32 elem_size = (dr->index_format == DXGI_FORMAT_R32_UINT) ? 4 : 2;
                ibv.SizeInBytes = dr->vertex_count * elem_size;
                ibv.Format = (DXGI_FORMAT)dr->index_format;
                s_d3d.cmd_list->lpVtbl->IASetIndexBuffer(s_d3d.cmd_list, &ibv);
                /* Indices are 0-based into the vertex range this draw uploaded
                 * (upload_vertices_from_rsx(0, max_index+1)); BaseVertexLocation
                 * points at where that range landed in the shared VB this frame. */
                u32 base_vertex = dr->vb_byte_offset / 28;
                s_d3d.cmd_list->lpVtbl->DrawIndexedInstanced(
                    s_d3d.cmd_list, dr->vertex_count, 1, 0, (INT)base_vertex, 0);
            } else {
                u32 start_vert = dr->vb_byte_offset / 28; /* 28 bytes per BasicVertex */
                s_d3d.cmd_list->lpVtbl->DrawInstanced(
                    s_d3d.cmd_list, dr->vertex_count, 1, start_vert, 0);
            }
        }
    }
    s_d3d.vb_offset  = 0; /* reset for next frame */
    s_d3d.ib_offset  = 0;
    s_d3d.draw_count = 0;

    /* Host-decoded movie frames draw on top of whatever the game produced. */
    movie_overlay_render();

    /* Runtime overlay UI: last pass, after guest and movie composition. No-op
     * while the overlay is hidden. */
    if (s_d3d.overlay)
        rsx_overlay_d3d12_renderer_render(s_d3d.overlay, s_d3d.cmd_list, fi,
                                          s_d3d.width, s_d3d.height);

    /* Transition render target to PRESENT state */
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &barrier);

    /* Close and execute */
    s_d3d.cmd_list->lpVtbl->Close(s_d3d.cmd_list);
    ID3D12CommandList* cmd_lists[] = {(ID3D12CommandList*)s_d3d.cmd_list};
    s_d3d.cmd_queue->lpVtbl->ExecuteCommandLists(s_d3d.cmd_queue, 1, cmd_lists);

    /* Present */
    s_d3d.swap_chain->lpVtbl->Present(s_d3d.swap_chain, d3d12_sync_interval(), 0);

    move_to_next_frame();

    /* Present-to-present timing (includes the vsync/fence wait above). */
    rsx_overlay_frame_timer_tick(&s_d3d.frame_timer, d3d12_now_ns());
    s_d3d.guest_draws_total += guest_draws;
    if (s_d3d.frame_timer.fps_valid) {
        rsx_overlay_backend_metrics metrics;
        metrics.fps = s_d3d.frame_timer.fps;
        metrics.frame_ms = s_d3d.frame_timer.frame_ms;
        metrics.guest_draws_frame = guest_draws;
        metrics.guest_draws_total = s_d3d.guest_draws_total;
        rsx_overlay_publish_backend_metrics(&metrics);
        s_d3d.fps = (u32)(s_d3d.frame_timer.fps + 0.5);
    }
    /* Shader counters bumped by d3d12_set_shader, published once per present.
     * compiles = FP D3DCompile successes; failures = FP and VP
     * decompile/compile failures that fell back. */
    {
        rsx_overlay_shader_stats shaders;
        shaders.hits = s_fp_cache_hits;
        shaders.misses = s_fp_cache_misses;
        shaders.compiles = s_fp_decompile_ok;
        shaders.failures = s_fp_decompile_fail + s_vp_decompile_fail;
        rsx_overlay_publish_shader_stats(&shaders);
    }

    /* Safe place for deferred FORCE-TEX binds (own one-shot cmd list). */
    force_tex_flush_pending();

    s_d3d.frame_count++;
    /* Lightweight draw proof line (opt-in). */
    {
        static int px_on = -1;
        if (px_on < 0) px_on = getenv("PS3_DUMP_FRAMES") ? 1 : 0;
        if (px_on && (s_d3d.frame_count <= 8 || (s_d3d.frame_count & 31) == 0)) {
            fprintf(stderr, "[PIXELS] present frame=%llu draws_this_slot_reset draws_prev_log_via_RSX\n",
                    (unsigned long long)s_d3d.frame_count);
            fflush(stderr);
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

static void d3d12_begin_frame(void* ud)
{
    (void)ud;
}

static void d3d12_end_frame(void* ud)
{
    (void)ud;
}

static void d3d12_present(void* ud, u32 buffer_id)
{
    (void)ud;
    (void)buffer_id;

    if (s_d3d.initialized)
        render_frame();

    /* FPS/frame time are measured in render_frame (elapsed-time timer). This
     * window only paces the shader-pipeline summary below. */
    ULONGLONG now = GetTickCount64();
    if (now - s_d3d.last_fps_time >= 1000) {
        s_d3d.last_fps_time = now;

        /* Content-pipeline health summary (Fase A observability), once per
         * ~second window. The real game's render loop never returns to
         * rsx_d3d12_backend_shutdown() (it's an infinite loop inside the
         * recompiled PPU code, only ever killed externally), so this is the
         * only reliable place to observe the counters during a live boot. */
        if (getenv("PS3_TRACE_RSX_SHADERS")) {
            fprintf(stderr, "[RSX-SH] hits=%u misses=%u ok=%u fail=%u cache=%u\n",
                    s_fp_cache_hits, s_fp_cache_misses, s_fp_decompile_ok,
                    s_fp_decompile_fail, s_d3d.shader_cache_count);
        }
    }
}

static void d3d12_clear(void* ud, u32 flags, u32 color, float depth, u8 stencil)
{
    (void)ud;
    (void)flags;
    (void)depth;
    (void)stencil;

    /* Convert RSX ARGB u32 to float[4] RGBA */
    s_d3d.clear_color[0] = ((color >> 16) & 0xFF) / 255.0f; /* R */
    s_d3d.clear_color[1] = ((color >> 8) & 0xFF) / 255.0f;  /* G */
    s_d3d.clear_color[2] = (color & 0xFF) / 255.0f;          /* B */
    s_d3d.clear_color[3] = ((color >> 24) & 0xFF) / 255.0f;  /* A */
}

static void d3d12_set_render_target(void* ud, const rsx_state* state)
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

static void d3d12_set_viewport(void* ud, const rsx_state* state)
{
    (void)ud;
    /* TODO: update D3D12 viewport from RSX state */
    (void)state;
}

/* Helper: read RSX vertex data from guest memory and convert to our BasicVertex format.
 * Reads position (float3) from attrib 0 and color (float4) from attrib 3.
 * If attribs aren't available, generates placeholder geometry.
 * Returns the actual number of vertices uploaded (may be less than `count`
 * if the per-frame buffer is full). */
static u32 upload_vertices_from_rsx(u32 first, u32 count)
{
    extern uint8_t* vm_base;
    typedef struct { float x, y, z; float r, g, b, a; } BasicVertex;
    BasicVertex* verts = (BasicVertex*)((u8*)s_d3d.vb_mapped + s_d3d.vb_offset);
    u32 max_verts = (MAX_VERTICES * 28 - s_d3d.vb_offset) / sizeof(BasicVertex);
    if (count > max_verts) count = max_verts;

    const rsx_state* state = s_d3d.current_rsx_state;
    int has_position = 0;
    int has_color = 0;

    /* Check if position attrib (typically attrib 0) is enabled and is float */
    if (state) {
        const rsx_vertex_attrib* pos = &state->vertex_attribs[0];
        if (pos->enabled && pos->type == 2 /* float */ && pos->size >= 3) {
            has_position = 1;
        }
        /* Color is typically attrib 3 (diffuse) */
        const rsx_vertex_attrib* col = &state->vertex_attribs[3];
        if (col->enabled && col->size >= 3) {
            has_color = 1;
        }
    }

    for (u32 i = 0; i < count; i++) {
        if (has_position && vm_base) {
            /* Read position from guest memory.
             * RSX stores vertices in big-endian. For float type, each component
             * is a 32-bit BE float. We need to byte-swap. */
            const rsx_vertex_attrib* pos = &state->vertex_attribs[0];
            u32 addr = pos->offset + (first + i) * pos->stride;
            if (addr < 0x20000000) { /* sanity check */
                u8* src = vm_base + addr;
                /* Byte-swap each float component (BE → LE) */
                u32 fx, fy, fz;
                memcpy(&fx, src,     4); fx = ((fx>>24)&0xFF)|((fx>>8)&0xFF00)|((fx<<8)&0xFF0000)|((fx<<24)&0xFF000000);
                memcpy(&fy, src + 4, 4); fy = ((fy>>24)&0xFF)|((fy>>8)&0xFF00)|((fy<<8)&0xFF0000)|((fy<<24)&0xFF000000);
                memcpy(&fz, src + 8, 4); fz = ((fz>>24)&0xFF)|((fz>>8)&0xFF00)|((fz<<8)&0xFF0000)|((fz<<24)&0xFF000000);
                memcpy(&verts[i].x, &fx, 4);
                memcpy(&verts[i].y, &fy, 4);
                memcpy(&verts[i].z, &fz, 4);
            } else {
                verts[i].x = verts[i].y = verts[i].z = 0.0f;
            }
        } else {
            /* Placeholder: distribute vertices in a circle */
            float t = (float)(first + i) / 100.0f;
            verts[i].x = sinf(t * 6.28f) * 0.5f;
            verts[i].y = cosf(t * 6.28f) * 0.5f;
            verts[i].z = 0.0f;
        }

        if (has_color && vm_base) {
            const rsx_vertex_attrib* col = &state->vertex_attribs[3];
            u32 addr = col->offset + (first + i) * col->stride;
            if (addr < 0x20000000 && col->type == 4 /* ubyte */) {
                u8* src = vm_base + addr;
                /* RGBA bytes, normalized to 0-1 */
                verts[i].r = src[0] / 255.0f;
                verts[i].g = src[1] / 255.0f;
                verts[i].b = src[2] / 255.0f;
                verts[i].a = (col->size >= 4) ? src[3] / 255.0f : 1.0f;
            } else if (addr < 0x20000000 && col->type == 2 /* float */) {
                u8* src = vm_base + addr;
                u32 fr, fg, fb, fa;
                memcpy(&fr, src,     4); fr = ((fr>>24)&0xFF)|((fr>>8)&0xFF00)|((fr<<8)&0xFF0000)|((fr<<24)&0xFF000000);
                memcpy(&fg, src + 4, 4); fg = ((fg>>24)&0xFF)|((fg>>8)&0xFF00)|((fg<<8)&0xFF0000)|((fg<<24)&0xFF000000);
                memcpy(&fb, src + 8, 4); fb = ((fb>>24)&0xFF)|((fb>>8)&0xFF00)|((fb<<8)&0xFF0000)|((fb<<24)&0xFF000000);
                memcpy(&verts[i].r, &fr, 4);
                memcpy(&verts[i].g, &fg, 4);
                memcpy(&verts[i].b, &fb, 4);
                if (col->size >= 4) {
                    memcpy(&fa, src + 12, 4); fa = ((fa>>24)&0xFF)|((fa>>8)&0xFF00)|((fa<<8)&0xFF0000)|((fa<<24)&0xFF000000);
                    memcpy(&verts[i].a, &fa, 4);
                } else {
                    verts[i].a = 1.0f;
                }
            } else {
                verts[i].r = 1.0f; verts[i].g = 1.0f; verts[i].b = 1.0f; verts[i].a = 1.0f;
            }
        } else {
            /* Default white */
            verts[i].r = 1.0f; verts[i].g = 1.0f; verts[i].b = 1.0f; verts[i].a = 1.0f;
        }
    }
    s_d3d.vb_offset += count * sizeof(BasicVertex);
    return count;
}

static void d3d12_draw_arrays(void* ud, u32 primitive, u32 first, u32 count)
{
    (void)ud;
    /* Log the first 20 calls in detail, then every 1000th to show liveness
     * without flooding. */
    static u64 s_total = 0;
    if (s_total < 20 || (s_total % 1000) == 0) {
        printf("[D3D12] draw_arrays #%llu prim=%u first=%u count=%u\n",
               (unsigned long long)s_total, primitive, first, count);
    }
    s_total++;

    if (!s_d3d.pipeline_ready || !s_d3d.vb_mapped) return;
    if (count == 0 || count > MAX_VERTICES) return;

    /* One-shot: dump the MVP and first vertex on the very first draw so we
     * can see what coordinate space the game is sending positions in. */
    static int s_dumped = 0;
    if (!s_dumped && s_d3d.current_rsx_state) {
        extern uint8_t* vm_base;
        s_dumped = 1;
        const rsx_state* st = s_d3d.current_rsx_state;
        printf("[D3D12-DUMP] vertex_constants slots 0..7:\n");
        for (u32 i = 0; i < 8; i++) {
            printf("  [%u] = (% .4f, % .4f, % .4f, % .4f)\n", i,
                   st->vertex_constants[i][0], st->vertex_constants[i][1],
                   st->vertex_constants[i][2], st->vertex_constants[i][3]);
        }
        printf("[D3D12-DUMP] vc dirty=%d range=[%u..%u]\n",
               st->vertex_constants_dirty,
               st->vertex_constants_lo, st->vertex_constants_hi);
        printf("[D3D12-DUMP] viewport=%ux%u clip=%ux%u\n",
               st->viewport_w, st->viewport_h,
               st->surface_clip_w, st->surface_clip_h);
        const rsx_vertex_attrib* pos = &st->vertex_attribs[0];
        printf("[D3D12-DUMP] attrib0: enabled=%d type=%u size=%u stride=%u offset=0x%08X\n",
               pos->enabled, pos->type, pos->size, pos->stride, pos->offset);
        if (pos->enabled && pos->type == 2 && vm_base) {
            for (u32 v = 0; v < (count < 4 ? count : 4); v++) {
                u32 addr = pos->offset + (first + v) * pos->stride;
                if (addr >= 0x20000000) break;
                u8* src = vm_base + addr;
                u32 fx, fy, fz;
                memcpy(&fx, src,     4); fx = ((fx>>24)&0xFF)|((fx>>8)&0xFF00)|((fx<<8)&0xFF0000)|((fx<<24)&0xFF000000);
                memcpy(&fy, src + 4, 4); fy = ((fy>>24)&0xFF)|((fy>>8)&0xFF00)|((fy<<8)&0xFF0000)|((fy<<24)&0xFF000000);
                memcpy(&fz, src + 8, 4); fz = ((fz>>24)&0xFF)|((fz>>8)&0xFF00)|((fz<<8)&0xFF0000)|((fz<<24)&0xFF000000);
                float x, y, z;
                memcpy(&x, &fx, 4); memcpy(&y, &fy, 4); memcpy(&z, &fz, 4);
                printf("[D3D12-DUMP] v[%u] pos=(% .4f, % .4f, % .4f)\n", v, x, y, z);
            }
        }
    }

    u32 topo = rsx_to_d3d12_topology(primitive);

    /* QUADS/TRIANGLE_FAN map to D3D_TOPOLOGY_TRIANGLELIST here (not
     * UNDEFINED), so the skip-guard below never caught them and the raw
     * [first,count) vertex range went out as if it were already a
     * triangle list. Build a real index buffer with the existing,
     * already-correct helpers instead -- same triangulation
     * rsx_live_draw.c's inline QUADS/TRIANGLE_FAN expansion uses (see
     * libs/video/tests/test_rsx_geom.c). Indices are local (first=0) to
     * the vertex slice this draw uploads; BaseVertexLocation (derived from
     * vb_byte_offset at render time, same as the indexed path below)
     * anchors that slice. */
    if (rsx_prim_needs_conversion(primitive)) {
        if (!s_d3d.ib_mapped) return;

        static u32 idx_scratch[MAX_INDICES];
        u32 n_idx = rsx_expand_prim_indices(primitive, 0, count, idx_scratch, MAX_INDICES);
        if (!n_idx) return;

        u32 record_vb_offset = s_d3d.vb_offset;
        u32 actual_count = upload_vertices_from_rsx(first, count);
        if (actual_count < count) return; /* per-frame VB full: drop rather than sample garbage */

        if (s_d3d.ib_offset + n_idx * 4 > s_d3d.ib_capacity_bytes) return; /* per-frame IB full */
        u32 record_ib_offset = s_d3d.ib_offset;
        memcpy((u8*)s_d3d.ib_mapped + record_ib_offset, idx_scratch, (size_t)n_idx * 4);
        s_d3d.ib_offset += n_idx * 4;

        if (s_d3d.draw_count < MAX_DRAWS) {
            D3D12DrawRecord* dr = &s_d3d.draws[s_d3d.draw_count];
            dr->vb_byte_offset = record_vb_offset;
            dr->vertex_count   = n_idx;   /* index count for indexed draws */
            dr->topology       = topo;    /* TRIANGLELIST */
            dr->is_indexed     = 1;
            dr->ib_byte_offset = record_ib_offset;
            dr->index_format   = DXGI_FORMAT_R32_UINT; /* synthesized indices are always u32 */
            dr->shader         = s_d3d.current_shader;
            s_d3d.draw_count++;
        }
        return;
    }

    if (topo == D3D_TOPOLOGY_UNDEFINED) {
        /* Skip primitives that still need index-buffer conversion
         * (line loops, quad strips) rather than silently
         * rendering them as the wrong shape. */
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
        D3D12DrawRecord* dr = &s_d3d.draws[s_d3d.draw_count];
        dr->vb_byte_offset = record_offset;
        dr->vertex_count   = actual_count;
        dr->topology       = topo;
        dr->is_indexed     = 0;
        dr->shader         = s_d3d.current_shader; /* NULL = fixed fallback PSO */
        s_d3d.draw_count++;
    }
}

static void d3d12_draw_indexed(void* ud, u32 primitive, u32 offset, u32 count)
{
    (void)ud;
    static int log_count = 0;
    if (log_count < 20) {
        printf("[D3D12] draw_indexed(prim=%u, offset=%u, count=%u)\n",
               primitive, offset, count);
        log_count++;
    }

    if (!s_d3d.pipeline_ready || !s_d3d.ib_mapped || !s_d3d.vb_mapped) return;
    if (count == 0 || count > MAX_INDICES) return;

    extern uint8_t* vm_base;
    extern int ppu_guest_range_committed(uint32_t addr, uint32_t n);
    const rsx_state* state = s_d3d.current_rsx_state;
    if (!vm_base || !state) return;

    u32 topo = rsx_to_d3d12_topology(primitive);
    if (topo == D3D_TOPOLOGY_UNDEFINED) {
        static int s_skipped = 0;
        if (s_skipped < 3) { printf("[D3D12] draw_indexed: skipping prim=%u (needs index conversion)\n", primitive); s_skipped++; }
        return;
    }

    u32 elem_size = state->index_is_16bit ? 2 : 4;
    u32 loc = state->index_location;
    u32 ea  = (loc == 0) ? (0xC0000000u + state->index_array_addr) : state->index_array_addr;
    u32 read_addr = ea + offset * elem_size;
    if (!ppu_guest_range_committed(read_addr, count * elem_size)) return;

    /* Read + byte-swap every index up front (need the max value before we
     * know how much of the vertex attribute range to upload). */
    static u32 idx_scratch[MAX_INDICES];
    const u8* isrc = vm_base + read_addr;
    u32 max_index = 0;
    for (u32 i = 0; i < count; i++) {
        u32 v;
        if (elem_size == 2) {
            u16 v16; memcpy(&v16, isrc + (size_t)i * 2, 2);
            v = (u16)((v16 << 8) | (v16 >> 8));
        } else {
            memcpy(&v, isrc + (size_t)i * 4, 4);
            v = __builtin_bswap32(v);
        }
        idx_scratch[i] = v;
        if (v > max_index) max_index = v;
    }

    /* QUADS/TRIANGLE_FAN: the guest's index array names one guest vertex per
     * quad/fan *sequence position*; the sequence itself still needs
     * triangulating (2 tris/quad, N-2 tris/fan), same as draw_arrays above.
     * Re-triangulate the sequence POSITIONS with the existing, already-
     * correct helpers (first=0), then remap each generated position through
     * the guest's index VALUES -- position j in the sequence names guest
     * vertex idx_scratch[j], not vertex j itself. Without this, QUADS/
     * TRIANGLE_FAN was submitted as a raw TRIANGLELIST over the untouched
     * index array (see libs/video/tests/test_rsx_geom.c, *_indexed_wiring
     * tests). This is a self-contained early return; the non-converted tail
     * below is untouched for every other primitive. */
    if (rsx_prim_needs_conversion(primitive)) {
        static u32 pos_scratch[MAX_INDICES];
        static u32 tri_idx[MAX_INDICES];
        u32 n_idx = rsx_expand_indexed_prim(primitive, idx_scratch, count,
                                            pos_scratch, MAX_INDICES,
                                            tri_idx, MAX_INDICES);
        if (!n_idx) return;

        u32 record_vb_offset = s_d3d.vb_offset;
        u32 needed_verts = max_index + 1;
        u32 actual_verts = upload_vertices_from_rsx(0, needed_verts);
        if (actual_verts < needed_verts) return; /* per-frame VB full: drop rather than sample garbage */

        if (s_d3d.ib_offset + n_idx * 4 > s_d3d.ib_capacity_bytes) return; /* per-frame IB full */
        u32 record_ib_offset = s_d3d.ib_offset;
        memcpy((u8*)s_d3d.ib_mapped + record_ib_offset, tri_idx, (size_t)n_idx * 4);
        s_d3d.ib_offset += n_idx * 4;

        if (s_d3d.draw_count < MAX_DRAWS) {
            D3D12DrawRecord* dr = &s_d3d.draws[s_d3d.draw_count];
            dr->vb_byte_offset = record_vb_offset;
            dr->vertex_count   = n_idx; /* index count for indexed draws */
            dr->topology       = topo;  /* TRIANGLELIST */
            dr->is_indexed     = 1;
            dr->ib_byte_offset = record_ib_offset;
            dr->index_format   = DXGI_FORMAT_R32_UINT; /* synthesized indices are always u32 */
            dr->shader         = s_d3d.current_shader;
            s_d3d.draw_count++;
        }
        return;
    }

    /* Upload vertex attribute data for [0, max_index] so index values map
     * 1:1 onto this draw's slice of the shared vertex buffer (BaseVertexLocation
     * anchors that slice — see render_frame). */
    u32 record_vb_offset = s_d3d.vb_offset;
    u32 needed_verts = max_index + 1;
    u32 actual_verts = upload_vertices_from_rsx(0, needed_verts);
    if (actual_verts < needed_verts) return; /* per-frame VB full: drop rather than sample garbage */

    if (s_d3d.ib_offset + count * elem_size > s_d3d.ib_capacity_bytes) return; /* per-frame IB full */
    u32 record_ib_offset = s_d3d.ib_offset;
    memcpy((u8*)s_d3d.ib_mapped + record_ib_offset, idx_scratch, (size_t)count * elem_size);
    s_d3d.ib_offset += count * elem_size;

    if (s_d3d.draw_count < MAX_DRAWS) {
        D3D12DrawRecord* dr = &s_d3d.draws[s_d3d.draw_count];
        dr->vb_byte_offset = record_vb_offset;
        dr->vertex_count   = count; /* index count for indexed draws */
        dr->topology       = topo;
        dr->is_indexed     = 1;
        dr->ib_byte_offset = record_ib_offset;
        dr->index_format   = state->index_is_16bit ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        dr->shader         = s_d3d.current_shader;
        s_d3d.draw_count++;
    }
}

/* Resolve the guest effective address of a texture's pixel data.
 *
 * The NV4097_SET_TEXTURE_FORMAT register encodes the memory location in bits
 * [1:0] (gcm/RPCS3: location() == (reg & 0x3) - 1, so raw 1 = LOCAL/RSX VRAM,
 * raw 2 = MAIN). This mirrors the index-array path in d3d12_draw_indexed,
 * where local memory (loc==0 after normalization) is mapped at 0xC0000000.
 *
 * Conservative decode: only raw==1 (explicit LOCAL) is routed to the RSX
 * local-memory window at 0xC0000000 + offset. raw==2 (MAIN) and the ambiguous
 * raw==0 (bits unset) keep the pre-existing behavior of treating offset as a
 * direct main-memory guest EA — so this cannot break textures that already
 * uploaded. */
static inline u32 resolve_texture_ea(const rsx_texture_state* tex)
{
    u32 loc = tex->format & 0x3u;
    if (loc == 1) return 0xC0000000u + tex->offset; /* RSX local memory (VRAM) */
    return tex->offset;                             /* main memory / unset */
}

static void d3d12_bind_texture(void* ud, u32 unit, const rsx_texture_state* tex)
{
    (void)ud;
    extern uint8_t* vm_base;
    extern int ppu_guest_range_committed(uint32_t addr, uint32_t n);

    u32 width  = (tex->image_rect >> 16) & 0xFFFF;
    u32 height = tex->image_rect & 0xFFFF;
    u32 format = rsx_texture_get_format(tex->format); /* base format, LN/UN stripped */
    u32 offset = tex->offset;
    u32 ea     = resolve_texture_ea(tex);
    int is_swizzled = rsx_texture_is_swizzled(tex->format);

    /* Always log non-movie sizes (content-pixel signal); cap full-HD logs. */
    {
        static int log_hd = 0, log_other = 0;
        int is_hd = (width == 1280 && height == 720);
        if ((!is_hd && log_other < 64) || (is_hd && log_hd < 8)) {
            printf("[D3D12] bind_texture(unit=%u, offset=0x%X, ea=0x%08X, fmt=0x%02X, %ux%u, swizzled=%d)%s\n",
                   unit, offset, ea, format, width, height, is_swizzled,
                   is_hd ? "" : " [CONTENT?]");
            if (is_hd) log_hd++; else log_other++;
        }
    }

    if (!vm_base || width == 0 || height == 0 || unit >= RSX_MAX_TEXTURES) return;
    if (!s_d3d.srv_heap) return; /* descriptor tables not ready */

    u32 dxgi_fmt_raw = rsx_to_dxgi_texture_format(format);
    if (dxgi_fmt_raw == 0) {
        static int fmt_log = 0;
        if (fmt_log++ < 5) printf("[D3D12] bind_texture: unit=%u fmt=0x%02X has no DXGI mapping, skipping\n", unit, format);
        return;
    }
    DXGI_FORMAT dxgi_fmt = (DXGI_FORMAT)dxgi_fmt_raw;

    int is_bc = (dxgi_fmt == DXGI_FORMAT_BC1_UNORM || dxgi_fmt == DXGI_FORMAT_BC2_UNORM || dxgi_fmt == DXGI_FORMAT_BC3_UNORM);
    u32 bpp = rsx_texture_bpp(format);
    u32 row_bytes, src_rows;
    if (is_bc) {
        u32 block_w = (width + 3) / 4;
        src_rows = (height + 3) / 4;
        row_bytes = block_w * (dxgi_fmt == DXGI_FORMAT_BC1_UNORM ? 8u : 16u);
    } else {
        src_rows = height;
        row_bytes = width * (bpp / 8);
    }
    /* Morton deswizzle applies only to uncompressed linear-swizzled formats;
     * BC/DXT blocks are never Morton swizzled -> treat as linear (verbatim). */
    int do_deswizzle = is_swizzled && !is_bc;

    /* Committed-page guard before any guest read. A swizzled surface is stored
     * in a power-of-two padded Morton block, so its guest footprint can exceed
     * width*height -- guard the padded extent to cover every byte the
     * deswizzle will touch. */
    u32 guest_bytes = do_deswizzle
        ? rsx_morton_padded_texels(width, height) * (bpp / 8u)
        : row_bytes * src_rows;
    if (!ppu_guest_range_committed(ea, guest_bytes)) {
        static int nc_log = 0;
        if (nc_log++ < 5)
            printf("[D3D12] bind_texture: unit=%u ea=0x%08X (%u bytes) not committed, skipping\n",
                   unit, ea, guest_bytes);
        return;
    }

    /* For swizzled surfaces, deswizzle the guest Morton data into a tightly
     * packed linear scratch buffer that the upload/byte-swap loop reads from
     * instead of guest memory directly. */
    u8* linear_tmp = NULL;
    const u8* pixel_src = vm_base + ea;
    if (do_deswizzle) {
        u32 lin_bytes = width * height * (bpp / 8u);
        linear_tmp = (u8*)malloc(lin_bytes);
        if (!linear_tmp) {
            printf("[D3D12] bind_texture: unit=%u deswizzle alloc failed (%u bytes)\n", unit, lin_bytes);
            return;
        }
        rsx_deswizzle_texels(vm_base + ea, linear_tmp, width, height, bpp / 8u);
        pixel_src = linear_tmp;
    }

    HRESULT hr;
    D3D12_HEAP_PROPERTIES default_heap = {0};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC tex_desc = {0};
    tex_desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Width            = width;
    tex_desc.Height           = height;
    tex_desc.DepthOrArraySize = 1;
    tex_desc.MipLevels        = 1;
    tex_desc.Format           = dxgi_fmt;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ID3D12Resource* new_tex = NULL;
    hr = s_d3d.device->lpVtbl->CreateCommittedResource(
        s_d3d.device, &default_heap, D3D12_HEAP_FLAG_NONE, &tex_desc,
        D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&new_tex);
    if (FAILED(hr)) { printf("[D3D12] bind_texture: resource creation failed unit=%u (0x%08lX)\n", unit, hr); free(linear_tmp); return; }

    u32 aligned_row  = (row_bytes + 255u) & ~255u; /* D3D12_TEXTURE_DATA_PITCH_ALIGNMENT */
    u32 upload_bytes = aligned_row * src_rows;

    D3D12_HEAP_PROPERTIES upload_heap = {0};
    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC upload_desc = {0};
    upload_desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    upload_desc.Width            = upload_bytes;
    upload_desc.Height           = 1;
    upload_desc.DepthOrArraySize = 1;
    upload_desc.MipLevels        = 1;
    upload_desc.SampleDesc.Count = 1;
    upload_desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* upload_buf = NULL;
    hr = s_d3d.device->lpVtbl->CreateCommittedResource(
        s_d3d.device, &upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&upload_buf);
    if (FAILED(hr)) {
        new_tex->lpVtbl->Release(new_tex);
        printf("[D3D12] bind_texture: upload buffer failed unit=%u (0x%08lX)\n", unit, hr);
        free(linear_tmp);
        return;
    }

    /* Copy from guest (big-endian) memory, byte-swapping per-texel for the
     * uncompressed formats; block-compressed (DXT) data is copied verbatim —
     * BC blocks are byte streams, not multi-byte scalars. */
    void* mapped = NULL;
    D3D12_RANGE no_read = {0, 0};
    upload_buf->lpVtbl->Map(upload_buf, 0, &no_read, &mapped);
    const u8* src = pixel_src;
    for (u32 y = 0; y < src_rows; y++) {
        u8* dst_row       = (u8*)mapped + (size_t)y * aligned_row;
        const u8* src_row = src + (size_t)y * row_bytes;
        if (is_bc || bpp == 8) {
            memcpy(dst_row, src_row, row_bytes);
        } else if (bpp == 16) {
            for (u32 x = 0; x < width; x++) {
                u16 texel; memcpy(&texel, src_row + (size_t)x * 2, 2);
                texel = (u16)((texel << 8) | (texel >> 8));
                memcpy(dst_row + (size_t)x * 2, &texel, 2);
            }
        } else { /* bpp == 32 (or unknown -- treat as 32) */
            for (u32 x = 0; x < width; x++) {
                u32 texel; memcpy(&texel, src_row + (size_t)x * 4, 4);
                texel = __builtin_bswap32(texel);
                memcpy(dst_row + (size_t)x * 4, &texel, 4);
            }
        }
    }
    upload_buf->lpVtbl->Unmap(upload_buf, 0, NULL);

    /* bind_texture fires while the RSX FIFO is being walked (rsx_process_method),
     * before render_frame ever opens the frame's command list — so this is a
     * self-contained one-shot submission, the same pattern create_texture_resources
     * uses for the dummy texture. Correctness first; a hot per-frame texture
     * rebind could later be optimized to piggyback on the frame's own command
     * list instead of a dedicated Reset/Execute/Wait round trip. */
    s_d3d.cmd_allocators[0]->lpVtbl->Reset(s_d3d.cmd_allocators[0]);
    s_d3d.cmd_list->lpVtbl->Reset(s_d3d.cmd_list, s_d3d.cmd_allocators[0], NULL);

    D3D12_TEXTURE_COPY_LOCATION dst_loc = {0};
    dst_loc.pResource        = new_tex;
    dst_loc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src_loc = {0};
    src_loc.pResource                        = upload_buf;
    src_loc.Type                             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_loc.PlacedFootprint.Footprint.Format  = is_bc ? DXGI_FORMAT_R8_UINT : dxgi_fmt; /* block data has no per-texel format meaning */
    src_loc.PlacedFootprint.Footprint.Width   = is_bc ? row_bytes : width;
    src_loc.PlacedFootprint.Footprint.Height  = src_rows;
    src_loc.PlacedFootprint.Footprint.Depth   = 1;
    src_loc.PlacedFootprint.Footprint.RowPitch = aligned_row;
    if (is_bc) {
        /* Copy the compressed payload as a raw byte buffer sized to match the
         * destination's actual block layout via the full-texture overload. */
        src_loc.PlacedFootprint.Footprint.Format = dxgi_fmt;
        src_loc.PlacedFootprint.Footprint.Width  = width;
        src_loc.PlacedFootprint.Footprint.Height = height;
    }

    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst_loc, 0, 0, 0, &src_loc, NULL);

    D3D12_RESOURCE_BARRIER barrier = {0};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = new_tex;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &barrier);

    s_d3d.cmd_list->lpVtbl->Close(s_d3d.cmd_list);
    ID3D12CommandList* lists[] = { (ID3D12CommandList*)s_d3d.cmd_list };
    s_d3d.cmd_queue->lpVtbl->ExecuteCommandLists(s_d3d.cmd_queue, 1, lists);
    wait_for_gpu();
    upload_buf->lpVtbl->Release(upload_buf);
    free(linear_tmp);

    if (s_d3d.textures[unit]) s_d3d.textures[unit]->lpVtbl->Release(s_d3d.textures[unit]);
    s_d3d.textures[unit] = new_tex;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {0};
    srv_desc.Format                 = dxgi_fmt;
    srv_desc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels     = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE srv_handle;
    s_d3d.srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &srv_handle);
    srv_handle.ptr += (size_t)unit * s_d3d.srv_descriptor_size;
    s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, new_tex, &srv_desc, srv_handle);

    static int detail_log = 0;
    if (detail_log < 10) {
        printf("[D3D12] texture uploaded: unit=%u %ux%u swizzled=%d fmt=0x%02X (dxgi=%u) at ea 0x%08X\n",
               unit, width, height, do_deswizzle, format, (unsigned)dxgi_fmt, ea);
        detail_log++;
    }
}

static void d3d12_set_vertex_attribs(void* ud, const rsx_state* state)
{
    (void)ud;
    s_d3d.current_rsx_state = state;

    /* Log enabled vertex attributes for debugging */
    static int log_count = 0;
    if (log_count < 5) {
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

/* ---------------------------------------------------------------------------
 * Fragment-program shader cache: decompile NV40 bytecode (rsx_fp_decompiler,
 * see docs/RSX_FRAGMENT_PROGRAM.md) -> D3DCompile -> PSO triple, keyed by a
 * hash of the raw bytecode so re-binding the same program (routine every
 * frame) doesn't recompile it.
 * -----------------------------------------------------------------------*/

static u32 fnv1a(const u8* data, u32 len)
{
    u32 h = 2166136261u;
    for (u32 i = 0; i < len; i++) { h ^= data[i]; h *= 16777619u; }
    return h;
}

/* Passthrough vertex shader — the fallback VS used whenever the real RSX
 * vertex program is absent (transform_program_words < 4) or fails to
 * decompile/compile. When a VP *is* present it is translated by
 * rsx_vp_decompile() (see d3d12_set_shader below) into a dedicated vs_5_0
 * blob; this passthrough only plumbs attributes v0/v3 so it stays the safe
 * default. Output struct matches rsx_fp_decompile()'s PSInput exactly
 * (position/col0/col1/fog/tc0..7) field-for-field so the two stages link.
 * Texcoords are zeroed for now — real UV upload is a follow-up once vertex
 * attribute reading grows past POSITION+COLOR (see upload_vertices_from_rsx). */
static const char s_vs_passthrough_hlsl[] =
    "cbuffer cb0 : register(b0) {\n"
    "    float4 mvp_col0; float4 mvp_col1; float4 mvp_col2; float4 mvp_col3;\n"
    "};\n"
    "struct VSInput { float3 pos : POSITION; float4 col : COLOR; };\n"
    "struct VSOutput {\n"
    "    float4 position : SV_POSITION; float4 col0 : COLOR0; float4 col1 : COLOR1;\n"
    "    float4 fog : FOG;\n"
    "    float4 tc0:TEXCOORD0; float4 tc1:TEXCOORD1; float4 tc2:TEXCOORD2; float4 tc3:TEXCOORD3;\n"
    "    float4 tc4:TEXCOORD4; float4 tc5:TEXCOORD5; float4 tc6:TEXCOORD6; float4 tc7:TEXCOORD7;\n"
    "};\n"
    "VSOutput main(VSInput i) {\n"
    "    VSOutput o;\n"
    "    float4 p = float4(i.pos, 1.0);\n"
    "    o.position = mvp_col0 * p.x + mvp_col1 * p.y + mvp_col2 * p.z + mvp_col3 * p.w;\n"
    "    o.col0 = i.col; o.col1 = i.col; o.fog = float4(0,0,0,0);\n"
    "    o.tc0=o.tc1=o.tc2=o.tc3=o.tc4=o.tc5=o.tc6=o.tc7=float4(0,0,0,0);\n"
    "    return o;\n"
    "}\n";

/* Create the {triangle, line, point} PSO trio for one compiled VS+PS pair,
 * reusing the exact rasterizer/blend/depth setup the hardcoded fallback PSO
 * already uses (see init_d3d12). Returns 0 on success (at least the triangle
 * PSO must succeed), -1 otherwise. */
static int create_pso_triple(ID3DBlob* vs_blob, ID3DBlob* ps_blob, ShaderPsoSet* out)
{
    HRESULT hr;
    D3D12_INPUT_ELEMENT_DESC input_layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
    pso_desc.pRootSignature = s_d3d.root_signature;
    pso_desc.VS.pShaderBytecode = vs_blob->lpVtbl->GetBufferPointer(vs_blob);
    pso_desc.VS.BytecodeLength  = vs_blob->lpVtbl->GetBufferSize(vs_blob);
    pso_desc.PS.pShaderBytecode = ps_blob->lpVtbl->GetBufferPointer(ps_blob);
    pso_desc.PS.BytecodeLength  = ps_blob->lpVtbl->GetBufferSize(ps_blob);
    pso_desc.InputLayout.pInputElementDescs = input_layout;
    pso_desc.InputLayout.NumElements = 2;
    pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pso_desc.DepthStencilState.DepthEnable    = TRUE;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pso_desc.DepthStencilState.StencilEnable  = FALSE;
    pso_desc.SampleDesc.Count = 1;

    memset(out, 0, sizeof(*out));

    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
        s_d3d.device, &pso_desc, &IID_ID3D12PipelineState, (void**)&out->tri);
    if (FAILED(hr)) { printf("[D3D12] shader PSO TRIANGLE creation failed (0x%08lX)\n", hr); return -1; }

    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
        s_d3d.device, &pso_desc, &IID_ID3D12PipelineState, (void**)&out->line);
    if (FAILED(hr)) printf("[D3D12] shader PSO LINE creation failed (0x%08lX)\n", hr);

    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(
        s_d3d.device, &pso_desc, &IID_ID3D12PipelineState, (void**)&out->point);
    if (FAILED(hr)) printf("[D3D12] shader PSO POINT creation failed (0x%08lX)\n", hr);

    return 0;
}

/* Resolve NV4097_SET_SHADER_PROGRAM's address to a flat guest EA.
 * Bit0 of shader_program: 0 = RSX local memory (VRAM, base
 * s_config.localAddress == 0xC0000000 — see cellGcmSys.c), 1 = main memory
 * (already a flat EA). Confirmed against a real GoW2 boot via the
 * PS3_DUMP_FP_ONCE diagnostic (2026-07-02). */
static u32 resolve_fragment_program_ea(const rsx_state* state)
{
    u32 loc = state->shader_program & 0x3u;
    return (loc == 0) ? (0xC0000000u + state->fragment_program_addr)
                       : state->fragment_program_addr;
}

static void d3d12_set_shader(void* ud, const rsx_state* state)
{
    (void)ud;
    s_d3d.current_rsx_state = state;

    static int log_count = 0;
    if (log_count < 5) {
        printf("[D3D12] set_shader: fp_addr=0x%08X, vp_load=%u, output_mask=0x%08X, loc=%u\n",
               state->fragment_program_addr, state->transform_program_load,
               state->vertex_attrib_output_mask, state->shader_program & 0x3u);
        log_count++;
    }

    /* "Sem programa" e' o registo CRU a zero; o offset 0 e' valido (ver o
     * mesmo teste em rsx_metal_backend.m: quadrante verde no menu do GoW2). */
    if (!state->shader_program) { s_d3d.current_shader = NULL; return; }

    extern uint8_t* vm_base;
    extern int ppu_guest_range_committed(uint32_t addr, uint32_t n);
    u32 ea = resolve_fragment_program_ea(state);

    /* Bound how far we're willing to read: rsx_fp_program_size stops at the
     * PROGRAM_END bit, but a corrupt/garbage address could scan forever into
     * unmapped memory. 16 KB comfortably covers any real NV40 fragment
     * program (RPCS3's own cap is similar). */
    const u32 MAX_FP_BYTES = 16 * 1024;
    if (!ppu_guest_range_committed(ea, 512)) {
        static int warn = 0;
        if (warn++ < 5) printf("[D3D12] set_shader: fp ea=0x%08X not committed, keeping fallback PSO\n", ea);
        s_d3d.current_shader = NULL;
        return;
    }
    const u8* ucode = vm_base + ea;
    u32 prog_bytes = rsx_fp_program_size(ucode, MAX_FP_BYTES);
    if (!prog_bytes) { s_d3d.current_shader = NULL; return; }

    u32 hash = fnv1a(ucode, prog_bytes);

    /* Vertex program: hash the raw NV40 VP words so the PSO cache is keyed by
     * (fp, vp), not fp alone. A real program is at least one 4-word (16-byte)
     * instruction; anything shorter means "no VP" -> vp_hash 0 -> shared
     * passthrough VS. VP words are host-order u32 == little-endian on x86,
     * exactly what rsx_vp_decompile expects. */
    u32 vpw = 0;
    const u32* vpwords = rsx_vp_entry(state, &vpw);
    u32 vp_hash = (vpw >= 4) ? fnv1a((const u8*)vpwords, vpw * 4) : 0u;

    /* Shader dump (PS3_DUMP_SHADERS=dir): write each unique FP program and the
     * current VP instruction memory to disk as a real-shader corpus for the
     * NV40->HLSL translator validation harness. */
    { static int dmp = -1; static const char* dir = 0;
      if (dmp < 0) { extern char* getenv(const char*); dir = getenv("PS3_DUMP_SHADERS"); dmp = dir ? 1 : 0; }
      if (dmp) {
        static u32 seen[256]; static u32 nseen = 0; int dup = 0;
        for (u32 i = 0; i < nseen; i++) if (seen[i] == hash) { dup = 1; break; }
        if (!dup && nseen < 256) {
            seen[nseen++] = hash;
            char fn[512]; FILE* f;
            snprintf(fn, sizeof fn, "%s/fp_%08X.bin", dir, hash);
            f = fopen(fn, "wb"); if (f) { fwrite(ucode, 1, prog_bytes, f); fclose(f); }
            if (state->transform_program_words) {
                u32 vpw = state->transform_program_words;
                snprintf(fn, sizeof fn, "%s/vp_%08X.bin", dir, hash);
                f = fopen(fn, "wb");
                if (f) { fwrite(state->transform_program, 4, vpw, f); fclose(f); }
            }
            printf("[D3D12] dumped shader fp=0x%08X (%u B) vp=%u words\n",
                   hash, prog_bytes, state->transform_program_words);
        }
      }
    }

    /* Fold 32_BITS_EXPORTS into the cache key so r0 vs h0 do not share a PSO. */
    if (state->shader_control_valid)
        hash ^= (state->shader_control & 0x40u);
    for (u32 i = 0; i < s_d3d.shader_cache_count; i++) {
        if (s_d3d.shader_cache[i].fp_hash == hash &&
            s_d3d.shader_cache[i].vp_hash == vp_hash) {
            s_fp_cache_hits++;
            s_d3d.current_shader = &s_d3d.shader_cache[i];
            return;
        }
    }
    s_fp_cache_misses++;

    /* Cache miss: decompile + compile. Compile the shared passthrough VS
     * once, lazily, the first time any fragment shader needs it. */
    if (!s_d3d.vs_passthrough_blob) {
        ID3DBlob* err = NULL;
        HRESULT hr = D3DCompile(s_vs_passthrough_hlsl, sizeof(s_vs_passthrough_hlsl) - 1,
                                "vs_passthrough", NULL, NULL, "main", "vs_5_0", 0, 0,
                                &s_d3d.vs_passthrough_blob, &err);
        if (FAILED(hr)) {
            printf("[D3D12] passthrough VS compile failed: %s\n",
                   err ? (const char*)err->lpVtbl->GetBufferPointer(err) : "unknown");
            if (err) err->lpVtbl->Release(err);
            s_d3d.current_shader = NULL;
            return;
        }
    }

    static char hlsl[64 * 1024];
    if (state->shader_control_valid)
        rsx_fp_set_color_export((state->shader_control & 0x40u) ? 1 : 0);
    else
        rsx_fp_set_color_export(-1);
    int n_instr = rsx_fp_decompile(ucode, prog_bytes, hlsl, sizeof(hlsl));
    rsx_fp_set_color_export(-1);
    if (n_instr < 0) {
        static int fail_log = 0;
        s_fp_decompile_fail++;
        if (fail_log++ < 5) {
            printf("[D3D12] set_shader: rsx_fp_decompile failed (ea=0x%08X, %u bytes)\n", ea, prog_bytes);
            d3d12_overlay_shader_log("FP decompile failed ea=0x%08X", ea);
        }
        s_d3d.current_shader = NULL;
        return;
    }

    ID3DBlob* ps_blob = NULL;
    ID3DBlob* err = NULL;
    HRESULT hr = D3DCompile(hlsl, strlen(hlsl), "fp_decompiled", NULL, NULL,
                            "main", "ps_5_0", 0, 0, &ps_blob, &err);
    if (FAILED(hr)) {
        static int fail_log = 0;
        s_fp_decompile_fail++;
        if (fail_log++ < 5) {
            printf("[D3D12] set_shader: D3DCompile of decompiled FP failed (ea=0x%08X, %d instr): %s\n",
                   ea, n_instr, err ? (const char*)err->lpVtbl->GetBufferPointer(err) : "unknown");
            d3d12_overlay_shader_log("FP compile failed ea=0x%08X instr=%d", ea, n_instr);
        }
        if (err) err->lpVtbl->Release(err);
        s_d3d.current_shader = NULL;
        return;
    }
    if (err) err->lpVtbl->Release(err);
    s_fp_decompile_ok++;

    /* Vertex shader selection. Default to the shared passthrough VS; if a real
     * NV40 VP is present (vp_hash != 0), decompile it and compile a dedicated
     * vs_5_0 blob. Any failure (decompile error or D3DCompile error) silently
     * falls back to the passthrough — the frame must never hard-fail on VP.
     * vs_owned != NULL means this cache slot owns the blob and must Release it. */
    ID3DBlob* vs_blob  = s_d3d.vs_passthrough_blob;
    ID3DBlob* vs_owned = NULL;
    if (vp_hash) {
        static char vp_hlsl[64 * 1024];
        int vp_instr = rsx_vp_decompile((const u8*)vpwords,
                                        vpw * 4, vp_hlsl, sizeof(vp_hlsl));
        if (vp_instr > 0) {
            ID3DBlob* vsb = NULL; ID3DBlob* vperr = NULL;
            HRESULT vhr = D3DCompile(vp_hlsl, strlen(vp_hlsl), "vp_decompiled",
                                     NULL, NULL, "main", "vs_5_0", 0, 0, &vsb, &vperr);
            if (SUCCEEDED(vhr)) {
                vs_blob = vsb; vs_owned = vsb;
                s_vp_decompile_ok++;
            } else {
                static int vlog = 0;
                s_vp_decompile_fail++;
                if (vlog++ < 5) {
                    printf("[D3D12] set_shader: D3DCompile of decompiled VP failed "
                           "(vp_hash=0x%08X): %s\n", vp_hash,
                           vperr ? (const char*)vperr->lpVtbl->GetBufferPointer(vperr) : "unknown");
                    d3d12_overlay_shader_log("VP compile failed vp=0x%08X", vp_hash);
                }
                if (vsb) vsb->lpVtbl->Release(vsb);
                vp_hash = 0; /* keyed as passthrough now */
            }
            if (vperr) vperr->lpVtbl->Release(vperr);
        } else {
            static int vlog2 = 0;
            s_vp_decompile_fail++;
            if (vlog2++ < 5) {
                printf("[D3D12] set_shader: rsx_vp_decompile failed (vp_hash=0x%08X, %u words)\n",
                       vp_hash, vpw);
                d3d12_overlay_shader_log("VP decompile failed vp=0x%08X", vp_hash);
            }
            vp_hash = 0; /* keyed as passthrough now */
        }
    }

    if (s_d3d.shader_cache_count >= MAX_SHADER_CACHE) {
        /* Cache full: evict slot 0 (simple, not LRU — real games settle into
         * a small working set of distinct shaders well under 32). */
        ShaderPsoSet* victim = &s_d3d.shader_cache[0];
        if (victim->tri)      victim->tri->lpVtbl->Release(victim->tri);
        if (victim->line)     victim->line->lpVtbl->Release(victim->line);
        if (victim->point)    victim->point->lpVtbl->Release(victim->point);
        if (victim->vs_owned) victim->vs_owned->lpVtbl->Release(victim->vs_owned);
        memmove(&s_d3d.shader_cache[0], &s_d3d.shader_cache[1],
                (MAX_SHADER_CACHE - 1) * sizeof(ShaderPsoSet));
        s_d3d.shader_cache_count--;
    }

    ShaderPsoSet* slot = &s_d3d.shader_cache[s_d3d.shader_cache_count];
    int pso_ok = (create_pso_triple(vs_blob, ps_blob, slot) == 0 && slot->tri);
    if (!pso_ok && vs_owned) {
        /* The decompiled VS didn't yield a valid PSO (e.g. its output signature
         * didn't link with the FP's PSInput). Retry once with the passthrough
         * VS before giving up — never hard-fail the frame on a VP problem. */
        static int rlog = 0;
        if (rlog++ < 5)
            printf("[D3D12] set_shader: VP PSO failed, retrying with passthrough VS (vp_hash=0x%08X)\n", vp_hash);
        vs_owned->lpVtbl->Release(vs_owned);
        vs_owned = NULL;
        vp_hash = 0;
        pso_ok = (create_pso_triple(s_d3d.vs_passthrough_blob, ps_blob, slot) == 0 && slot->tri);
    }
    if (!pso_ok) {
        if (vs_owned) vs_owned->lpVtbl->Release(vs_owned);
        ps_blob->lpVtbl->Release(ps_blob);
        s_d3d.current_shader = NULL;
        return;
    }
    slot->fp_hash  = hash;
    slot->vp_hash  = vp_hash;
    slot->vs_owned = vs_owned;
    s_d3d.shader_cache_count++;
    s_d3d.current_shader = slot;
    ps_blob->lpVtbl->Release(ps_blob);

    static int compiled_log = 0;
    if (compiled_log++ < 10) {
        printf("[D3D12] set_shader: compiled new PSO (ea=0x%08X, fp=%d instr, hash=0x%08X, "
               "vp_hash=0x%08X %s, vp_ok=%u vp_fail=%u, cache=%u/%u)\n",
               ea, n_instr, hash, vp_hash, vs_owned ? "VP" : "passthrough",
               s_vp_decompile_ok, s_vp_decompile_fail,
               s_d3d.shader_cache_count, (u32)MAX_SHADER_CACHE);
    }
}

static void d3d12_set_blend(void* ud, const rsx_state* state)
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

static void d3d12_set_depth_stencil(void* ud, const rsx_state* state)
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
    rsx_overlay_graphics_settings graphics;
    int fullscreen = 0;

    /* Persisted window size/fullscreen are consumed here, at creation: the
     * swap chain, RTVs and depth buffer are sized once, so changing them is
     * restart-required. Without an initialized overlay the caller's size is
     * used unchanged. */
    if (rsx_overlay_get_graphics_settings(&graphics)) {
        width = graphics.window_width;
        height = graphics.window_height;
        fullscreen = graphics.fullscreen != 0;
    }
    if (fullscreen) {
        int monitor_w = GetSystemMetrics(SM_CXSCREEN);
        int monitor_h = GetSystemMetrics(SM_CYSCREEN);
        if (monitor_w > 0 && monitor_h > 0) {
            width = (u32)monitor_w;
            height = (u32)monitor_h;
        } else {
            fullscreen = 0;
        }
    }

    memset(&s_d3d, 0, sizeof(s_d3d));
    rsx_overlay_frame_timer_init(&s_d3d.frame_timer);
    s_d3d.width = width;
    s_d3d.height = height;
    s_d3d.clear_color[0] = 0.0f;
    s_d3d.clear_color[1] = 0.0f;
    s_d3d.clear_color[2] = 0.1f;
    s_d3d.clear_color[3] = 1.0f;

    /* Create window */
    s_d3d.hwnd = create_window(width, height, title, fullscreen);
    if (!s_d3d.hwnd) {
        printf("[D3D12] ERROR: Window creation failed\n");
        return -1;
    }

    /* Initialize D3D12 */
    if (init_d3d12(width, height) != 0) {
        printf("[D3D12] ERROR: D3D12 initialization failed\n");
        return -1;
    }

    if (create_texture_resources() != 0) {
        printf("[D3D12] ERROR: texture descriptor table setup failed\n");
        return -1;
    }

    /* The overlay is optional: without its renderer the game renders exactly
     * as before and the UI simply never appears. Formats match render_frame's
     * bound targets (swap-chain RTV + D24S8 DSV). */
    s_d3d.overlay = rsx_overlay_d3d12_renderer_create(
        s_d3d.device, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT);
    if (!s_d3d.overlay) {
        fprintf(stderr, "[D3D12] overlay renderer unavailable\n");
        /* Nobody could see the menu: stop the core from toggling it and from
         * withholding pad/keyboard input. */
        rsx_overlay_disable("D3D12 overlay renderer unavailable");
    }
    {
        rsx_overlay_backend_capabilities caps;
        caps.window_size = RSX_OVERLAY_SETTING_RESTART_REQUIRED;
        caps.fullscreen = RSX_OVERLAY_SETTING_RESTART_REQUIRED;
        caps.vsync = RSX_OVERLAY_SETTING_LIVE;
        rsx_overlay_set_backend_capabilities(&caps);
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

    rsx_set_backend(&s_d3d12_backend);

    s_d3d.initialized = 1;
    s_d3d.last_fps_time = GetTickCount64();

    printf("[D3D12] Backend ready: %ux%u\n", width, height);
    return 0;
}

void rsx_d3d12_backend_shutdown(void)
{
    if (!s_d3d.initialized) return;

    if (getenv("PS3_TRACE_RSX_SHADERS")) {
        fprintf(stderr, "[RSX-SH] hits=%u misses=%u ok=%u fail=%u cache=%u\n",
                s_fp_cache_hits, s_fp_cache_misses, s_fp_decompile_ok,
                s_fp_decompile_fail, s_d3d.shader_cache_count);
    }

    wait_for_gpu();

    rsx_overlay_set_backend_capabilities(NULL);
    rsx_overlay_d3d12_renderer_destroy(s_d3d.overlay);
    s_d3d.overlay = NULL;

    /* Release D3D12 resources */
    if (s_d3d.vertex_buffer) {
        s_d3d.vertex_buffer->lpVtbl->Unmap(s_d3d.vertex_buffer, 0, NULL);
        s_d3d.vertex_buffer->lpVtbl->Release(s_d3d.vertex_buffer);
    }
    if (s_d3d.pipeline_state)        s_d3d.pipeline_state->lpVtbl->Release(s_d3d.pipeline_state);
    if (s_d3d.pipeline_state_lines)  s_d3d.pipeline_state_lines->lpVtbl->Release(s_d3d.pipeline_state_lines);
    if (s_d3d.pipeline_state_points) s_d3d.pipeline_state_points->lpVtbl->Release(s_d3d.pipeline_state_points);
    if (s_d3d.vs_passthrough_blob)   s_d3d.vs_passthrough_blob->lpVtbl->Release(s_d3d.vs_passthrough_blob);
    for (u32 i = 0; i < s_d3d.shader_cache_count; i++) {
        ShaderPsoSet* sc = &s_d3d.shader_cache[i];
        if (sc->tri)      sc->tri->lpVtbl->Release(sc->tri);
        if (sc->line)     sc->line->lpVtbl->Release(sc->line);
        if (sc->point)    sc->point->lpVtbl->Release(sc->point);
        if (sc->vs_owned) sc->vs_owned->lpVtbl->Release(sc->vs_owned);
    }
    if (s_d3d.index_buffer) {
        s_d3d.index_buffer->lpVtbl->Unmap(s_d3d.index_buffer, 0, NULL);
        s_d3d.index_buffer->lpVtbl->Release(s_d3d.index_buffer);
    }
    if (s_d3d.dummy_texture) s_d3d.dummy_texture->lpVtbl->Release(s_d3d.dummy_texture);
    for (u32 i = 0; i < RSX_MAX_TEXTURES; i++)
        if (s_d3d.textures[i]) s_d3d.textures[i]->lpVtbl->Release(s_d3d.textures[i]);
    if (s_d3d.srv_heap)     s_d3d.srv_heap->lpVtbl->Release(s_d3d.srv_heap);
    if (s_d3d.sampler_heap) s_d3d.sampler_heap->lpVtbl->Release(s_d3d.sampler_heap);
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
    /* The window thread owns the overlay's shell state: it builds the next
     * immutable draw packet here, after this batch of input. Nothing is
     * built while the shell is closed (render inactive); then only a pending
     * settings change (e.g. the menu just closed) is written, and an idle
     * call does no I/O. The size (fixed, scale 1.0) is published only when
     * the core asks for it. */
    if (rsx_overlay_display_stale())
        rsx_overlay_set_display(s_d3d.width, s_d3d.height, 1.0f);
    if (rsx_overlay_render_active())
        rsx_overlay_build_draw_data();
    else
        (void)rsx_overlay_persist_settings();
    if (rsx_overlay_take_quit_request()) {
        (void)rsx_overlay_persist_settings();
        s_d3d.window_closed = 1;
    }
    return s_d3d.window_closed ? -1 : 0;
}

void rsx_d3d12_backend_present(void)
{
    if (s_d3d.initialized)
        render_frame();
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

/* Content-pipeline health counters: not tracked on this platform (no D3D12
 * shader cache exists here), so the getters just report zero. */
u32 rsx_d3d12_fp_cache_hits(void)    { return 0; }
u32 rsx_d3d12_fp_cache_misses(void)  { return 0; }
u32 rsx_d3d12_fp_decompile_ok(void)  { return 0; }
u32 rsx_d3d12_fp_decompile_fail(void){ return 0; }

#endif /* _WIN32 */
