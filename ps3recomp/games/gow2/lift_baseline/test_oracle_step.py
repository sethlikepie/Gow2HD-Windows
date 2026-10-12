#!/usr/bin/env python3
"""test_oracle_step.py -- prova OFFLINE da cadeia do passo `oracle_ghidra`.

Corre a MESMA cadeia que games/gow2/verify_lift.sh executa com VERIFY_ORACLE=1:

    audit_boundaries.py --oracle --json  ->  oracle_manifest.py ids  ->  baseline_delta.py

... mas com ranges SINTETICOS de 4 funcoes em vez do corpus do GoW2 (20750
ranges, 13143 funcoes do Ghidra). Assim a semantica do gate e' verificavel sem
EBOOT, sem ghidra_out e sem lift -- corre em qualquer maquina, em segundos.

Nota: sem --elf, o audit_boundaries salta I2/I3 (precisam de descodificar
codigo) e avalia I1/I4/I5 -- que e' exactamente o que este passo gateia.

Nivel de evidencia: offline-unit. A prova por mutacao no corpus REAL
(functions.json de producao encurtado numa copia -> rc=1) esta no
20-01-SUMMARY.md.

Uso:  .venv/bin/python3 games/gow2/lift_baseline/test_oracle_step.py
rc=0  todos [PASS]
rc=1  pelo menos um [FAIL]
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
AUDIT = ROOT / "tools" / "audit_boundaries.py"
ORACLE = ROOT / "tools" / "oracle_manifest.py"
DELTA = HERE / "baseline_delta.py"

# 4 funcoes de 0x40 bytes; a janela I4/I5 sai daqui (sem --elf, lo/hi vem dos
# proprios ranges). A ultima esta afastada para haver espaco entre elas.
NOSSAS = [(0x10000, 0x10040), (0x10040, 0x10080), (0x10080, 0x100C0), (0x10100, 0x10140)]


def escreve_nossas(path: Path, ranges) -> None:
    path.write_text(json.dumps([{"start": f"0x{s:08X}", "end": f"0x{e:08X}"}
                                for s, e in ranges], indent=1))


def escreve_oraculo(path: Path, ranges) -> None:
    path.write_text(json.dumps([{"addr": f"0x{s:08X}", "size": e - s,
                                 "name": f"FUN_{s:08x}", "thunk": False}
                                for s, e in ranges], indent=1))


def cadeia(tmp: Path, nome: str, nossas, oraculo, baseline_ids) -> subprocess.CompletedProcess:
    """audit_boundaries -> ids -> baseline_delta. Devolve o resultado do delta.

    Cada teste tem o seu subdirectorio para que o ficheiro do oraculo se chame
    sempre `oraculo.json` -- o id carrega o basename, e um nome estavel torna
    os ids do teste literais (`I5:oraculo.json:0x...`).
    """
    d = tmp / nome
    d.mkdir(parents=True, exist_ok=True)
    fn, orc = d / "nossas.json", d / "oraculo.json"
    rep, ids, base = d / "rep.json", d / "ids.json", d / "base.json"
    escreve_nossas(fn, nossas)
    escreve_oraculo(orc, oraculo)
    base.write_text(json.dumps({"ids": baseline_ids}))

    r = subprocess.run([sys.executable, str(AUDIT), str(fn), "--oracle", str(orc),
                        "--json", str(rep), "--max-report", "0"],
                       capture_output=True, text=True)
    assert r.returncode == 0, f"audit_boundaries rc={r.returncode}: {r.stdout}{r.stderr}"
    r = subprocess.run([sys.executable, str(ORACLE), "ids", "--report", str(rep),
                        "--out", str(ids)], capture_output=True, text=True)
    assert r.returncode == 0, f"ids rc={r.returncode}: {r.stdout}{r.stderr}"
    d = subprocess.run([sys.executable, str(DELTA), str(ids), str(base),
                        "--label", "oracle_i4_i5"], capture_output=True, text=True)
    d.stdout = r.stdout + d.stdout            # contagens por classe + delta
    return d


def test_sem_divergencia(tmp: Path) -> None:
    """Fronteiras iguais as do oraculo -> zero ids -> rc=0."""
    d = cadeia(tmp, "limpo", NOSSAS, NOSSAS, [])
    assert d.returncode == 0, f"rc esperado 0: {d.stdout}{d.stderr}"
    assert "I4=0" in d.stdout and "extende-seguro=0" in d.stdout, d.stdout
    print("[PASS] test_sem_divergencia: lift == oraculo -> rc=0, gate vazio")


def test_encurtar_funcao_falha(tmp: Path) -> None:
    """Encurtar uma funcao 8 bytes -> I5 extende-seguro NOVO -> rc=1.

    E' a classe do 0x0037C384 (e do func_002550C8, que custou meses): o fim
    errado faz o emissor trampolinar para o vizinho e saltar codigo real.
    """
    mut = [(0x10080, 0x100B8) if s == 0x10080 else (s, e) for s, e in NOSSAS]
    d = cadeia(tmp, "curta", mut, NOSSAS, [])
    assert d.returncode == 1, f"rc esperado 1: {d.stdout}{d.stderr}"
    assert "I5:oraculo.json:0x00010080:extende-seguro" in d.stdout, d.stdout
    print("[PASS] test_encurtar_funcao_falha: -8 bytes -> extende-seguro novo -> rc=1")


def test_funcao_em_falta_falha(tmp: Path) -> None:
    """Uma funcao que o oraculo ve e nos nao -> I4 NOVO -> rc=1."""
    sem = [r for r in NOSSAS if r[0] != 0x10040]
    d = cadeia(tmp, "falta", sem, NOSSAS, [])
    assert d.returncode == 1, f"rc esperado 1: {d.stdout}{d.stderr}"
    assert "I4" in d.stdout and "0x00010040" in d.stdout, d.stdout
    print("[PASS] test_funcao_em_falta_falha: I4 novo -> rc=1")


def test_divergencia_no_baseline_nao_falha(tmp: Path) -> None:
    """A MESMA divergencia, ja' congelada no baseline -> rc=0 (delta, nao limiar).

    E' a propriedade central: 2673 achados historicos no corpus real nao fazem
    o gate falhar; so' um achado NOVO faz.
    """
    mut = [(0x10080, 0x100B8) if s == 0x10080 else (s, e) for s, e in NOSSAS]
    congelado = ["I5:oraculo.json:0x00010080:extende-seguro"]
    d = cadeia(tmp, "base", mut, NOSSAS, congelado)
    assert d.returncode == 0, f"rc esperado 0 (achado ja' no baseline): {d.stdout}{d.stderr}"
    assert "DELTA OK" in d.stdout, d.stdout
    print("[PASS] test_divergencia_no_baseline_nao_falha: achado congelado -> rc=0")


def test_funde_novo_nao_falha(tmp: Path) -> None:
    """Um extende-funde NOVO nao falha o gate, mas e' CONTADO (politica).

    O oraculo funde 0x10040+0x10080 numa so' funcao; a lacuna ja' e' reclamada
    por uma funcao NOSSA e a discordancia pode ser legitima -- por isso fica
    fora do gate, mas impresso.
    """
    orc = [(0x10000, 0x10040), (0x10040, 0x100C0), (0x10080, 0x100C0), (0x10100, 0x10140)]
    d = cadeia(tmp, "funde", NOSSAS, orc, [])
    assert d.returncode == 0, f"funde novo nao pode falhar o gate: {d.stdout}{d.stderr}"
    assert "extende-funde=1" in d.stdout and "fora do gate" in d.stdout, d.stdout
    print("[PASS] test_funde_novo_nao_falha: funde contado, fora do gate -> rc=0")


def main() -> int:
    tests = [test_sem_divergencia, test_encurtar_funcao_falha, test_funcao_em_falta_falha,
             test_divergencia_no_baseline_nao_falha, test_funde_novo_nao_falha]
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
