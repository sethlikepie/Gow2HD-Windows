# Vulkan V2.0b — draw-granular guest-memory capture (faithful replay)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `rsx_replay` reproduce the live frame by recording, at the exact moment each backend callback reads guest memory, the bytes it reads — so V2.0's live-vs-replay comparison (3/17 identical, FP ucode ring written mid-frame) becomes identical from frame 1.

**Architecture:** While recording, `rsx_capture` wraps the registered `rsx_backend` vtable with an interposer. Before forwarding `set_shader`, `bind_texture`, `draw_arrays` and `draw_indexed`, it computes the guest ranges that callback reads (same rules as the Metal backend, via shared helpers), splits them into 4 KiB chunks, and emits a new `RANGE` record for every chunk whose content hash changed since it was last emitted. The walker's method hook moves to **after** dispatch, so ranges read while a method executes precede that method in the stream; replay applies each RANGE when it reads it, then dispatches the method. Format bumps to v2; v1 captures still load.

**Tech Stack:** C17, CMake, the existing capture/replay tooling.

**Spec:** `docs/superpowers/specs/2026-09-23-vulkan-backend-a-design.md` (§7 V2.0). **Predecessor:** `docs/superpowers/plans/2026-09-23-vulkan-v2-0-rsx-capture-replay.md` (Execution record + "Carried to V2.0b").

## Global Constraints

- Recorder OFF (env unset): walker, Metal and every backend behave and cost exactly as today; the interposer is installed only while RECORDING and removed at close; the method hook stays one inline check.
- Ranges emitted are a superset of what the backend reads (emitting more real guest bytes is safe); never read uncommitted memory — every chunk is checked with `ppu_guest_range_committed` before hashing/copying.
- Portable C only (rsx_capture.c / cellGcmSys.c build on Windows); no fault handlers / page protection.
- Captures are game data: outside git (`../gow2-recomp/rsx_captures/`, gitignored).
- Commits pt-BR, **no** `Co-Authored-By`; explicit `git add`; no `git stash`; never stage build dirs, captures or BMPs. Worktree `.worktrees/vulkan-v0`. Anchors from HEAD after merge `c06df162`; re-verify with `grep -n`.
- Opening a game window (Task 5) requires asking the user first.

## Review Focus

1. **Ordering:** a RANGE emitted during a callback must precede, in the file, the method whose dispatch triggered it (including draws flushed at a later method by batching) — pinned in Task 3 `range_precedes_method` with a fake backend.
2. **FP constants patched per draw:** the FP range must be re-emitted (when changed) at every draw, not only at `set_shader` — pinned in Task 3 `fp_reemitted_per_draw`.
3. **Overlapping ranges** (interleaved vertex attributes, atlases bound with several formats): chunk-hash dedupe must emit each changed chunk once per change — pinned in Task 1 `range_dedupe_overlap`.
4. **Uncommitted chunks inside a range** (straddling the end of committed memory): skipped, never read — pinned in Task 2 `ranges_skip_uncommitted`.
5. **v1 captures** (made before this plan) still replay; unknown tags stay fatal — pinned in Task 4 `replay_v1_capture_still_loads`.

---

### Task 1: format v2 — `RANGE` record + chunk-hash dedupe

**Files:** Modify `libs/video/rsx_capture.h`, `libs/video/rsx_capture.c`, `libs/video/tests/test_rsx_capture.c`.

**Interfaces (produced):**
```c
#define RSXC_VERSION     2u            /* v2 adds RANGE; the reader accepts 1 and 2 */
#define RSXC_CHUNK       4096u
enum { RSXC_TAG_RANGE = 9 };           /* u32 ea, u32 len (<= RSXC_CHUNK), u8 data[len] */
int  rsxc_write_range(rsxc_writer* w, u32 ea, const u8* data, u32 len);
/* Chunk dedupe table owned by the recorder (open addressing, grows by doubling). */
typedef struct rsxc_chunk_table rsxc_chunk_table;
rsxc_chunk_table* rsxc_chunks_new(void);
void rsxc_chunks_free(rsxc_chunk_table* t);
/* Returns 1 if the chunk at ea (aligned to RSXC_CHUNK) is new or its hash changed (and records it), else 0. */
int  rsxc_chunks_changed(rsxc_chunk_table* t, u32 chunk_ea, u64 hash);
u64  rsxc_hash_bytes(const u8* p, u32 len);   /* same word-wise FNV as rsxc_hash_page, any length */
```

- [ ] **Step 1: failing tests** in `test_rsx_capture.c`: `range_roundtrip` (write 3 RANGE records incl. one with len < RSXC_CHUNK, read back byte-equal, header version 2); `reader_accepts_v1` (hand-write a v1 header + END → opens); `range_dedupe_overlap` (table: same chunk/hash → changed 1 then 0; different hash → 1; 10 000 distinct chunks force growth, all still found).
- [ ] **Step 2:** run → fail (symbols missing).
- [ ] **Step 3: implement.** Writer flushes METHODS before RANGE like every other record. Reader: accept `version == 1 || version == 2`. Chunk table: key = `chunk_ea / RSXC_CHUNK` (u32), value u64 hash, empty marker via a separate occupancy byte; load factor ≤ 0.5.
- [ ] **Step 4:** `ctest --test-dir build-tests -R test_rsx_capture --output-on-failure`; `cmake --build build-macos-wt --target ps3recomp_runtime rsx_replay`.
- [ ] **Step 5: commit** `git add libs/video/rsx_capture.h libs/video/rsx_capture.c libs/video/tests/test_rsx_capture.c && git commit -m "feat(rsx): captura v2 com registro RANGE e deduplicacao por blocos de 4 KiB"`

---

### Task 2: range calculators (portable, mirror the Metal read rules)

**Files:** Create `libs/video/rsx_capture_ranges.h`, `libs/video/rsx_capture_ranges.c`, `libs/video/tests/test_rsx_capture_ranges.c`; Modify `CMakeLists.txt`.

**Interfaces (produced):**
```c
typedef struct { u32 ea, len; } rsxc_span;
typedef int (*rsxc_io2ea_fn)(u32 io_off, u32* ea);
/* FP ucode: EA via rsx_core_resolve_fp_ea, len = max(512, rsx_fp_program_size(vm_base+ea, 16 KiB)) scanned
 * on the SAME bytes; 0 spans when shader_program == 0 or not committed. */
int rsxc_fp_span(const rsx_state* s, const u8* vm_base, rsxc_io2ea_fn io2ea, rsxc_span* out);
/* Texture unit: EA via rsx_core_resolve_tex_ea; len = swizzled&&!BC ? rsx_morton_padded_texels(w,h)*bpp/8
 * : rsx_texture_row_pitch(fmt,w) * rsx_texture_src_rows(fmt,h); 0 spans for w/h 0, side > 4096,
 * unsupported format, size 0 or > 256 MiB (same skip rules as metal bind_texture ~2044-2189). */
int rsxc_tex_span(const rsx_texture_state* t, rsxc_io2ea_fn io2ea, rsxc_span* out);
/* Vertex attribute a (enabled, type != NONE, size != 0): EA per metal_vtx_ea (loc bit31, base offset
 * (vertex_data_base_offset + off) & 0x0FFFFFFF when vertex_base_on, local 0xC0000000+off, main via io2ea,
 * fallback raw off), span = [ea0, ea0 + count*stride + 16), stride = a->stride ? a->stride
 * : rsx_vertex_attrib_size(type, size). Writes up to 16 spans; returns the count. */
int rsxc_vertex_spans(const rsx_state* s, u32 first, u32 count, int vertex_base_on,
                      rsxc_io2ea_fn io2ea, rsxc_span out[16]);
/* Index buffer for draw_indexed(offset, count): EA per metal draw_indexed ~10466-10481, span
 * [ea + offset*elem, + count*elem); also decodes min/max (restart indices skipped, rsx_index.h)
 * from vm_base to produce vfirst/nvert exactly like metal (~10566-10568). */
int rsxc_index_span(const rsx_state* s, u32 offset, u32 count, const u8* vm_base, int vertex_base_on,
                    rsxc_io2ea_fn io2ea, rsxc_span* out, u32* vfirst, u32* nvert);
```

- [ ] **Step 1: failing tests** `test_rsx_capture_ranges.c` with a fake 1 MiB `vm_base` arena and a stub io2ea: FP span size equals `rsx_fp_program_size` of a synthetic 3-instruction program (end bit on the 3rd) and ≥ 512; texture spans for A8R8G8B8 swizzled 100×60 (morton padded 128×64×4), linear 100×60 pitch, DXT1 64×64 (8 B/block), and the skip cases; vertex spans for an interleaved 2-attribute buffer (overlapping spans OK), base offset on/off, main-memory location via io2ea; index span for 16-bit with a restart index and min/max; `ranges_skip_uncommitted` lives in Task 3 (it needs the recorder).
- [ ] **Step 2:** run → fail.
- [ ] **Step 3: implement** using the shared helpers named in the interfaces (`rsx_core_resolve_fp_ea`, `rsx_core_resolve_tex_ea`, `rsx_texture_formats.h`, `rsx_morton_padded_texels`, `rsx_vertex_attrib_size`, `rsx_index_from_guest`/`rsx_index_is_restart`); mirror `metal_vtx_ea` and the index EA code **verbatim** in logic (cite the Metal lines in comments). CMake: add the test executable (sources: test + `rsx_capture_ranges.c` + `rsx_core_rules.c` + `rsx_fp_decompiler.c` + `rsx_glsl_dialect.c` as needed to link).
- [ ] **Step 4:** ctest `-R "rsx_capture"`; runtime build.
- [ ] **Step 5: commit** `git add libs/video/rsx_capture_ranges.h libs/video/rsx_capture_ranges.c libs/video/tests/test_rsx_capture_ranges.c CMakeLists.txt && git commit -m "feat(rsx): calculo dos trechos de memoria que cada chamada do backend le (espelha o Metal)"`

---

### Task 3: backend interposer + method hook after dispatch

**Files:** Modify `libs/video/rsx_capture.c`, `libs/video/rsx_capture.h`, `libs/video/cellGcmSys.c` (method hook ~1652-1656), `libs/video/tests/test_rsx_capture.c`.

**Interfaces:**
- Consumes: Task 1 (`rsxc_write_range`, chunk table, `rsxc_hash_bytes`), Task 2 calculators, `rsx_get_backend`/`rsx_set_backend` (rsx_commands.h), `gcm_capture_rsx_state()`, `gcm_io_offset_to_ea`, `ppu_guest_range_committed`.
- Produces: interposer installed in `rc_start` (after the header/snapshot) by copying the registered backend into a static `rsx_backend s_rc_real` and registering a wrapper whose `set_shader/bind_texture/draw_arrays/draw_indexed` emit ranges then forward to `s_rc_real` (all other callbacks forward unchanged, `userdata` preserved); removed in the close path by re-registering the real backend. Emission helper: for each span, for each 4 KiB-aligned chunk intersecting it that is committed, hash the chunk's bytes, and if `rsxc_chunks_changed` write `RANGE(chunk_ea, bytes)`. Texture rule: emit on the first bind of a given `(ea,len,fmt,w,h,remap)` key in the frame only (mirror Metal ~2213); FP: emit at `set_shader` **and** at every draw (Metal re-reads it for inline constants).

- [ ] **Step 1: failing tests** (fake backend in the test that records the order of callbacks, a fake `vm_base` arena, stubs): `range_precedes_method` — start a recording, simulate method M whose dispatch calls the backend's `draw_arrays`, then read the file: the RANGE records appear **before** M's METHODS entry; `fp_reemitted_per_draw` — change one inline-constant word of the FP bytes between two draws → a new RANGE for that chunk appears before the second draw's method; `ranges_skip_uncommitted` — a vertex span straddling the end of committed memory emits only the committed chunks (stub counts reads); `interposer_removed_on_close` — after the last present the registered backend is the real one again.
- [ ] **Step 2:** run → fail.
- [ ] **Step 3: implement** the interposer and move the walker's `rsx_capture_on_method(...)` call to **after** `rsx_process_method` / `gcm_2d_method` (keep the inline `g_rsx_capture_state == 2` guard). Explain in a comment why after-dispatch is required (ranges read during dispatch must precede the method).
- [ ] **Step 4:** ctest `-R "rsx_capture|core|msl"`; runtime + `rsx_replay` build; `bash libs/video/tests/test_rsx_replay_synthetic.sh` still passes.
- [ ] **Step 5: commit** `git add libs/video/rsx_capture.h libs/video/rsx_capture.c libs/video/cellGcmSys.c libs/video/tests/test_rsx_capture.c && git commit -m "feat(rsx): captura grava a memoria que cada draw le (interposicao no backend; metodo registrado apos a execucao)"`

---

### Task 4: replay applies RANGE records

**Files:** Modify `tools/rsx_replay/rsx_replay_main.c`, `tools/rsx_replay/replay_shim.c`, `libs/video/tests/test_rsx_replay_synthetic.sh`.

- [ ] **Step 1: failing test** — extend the synthetic script: a capture whose single frame binds a 64×64 linear A8R8G8B8 texture at local offset X, draws a full-screen textured quad, where the texture bytes at X in the frame-start PAGE are colour A but a RANGE record written before the draw sets them to colour B; the replayed display centre pixel must be B. Plus `replay_v1_capture_still_loads` (the existing v1 synthetic capture still replays). Simpler equivalent allowed if building a textured draw by hand is disproportionate: an FP whose output colour is an **inline constant** (FP ucode lives in guest memory; the RANGE rewrites the constant word before the draw) drawn with a passthrough VP uploaded through the method stream and a 3-vertex array in guest memory — reuse the FP/VP encoding helpers in `libs/video/tests/test_glsl_decompiler.c`. If neither is feasible, add `rsx_replay --dump-range <ea>:<len>` and assert the bytes at the draw point instead, and say so in the report.
- [ ] **Step 2:** run → fail (replay rejects tag 9 as unknown).
- [ ] **Step 3: implement** — RANGE → `memcpy(vm_base + ea, data, len)` immediately, and mark the covering 1 MiB page(s) as committed in the shim (`replay_shim.c` committed check accepts local, IO window, EATABLE pages and pages touched by RANGE/PAGE). Keep unknown tags fatal.
- [ ] **Step 4:** run the synthetic script (both cases) + determinism (`cmp`).
- [ ] **Step 5: commit** `git add tools/rsx_replay/rsx_replay_main.c tools/rsx_replay/replay_shim.c libs/video/tests/test_rsx_replay_synthetic.sh && git commit -m "feat(tools): rsx_replay aplica os registros RANGE da captura v2"`

---

### Task 5: acceptance — live vs replay on the menu

- [x] **Step 1:** build the capture binary from the worktree (`cmake --build build-macos-wt --target ps3recomp_runtime rsx_replay`; `cd ../gow2-recomp && PY=<main>/.venv/bin/python PS3_ENGINE_ROOT=<wt> RUNTIME_LIB=<wt>/build-macos-wt/libps3recomp_runtime.a OUT=$PWD/boot_gow2_v20bcap ./build_macos.sh recomp_macos_e435`).
- [x] **Step 2: ask the user**, then capture 4 menu frames (`PS3_RSX_CAPTURE=…/rsx_captures/menu_v2.rsxc:150:4`, windowed, no pad autostart). The done line must show `forced=0` (else re-capture once; if still forced, record it).
- [x] **Step 3:** replay twice (`cmp` identical) and compare `menu_v2_live_*` vs replay **from the last frame** (frame 0 is warm-up; the live dump is taken at the last captured present). Per surface: identical yes/no, % px > 8/255.
- [x] **Step 4:** expected: display + every colour surface identical, or a measured, explained residual (E488-class concurrent writes during a callback; surfaces carried from before frame 0). Record numbers and the census in an `## Execution record (V2.0b acceptance)` at the end of this plan; commit it. Delete `boot_gow2_v20bcap`.

## Execution record (V2.0b acceptance)

- 2026-09-23. Engine at worktree HEAD `22ea3fa4` (Tasks 1-4: `RANGE` tag / `RSXC_VERSION` 2, backend interposer, method recorded after dispatch, replay applies RANGE). Capture binary `g2cap_v20b` (lift `recomp_macos_e435`, runtime from `build-macos-wt`), deleted after the run.
- Capture (user OK, one instance, window `PS3_FULLSCREEN=0`, launcher env from `env_gow2.sh` + `PS3_MOVIE_DONE_MS=auto`, `PS3_PAD_AUTOSTART` unset, `PS3_MUTE=1`, `PS3_RSX_CAPTURE=…/rsx_captures/menu_v2.rsxc:150:4`); killed by PID right after the dump line. Recorder lines:
  - `recording: prev_frame_draws=183 local=0xC0000000+0x10000000 io=0x47E00000+0x3000000 state_size=18036 skipped=0`
  - `done: frames=4 pages=1142 ranges=5230 methods=145122 skipped=0 skipped_chunks=0 forced=0 bytes=98307024`
  - `live dump prefix=…/menu_v2_live rc=17` (17 surfaces, same set as V2.0: display, 12 `c0`, 2 `c1` MRT B, 2 `z0`).
- Replay: `rsx_replay menu_v2.rsxc <out> --census`, ~1.4 s wall, `frames=4 presents=4 pages=1142 ranges=5230 (21 422 080 B) dumped=17`; zero `fallback` lines in the replay log (V2.0 had `set_shader … size=0, fallback PSO`).
- Census (per frame, identical to V2.0): frames 0-2 `methods=36163 draws=140 clears=5 blend_on=80`, frame 3 `methods=36633 draws=144 blend_on=82`; `color_targets={0x1,0x0,0x13}`, 10 tex formats, `stencil_on=0`, `2d_words=0`.
- **Determinism: PASS**: two replays, 17/17 BMP pairs byte-identical (`cmp`).
- **Live vs replay: PASS, faithful**: compared the live dumps (taken at the last captured present) with replay dumps (after the last frame). Result: **17/17 surfaces byte-identical** (`cmp`), 0.000 % px > 8/255, max channel diff 0. That covers display, all 12 `c0` colour surfaces, both MRT-B `c1` (`00200000`, `008E0000`) and both depth surfaces. The live display isn't trivial (772 distinct colours in a 1/97 sample, mean 47/255).
- Comparator control: the same script run against the **V2.0** capture (`menu.rsxc`, v1, which still replays) reports display 90.09 % px > 8/255 (max 255) and `c1_00200000` 16.93 %. The comparator detects differences, and the RANGE records are what closed the gap.
- Residual: none measured. No E488-class concurrent-write residue and no carried-over surfaces showed up in this 4-frame menu window. This is one capture of the static menu only. Gameplay scenes, with more mid-frame CPU and SPU writes, are unmeasured.


## Carried to V2.1+ (parked review findings)

- Vertex spans are computed from the live `rsx_state`, while Metal draws from the `s_draw.attribs` snapshot taken at BEGIN; they diverge only if attributes change inside a BEGIN/END with a pending batch.
- The replay shim marks a whole 1 MiB page committed per RANGE, so Metal's committed-memory guards can pass in replay where they failed live.
- `rc_install` swaps `s_backend` non-atomically from the present thread, and the wrapper can forward to a shut-down Metal backend if Metal stops mid-capture (capture-only).
- Metal's sampled-signature texture cache can serve stale content live that replay will not reproduce (inherent; not a capture defect).
- The reader accepts RANGE in a v1-header file; RANGE outside the local/IO/EATABLE windows is untested.
- Acceptance covers one static-menu capture; gameplay (more mid-frame CPU/SPU writes) is not yet measured.
