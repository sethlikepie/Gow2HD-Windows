# Contributors

ps3recomp exists because people who love the PS3 keep showing up. Thank you to
everyone who has contributed code, fixes, testing, or a hard-won debugging
insight. This file is the canonical record of who did what; the README changelog
tells the story version-by-version, but credit lives here.

If you've contributed and aren't listed (or a line is wrong), open a PR against
this file — we want every name right.

---

## Maintainer

### Ned Heller — [@sp00nznet](https://github.com/sp00nznet)
Project creator and maintainer. Core toolchain (PPU/SPU disassemblers and
lifters, ELF/SELF/PRX parsing, NID system, runtime libraries and system-call
layer), the recompilation pipeline, and the batch-triage harness.

---

## Contributors

### Caner Saka — [@canersaka](https://github.com/canersaka)
Found and fixed a remarkable run of correctness bugs while stress-testing the
toolkit against a 22 MB / ~45,000-function **Yakuza: Dead Souls** port — which
turned out to be an excellent fuzzer for everything the pipeline got subtly
wrong. Landed in **v0.6.1 "Many Hands"**:

*Decode & lift correctness*
- **NID computation fix** — the suffix constant was truncated/corrupted and the
  digest was read big-endian; corrected to the authoritative 16-byte suffix +
  little-endian. Took NID resolution on a real game from 0/354 to 343/354.
- **VMX/AltiVec decode tables** — cross-referenced against the Power ISA manuals;
  fixed dozens of mnemonics mapped to the wrong opcodes that were decoding
  silently and lifting to valid C for the *wrong* operation.
- **VMX lifter handlers** — `addc`/`subfc` with carry-out into XER[CA], the
  unaligned vector loads `lvlx`/`lvrx`/`lvlxl`, `ldbrx`, `vsrw`, `vsububs`,
  `vsum2sws`, `vupkhsh`, `vrfim`, and a missing `vand` dispatch entry.
- **`sradi` decode for shifts ≥ 32** — the 6-bit shift's top bit lives in
  instruction bit 30, so the XO field reads 827 for shifts of 32+.
- **Function detection seeded from the `.opd` table** — seeds starts from every
  address-taken function descriptor (located by shape, since section names are
  often stripped), bounds them at the first `blr`, and filters phantom branch
  targets. A large improvement to coverage of pointer-only/virtual functions.

*Lifter throughput*
- **Parallel lifting** (`--jobs N`) across a process pool, chunked C output, and
  record-form CR0 handling — turned a multi-hour single-core lift into minutes.

*Runtime & libraries*
- **`sys_vm` syscall family (300–313)** implemented from scratch.
- **`sys_memory`** allocations from a committed-on-demand window matching
  real-hardware addresses, plus thread-safe bump allocation.
- **`sys_lwmutex_destroy`** lv2-correct ESRCH/EBUSY semantics + slot locking.
- **TTY syscall numbers** corrected to 402/403.
- **`cellVideoOut`** big-endian guest structs + corrected `CellGcmContextData`
  layout.

*Build & tooling*
- Excluded runtime test harnesses from the library build (clean MSVC build).
- **`tools/show_func.py`** — dump a single lifted function's C and/or its
  original PowerPC disassembly from the chunked output.

### Paulo Adriano Alves — [@pauloadrianoalves](https://github.com/pauloadrianoalves)
Initial **PPU boot path** and supporting tooling (PR #3, partially incorporated
in **v0.6.2** — the SPU portions were superseded by the v0.6.0 SPU subsystem and
not taken):
- **PPU boot scaffold** — `runtime/ppu/` (loader, HLE dispatch, sysprx, fs) +
  `runtime/host/host_main.c`: the per-game path that loads a lifted PPU image,
  links it with the HLE runtime, and boots it. Compiled per-game against the
  lifter-generated `ppu_recomp.h`, so it lives outside the game-agnostic runtime
  library build.
- **RSX shader decompilers** — `libs/video/rsx_fp_decompiler.*` and
  `rsx_vp_decompiler.*`: NV40 fragment/vertex program → host shader translation,
  with a validation-test corpus.
- **Tooling** — `tools/ppu_loader.py` (image manifest / OPD table / TOC / imports
  extraction) and `tools/gen_hle_nids.py`.
- **Docs** — `docs/PPU_RECOMP.md`, `docs/RSX_FRAGMENT_PROGRAM.md`.

### gnome41 — [@gnome41](https://github.com/gnome41)
While porting *DBZ Budokai HD*, [gnome41's fork](https://github.com/gnome41/ps3recomp-dbz-budokai-hd)
surfaced and fixed real bugs in the shared SDK that have been incorporated
upstream:
- the AltiVec VX-form **11-bit XO extraction** fix (~3,800 instructions were
  mis-decoded);
- **19 additional VMX integer lifter handlers** + `crorc` / `crandc`;
- **SPU decoder corrections** (RI16 branch forms, channel opcodes per the IBM
  Cell BE manual);
- a `find_functions.py` walrus-syntax fix.

### Lucas Picoli — [@LucasPicoli](https://github.com/LucasPicoli)
- **Linux/GCC build fixes** — glibc's `st_atime`/`st_mtime`/`st_ctime` macros
  were shadowing identically named `CellFsStat` / `CellSaveData` members; five
  translation units now compile clean so downstream game projects get a Linux
  runtime to link against. (v0.6.1)

---

## A note on AI-assisted contributions

Parts of this project — and some contributions to it — were developed with the
help of AI coding tools. That's welcome here: what matters is that every change
is understood, reviewed, and verified by a human before it lands. If you used an
assistant, just say so in your PR (several contributors have) and make sure
you can stand behind the result.
