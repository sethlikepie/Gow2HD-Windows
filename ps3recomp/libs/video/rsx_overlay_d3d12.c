/*
 * ps3recomp - D3D12 renderer for the runtime overlay.
 *
 * Plain C with the COM C interface, like rsx_d3d12_backend.c. It never reads
 * live Dear ImGui state: geometry comes from rsx_overlay_copy_draw_packet()
 * and the font from rsx_overlay_copy_font_atlas(), both immutable copies made
 * by the overlay core on its event thread. The IID_* GUIDs it references are
 * defined once, by rsx_d3d12_backend.c (initguid.h before <d3d12.h>).
 */

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rsx_overlay.h"
#include "rsx_overlay_d3d12.h"
#include "rsx_overlay_d3d12_packet.h"

struct rsx_overlay_d3d12_renderer {
    ID3D12Device* device;
    ID3D12RootSignature* root_signature;
    ID3D12PipelineState* pipeline_state;
    ID3D12DescriptorHeap* srv_heap;
    ID3D12Resource* font_texture;
    ID3D12Resource* font_upload;
    ID3D12Resource* vertex_buffers[RSX_OVERLAY_D3D12_FRAME_SLOTS];
    ID3D12Resource* index_buffers[RSX_OVERLAY_D3D12_FRAME_SLOTS];
    void* vertex_mapped[RSX_OVERLAY_D3D12_FRAME_SLOTS];
    void* index_mapped[RSX_OVERLAY_D3D12_FRAME_SLOTS];
    rsx_overlay_draw_packet* packet; /* ~870 KB, heap-owned scratch copy */
    uint32_t font_width;
    uint32_t font_height;
    uint32_t font_row_pitch;
    int font_upload_pending;
    int font_ready;
    uint32_t font_failures;
};

static void overlay_release(IUnknown** object)
{
    if (*object != NULL) {
        (*object)->lpVtbl->Release(*object);
        *object = NULL;
    }
}
#define OVERLAY_RELEASE(p) overlay_release((IUnknown**)&(p))

static int overlay_create_upload_buffer(ID3D12Device* device, UINT64 size,
    ID3D12Resource** resource, void** mapped)
{
    D3D12_HEAP_PROPERTIES heap;
    D3D12_RESOURCE_DESC desc;
    D3D12_RANGE no_read = { 0, 0 };

    memset(&heap, 0, sizeof(heap));
    memset(&desc, 0, sizeof(desc));
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->lpVtbl->CreateCommittedResource(device, &heap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL, &IID_ID3D12Resource, (void**)resource)))
        return 0;
    if (FAILED((*resource)->lpVtbl->Map(*resource, 0, &no_read, mapped))) {
        OVERLAY_RELEASE(*resource);
        return 0;
    }
    return 1;
}

static void overlay_log_blob(const char* what, ID3DBlob* blob)
{
    if (blob != NULL)
        fprintf(stderr, "[OVERLAY-D3D12] %s: %s\n", what,
                (const char*)blob->lpVtbl->GetBufferPointer(blob));
    else
        fprintf(stderr, "[OVERLAY-D3D12] %s failed\n", what);
}

static int overlay_create_pipeline(rsx_overlay_d3d12_renderer* renderer,
    DXGI_FORMAT rtv_format, DXGI_FORMAT dsv_format)
{
    /* Vertex color is ImGui's packed RGBA8 (R in the low byte), read by the
     * input assembler as R8G8B8A8_UNORM, so it arrives as float4. */
    static const char vertex_shader[] =
        "cbuffer canvas : register(b0) { float2 scale; float2 translate; };\n"
        "struct vs_in { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };\n"
        "struct vs_out { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };\n"
        "vs_out main(vs_in v) { vs_out o;"
        " o.pos = float4(v.pos * scale + translate, 0.0, 1.0);"
        " o.uv = v.uv; o.col = v.col; return o; }\n";
    static const char pixel_shader[] =
        "Texture2D font_texture : register(t0);\n"
        "SamplerState font_sampler : register(s0);\n"
        "struct ps_in { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };\n"
        "float4 main(ps_in p) : SV_TARGET { return p.col * font_texture.Sample(font_sampler, p.uv); }\n";
    D3D12_DESCRIPTOR_RANGE range;
    D3D12_ROOT_PARAMETER parameters[2];
    D3D12_STATIC_SAMPLER_DESC sampler;
    D3D12_ROOT_SIGNATURE_DESC root_desc;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc;
    D3D12_INPUT_ELEMENT_DESC input_layout[3];
    ID3DBlob* root_blob = NULL;
    ID3DBlob* error_blob = NULL;
    ID3DBlob* vs = NULL;
    ID3DBlob* ps = NULL;
    int ok = 0;

    memset(&range, 0, sizeof(range));
    memset(parameters, 0, sizeof(parameters));
    memset(&sampler, 0, sizeof(sampler));
    memset(&root_desc, 0, sizeof(root_desc));
    memset(&pipeline_desc, 0, sizeof(pipeline_desc));
    memset(input_layout, 0, sizeof(input_layout));

    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.ShaderRegister = 0;
    parameters[0].Constants.Num32BitValues = 4;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &range;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MaxLOD = 0.0f;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_desc.NumParameters = 2;
    root_desc.pParameters = parameters;
    root_desc.NumStaticSamplers = 1;
    root_desc.pStaticSamplers = &sampler;
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
    if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
            &root_blob, &error_blob))) {
        overlay_log_blob("root signature", error_blob);
        goto done;
    }
    if (FAILED(renderer->device->lpVtbl->CreateRootSignature(renderer->device, 0,
            root_blob->lpVtbl->GetBufferPointer(root_blob),
            root_blob->lpVtbl->GetBufferSize(root_blob), &IID_ID3D12RootSignature,
            (void**)&renderer->root_signature)))
        goto done;
    OVERLAY_RELEASE(error_blob);
    if (FAILED(D3DCompile(vertex_shader, sizeof(vertex_shader) - 1, "overlay_vs", NULL,
            NULL, "main", "vs_5_0", 0, 0, &vs, &error_blob))) {
        overlay_log_blob("vertex shader", error_blob);
        goto done;
    }
    OVERLAY_RELEASE(error_blob);
    if (FAILED(D3DCompile(pixel_shader, sizeof(pixel_shader) - 1, "overlay_ps", NULL,
            NULL, "main", "ps_5_0", 0, 0, &ps, &error_blob))) {
        overlay_log_blob("pixel shader", error_blob);
        goto done;
    }

    input_layout[0].SemanticName = "POSITION";
    input_layout[0].Format = DXGI_FORMAT_R32G32_FLOAT;
    input_layout[0].AlignedByteOffset = (UINT)offsetof(rsx_overlay_draw_vertex, position);
    input_layout[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    input_layout[1].SemanticName = "TEXCOORD";
    input_layout[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    input_layout[1].AlignedByteOffset = (UINT)offsetof(rsx_overlay_draw_vertex, uv);
    input_layout[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    input_layout[2].SemanticName = "COLOR";
    input_layout[2].Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    input_layout[2].AlignedByteOffset = (UINT)offsetof(rsx_overlay_draw_vertex, color);
    input_layout[2].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

    pipeline_desc.pRootSignature = renderer->root_signature;
    pipeline_desc.VS.pShaderBytecode = vs->lpVtbl->GetBufferPointer(vs);
    pipeline_desc.VS.BytecodeLength = vs->lpVtbl->GetBufferSize(vs);
    pipeline_desc.PS.pShaderBytecode = ps->lpVtbl->GetBufferPointer(ps);
    pipeline_desc.PS.BytecodeLength = ps->lpVtbl->GetBufferSize(ps);
    pipeline_desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    pipeline_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    pipeline_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    pipeline_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pipeline_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pipeline_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    pipeline_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pipeline_desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    pipeline_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline_desc.RasterizerState.DepthClipEnable = TRUE;
    /* No depth test and no depth write: the UI never touches the guest's
     * depth/stencil contents even though the frame's DSV stays bound. */
    pipeline_desc.DepthStencilState.DepthEnable = FALSE;
    pipeline_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pipeline_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pipeline_desc.DepthStencilState.StencilEnable = FALSE;
    pipeline_desc.SampleMask = UINT_MAX;
    pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline_desc.InputLayout.pInputElementDescs = input_layout;
    pipeline_desc.InputLayout.NumElements = 3;
    pipeline_desc.NumRenderTargets = 1;
    pipeline_desc.RTVFormats[0] = rtv_format;
    pipeline_desc.DSVFormat = dsv_format;
    pipeline_desc.SampleDesc.Count = 1;
    if (FAILED(renderer->device->lpVtbl->CreateGraphicsPipelineState(renderer->device,
            &pipeline_desc, &IID_ID3D12PipelineState, (void**)&renderer->pipeline_state)))
        goto done;
    ok = 1;
done:
    OVERLAY_RELEASE(root_blob);
    OVERLAY_RELEASE(error_blob);
    OVERLAY_RELEASE(vs);
    OVERLAY_RELEASE(ps);
    return ok;
}

/* Creates the font texture from the core's atlas copy and stages its upload;
 * the copy itself is recorded into the first frame that draws UI. */
static int overlay_create_font_texture(rsx_overlay_d3d12_renderer* renderer)
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t* pixels = NULL;
    uint8_t* mapped = NULL;
    size_t size;
    UINT64 upload_size;
    uint32_t row_pitch;
    uint32_t row;
    D3D12_HEAP_PROPERTIES default_heap;
    D3D12_RESOURCE_DESC texture_desc;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE handle;
    int ok = 0;

    if (!rsx_overlay_copy_font_atlas(NULL, 0, &width, &height) ||
        width == 0 || height == 0 || width > 16384u || height > 16384u)
        return 0;
    size = (size_t)width * height * 4u;
    pixels = (uint8_t*)malloc(size);
    if (pixels == NULL)
        return 0;
    /* The atlas is fixed after init; a size change between the two calls
     * means the overlay was re-initialized, so retry on a later frame. */
    if (!rsx_overlay_copy_font_atlas(pixels, size, &width, &height) ||
        (size_t)width * height * 4u != size)
        goto done;

    row_pitch = (width * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) &
        ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
    upload_size = (UINT64)row_pitch * height;

    memset(&default_heap, 0, sizeof(default_heap));
    memset(&texture_desc, 0, sizeof(texture_desc));
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_desc.Width = width;
    texture_desc.Height = height;
    texture_desc.DepthOrArraySize = 1;
    texture_desc.MipLevels = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (FAILED(renderer->device->lpVtbl->CreateCommittedResource(renderer->device,
            &default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource,
            (void**)&renderer->font_texture)))
        goto done;
    if (!overlay_create_upload_buffer(renderer->device, upload_size,
            &renderer->font_upload, (void**)&mapped))
        goto done;
    for (row = 0; row < height; ++row)
        memcpy(mapped + (size_t)row * row_pitch, pixels + (size_t)row * width * 4u,
               (size_t)width * 4u);
    renderer->font_upload->lpVtbl->Unmap(renderer->font_upload, 0, NULL);

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.NumDescriptors = 1;
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(renderer->device->lpVtbl->CreateDescriptorHeap(renderer->device, &heap_desc,
            &IID_ID3D12DescriptorHeap, (void**)&renderer->srv_heap)))
        goto done;
    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels = 1;
    renderer->srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(renderer->srv_heap, &handle);
    renderer->device->lpVtbl->CreateShaderResourceView(renderer->device,
        renderer->font_texture, &srv_desc, handle);

    renderer->font_width = width;
    renderer->font_height = height;
    renderer->font_row_pitch = row_pitch;
    renderer->font_upload_pending = 1;
    ok = 1;
done:
    if (!ok) {
        OVERLAY_RELEASE(renderer->srv_heap);
        OVERLAY_RELEASE(renderer->font_upload);
        OVERLAY_RELEASE(renderer->font_texture);
    }
    free(pixels);
    return ok;
}

static void overlay_record_font_upload(rsx_overlay_d3d12_renderer* renderer,
    ID3D12GraphicsCommandList* command_list)
{
    D3D12_TEXTURE_COPY_LOCATION destination;
    D3D12_TEXTURE_COPY_LOCATION source;
    D3D12_RESOURCE_BARRIER barrier;

    memset(&destination, 0, sizeof(destination));
    memset(&source, 0, sizeof(source));
    memset(&barrier, 0, sizeof(barrier));
    destination.pResource = renderer->font_texture;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = 0;
    source.pResource = renderer->font_upload;
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint.Offset = 0;
    source.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    source.PlacedFootprint.Footprint.Width = renderer->font_width;
    source.PlacedFootprint.Footprint.Height = renderer->font_height;
    source.PlacedFootprint.Footprint.Depth = 1;
    source.PlacedFootprint.Footprint.RowPitch = renderer->font_row_pitch;
    command_list->lpVtbl->CopyTextureRegion(command_list, &destination, 0, 0, 0, &source, NULL);
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = renderer->font_texture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list->lpVtbl->ResourceBarrier(command_list, 1, &barrier);
    /* The upload buffer stays alive until destroy(): the copy executes later. */
    renderer->font_upload_pending = 0;
    renderer->font_ready = 1;
}

rsx_overlay_d3d12_renderer* rsx_overlay_d3d12_renderer_create(ID3D12Device* device,
    DXGI_FORMAT rtv_format, DXGI_FORMAT dsv_format)
{
    rsx_overlay_d3d12_renderer* renderer;
    uint32_t slot;

    if (device == NULL)
        return NULL;
    renderer = (rsx_overlay_d3d12_renderer*)calloc(1, sizeof(*renderer));
    if (renderer == NULL)
        return NULL;
    renderer->device = device;
    renderer->packet = (rsx_overlay_draw_packet*)calloc(1, sizeof(*renderer->packet));
    if (renderer->packet == NULL ||
        !overlay_create_pipeline(renderer, rtv_format, dsv_format))
        goto fail;
    for (slot = 0; slot < RSX_OVERLAY_D3D12_FRAME_SLOTS; ++slot) {
        if (!overlay_create_upload_buffer(device,
                (UINT64)RSX_OVERLAY_DRAW_VERTEX_CAPACITY * sizeof(rsx_overlay_draw_vertex),
                &renderer->vertex_buffers[slot], &renderer->vertex_mapped[slot]) ||
            !overlay_create_upload_buffer(device,
                (UINT64)RSX_OVERLAY_DRAW_INDEX_CAPACITY * sizeof(uint16_t),
                &renderer->index_buffers[slot], &renderer->index_mapped[slot]))
            goto fail;
    }
    return renderer;
fail:
    rsx_overlay_d3d12_renderer_destroy(renderer);
    return NULL;
}

void rsx_overlay_d3d12_renderer_destroy(rsx_overlay_d3d12_renderer* renderer)
{
    uint32_t slot;

    if (renderer == NULL)
        return;
    for (slot = 0; slot < RSX_OVERLAY_D3D12_FRAME_SLOTS; ++slot) {
        if (renderer->vertex_buffers[slot] != NULL)
            renderer->vertex_buffers[slot]->lpVtbl->Unmap(renderer->vertex_buffers[slot], 0, NULL);
        if (renderer->index_buffers[slot] != NULL)
            renderer->index_buffers[slot]->lpVtbl->Unmap(renderer->index_buffers[slot], 0, NULL);
        OVERLAY_RELEASE(renderer->vertex_buffers[slot]);
        OVERLAY_RELEASE(renderer->index_buffers[slot]);
    }
    OVERLAY_RELEASE(renderer->font_upload);
    OVERLAY_RELEASE(renderer->font_texture);
    OVERLAY_RELEASE(renderer->srv_heap);
    OVERLAY_RELEASE(renderer->pipeline_state);
    OVERLAY_RELEASE(renderer->root_signature);
    free(renderer->packet);
    free(renderer);
}

int rsx_overlay_d3d12_renderer_render(rsx_overlay_d3d12_renderer* renderer,
    ID3D12GraphicsCommandList* command_list, uint32_t frame_slot,
    uint32_t framebuffer_width, uint32_t framebuffer_height)
{
    rsx_overlay_draw_packet* packet;
    D3D12_VERTEX_BUFFER_VIEW vertex_view;
    D3D12_INDEX_BUFFER_VIEW index_view;
    D3D12_VIEWPORT viewport;
    D3D12_GPU_DESCRIPTOR_HANDLE font_handle;
    ID3D12DescriptorHeap* heaps[1];
    float transform[4];
    uint32_t command_index;
    int drawn = 0;

    if (renderer == NULL || command_list == NULL || frame_slot >= RSX_OVERLAY_D3D12_FRAME_SLOTS ||
        framebuffer_width == 0 || framebuffer_height == 0 || !rsx_overlay_render_active())
        return 0;
    packet = renderer->packet;
    if (!rsx_overlay_copy_draw_packet(packet) || !rsx_overlay_d3d12_packet_valid(packet))
        return 0;
    if (renderer->font_texture == NULL) {
        if (renderer->font_failures >= 8u)
            return 0;
        if (!overlay_create_font_texture(renderer)) {
            /* The menu would be open but invisible, withholding input. */
            if (++renderer->font_failures == 8u) {
                fprintf(stderr, "[OVERLAY-D3D12] font texture unavailable\n");
                rsx_overlay_disable("D3D12 overlay font texture unavailable");
            }
            return 0;
        }
    }
    if (renderer->font_upload_pending)
        overlay_record_font_upload(renderer, command_list);
    if (!renderer->font_ready)
        return 0;

    /* This slot's previous frame has passed the backend's frame fence. */
    memcpy(renderer->vertex_mapped[frame_slot], packet->vertices,
           (size_t)packet->vertex_count * sizeof(packet->vertices[0]));
    memcpy(renderer->index_mapped[frame_slot], packet->indices,
           (size_t)packet->index_count * sizeof(packet->indices[0]));

    memset(&vertex_view, 0, sizeof(vertex_view));
    memset(&index_view, 0, sizeof(index_view));
    vertex_view.BufferLocation = renderer->vertex_buffers[frame_slot]->lpVtbl->GetGPUVirtualAddress(
        renderer->vertex_buffers[frame_slot]);
    vertex_view.SizeInBytes = packet->vertex_count * (UINT)sizeof(packet->vertices[0]);
    vertex_view.StrideInBytes = (UINT)sizeof(packet->vertices[0]);
    index_view.BufferLocation = renderer->index_buffers[frame_slot]->lpVtbl->GetGPUVirtualAddress(
        renderer->index_buffers[frame_slot]);
    index_view.SizeInBytes = packet->index_count * (UINT)sizeof(packet->indices[0]);
    index_view.Format = DXGI_FORMAT_R16_UINT;

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = (float)framebuffer_width;
    viewport.Height = (float)framebuffer_height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    rsx_overlay_d3d12_display_transform(packet->display_width, packet->display_height,
                                        transform);

    command_list->lpVtbl->RSSetViewports(command_list, 1, &viewport);
    command_list->lpVtbl->SetGraphicsRootSignature(command_list, renderer->root_signature);
    command_list->lpVtbl->SetPipelineState(command_list, renderer->pipeline_state);
    command_list->lpVtbl->SetGraphicsRoot32BitConstants(command_list, 0, 4, transform, 0);
    heaps[0] = renderer->srv_heap;
    command_list->lpVtbl->SetDescriptorHeaps(command_list, 1, heaps);
    renderer->srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(renderer->srv_heap, &font_handle);
    command_list->lpVtbl->SetGraphicsRootDescriptorTable(command_list, 1, font_handle);
    command_list->lpVtbl->IASetVertexBuffers(command_list, 0, 1, &vertex_view);
    command_list->lpVtbl->IASetIndexBuffer(command_list, &index_view);
    command_list->lpVtbl->IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (command_index = 0; command_index < packet->command_count; ++command_index) {
        const rsx_overlay_draw_command* command = &packet->commands[command_index];
        rsx_overlay_d3d12_scissor scissor;
        D3D12_RECT rect;

        /* Texture id 0 is the shell atlas (see rsx_overlay_command_uses_atlas);
         * the core registers no other texture, so anything else is skipped. */
        if (!rsx_overlay_command_uses_atlas(command) ||
            !rsx_overlay_d3d12_packet_command_valid(packet, command) ||
            !rsx_overlay_d3d12_clip_to_scissor(command->clip_rect,
                packet->display_width, packet->display_height,
                framebuffer_width, framebuffer_height, &scissor))
            continue;
        rect.left = scissor.left;
        rect.top = scissor.top;
        rect.right = scissor.right;
        rect.bottom = scissor.bottom;
        command_list->lpVtbl->RSSetScissorRects(command_list, 1, &rect);
        command_list->lpVtbl->DrawIndexedInstanced(command_list, command->element_count,
            1, command->index_offset, (INT)command->vertex_offset, 0);
        drawn = 1;
    }
    return drawn;
}

#endif /* _WIN32 */
