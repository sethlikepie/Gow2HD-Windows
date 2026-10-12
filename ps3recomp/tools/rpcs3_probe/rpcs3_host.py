"""rpcs3_host.py - host-platform glue for the RPCS3 oracle (macOS/Linux/Windows).

Upstream's probe.py takes `--rpcs3 <exe>` and assumes the caller knows the path
and that the GDB stub is already enabled. On macOS neither holds: the binary is
buried inside an .app bundle and the stub is off by default in config.yml, so a
misconfigured run burns a full boot and returns "could not connect" with no clue
which of the two causes it was.

This module answers both questions BEFORE launching anything:

  find_rpcs3()      -> path to the executable, or None
  gdb_stub_status() -> ('on'|'off'|'unknown', detail, config_path)
  preflight()       -> (ok, list_of_human_readable_problems)

Nothing here is title-specific.
"""
import os
import platform
import re
import subprocess

# Candidate executable locations, most specific first.
_MAC_CANDIDATES = (
    "/Applications/RPCS3.app/Contents/MacOS/rpcs3",
    os.path.expanduser("~/Applications/RPCS3.app/Contents/MacOS/rpcs3"),
)
_LINUX_CANDIDATES = (
    "/usr/bin/rpcs3",
    "/usr/local/bin/rpcs3",
    os.path.expanduser("~/.local/bin/rpcs3"),
)


def config_dir():
    """Where RPCS3 keeps config.yml on this platform."""
    if platform.system() == "Darwin":
        return os.path.expanduser("~/Library/Application Support/rpcs3")
    if os.name == "nt":
        return os.path.join(os.environ.get("LOCALAPPDATA", ""), "rpcs3")
    xdg = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    return os.path.join(xdg, "rpcs3")


def find_rpcs3(explicit=None):
    """Resolve the RPCS3 executable. An .app bundle path is accepted and
    rewritten to the inner binary, which is what Popen needs."""
    if explicit:
        p = explicit
        if p.endswith(".app"):
            p = os.path.join(p, "Contents", "MacOS", "rpcs3")
        return p if os.path.exists(p) else None
    cands = _MAC_CANDIDATES if platform.system() == "Darwin" else _LINUX_CANDIDATES
    for c in cands:
        if os.path.exists(c):
            return c
    # last resort: PATH
    try:
        out = subprocess.run(["which", "rpcs3"], capture_output=True, text=True)
        if out.returncode == 0 and out.stdout.strip():
            return out.stdout.strip()
    except OSError:
        pass
    return None


# Two config.yml shapes exist in the wild:
#
#   (A) current builds (v0.0.42 / 19734 verified 2026-08-12) -- a single scalar
#       under Miscellaneous, empty string meaning disabled:
#           Miscellaneous:
#             GDB Server: 127.0.0.1:2345
#
#   (B) the block form upstream's docs describe:
#           GDB Server:
#             Enabled: true
#             Port: 2345
#
# Both are parsed with regexes rather than a YAML dependency -- ps3recomp's tools
# are dependency-free on purpose (tools/requirements.txt).
_STUB_BLOCK = re.compile(
    r'^\s*GDB\s+Server\s*:\s*$(?P<body>(?:\n[ \t]+.*)*)', re.MULTILINE)
_STUB_SCALAR = re.compile(
    r'^\s*GDB\s+Server\s*:\s*(?P<val>\S.*?)\s*$', re.MULTILINE)

DEFAULT_PORT = 2345


def _parse_scalar_addr(val):
    """'127.0.0.1:2345' | '"127.0.0.1:2345"' | ':2345' -> (host, port) or None."""
    val = val.strip().strip('"').strip("'")
    if not val:
        return None
    host, _, port = val.rpartition(":")
    if not port.isdigit():
        return None
    return (host or "127.0.0.1"), int(port)


def gdb_stub_status(cfg_path=None):
    """Return (state, detail, path). state is 'on', 'off' or 'unknown'."""
    path = cfg_path or os.path.join(config_dir(), "config.yml")
    if not os.path.exists(path):
        return "unknown", f"config.yml not found at {path}", path
    try:
        with open(path, "r", errors="replace") as fh:
            text = fh.read()
    except OSError as e:
        return "unknown", f"could not read config.yml: {e}", path

    # Shape (B) first: its key line has nothing after the colon, so the scalar
    # regex cannot match it, but checking block-first keeps the intent obvious.
    m = _STUB_BLOCK.search(text)
    if m:
        body = m.group("body")
        en = re.search(r'^\s*Enabled\s*:\s*(\S+)', body, re.MULTILINE)
        port = re.search(r'^\s*Port\s*:\s*"?(\d+)"?', body, re.MULTILINE)
        port_v = int(port.group(1)) if port else DEFAULT_PORT
        if en and en.group(1).lower() in ("true", "1", "yes"):
            return "on", f"enabled, port {port_v}", path
        return "off", f"present but disabled (port {port_v})", path

    # Shape (A): a non-empty address means the server listens.
    m = _STUB_SCALAR.search(text)
    if m:
        addr = _parse_scalar_addr(m.group("val"))
        if addr:
            return "on", f"enabled, port {addr[1]} (address {addr[0]}:{addr[1]})", path
        return "off", "'GDB Server' is empty (disabled)", path

    return "unknown", "no 'GDB Server' key in config.yml", path


def stub_addr(cfg_path=None):
    """(host, port) the stub listens on, per config.yml."""
    _, detail, _ = gdb_stub_status(cfg_path)
    m = re.search(r'address (\S+?):(\d+)', detail)
    if m:
        return m.group(1), int(m.group(2))
    m = re.search(r'port (\d+)', detail)
    return "127.0.0.1", int(m.group(1)) if m else DEFAULT_PORT


def stub_port(cfg_path=None):
    """Configured GDB stub port, defaulting to RPCS3's own default."""
    return stub_addr(cfg_path)[1]


HOWTO_ENABLE = """\
To enable RPCS3's GDB stub, set a listen address in config.yml under
Miscellaneous (current builds; an EMPTY value disables it):

      Miscellaneous:
        GDB Server: 127.0.0.1:2345

Older/other builds use the block form instead:

      GDB Server:
        Enabled: true
        Port: 2345

Either way, restart RPCS3 once so the setting is read back."""


def preflight(explicit_rpcs3=None, cfg_path=None):
    """Check everything the probe needs. Returns (ok, problems[])."""
    problems = []
    exe = find_rpcs3(explicit_rpcs3)
    if not exe:
        problems.append(
            "RPCS3 executable not found. Install it, or pass --rpcs3 "
            "<path/to/RPCS3.app>.")
    state, detail, path = gdb_stub_status(cfg_path)
    if state == "off":
        problems.append(f"GDB stub is DISABLED in {path} ({detail}).\n" + HOWTO_ENABLE)
    elif state == "unknown":
        problems.append(
            f"Could not confirm the GDB stub is on ({detail}). Run RPCS3 once so "
            f"it writes config.yml.\n" + HOWTO_ENABLE)
    return (not problems), problems


def describe():
    """One-line-per-fact summary, for the probe's banner and for tests."""
    exe = find_rpcs3()
    state, detail, path = gdb_stub_status()
    return {
        "platform": platform.system(),
        "rpcs3": exe or "(not found)",
        "config": path,
        "gdb_stub": state,
        "gdb_stub_detail": detail,
        "port": stub_port(),
    }


if __name__ == "__main__":
    import json
    info = describe()
    print(json.dumps(info, indent=2))
    ok, probs = preflight()
    print("\npreflight:", "OK" if ok else "BLOCKED")
    for p in probs:
        print("\n- " + p)
