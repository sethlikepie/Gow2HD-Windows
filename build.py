#!/usr/bin/env python3
"""build.py -- build God of War II HD (PS3, NPUA80491 v01.00) as a native Windows program from YOUR copy of the game.

    python build.py --game <PS3_GAME dir> --rap <.rap file> [--elf <decrypted EBOOT.ELF>]
                    --tools <dir with toolchain> --out <install dir> [--jobs N]
    python build.py --pkg <PSN .pkg> --rap <.rap file> --tools <dir> --out <install dir> [--jobs N]
    python build.py --print-tools

--game   the game's folder with USRDIR/EBOOT.BIN and USRDIR/gow2.psarc (RPCS3: dev_hdd0/game/NPUA80491)
--pkg    instead of --game: the PSN package UP9000-NPUA80491_00-GODOFWARIIHDUS00.pkg (any file name). The files the
         build and the game need (PARAM.SFO, ICON0.PNG, USRDIR/*) are extracted into <out>/game/NPUA80491, which is
         then the game folder (the game reads gow2.psarc from there at run time, so it stays). Restartable.
--rap    your licence UP9000-NPUA80491_00-GODOFWARIIHDUS00.rap (RPCS3: dev_hdd0/home/<user>/exdata)
--elf    an EBOOT.ELF you already decrypted (then --rap is not needed)
--tools  folder holding the downloads listed by --print-tools, each extracted into its "extract-to" subfolder
--out    install folder: the game exe, EBOOT.ELF, movie_cache, ffmpeg, gow2_port.ini (the game folder path);
         build intermediates go to <out>/.build (needed to resume or rebuild quickly; safe to delete afterwards)
--jobs   parallel compile jobs (default: number of CPUs)

Output protocol (stdout): `PROGRESS <0-100> <message>` lines, on success a final `EXE <absolute exe path>` line, and
on failure one `ERROR <message>` line;
exit code 0 on success. Details go to <out>/.build/build.log. Finished steps are skipped when the script is run
again (stamp files in <out>/.build/stamps). Needs only Python >= 3.11 (the embeddable CPython is fine) and the
tools from --print-tools: no Git Bash, cmake, ninja or Python packages. Writes only to --out, --tools and temp.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
REPO = os.path.dirname(os.path.abspath(__file__))
WINPORT = os.path.join(REPO, "winport")
sys.path.insert(0, WINPORT)

EXE_NAME = "gow2hd.exe"
ELF_SHA = "23cfd435be284adb83745c3e1bcad7b7660782470f71255cb74a1d2be8b1163b"   # NPUA80491 v01.00 (kit/setup.sh)
CONTENT_ID = "UP9000-NPUA80491_00-GODOFWARIIHDUS00"

TOOLS = [
    {"name": "llvm-mingw",
     "version": "20260922 ucrt x86_64",
     "url": "https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-x86_64.zip",
     "sha256": "e3ad77d117a4bea19a7a3b333341824d79a5a371004a10e25b8504e7b3047666",
     "size": 190725905,
     "extract-to": "llvm-mingw"},
    {"name": "ffmpeg",
     "version": "N-127021-ge0c94b2d1c win64 LGPL shared (BtbN autobuild-2026-09-30-13-08)",
     "url": "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-09-30-13-08/"
            "ffmpeg-N-127021-ge0c94b2d1c-win64-lgpl-shared.zip",
     "sha256": "2cc3dd0baf74ebd68a3fceaf771df8351a7a6d69694d05f43b16bb0c966ec909",
     "size": 78636802,
     "extract-to": "ffmpeg"},
]

GAME_FILES = {   # USRDIR files of NPUA80491 v01.00: size, sha256
    "EBOOT.BIN": (2586416, "dbe0d79402a32d7cc45df40fd59e8f2ab0cc26c9fd3f3719ca54e68f02d8c120"),
    "gow2.psarc": (6987947360, "1ab2e1330db760ea30368e07d5b930f680e3a77ec4bdbfa61075465b755f6d87"),
}


# ------------------------------------------------------------------------------------------- output
_PSCALE = [0.0, 1.0]   # with --pkg the extraction takes 0..PKG_SHARE % and the other steps are scaled into the rest
PKG_SHARE = 12


def progress(pct, msg, raw=False):
    if not raw:
        pct = _PSCALE[0] + pct * _PSCALE[1]
    sys.stdout.write("PROGRESS %d %s\n" % (max(0, min(100, int(pct))), " ".join(str(msg).split())))
    sys.stdout.flush()
    log("[%5.1f%%] %s" % (pct, msg))


_LOG = [None]


def log(msg):
    if _LOG[0]:
        with open(_LOG[0], "a", encoding="utf-8") as f:
            f.write(time.strftime("%H:%M:%S ") + msg + "\n")


class Fail(Exception):
    pass


def sha256_file(p, chunk=1 << 22):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(chunk), b""):
            h.update(b)
    return h.hexdigest()


def tree_digest(roots, skip_dirs=(".git", "__pycache__")):
    """Content digest of source trees (paths + bytes)."""
    h = hashlib.sha256()
    for root in roots:
        if os.path.isfile(root):
            h.update(os.path.basename(root).encode())
            h.update(open(root, "rb").read())
            continue
        for d, dirs, files in os.walk(root):
            dirs[:] = sorted(x for x in dirs if x not in skip_dirs)
            for fn in sorted(files):
                p = os.path.join(d, fn)
                h.update(os.path.relpath(p, root).replace("\\", "/").encode() + b"\0")
                h.update(open(p, "rb").read())
    return h.hexdigest()


class Stamps:
    def __init__(self, d):
        self.d = d
        os.makedirs(d, exist_ok=True)

    def ok(self, name, key):
        p = os.path.join(self.d, name + ".stamp")
        return os.path.isfile(p) and open(p, encoding="utf-8").read() == key

    def done(self, name, key):
        with open(os.path.join(self.d, name + ".stamp"), "w", encoding="utf-8") as f:
            f.write(key)

    def clear(self, name):
        p = os.path.join(self.d, name + ".stamp")
        if os.path.exists(p):
            os.remove(p)


# ------------------------------------------------------------------------------------------- tools
def find_tool(tools_dir, sub, rel):
    """<tools>/<sub>/<rel>, <tools>/<sub>/*/<rel> or <tools>/<sub>-*/<rel>."""
    import glob
    for pat in (os.path.join(tools_dir, sub, rel), os.path.join(tools_dir, sub, "*", rel),
                os.path.join(tools_dir, sub + "-*", rel)):
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[0]
    return None


# ------------------------------------------------------------------------------------------- steps
def sync_tree(src, dst):
    """Copy src over dst: changed/new files only (fresh mtime so dependants rebuild); generated files kept."""
    n = 0
    for d, dirs, files in os.walk(src):
        dirs[:] = [x for x in dirs if x not in (".git", "__pycache__")]
        rel = os.path.relpath(d, src)
        od = os.path.join(dst, rel) if rel != "." else dst
        os.makedirs(od, exist_ok=True)
        for fn in files:
            s, t = os.path.join(d, fn), os.path.join(od, fn)
            if os.path.isfile(t) and os.path.getsize(t) == os.path.getsize(s) and open(t, "rb").read() == open(s, "rb").read():
                continue
            shutil.copyfile(s, t)
            n += 1
    return n


def copy_if_changed(s, t):
    if os.path.isfile(t) and os.path.getsize(t) == os.path.getsize(s) and sha256_file(t) == sha256_file(s):
        return
    tmp = t + ".tmp"
    shutil.copyfile(s, tmp)
    os.replace(tmp, t)


def extract_pkg(pkg_path, out, stamps):
    """Extract what the build and the game need from the PSN package into <out>/game/NPUA80491; returns that folder.
    Checks the package first (PS3, content id, complete, full game v01.00); each file is streamed via a resumable
    .part, EBOOT.BIN and gow2.psarc are checked against GAME_FILES, and a stamp per file makes reruns instant."""
    from buildlib import pkg as pkgmod
    progress(0, "reading the game package %s" % os.path.basename(pkg_path), raw=True)
    if not os.path.isfile(pkg_path):
        raise Fail("game package not found: " + pkg_path)
    try:
        p = pkgmod.Pkg(pkg_path)
    except pkgmod.PkgError as e:
        raise Fail(str(e))
    name = os.path.basename(pkg_path)
    try:
        if p.content_id != CONTENT_ID:
            raise Fail("%s is the package of %s, not God of War II HD US (%s). Only NPUA80491 v01.00 is supported"
                       % (name, p.content_id or "an unknown title", CONTENT_ID))
        if not p.complete:
            raise Fail("%s is incomplete (%.2f of %.2f GB): the download did not finish, download it again"
                       % (name, p.file_size / 1e9, p.total_size / 1e9))
        sfo_it, psarc_it = p.item("PARAM.SFO"), p.item("USRDIR/gow2.psarc")
        sfo = pkgmod.parse_sfo(p.read_data(sfo_it.offset, sfo_it.size)) if sfo_it else {}
        log("pkg %s: content %s, revision %04X, content type %s, %d items, SFO %s" % (
            pkg_path, p.content_id, p.revision, p.content_type, len(p.items),
            {k: sfo.get(k) for k in ("TITLE_ID", "CATEGORY", "APP_VER", "VERSION", "TITLE")}))
        if sfo.get("CATEGORY") == "GD" or (p.item("USRDIR/EBOOT.BIN") and not psarc_it):
            raise Fail("%s holds only EBOOT.BIN (a game update or a modified EBOOT, v%s), not the full game: use the full "
                       "game package %s.pkg (about 7 GB)" % (name, sfo.get("APP_VER", "?"), CONTENT_ID))
        if not psarc_it or not p.item("USRDIR/EBOOT.BIN") or not sfo_it:
            raise Fail("%s does not hold the game files (PARAM.SFO, USRDIR/EBOOT.BIN, USRDIR/gow2.psarc)" % name)
        if sfo.get("TITLE_ID") != "NPUA80491" or sfo.get("APP_VER") != "01.00":
            raise Fail("%s is %s version %s: only NPUA80491 v01.00 is supported" % (name, sfo.get("TITLE_ID"), sfo.get("APP_VER")))
        for fn, (size, _sha) in GAME_FILES.items():
            if p.item("USRDIR/" + fn).size != size:
                raise Fail("USRDIR/%s in %s has the wrong size: not God of War II HD NPUA80491 v01.00" % (fn, name))

        # PARAM.SFO + ICON0.PNG (the game reads its window title from the game root) and everything in USRDIR
        want = [it for it in p.items if not it.is_dir and
                (it.name in ("PARAM.SFO", "ICON0.PNG") or it.name.startswith("USRDIR/"))]
        dest = os.path.join(out, "game", "NPUA80491")
        need = sum(it.size for it in want if not os.path.isfile(os.path.join(dest, *it.name.split("/"))))
        free = shutil.disk_usage(out).free
        if need and free < need + (1 << 30):
            raise Fail("not enough disk space for the game files from the package: %.1f GB needed in %s, %.1f GB free"
                       % ((need + (1 << 30)) / 1e9, out, free / 1e9))
        pkey = "%s|%d|%d" % (os.path.normcase(pkg_path), p.file_size, int(os.path.getmtime(pkg_path)))
        total = sum(it.size for it in want) or 1
        base = [0]

        for it in want:
            dst = os.path.join(dest, *it.name.split("/"))
            pin = GAME_FILES.get(it.name[7:]) if it.name.startswith("USRDIR/") else None
            skey = "pkgfile_" + it.name.replace("/", "_")
            if os.path.isfile(dst) and os.path.getsize(dst) == it.size:
                fkey = "%s|%d|%s" % (pkey, int(os.path.getmtime(dst)), it.name)
                if stamps.ok(skey, fkey):
                    base[0] += it.size
                    continue
                if pin:   # already there (stamp lost): check it instead of extracting again
                    progress(PKG_SHARE * base[0] / total, "checking %s (sha256)" % it.name, raw=True)
                    if sha256_file(dst) == pin[1]:
                        stamps.done(skey, fkey)
                        stamps.done("game_" + it.name[7:], "%s|%d|%d" % (dst, it.size, int(os.path.getmtime(dst))))
                        base[0] += it.size
                        continue
                os.remove(dst)

            def prog(done, size, secs, nm=it.name):
                progress(PKG_SHARE * (base[0] + done) / total, "extracting %s from the package: %d of %d MB (%.0f MB/s)"
                         % (nm, done >> 20, size >> 20, done / max(secs, 1e-3) / 1e6), raw=True)
            t0 = time.time()
            try:
                sha = p.extract(it, dst, prog if it.size > (64 << 20) else None,
                                want_sha256=pin[1] if pin else None)
            except pkgmod.PkgError as e:
                raise Fail("%s: %s" % (name, e))
            log("pkg: %s %d bytes %.1f s sha256 %s" % (it.name, it.size, time.time() - t0, sha))
            stamps.done(skey, "%s|%d|%s" % (pkey, int(os.path.getmtime(dst)), it.name))
            if pin:   # step 1's check of the game files then needs no second read
                stamps.done("game_" + it.name[7:], "%s|%d|%d" % (dst, os.path.getsize(dst), int(os.path.getmtime(dst))))
            base[0] += it.size
    finally:
        p.close()
    progress(PKG_SHARE, "game files extracted from the package", raw=True)
    return dest


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game")
    ap.add_argument("--pkg")
    ap.add_argument("--rap")
    ap.add_argument("--elf")
    ap.add_argument("--tools")
    ap.add_argument("--out")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--print-tools", action="store_true")
    a = ap.parse_args(argv)

    if a.print_tools:
        print(json.dumps(TOOLS, indent=2))
        return 0
    for k in ("tools", "out"):
        if not getattr(a, k):
            raise Fail("--%s is required (see python build.py --help)" % k)
    if bool(a.game) == bool(a.pkg):
        raise Fail("give either --game (the game folder) or --pkg (the PSN package .pkg), see python build.py --help")
    if not a.rap and not a.elf:
        raise Fail("--rap (your licence %s.rap) or --elf (a decrypted EBOOT.ELF) is required" % CONTENT_ID)
    if sys.version_info < (3, 11):
        raise Fail("Python 3.11 or newer is needed (this is %s)" % sys.version.split()[0])
    if os.name != "nt":
        raise Fail("this build script is for Windows")

    out = os.path.abspath(a.out)
    tools_dir = os.path.abspath(a.tools)
    bdir = os.path.join(out, ".build")
    os.makedirs(bdir, exist_ok=True)
    _LOG[0] = os.path.join(bdir, "build.log")
    log("==== build.py %s | python %s | jobs %d" % (time.strftime("%Y-%m-%d"), sys.version.split()[0], a.jobs))
    stamps = Stamps(os.path.join(bdir, "stamps"))
    t_start = time.time()

    from buildlib import cbuild, kitlift, movies, pyenv, unself

    # ---- 0. PSN package -> <out>/game/NPUA80491 -------------------------------------------------
    if a.pkg:
        game = extract_pkg(os.path.abspath(a.pkg), out, stamps)
        _PSCALE[:] = [PKG_SHARE, (100 - PKG_SHARE) / 100.0]
    else:
        game = os.path.abspath(a.game)

    # ---- 1. inputs ----------------------------------------------------------------------------
    progress(0, "checking the game files")
    usr = os.path.join(game, "USRDIR")
    if not os.path.isdir(usr) and os.path.isdir(os.path.join(game, "PS3_GAME", "USRDIR")):
        game = os.path.join(game, "PS3_GAME")
        usr = os.path.join(game, "USRDIR")
    psarc = os.path.join(usr, "gow2.psarc")
    eboot_bin = os.path.join(usr, "EBOOT.BIN")
    if not os.path.isfile(psarc):
        raise Fail("USRDIR\\gow2.psarc not found in %s (pass the game's PS3_GAME folder)" % game)
    for name, want in GAME_FILES.items():
        p = os.path.join(usr, name)
        if not want or not os.path.isfile(p):
            continue
        size, sha = want
        if os.path.getsize(p) != size:
            raise Fail("%s has the wrong size: this is not God of War II HD NPUA80491 v01.00" % name)
        key = "%s|%d|%d" % (p, os.path.getsize(p), int(os.path.getmtime(p)))
        if not stamps.ok("game_" + name, key):
            progress(1, "verifying %s (sha256)" % name)
            if sha256_file(p) != sha:
                raise Fail("%s does not match NPUA80491 v01.00 (sha256): wrong game version or damaged file" % name)
            stamps.done("game_" + name, key)

    llvm = find_tool(tools_dir, "llvm-mingw", os.path.join("bin", "x86_64-w64-mingw32-clang.exe"))
    if not llvm:
        raise Fail("llvm-mingw not found under %s (see --print-tools)" % tools_dir)
    tc = cbuild.Toolchain(os.path.dirname(os.path.dirname(llvm)))
    ffmpeg = find_tool(tools_dir, "ffmpeg", os.path.join("bin", "ffmpeg.exe"))
    if not ffmpeg:
        raise Fail("ffmpeg not found under %s (see --print-tools)" % tools_dir)

    # ---- 2. build tree (a copy of the sources: the kit's scripts write next to themselves) ------
    progress(2, "preparing the build folder")
    src = os.path.join(bdir, "src")
    for d in ("ps3recomp", "gow2-recomp", "winport"):
        sync_tree(os.path.join(REPO, d), os.path.join(src, d))
    kit = src
    src_here, src_engine, src_wp = (os.path.join(REPO, d) for d in ("gow2-recomp", "ps3recomp", "winport"))   # digests: repo sources, not the build copy (it also holds generated files)
    here = os.path.join(kit, "gow2-recomp")
    engine = os.path.join(kit, "ps3recomp")
    wp = os.path.join(kit, "winport")
    nogit = os.path.join(tempfile.gettempdir(), "gow2_build_no_git_here", "none")
    py, env = pyenv.prepare(wp, tools_dir, nogit)
    log("python for the kit scripts: %s" % py)

    # ---- 3. EBOOT.ELF ---------------------------------------------------------------------------
    elf = os.path.join(here, "EBOOT.ELF")
    if not (os.path.isfile(elf) and sha256_file(elf) == ELF_SHA):
        if a.elf:
            progress(3, "checking the EBOOT.ELF you gave")
            if sha256_file(a.elf) != ELF_SHA:
                raise Fail("%s is not the supported EBOOT.ELF (God of War II HD NPUA80491 v01.00)" % a.elf)
            shutil.copyfile(a.elf, elf)
        else:
            progress(3, "decrypting EBOOT.BIN with your licence")
            if not os.path.isfile(eboot_bin):
                raise Fail("USRDIR\\EBOOT.BIN not found in " + game)
            if not os.path.isfile(a.rap):
                raise Fail("licence file not found: " + a.rap)
            try:
                info = unself.unself(eboot_bin, elf + ".tmp", rap=a.rap,
                                     keys_inc=os.path.join(engine, "tools", "unself", "npdrm_keys.inc"))
            except unself.UnselfError as e:
                raise Fail("decrypting EBOOT.BIN failed: %s (is the .rap the licence for %s?)" % (e, CONTENT_ID))
            log("unself: " + info)
            progress(4, "verifying the decrypted EBOOT.ELF")
            if sha256_file(elf + ".tmp") != ELF_SHA:
                os.remove(elf + ".tmp")
                raise Fail("the decrypted EBOOT.BIN is not the supported version (NPUA80491 v01.00)")
            os.replace(elf + ".tmp", elf)
    progress(5, "EBOOT.ELF verified")

    # ---- 4. PPU lift (+ the port's lift patches) ------------------------------------------------
    lift = os.path.join(here, "recomp_macos_e435")
    lift_key = ELF_SHA + "|" + tree_digest([src_here, os.path.join(src_engine, "tools_pinned"),
                                            os.path.join(src_engine, "games"), os.path.join(src_wp, "lift_patches"),
                                            os.path.join(src_wp, "buildlib", "kitlift.py"),
                                            os.path.join(src_wp, "buildlib", "udiff.py")])

    def lift_files_key():
        import glob
        fs = [os.path.join(lift, "ppu_recomp.h")] + sorted(glob.glob(os.path.join(lift, "ppu_recomp_00?.cpp")))
        if not all(os.path.isfile(f) for f in fs) or len(fs) != 8:
            return None
        return "|".join(sha256_file(f) for f in fs)

    ppu_ok = False
    if stamps.ok("ppu_lift_key", lift_key):
        k = lift_files_key()
        ppu_ok = k is not None and stamps.ok("ppu_lift_files", k)
    if not ppu_ok:
        stamps.clear("exe")
        t0 = time.time()
        kitlift.lift_ppu(kit, py, env, a.jobs, os.path.join(bdir, "lift_ppu.log"),
                         lambda m: progress(7, m))
        progress(18, "port changes to the lifted code")
        r = subprocess.run([py, os.path.join(wp, "lift_patches", "apply_lift_patches.py"), engine],
                                         env=env, cwd=here, stdout=-1, stderr=-2)
        log(r.stdout.decode("utf-8", "replace"))
        if r.returncode:
            raise Fail("lift patches failed: " + r.stdout.decode("utf-8", "replace")[-400:])
        stamps.done("ppu_lift_key", lift_key)
        stamps.done("ppu_lift_files", lift_files_key())
        log("PPU lift %.0f s" % (time.time() - t0))
    progress(20, "PPU code lifted and verified")

    # ---- 5. SPU lifts -----------------------------------------------------------------------------
    if not kitlift.spu_lift_ok(kit):
        stamps.clear("exe")
        t0 = time.time()
        kitlift.lift_spu(kit, py, env, os.path.join(bdir, "lift_spu.log"), lambda m: progress(22, m))
        log("SPU lift %.0f s" % (time.time() - t0))
    progress(28, "SPU programs lifted and verified")

    # ---- 6. SCREAM mixer jobs as native code ------------------------------------------------------
    scr = os.path.join(kit, "scream_lift")
    tgt = os.path.join(wp, "scream", "scream_tgt.bin")   # optional run-time label list (not shipped)
    scr_key = ELF_SHA + "|" + tree_digest([os.path.join(src_wp, "scream"), os.path.join(src_engine, "tools")])
    if not (stamps.ok("scream", scr_key) and os.path.isfile(os.path.join(scr, "scr_jobs.c"))):
        stamps.clear("exe")
        progress(29, "SCREAM audio mixer as native code")
        shutil.rmtree(scr, ignore_errors=True)
        args = [py, os.path.join(wp, "scream", "gen_scream_native.py"), elf, engine, scr]
        if os.path.isfile(tgt):
            args.append(tgt)
        r = subprocess.run(args, env=env, cwd=here, stdout=-1, stderr=-2)
        log(r.stdout.decode("utf-8", "replace"))
        if r.returncode:
            raise Fail("gen_scream_native.py failed: " + r.stdout.decode("utf-8", "replace")[-400:])
        shutil.copyfile(os.path.join(wp, "scream", "scr_native.h"), os.path.join(scr, "scr_native.h"))
        shutil.rmtree(os.path.join(scr, "elf"), ignore_errors=True)
        stamps.done("scream", scr_key)

    # ---- 7. movie cache ---------------------------------------------------------------------------
    cache = os.path.join(out, "movie_cache")
    mv_key = "%d|%d|%s" % (os.path.getsize(psarc), int(os.path.getmtime(psarc)),
                           tree_digest([os.path.join(src_wp, "buildlib", "movies.py"),
                                        os.path.join(src_here, "kit", "movie_cache.sha256")]))
    if not (stamps.ok("movies", mv_key) and os.path.isfile(os.path.join(cache, "_psarc_movies.txt"))):
        t0 = time.time()
        mv = {"n": 0}

        def mprog(m):
            mv["n"] += 1
            progress(30 + min(24, mv["n"] * 24 // 62), m)
        try:
            movies.build_cache(psarc, cache, os.path.join(here, "kit", "movie_cache.sha256"),
                               os.path.join(engine, "tools", "psarc_extract.py"), mprog)
        except ValueError as e:
            raise Fail(str(e))
        stamps.done("movies", mv_key)
        log("movie cache %.0f s" % (time.time() - t0))
    progress(55, "movie cache ready")

    # ---- 8. engine runtime ------------------------------------------------------------------------
    obj = os.path.join(bdir, "obj")
    rt_lib = os.path.join(obj, "libps3recomp_runtime.a")
    rt_key = tree_digest([os.path.join(src_engine, d) for d in ("include", "libs", "runtime", "third_party")] +
                         [os.path.join(src_wp, "buildlib", "cbuild.py")]) + "|" + tc.cc
    if not (stamps.ok("runtime", rt_key) and os.path.isfile(rt_lib)):
        stamps.clear("exe")
        t0 = time.time()
        force = os.path.isfile(os.path.join(bdir, "stamps", "runtime.stamp"))   # sources changed since last build
        cbuild.runtime(tc, engine, os.path.join(obj, "rt"), rt_lib, a.jobs, os.path.join(bdir, "compile.log"), force,
                       lambda d, t: progress(56 + 14 * d // max(1, t), "engine runtime %d/%d" % (d, t)))
        stamps.done("runtime", rt_key)
        log("runtime %.0f s" % (time.time() - t0))
    progress(70, "engine runtime built")

    # ---- 9. game exe ------------------------------------------------------------------------------
    exe_tmp = os.path.join(bdir, EXE_NAME)
    exe_key = "|".join([rt_key, tree_digest([src_wp, os.path.join(src_here, "hooks"), src_here + os.sep + "recomp_mid_v2",
                                             os.path.join(src_engine, "runtime", "ppu")]),
                        open(os.path.join(bdir, "stamps", "ppu_lift_files.stamp"), encoding="utf-8").read(),
                        scr_key])
    if not (stamps.ok("exe", exe_key) and os.path.isfile(exe_tmp)):
        t0 = time.time()
        force = os.path.isfile(os.path.join(bdir, "stamps", "exe_built_once.stamp"))
        steps = {"n": 0}

        def eprog(m, *x):
            steps["n"] += 1
            progress(min(95, 71 + steps["n"] // 2), m)
        cbuild.exe(tc, kit, wp, os.path.join(obj, "exe"), rt_lib, exe_tmp, py, env, a.jobs,
                   os.path.join(bdir, "compile.log"), force, progress=eprog)
        stamps.done("exe", exe_key)
        stamps.done("exe_built_once", "1")
        log("exe %.0f s" % (time.time() - t0))
    progress(96, "game exe built")

    # ---- 10. install ------------------------------------------------------------------------------
    progress(97, "installing")
    copy_if_changed(exe_tmp, os.path.join(out, EXE_NAME))
    copy_if_changed(elf, os.path.join(out, "EBOOT.ELF"))
    installed = [EXE_NAME, "EBOOT.ELF"]   # files of <out> this script owns (the launcher's "Remove build" deletes them)
    fm = os.path.join(wp, "d3d12", "ui_font_metrics.txt")   # numbers only (glyph boxes + spacing); game font in the port UI
    if os.path.isfile(fm):
        copy_if_changed(fm, os.path.join(out, "ui_font_metrics.txt"))
        installed.append("ui_font_metrics.txt")
    fbin = os.path.dirname(ffmpeg)
    for fn in sorted(os.listdir(fbin)):
        if fn.lower().endswith((".exe", ".dll")) and not fn.lower().startswith("ffplay"):
            copy_if_changed(os.path.join(fbin, fn), os.path.join(out, fn))
            installed.append(fn)
    lic = os.path.join(out, "licenses")
    os.makedirs(lic, exist_ok=True)
    for s, t in ((os.path.join(REPO, "LICENSE"), "LICENSE.txt"), (os.path.join(REPO, "NOTICE.md"), "NOTICE.md"),
                 (os.path.join(os.path.dirname(fbin), "LICENSE.txt"), "FFmpeg-LICENSE.txt")):
        if os.path.isfile(s):
            copy_if_changed(s, os.path.join(lic, t))
    ini = os.path.join(out, "gow2_port.ini")
    with open(ini + ".tmp", "w", encoding="utf-8", newline="\r\n") as f:
        f.write("# Written by build.py. The game reads its data from vfs_root (the game's USRDIR folder).\n"
                "# Extra environment defaults may be added under [env] as KEY=VALUE.\n"
                "[paths]\nvfs_root=%s\nmovie_cache=movie_cache\n" % usr)
    os.replace(ini + ".tmp", ini)
    with open(os.path.join(out, "build_info.json"), "w", encoding="utf-8") as f:
        json.dump({"exe": EXE_NAME, "game": game, "elf_sha256": ELF_SHA, "built": time.strftime("%Y-%m-%d %H:%M:%S"),
                   "seconds": round(time.time() - t_start),
                   "installed": installed + ["gow2_port.ini", "build_info.json"],
                   "installed_dirs": [".build", "movie_cache", "licenses"] + (["game"] if a.pkg else []),
                   "pkg": os.path.abspath(a.pkg) if a.pkg else None}, f, indent=2)
    log("build finished in %.0f s" % (time.time() - t_start))
    progress(100, "done: %s" % os.path.join(out, EXE_NAME))
    sys.stdout.write("EXE %s\n" % os.path.join(out, EXE_NAME))   # the launcher starts this exe (cwd = its folder)
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Fail as e:
        log("ERROR " + str(e))
        print("ERROR " + " | ".join(str(e).splitlines()), flush=True)
        sys.exit(1)
    except KeyboardInterrupt:
        print("ERROR interrupted", flush=True)
        sys.exit(1)
    except Exception as e:   # tool failures from buildlib: one line on stdout, details in the log
        import traceback
        log(traceback.format_exc())
        msg = " | ".join(str(e).splitlines()[:6])
        print("ERROR %s: %s (details: %s)" % (type(e).__name__, msg, _LOG[0] or "-"), flush=True)
        sys.exit(1)
