#!/usr/bin/env python3
"""
Parse a ps3recomp / boot_gow2 stderr+stdout log into structured facts for an
RPCS3↔recomp oracle session.

Usage:
  python tools/oracle_parse_recomp_log.py /tmp/recomp_boot.log
  python tools/oracle_parse_recomp_log.py /tmp/recomp_boot.log --markdown
  python tools/oracle_parse_recomp_log.py /tmp/recomp_boot.log --json out.json

Recognises common tags emitted by the runtime (ppu_fs, sys_fs, movie_io,
spurs, HLE, lv2 stubs). Unknown lines are ignored.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import List, Optional


# --- line patterns (keep loose: logs evolve) ---------------------------------

RE_FS_OPEN = re.compile(
    r"\[fs\]\s+open\s+'(?P<path>[^']+)'\s*->\s*fd\s+(?P<fd>\d+)",
    re.I,
)
RE_FS_OPEN_FAIL = re.compile(
    r"\[fs\]\s+open\s+FAIL\s+'(?P<path>[^']+)'",
    re.I,
)
RE_FS_NOMOVIES = re.compile(
    r"\[fs\]\s+NOMOVIES:\s+fail open\s+'(?P<path>[^']+)'",
    re.I,
)
RE_SYS_FS_OK = re.compile(
    r"\[sys_fs\]\s+open\s+OK:\s+(?P<path>.+)$",
    re.I,
)
RE_SYS_FS_FAIL = re.compile(
    r"\[sys_fs\]\s+open\s+FAILED:\s+(?P<path>.+)$",
    re.I,
)
RE_MOVIEIO_OPEN = re.compile(
    r"\[movieio\]\s+open\s+'(?P<path>[^']+)'\s*->\s*'(?P<host>[^']+)'\s*size=(?P<size>\d+)",
    re.I,
)
RE_MOVIEIO_MISS = re.compile(
    r"\[movieio\]\s+open\s+'(?P<path>[^']+)':\s+no cache",
    re.I,
)
RE_MOVIE_GUEST = re.compile(
    r"\[movie\]\s+guest (?:movie|opened)\s+'(?P<path>[^']+)'",
    re.I,
)
RE_SND = re.compile(
    r"couldn't open file\s+(?P<path>\S+)|snd_stream.*'(?P<path2>[^']+)'",
    re.I,
)
RE_UNRESOLVED = re.compile(
    r"unresolved NID\s+(?P<nid>0x[0-9A-Fa-f]+)",
    re.I,
)
RE_LV2_STUB = re.compile(
    r"lv2_syscall\s+(?P<num>\d+)\s+\(stub\)|"
    r"\[LV2\]\s+unimplemented syscall\s+(?P<num2>\d+)|"
    r"\[SC\].*num=(?P<num3>\d+)",
    re.I,
)
RE_SPURS_INIT = re.compile(
    r"cellSpursInitializeWithAttribute|spurs_kernel.*start|\[spurs kernel\]",
    re.I,
)
RE_ADD_WL = re.compile(
    r"AddWorkload|cellSpursAddWorkload",
    re.I,
)
RE_GCM = re.compile(
    r"cellGcm|\[GCM\]|\[RSX\]",
    re.I,
)
RE_VDEC = re.compile(
    r"cellVdec|\[vdec\]",
    re.I,
)
RE_MOVIEFSM = re.compile(
    r"\[MOVIEFSM\]|st620\s*=\s*(?P<st>\d+)|st620.*?(?P<st2>\d+)",
    re.I,
)
RE_MOVIEOBJ = re.compile(
    r"\[MOVIEOBJ\]",
    re.I,
)
RE_FIOSOPEN = re.compile(
    r"\[FIOSOPEN\]\s*(?P<body>.*)$",
    re.I,
)
RE_FIOSOPEN_PATH = re.compile(
    r"path='(?P<path>[^']+)'",
    re.I,
)
RE_SNDOPEN = re.compile(
    r"\[SNDOPEN\]\s*(?P<body>.*)$",
    re.I,
)
RE_FIOSSCHED = re.compile(
    r"\[FIOSSCHED\]",
    re.I,
)


@dataclass
class OpenEvent:
    path: str
    ok: bool
    source: str  # fs | sys_fs | movieio | movie | guest_print
    detail: str = ""


@dataclass
class ParseResult:
    opens: List[OpenEvent] = field(default_factory=list)
    unresolved_nids: Counter = field(default_factory=Counter)
    lv2_stubs: Counter = field(default_factory=Counter)
    spurs_init_hits: int = 0
    add_workload_hits: int = 0
    gcm_hits: int = 0
    vdec_hits: int = 0
    moviefsm_hits: int = 0
    movieobj_hits: int = 0
    fiosopen_hits: int = 0
    sndopen_hits: int = 0
    fiossched_hits: int = 0
    fios_paths: List[str] = field(default_factory=list)
    fios_poll_done_nonzero: int = 0
    fios_poll_lines: int = 0
    smlogo_mentions: List[str] = field(default_factory=list)
    wav_mentions: List[str] = field(default_factory=list)
    line_count: int = 0

    def to_jsonable(self) -> dict:
        d = asdict(self)
        d["unresolved_nids"] = dict(self.unresolved_nids)
        d["lv2_stubs"] = {str(k): v for k, v in self.lv2_stubs.items()}
        return d


def parse_log(text: str) -> ParseResult:
    r = ParseResult()
    for line in text.splitlines():
        r.line_count += 1
        s = line.strip()

        m = RE_FS_OPEN.search(s)
        if m:
            r.opens.append(OpenEvent(m.group("path"), True, "fs", f"fd={m.group('fd')}"))
            _note_media(r, m.group("path"), s)
            continue
        m = RE_FS_OPEN_FAIL.search(s) or RE_FS_NOMOVIES.search(s)
        if m:
            r.opens.append(OpenEvent(m.group("path"), False, "fs", "FAIL"))
            _note_media(r, m.group("path"), s)
            continue
        m = RE_SYS_FS_OK.search(s)
        if m:
            path = m.group("path").strip()
            r.opens.append(OpenEvent(path, True, "sys_fs"))
            _note_media(r, path, s)
            continue
        m = RE_SYS_FS_FAIL.search(s)
        if m:
            path = m.group("path").strip()
            r.opens.append(OpenEvent(path, False, "sys_fs", "FAILED"))
            _note_media(r, path, s)
            continue
        m = RE_MOVIEIO_OPEN.search(s)
        if m:
            r.opens.append(
                OpenEvent(
                    m.group("path"),
                    True,
                    "movieio",
                    f"host={m.group('host')} size={m.group('size')}",
                )
            )
            _note_media(r, m.group("path"), s)
            continue
        m = RE_MOVIEIO_MISS.search(s)
        if m:
            r.opens.append(OpenEvent(m.group("path"), False, "movieio", "no cache"))
            _note_media(r, m.group("path"), s)
            continue
        m = RE_MOVIE_GUEST.search(s)
        if m:
            r.opens.append(OpenEvent(m.group("path"), True, "movie", "guest overlay path"))
            _note_media(r, m.group("path"), s)
            continue
        m = RE_SND.search(s)
        if m:
            path = m.group("path") or m.group("path2") or ""
            if path:
                r.opens.append(OpenEvent(path, False, "guest_print", "snd_stream"))
                _note_media(r, path, s)

        for mm in RE_UNRESOLVED.finditer(s):
            r.unresolved_nids[mm.group("nid").lower()] += 1

        m = RE_LV2_STUB.search(s)
        if m:
            num = m.group("num") or m.group("num2") or m.group("num3")
            if num:
                r.lv2_stubs[int(num)] += 1

        if RE_SPURS_INIT.search(s):
            r.spurs_init_hits += 1
        if RE_ADD_WL.search(s):
            r.add_workload_hits += 1
        if RE_GCM.search(s):
            r.gcm_hits += 1
        if RE_VDEC.search(s):
            r.vdec_hits += 1
        if RE_MOVIEFSM.search(s):
            r.moviefsm_hits += 1
        if RE_MOVIEOBJ.search(s):
            r.movieobj_hits += 1

        m = RE_FIOSOPEN.search(s)
        if m:
            r.fiosopen_hits += 1
            body = m.group("body") or ""
            pm = RE_FIOSOPEN_PATH.search(body)
            if pm:
                path = pm.group("path")
                r.fios_paths.append(path)
                _note_media(r, path, s)
                # Treat as a logical "open attempt" even though not cellFs
                r.opens.append(
                    OpenEvent(path, "ret=" in body or "file_new" in body, "fiosopen", body[:80])
                )
            if "002B4224 poll" in body or "002B4224 poll" in s:
                r.fios_poll_lines += 1
                if "done=0x00000000" not in s and "done=0x0" not in s.split("done=")[-1][:12]:
                    # crude: non-zero done word if not all zeros after done=
                    dm = re.search(r"done=(0x[0-9A-Fa-f]+)", s)
                    if dm and dm.group(1) not in ("0x00000000", "0x0", "0"):
                        r.fios_poll_done_nonzero += 1
            if "002B4224 DONE" in s:
                r.fios_poll_done_nonzero += 1

        m = RE_SNDOPEN.search(s)
        if m:
            r.sndopen_hits += 1
            body = m.group("body") or ""
            pm = re.search(r"path='([^']+)'|inline '([^']+)'", body)
            if pm:
                path = pm.group(1) or pm.group(2)
                if path:
                    r.opens.append(OpenEvent(path, False, "sndopen", body[:80]))
                    _note_media(r, path, s)

        if RE_FIOSSCHED.search(s):
            r.fiossched_hits += 1

    return r


def _note_media(r: ParseResult, path: str, line: str) -> None:
    pl = path.lower()
    if "smlogo" in pl or "sm_logo" in pl:
        r.smlogo_mentions.append(line[:200])
    if pl.endswith(".wav") or ".wav" in pl:
        r.wav_mentions.append(line[:200])


def render_markdown(r: ParseResult, log_path: str) -> str:
    lines = [
        f"# Oracle parse — recomp log",
        "",
        f"- **log:** `{log_path}`",
        f"- **lines:** {r.line_count}",
        f"- **opens recorded:** {len(r.opens)}",
        f"- **spurs init-ish hits:** {r.spurs_init_hits}",
        f"- **AddWorkload hits:** {r.add_workload_hits}",
        f"- **GCM-ish hits:** {r.gcm_hits}",
        f"- **Vdec-ish hits:** {r.vdec_hits}",
        f"- **MOVIEFSM hits:** {r.moviefsm_hits}",
        f"- **MOVIEOBJ hits:** {r.movieobj_hits}",
        f"- **FIOSOPEN hits:** {r.fiosopen_hits}",
        f"- **SNDOPEN hits:** {r.sndopen_hits}",
        f"- **FIOSSCHED hits:** {r.fiossched_hits}",
        f"- **FIOS poll lines / done≠0:** {r.fios_poll_lines} / {r.fios_poll_done_nonzero}",
        "",
        "## Opens (order preserved, capped at 80)",
        "",
        "| # | ok | source | path | detail |",
        "|---|----|--------|------|--------|",
    ]
    for i, o in enumerate(r.opens[:80], 1):
        ok = "yes" if o.ok else "NO"
        path = o.path.replace("|", "\\|")
        det = o.detail.replace("|", "\\|")
        lines.append(f"| {i} | {ok} | {o.source} | `{path}` | {det} |")
    if len(r.opens) > 80:
        lines.append(f"| … | | | *+{len(r.opens) - 80} more* | |")

    lines += ["", "## SmLogo / .wav mentions", ""]
    if r.smlogo_mentions or r.wav_mentions:
        for x in (r.smlogo_mentions + r.wav_mentions)[:30]:
            lines.append(f"- `{x}`")
    else:
        lines.append(
            "- **none** — guest print or open for SmLogo/wav never hit the log "
            "(path may not call cellFs / movie_io; compare RPCS3 FS log)."
        )

    lines += ["", "## Unresolved NIDs (top)", ""]
    if r.unresolved_nids:
        for nid, c in r.unresolved_nids.most_common(20):
            lines.append(f"- `{nid}` × {c}")
    else:
        lines.append("- (none)")

    lines += ["", "## LV2 stubs / SC hits (top)", ""]
    if r.lv2_stubs:
        for num, c in r.lv2_stubs.most_common(20):
            lines.append(f"- sc `{num}` × {c}")
    else:
        lines.append("- (none logged as stub)")

    lines += [
        "",
        "## Intro checklist seeds (fill RPCS3 column by hand)",
        "",
        "| # | Question | recomp (auto) | RPCS3 (fill) | Gap / action |",
        "|---|----------|---------------|--------------|--------------|",
    ]

    def yesno(cond: bool) -> str:
        return "yes" if cond else "no"

    opens_ok = [o for o in r.opens if o.ok]
    opens_fail = [o for o in r.opens if not o.ok]
    smlogo_open = any("smlogo" in o.path.lower() for o in r.opens)
    wav_fail = any(
        (not o.ok) and (".wav" in o.path.lower() or "smlogo" in o.path.lower())
        for o in r.opens
    )
    movieio_hit = any(o.source == "movieio" and o.ok for o in r.opens)
    only_psarc = opens_ok and all(
        "psarc" in o.path.lower() or o.path.endswith(".psarc") for o in opens_ok
    )

    rows = [
        (
            1,
            "How many successful opens?",
            f"{len(opens_ok)} ok / {len(opens_fail)} fail",
        ),
        (
            2,
            "SmLogo / smlogo path seen?",
            yesno(smlogo_open or bool(r.smlogo_mentions) or any("smlogo" in p.lower() for p in r.fios_paths)),
        ),
        (
            3,
            "Open via cellFs/sys_fs/movieio/fios?",
            ", ".join(sorted({o.source for o in r.opens})) or "none",
        ),
        (
            4,
            "movie_io served a file?",
            yesno(movieio_hit),
        ),
        (
            5,
            "FIOS path (e.g. /_movies/…m2v)?",
            (", ".join(dict.fromkeys(r.fios_paths[:5])) if r.fios_paths else "none")
            + f" (FIOSOPEN hits={r.fiosopen_hits})",
        ),
        (
            6,
            "wav / SNDOPEN activity?",
            f"sndopen={r.sndopen_hits} wav_mentions={len(r.wav_mentions)} "
            + yesno(wav_fail or bool(r.wav_mentions) or r.sndopen_hits > 0),
        ),
        (
            7,
            "FIOS open poll done≠0?",
            f"polls={r.fios_poll_lines} done_nz={r.fios_poll_done_nonzero}",
        ),
        (
            8,
            "FIOS scheduler (FIOSSCHED) ran?",
            f"hits={r.fiossched_hits}",
        ),
        (
            9,
            "MOVIEFSM / st620 probes?",
            f"hits={r.moviefsm_hits}",
        ),
        (
            10,
            "lv2 stubs / AddWorkload?",
            f"stubs={len(r.lv2_stubs)} addwl={r.add_workload_hits}",
        ),
    ]
    for num, q, auto in rows:
        lines.append(f"| {num} | {q} | {auto} |  |  |")

    lines += [
        "",
        "## Next action (template)",
        "",
        "1. Paste RPCS3 FS log lines for the same boot window into the empty column.",
        "2. Pick **one** gap (usually: path open missing → HLE/VFS; or path wrong case).",
        "3. Implement minimum fix; re-run this script; confirm the auto column moved.",
        "4. Do **not** forge st620/EOS (see docs/RPCS3_AS_REFERENCE.md).",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path, help="boot_gow2 / recomp log file")
    ap.add_argument("--markdown", "-m", action="store_true", help="print markdown report")
    ap.add_argument("--json", type=Path, help="write JSON to this path")
    ap.add_argument("-o", "--output", type=Path, help="write markdown to this path")
    args = ap.parse_args()

    if not args.log.is_file():
        print(f"error: log not found: {args.log}", file=sys.stderr)
        return 2

    text = args.log.read_text(encoding="utf-8", errors="replace")
    result = parse_log(text)

    if args.json:
        args.json.write_text(
            json.dumps(result.to_jsonable(), indent=2), encoding="utf-8"
        )
        print(f"wrote {args.json}")

    md = render_markdown(result, str(args.log))
    if args.output:
        args.output.write_text(md, encoding="utf-8")
        print(f"wrote {args.output}")
    if args.markdown or not args.output:
        print(md)

    # exit 0 always on successful parse; humans use the table
    return 0


if __name__ == "__main__":
    sys.exit(main())
