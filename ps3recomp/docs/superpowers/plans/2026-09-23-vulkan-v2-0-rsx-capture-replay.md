# Vulkan V2.0 — RSX frame recorder + offline replay into the production backends

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Record K consecutive real GoW2 frames in-game (RSX state + guest memory + every FIFO method + presents) behind an OFF-by-default env var, and replay a recording offline into the **production** Metal backend, producing native-size surface dumps that are bit-identical across replays — the deterministic comparison tool that V2.2 (Metal before/after) and V2.3/V2.4 (Vulkan vs Metal) depend on.

**Architecture:** A portable C module `rsx_capture` owns the file format (writer + reader). Four hooks in `cellGcmSys.c` feed it (method, begin-frame, present, display-buffer), all a single predicted branch when off. Memory is captured as 64 KiB pages of the two regions the backends read (RSX local 256 MiB at `0xC0000000` + the GCM IO window), written at each captured frame boundary only when the page's hash changed. A replay host links the Metal backend with a tiny shim (`vm_base`, IO→EA table, display table) instead of the runtime, drives `rsx_process_method` exactly like the walker, and asks the backend to dump its surfaces through a registered hook — the same hook the recorder calls in-game on the last captured frame, so live and replayed pixels can be compared.

**Tech Stack:** C17, Objective-C (Metal backend), SDL2 (hidden window), CMake.

**Spec:** `docs/superpowers/specs/2026-09-23-vulkan-backend-a-design.md` (§6 testing, §7 V2.0 — revised 2026-09-23).

## Global Constraints

- Recorder is a probe: gated by `PS3_RSX_CAPTURE`, **OFF by default, no-op when unset** (one cached static int check per hook; CLAUDE.md rule 6). No behaviour change of the walker or backends when off.
- Captures contain guest memory = game data → written **outside git** (default under `../gow2-recomp/rsx_captures/`, gitignored); never committed (CLAUDE.md rule 1).
- Metal output must not change: the only Metal edits are an exported dump function and one hook registration; the existing gameplay-shot code is untouched.
- Portable C for `rsx_capture.{h,c}` (it is globbed into the runtime on all platforms): no Apple-only/GNU-only calls; little-endian file format with a magic + version + `sizeof(rsx_state)` guard.
- Commits pt-BR, **no** `Co-Authored-By`; explicit `git add`; no `git stash`; never stage build dirs. Worktree `.worktrees/vulkan-v0`.
- Line anchors are from HEAD `39135162`; re-verify with `grep -n` before editing.
- Opening game windows (Task 5) requires asking the user first; `PS3_RSX_BACKEND=null` does **not** avoid a window (it falls back to Metal).

## Review Focus

1. **Recorder OFF cost and zero behaviour change** — pinned in Task 2 by a unit check that with the env unset no file is created and the hooks return before any work (`capture_off_is_noop`).
2. **Truncated or wrong-ABI capture files** (crash mid-write, older build with a different `sizeof(rsx_state)`): the reader must refuse with a clear error, never replay garbage — pinned in Task 1 `reader_rejects_bad_abi` / `reader_rejects_truncated`.
3. **Presents from two paths** (in-stream flip vs immediate HLE flip at `cellGcmSys.c:~2229`): both must reach the single `rsx_capture_on_present` choke point — pinned in Task 2 by routing the duplicated block through `gcm_bridge_present`.
4. **Mid-frame guest-memory changes** (SPU-written vertex rings): frame-boundary page snapshots may not reproduce them; the replay must be compared against the live dump of the same frame and the difference reported, not assumed zero — pinned in Task 5 (`live_vs_replay`).
5. **2D-engine methods (subchannel ≠ 0) in the captured frames**: the replay host cannot execute them in V2.0; it must count and report them, never silently drop them — pinned in Task 4 census line `2d_words=<n>`.

---

### Task 1: `rsx_capture` — file format writer/reader

**Files:**
- Create: `libs/video/rsx_capture.h`, `libs/video/rsx_capture.c`
- Create: `libs/video/tests/test_rsx_capture.c`
- Modify: `CMakeLists.txt` (tests section, outside the shaderc `if`)

**Interfaces (produced):**
```c
/* File: little-endian stream of records { u32 tag; u32 len; u8 payload[len]; }. */
#define RSXC_MAGIC   0x43585352u   /* "RSXC" */
#define RSXC_VERSION 1u
#define RSXC_PAGE    65536u
enum {
    RSXC_TAG_HEADER  = 1,  /* rsxc_header */
    RSXC_TAG_EATABLE = 2,  /* u16 io_to_ea[65536] (1 MiB pages, 0xFFFF = none) */
    RSXC_TAG_DISPLAY = 3,  /* u32 id, offset, pitch, width, height */
    RSXC_TAG_FRAME   = 4,  /* u32 frame_index, u32 pending_batch, u8 state[sizeof(rsx_state)] */
    RSXC_TAG_PAGE    = 5,  /* u32 ea, u8 data[RSXC_PAGE] */
    RSXC_TAG_METHODS = 6,  /* n × { u32 subch, u32 method, u32 value } */
    RSXC_TAG_PRESENT = 7,  /* u32 buffer_id */
    RSXC_TAG_END     = 8   /* u32 frames_written (a file without END is truncated) */
};
typedef struct {
    u32 magic, version, state_size;
    u32 io_address, io_size, local_address, local_size, page_size;
} rsxc_header;

typedef struct rsxc_writer rsxc_writer;
rsxc_writer* rsxc_open_write(const char* path, const rsxc_header* h);   /* NULL on error */
int  rsxc_write_eatable(rsxc_writer* w, const u16* io_to_ea /*65536*/);
int  rsxc_write_display(rsxc_writer* w, u32 id, u32 offset, u32 pitch, u32 width, u32 height);
int  rsxc_write_frame(rsxc_writer* w, u32 frame_index, u32 pending_batch, const void* state, u32 state_size);
int  rsxc_write_page(rsxc_writer* w, u32 ea, const u8* data /*RSXC_PAGE*/);
int  rsxc_write_method(rsxc_writer* w, u32 subch, u32 method, u32 value);   /* buffered, flushed as METHODS records */
int  rsxc_write_present(rsxc_writer* w, u32 buffer_id);
int  rsxc_close_write(rsxc_writer* w, u32 frames_written);               /* writes END, closes */

typedef struct {
    u32 tag, len;
    const u8* data;           /* valid until the next rsxc_next / rsxc_close_read */
} rsxc_record;
typedef struct rsxc_reader rsxc_reader;
/* Validates magic/version/state_size (expected_state_size) and that the file ends with END.
 * On error returns NULL and writes a message into err (size errsz). */
rsxc_reader* rsxc_open_read(const char* path, u32 expected_state_size, rsxc_header* out_h, char* err, u32 errsz);
int  rsxc_next(rsxc_reader* r, rsxc_record* rec);   /* 1 record, 0 at END, -1 on corruption */
void rsxc_close_read(rsxc_reader* r);
/* 64-bit content hash used by the recorder to skip unchanged pages. */
u64  rsxc_hash_page(const u8* data /*RSXC_PAGE*/);
```

- [ ] **Step 1: Write the failing test** `test_rsx_capture.c`:
```c
#include "rsx_capture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define CHECK(n, c) do { if (!(c)) { printf("FAIL %s\n", n); fails++; } else printf("[PASS] %s\n", n); } while (0)
static const char* tmp_path(const char* name) { static char p[512]; snprintf(p, sizeof p, "%s/%s", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", name); return p; }
int main(void)
{
    rsxc_header h = { RSXC_MAGIC, RSXC_VERSION, 1234u, 0x47E00000u, 0x3000000u, 0xC0000000u, 0x10000000u, RSXC_PAGE };
    static u16 eat[65536]; for (u32 i = 0; i < 65536u; i++) eat[i] = (u16)(i < 48u ? 0x47Eu + i : 0xFFFFu);
    static u8 page[RSXC_PAGE]; for (u32 i = 0; i < RSXC_PAGE; i++) page[i] = (u8)(i * 7u);
    static u8 state[1234]; for (u32 i = 0; i < sizeof state; i++) state[i] = (u8)(i ^ 0x5Au);
    char path[512]; snprintf(path, sizeof path, "%s", tmp_path("rsxc_roundtrip.rsxc"));
    rsxc_writer* w = rsxc_open_write(path, &h);
    CHECK("open_write", w != NULL);
    rsxc_write_eatable(w, eat);
    rsxc_write_display(w, 0, 0x400000u, 5120u, 1280u, 720u);
    rsxc_write_frame(w, 7u, 0u, state, sizeof state);
    rsxc_write_page(w, 0xC0010000u, page);
    for (u32 i = 0; i < 100000u; i++) rsxc_write_method(w, i & 1u, 0x1800u + (i & 0xFFu) * 4u, i);
    rsxc_write_present(w, 2u);
    CHECK("close_write", rsxc_close_write(w, 1u) == 0);

    char err[256]; rsxc_header rh;
    rsxc_reader* r = rsxc_open_read(path, 1234u, &rh, err, sizeof err);
    CHECK("open_read", r != NULL && memcmp(&rh, &h, sizeof h) == 0);
    rsxc_record rec; u32 methods = 0, sum_ok = 1, seen = 0; int st;
    while ((st = rsxc_next(r, &rec)) == 1) {
        seen |= 1u << rec.tag;
        if (rec.tag == RSXC_TAG_EATABLE) sum_ok &= memcmp(rec.data, eat, sizeof eat) == 0;
        if (rec.tag == RSXC_TAG_FRAME) sum_ok &= rec.len == 8u + sizeof state && memcmp(rec.data + 8, state, sizeof state) == 0;
        if (rec.tag == RSXC_TAG_PAGE) sum_ok &= memcmp(rec.data + 4, page, RSXC_PAGE) == 0;
        if (rec.tag == RSXC_TAG_METHODS) {
            const u32 n = rec.len / 12u;
            for (u32 k = 0; k < n; k++) {
                u32 t[3]; memcpy(t, rec.data + k * 12u, 12);
                sum_ok &= t[0] == (methods & 1u) && t[1] == 0x1800u + (methods & 0xFFu) * 4u && t[2] == methods;
                methods++;
            }
        }
    }
    CHECK("roundtrip_records", st == 0 && sum_ok && methods == 100000u &&
          (seen & ((1u<<2)|(1u<<3)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<7))) == ((1u<<2)|(1u<<3)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<7)));
    rsxc_close_read(r);

    /* reader_rejects_bad_abi */
    CHECK("reader_rejects_bad_abi", rsxc_open_read(path, 999u, &rh, err, sizeof err) == NULL && strstr(err, "state_size"));
    /* reader_rejects_truncated: drop the END record */
    {
        FILE* f = fopen(path, "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        u8* b = malloc((size_t)n); fread(b, 1, (size_t)n, f); fclose(f);
        char tpath[512]; snprintf(tpath, sizeof tpath, "%s", tmp_path("rsxc_trunc.rsxc"));
        f = fopen(tpath, "wb"); fwrite(b, 1, (size_t)n - 12, f); fclose(f); free(b);
        CHECK("reader_rejects_truncated", rsxc_open_read(tpath, 1234u, &rh, err, sizeof err) == NULL);
        remove(tpath);
    }
    /* hash sanity: equal pages equal hash, one flipped byte changes it */
    { u64 a = rsxc_hash_page(page); page[RSXC_PAGE - 1] ^= 1u; CHECK("hash_changes", a != rsxc_hash_page(page)); }
    remove(path);
    printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
```
CMake: `add_executable(test_rsx_capture libs/video/tests/test_rsx_capture.c libs/video/rsx_capture.c)`, include dirs `libs/video` + `include`, `add_test(NAME test_rsx_capture COMMAND test_rsx_capture)`.
- [ ] **Step 2: Run to confirm it fails** — header missing.
- [ ] **Step 3: Implement** `rsx_capture.c` with `stdio` only: writer buffers methods in a 4096-triple array and emits a `METHODS` record when full, before any other record, and at close; `rsxc_open_read` reads the whole file into memory (fread), validates the header record first, scans record boundaries to the end and requires the last record to be `END` (else "truncated"), then iterates. `rsxc_hash_page`: 64-bit FNV-1a over the page taken 8 bytes at a time (`memcpy` into u64, `h ^= w; h *= 0x100000001B3ull`), plus the page length — any portable full-content hash is fine; it must read every byte.
- [ ] **Step 4: Run** `ctest --test-dir build-tests -R test_rsx_capture --output-on-failure` → ALL PASS.
- [ ] **Step 5: Commit** — `git add libs/video/rsx_capture.h libs/video/rsx_capture.c libs/video/tests/test_rsx_capture.c CMakeLists.txt && git commit -m "feat(rsx): formato de captura de quadros do RSX (escritor/leitor)"`

---

### Task 2: recorder hooks in `cellGcmSys.c` (`PS3_RSX_CAPTURE`)

**Files:**
- Modify: `libs/video/rsx_capture.h`, `libs/video/rsx_capture.c` (recorder state machine)
- Modify: `libs/video/cellGcmSys.c` (method hook ~1646-1649; begin-frame ~1454-1458 and inside `gcm_flip_present_id` ~1366-1373; `gcm_bridge_present` ~2037; the duplicated immediate-present block ~2229-2240; `cellGcmSetDisplayBuffer` ~1847; accessors)
- Modify: `libs/video/rsx_commands.c`, `libs/video/rsx_commands.h` (`rsx_commands_pending_batch()`)
- Modify: `libs/video/tests/test_rsx_capture.c`

**Interfaces:**
- Consumes: Task 1 writer API.
- Produces:
```c
/* PS3_RSX_CAPTURE=<path>:<min_draws>:<frames>  e.g. "/…/rsx_captures/menu.rsxc:150:4"
 * Arms at the first begin-frame whose PREVIOUS frame had rsx_stats_logical() >= min_draws,
 * records <frames> consecutive frames, then closes the file and calls the dump hook
 * (prefix = <path> without extension + "_live"). Unset = no-op. */
int  rsx_capture_enabled(void);                          /* cached env check */
void rsx_capture_on_begin_frame(void);
void rsx_capture_on_method(u32 subch, u32 method, u32 value);
void rsx_capture_on_present(u32 buffer_id);              /* called AFTER the backend presented */
void rsx_capture_on_display_buffer(u32 id, u32 offset, u32 pitch, u32 width, u32 height);
/* Backends register a surface dump (native size); the recorder calls it once, on the last captured present. */
typedef int (*rsx_capture_dump_fn)(const char* prefix);
void rsx_capture_set_dump_hook(rsx_capture_dump_fn fn);
/* Providers the recorder calls (implemented in cellGcmSys.c / rsx_commands.c): */
const rsx_state* gcm_capture_rsx_state(void);
void gcm_capture_io_info(u32* io_address, u32* io_size, const u16** io_to_ea /*65536*/);
void gcm_capture_display_table(void (*emit)(u32 id, u32 off, u32 pitch, u32 w, u32 h));
int  rsx_commands_pending_batch(void);                   /* s_idx_pending || s_arr_pending */
```

- [ ] **Step 1: Write the failing test.** Add to `test_rsx_capture.c` a block `capture_off_is_noop`: `unsetenv("PS3_RSX_CAPTURE")`, then `CHECK("capture_off_is_noop", rsx_capture_enabled() == 0)`, then call `rsx_capture_on_begin_frame(); rsx_capture_on_method(0, 0x1808, 1); rsx_capture_on_present(0); rsx_capture_on_display_buffer(0, 0, 5120, 1280, 720);` and `CHECK` that the stub providers below were **never called** (each stub increments a counter). The test links `rsx_capture.c` only, so define in the test file: `uint8_t* vm_base = NULL;` and counting stubs of `gcm_capture_rsx_state`, `gcm_capture_io_info`, `gcm_capture_display_table`, `rsx_commands_pending_batch`, `rsx_stats_logical`.
- [ ] **Step 2: Run to confirm it fails** (symbols missing).
- [ ] **Step 3: Implement.**
  - `rsx_capture.c` recorder: statics `s_state` (−1 unread / 0 off / 1 armed / 2 recording / 3 done), path, min_draws, frames, frames_done, `rsxc_writer*`, a `u64 s_page_hash[(0x10000000u + 0x3000000u) / RSXC_PAGE]` table initialised to a sentinel; env parse once (`sscanf` of `%[^:]:%u:%u`, invalid → off with one stderr line). `on_begin_frame`: if armed and the previous frame's `rsx_stats_logical() >= min_draws` → open writer (header from `gcm_capture_io_info` + `sizeof(rsx_state)`, local 0xC0000000/0x10000000), write EATABLE and the display table (via `gcm_capture_display_table`), state=recording. If recording: `rsxc_write_frame(frames_done, rsx_commands_pending_batch(), gcm_capture_rsx_state(), sizeof(rsx_state))`, then walk every 64 KiB page of RSX local (`vm_base + 0xC0000000 …`; `extern uint8_t* vm_base;` from `runtime/memory/vm.h`) and of the IO window (`vm_base + io_address …`): hash; if different from `s_page_hash` (or first frame and non-zero content) write the page and store the hash. `on_method`: if recording, `rsxc_write_method`. `on_present`: if recording, write PRESENT, `frames_done++`; when `frames_done == frames`: close, state=done, one stderr line with path/frames/bytes, then call the dump hook with `<path-without-ext>_live`. `on_display_buffer`: if recording, write DISPLAY. All hooks start with `if (s_state <= 0 && !rsx_capture_enabled()) return;` so the OFF path is one cached branch.
  - `cellGcmSys.c`: inside `if (bridge)` at ~1646 add `rsx_capture_on_method(subch, m, val);` **before** dispatching (the order within a frame is what matters); at both begin-frame sites call `rsx_capture_on_begin_frame()` right after `s_rsx_in_frame = 1`; at the end of `gcm_bridge_present` (after `b->present`) call `rsx_capture_on_present(bufferId)`; replace the duplicated immediate-present block (~2229-2240: `end_frame` + `present`) with a call to `gcm_bridge_present(bufferId)` **keeping every other statement of that block (logging/FIRST_PRESENT notes) exactly as is** — read the whole block first and move only the two backend calls; in `cellGcmSetDisplayBuffer` add `rsx_capture_on_display_buffer(bufferId, offset, pitch, width, height);`. Add the three providers at file end (`gcm_capture_rsx_state` returns `&s_rsx_state`; `gcm_capture_io_info` returns `s_config.ioAddress`/`ioSize` and `s_io_address_table`… note: the recorder needs **io offset → ea** = `s_ea_address_table` indexed by io page — name the export accordingly and double-check which table maps which direction at lines ~198-224; `gcm_capture_display_table` iterates `s_display_buffers`).
  - `rsx_commands.c`: `int rsx_commands_pending_batch(void) { return s_idx_pending || s_arr_pending; }` + prototype in the header.
- [ ] **Step 4: Run** `ctest -R "test_rsx_capture|core|msl"` and `cmake --build build-macos-wt --target ps3recomp_runtime`.
- [ ] **Step 5: Commit** — `git add libs/video/rsx_capture.h libs/video/rsx_capture.c libs/video/cellGcmSys.c libs/video/rsx_commands.c libs/video/rsx_commands.h libs/video/tests/test_rsx_capture.c && git commit -m "diag(rsx): PS3_RSX_CAPTURE grava K quadros (estado, paginas, metodos, presents)"`

---

### Task 3: Metal native-size surface dump + hook registration

**Files:**
- Modify: `libs/video/rsx_metal_backend.m` (new exported `rsx_metal_dump_surfaces`; registration in `rsx_metal_backend_init`)
- Modify: `libs/video/rsx_metal_backend.h`

**Interfaces:**
- Produces: `int rsx_metal_dump_surfaces(const char* prefix);` — commits and waits for all in-flight command buffers (same idiom as the gameplay shot around `rsx_metal_backend.m:~9292`), then writes `<prefix>_display.bmp` (the surface the last present showed: `cellGcmGetDisplayInfo(last buffer id)->offset` → `metal_surface_find(0, off, 0)`, falling back to the last colour target — reuse the exact selection code of `metal_present_impl`) and, for every initialised colour surface, `<prefix>_c<loc>_<offset:08X>_<w>x<h>.bmp` via the existing `metal_dump_texture_to`. Names depend only on guest location/offset/size, never on cache slot order. Returns the number of files written. Registered with `rsx_capture_set_dump_hook(rsx_metal_dump_surfaces)` at the end of a successful init.

- [ ] **Step 1: Test ordering.** This function is exercised by Task 4's synthetic replay test (`replay_synthetic_clear`), which cannot exist before the replay host; here only build it. Say so in the report.
- [ ] **Step 2: Implement** as specified; do not touch the existing gameplay-shot code path.
- [ ] **Step 3: Build** `cmake --build build-macos-wt --target ps3recomp_runtime` (no new warnings).
- [ ] **Step 4: Commit** — `git add libs/video/rsx_metal_backend.m libs/video/rsx_metal_backend.h && git commit -m "feat(rsx): dump nativo das superficies do Metal por offset (usado pela captura/replay)"`

---

### Task 4: replay host `rsx_replay` + census

**Files:**
- Create: `tools/rsx_replay/rsx_replay_main.c`, `tools/rsx_replay/replay_shim.c`
- Create: `libs/video/tests/test_rsx_replay_synthetic.sh`
- Modify: `CMakeLists.txt` (APPLE-only executable `rsx_replay`, not in the runtime; links the video sources it needs + SDL2 + Metal/QuartzCore frameworks)

**Interfaces:**
- Consumes: Task 1 reader, Task 3 dump, `rsx_process_method`, `rsx_metal_backend_init/_pump_messages`.
- Produces: CLI `rsx_replay <capture.rsxc> <out_prefix> [--census] [--backend metal]` → exit 0 and files `<out_prefix>_display.bmp` + per-surface BMPs; with `--census` prints one line per frame: `frame=<i> methods=<n> draws=<n> clears=<n> color_targets=<set of SET_SURFACE_COLOR_TARGET values> tex_formats=<set> blend_on=<n> stencil_on=<n> 2d_words=<n>`.

- [ ] **Step 1: Shim.** `replay_shim.c` defines: `uint8_t* vm_base` (4 GiB `mmap(PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON|MAP_NORESERVE)`), `ppu_guest_range_committed` (true iff inside RSX local or the recorded IO window), `gcm_io_offset_to_ea` (from the recorded EATABLE, same fallback `ioAddress + off` as `cellGcmSys.c:~2534`), `cellGcmGetDisplayInfo` / `cellGcmGetCurrentDisplayBufferId` (from recorded DISPLAY records and the last PRESENT id), and no-op/zero stubs for every other undefined symbol the Metal object needs — get the exact list with `nm -u build-macos-wt/CMakeFiles/ps3recomp_runtime.dir/libs/video/rsx_metal_backend.m.o` after linking the video sources below, and stub only what remains (pads, SPU put map, stats, movie/host-content, gcm perf). **Do not link `cellGcmSys.c`.**
- [ ] **Step 2: Main.** Parse args; `rsxc_open_read(path, sizeof(rsx_state), …)`; set `PS3_WINDOW_HIDDEN=1`, `PS3_METAL_PER_DRAW_RT=1`, and disable host overlays/movies/prompts via their env gates (list them in the report); `rsx_metal_backend_init(1280, 720, "rsx_replay")`. Iterate records: EATABLE/DISPLAY → shim; PAGE → `memcpy(vm_base + ea, data, RSXC_PAGE)`; FRAME → `memcpy(&state, …)` into a replay-owned `static rsx_state state` (abort with a message if `pending_batch != 0`), then `b->begin_frame` (mirroring the walker); METHODS → subch 0: `rsx_process_method(&state, m, v)`; subch ≠ 0: count `2d_words` (and skip); PRESENT → `b->end_frame` + `b->present(id)` + pump messages. After END: `rsx_metal_dump_surfaces(out_prefix)`. Census counters come from the method stream (NV4097 method ids: `SET_SURFACE_COLOR_TARGET`, `SET_TEXTURE_FORMAT(n)`, `SET_BLEND_ENABLE`, `SET_STENCIL_TEST_ENABLE`, `CLEAR_SURFACE`, `SET_BEGIN_END`) — take the ids from `rsx_commands.h`.
- [ ] **Step 3: Synthetic test first** — `test_rsx_replay_synthetic.sh` builds a tiny capture with a helper (compile a 30-line C writer using `rsx_capture.c` that writes HEADER/EATABLE/DISPLAY(0, offset 0, 5120, 1280, 720)/FRAME(zeroed state)/METHODS: `SET_SURFACE_COLOR_TARGET=1`, color offset 0, `SET_COLOR_CLEAR_VALUE=0xFF20A040`, `CLEAR_SURFACE=0xF0`/PRESENT(0)/END), runs `rsx_replay` on it, and checks the centre pixel of `<prefix>_display.bmp` equals the clear colour (read BMP with `python3` stdlib `struct`, no PIL). Run it: FAIL before Steps 1-2 compile, PASS after.
- [ ] **Step 4: Determinism** — run the synthetic replay twice; `cmp` of every BMP pair is empty.
- [ ] **Step 5: Commit** — `git add tools/rsx_replay/rsx_replay_main.c tools/rsx_replay/replay_shim.c libs/video/tests/test_rsx_replay_synthetic.sh CMakeLists.txt && git commit -m "feat(tools): rsx_replay reproduz uma captura no backend Metal de producao (+ censo)"`

---

### Task 5: acceptance on real GoW2 frames (menu)

**Files:** `../gow2-recomp/.gitignore` (add `/rsx_captures/`, separate repo commit), the Execution record appended to this plan.

- [ ] **Step 1: Build the capture binary** from the worktree engine: `cmake --build build-macos-wt --target ps3recomp_runtime` then in `../gow2-recomp`: `PY=<main>/.venv/bin/python PS3_ENGINE_ROOT=<worktree> RUNTIME_LIB=<worktree>/build-macos-wt/libps3recomp_runtime.a OUT=$PWD/boot_gow2_v20cap ./build_macos.sh recomp_macos_e435`.
- [ ] **Step 2: Ask the user** before opening a window. With their OK, run the launcher env windowed with `PS3_RSX_CAPTURE="$PWD/rsx_captures/menu.rsxc:150:4"` (menu steady state is ~219 logical draws per frame; 150 arms on the title/menu) and `PS3_PAD_AUTOSTART` unset, until the recorder prints its closing line; then close the game. Confirm `menu_live_display.bmp` and per-surface `_live_` BMPs exist.
- [ ] **Step 3: Replay twice** — `rsx_replay rsx_captures/menu.rsxc /tmp/r1 --census` and again to `/tmp/r2`: every `r1_*` vs `r2_*` BMP pair identical (`cmp`). Record the census lines.
- [ ] **Step 4: Live vs replay** (`live_vs_replay`) — compare `menu_live_*` with `r1_*` per surface: % pixels > 8/255 and identical yes/no. Expected: identical if frame-boundary page snapshots suffice; if not, report which surfaces differ and by how much (this measures the mid-frame-memory risk; do not tune it away).
- [ ] **Step 5: Record** an `## Execution record (V2.0 acceptance)` at the end of this plan (capture size, frames, census, determinism result, live-vs-replay numbers, 2D word count) and commit it; commit the gow2-recomp `.gitignore` line there (pt-BR). Delete `boot_gow2_v20cap`. Keep the capture file for V2.2-V2.4 (outside git).

## Execution record (V2.0 acceptance)

- 2026-09-23. Commits on `vulkan-v0`: fb45ea97 + 0ef551c6 (format; 64-bit file offsets), 5bddacd0 (recorder + single present choke point), e3fc5833 (Metal native surface dump), fef254eb (`rsx_replay` + census + synthetic test).
- Capture (in-game, user OK, window, no pad autostart, `PS3_RSX_CAPTURE=…/menu.rsxc:150:4`): armed at prev_frame_draws=183, 4 frames, 815 pages, 145 122 methods, 55 366 844 bytes, 48 s after launch; 17 live surface dumps (incl. MRT B `c1_00200000_1216x684`).
- Census (replay, per frame): methods 36 163-36 633, draws 140-144, clears 5, colour targets {0x1, 0x0, 0x13 (MRT A+B)}, 10 texture formats, blend_on 80-82, stencil_on 0, **2d_words 0**.
- **Determinism: PASS** — two replays, 17/17 surfaces byte-identical (`cmp`); ~4 s per replay.
- **Live vs replay: FAIL (measured, not tuned):** 3/17 identical (both depth surfaces + `c0_00000000`); display differs on 90 % of pixels (replay shows the fallback PSO's UV gradient). Cause proven in the replay log: `set_shader: fp ea=0xC03C2600..0xC03C2880 size=0, fallback PSO` — GoW2 writes fragment-program ucode into an RSX-local ring **during the frame**, so frame-boundary page snapshots miss it (Review Focus 4). Textures/vertices written mid-frame are presumably affected the same way.
- **V2.0 status:** the tooling (format, recorder, replay, dumps, census) works and is deterministic; replay is **not yet faithful**. Follow-up **V2.0b**: capture guest memory at draw granularity (e.g. write-protect the captured ranges while recording, mark pages dirty on fault, emit dirty pages before each draw/shader bind), then re-run live-vs-replay.
- Final whole-branch review (2026-09-23): "with fixes" → f2310607 (recorder skips uncommitted pages via `ppu_guest_range_committed` — SIGBUS risk when `PS3_VM_RSX_MB` < 256, ASan-proven test; forced flips during recording counted and reported as "not a valid baseline"; unknown record tags fatal in `rsx_replay`; dump logs skipped non-BGRA8 surfaces, path buffer 1100; giant-lock assumption documented; leak fixed). Re-review: all addressed.

### Carried to V2.0b (draw-granular memory capture)
1. The format already supports mid-frame PAGE records (writer flushes METHODS before any other record; replay applies PAGE in stream order) — no break needed for full pages.
2. Recommended design: at the method hook (state current, right before `rsx_process_method`), capture the ranges the next draw will read — FP ucode (scan to the end bit) at `SET_SHADER_PROGRAM`/draw, vertex arrays, index buffer, bound textures — hashed and emitted only when changed (new versioned `RANGE{ea,len,data}` tag, `RSXC_VERSION` 2), instead of write-protect/fault handlers (host-side SPU DMA/atomics, existing SIGBUS handling, 16 KB vs 64 KB pages, Windows handlers). First confirm the Metal backend reads guest memory synchronously inside `rsx_process_method` (not deferred).
3. Snapshot EATABLE-mapped EA pages outside the IO window (or refuse them); compare live vs replay from frame 1 (frame 0 = warm-up for the empty surface cache); consider `mmap` in the reader for GB-sized captures; every capture reports `forced=` and is discarded as a baseline when > 0.
