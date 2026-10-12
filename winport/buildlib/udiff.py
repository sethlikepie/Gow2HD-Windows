"""udiff.py -- apply a unified diff (the subset `patch -p1` needs for gow2-recomp/kit/ppu_lift_delta.patch):
modified files only, exact context match, hunks found at their line number or by searching outward (offset),
no fuzz. The result is checked against the kit's sha256 list afterwards, so an inexact apply cannot pass."""
import os
import re

_HUNK = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


class PatchError(Exception):
    pass


def _parse(text):
    files, cur, hunk = [], None, None
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    i = 0
    while i < len(lines):
        l = lines[i]
        if l.startswith("--- ") and i + 1 < len(lines) and lines[i + 1].startswith("+++ "):
            path = lines[i + 1][4:].split("\t")[0].strip()
            cur = {"path": path, "hunks": []}
            files.append(cur)
            hunk = None
            i += 2
            continue
        m = _HUNK.match(l)
        if m and cur is not None:
            hunk = {"old_start": int(m.group(1)), "old": [], "new": []}
            cur["hunks"].append(hunk)
            i += 1
            continue
        if hunk is not None and l[:1] in (" ", "-", "+"):
            if l[0] in " -":
                hunk["old"].append(l[1:])
            if l[0] in " +":
                hunk["new"].append(l[1:])
        elif hunk is not None and l.startswith("\\"):
            pass   # "\ No newline at end of file"
        i += 1
    return files


def apply(patch_path, root, strip=1):
    text = open(patch_path, encoding="utf-8", errors="surrogateescape", newline="").read()
    for f in _parse(text):
        rel = f["path"].split("/", strip)[-1] if strip else f["path"]
        p = os.path.join(root, rel)
        src = open(p, encoding="utf-8", errors="surrogateescape", newline="").read()
        ends_nl = src.endswith("\n")
        body = src.split("\n")
        if ends_nl:
            body.pop()
        offset = 0
        for h in f["hunks"]:
            want = h["old_start"] - 1 + offset if h["old"] else h["old_start"] + offset
            n = len(h["old"])
            pos = None
            for d in range(0, len(body) + 1):
                for cand in ((want + d, want - d) if d else (want,)):
                    if 0 <= cand <= len(body) - n and body[cand:cand + n] == h["old"]:
                        pos = cand
                        break
                if pos is not None:
                    break
            if pos is None:
                raise PatchError("%s: hunk at line %d does not apply" % (rel, h["old_start"]))
            body[pos:pos + n] = h["new"]
            offset += (pos - (h["old_start"] - 1 if h["old"] else h["old_start"])) - offset + len(h["new"]) - n
        out = "\n".join(body) + ("\n" if ends_nl else "")
        with open(p, "w", encoding="utf-8", errors="surrogateescape", newline="") as o:
            o.write(out)
