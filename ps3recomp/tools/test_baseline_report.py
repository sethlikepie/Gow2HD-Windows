"""Self-check para tools/baseline_report.py (Task 7, plano bringup-instrumentacao-fase0).

Sem dependencia de pytest (convencao ja usada em tools/ -- ver
tools/test_recomp_config.py, que este ficheiro segue a risca): funcoes
`def test_*(tmp_path)` simples + `main()` no fundo que corre cada uma dentro
de um `tempfile.TemporaryDirectory()` e imprime "ok: ...".

Fixtures sao SEMPRE logs sinteticos, escritos pelo proprio teste como
strings -- nunca um log de boot real committado (G7 do plano: logs de boot
podem conter memoria do jogo e nao podem ser committados). As linhas
canonicas usam os formatos REAIS de <data> encontrados nos call-sites das
Tasks 1-5 (mesma auditoria que tools/test_trace_parse.py ja documenta: Task 2
CHECKPOINTS, Task 3 MEMORY/OOB[_TRUNCATED], Task 4 THREADS/CREATE e
SPURS/TASK_START, Task 5 FIOS/OP_BEGIN com wad= e RSX/PRESENT). A linha
"[hle] unresolved NID ..." reproduz literalmente o formato de
runtime/ppu/ppu_hle.cpp:172 -- e' AD-HOC (sem sentinela [PS3T]), por isso nao
passa por trace_parse.parse_canonical_line, so pelo grep de substring que
baseline_report._scan_counts() faz.

Run: .venv/bin/python tools/test_baseline_report.py
"""
import json
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from baseline_report import (
    BaselineReportError,
    build_baseline,
    compute_run_metrics,
    main as baseline_report_main,
)

REQUIRED_TOP_KEYS = {
    "schema", "jogo", "gerado_em", "commit_engine", "commit_jogo",
    "binario", "env", "runs", "barreira",
}
REQUIRED_RUN_KEYS = {
    "idx", "duracao_s", "primeiro_erro", "ultimo_checkpoint", "oob",
    "imports_nao_resolvidos", "instrucoes_desconhecidas", "threads_criadas",
    "jobs_spu", "wads_abertos", "frames_apresentados",
}
REQUIRED_BARRIER_KEYS = {"igual_nos_runs", "descricao", "variacao_explicada"}


def _write_log(tmp_path: Path, name: str, lines) -> str:
    p = tmp_path / name
    p.write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")
    return str(p)


def _fake_provenance():
    return {
        "commit_engine": "deadbeef",
        "commit_jogo": "cafef00d",
        "binario": {"path": "/fake/boot_gow2", "sha256": "abc123", "mtime": "2026-07-24T00:00:00Z"},
    }


# NOTA (fix pos-review, achado DONE_WITH_CONCERNS da Task 7): estas fixtures
# incluem uma linha RSX/PRESENT sintetica so' para exercitar o MECANISMO de
# contagem de _scan_counts() (uma linha canonica RSX/PRESENT no log conta
# para frames_apresentados, ponto). NAO representam um log real de
# scripts/baseline_run.sh -- com PS3_RSX_BACKEND=trace forcado (o fix desta
# ronda), um run real headless NUNCA emite RSX/PRESENT (o trace backend nao
# tem present()), por isso frames_apresentados=0 e' o valor real esperado em
# baseline.json produzido pelo harness. Ver o cabecalho de
# tools/baseline_report.py para a semantica completa.
LOG_A_GUEST_ENTRY = [
    "[PS3T] 1000 | - | ppu_entry | CHECKPOINTS | ELF_LOADED | -",
    "[PS3T] 1010 | - | ppu_entry | CHECKPOINTS | VM_READY | -",
    "[PS3T] 1020 | - | ppu_entry | CHECKPOINTS | GUEST_ENTRY | -",
    "[PS3T] 1030 | 0x00000001 | sys_ppu_thread_create | THREADS | CREATE | "
    "tid=1 name=main entry=0x00010000 prio=1000",
    "[PS3T] 1040 | 0x00000001 | sys_ppu_thread_create | THREADS | CREATE | "
    "tid=2 name=worker entry=0x00011000 prio=1000",
    "[PS3T] 1050 | - | spurs_task_kick | SPURS | TASK_START | task_id=1 entry=0x00020000",
    "[PS3T] 1060 | - | movie_io_open | FIOS | OP_BEGIN | "
    "op=1 path=R_PermA.wad_ps3 len=1000 state=open wad=1",
    "[PS3T] 1065 | - | movie_io_open | FIOS | OP_BEGIN | "
    "op=2 path=intro.m2v len=2000 state=open wad=0",
    "[PS3T] 1070 | - | cellGcmSetFlipCommand | RSX | PRESENT | buf=0",
]

LOG_B_GUEST_ENTRY = [
    # Mesma barreira que A (GUEST_ENTRY, sem erro), mas tb/tid/task_id/buf
    # todos DIFERENTES -- prova que a comparacao ignora esses campos volateis.
    "[PS3T] 5000 | - | ppu_entry | CHECKPOINTS | ELF_LOADED | -",
    "[PS3T] 5010 | - | ppu_entry | CHECKPOINTS | VM_READY | -",
    "[PS3T] 5020 | - | ppu_entry | CHECKPOINTS | GUEST_ENTRY | -",
    "[PS3T] 5030 | 0x00000009 | sys_ppu_thread_create | THREADS | CREATE | "
    "tid=9 name=main entry=0x00010000 prio=1000",
    "[PS3T] 5050 | - | spurs_task_kick | SPURS | TASK_START | task_id=7 entry=0x00020000",
    "[PS3T] 5060 | - | movie_io_open | FIOS | OP_BEGIN | "
    "op=3 path=R_LglScA.wad_ps3 len=500 state=open wad=1",
    "[PS3T] 5070 | - | cellGcmSetFlipCommand | RSX | PRESENT | buf=1",
]

LOG_C_GUEST_ENTRY_MINIMAL = [
    "[PS3T] 9000 | - | ppu_entry | CHECKPOINTS | ELF_LOADED | -",
    "[PS3T] 9010 | - | ppu_entry | CHECKPOINTS | VM_READY | -",
    "[PS3T] 9020 | - | ppu_entry | CHECKPOINTS | GUEST_ENTRY | -",
]

LOG_D_STOPS_EARLY = [
    # Barreira DIFERENTE: para em ELF_LOADED, nunca chega a GUEST_ENTRY.
    "[PS3T] 2000 | - | ppu_entry | CHECKPOINTS | ELF_LOADED | -",
]


# --- Caso "mesma barreira" ---------------------------------------------------

def test_same_barrier_across_three_runs(tmp_path: Path) -> None:
    logs = [
        _write_log(tmp_path, "run1.log", LOG_A_GUEST_ENTRY),
        _write_log(tmp_path, "run2.log", LOG_B_GUEST_ENTRY),
        _write_log(tmp_path, "run3.log", LOG_C_GUEST_ENTRY_MINIMAL),
    ]
    baseline = build_baseline(
        jogo="gow2", log_paths=logs, durations=[10.0, 11.5, 9.9],
        provenance=_fake_provenance(), env={"PS3_NO_RSX": "1"},
    )
    assert baseline["barreira"]["igual_nos_runs"] is True, baseline["barreira"]
    assert baseline["barreira"]["variacao_explicada"] is None
    assert len(baseline["runs"]) == 3
    for i, run in enumerate(baseline["runs"], start=1):
        assert run["idx"] == i
        assert run["ultimo_checkpoint"] == "GUEST_ENTRY", run
        assert run["primeiro_erro"] is None, run


# --- Caso "checkpoints diferentes" ------------------------------------------

def test_different_checkpoints_requires_variacao_explicada(tmp_path: Path) -> None:
    logs = [
        _write_log(tmp_path, "runA.log", LOG_A_GUEST_ENTRY),
        _write_log(tmp_path, "runD.log", LOG_D_STOPS_EARLY),
    ]

    # Sem --variacao-explicada: build_baseline recusa (nunca null com False).
    try:
        build_baseline(
            jogo="gow2", log_paths=logs, durations=[10.0, 3.0],
            provenance=_fake_provenance(), env={},
        )
    except BaselineReportError:
        pass
    else:
        raise AssertionError("esperava BaselineReportError sem variacao_explicada")

    baseline = build_baseline(
        jogo="gow2", log_paths=logs, durations=[10.0, 3.0],
        provenance=_fake_provenance(), env={},
        variacao_explicada="run 2 abortou cedo por causa X (achado sintetico do teste)",
    )
    assert baseline["barreira"]["igual_nos_runs"] is False, baseline["barreira"]
    assert baseline["barreira"]["variacao_explicada"], baseline["barreira"]
    assert baseline["runs"][0]["ultimo_checkpoint"] == "GUEST_ENTRY"
    assert baseline["runs"][1]["ultimo_checkpoint"] == "ELF_LOADED"


# --- Metrica sem fonte: instrucoes_desconhecidas e' SEMPRE null ------------

def test_instrucoes_desconhecidas_is_always_null_never_zero(tmp_path: Path) -> None:
    logs = [_write_log(tmp_path, "run1.log", LOG_A_GUEST_ENTRY)]
    baseline = build_baseline(
        jogo="gow2", log_paths=logs, durations=[1.0],
        provenance=_fake_provenance(), env={},
    )
    run = baseline["runs"][0]
    assert "instrucoes_desconhecidas" in run
    assert run["instrucoes_desconhecidas"] is None
    # nao pode NUNCA sair 0 fabricado, mesmo com um log rico em eventos.
    assert run["instrucoes_desconhecidas"] != 0


# --- As outras nove metricas, contadas de um log sintetico rico ------------

def test_nine_metrics_counted_correctly(tmp_path: Path) -> None:
    log = _write_log(tmp_path, "rich.log", LOG_A_GUEST_ENTRY)
    metrics, raw = compute_run_metrics(log)
    assert metrics["primeiro_erro"] is None
    assert metrics["ultimo_checkpoint"] == "GUEST_ENTRY"
    assert metrics["oob"] == 0
    assert metrics["imports_nao_resolvidos"] == 0
    assert metrics["instrucoes_desconhecidas"] is None
    assert metrics["threads_criadas"] == 2
    assert metrics["jobs_spu"] == 1
    # so' o OP_BEGIN com wad=1 conta -- o wad=0 (filme .m2v) fica de fora.
    assert metrics["wads_abertos"] == 1
    assert metrics["frames_apresentados"] == 1
    assert raw["ultima_transicao_estado"]["nome"] == "GUEST_ENTRY"


# --- oob: cap + OOB_TRUNCATED (Task 3, ps3_mem_trace.c) ---------------------

def test_oob_under_cap_counts_raw_lines(tmp_path: Path) -> None:
    lines = [
        "[PS3T] 100 | - | vm_read32 | MEMORY | OOB | ea=0x00000000 len=4 region=unmapped op=read",
        "[PS3T] 200 | - | vm_read32 | MEMORY | OOB | ea=0x00000004 len=4 region=unmapped op=read",
        "[PS3T] 300 | - | vm_read32 | MEMORY | OOB | ea=0x00000008 len=4 region=unmapped op=read",
    ]
    log = _write_log(tmp_path, "oob_raw.log", lines)
    metrics, _ = compute_run_metrics(log)
    assert metrics["oob"] == 3


def test_oob_over_cap_uses_truncated_marker_total(tmp_path: Path) -> None:
    lines = [
        "[PS3T] 100 | - | vm_read32 | MEMORY | OOB | ea=0x00000000 len=4 region=unmapped op=read",
        "[PS3T] 200 | - | vm_read32 | MEMORY | OOB | ea=0x00000004 len=4 region=unmapped op=read",
        # o total real (130) e' MAIOR que as duas linhas OOB completas
        # acima -- o parser tem de preferir o marcador, nao a contagem crua.
        "[PS3T] 900 | - | vm_read32 | MEMORY | OOB_TRUNCATED | count=65",
        "[PS3T] 1900 | - | vm_read32 | MEMORY | OOB_TRUNCATED | count=130",
    ]
    log = _write_log(tmp_path, "oob_truncated.log", lines)
    metrics, _ = compute_run_metrics(log)
    assert metrics["oob"] == 130, metrics["oob"]


# --- imports_nao_resolvidos: linha ad-hoc de ppu_hle.cpp:172 ---------------

def test_imports_nao_resolvidos_counts_adhoc_unresolved_nid_lines(tmp_path: Path) -> None:
    lines = [
        "[PS3T] 100 | - | ppu_entry | CHECKPOINTS | GUEST_ENTRY | -",
        "[hle] unresolved NID 0x0000ABCD (g_ctx_count=5 in_ctx=-1 gen_inited=1)",
        "[hle] unresolved NID 0x0000BEEF (g_ctx_count=5 in_ctx=-1 gen_inited=1)",
        "some unrelated ad-hoc line that should not match anything",
    ]
    log = _write_log(tmp_path, "unresolved.log", lines)
    metrics, _ = compute_run_metrics(log)
    assert metrics["imports_nao_resolvidos"] == 2


# --- JSON valida contra as chaves obrigatorias (offline-unit, via API) -----

def test_json_schema_keys_exact(tmp_path: Path) -> None:
    logs = [
        _write_log(tmp_path, "run1.log", LOG_A_GUEST_ENTRY),
        _write_log(tmp_path, "run2.log", LOG_B_GUEST_ENTRY),
        _write_log(tmp_path, "run3.log", LOG_C_GUEST_ENTRY_MINIMAL),
    ]
    baseline = build_baseline(
        jogo="gow2", log_paths=logs, durations=[10.0, 11.0, 9.0],
        provenance=_fake_provenance(), env={"PS3_NO_RSX": "1", "PS3_MOVIE_EOS": "1"},
    )
    assert baseline["schema"] == "ps3recomp.baseline/1"
    assert set(baseline.keys()) == REQUIRED_TOP_KEYS, set(baseline.keys())
    for run in baseline["runs"]:
        assert set(run.keys()) == REQUIRED_RUN_KEYS, set(run.keys())
    assert set(baseline["barreira"].keys()) == REQUIRED_BARRIER_KEYS

    # round-trip via json.dumps/json.loads -- tem de ser serializavel tal e qual.
    encoded = json.dumps(baseline, ensure_ascii=False)
    decoded = json.loads(encoded)
    assert decoded == baseline


# --- CLI ponta-a-ponta: le --out DIR (run_N.log/.meta + provenance/env) ----

def test_cli_main_reads_out_dir_and_writes_baseline_json(tmp_path: Path) -> None:
    out_dir = tmp_path / "out"
    out_dir.mkdir()

    (out_dir / "run_1.log").write_text("\n".join(LOG_A_GUEST_ENTRY) + "\n", encoding="utf-8")
    (out_dir / "run_1.meta").write_text("duration_s=12.5\ntimeout_s=60\nattempts=1\n", encoding="utf-8")
    (out_dir / "run_2.log").write_text("\n".join(LOG_B_GUEST_ENTRY) + "\n", encoding="utf-8")
    (out_dir / "run_2.meta").write_text("duration_s=13.1\ntimeout_s=60\nattempts=2\n", encoding="utf-8")
    (out_dir / "provenance.meta").write_text(
        "boot_bin=/fake/boot_gow2\nboot_sha256=deadbeefcafe\n"
        "boot_mtime_iso=2026-07-24T12:00:00Z\ncommit_engine=eee111\ncommit_jogo=ggg222\n",
        encoding="utf-8",
    )
    (out_dir / "env.txt").write_text(
        "PS3_NO_RSX=1\nPS3_TRACE_CHECKPOINTS=1\nPS3_MOVIE_EOS=1\n", encoding="utf-8",
    )

    json_out = tmp_path / "baseline.json"
    rc = baseline_report_main(["--out", str(out_dir), "--json-out", str(json_out)])
    assert rc == 0, rc
    assert json_out.is_file()

    with open(json_out, "r", encoding="utf-8") as f:
        baseline = json.load(f)

    assert set(baseline.keys()) == REQUIRED_TOP_KEYS
    assert baseline["commit_engine"] == "eee111"
    assert baseline["commit_jogo"] == "ggg222"
    assert baseline["binario"]["sha256"] == "deadbeefcafe"
    assert baseline["env"]["PS3_MOVIE_EOS"] == "1"
    assert len(baseline["runs"]) == 2
    assert baseline["runs"][0]["duracao_s"] == 12.5
    assert baseline["runs"][1]["duracao_s"] == 13.1
    assert baseline["barreira"]["igual_nos_runs"] is True


def test_cli_main_fails_cleanly_when_barrier_differs_without_explanation(tmp_path: Path) -> None:
    out_dir = tmp_path / "out2"
    out_dir.mkdir()

    (out_dir / "run_1.log").write_text("\n".join(LOG_A_GUEST_ENTRY) + "\n", encoding="utf-8")
    (out_dir / "run_1.meta").write_text("duration_s=1.0\n", encoding="utf-8")
    (out_dir / "run_2.log").write_text("\n".join(LOG_D_STOPS_EARLY) + "\n", encoding="utf-8")
    (out_dir / "run_2.meta").write_text("duration_s=1.0\n", encoding="utf-8")
    (out_dir / "provenance.meta").write_text(
        "boot_bin=/fake/boot_gow2\nboot_sha256=abc\nboot_mtime_iso=2026-07-24T00:00:00Z\n"
        "commit_engine=e\ncommit_jogo=g\n", encoding="utf-8",
    )
    (out_dir / "env.txt").write_text("PS3_NO_RSX=1\n", encoding="utf-8")

    json_out = tmp_path / "should_not_exist.json"
    rc = baseline_report_main(["--out", str(out_dir), "--json-out", str(json_out)])
    assert rc != 0, "esperava falha limpa (barreira falsa sem --variacao-explicada)"
    assert not json_out.exists(), "nao deve escrever baseline.json invalido"

    rc2 = baseline_report_main([
        "--out", str(out_dir), "--json-out", str(json_out),
        "--variacao-explicada", "run 2 e' um stub sintetico de teste que para cedo",
    ])
    assert rc2 == 0, rc2
    assert json_out.is_file()


def main() -> int:
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        test_same_barrier_across_three_runs(tmp)
        test_different_checkpoints_requires_variacao_explicada(tmp)
        test_instrucoes_desconhecidas_is_always_null_never_zero(tmp)
        test_nine_metrics_counted_correctly(tmp)
        test_oob_under_cap_counts_raw_lines(tmp)
        test_oob_over_cap_uses_truncated_marker_total(tmp)
        test_imports_nao_resolvidos_counts_adhoc_unresolved_nid_lines(tmp)
        test_json_schema_keys_exact(tmp)
        test_cli_main_reads_out_dir_and_writes_baseline_json(tmp)
        test_cli_main_fails_cleanly_when_barrier_differs_without_explanation(tmp)
    print("ok: baseline_report (schema ps3recomp.baseline/1, barreira, "
          "10 metricas por run, null nunca fabricado)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
