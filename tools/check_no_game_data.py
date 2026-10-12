#!/usr/bin/env python3
"""check_no_game_data.py -- refuse to commit game data or personal paths.

    python tools/check_no_game_data.py            # check the files git would commit (index + tracked)
    python tools/check_no_game_data.py --all      # check every file in the working tree (except .git)
    python tools/check_no_game_data.py --history  # check every blob of every commit reachable from the branches and
                                                  # tags, plus commit/tag messages and author/committer identities
    python tools/check_no_game_data.py --history --ref=v0.1.0   # the same for the history of one ref only

Personal strings are read from tools/private_needles.txt (git-ignored) or GOW2_PRIVATE_NEEDLES, never from this file.

Fails (exit 1) when the repository contains:
  * game binaries/data: ELF/SELF/SPRX/PRX, EBOOT, PSARC, PKG, RAP/RIF/EDAT, movies/sounds/WADs (.m2v .wav .wad .vpk
    .at3), save data (PARAM.SFO, *.sav, the save folder BCUS98229_GOW2), dumped images/memory (.bin .dds .gtf);
  * pictures (.bmp .png .jpg .tga) except the allow-listed upstream assets (generated, not from the game);
  * lifted game code: ppu_recomp*.cpp/.h, spu_recomp.c/.h, the lifted SCREAM jobs (scrA/scrB/scr_jobs),
    recomp_macos_e435/, spu_lifted/, scream_lift/, movie_cache/;
  * logs and captures (*.log, rsxcap_*, tprof_*, profile_*, csprobe_*);
  * personal strings: the owner's Windows user name / e-mail, absolute paths of the development PC.
Install as a pre-commit hook:  copy tools\\hooks\\pre-commit .git\\hooks\\pre-commit
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BAD_EXT = {".elf", ".self", ".sprx", ".prx", ".psarc", ".pkg", ".rap", ".rif", ".edat", ".sdat", ".m2v", ".wav",
           ".wad", ".wad_ps3", ".vpk", ".at3", ".sfo", ".sav", ".bin", ".dds", ".gtf", ".bmp", ".png", ".jpg",
           ".jpeg", ".tga", ".log", ".pam", ".mp4", ".mkv", ".avi"}
ALLOWED_FILES = {   # upstream, generated (not game data)
    "ps3recomp/assets/input/host_prompt_atlas_base.png",
    "launcher/res/icon-256.png",   # our own icon, drawn from shapes by launcher/tools/make_icon.py
    "res/icon-256.png",   # the same icon in the launcher's history before it moved under launcher/ (--history)
    "winport/d3d12/ui_font_metrics.txt",   # numbers only (glyph boxes + spacing), approved by Seth 2026-10-09
}
# upstream engine self-tests: lifter output of small synthetic test programs, not game code
ALLOWED_PREFIXES = ("ps3recomp/runtime/spu/tests/out_", "ps3recomp/runtime/ppu/tests/fixture/")
BAD_NAME = re.compile(r"(^|/)(EBOOT(\.BIN|\.ELF)?|ppu_recomp(_\d+)?\.(cpp|h)|spu_recomp\.(c|h)|scr[AB]\.(c|h)|scr_jobs\.c|"
                      r"ui_font_metrics\.txt|fix_v81_ui_glyphs\.inc|scream_tgt\.bin|PARAM\.SFO|ICON0\.PNG|PIC[01]\.PNG|"
                      r"[^/]*\.(r\d\d|rar))$", re.I)
BAD_DIR = re.compile(r"(^|/)(recomp_macos_e435|spu_lifted|scream_lift|movie_cache|bench_saves|BCUS98229_GOW2[^/]*|"
                     r"rsxcap_[^/]*|logs|\.kit_lift|build-win|obj|test_out)/", re.I)
BAD_LOGNAME = re.compile(r"(^|/)(run_|tprof_|profile_|csprobe_)[^/]*\.(txt|log|bin)$", re.I)
# Personal strings (user name, e-mail, local paths of the development PC) are deliberately NOT written here, so that
# publishing this checker does not publish them: one per line in tools/private_needles.txt (git-ignored) or in
# GOW2_PRIVATE_NEEDLES (';'-separated). Without either, only the generic development-tree markers below are checked.
def _private_needles():
    out = [n for n in os.environ.get("GOW2_PRIVATE_NEEDLES", "").split(";") if n]
    try:
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "private_needles.txt"), encoding="utf-8") as f:
            out += [l.strip() for l in f if l.strip() and not l.startswith("#")]
    except OSError:
        pass
    return [n.encode("utf-8") for n in out]


SECRETS = [b"GodOfWar2HD" + b"\\work", b"GodOfWar2HD" + b"/work"] + _private_needles()
ALLOWED_SECRET_FILES = {"tools/private_needles.txt"}   # git-ignored, holds the needles
ELF_MAGIC = (b"\x7fELF", b"SCE\x00", b"PSAR", b"\x7fPKG")
MAX_SIZE = 4 * 1024 * 1024   # anything bigger is suspicious in a source repo
ALLOWED_BIG = {"gow2-recomp/functions.json"}


def files_to_check(all_files):
    if all_files:
        out = []
        for d, dirs, fs in os.walk(ROOT):
            dirs[:] = [x for x in dirs if x != ".git"]
            for f in fs:
                out.append(os.path.relpath(os.path.join(d, f), ROOT).replace("\\", "/"))
        return out
    r = subprocess.run(["git", "-C", ROOT, "ls-files", "-z", "--cached"], capture_output=True)
    if r.returncode:
        sys.exit("git ls-files failed: " + r.stderr.decode(errors="replace"))
    return [p for p in r.stdout.decode("utf-8", "replace").split("\0") if p]


def check_file(rel, size, data, problems):
    low = rel.lower()
    ext = os.path.splitext(low)[1]
    if rel not in ALLOWED_FILES:
        if ext in BAD_EXT:
            problems.append((rel, "game data / capture / log file type " + ext))
        if BAD_NAME.search(rel) and not rel.startswith(ALLOWED_PREFIXES):
            problems.append((rel, "game-derived file name"))
        if BAD_DIR.search(rel):
            problems.append((rel, "generated / game-data folder"))
        if BAD_LOGNAME.search(rel):
            problems.append((rel, "run log / profile / capture"))
    if size > MAX_SIZE and rel not in ALLOWED_BIG:
        problems.append((rel, "large file (%d MB)" % (size >> 20)))
    if data[:4] in ELF_MAGIC and rel not in ALLOWED_FILES:
        problems.append((rel, "ELF/SELF/PSARC/PKG header"))
    if rel not in ALLOWED_SECRET_FILES:
        low_data = data.lower()
        for s in SECRETS:
            if s.lower() in low_data:
                problems.append((rel, "personal string %r" % s.decode()))


def git(*args):
    r = subprocess.run(["git", "-C", ROOT] + list(args), capture_output=True)
    if r.returncode:
        sys.exit("git %s failed: %s" % (args[0], r.stderr.decode(errors="replace")))
    return r.stdout


def check_history(problems):
    """Every (path, blob) of every commit reachable from the branches and tags, commit and tag messages, identities."""
    refs = [a.split("=", 1)[1] for a in sys.argv if a.startswith("--ref=")] or ["--branches", "--tags"]
    commits = git("rev-list", *refs).decode().split()
    blobs = {}   # blob sha -> set of paths
    for c in commits:
        for ent in git("ls-tree", "-r", "-z", "--full-tree", c).split(b"\0"):
            if not ent:
                continue
            meta, path = ent.split(b"\t", 1)
            mode, typ, sha = meta.split()
            if typ == b"blob":
                blobs.setdefault(sha.decode(), set()).add(path.decode("utf-8", "replace"))
    shas = sorted(blobs)
    r = subprocess.run(["git", "-C", ROOT, "cat-file", "--batch"], input=("\n".join(shas) + "\n").encode(),
                       capture_output=True)
    out, pos = r.stdout, 0
    for sha in shas:
        nl = out.index(b"\n", pos)
        size = int(out[pos:nl].split()[2])
        data = out[nl + 1:nl + 1 + size]
        pos = nl + 1 + size + 1
        for rel in sorted(blobs[sha]):
            check_file(rel, size, data, problems)
    ok_id = b"sethlikepie <sethlikepie@users.noreply.github.com>"
    for c in commits:
        raw = git("cat-file", "commit", c)
        for line in raw.split(b"\n\n", 1)[0].split(b"\n"):
            if line.startswith((b"author ", b"committer ")) and ok_id not in line:
                problems.append(("commit " + c[:10], "identity " + line.split(b">")[0].decode(errors="replace") + ">"))
        for s in SECRETS:
            if s.lower() in raw.lower():
                problems.append(("commit " + c[:10], "personal string %r in the message" % s.decode()))
    for ref in git("for-each-ref", "--format=%(objectname) %(objecttype)", "refs/tags").decode().split("\n"):
        if ref.endswith(" tag"):
            raw = git("cat-file", "tag", ref.split()[0])
            if ok_id not in raw.split(b"\n\n", 1)[0]:
                problems.append(("tag " + ref[:10], "tagger is not the noreply identity"))
            for s in SECRETS:
                if s.lower() in raw.lower():
                    problems.append(("tag " + ref[:10], "personal string %r" % s.decode()))
    print("check_no_game_data --history: %d commits, %d blobs" % (len(commits), len(shas)))


def main():
    problems = []
    if "--history" in sys.argv:
        check_history(problems)
    for rel in ([] if "--history" in sys.argv else files_to_check("--all" in sys.argv)):
        p = os.path.join(ROOT, rel)
        if not os.path.isfile(p):
            continue
        with open(p, "rb") as f:
            data = f.read()
        check_file(rel, len(data), data, problems)
    if problems:
        print("check_no_game_data: %d problem(s) -- NOT committing:" % len(problems))
        for rel, why in problems:
            print("  %s: %s" % (rel, why))
        return 1
    print("check_no_game_data: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
