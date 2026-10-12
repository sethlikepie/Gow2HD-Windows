#!/usr/bin/env python3
"""pack_scripts.py -- zip exactly what build.py needs from the repository, deterministically.

    python pack_scripts.py <repo root> <out.zip>            # release: the real build.py + its sources
    python pack_scripts.py --dir <folder> <out.zip>         # tests: every file of a folder (the stub scripts)

Repo mode packs the files git tracks under INCLUDE (build.py, the licences and the three source trees build.py copies
into <out>/.build/src), minus the folders and file types no build step reads (docs, planning notes, agent configs,
macOS-only sources, pictures). The bytes come from the working tree (the repo has `* -text`, so they equal the
committed blobs); a warning is printed when an included file has uncommitted changes.

The zip is reproducible: sorted entries, fixed timestamps (1980-01-01), fixed attributes, deflate level 9.
Prints `files=<n> bytes=<raw size> zip=<zip size> sha256=<zip sha256>`.
"""
import hashlib
import os
import subprocess
import sys
import zipfile

INCLUDE = ["build.py", "LICENSE", "NOTICE.md", "ps3recomp", "gow2-recomp", "winport"]
# folder names (any depth) that no build step reads
EXCLUDE_DIRS = {"docs", ".planning", ".codex", ".agents", ".superpowers", ".claude", ".github", "notes", "claude_runs",
                "assets", "mods", "__pycache__"}
# file types no build step reads (macOS sources, pictures, docs); licence/notice files are always kept
EXCLUDE_EXT = {".md", ".m", ".mm", ".png", ".jpg", ".jpeg", ".gif", ".pdf", ".gdb", ".pyc"}
KEEP_NAMES = ("LICENSE", "NOTICE", "COPYING")
FIXED_TIME = (1980, 1, 1, 0, 0, 0)


def keep(rel):
    parts = rel.split("/")
    if any(p in EXCLUDE_DIRS for p in parts[:-1]):
        return False
    name = parts[-1]
    if name.upper().startswith(KEEP_NAMES):
        return True
    return os.path.splitext(name)[1].lower() not in EXCLUDE_EXT


def repo_files(root):
    r = subprocess.run(["git", "-C", root, "ls-files", "-z", "--"] + INCLUDE, capture_output=True, check=True)
    files = sorted(p for p in r.stdout.decode("utf-8").split("\0") if p and keep(p))
    d = subprocess.run(["git", "-C", root, "status", "--porcelain", "--"] + INCLUDE, capture_output=True, text=True)
    dirty = [l for l in d.stdout.splitlines() if l.strip()]
    if dirty:
        print("warning: %d included path(s) have uncommitted changes (packed as they are on disk)" % len(dirty),
              file=sys.stderr)
    return files


def dir_files(root):
    out = []
    for d, dirs, fs in os.walk(root):
        dirs[:] = [x for x in dirs if x != "__pycache__"]
        for f in fs:
            if not f.endswith(".pyc"):
                out.append(os.path.relpath(os.path.join(d, f), root).replace("\\", "/"))
    return sorted(out)


def main(argv):
    if len(argv) == 3 and argv[0] == "--dir":
        root, out, files = argv[1], argv[2], dir_files(argv[1])
    elif len(argv) == 2:
        root, out = argv
        files = repo_files(root)
        if "build.py" not in files:
            sys.exit("pack_scripts: no tracked build.py in " + root)
    else:
        sys.exit(__doc__)
    raw = 0
    tmp = out + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for rel in files:
            with open(os.path.join(root, rel), "rb") as f:
                data = f.read()
            raw += len(data)
            zi = zipfile.ZipInfo(rel, FIXED_TIME)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.create_system = 0
            zi.external_attr = 0x20   # FILE_ATTRIBUTE_ARCHIVE
            z.writestr(zi, data, compresslevel=9)
    os.replace(tmp, out)
    sha = hashlib.sha256(open(out, "rb").read()).hexdigest()
    print("files=%d bytes=%d zip=%d sha256=%s" % (len(files), raw, os.path.getsize(out), sha))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
