"""kitlift.py -- steps 4-5 of the kit's gow2-recomp/kit/setup.sh (PPU + SPU lifts) in Python, for Windows.

Same commands, arguments and order as the kit's bash scripts (kit/setup.sh, apply_all_patches.sh,
kit/make_spu_lifts.sh), so the output is byte-identical to the hashes in gow2-recomp/kit/*.sha256. What the
bash versions got from Linux is reproduced here:
  * LF line endings and UTF-8 in every Python child   -> pyfix/sitecustomize.py + PYTHONUTF8=1 (see pyenv.py)
  * the patch list sorted in byte order (LC_ALL=C)    -> sorted() on the names
  * no git revision stamp from the lifter             -> GIT_DIR points at a folder that does not exist
  * `patch -p1` for kit/ppu_lift_delta.patch           -> udiff.py
"""
import glob
import hashlib
import os
import re
import shutil
import subprocess
import tempfile

from . import udiff

EBOOT_SHA = "23cfd435be284adb83745c3e1bcad7b7660782470f71255cb74a1d2be8b1163b"
PPU_LIFTER_REV = "5b004fc7"
REV_OLD, REV_45 = "5f36a40e", "11a1c3c5"


class LiftError(Exception):
    pass


def sha256_file(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def read_sha_list(p):
    out = []
    for l in open(p, encoding="utf-8"):
        l = l.strip()
        if l:
            h, name = l.split(None, 1)
            out.append((h, name.lstrip("*")))
    return out


def verify_list(listfile, root, require_all=True):
    """sha256sum -c [--ignore-missing]; returns the names that differ (or are missing when require_all)."""
    bad = []
    for h, name in read_sha_list(listfile):
        p = os.path.join(root, name)
        if not os.path.isfile(p):
            if require_all:
                bad.append(name)
            continue
        if sha256_file(p) != h:
            bad.append(name)
    return bad


def _run(py, args, env, cwd, log=None, stdin=None):
    r = subprocess.run([py] + [str(a) for a in args], env=env, cwd=str(cwd), input=stdin,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = r.stdout.decode("utf-8", "replace")
    if log is not None:
        with open(log, "a", encoding="utf-8", newline="\n") as f:
            f.write("$ %s\n%s\n" % (" ".join(str(a) for a in args), out))
    return r.returncode, out


# ---------------------------------------------------------------------------------------------- PPU
def lift_ppu(kit, py, env, jobs, log, progress=lambda m: None):
    here = os.path.join(kit, "gow2-recomp")
    engine = os.path.join(kit, "ps3recomp")
    kdir = os.path.join(here, "kit")
    lift = os.path.join(here, "recomp_macos_e435")
    t = tempfile.mkdtemp(prefix="gow2_ppu_")
    try:
        shutil.copytree(os.path.join(engine, "tools_pinned", PPU_LIFTER_REV, "tools"), os.path.join(t, "tools"))
        progress("PPU lifter %s" % PPU_LIFTER_REV)
        rc, out = _run(py, [os.path.join(t, "tools", "ppu_lifter.py"), os.path.join(here, "EBOOT.ELF"),
                            "--functions", os.path.join(here, "functions.json"),
                            "--config", os.path.join(here, "config", "gow2_recomp.toml"),
                            "-o", os.path.join(t, "lift"), "-j", str(jobs)], env, here, log)
        if rc:
            raise LiftError("PPU lifter failed:\n" + "\n".join(out.splitlines()[-20:]))
        for p in sorted(glob.glob(os.path.join(t, "lift", "ppu_recomp_00?.cpp"))):
            s = open(p, "rb").read()
            s = b"\n".join(l.replace(b"/* lifter-rev: unknown */", b"/* lifter-rev: %s */" % PPU_LIFTER_REV.encode(), 1)
                           for l in s.split(b"\n"))
            open(p, "wb").write(s)
        w = os.path.join(here, ".kit_lift")
        shutil.rmtree(w, ignore_errors=True)
        shutil.move(os.path.join(t, "lift"), w)
        progress("PPU patch scripts")
        apply_all_patches(here, engine, w, py, env, log)
        progress("PPU kit delta")
        udiff.apply(os.path.join(kdir, "ppu_lift_delta.patch"), w, strip=1)
        bad = verify_list(os.path.join(kdir, "ppu_lift.sha256"), w, require_all=False)
        if bad:
            raise LiftError("PPU lift does not match kit/ppu_lift.sha256: " + ", ".join(bad))
        shutil.rmtree(lift, ignore_errors=True)
        os.makedirs(lift)
        for p in [os.path.join(w, "ppu_recomp.h")] + sorted(glob.glob(os.path.join(w, "ppu_recomp_00?.cpp"))):
            shutil.copy2(p, lift)
        if os.path.isdir(os.path.join(w, "gen")):
            shutil.copytree(os.path.join(w, "gen"), os.path.join(lift, "gen"))
        shutil.rmtree(w, ignore_errors=True)
    finally:
        shutil.rmtree(t, ignore_errors=True)


def apply_all_patches(here, engine, lift, py, env, log):
    """gow2-recomp/apply_all_patches.sh (apply mode): every recomp_mid_v2/patch_*.py on the lift, in the order
    of games/gow2/lift_baseline/PATCH_DEPS.tsv. A failing patch is logged and the run goes on, as in bash;
    the kit delta + sha256 check afterwards decide."""
    pdir = os.path.join(here, "recomp_mid_v2")
    names = sorted(os.path.basename(p) for p in glob.glob(os.path.join(pdir, "patch_*.py")))
    deps = os.path.join(engine, "games", "gow2", "lift_baseline", "PATCH_DEPS.tsv")
    order = os.path.join(engine, "games", "gow2", "lift_baseline", "order_patches.py")
    rc, out = _run(py, [order, "--deps", deps], env, here, None, stdin=("\n".join(names) + "\n").encode())
    if rc:
        raise LiftError("order_patches.py failed: " + out[-500:])
    ordered = [l.strip() for l in out.splitlines() if l.strip()]
    if sorted(ordered) != names:
        raise LiftError("order_patches.py returned a different patch set")
    penv = dict(env, PYTHON=py)
    failed = []
    for n in ordered:
        rc, _ = _run(py, [os.path.join(pdir, n), lift], penv, here, log)
        if rc:
            failed.append(n)
    with open(log, "a", encoding="utf-8") as f:
        f.write("apply_all_patches: %d patches, rc!=0: %s\n" % (len(ordered), " ".join(failed) or "none"))


# ---------------------------------------------------------------------------------------------- SPU
def _helper_source(make_spu_lifts):
    s = open(make_spu_lifts, encoding="utf-8").read()
    a = s.index("<<'PYEOF'\n") + len("<<'PYEOF'\n")
    b = s.index("\nPYEOF\n", a)
    return s[a:b + 1]


def lift_spu(kit, py, env, log, progress=lambda m: None):
    """gow2-recomp/kit/make_spu_lifts.sh <EBOOT.ELF> spu_lifted <ps3recomp> <gow2-recomp> with IMAGES_DIR set."""
    here = os.path.join(kit, "gow2-recomp")
    ps3 = os.path.join(kit, "ps3recomp")
    eboot = os.path.join(here, "EBOOT.ELF")
    out_dir = os.path.join(here, "spu_lifted")
    e427 = os.path.join(here, "recomp_mid_v2", "patch_e427_spu_il_double_sext.py")
    spu6p = os.path.join(here, "recomp_mid_v2", "patch_spu6_extra_funcs.py")
    t_head = os.path.join(ps3, "tools")
    w = tempfile.mkdtemp(prefix="gow2_spu_lifts_")
    try:
        for r in (REV_OLD, REV_45):
            shutil.copytree(os.path.join(ps3, "tools_pinned", r, "tools"), os.path.join(w, "rev_" + r, "tools"))
        t_old = os.path.join(w, "rev_" + REV_OLD, "tools")
        t_45 = os.path.join(w, "rev_" + REV_45, "tools")
        with open(os.path.join(w, "helper.py"), "w", encoding="utf-8", newline="\n") as f:
            f.write(_helper_source(os.path.join(here, "kit", "make_spu_lifts.sh")))
        henv = dict(env, W=w, PY=py, T_HEAD=t_head, SPU6P=spu6p)

        def H(*a):
            rc, out = _run(py, [os.path.join(w, "helper.py")] + list(a), henv, here, log)
            if rc:
                raise LiftError("SPU helper %s failed:\n%s" % (a[0], out[-1500:]))

        def L(tools, o, *a):
            rc, out = _run(py, [os.path.join(tools, "spu_lifter.py")] + list(a) + ["-o", o], henv, here, log)
            if rc:
                raise LiftError("spu_lifter failed (%s):\n%s" % (os.path.basename(o), out[-1500:]))

        def P(*a):
            rc, out = _run(py, list(a), henv, here, log)
            if rc:
                raise LiftError("%s failed:\n%s" % (os.path.basename(a[0]), out[-1500:]))

        progress("SPU images")
        P(os.path.join(t_head, "extract_spu_images.py"), eboot, "-o", os.path.join(w, "extracted"))
        H("prep", eboot, os.path.join(w, "extracted"))
        with open(os.path.join(w, "f0.json"), "w", newline="\n") as f:
            f.write('[{"start":"0x5c00","end":"0x5c10"}]\n')
        with open(os.path.join(w, "f1.json"), "w", newline="\n") as f:
            f.write('[{"start":"0x3128","end":"0x3174"},{"start":"0x4070","end":"0x4078"},{"start":"0x4078","end":"0x4094"},'
                    '{"start":"0x409c","end":"0x40a4"},{"start":"0x40a4","end":"0x40ac"},{"start":"0x40ac","end":"0x40bc"},'
                    '{"start":"0x9300","end":"0x937c"},{"start":"0x96f8","end":"0x9728"}]\n')
        b = os.path.join(w, "build")
        os.makedirs(b)
        img = lambda n: os.path.join(w, "img%d.bin" % n)
        progress("SPU lift spu0")
        L(t_old, os.path.join(b, "spu0_v2"), "--auto-functions", img(0), "--functions", os.path.join(w, "f0.json"),
          "--symbol-prefix", "spu0_")
        H("probes", "0", os.path.join(b, "spu0_v2"))
        P(e427, os.path.join(b, "spu0_v2", "spu_recomp.c"))
        H("promote", "0", os.path.join(b, "spu0_v2"), "391C")
        progress("SPU lift spu1")
        L(t_old, os.path.join(b, "spu1_v2"), "--auto-functions", img(1), "--functions", os.path.join(w, "f1.json"),
          "--symbol-prefix", "spu1_")
        H("probes", "1", os.path.join(b, "spu1_v2"))
        P(e427, os.path.join(b, "spu1_v2", "spu_recomp.c"))
        H("promote", "1", os.path.join(b, "spu1_v2"), "391C", "6088", "64E0", "56AC", "3318", "3410", "ABA0")
        progress("SPU lift spu2-6")
        L(t_head, os.path.join(b, "spu2_v2"), "--auto-functions", img(2), "--symbol-prefix", "spu2_",
          "--extra-funcs", "0x4080,0x92F8,0x91C0")
        L(t_old, os.path.join(b, "spu3_v2"), "--auto-functions", img(3), "--symbol-prefix", "spu3_")
        P(e427, os.path.join(b, "spu3_v2", "spu_recomp.c"))
        for n in (4, 5):
            L(t_45, os.path.join(b, "spu%d_v2" % n), "--auto-functions", img(n), "--symbol-prefix", "spu%d_" % n)
        os.makedirs(os.path.join(b, "spu6_v2"))
        P(spu6p, img(6), os.path.join(b, "spu6_v2"))
        P(os.path.join(here, "recomp_mid_v2", "patch_spu_halfword_cond.py"),
          *sorted(glob.glob(os.path.join(b, "spu?_v2"))))
        for n in range(7):
            d = os.path.join(out_dir, "spu%d_v2" % n)
            shutil.rmtree(d, ignore_errors=True)
            os.makedirs(d)
            for f in ("spu_recomp.c", "spu_recomp.h"):
                shutil.copy2(os.path.join(b, "spu%d_v2" % n, f), d)
        shutil.copy2(img(0), os.path.join(here, "spu_hit_de6dc3a5ea2be487.bin"))
        shutil.copy2(img(1), os.path.join(here, "spu_hit_2a5c4e67a14505b8.bin"))
        shutil.copy2(img(6), os.path.join(here, "spu_miss_cedb9a67a0c3a305.bin"))
    finally:
        shutil.rmtree(w, ignore_errors=True)
    bad = verify_list(os.path.join(here, "kit", "spu_lift.sha256"), out_dir, require_all=True)
    if bad:
        raise LiftError("SPU lifts do not match kit/spu_lift.sha256: " + ", ".join(bad))


def spu_lift_ok(kit):
    here = os.path.join(kit, "gow2-recomp")
    return (not verify_list(os.path.join(here, "kit", "spu_lift.sha256"), os.path.join(here, "spu_lifted"))
            and all(os.path.isfile(os.path.join(here, f)) for f in
                    ("spu_hit_de6dc3a5ea2be487.bin", "spu_hit_2a5c4e67a14505b8.bin", "spu_miss_cedb9a67a0c3a305.bin")))
