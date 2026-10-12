#!/usr/bin/env python3
"""oracle_manifest.py -- proveniencia do oraculo (Ghidra) e ids I4/I5 por delta.

Porque existe
-------------
O `ghidra_out/` do GoW2 tem 5 JSON (functions, symbols, strings, decompiled,
names) e NENHUM deles diz de que EBOOT saiu, quando, nem com que Ghidra. Um
export obsoleto passa por verdade: "o oraculo diz X" sem binario nem data nao
e' oraculo, e' folclore. Este script transforma ausencia/obsolescencia num
FACTO REPORTADO (rc!=0), nunca em silencio -- a mesma classe de silencio que
deixou passar o `func_0037C384`.

E fornece o segundo lado: mapear os achados I4/I5 de tools/audit_boundaries.py
para IDs opacos que games/gow2/lift_baseline/baseline_delta.py compara por
DELTA contra um baseline congelado. NUNCA por limiar absoluto: o GoW2 tem
milhares de I5 historicos, e um limiar cru nasceria vermelho e ensinaria toda
a gente a ignorar o gate (a licao do verify_lift no v1.0).

Subcomandos
-----------
  write  --out GHIDRA_OUT --elf EBOOT.ELF [--ghidra-version V]
         Carimba GHIDRA_OUT/MANIFEST.json a partir do ELF (sha256) e das
         contagens dos JSON que ja' la' estao. Serve para dar proveniencia a
         um export ANTIGO sem re-analisar tudo (dezenas de minutos).

  check  --manifest MANIFEST.json --elf EBOOT.ELF
         rc=0  ORACLE: ok      (sha do manifesto == sha do ELF actual)
         rc=1  ORACLE: absent  (manifesto ausente ou ilegivel)
         rc=2  ORACLE: stale   (o ELF mudou desde o export)
         rc=3  erro de uso     (ELF ausente/ilegivel)

  ids    --report BOUNDS.json --out IDS.json [--gate-funde]
         Le' o --json de audit_boundaries.py e escreve {"ids":[...]} com o
         conjunto do GATE. rc=2 se o relatorio nao for legivel (nunca ids
         vazios em silencio).

Politica do gate (Item 3 do plano de adopcao + 20-CONTEXT.md)
-------------------------------------------------------------
  DENTRO do gate : todos os I4, e os I5 de tipo `extende-seguro` e `encurta`.
  FORA do gate   : I5 `extende-funde` -- a lacuna ja' e' reclamada por outra
                   funcao NOSSA e a discordancia pode ser legitima
                   (0x00379C70, 6912 bytes). Fica CONTADO e impresso; entra
                   no gate so' com --gate-funde (escotilha declarada).
  NUNCA: contagem absoluta de I4/I5 como criterio de falha.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import sys

# Os JSON que um export do Ghidra pode conter. `None` no manifesto significa
# AUSENTE -- que nao e' a mesma coisa que zero (honestidade: G5).
EXPORT_FILES = {
    "functions": "functions.json",
    "decompiled": "decompiled.json",
    "symbols": "symbols.json",
    "strings": "strings.json",
    "names": "names.json",
}

MANIFEST_NAME = "MANIFEST.json"

# I5 que NAO entram no conjunto do gate por default (ver docstring).
FUNDE = "extende-funde"


# ------------------------------------------------------------------ utils ---
def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def count_json(path: str):
    """len() do JSON em `path`; None se ausente/ilegivel (ausente != 0)."""
    if not os.path.isfile(path):
        return None
    try:
        with open(path, "r", encoding="utf-8") as f:
            return len(json.load(f))
    except (json.JSONDecodeError, TypeError, ValueError):
        return None


def iso_utc(ts: float | None = None) -> str:
    dt = (datetime.datetime.fromtimestamp(ts, datetime.timezone.utc) if ts is not None
          else datetime.datetime.now(datetime.timezone.utc))
    return dt.replace(microsecond=0).isoformat().replace("+00:00", "Z")


def export_mtime(out_dir: str):
    """mtime mais recente dos JSON do export (None se nao houver nenhum).

    Carimbar um export ANTIGO com a data de hoje seria mentira: o `exported_at`
    tem de ser quando os JSON foram produzidos, nao quando lhes demos etiqueta
    (para isso ha' `stamped_at`).
    """
    mts = [os.path.getmtime(os.path.join(out_dir, fn)) for fn in EXPORT_FILES.values()
           if os.path.isfile(os.path.join(out_dir, fn))]
    return max(mts) if mts else None


def detect_ghidra_version():
    """Versao do Ghidra INSTALADO agora. Devolve (versao|None, fonte)."""
    cands = [os.environ.get("GHIDRA_INSTALL_DIR"), os.environ.get("GHIDRA_HOME"),
             "/opt/homebrew/opt/ghidra/libexec", "/usr/local/opt/ghidra/libexec",
             "/opt/ghidra", os.path.expanduser("~/ghidra")]
    for c in cands:
        if not c:
            continue
        props = os.path.join(c, "Ghidra", "application.properties")
        if os.path.isfile(props):
            try:
                with open(props, "r", encoding="utf-8") as f:
                    for line in f:
                        if line.startswith("application.version="):
                            return line.split("=", 1)[1].strip(), "detectada-no-stamp"
            except OSError:
                pass
    return None, "desconhecida"


# ------------------------------------------------------------------ write ---
def write_manifest(out_dir: str, elf_path: str, ghidra_version: str | None = None,
                   manifest_path: str | None = None, exported_at: str | None = None) -> dict:
    """Escreve GHIDRA_OUT/MANIFEST.json e devolve o dict escrito.

    Chamavel por tools/ghidra_analyze.py no fim de um export (proveniencia
    nasce com o export) ou pela CLI para carimbar um export ja' existente.
    """
    counts = {k: count_json(os.path.join(out_dir, fn)) for k, fn in EXPORT_FILES.items()}
    if ghidra_version:
        version, source = ghidra_version, "declarada"
    else:
        version, source = detect_ghidra_version()
    man = {
        "elf_sha256": sha256_file(elf_path),
        "elf_path": os.path.abspath(elf_path),
        "ghidra_version": version or "unknown",
        # De onde veio a versao: um export antigo carimbado hoje NAO sabe com
        # que Ghidra foi produzido -- dizer "detectada-no-stamp" e' o honesto.
        "ghidra_version_source": source,
        # exported_at = quando os JSON foram produzidos (mtime); stamped_at =
        # quando este manifesto foi escrito. Num export fresco coincidem.
        "exported_at": exported_at or iso_utc(export_mtime(out_dir)),
        "stamped_at": iso_utc(),
        "counts": counts,
    }
    dest = manifest_path or os.path.join(out_dir, MANIFEST_NAME)
    os.makedirs(os.path.dirname(os.path.abspath(dest)), exist_ok=True)
    with open(dest, "w", encoding="utf-8") as f:
        json.dump(man, f, indent=1, sort_keys=True)
        f.write("\n")
    return man


def cmd_write(args) -> int:
    if not os.path.isfile(args.elf):
        print(f"ERRO: ELF ausente: {args.elf}", file=sys.stderr)
        return 3
    if not os.path.isdir(args.out):
        print(f"ERRO: dir de export ausente: {args.out}", file=sys.stderr)
        return 3
    man = write_manifest(args.out, args.elf, args.ghidra_version, args.manifest,
                         args.exported_at)
    dest = args.manifest or os.path.join(args.out, MANIFEST_NAME)
    have = ", ".join(f"{k}={v}" for k, v in man["counts"].items() if v is not None)
    print(f"ORACLE: carimbado {dest}")
    print(f"  elf_sha256     {man['elf_sha256'][:16]}...  ({man['elf_path']})")
    print(f"  exported_at    {man['exported_at']}  (stamped_at {man['stamped_at']})")
    print(f"  ghidra_version {man['ghidra_version']} ({man['ghidra_version_source']})")
    print(f"  counts         {have or '(nenhum JSON encontrado)'}")
    return 0


# ------------------------------------------------------------------ check ---
def cmd_check(args) -> int:
    if not os.path.isfile(args.elf):
        print(f"ERRO: ELF ausente: {args.elf}", file=sys.stderr)
        return 3
    if not os.path.isfile(args.manifest):
        print(f"ORACLE: absent -- sem manifesto em {args.manifest}")
        print("  carimba com: oracle_manifest.py write --out <ghidra_out> --elf <EBOOT.ELF>")
        return 1
    try:
        with open(args.manifest, "r", encoding="utf-8") as f:
            man = json.load(f)
        declared = man["elf_sha256"]
    except (json.JSONDecodeError, KeyError, TypeError, ValueError, OSError) as e:
        print(f"ORACLE: absent -- manifesto ilegivel ({type(e).__name__}): {args.manifest}")
        return 1

    actual = sha256_file(args.elf)
    if declared != actual:
        print(f"ORACLE: stale -- o sha256 do ELF difere do carimbado no export")
        print(f"  manifesto {declared}")
        print(f"  actual    {actual}  ({args.elf})")
        print("  re-analisa (analyze_eboot_ghidra.sh) ou re-carimba se o export e' do ELF actual")
        return 2

    counts = man.get("counts") or {}
    have = " ".join(f"{v} {k}" for k, v in counts.items() if v is not None)
    print(f"ORACLE: ok ({have or 'sem contagens'}, exportado {man.get('exported_at', '?')}, "
          f"ghidra {man.get('ghidra_version', '?')})")
    return 0


# -------------------------------------------------------------------- ids ---
def ids_from_findings(report: dict, gate_funde: bool = False) -> tuple[list[str], dict]:
    """Achados de audit_boundaries -> (ids do gate, contagens por classe).

    Forma dos ids (fixada aqui e no teste -- mudar isto invalida o baseline):
      I4:<oraculo>:<addr>
      I5:<oraculo>:<addr>:<tipo>
    """
    ids: set[str] = set()
    stats = {"I4": 0, "extende-seguro": 0, "encurta": 0, FUNDE: 0, "outro": 0}

    for x in report.get("I4") or []:
        stats["I4"] += 1
        ids.add(f"I4:{x.get('oraculo', '?')}:{x['addr']}")

    for x in report.get("I5") or []:
        tipo = x.get("tipo") or "outro"
        stats[tipo] = stats.get(tipo, 0) + 1
        if tipo == FUNDE and not gate_funde:
            continue                      # contado, fora do gate (politica)
        ids.add(f"I5:{x.get('oraculo', '?')}:{x['addr']}:{tipo}")

    return sorted(ids), stats


def cmd_ids(args) -> int:
    try:
        with open(args.report, "r", encoding="utf-8") as f:
            report = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        print(f"ERRO: relatorio de fronteiras ilegivel ({type(e).__name__}): {args.report}",
              file=sys.stderr)
        return 2
    # Um relatorio sem as chaves I4/I5 nao e' "zero achados" -- e' o formato
    # errado. Falhar alto em vez de escrever uma lista vazia que passa o gate.
    if not isinstance(report, dict) or ("I4" not in report and "I5" not in report):
        print(f"ERRO: {args.report} nao tem I4/I5 -- e' output de audit_boundaries --json?",
              file=sys.stderr)
        return 2

    ids, stats = ids_from_findings(report, gate_funde=args.gate_funde)
    funde_nota = "NO gate (--gate-funde)" if args.gate_funde else "fora do gate"
    print(f"[oracle] I4={stats['I4']}  I5 extende-seguro={stats['extende-seguro']}  "
          f"encurta={stats['encurta']}  extende-funde={stats[FUNDE]} ({funde_nota})")
    if stats["outro"]:
        print(f"[oracle] AVISO: {stats['outro']} achados I5 de tipo desconhecido (no gate)")
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump({"ids": ids}, f, indent=1)
        f.write("\n")
    print(f"[oracle] ids no gate: {len(ids)} -> {args.out}")
    return 0


# ------------------------------------------------------------------- main ---
def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    w = sub.add_parser("write", help="carimbar MANIFEST.json num ghidra_out")
    w.add_argument("--out", required=True, help="dir do export (ghidra_out)")
    w.add_argument("--elf", required=True, help="EBOOT.ELF analisado")
    w.add_argument("--ghidra-version", default=None,
                   help="versao que produziu o export (default: detectar a instalada)")
    w.add_argument("--manifest", default=None, help="caminho alternativo do MANIFEST.json")
    w.add_argument("--exported-at", default=None,
                   help="data ISO do export (default: mtime dos JSON do ghidra_out)")
    w.set_defaults(func=cmd_write)

    c = sub.add_parser("check", help="validar proveniencia (ok/absent/stale)")
    c.add_argument("--manifest", required=True)
    c.add_argument("--elf", required=True)
    c.set_defaults(func=cmd_check)

    i = sub.add_parser("ids", help="achados I4/I5 -> {\"ids\":[...]} para baseline_delta")
    i.add_argument("--report", required=True, help="--json de audit_boundaries.py")
    i.add_argument("--out", required=True, help="ficheiro {\"ids\":[...]} a escrever")
    i.add_argument("--gate-funde", action="store_true",
                   help="incluir I5 extende-funde no conjunto do gate (default: fora)")
    i.set_defaults(func=cmd_ids)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
