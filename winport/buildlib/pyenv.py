"""pyenv.py -- the Python that build.py uses for the kit's scripts (and that they use for their own children).

Every one of those processes must load pyfix/sitecustomize.py (LF output, UTF-8, script folder on sys.path):
  * a normal CPython install: sys.executable + PYTHONPATH=<pyfix>;
  * the embeddable CPython (a pythonXY._pth next to python.exe, which ignores PYTHONPATH and does not import
    site): a private copy of that interpreter in <tools>/gow2-build-python whose ._pth also lists <pyfix> and
    `import site`, so the hook loads in every child that runs sys.executable.
"""
import glob
import os
import shutil
import sys


def _pth_of(d):
    p = glob.glob(os.path.join(d, "python*._pth"))
    return p[0] if p else None


def prepare(repo_winport, tools_dir, nonexistent_git):
    pyfix_src = os.path.join(repo_winport, "buildlib", "pyfix")
    home = os.path.dirname(os.path.abspath(sys.executable))
    pth = _pth_of(home)
    env = dict(os.environ)
    for k in ("PYTHONHOME", "PYTHONSTARTUP", "PYTHONINSPECT", "PYTHONOPTIMIZE"):
        env.pop(k, None)
    env.update(PYTHONUTF8="1", PYTHONIOENCODING="utf-8", PYTHONNOUSERSITE="1", PYTHONDONTWRITEBYTECODE="1",
               LC_ALL="C", GIT_DIR=nonexistent_git, GIT_CEILING_DIRECTORIES=os.path.dirname(nonexistent_git))
    if not pth:
        env["PYTHONPATH"] = pyfix_src
        return sys.executable, env
    dst = os.path.join(tools_dir, "gow2-build-python")
    stamp = os.path.join(dst, "stamp.txt")
    want = "%s|%s|%d" % (sys.version, home, os.path.getsize(sys.executable))
    if not (os.path.isfile(stamp) and open(stamp, encoding="utf-8").read() == want):
        shutil.rmtree(dst, ignore_errors=True)
        os.makedirs(dst)
        for f in os.listdir(home):
            s = os.path.join(home, f)
            if os.path.isfile(s):
                shutil.copy2(s, os.path.join(dst, f))
    fix = os.path.join(dst, "pyfix")
    shutil.rmtree(fix, ignore_errors=True)
    shutil.copytree(pyfix_src, fix)
    lines = [l.rstrip("\r\n") for l in open(pth, encoding="utf-8")]
    keep = [l for l in lines if l.strip() and not l.strip().startswith("#") and "import site" not in l]
    with open(os.path.join(dst, os.path.basename(pth)), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(keep + [fix, "import site"]) + "\n")
    with open(stamp, "w", encoding="utf-8") as f:
        f.write(want)
    env.pop("PYTHONPATH", None)
    return os.path.join(dst, "python.exe"), env
