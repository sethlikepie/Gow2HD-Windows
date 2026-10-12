/* Host-independent pieces of the D3D12 overlay renderer and Win32 event
 * routing: packet validation, display->framebuffer scissor conversion,
 * Win32 VK/modifier translation and the elapsed-time frame timer. The D3D12
 * command recording itself only builds on Windows. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rsx_overlay.h"
#include "rsx_overlay_d3d12_packet.h"
#include "rsx_overlay_frame_timer.h"
#include "rsx_overlay_win32_input.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "check failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__); \
        return 0; \
    } \
} while (0)

static rsx_overlay_draw_packet s_packet;

static int test_command_atlas_contract(void)
{
    rsx_overlay_draw_command command;
    memset(&command, 0, sizeof(command));
    CHECK(rsx_overlay_command_uses_atlas(&command));
    command.texture_id = 1;
    CHECK(!rsx_overlay_command_uses_atlas(&command));
    CHECK(!rsx_overlay_command_uses_atlas(NULL));
    return 1;
}

static int test_packet_counts_are_bounded(void)
{
    memset(&s_packet, 0, sizeof(s_packet));
    CHECK(!rsx_overlay_d3d12_packet_valid(NULL));
    CHECK(!rsx_overlay_d3d12_packet_valid(&s_packet));
    s_packet.vertex_count = 3;
    s_packet.index_count = 3;
    s_packet.command_count = 1;
    CHECK(!rsx_overlay_d3d12_packet_valid(&s_packet)); /* no display size yet */
    s_packet.display_width = 1280u;
    s_packet.display_height = 720u;
    CHECK(rsx_overlay_d3d12_packet_valid(&s_packet));
    s_packet.vertex_count = RSX_OVERLAY_DRAW_VERTEX_CAPACITY + 1u;
    CHECK(!rsx_overlay_d3d12_packet_valid(&s_packet));
    s_packet.vertex_count = 3;
    s_packet.index_count = RSX_OVERLAY_DRAW_INDEX_CAPACITY + 1u;
    CHECK(!rsx_overlay_d3d12_packet_valid(&s_packet));
    s_packet.index_count = 3;
    s_packet.command_count = RSX_OVERLAY_DRAW_COMMAND_CAPACITY + 1u;
    CHECK(!rsx_overlay_d3d12_packet_valid(&s_packet));
    s_packet.command_count = 1;
    s_packet.display_width = RSX_OVERLAY_DISPLAY_MAX + 1u;
    CHECK(!rsx_overlay_d3d12_packet_valid(&s_packet));
    return 1;
}

static int test_packet_command_rejects_out_of_bounds_indices(void)
{
    rsx_overlay_draw_command command;

    memset(&s_packet, 0, sizeof(s_packet));
    memset(&command, 0, sizeof(command));
    s_packet.vertex_count = 4;
    s_packet.index_count = 3;
    s_packet.command_count = 1;
    s_packet.indices[0] = 0;
    s_packet.indices[1] = 1;
    s_packet.indices[2] = 3;
    command.element_count = 3;

    CHECK(rsx_overlay_d3d12_packet_command_valid(&s_packet, &command));
    command.element_count = 4;
    CHECK(!rsx_overlay_d3d12_packet_command_valid(&s_packet, &command));
    command.element_count = 3;
    s_packet.indices[2] = 4;
    CHECK(!rsx_overlay_d3d12_packet_command_valid(&s_packet, &command));
    /* The base vertex shifts the index range: 3 + 1 is past vertex_count. */
    s_packet.indices[2] = 3;
    command.vertex_offset = 1;
    CHECK(!rsx_overlay_d3d12_packet_command_valid(&s_packet, &command));
    command.vertex_offset = 0;
    command.element_count = 0;
    CHECK(!rsx_overlay_d3d12_packet_command_valid(&s_packet, &command));
    return 1;
}

static int test_clip_rect_maps_display_to_framebuffer(void)
{
    const float half[4] = { -5.0f, 10.0f, 1300.0f, 730.0f };
    const float inner[4] = { 100.25f, 50.5f, 200.5f, 60.25f };
    const float empty[4] = { 9.0f, 9.0f, 9.0f, 10.0f };
    const float nan_rect[4] = { 0.0f, 0.0f, NAN, 10.0f };
    rsx_overlay_d3d12_scissor scissor;

    /* A 1280x720 layout presented on 640x360 scales by 0.5. */
    CHECK(rsx_overlay_d3d12_clip_to_scissor(half, 1280u, 720u, 640u, 360u, &scissor));
    CHECK(scissor.left == 0 && scissor.top == 5 && scissor.right == 640 && scissor.bottom == 360);
    CHECK(rsx_overlay_d3d12_clip_to_scissor(inner, 1280u, 720u, 1920u, 1080u, &scissor));
    CHECK(scissor.left == 150 && scissor.top == 75 && scissor.right == 301 && scissor.bottom == 91);
    /* Laid out at the framebuffer's own size (the shell's normal case): 1:1. */
    CHECK(rsx_overlay_d3d12_clip_to_scissor(inner, 1920u, 1080u, 1920u, 1080u, &scissor));
    CHECK(scissor.left == 100 && scissor.top == 50 && scissor.right == 201 && scissor.bottom == 61);
    CHECK(!rsx_overlay_d3d12_clip_to_scissor(empty, 1280u, 720u, 640u, 360u, &scissor));
    CHECK(!rsx_overlay_d3d12_clip_to_scissor(nan_rect, 1280u, 720u, 640u, 360u, &scissor));
    CHECK(!rsx_overlay_d3d12_clip_to_scissor(inner, 1280u, 720u, 0u, 360u, &scissor));
    CHECK(!rsx_overlay_d3d12_clip_to_scissor(inner, 0u, 720u, 640u, 360u, &scissor));
    return 1;
}

static int test_display_transform_maps_corners_to_ndc(void)
{
    float t[4];

    rsx_overlay_d3d12_display_transform(2560u, 1440u, t);
    CHECK(fabsf(0.0f * t[0] + t[2] + 1.0f) < 1e-6f);
    CHECK(fabsf(0.0f * t[1] + t[3] - 1.0f) < 1e-6f);
    CHECK(fabsf(2560.0f * t[0] + t[2] - 1.0f) < 1e-6f);
    CHECK(fabsf(1440.0f * t[1] + t[3] + 1.0f) < 1e-6f);
    return 1;
}

static int test_win32_keys_map_to_overlay_keys(void)
{
    /* Win32 virtual-key values (winuser.h). */
    CHECK(rsx_overlay_win32_key(0x70u /* VK_F1 */) == RSX_OVERLAY_KEY_F1);
    CHECK(rsx_overlay_win32_key(0x71u /* VK_F2 */) == RSX_OVERLAY_KEY_F2);
    CHECK(rsx_overlay_win32_key(0x26u /* VK_UP */) == RSX_OVERLAY_KEY_UP);
    CHECK(rsx_overlay_win32_key(0x28u /* VK_DOWN */) == RSX_OVERLAY_KEY_DOWN);
    CHECK(rsx_overlay_win32_key(0x25u /* VK_LEFT */) == RSX_OVERLAY_KEY_LEFT);
    CHECK(rsx_overlay_win32_key(0x27u /* VK_RIGHT */) == RSX_OVERLAY_KEY_RIGHT);
    CHECK(rsx_overlay_win32_key(0x0Du /* VK_RETURN */) == RSX_OVERLAY_KEY_ENTER);
    CHECK(rsx_overlay_win32_key(0x1Bu /* VK_ESCAPE */) == RSX_OVERLAY_KEY_ESCAPE);
    /* Keys the overlay does not own never reach it. */
    CHECK(rsx_overlay_win32_key(0x41u /* 'A' */) == 0);
    CHECK(rsx_overlay_win32_key(0x72u /* VK_F3 */) == 0);
    CHECK(rsx_overlay_win32_key(0x20u /* VK_SPACE */) == 0);
    CHECK(rsx_overlay_win32_key(0x10u /* VK_SHIFT */) == 0);
    CHECK(rsx_overlay_win32_key(0x170u) == 0);
    return 1;
}

static int test_win32_modifiers(void)
{
    CHECK(rsx_overlay_win32_modifiers(0, 0, 0) == 0u);
    CHECK(rsx_overlay_win32_modifiers(1, 0, 0) == RSX_OVERLAY_MOD_SHIFT);
    CHECK(rsx_overlay_win32_modifiers(0, 1, 0) == RSX_OVERLAY_MOD_CTRL);
    CHECK(rsx_overlay_win32_modifiers(0, 0, 1) == RSX_OVERLAY_MOD_ALT);
    CHECK(rsx_overlay_win32_modifiers(1, 1, 1) ==
          (RSX_OVERLAY_MOD_SHIFT | RSX_OVERLAY_MOD_CTRL | RSX_OVERLAY_MOD_ALT));
    return 1;
}

static int test_frame_timer_uses_elapsed_time(void)
{
    rsx_overlay_frame_timer timer;
    const uint64_t ms = 1000000ull;
    uint64_t now = 5000ull * ms;
    int i;

    rsx_overlay_frame_timer_init(&timer);
    /* First present has nothing to measure against. */
    CHECK(rsx_overlay_frame_timer_tick(&timer, now) == 0);
    CHECK(!timer.fps_valid);
    /* 25 ms frames: frame time is measured immediately, FPS only after the
     * averaging window has elapsed -- never a frame count per wall second. */
    for (i = 0; i < 19; ++i) {
        now += 25ull * ms;
        CHECK(rsx_overlay_frame_timer_tick(&timer, now) == 0);
        CHECK(timer.frame_ms == 25.0);
        CHECK(!timer.fps_valid);
    }
    now += 25ull * ms; /* 20 frames in 500 ms */
    CHECK(rsx_overlay_frame_timer_tick(&timer, now) == 1);
    CHECK(timer.fps_valid);
    CHECK(timer.fps == 40.0);
    CHECK(timer.frame_ms == 25.0);
    /* A slow frame changes frame time at once; FPS keeps its last window. */
    now += 100ull * ms;
    CHECK(rsx_overlay_frame_timer_tick(&timer, now) == 0);
    CHECK(timer.frame_ms == 100.0);
    CHECK(timer.fps == 40.0);
    /* 100 ms + 16 x 25 ms = 500 ms window, 17 frames -> 34 FPS. */
    for (i = 0; i < 16; ++i) {
        now += 25ull * ms;
        rsx_overlay_frame_timer_tick(&timer, now);
    }
    CHECK(timer.fps == 34.0);
    /* A clock that goes backwards is ignored instead of producing garbage. */
    CHECK(rsx_overlay_frame_timer_tick(&timer, now - 1ull) == 0);
    CHECK(timer.frame_ms == 25.0);
    CHECK(timer.fps == 34.0);
    return 1;
}

int main(void)
{
    int passed = 0;
    const int total = 8;

    passed += test_packet_counts_are_bounded();
    passed += test_packet_command_rejects_out_of_bounds_indices();
    passed += test_clip_rect_maps_display_to_framebuffer();
    passed += test_display_transform_maps_corners_to_ndc();
    passed += test_win32_keys_map_to_overlay_keys();
    passed += test_win32_modifiers();
    passed += test_frame_timer_uses_elapsed_time();
    passed += test_command_atlas_contract();
    if (passed != total) {
        printf("test_rsx_overlay_d3d12: %d/%d passed\n", passed, total);
        return 1;
    }
    printf("test_rsx_overlay_d3d12: %d passed\n", passed);
    return 0;
}
