# Build-only start-up hook for every Python process build.py starts (the kit's lifter, its patch scripts and
# their children). It makes Windows Python behave like the Linux/macOS Python the kit was verified with, so the
# lift is byte-identical to the hashes in gow2-recomp/kit/*.sha256:
#   * text files and stdout/stderr are written with LF, not CRLF (the kit's patch scripts look for "\n" needles);
#   * text I/O defaults to UTF-8 even if UTF-8 mode is off (build.py also sets PYTHONUTF8=1);
#   * the script's own folder is sys.path[0] (the embeddable CPython's ._pth disables that) and the user's
#     site-packages are not used.
# Loaded through PYTHONPATH (normal CPython) or through the ._pth of build.py's private copy of an embeddable
# CPython (which ignores PYTHONPATH).
import builtins, io, os, sys

_open = io.open
_utf8 = bool(getattr(sys.flags, "utf8_mode", 0))


def _lf_open(file, mode="r", buffering=-1, encoding=None, errors=None, newline=None, closefd=True, opener=None):
    if "b" not in mode:
        if newline is None and any(c in mode for c in "wax+"):
            newline = "\n"
        if encoding is None and not _utf8:
            encoding = "utf-8"
    return _open(file, mode, buffering, encoding, errors, newline, closefd, opener)


builtins.open = io.open = _lf_open
try:
    import pathlib

    def _wt(self, data, encoding=None, errors=None, newline=None):
        with _lf_open(self, "w", encoding=encoding, errors=errors, newline=newline) as f:
            return f.write(data)
    pathlib.Path.write_text = _wt
except Exception:
    pass
for _s in (sys.stdout, sys.stderr):
    try:
        if _utf8:
            _s.reconfigure(newline="\n")
        else:
            _s.reconfigure(newline="\n", encoding="utf-8", errors="backslashreplace")
    except Exception:
        pass
try:
    import site as _site
    _u = _site.getusersitepackages()
    sys.path[:] = [p for p in sys.path if p != _u]
except Exception:
    pass
if sys.argv and sys.argv[0] not in ("", "-c", "-m") and os.path.isfile(sys.argv[0]):
    _d = os.path.dirname(os.path.abspath(sys.argv[0]))
    if not sys.path or os.path.normcase(os.path.abspath(sys.path[0] or ".")) != os.path.normcase(_d):
        sys.path.insert(0, _d)
