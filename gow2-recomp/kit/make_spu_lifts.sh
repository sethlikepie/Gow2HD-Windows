#!/usr/bin/env bash
# make_spu_lifts.sh -- regenerate GoW2's seven production SPU lifts
# (spu_lifted/spu{0..6}_v2/spu_recomp.{c,h}) from the user's own EBOOT.ELF.
#
# Usage: make_spu_lifts.sh <EBOOT.ELF> <out_dir> <ps3recomp_dir> [gow2_recomp_dir]
#   out_dir          receives spu0_v2 .. spu6_v2 (each one is replaced as a whole)
#   ps3recomp_dir    the engine: a release snapshot with tools_pinned/<rev>/tools, or a git
#                    clone with history (the pinned lifter revisions are exported from it)
#   gow2_recomp_dir  default: <ps3recomp_dir>/../gow2-recomp (only its versioned
#                    recomp_mid_v2/patch_e427_*.py and patch_spu6_extra_funcs.py are used)
# Env:
#   PY=<python3>     default: <ps3recomp>/.venv/bin/python, else python3
#   RE_PROBES=0      omit the 8 hand-inserted RE trace lines (spu0 x1, spu1 x7) that the
#                    production lifts carry; default 1 = byte-identical to production
#   REF=<dir>        after generating, diff every spuN_v2 against <dir>/spuN_v2
#   IMAGES_DIR=<dir> also write the spu_hit_/spu_miss_ image dumps build_macos.sh reads
#
# Provenance (measured 2026-09-22 against gow2-recomp/spu_lifted, all 7 IDENTICAL):
#   spu0  EBOOT SPU ELF #0 (fp DE6DC3A5EA2BE487)  lifter 5f36a40e + funcs.json merge (0x5C00)
#         + RE probe + E427 + observed import 0x391C (HEAD lifter + HEAD patch_spu_add_entries)
#   spu1  EBOOT SPU ELF #1 (fp 2A5C4E67A14505B8)  lifter 5f36a40e + funcs.json merge (8 entries)
#         + 7 RE probes + E427 + the 7 code destinations of spu1_observed_indirect_targets.lst
#   spu2  EBOOT SPU ELF #2 (fp ABCD0BA4D18DED49)  lifter 5f36a40e --auto-functions + E427
#   spu3  EBOOT SPU ELF #3 (fp ED6A0C318DEB46C6)  lifter 5f36a40e --auto-functions + E427
#   spu4  ELF #1 + 3 guest free-list words (fp 9527C889B1945669)  lifter 11a1c3c5 --auto-functions
#   spu5  ELF #0 + 5 guest free-list words (fp 3512A7E99D34E0FF)  lifter 11a1c3c5 --auto-functions
#   spu6  raw 0x2D00 B at EBOOT vaddr 0x4FD980 (fp CEDB9A67A0C3A305)  HEAD lifter --base 0x3000
#         --functions <42 starts> + patch_spu6_extra_funcs.py source fixups
# Game bytes only ever land in a private temp dir and in <out_dir>; nothing is written to git.
set -euo pipefail

[ $# -ge 3 ] || { sed -n '2,12p' "$0"; exit 2; }
EBOOT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
OUT=$2
PS3=$(cd "$3" && pwd)
GOW2=$(cd "${4:-$PS3/../gow2-recomp}" && pwd)
PY=${PY:-$PS3/.venv/bin/python}
[ -x "$PY" ] || PY=python3
RE_PROBES=${RE_PROBES:-1}
E427="$GOW2/recomp_mid_v2/patch_e427_spu_il_double_sext.py"
SPU6P="$GOW2/recomp_mid_v2/patch_spu6_extra_funcs.py"
for f in "$EBOOT" "$E427" "$SPU6P" "$PS3/tools/spu_lifter.py" "$PS3/tools/patch_spu_add_entries.py"; do
    [ -f "$f" ] || { echo "missing: $f" >&2; exit 2; }
done

REV_OLD=5f36a40e   # spu0..3 base lifts (July lift; == edfad917 output)
REV_45=11a1c3c5    # spu4/5 (heqi/hgti decoded as .word TODO in this window)
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
W=$(mktemp -d "${TMPDIR:-/tmp}/gow2_spu_lifts.XXXXXX")
trap 'rm -rf "$W"' EXIT

echo "== lifter revisions $REV_OLD $REV_45"
for r in $REV_OLD $REV_45; do
    mkdir -p "$W/rev_$r"
    if [ -d "$PS3/tools_pinned/$r/tools" ]; then
        # release kit: the engine snapshot ships the pinned revisions (no git history)
        cp -R "$PS3/tools_pinned/$r/tools" "$W/rev_$r/"
    else
        git -C "$PS3" archive "$r" tools | tar -x -C "$W/rev_$r"
    fi
done
T_OLD="$W/rev_$REV_OLD/tools"; T_45="$W/rev_$REV_45/tools"; T_HEAD="$PS3/tools"

cat > "$W/helper.py" <<'PYEOF'
import json, os, re, struct, subprocess, sys, pathlib, importlib.util

W = pathlib.Path(os.environ["W"])
PY = os.environ["PY"]

def fnv(b):  # runtime/spu/spu_workload.c spu_workload_fingerprint (its basis, verbatim)
    h = 1469598103934665603
    for x in b:
        h = ((h ^ x) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h

def vaddr_to_off(elf, va):
    phoff = struct.unpack_from(">Q", elf, 0x20)[0]; n = struct.unpack_from(">H", elf, 0x38)[0]
    for k in range(n):
        t, _f, off, pva, _pa, fsz = struct.unpack_from(">IIQQQQ", elf, phoff + k * 56)
        if t == 1 and pva <= va < pva + fsz:
            return off + (va - pva)
    raise SystemExit("vaddr 0x%X not in a PT_LOAD" % va)

def off_to_vaddr(elf, off):
    phoff = struct.unpack_from(">Q", elf, 0x20)[0]; n = struct.unpack_from(">H", elf, 0x38)[0]
    for k in range(n):
        t, _f, poff, pva, _pa, fsz = struct.unpack_from(">IIQQQQ", elf, phoff + k * 56)
        if t == 1 and fsz and poff <= off < poff + fsz:
            return pva + (off - poff)
    raise SystemExit("file offset 0x%X not in a PT_LOAD" % off)

def prep(eboot, extracted_dir):
    elf = open(eboot, "rb").read()
    by_fp = {}
    for p in sorted(pathlib.Path(extracted_dir).glob("*.elf")):
        b = p.read_bytes(); by_fp[fnv(b)] = (p, b)
    want = {0: 0xDE6DC3A5EA2BE487, 1: 0x2A5C4E67A14505B8, 2: 0xABCD0BA4D18DED49, 3: 0xED6A0C318DEB46C6}
    imgs = {}
    for n, fp in want.items():
        if fp not in by_fp:
            raise SystemExit("EBOOT has no SPU ELF with fp 0x%016X (spu%d) -- different EBOOT revision?" % (fp, n))
        imgs[n] = by_fp[fp][1]
    # spu4/spu5: the in-memory copies the runtime fingerprinted are ELF #1/#0 with one word every
    # 0x2F80 B overwritten by a guest free-list (word at vaddr v holds v + 0x2F80), chain
    # 0x4D8F00 .. 0x4EDB80. Rebuild them from the pristine ELFs.
    def overlay(n):
        b = bytearray(imgs[n])
        img_va = off_to_vaddr(elf, elf.find(imgs[n]))
        for k in range(8):
            v = 0x4D8F00 + k * 0x2F80
            o = v - img_va
            if 0 <= o <= len(b) - 4:
                struct.pack_into(">I", b, o, v + 0x2F80)
        return bytes(b)
    imgs[4] = overlay(1); imgs[5] = overlay(0)
    o6 = vaddr_to_off(elf, 0x4FD980); imgs[6] = elf[o6:o6 + 0x2D00]
    want.update({4: 0x9527C889B1945669, 5: 0x3512A7E99D34E0FF, 6: 0xCEDB9A67A0C3A305})
    for n, b in imgs.items():
        if fnv(b) != want[n]:
            raise SystemExit("spu%d image fp 0x%016X != expected 0x%016X" % (n, fnv(b), want[n]))
        (W / ("img%d.bin" % n)).write_bytes(b)
        print("  spu%d image %6d B fp 0x%016X ok" % (n, len(b), want[n]))

PROBES = [
 (0, 'void spu0_spu_func_000030B8(spu_context* ctx) {', '        { static int n = 0; if (n < 16) { n++; extern int spu_dbg_log(const char* fmt, unsigned a, unsigned b); spu_dbg_log("[SPU0ASSERT] a=0x%08X b=0x%08X\\n", ctx->gpr[3]._u32[0], ctx->gpr[4]._u32[0]); } }'),
 (1, 'void spu1_spu_func_000030A8(spu_context* ctx) {', '        { static int n = 0; if (n < 12) { n++; extern int spu_dbg_log(const char* fmt, unsigned a, unsigned b); spu_dbg_log("[EDGEASSERT] a=0x%08X b=0x%08X\\n", ctx->gpr[3]._u32[0], ctx->gpr[4]._u32[0]); } }'),
 (1, '        ctx->gpr[0] = spu_splat_u32(0x3188); spu1_spu_func_000094B0(ctx); SPU_DRAIN(ctx);', '        { static int n = 0; if (n < 12) { n++; extern int spu_dbg_log(const char* fmt, unsigned a, unsigned b); spu_dbg_log("[EDGEATT1] a=0x%08X b=0x%08X\\n", ctx->gpr[90]._u32[1], ctx->gpr[3]._u32[0]); } }'),
 (1, '        ctx->gpr[0] = spu_splat_u32(0x3198); spu1_spu_func_00009398(ctx); SPU_DRAIN(ctx);', '        { static int n = 0; if (n < 12) { n++; extern int spu_dbg_log(const char* fmt, unsigned a, unsigned b); spu_dbg_log("[EDGEATT2] a=0x%08X b=0x%08X\\n", ctx->gpr[3]._u32[0], 0); } }'),
 (1, 'void spu1_spu_func_00003220(spu_context* ctx) {', '        { static int n = 0; if (n < 12) { n++; extern int spu_dbg_log(const char* fmt, unsigned a, unsigned b); spu_dbg_log("[EDGEWORK] a=0x%08X b=0x%08X\\n", ctx->gpr[89]._u32[0], ctx->gpr[85]._u32[0]); } }'),
 (1, '        ctx->gpr[0] = spu_splat_u32(0x32AC); spu1_spu_func_00008ED0(ctx); SPU_DRAIN(ctx);', '        { static int n = 0; if (n < 12) { n++; extern int spu_dbg_log(const char* fmt, unsigned a, unsigned b); spu_dbg_log("[EDGEPOLL] a=0x%08X b=0x%08X\\n", ctx->gpr[3]._u32[0], 0); } }'),
 (1, 'void spu1_spu_func_00009658(spu_context* ctx) {', '        { static int n = 0; if (n < 12) { n++; extern int spu_dbg_log(const char* fmt, unsigned a, unsigned b); spu_dbg_log("[ATTERR] a=0x%08X b=0x%08X\\n", ctx->gpr[7]._u32[0], 0); } }'),
 (1, 'void spu1_spu_func_0000A8C0(spu_context* ctx) {', '        { static int n = 0; if (n < 12) { n++; extern int spu_dbg_log(const char* fmt, unsigned a, unsigned b); spu_dbg_log("[EDGEEXIT] a=0x%08X b=0x%08X\\n", ctx->gpr[3]._u32[0], 0); } }'),
]

def probes(n, d):
    p = pathlib.Path(d) / "spu_recomp.c"; lines = p.read_text().split("\n")
    for m, anchor, probe in PROBES:
        if m != n or probe in lines:
            continue
        idx = [i for i, l in enumerate(lines) if l == anchor]
        if len(idx) != 1:
            raise SystemExit("probe anchor count %d for %r" % (len(idx), anchor))
        lines.insert(idx[0] + 1, probe)
    p.write_text("\n".join(lines))

def lift(tools, out, *args):
    r = subprocess.run([PY, str(pathlib.Path(tools) / "spu_lifter.py"), *args, "-o", str(out)],
                       capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr); raise SystemExit("lift failed: %s" % " ".join(args))

def add_entries(d, fresh_c, prefix, targets):
    r = subprocess.run([PY, os.environ["T_HEAD"] + "/patch_spu_add_entries.py", str(d), str(fresh_c),
                        prefix, targets], capture_output=True, text=True)
    print("   ", r.stdout.strip()[:160])
    if r.returncode:
        sys.stderr.write(r.stderr); raise SystemExit("patch_spu_add_entries failed")

def promote(n, d, steps):
    """Replay the observed-target promotions in their historical order. Each step lifts a fresh
    image with the HEAD lifter (--extra-funcs = the cumulative, sorted manifest -- the sorted
    set is what --observed-indirect-targets produced) and imports the cumulative list."""
    cum = []
    for i, step in enumerate(steps):
        if step == "NORM":  # 4324abb: targets normalized with & ~3 -> odd entries leave the set
            cum = [t for t in cum if int(t, 16) & 3 == 0]; continue
        tg, _, fresh_set = step.partition("@")
        cum += tg.split(",")
        fset = (fresh_set.split(",") + tg.split(",")) if fresh_set else cum
        extra = ",".join("0x%X" % v for v in sorted({int(t, 16) for t in fset}))
        f = W / ("fresh%d_%02d" % (n, i))
        lift(os.environ["T_HEAD"], f, "--auto-functions", str(W / ("img%d.bin" % n)),
             "--extra-funcs", extra, "--symbol-prefix", "spu%d_" % n)
        add_entries(d, f / "spu_recomp.c", "spu%d_" % n, ",".join(cum))

def spu6(out):
    spec = importlib.util.spec_from_file_location("p6", os.environ["SPU6P"])
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    starts = [0x3000, 0x3090, 0x3C00, 0x3CE0, 0x4178, 0x4920, 0x4930, 0x4960, 0x4A18, 0x4BD0,
              0x4C60, 0x4C64, 0x4C68, 0x4C98, 0x4CC8, 0x4D20, 0x4E08, 0x4EC4, 0x4EF0, 0x4F30,
              0x4F98, 0x5028, 0x5120, 0x51F0, 0x5258, 0x5260, 0x5368, 0x53B8, 0x53F8, 0x5478,
              0x54E0, 0x5530, 0x5638, 0x57C8, 0x5868, 0x5920, 0x5B68, 0x5B78, 0x5B88, 0x5BA0,
              0x5BE0, 0x5C70]
    m.INPUT = W / "img6.bin"; m.OUTPUT = pathlib.Path(out)
    m.LIFTER = pathlib.Path(os.environ["T_HEAD"]) / "spu_lifter.py"
    m.existing_starts = lambda _p: starts   # the script bootstraps from its own previous output
    m.sys.executable = PY
    if m.main():
        raise SystemExit("spu6 relift failed")

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "prep": prep(sys.argv[2], sys.argv[3])
    elif cmd == "probes": probes(int(sys.argv[2]), sys.argv[3])
    elif cmd == "promote": promote(int(sys.argv[2]), sys.argv[3], sys.argv[4:])
    elif cmd == "spu6": spu6(sys.argv[2])
PYEOF
export W PY T_HEAD SPU6P
H() { "$PY" "$W/helper.py" "$@"; }
L() { local t=$1 o=$2; shift 2; "$PY" "$t/spu_lifter.py" "$@" -o "$o" > "$o.log" 2>&1 || { cat "$o.log" >&2; exit 1; }; }

echo "== SPU images from $EBOOT"
"$PY" "$T_HEAD/extract_spu_images.py" "$EBOOT" -o "$W/extracted" > /dev/null
H prep "$EBOOT" "$W/extracted"

# Manual function-pointer-only boundaries (gow2-recomp recomp_mid_v2/spu{0,1}_funcs.json: these are
# the only entries of those files that the 5f36a40e detector does not already find).
echo '[{"start":"0x5c00","end":"0x5c10"}]' > "$W/f0.json"
echo '[{"start":"0x3128","end":"0x3174"},{"start":"0x4070","end":"0x4078"},{"start":"0x4078","end":"0x4094"},{"start":"0x409c","end":"0x40a4"},{"start":"0x40a4","end":"0x40ac"},{"start":"0x40ac","end":"0x40bc"},{"start":"0x9300","end":"0x937c"},{"start":"0x96f8","end":"0x9728"}]' > "$W/f1.json"

B="$W/build"; mkdir -p "$B"
echo "== spu0"
L "$T_OLD" "$B/spu0_v2" --auto-functions "$W/img0.bin" --functions "$W/f0.json" --symbol-prefix spu0_
[ "$RE_PROBES" = 1 ] && H probes 0 "$B/spu0_v2"
"$PY" "$E427" "$B/spu0_v2/spu_recomp.c" > /dev/null
H promote 0 "$B/spu0_v2" 391C          # == patch_spu0_observed_entries.py with its manifest

echo "== spu1"
L "$T_OLD" "$B/spu1_v2" --auto-functions "$W/img1.bin" --functions "$W/f1.json" --symbol-prefix spu1_
[ "$RE_PROBES" = 1 ] && H probes 1 "$B/spu1_v2"
"$PY" "$E427" "$B/spu1_v2/spu_recomp.c" > /dev/null
# Promotion order of recomp_mid_v2/spu1_observed_indirect_targets.lst (2026-09-22): the code
# destinations of the 2026-09-20 A/B runs, without 0x6074 (the -3 error exit whose cleanup
# writes into the PPU code segment) and without the ten destinations outside the image code.
H promote 1 "$B/spu1_v2" 391C 6088 64E0 56AC 3318 3410 ABA0

# spu2 is the SCREAM mixer job the policy module loads at LS 0x4000 and calls:
# lifted with the current lifter (brsl/bisl as real calls, interrupt windows),
# plus its entry and the indirect targets it takes.
echo "== spu2"
L "$T_HEAD" "$B/spu2_v2" --auto-functions "$W/img2.bin" --symbol-prefix spu2_ \
    --extra-funcs 0x4080,0x92F8,0x91C0
echo "== spu3"
L "$T_OLD" "$B/spu3_v2" --auto-functions "$W/img3.bin" --symbol-prefix spu3_
"$PY" "$E427" "$B/spu3_v2/spu_recomp.c" > /dev/null
for n in 4 5; do
    echo "== spu$n"
    L "$T_45" "$B/spu${n}_v2" --auto-functions "$W/img$n.bin" --symbol-prefix spu${n}_
done
echo "== spu6"
mkdir -p "$B/spu6_v2"; "$PY" "$SPU6P" "$W/img6.bin" "$B/spu6_v2" > "$B/spu6_v2.log"
# brsl/bih* halfword conditions of the lifts made by 2026-07-21..09-23 lifters.
"$PY" "$GOW2/recomp_mid_v2/patch_spu_halfword_cond.py" "$B"/spu?_v2 > /dev/null

for n in 0 1 2 3 4 5 6; do
    rm -rf "$OUT/spu${n}_v2"; mkdir -p "$OUT/spu${n}_v2"
    cp "$B/spu${n}_v2/spu_recomp.c" "$B/spu${n}_v2/spu_recomp.h" "$OUT/spu${n}_v2/"
done
echo "== wrote $OUT/spu{0..6}_v2"

# build_macos.sh step 3b re-verifies spu0/spu1/spu6 against the runtime's image
# dumps (spu_hit_<fp>.bin / spu_miss_<fp>.bin in the gow2-recomp root). They are
# the same bytes as these images, so a kit build writes them here instead.
if [ -n "${IMAGES_DIR:-}" ]; then
    cp "$W/img0.bin" "$IMAGES_DIR/spu_hit_de6dc3a5ea2be487.bin"
    cp "$W/img1.bin" "$IMAGES_DIR/spu_hit_2a5c4e67a14505b8.bin"
    cp "$W/img6.bin" "$IMAGES_DIR/spu_miss_cedb9a67a0c3a305.bin"
    echo "== wrote SPU image dumps to $IMAGES_DIR"
fi

if [ -n "${REF:-}" ]; then
    rc=0
    for n in 0 1 2 3 4 5 6; do
        for f in spu_recomp.c spu_recomp.h; do
            if cmp -s "$REF/spu${n}_v2/$f" "$OUT/spu${n}_v2/$f"; then r=IDENTICAL
            else r="DIFF ($(diff "$REF/spu${n}_v2/$f" "$OUT/spu${n}_v2/$f" | grep -c '^[<>]') lines)"; rc=1; fi
            printf '  spu%d_v2/%-13s %s\n' "$n" "$f" "$r"
        done
    done
    exit $rc
fi
