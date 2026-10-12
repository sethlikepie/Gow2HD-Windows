#!/usr/bin/env python3
"""Inventario completo (nao so FIOS-*) de marcadores de lift apagados pelo
re-lift, patches sem escritor, e patches em NO-MATCH/UNVERIFIED.

Porque existe
-------------
A Fase 9 gastou duas iteracoes do gate (~40min cada) porque descobriu
marcadores apagados um a um, so' correndo o diff de FIOS-*. A CONTEXT.md da
Fase 10 ("A parede do DecodeAu") manda fazer o inventario completo ANTES de
escrever qualquer patch novo -- esta e' essa ferramenta, reutilizavel para
qualquer fase futura que sofra o mesmo re-lift.

Tres varreduras, cada uma barata (segundos):

1. Diff completo de marcadores: todo `[TAG-COM-TRACOS]` dentro de literais
   fprintf, e todo token nu (>=2 palavras maiusculas ligadas por '-') dentro
   de comentarios /* ... */, em recomp_macos_v2.pre_v4/*.cpp (referencia) vs
   recomp_macos_v2/*.cpp (actual) -- presente na referencia, ausente no actual.
2. Patches NO-MATCH/UNVERIFIED: corre apply_all_patches.sh --status de
   verdade (idempotente) e filtra as linhas desses dois estados, excluindo
   classe PROBE (fora do gate, ja documentado no catalogo).
3. Patches sem escritor: todo patch_*.py cujo texto nao contem write_text(,
   nem .write( fora de modo leitura, nem open(...'w'.

Depois cruza: para cada marcador ausente (achado 1), diz se ha' um
patch_*.py que o declara via MARKER (achado 3 dá pista de "sem escritor" e
achado 2 dá pista de "aplica mas nao muda nada").

Uso: python3 inventory_lift_markers.py [--ref DIR] [--cur DIR]
Saida: relatorio em stdout + grava em
notes/<data>-fase10-inventario-marcadores.md (a data e' fixa nesta sessao:
2026-07-31, para bater com o resto dos artefactos da Fase 10).
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent
PATCH_DIR = REPO / "recomp_mid_v2"
NOTES_DIR = REPO / "notes"
REPORT_PATH = NOTES_DIR / "2026-07-31-fase10-inventario-marcadores.md"

# Tag entre parenteses rectos dentro de fprintf: "[FIOS-HOST-POP]", "[AREAD]"...
TAG_BRACKET_RE = re.compile(r"\[([A-Z][A-Z0-9]*(?:-[A-Z0-9]+){1,})\]")
# Token nu de >=2 palavras maiusculas ligadas por '-' dentro de comentarios.
TAG_BARE_RE = re.compile(r"\b([A-Z][A-Z0-9]*(?:-[A-Z0-9]+){1,})\b")

COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)

# Ruido conhecido: tags que aparecem em ambos os lifts so' porque sao nomes
# de tipos/constantes do jogo, nao marcadores de patch (evita falsos positivos
# no diff). Mantido curto de proposito -- preferimos ver ruido a esconder sinal.
NOISE = {
    "PS3-VDEC", "R-LGLSCA", "R-PERMA",
}


def extract_tags_from_text(text: str) -> set[str]:
    tags: set[str] = set()
    # 1) tags entre [] em qualquer parte do ficheiro (cobre fprintf literais
    #    tipo "[FIOSOPEN]").
    for m in TAG_BRACKET_RE.finditer(text):
        tags.add(m.group(1))
    # 2) tokens nus (>=2 palavras maiusculas ligadas por '-') em QUALQUER
    #    parte do ficheiro -- nao so' comentarios. Hifens nao sao validos em
    #    identificadores C++, por isso um token deste formato so' pode vir de
    #    um comentario /* */ ou de um literal fprintf ("F2B-STREAM-ALIGN
    #    residual ..."), nunca de codigo real -- seguro varrer o ficheiro
    #    inteiro em vez de restringir a comentarios (perdia tags dentro de
    #    literais fprintf, ex. F2B-STREAM-ALIGN/DESYNC-STOP completos).
    for m in TAG_BARE_RE.finditer(text):
        tags.add(m.group(1))
    return tags - NOISE


def collect_tags(root: Path) -> dict[str, set[str]]:
    """tag -> conjunto de nomes de ficheiro (chunk) onde aparece."""
    out: dict[str, set[str]] = {}
    for f in sorted(root.glob("*.cpp")):
        try:
            text = f.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for tag in extract_tags_from_text(text):
            out.setdefault(tag, set()).add(f.name)
    return out


def scan_1_marker_diff(ref_dir: Path, cur_dir: Path) -> tuple[dict[str, set[str]], dict[str, set[str]]]:
    ref_tags = collect_tags(ref_dir)
    cur_tags = collect_tags(cur_dir)
    return ref_tags, cur_tags


def scan_2_nomatch_unverified() -> tuple[list[tuple[str, str, str]], str]:
    """Corre apply_all_patches.sh --status de verdade. Devolve (linhas, texto bruto)."""
    status_tsv = "/tmp/patch_status_fase10.tsv"
    script = REPO / "apply_all_patches.sh"
    if not script.is_file():
        return [], "apply_all_patches.sh nao encontrado -- varredura 2 saltada"
    proc = subprocess.run(
        [str(script), "recomp_macos_v2", "--status", status_tsv],
        cwd=str(REPO), capture_output=True, text=True, timeout=300,
    )
    raw = proc.stdout + proc.stderr
    tsv_path = Path(status_tsv)
    if not tsv_path.is_file():
        return [], raw + "\n(TSV nao foi escrito)"
    lines = tsv_path.read_text(encoding="utf-8", errors="replace").splitlines()
    if not lines:
        return [], raw + "\n(TSV vazio)"
    header = lines[0].split("\t")
    # Confirma nomes de coluna antes de indexar -- nao adivinhar a ordem.
    try:
        i_patch = header.index("patch")
        i_status = header.index("status")
        i_classe = header.index("classe")
    except ValueError:
        return [], raw + "\n(cabecalho TSV inesperado: %r)" % (header,)
    out: list[tuple[str, str, str]] = []
    for line in lines[1:]:
        cols = line.split("\t")
        if len(cols) <= max(i_patch, i_status, i_classe):
            continue
        name, status, classe = cols[i_patch], cols[i_status], cols[i_classe]
        if status in ("NO-MATCH", "UNVERIFIED") and classe != "PROBE":
            out.append((name, status, classe))
    return out, raw


def scan_3_no_writer() -> list[str]:
    out: list[str] = []
    for p in sorted(PATCH_DIR.glob("patch_*.py")):
        try:
            text = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        has_write_text = "write_text(" in text
        has_dot_write = ".write(" in text
        has_open_write = bool(re.search(r"open\([^)]*['\"]w", text))
        if not (has_write_text or has_dot_write or has_open_write):
            out.append(p.name)
    return out


def find_markers_declared_by_patches() -> dict[str, list[str]]:
    """marcador -> lista de patch_*.py cujo texto contem esse token literal."""
    out: dict[str, list[str]] = {}
    patches = sorted(PATCH_DIR.glob("patch_*.py"))
    texts = {p.name: p.read_text(encoding="utf-8", errors="replace") for p in patches}
    return texts  # devolvido cru; cruzamento feito no chamador


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", default=str(REPO / "recomp_macos_v2.pre_v4"))
    ap.add_argument("--cur", default=str(REPO / "recomp_macos_v2"))
    args = ap.parse_args()

    ref_dir = Path(args.ref)
    cur_dir = Path(args.cur)
    if not ref_dir.is_dir():
        print(f"ERRO: dir de referencia nao existe: {ref_dir}", file=sys.stderr)
        return 2
    if not cur_dir.is_dir():
        print(f"ERRO: dir actual nao existe: {cur_dir}", file=sys.stderr)
        return 2

    lines: list[str] = []

    def emit(s: str = "") -> None:
        print(s)
        lines.append(s)

    emit("=" * 78)
    emit(" inventory_lift_markers.py -- Fase 10, inventario antes de codigo")
    emit(f" referencia : {ref_dir}")
    emit(f" actual     : {cur_dir}")
    emit("=" * 78)
    emit()

    # ---- varredura 1: diff de marcadores --------------------------------
    emit("-- varredura 1: marcadores presentes em pre_v4, ausentes em v2 --")
    ref_tags, cur_tags = scan_1_marker_diff(ref_dir, cur_dir)
    missing = sorted(set(ref_tags) - set(cur_tags))
    patch_texts = find_markers_declared_by_patches()
    for tag in missing:
        chunks = ", ".join(sorted(ref_tags[tag]))
        declaring = [name for name, text in patch_texts.items() if tag in text]
        if declaring:
            decl = "candidato(s): " + ", ".join(sorted(declaring))
        else:
            decl = "SEM patch_*.py candidato -- buraco novo"
        emit(f"  {tag:32s} chunks(ref)={chunks:20s} {decl}")
    emit(f"  total ausentes: {len(missing)}")
    emit()

    # ---- varredura 2: NO-MATCH / UNVERIFIED ------------------------------
    emit("-- varredura 2: patches NO-MATCH/UNVERIFIED (excl. PROBE) --")
    nomatch_rows, raw2 = scan_2_nomatch_unverified()
    if not nomatch_rows:
        emit("  (nenhum NO-MATCH/UNVERIFIED fora de PROBE, ou apply_all_patches.sh nao correu)")
    for name, status, classe in nomatch_rows:
        emit(f"  {status:11s} {name:44s} classe={classe}")
    emit(f"  total: {len(nomatch_rows)}")
    emit()

    # ---- varredura 3: patches sem escritor --------------------------------
    emit("-- varredura 3: patch_*.py sem write_text()/.write()/open(...'w') --")
    no_writer = scan_3_no_writer()
    for name in no_writer:
        emit(f"  {name}")
    emit(f"  total: {len(no_writer)}")
    emit()

    # ---- cruzamento -------------------------------------------------------
    emit("-- cruzamento: marcador ausente x patch candidato x estado --")
    nomatch_names = {name for name, _, _ in nomatch_rows}
    for tag in missing:
        declaring = [name for name, text in patch_texts.items() if tag in text]
        if not declaring:
            emit(f"  {tag:32s} -> SEM candidato (buraco novo, precisa de patch_*.py de raiz)")
            continue
        for name in declaring:
            flags = []
            if name in no_writer:
                flags.append("SEM ESCRITOR")
            if name in nomatch_names:
                flags.append("NO-MATCH/UNVERIFIED no gate")
            flag_s = (" [" + ", ".join(flags) + "]") if flags else " [aparenta ok -- revalidar]"
            emit(f"  {tag:32s} -> {name}{flag_s}")
    emit()
    emit("=" * 78)
    emit("FIM DO INVENTARIO")
    emit("=" * 78)

    NOTES_DIR.mkdir(parents=True, exist_ok=True)
    # Sem kwarg `newline=` (so' existe a partir do Python 3.10; este script,
    # ao contrario dos patch_*.py, pode correr no python3 de sistema 3.9.6 da
    # Apple). \n explicito no join ja' basta em POSIX (sem CRLF a evitar aqui).
    REPORT_PATH.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"\nrelatorio gravado em {REPORT_PATH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
