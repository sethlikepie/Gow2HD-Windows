#!/usr/bin/env python3
"""Testes de tools/oracle_manifest.py -- proveniencia do oraculo + ids I4/I5.

Todos os fixtures sao SINTETICOS (tempfile). Nunca tocam no EBOOT, no
ghidra_out real nem no functions.json de producao -- por regra do projecto
(nunca commitar dados de jogo) e porque um teste que depende de 20 MB de
export nao corre em CI.

Cada teste invoca o SCRIPT como CLI via subprocess (prova fim-a-fim do
binario, mesmo estilo de games/gow2/lift_baseline/test_baseline_delta.py).

Uso:  .venv/bin/python3 tools/test_oracle_manifest.py
rc=0  todos [PASS]
rc=1  pelo menos um [FAIL]
"""
import datetime
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / "oracle_manifest.py"
DELTA = HERE.parent / "games" / "gow2" / "lift_baseline" / "baseline_delta.py"


# ---------------------------------------------------------------- helpers ---
def run(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(SCRIPT), *args],
                          capture_output=True, text=True)


def run_delta(current: Path, baseline: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(DELTA), str(current), str(baseline), "--label", "oracle_i4_i5"],
        capture_output=True, text=True)


def fake_elf(path: Path, content: bytes = b"ELF-sintetico-v1") -> str:
    path.write_bytes(content)
    return hashlib.sha256(content).hexdigest()


def fake_export(d: Path, n_funcs: int = 3) -> Path:
    """ghidra_out sintetico: functions.json + names.json (schema do oraculo)."""
    d.mkdir(parents=True, exist_ok=True)
    funcs = [{"addr": f"0x{0x10000 + i * 0x40:08X}", "size": 0x20,
              "name": f"FUN_{i}", "thunk": False} for i in range(n_funcs)]
    (d / "functions.json").write_text(json.dumps(funcs))
    (d / "names.json").write_text(json.dumps({"0x00010000": "FUN_0"}))
    return d


def finding_i4(addr: str, oraculo: str = "functions.json") -> dict:
    return {"oraculo": oraculo, "addr": addr, "nome": "FUN_x", "dentro_de_outra": False}


def finding_i5(addr: str, tipo: str, oraculo: str = "functions.json") -> dict:
    return {"oraculo": oraculo, "addr": addr, "nome": "FUN_x",
            "nosso_end": "0x00010020", "oraculo_end": "0x00010048",
            "tipo": tipo, "bytes": 40}


def write_report(path: Path, i4: list, i5: list) -> None:
    path.write_text(json.dumps({"I1": [], "I2": [], "I3": [], "I4": i4, "I5": i5}))


def ids_of(report: Path, out: Path, *extra: str) -> list[str]:
    r = run("ids", "--report", str(report), "--out", str(out), *extra)
    assert r.returncode == 0, f"ids rc={r.returncode}: {r.stdout}{r.stderr}"
    return json.loads(out.read_text())["ids"]


# ------------------------------------------------------------------ testes ---
def test_manifest_write_roundtrip(tmp: Path) -> None:
    """write escreve MANIFEST.json com sha do ELF, data ISO, versao e contagens."""
    out = fake_export(tmp / "gh1", n_funcs=7)
    elf = tmp / "eboot1.elf"
    sha = fake_elf(elf, b"conteudo-1")
    # export ANTIGO carimbado hoje: o exported_at tem de ser o do EXPORT (mtime
    # dos JSON), nao o de hoje -- senao a proveniencia mente sobre a idade.
    velho = 1785079560.0
    velho_iso = (datetime.datetime.fromtimestamp(velho, datetime.timezone.utc)
                 .isoformat().replace("+00:00", "Z"))
    for f in out.iterdir():
        os.utime(f, (velho, velho))
    r = run("write", "--out", str(out), "--elf", str(elf), "--ghidra-version", "12.1.2")
    assert r.returncode == 0, f"rc={r.returncode}: {r.stdout}{r.stderr}"
    man = json.loads((out / "MANIFEST.json").read_text())
    assert man["elf_sha256"] == sha, man
    assert man["elf_path"].endswith("eboot1.elf"), man
    assert man["ghidra_version"] == "12.1.2", man
    assert man["exported_at"].endswith("Z") and "T" in man["exported_at"], man
    assert man["exported_at"] == velho_iso, man                  # mtime, nao "agora"
    assert man["stamped_at"] > man["exported_at"], man           # carimbado depois
    assert man["counts"]["functions"] == 7, man
    assert man["counts"]["names"] == 1, man
    # ausentes ficam null (ausente != 0 -- honestidade G5)
    assert man["counts"]["decompiled"] is None, man
    assert man["counts"]["symbols"] is None, man
    print("[PASS] test_manifest_write_roundtrip: sha/data/versao/contagens persistem")


def test_oraculo_ausente_falha(tmp: Path) -> None:
    """check sem MANIFEST.json -> rc=1 e 'ORACLE: absent' (nunca pass silencioso)."""
    out = fake_export(tmp / "gh2")
    elf = tmp / "eboot2.elf"
    fake_elf(elf)
    r = run("check", "--manifest", str(out / "MANIFEST.json"), "--elf", str(elf))
    assert r.returncode == 1, f"rc esperado 1, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "ORACLE: absent" in r.stdout + r.stderr, r.stdout + r.stderr
    print("[PASS] test_oraculo_ausente_falha: sem manifesto -> rc=1, ORACLE: absent")


def test_oraculo_stale_falha(tmp: Path) -> None:
    """MANIFEST com sha != ELF actual -> rc=2 'stale'. NAO pode devolver 0."""
    out = fake_export(tmp / "gh3")
    elf = tmp / "eboot3.elf"
    fake_elf(elf, b"conteudo-original")
    assert run("write", "--out", str(out), "--elf", str(elf)).returncode == 0
    fake_elf(elf, b"conteudo-DIFERENTE-relift")          # o ELF mudou depois do export
    r = run("check", "--manifest", str(out / "MANIFEST.json"), "--elf", str(elf))
    assert r.returncode == 2, f"rc esperado 2, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "ORACLE: stale" in r.stdout + r.stderr, r.stdout + r.stderr
    print("[PASS] test_oraculo_stale_falha: sha divergente -> rc=2, ORACLE: stale")


def test_oraculo_ok(tmp: Path) -> None:
    """MANIFEST com sha do ELF actual -> rc=0 e 'ORACLE: ok'."""
    out = fake_export(tmp / "gh4", n_funcs=5)
    elf = tmp / "eboot4.elf"
    fake_elf(elf, b"conteudo-4")
    assert run("write", "--out", str(out), "--elf", str(elf)).returncode == 0
    r = run("check", "--manifest", str(out / "MANIFEST.json"), "--elf", str(elf))
    assert r.returncode == 0, f"rc esperado 0, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "ORACLE: ok" in r.stdout, r.stdout
    assert "5" in r.stdout, r.stdout                      # cita as contagens
    print("[PASS] test_oraculo_ok: sha coincide -> rc=0, ORACLE: ok com contagens")


def test_ids_from_findings(tmp: Path) -> None:
    """Achados I4/I5 sinteticos -> conjunto de ids exacto e determinista."""
    rep, out = tmp / "rep5.json", tmp / "ids5.json"
    write_report(rep,
                 i4=[finding_i4("0x00010200")],
                 i5=[finding_i5("0x0037C384", "extende-seguro"),
                     finding_i5("0x00379C70", "extende-funde"),
                     finding_i5("0x00400000", "encurta")])
    ids = ids_of(rep, out)
    assert ids == sorted(ids), "ids tem de vir ordenado (determinismo)"
    assert "I4:functions.json:0x00010200" in ids, ids
    assert "I5:functions.json:0x0037C384:extende-seguro" in ids, ids
    assert "I5:functions.json:0x00400000:encurta" in ids, ids
    # extende-funde esta FORA do conjunto do gate (politica, ver teste 8)
    assert not any("extende-funde" in i for i in ids), ids
    assert len(ids) == 3, ids
    print("[PASS] test_ids_from_findings: I4/I5 -> ids opacos estaveis e ordenados")


def test_baseline_i5_delta_clean(tmp: Path) -> None:
    """current subconjunto do baseline -> baseline_delta rc=0 (melhoria nunca falha)."""
    rep, out, base = tmp / "rep6.json", tmp / "ids6.json", tmp / "base6.json"
    write_report(rep, i4=[], i5=[finding_i5("0x0037C384", "extende-seguro")])
    ids_of(rep, out)
    base.write_text(json.dumps({"ids": ["I5:functions.json:0x0037C384:extende-seguro",
                                        "I5:functions.json:0x00500000:extende-seguro"]}))
    r = run_delta(out, base)
    assert r.returncode == 0, f"rc esperado 0, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "DELTA OK" in r.stdout, r.stdout
    print("[PASS] test_baseline_i5_delta_clean: subconjunto do baseline -> rc=0")


def test_baseline_i5_delta_new(tmp: Path) -> None:
    """Um extende-seguro NOVO (classe 0x0037C384) -> rc=1, citado pelo id."""
    rep, out, base = tmp / "rep7.json", tmp / "ids7.json", tmp / "base7.json"
    write_report(rep, i4=[], i5=[finding_i5("0x0037C384", "extende-seguro"),
                                 finding_i5("0x00BADBAD", "extende-seguro")])
    ids_of(rep, out)
    base.write_text(json.dumps({"ids": ["I5:functions.json:0x0037C384:extende-seguro"]}))
    r = run_delta(out, base)
    assert r.returncode == 1, f"rc esperado 1, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "0x00BADBAD" in r.stdout, r.stdout
    print("[PASS] test_baseline_i5_delta_new: extende-seguro novo -> rc=1 e cita o id")


def test_i4_novo_falha(tmp: Path) -> None:
    """Um I4 novo (funcao que o oraculo ve e nos nao) -> rc=1."""
    rep, out, base = tmp / "rep7b.json", tmp / "ids7b.json", tmp / "base7b.json"
    write_report(rep, i4=[finding_i4("0x00123456")], i5=[])
    ids_of(rep, out)
    base.write_text(json.dumps({"ids": []}))
    r = run_delta(out, base)
    assert r.returncode == 1, f"rc esperado 1, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "0x00123456" in r.stdout, r.stdout
    print("[PASS] test_i4_novo_falha: I4 novo -> rc=1")


def test_extende_funde_policy(tmp: Path) -> None:
    """POLITICA: extende-funde fica FORA do gate, mas e' CONTADO e impresso.

    Razao (plano de adopcao, Item 3): a lacuna ja' e' reclamada por outra
    funcao nossa e a discordancia pode ser legitima (0x00379C70, 6912 bytes).
    Um funde novo sozinho NAO pode fazer o gate falhar -- mas tem de aparecer
    no relatorio, senao desaparece do radar.
    """
    rep, out, base = tmp / "rep8.json", tmp / "ids8.json", tmp / "base8.json"
    write_report(rep, i4=[], i5=[finding_i5("0x00379C70", "extende-funde")])
    r = run("ids", "--report", str(rep), "--out", str(out))
    assert r.returncode == 0, r.stdout + r.stderr
    assert "extende-funde" in r.stdout and "1" in r.stdout, r.stdout
    assert "fora do gate" in r.stdout, r.stdout
    ids = json.loads(out.read_text())["ids"]
    assert ids == [], f"funde nao pode entrar no conjunto do gate: {ids}"
    base.write_text(json.dumps({"ids": []}))
    d = run_delta(out, base)
    assert d.returncode == 0, f"funde novo nao pode falhar o gate: {d.stdout}{d.stderr}"
    # ... mas com --gate-funde entra (escotilha declarada, nao default)
    ids_com = ids_of(rep, tmp / "ids8b.json", "--gate-funde")
    assert ids_com == ["I5:functions.json:0x00379C70:extende-funde"], ids_com
    print("[PASS] test_extende_funde_policy: funde fora do gate, contado; --gate-funde inclui")


def test_ids_report_ausente(tmp: Path) -> None:
    """Relatorio inexistente -> rc=2 com mensagem clara (nunca ids vazios em silencio)."""
    r = run("ids", "--report", str(tmp / "nao_existe.json"), "--out", str(tmp / "x.json"))
    assert r.returncode == 2, f"rc esperado 2, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "Traceback" not in r.stderr, r.stderr
    assert "ERRO" in r.stdout + r.stderr, r.stdout + r.stderr
    print("[PASS] test_ids_report_ausente: relatorio em falta -> rc=2, sem ids vazios")


def main() -> int:
    tests = [test_manifest_write_roundtrip, test_oraculo_ausente_falha,
             test_oraculo_stale_falha, test_oraculo_ok, test_ids_from_findings,
             test_baseline_i5_delta_clean, test_baseline_i5_delta_new,
             test_i4_novo_falha, test_extende_funde_policy, test_ids_report_ausente]
    failed = 0
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        for t in tests:
            try:
                t(tmp)
            except AssertionError as e:
                failed += 1
                print(f"[FAIL] {t.__name__}: {e}")
    if failed:
        print(f"\n{failed}/{len(tests)} testes falharam")
        return 1
    print(f"\n{len(tests)}/{len(tests)} testes [PASS]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
