#!/usr/bin/env python3
"""
ghidra_analyze.py — automate Ghidra headless analysis of a PS3 PPU ELF.

Imports the (decrypted) EBOOT.elf into a throwaway Ghidra project, runs
auto-analysis, then runs the ExportAnalysisJson post-script to dump:
    functions.json   symbols.json   strings.json   [decompiled.json]

At the end it also stamps MANIFEST.json (provenance: sha256 of the analysed
ELF, export date, Ghidra version, per-file counts) via tools/oracle_manifest.py.
Without it, an export cannot be told apart from a stale one -- and the oracle
step of verify_lift.sh (VERIFY_ORACLE=1) refuses to run on an unstamped tree.

The results feed ghidra_names.py, which builds a {addr: name} map the lifter
uses to give recompiled functions meaningful names instead of func_ADDR.

Ghidra is located via (in order): --ghidra-dir, $GHIDRA_INSTALL_DIR,
$GHIDRA_HOME, then a list of common install paths.

Usage:
    python tools/ghidra_analyze.py game/EBOOT.elf -o ghidra_out
    python tools/ghidra_analyze.py game/EBOOT.elf -o ghidra_out --decompile
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
GHIDRA_SCRIPT_DIR = os.path.join(SCRIPT_DIR, "ghidra")

# PS3 PPU = Cell BE PPE: 64-bit PowerPC ISA, big-endian, 32-bit addressing.
DEFAULT_PROCESSOR = "PowerPC:BE:64:64-32addr"

COMMON_GHIDRA_PATHS = [
    r"C:\tools\ghidra\ghidra_12.0.3_PUBLIC",
    r"C:\ghidra",
    r"C:\Program Files\ghidra",
    "/opt/ghidra",
    os.path.expanduser("~/ghidra"),
    # macOS: `brew install ghidra` poe o Ghidra em libexec/ do prefixo da
    # formula (arm64 em /opt/homebrew, x86_64 em /usr/local). Sem estes, o
    # find_ghidra falhava numa maquina onde o Ghidra ESTA instalado.
    "/opt/homebrew/opt/ghidra/libexec",
    "/usr/local/opt/ghidra/libexec",
    "/Applications/ghidra",
    "/Applications/Ghidra",
    os.path.expanduser("~/Applications/ghidra"),
]


def find_ghidra(explicit):
    candidates = []
    if explicit:
        candidates.append(explicit)
    for env in ("GHIDRA_INSTALL_DIR", "GHIDRA_HOME"):
        if os.environ.get(env):
            candidates.append(os.environ[env])
    candidates.extend(COMMON_GHIDRA_PATHS)
    # Also accept a glob of versioned dirs under C:\tools\ghidra
    for root in (r"C:\tools\ghidra", "/opt"):
        if os.path.isdir(root):
            for d in sorted(os.listdir(root), reverse=True):
                if d.lower().startswith("ghidra"):
                    candidates.append(os.path.join(root, d))
    for c in candidates:
        if not c:
            continue
        hl = os.path.join(c, "support",
                          "analyzeHeadless.bat" if os.name == "nt" else "analyzeHeadless")
        if os.path.isfile(hl):
            return c, hl
    return None, None


def ghidra_version(ghidra_dir):
    """application.version do Ghidra que vamos correr (None se ilegivel)."""
    props = os.path.join(ghidra_dir or "", "Ghidra", "application.properties")
    try:
        with open(props, "r", encoding="utf-8") as f:
            for line in f:
                if line.startswith("application.version="):
                    return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return None


def main():
    ap = argparse.ArgumentParser(description="Ghidra headless analysis for ps3recomp")
    ap.add_argument("elf", help="Path to decrypted EBOOT.elf")
    ap.add_argument("-o", "--output", default="ghidra_out", help="Output dir for JSONs")
    ap.add_argument("--ghidra-dir", default=None, help="Ghidra install dir")
    ap.add_argument("--processor", default=DEFAULT_PROCESSOR, help="Ghidra language id")
    ap.add_argument("--decompile", action="store_true",
                    help="Also export decompiled C (slow — minutes to hours)")
    ap.add_argument("--project-dir", default=None,
                    help="Ghidra project dir (default: temp, auto-deleted)")
    ap.add_argument("--max-cpu", type=int, default=0, help="analyzeHeadless -max-cpu")
    ap.add_argument("--keep-project", action="store_true",
                    help="Don't delete the Ghidra project afterwards")
    args = ap.parse_args()

    elf = os.path.abspath(args.elf)
    if not os.path.isfile(elf):
        print(f"ERROR: ELF not found: {elf}", file=sys.stderr); sys.exit(1)

    ghidra_dir, headless = find_ghidra(args.ghidra_dir)
    if not headless:
        print("ERROR: Ghidra not found. Pass --ghidra-dir or set GHIDRA_INSTALL_DIR.",
              file=sys.stderr)
        print("  Looked in:", ", ".join(filter(None, COMMON_GHIDRA_PATHS)), file=sys.stderr)
        sys.exit(1)
    print(f"[ghidra] using {ghidra_dir}")

    out_dir = os.path.abspath(args.output)
    os.makedirs(out_dir, exist_ok=True)

    script_file = os.path.join(GHIDRA_SCRIPT_DIR, "ExportAnalysisJson.java")
    if not os.path.isfile(script_file):
        print(f"ERROR: post-script missing: {script_file}", file=sys.stderr); sys.exit(1)

    tmp_proj = args.project_dir or tempfile.mkdtemp(prefix="ps3recomp_ghidra_")
    proj_name = "ps3recomp_analysis"

    cmd = [
        headless, tmp_proj, proj_name,
        "-import", elf,
        "-processor", args.processor,
        "-scriptPath", GHIDRA_SCRIPT_DIR,
        "-postScript", "ExportAnalysisJson.java", out_dir,
    ]
    if args.decompile:
        cmd.append("decompile")
    if args.max_cpu:
        cmd += ["-max-cpu", str(args.max_cpu)]
    if not args.keep_project and not args.project_dir:
        cmd.append("-deleteProject")

    print("[ghidra] running headless analysis (this can take many minutes)...")
    print("  " + " ".join(f'"{c}"' if " " in c else c for c in cmd))
    try:
        rc = subprocess.call(cmd)
    finally:
        if not args.keep_project and not args.project_dir and os.path.isdir(tmp_proj):
            shutil.rmtree(tmp_proj, ignore_errors=True)

    if rc != 0:
        print(f"[ghidra] analyzeHeadless exited {rc}", file=sys.stderr)
        sys.exit(rc)

    produced = [f for f in ("functions.json", "symbols.json", "strings.json",
                            "decompiled.json") if os.path.isfile(os.path.join(out_dir, f))]
    print(f"[ghidra] done. Produced: {', '.join(produced) or '(none!)'} in {out_dir}")

    # Proveniencia: sem isto o export nao diz de que ELF saiu nem quando, e um
    # dump obsoleto passa por verdade. Falhar aqui NAO invalida o export -- e'
    # aviso, e `oracle_manifest.py write` recarimba a posteriori.
    try:
        from oracle_manifest import write_manifest
        man = write_manifest(out_dir, elf, ghidra_version(ghidra_dir))
        print(f"[ghidra] MANIFEST.json: elf_sha256={man['elf_sha256'][:16]}... "
              f"exported_at={man['exported_at']} ghidra={man['ghidra_version']}")
    except Exception as e:                                          # noqa: BLE001
        print(f"[ghidra] WARNING: nao consegui carimbar MANIFEST.json ({e}). "
              f"Corre: tools/oracle_manifest.py write --out {out_dir} --elf {elf}",
              file=sys.stderr)

    if "functions.json" not in produced:
        print("[ghidra] WARNING: functions.json not produced — check analysis log.",
              file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    main()
