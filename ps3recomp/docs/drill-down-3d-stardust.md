# 3D dissection — Super Stardust HD (NPEA00014)

A deeper look at a genuinely **3D** PSN title (Housemarque's full-3D arcade
shooter, famous for massive SPU-driven particle physics), to contrast with the
2D arcade hubs and native indies from the [PSN drill-down](drill-down-psn.md).
Decrypted via the same crack-pkg + RPCS3 path; EBOOT.elf is **8.0 MB** (5× the
2D titles).

## Profile

| | Super Stardust HD (3D) | Gunstar / Sonic (2D hub) |
|---|---|---|
| EBOOT.elf | 8.0 MB | 1.6 MB |
| image / loadable | PPC64 EXEC @ 0x10000, 9.0 MB | 2.2 MB |
| **PPU functions** (IDA-seeded) | **18,426** | ~5,000 |
| **PPU instructions** | **1,967,386** | 375,474 |
| **embedded SPU images** | **27** (572 KB) | ~0 |

It's a different *kind* of program: ~5× the code, a real renderer + a fleet of
SPU jobs, where the 2D titles are an emulator core behind a PSN front-end.

## What makes it "3D" in the binary

**1. The import profile flips.** `cellGcmSys` is the #1 import and there is no
`cellSail` (the arcade-hub AV-menu lib that topped the 2D titles):

```
cellGcmSys(27)  sceNp(26)  cellSpurs(19)  cellSysutil(17)  sysPrxForUser(16)
cellResc(15)    sys_net(14) sys_io(12)    cellHttp(10)     sceNpTrophy(9)  ...
```

- **`cellGcmSys(27)`** — heavy GCM/RSX setup (render targets, shaders, textures,
  depth, tiling) instead of the 2D titles' single fullscreen blit.
- **`cellSpurs(19)`** — the SPURS job system is front-and-centre (vs ~9 for 2D).
- **`cellResc(15)`** — RSX resolution/scaling conversion, prominent here (the 2D
  titles barely touch it); Super Stardust HD renders high-res and rescales.

**2. It offloads massively to the SPUs.** 27 embedded SPU images (572 KB total,
largest 44 KB) — particle simulation, physics, culling, vertex/audio DSP. The 2D
arcade titles ship essentially none. **A faithful recomp must drive the SPU path
(spu_lifter) for all 27 images** — a whole workload the 2D titles don't have.

**3. The math density is 4–5× higher.** Counting AltiVec/VMX and FP across the
PPU text:

| | VMX | FP |
|---|---:|---:|
| Super Stardust HD (3D) | **20,145 (1.02%)** | 139,228 (7.08%) |
| Gunstar (2D) | 857 (0.23%) | 7,831 (2.09%) |

20,145 VMX instructions (3D transforms, SIMD particle math) vs a couple hundred.
**This is exactly where this session's VMX decode/lifter corrections matter** —
a 2D title with ~850 VMX ops barely exercises them; a 3D title with 20k would be
pervasively wrong without correct VMX decode + handlers.

## Recomp implications (vs the 2D class)

- **SPU is mandatory, not optional.** 27 SPU programs to lift; the 2D titles
  could (almost) ignore SPU.
- **VMX/FP correctness is load-bearing.** The high VMX/FP density means
  decode/lift bugs there are pervasive, not rare.
- **RSX backend + RESC** carry real weight (3D pipeline + rescale), where the 2D
  titles need little more than a blit.
- **Bigger everything** — 18k functions / ~2M instructions means longer lifts,
  more chunks, and more of the function-pointer/OPD CRT machinery to get right.

So on the recomp-suitability scale, a 3D SPU-heavy title like this sits well
above the arcade-hub class: more PPU code, a full RSX path, and a hard
requirement on the SPU recompiler — but it's also the case that most directly
validates the toolkit's vector and SPU work.

## Tooling note
The 27 SPU images extract cleanly as SPU ELFs, but `find_spu_functions` /
`spu_disasm` run directly on them found 0 functions (decoded the ELF header as
code) — the SPU-lift pipeline needs a parse/unwrap step for this format. Worth a
fix before tackling an SPU-heavy 3D port.
