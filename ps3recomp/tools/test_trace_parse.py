"""Self-check para tools/trace_parse.py (Task 6, plano bringup-instrumentacao-fase0).

Sem dependencia de pytest (convencao ja usada em tools/ -- ver
tools/test_recomp_config.py, que este ficheiro segue a risca): funcoes
`def test_*(tmp_path)` simples + `main()` no fundo que corre cada uma dentro
de um `tempfile.TemporaryDirectory()` e imprime "ok: ...".

Fixtures sao SEMPRE sinteticas, escritas pelo proprio teste como strings --
nunca um log de boot real committado (G7 do plano: logs de boot podem conter
memoria do jogo e nao podem ser committados).

As linhas sinteticas usam os formatos REAIS de <data> encontrados nos
call-sites das Tasks 1-5 (auditados via grep antes de escrever este ficheiro,
ver relatorio da Task 6): MEMORY/OOB (ps3_mem_trace.c), CHECKPOINTS
(ps3_checkpoint.c), THREADS BLOCK/WAKE/CREATE (sys_mutex.c/sys_ppu_thread.c),
DMA GET/PUT/DONE/TAG_WAIT (spu_dma.h), FIOS OP_BEGIN/OP_END (ppu_fs.cpp),
RSX FLIP_REQ/FLIP_DONE/PRESENT (cellGcmSys.c).

Run: .venv/bin/python tools/test_trace_parse.py
"""
import io
import json
import os
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from trace_parse import (
    DERIVATION_KEYS,
    compute_diff,
    is_adhoc_error,
    main as trace_parse_main,
    parse_kv,
    parse_log,
)


def _write_log(tmp_path: Path, name: str, lines) -> str:
    p = tmp_path / name
    p.write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")
    return str(p)


# --- T6.1 -------------------------------------------------------------------

def test_t6_1_empty_log_has_all_keys_and_null_values(tmp_path: Path) -> None:
    path = _write_log(tmp_path, "empty.log", [])
    result = parse_log(path)

    for key in DERIVATION_KEYS:
        assert key in result, f"chave em falta: {key}"
    assert "linhas_ignoradas" in result

    assert result["primeiro_erro"] is None
    assert result["primeiro_ea_invalido"] is None
    assert result["ultima_transicao_estado"] is None
    assert result["threads_bloqueadas"] == []
    assert result["dma_pendentes"] == []
    assert result["fios_pendentes"] == []
    assert result["ultimo_flip_present"] == {"flip_req": None, "flip_done": None, "present": None}
    assert result["diff_baseline"] is None
    assert result["linhas_ignoradas"] == 0


# --- T6.2 -------------------------------------------------------------------

def test_t6_2_primeiro_ea_invalido_e_o_de_menor_tb(tmp_path: Path) -> None:
    lines = [
        "[PS3T] 500 | 0x00000001 | fnA | MEMORY | OOB | ea=0x00000100 len=4 region=unmapped op=read",
        "[PS3T] 100 | 0x00000002 | fnB | MEMORY | OOB | ea=0x00000200 len=4 region=unmapped op=write",
        "[PS3T] 300 | 0x00000003 | fnC | MEMORY | OOB | ea=0x00000300 len=4 region=unmapped op=read",
    ]
    path = _write_log(tmp_path, "oob3.log", lines)
    result = parse_log(path)

    ea = result["primeiro_ea_invalido"]
    assert ea is not None
    assert ea["tb"] == 100, ea
    assert ea["ea"] == "0x00000200", ea
    assert ea["thr"] == "0x00000002", ea
    assert ea["fn"] == "fnB", ea


def test_t6_2b_primeiro_ea_invalido_tb_empatado_desempata_pela_ordem_do_log(tmp_path: Path) -> None:
    # Quirk 4 do brief: checkpoints/eventos cedo no boot repetem tb=1 varias
    # vezes. T6.2 so' exercita tbs DISTINTOS (500/100/300) -- o ramo
    # "tb < primeiro_ea['tb']" em trace_parse.py fica FALSO quando os tbs sao
    # IGUAIS (nao estritamente menor), e esse caminho nunca foi coberto.
    # Aqui os dois OOB tem o MESMO tb=1 -- o desempate tem de favorecer o
    # PRIMEIRO em ordem de log (fnA), nunca o segundo (fnB).
    lines = [
        "[PS3T] 1 | 0x00000001 | fnA | MEMORY | OOB | ea=0x00000100 len=4 region=unmapped op=read",
        "[PS3T] 1 | 0x00000002 | fnB | MEMORY | OOB | ea=0x00000200 len=4 region=unmapped op=write",
    ]
    path = _write_log(tmp_path, "oob_tie.log", lines)
    result = parse_log(path)

    ea = result["primeiro_ea_invalido"]
    assert ea is not None
    assert ea["tb"] == 1, ea
    assert ea["ea"] == "0x00000100", ea
    assert ea["thr"] == "0x00000001", ea
    assert ea["fn"] == "fnA", ea


# --- T6.3 -------------------------------------------------------------------

def test_t6_3_block_sem_wake_aparece_com_wake_nao_aparece(tmp_path: Path) -> None:
    lines = [
        # tid=1: BLOCK sem WAKE -> continua bloqueada.
        "[PS3T] 10 | 0x00000001 | sys_mutex_lock | THREADS | BLOCK | tid=1 on=mutex obj=0x00001000",
        # tid=2: BLOCK seguido de WAKE -> ja nao esta bloqueada.
        "[PS3T] 20 | 0x00000002 | sys_cond_wait | THREADS | BLOCK | tid=2 on=cond obj=0x00002000",
        "[PS3T] 30 | 0x00000002 | sys_cond_wait | THREADS | WAKE | tid=2 on=cond obj=0x00002000",
    ]
    path = _write_log(tmp_path, "block.log", lines)
    result = parse_log(path)

    blocked = result["threads_bloqueadas"]
    tids = [e["tid"] for e in blocked]
    assert tids == ["1"], blocked
    entry = blocked[0]
    assert entry["on"] == "mutex"
    assert entry["obj"] == "0x00001000"
    assert entry["tb"] == 10


def test_t6_3b_wake_com_result_timeout_tambem_termina_block(tmp_path: Path) -> None:
    # Quirk 6 do brief: WAKE com result=timeout/esrch tambem fecha um BLOCK.
    lines = [
        "[PS3T] 10 | 0x00000005 | sys_semaphore_wait | THREADS | BLOCK | tid=5 on=sema obj=0x00003000",
        "[PS3T] 20 | 0x00000005 | sys_semaphore_wait | THREADS | WAKE | tid=5 on=sema obj=0x00003000 result=timeout",
        "[PS3T] 30 | 0x00000006 | sys_event_queue_receive | THREADS | BLOCK | tid=6 on=eventqueue obj=0x00004000",
        "[PS3T] 40 | 0x00000006 | sys_event_queue_receive | THREADS | WAKE | tid=6 on=eventqueue obj=0x00004000 result=esrch",
    ]
    path = _write_log(tmp_path, "block_timeout.log", lines)
    result = parse_log(path)
    assert result["threads_bloqueadas"] == [], result["threads_bloqueadas"]


# --- T6.4 -------------------------------------------------------------------

def test_t6_4_get_sem_done_pendente_com_done_nao_pendente(tmp_path: Path) -> None:
    lines = [
        # tag=3: GET sem DONE -> pendente.
        "[PS3T] 10 | 0x00000001 | mfc_do_transfer | DMA | GET | ea=0x00010000 lsa=0x00100 size=128 tag=3",
        # tag=4: GET com DONE -> nao pendente.
        "[PS3T] 20 | 0x00000001 | mfc_do_transfer | DMA | GET | ea=0x00020000 lsa=0x00200 size=64 tag=4",
        "[PS3T] 25 | 0x00000001 | mfc_do_transfer | DMA | DONE | ea=0x00020000 lsa=0x00200 size=64 tag=4",
        # TAG_WAIT nao tem tag=/ea=/size= -- nunca deve ser tratado como transferencia (quirk 5).
        "[PS3T] 30 | 0x00000001 | mfc_tag_wait | DMA | TAG_WAIT | mask=0x00000001 mode=1 result=0x00000001",
    ]
    path = _write_log(tmp_path, "dma.log", lines)
    result = parse_log(path)

    pend = result["dma_pendentes"]
    tags = {e["tag"] for e in pend}
    assert tags == {"3"}, pend
    entry = pend[0]
    assert entry["ea"] == "0x00010000"
    assert entry["lsa"] == "0x00100"
    assert entry["size"] == "128"
    assert entry["status"] == "pendente"


# --- T6.5 -------------------------------------------------------------------

def test_t6_5_reutilizacao_de_tag_marca_uma_entrada_reutilizado(tmp_path: Path) -> None:
    lines = [
        "[PS3T] 10 | 0x00000001 | mfc_do_transfer | DMA | GET | ea=0x00010000 lsa=0x00100 size=128 tag=3",
        # Novo GET com a MESMA tag=3, ainda sem DONE do primeiro -- fecha o
        # anterior como "reutilizado" em vez de duplicar uma entrada "pendente".
        "[PS3T] 20 | 0x00000001 | mfc_do_transfer | DMA | GET | ea=0x00030000 lsa=0x00300 size=32 tag=3",
    ]
    path = _write_log(tmp_path, "dma_reuse.log", lines)
    result = parse_log(path)

    pend = result["dma_pendentes"]
    entries_tag3 = [e for e in pend if e["tag"] == "3"]
    assert len(entries_tag3) == 2, pend

    reutilizadas = [e for e in entries_tag3 if e["status"] == "reutilizado"]
    pendentes = [e for e in entries_tag3 if e["status"] == "pendente"]
    assert len(reutilizadas) == 1, pend
    assert len(pendentes) == 1, pend
    assert reutilizadas[0]["ea"] == "0x00010000"
    assert pendentes[0]["ea"] == "0x00030000"


def test_t6_5b_reutilizacao_tambem_tolerada_para_fios_op(tmp_path: Path) -> None:
    # Requisito 6 do brief cobre tag (DMA) E op (FIOS) explicitamente.
    lines = [
        "[PS3T] 10 | 0x00000001 | cellFsOpen | FIOS | OP_BEGIN | op=5 path=/old.bin state=open",
        "[PS3T] 20 | 0x00000001 | cellFsOpen | FIOS | OP_BEGIN | op=5 path=/new.bin state=open",
    ]
    path = _write_log(tmp_path, "fios_reuse.log", lines)
    result = parse_log(path)

    pend = result["fios_pendentes"]
    entries_op5 = [e for e in pend if e["op"] == "5"]
    assert len(entries_op5) == 2, pend
    reutilizadas = [e for e in entries_op5 if e["status"] == "reutilizado"]
    assert len(reutilizadas) == 1 and reutilizadas[0]["path"] == "/old.bin", pend


# --- T6.6 -------------------------------------------------------------------

def test_t6_6_flip_req_sem_flip_done_fica_nulo(tmp_path: Path) -> None:
    lines = [
        "[PS3T] 10 | 0x00000001 | cellGcmSetFlipCommand | RSX | FLIP_REQ | buf=1",
    ]
    path = _write_log(tmp_path, "flip.log", lines)
    result = parse_log(path)

    flip = result["ultimo_flip_present"]
    assert flip["flip_done"] is None, flip
    assert flip["flip_req"] == {"buf": "1", "tb": 10}, flip
    assert flip["present"] is None, flip


# --- T6.7 -------------------------------------------------------------------

def test_t6_7_linha_truncada_e_sem_sentinela_nao_derrubam_o_parser(tmp_path: Path) -> None:
    lines = [
        "[PS3T] 5 | - | vm_init | CHECKPOINTS | VM_READY | -",
        # Truncada a meio (escrita cortada -- so' 2 dos 6 campos G1).
        "[PS3T] 123 | 0x00000001",
        # Sem sentinela nenhuma, texto arbitrario (nao bate a heuristica de erro).
        "algum log de terceiros sem relacao com o [PS3T]",
        "[PS3T] 9000 | 0x00000001 | ppu_load_elf | CHECKPOINTS | ELF_LOADED | segments=3",
    ]
    path = _write_log(tmp_path, "malformed.log", lines)
    result = parse_log(path)

    assert result["linhas_ignoradas"] >= 1, result
    # O resto do parse continua correcto: ultimo CHECKPOINTS ainda e' capturado.
    assert result["ultima_transicao_estado"] == {"nome": "ELF_LOADED", "tb": 9000}, result


# --- T6.8 -------------------------------------------------------------------

def test_t6_8_diff_baseline_run_igual_vazio(tmp_path: Path) -> None:
    lines = [
        "[PS3T] 1000 | - | vm_init | CHECKPOINTS | VM_READY | -",
        "[PS3T] 2000 | 0x00000001 | ppu_load_elf | CHECKPOINTS | ELF_LOADED | segments=3",
    ]
    path = _write_log(tmp_path, "baseline_equal.log", lines)
    baseline = parse_log(path)
    current = parse_log(path)

    diff = compute_diff(current, baseline)
    assert diff == {}, diff


def test_t6_8b_diff_baseline_checkpoint_perdido_aponta_o_checkpoint(tmp_path: Path) -> None:
    baseline_lines = [
        "[PS3T] 1000 | - | vm_init | CHECKPOINTS | VM_READY | -",
        "[PS3T] 2000 | 0x00000001 | ppu_load_elf | CHECKPOINTS | ELF_LOADED | segments=3",
    ]
    # O run actual "perdeu" o checkpoint ELF_LOADED -- o log para antes dele.
    current_lines = [
        "[PS3T] 1000 | - | vm_init | CHECKPOINTS | VM_READY | -",
    ]
    baseline_path = _write_log(tmp_path, "baseline.log", baseline_lines)
    current_path = _write_log(tmp_path, "current.log", current_lines)

    baseline = parse_log(baseline_path)
    current = parse_log(current_path)

    diff = compute_diff(current, baseline)
    assert "ultima_transicao_estado" in diff, diff
    assert diff["ultima_transicao_estado"]["baseline"] == {"nome": "ELF_LOADED", "tb": 2000}
    assert diff["ultima_transicao_estado"]["atual"] == {"nome": "VM_READY", "tb": 1000}


def test_t6_8c_diff_baseline_via_json_no_disco(tmp_path: Path) -> None:
    # Exercita o caminho real do CLI: baseline.json escrito por uma execucao
    # anterior de --json, lido de volta por --baseline.
    baseline_lines = [
        "[PS3T] 1000 | - | vm_init | CHECKPOINTS | VM_READY | -",
        "[PS3T] 2000 | 0x00000001 | ppu_load_elf | CHECKPOINTS | ELF_LOADED | segments=3",
    ]
    baseline_log_path = _write_log(tmp_path, "b.log", baseline_lines)
    baseline_result = parse_log(baseline_log_path)
    baseline_json_path = tmp_path / "baseline.json"
    baseline_json_path.write_text(json.dumps(baseline_result), encoding="utf-8")

    current_log_path = _write_log(tmp_path, "c.log", baseline_lines)  # run identico

    buf = io.StringIO()
    with redirect_stdout(buf):
        rc = trace_parse_main([current_log_path, "--json", "--baseline", str(baseline_json_path)])
    assert rc == 0
    out = json.loads(buf.getvalue())
    assert out["diff_baseline"] == {}, out["diff_baseline"]


# --- Achado da review pos-Task 6: diff_baseline ignora campos volateis ------

def _synthetic_boot_log(tmp_path: Path, name: str, *, tb_base: int, thr: str,
                         obj: str, dma_ea: str, dma_lsa: str, dma_size: str,
                         fios_op: str, flip_buf: str) -> str:
    """Constroi um log sintetico de "boot" que atinge um conjunto FIXO de
    estados (2 checkpoints, 1 thread bloqueada, 1 DMA pendente, 1 FIOS
    pendente, 1 OOB, flip ate' FLIP_DONE sem PRESENT) -- mas com todos os
    campos VOLATEIS (tb, thr, obj, ea/lsa/size do DMA, op= do FIOS, buf= do
    flip) parametrizados, para simular dois boots DISTINTOS do mesmo binario
    que chegam ao MESMO estado por caminhos com timing/enderecos diferentes.
    `ea`/`fn` do OOB ficam FIXOS entre chamadas (nao sao parametro) -- sao a
    identidade "o que/onde" que o brief pede para COMPARAR (nao sao ruido)."""
    lines = [
        f"[PS3T] {tb_base} | - | vm_init | CHECKPOINTS | VM_READY | -",
        f"[PS3T] {tb_base + 100} | {thr} | ppu_load_elf | CHECKPOINTS | ELF_LOADED | segments=3",
        f"[PS3T] {tb_base + 200} | {thr} | sys_mutex_lock | THREADS | BLOCK | tid=7 on=mutex obj={obj}",
        f"[PS3T] {tb_base + 300} | {thr} | mfc_do_transfer | DMA | GET | ea={dma_ea} lsa={dma_lsa} size={dma_size} tag=5",
        f"[PS3T] {tb_base + 400} | {thr} | cellFsOpen | FIOS | OP_BEGIN | op={fios_op} path=/data/R_PermA.wad_ps3 state=open",
        f"[PS3T] {tb_base + 500} | {thr} | vm_read32 | MEMORY | OOB | ea=0x00099999 len=4 region=unmapped op=read",
        f"[PS3T] {tb_base + 600} | {thr} | cellGcmSetFlipCommand | RSX | FLIP_REQ | buf={flip_buf}",
        f"[PS3T] {tb_base + 700} | {thr} | cellGcmSetFlipCommand | RSX | FLIP_DONE | buf={flip_buf}",
    ]
    return _write_log(tmp_path, name, lines)


def test_diff_ignores_volatile_fields(tmp_path: Path) -> None:
    # ESTA e' a teste que teria apanhado o achado da review: T6.8 so' diffa
    # o MESMO ficheiro contra si proprio (tb/enderecos identicos por
    # construcao) -- nunca exercita o problema. Aqui construimos DOIS logs
    # que atingem os MESMOS estados (mesmos checkpoints, mesma thread
    # bloqueada por tid/on, mesma tag DMA pendente, mesmo path FIOS pendente,
    # mesmo ea/fn de OOB, mesma distancia no flip -- FLIP_REQ+FLIP_DONE sem
    # PRESENT) mas com tb E enderecos completamente diferentes em todo o
    # log (dois boots DISTINTOS e' precisamente o cenario descrito pelo
    # achado). diff_baseline tem de sair vazio.
    log_a = _synthetic_boot_log(
        tmp_path, "boot_a.log", tb_base=100, thr="0x00000001", obj="0x00AAAA00",
        dma_ea="0x00010000", dma_lsa="0x00100", dma_size="128", fios_op="3", flip_buf="1",
    )
    log_b = _synthetic_boot_log(
        tmp_path, "boot_b.log", tb_base=90000, thr="0x0000FEED", obj="0x00BBBB00",
        dma_ea="0x00777777", dma_lsa="0x00300", dma_size="64", fios_op="11", flip_buf="9",
    )

    result_a = parse_log(log_a)
    result_b = parse_log(log_b)

    # Confirma que os dois logs SAO de facto diferentes campo-a-campo nas
    # partes volateis -- senao o teste nao exercitaria nada.
    assert result_a["ultima_transicao_estado"] != result_b["ultima_transicao_estado"]
    assert result_a["threads_bloqueadas"] != result_b["threads_bloqueadas"]
    assert result_a["dma_pendentes"] != result_b["dma_pendentes"]
    assert result_a["fios_pendentes"] != result_b["fios_pendentes"]
    assert result_a["ultimo_flip_present"] != result_b["ultimo_flip_present"]

    diff = compute_diff(result_b, result_a)
    assert diff == {}, diff


def test_diff_reports_genuine_state_differences(tmp_path: Path) -> None:
    # Companheiro do teste acima: um log que realmente diverge (perdeu o
    # checkpoint ELF_LOADED E ganhou uma thread bloqueada extra) tem de
    # aparecer no diff -- normalizar por identidade estavel nao pode
    # mascarar uma regressao real. As outras chaves (DMA/FIOS/OOB/flip)
    # atingem o MESMO estado que o baseline (so' com tb/enderecos diferentes,
    # tal como no teste acima) precisamente para confirmar que SO' as duas
    # chaves que genuinamente mudaram aparecem no diff -- normalizar nao
    # esconde uma regressao real, mas tambem nao inventa ruido nas chaves
    # que continuam iguais.
    baseline_log = _synthetic_boot_log(
        tmp_path, "baseline.log", tb_base=100, thr="0x00000001", obj="0x00AAAA00",
        dma_ea="0x00010000", dma_lsa="0x00100", dma_size="128", fios_op="3", flip_buf="1",
    )
    current_lines = [
        "[PS3T] 100 | - | vm_init | CHECKPOINTS | VM_READY | -",
        # ELF_LOADED perdido -- o run actual parou mais cedo nos checkpoints.
        "[PS3T] 250 | 0x00000001 | sys_mutex_lock | THREADS | BLOCK | tid=7 on=mutex obj=0x00CCCC00",
        # thread EXTRA bloqueada (tid=9), que o baseline nao tem.
        "[PS3T] 260 | 0x00000002 | sys_cond_wait | THREADS | BLOCK | tid=9 on=cond obj=0x00DDDD00",
        # DMA/FIOS/OOB/flip atingem o MESMO estado do baseline (tag=5
        # pendente, mesmo path pendente, mesmo ea/fn de OOB, flip ate'
        # FLIP_DONE sem PRESENT) -- so' os campos volateis diferem.
        "[PS3T] 300 | 0x00000001 | mfc_do_transfer | DMA | GET | ea=0x00099999 lsa=0x00777 size=999 tag=5",
        "[PS3T] 400 | 0x00000001 | cellFsOpen | FIOS | OP_BEGIN | op=42 path=/data/R_PermA.wad_ps3 state=open",
        "[PS3T] 500 | 0x00000001 | vm_read32 | MEMORY | OOB | ea=0x00099999 len=4 region=unmapped op=read",
        "[PS3T] 600 | 0x00000001 | cellGcmSetFlipCommand | RSX | FLIP_REQ | buf=77",
        "[PS3T] 700 | 0x00000001 | cellGcmSetFlipCommand | RSX | FLIP_DONE | buf=77",
    ]
    current_log = _write_log(tmp_path, "current.log", current_lines)

    baseline = parse_log(baseline_log)
    current = parse_log(current_log)

    diff = compute_diff(current, baseline)
    assert "ultima_transicao_estado" in diff, diff
    assert diff["ultima_transicao_estado"]["baseline"] == {"nome": "ELF_LOADED", "tb": 200}
    assert diff["ultima_transicao_estado"]["atual"] == {"nome": "VM_READY", "tb": 100}

    assert "threads_bloqueadas" in diff, diff
    tids_atual = {e["tid"] for e in diff["threads_bloqueadas"]["atual"]}
    assert tids_atual == {"7", "9"}, diff["threads_bloqueadas"]

    # Chaves que NAO mudaram de identidade (erro/ea/dma/fios/flip) continuam
    # ausentes do diff -- normalizacao nao acende ruido nas chaves que
    # realmente sao iguais.
    assert "primeiro_erro" not in diff, diff
    assert "primeiro_ea_invalido" not in diff, diff
    assert "dma_pendentes" not in diff, diff
    assert "fios_pendentes" not in diff, diff
    assert "ultimo_flip_present" not in diff, diff
    assert len(diff) == 2, diff


# --- T6.9 -------------------------------------------------------------------

def test_t6_9_json_produz_apenas_json_em_stdout(tmp_path: Path) -> None:
    lines = [
        "[PS3T] 10 | 0x00000001 | mfc_do_transfer | DMA | GET | ea=0x00010000 lsa=0x00100 size=128 tag=3",
        "[PS3T] 20 | - | vm_init | CHECKPOINTS | VM_READY | -",
    ]
    path = _write_log(tmp_path, "json.log", lines)

    buf = io.StringIO()
    with redirect_stdout(buf):
        rc = trace_parse_main([path, "--json"])
    assert rc == 0

    raw = buf.getvalue()
    # json.loads sobre a string INTEIRA (apos strip) falha com "Extra data"
    # se houver qualquer coisa alem do objecto JSON em stdout.
    parsed = json.loads(raw.strip())
    assert isinstance(parsed, dict)
    for key in DERIVATION_KEYS:
        assert key in parsed
    # exactamente uma linha de output (o print da JSON + newline final).
    assert raw.count("\n") == 1, repr(raw)


# --- unidades auxiliares (parse_kv / is_adhoc_error) -------------------------

def test_parse_kv_valor_vazio_nao_e_descartado(tmp_path: Path) -> None:
    # Quirk 2 do brief: "tid=1 name= entry=0x..." tem de dar name -> "".
    kv = parse_kv("tid=1 name= entry=0x00001000")
    assert kv == {"tid": "1", "name": "", "entry": "0x00001000"}, kv


def test_is_adhoc_error_heuristica(tmp_path: Path) -> None:
    assert is_adhoc_error("[ppu] FATAL: stuck calling 0x00001234 (5 times) -- aborting run")
    assert is_adhoc_error("[SPUCRASH] code=0x00000001 rip=0x0000000000000000 fault=0x0")
    assert is_adhoc_error("[sys_fs] open FAILED: /dev_hdd0/x")
    assert not is_adhoc_error("[psarc-rd] fd=3 off=0x20 size=1024")
    # "FAIL" solto (idioma de harness de teste) nao deve acender a heuristica.
    assert not is_adhoc_error("FAIL: %s (linha 10)")


def main() -> int:
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        test_t6_1_empty_log_has_all_keys_and_null_values(tmp)
        test_t6_2_primeiro_ea_invalido_e_o_de_menor_tb(tmp)
        test_t6_2b_primeiro_ea_invalido_tb_empatado_desempata_pela_ordem_do_log(tmp)
        test_t6_3_block_sem_wake_aparece_com_wake_nao_aparece(tmp)
        test_t6_3b_wake_com_result_timeout_tambem_termina_block(tmp)
        test_t6_4_get_sem_done_pendente_com_done_nao_pendente(tmp)
        test_t6_5_reutilizacao_de_tag_marca_uma_entrada_reutilizado(tmp)
        test_t6_5b_reutilizacao_tambem_tolerada_para_fios_op(tmp)
        test_t6_6_flip_req_sem_flip_done_fica_nulo(tmp)
        test_t6_7_linha_truncada_e_sem_sentinela_nao_derrubam_o_parser(tmp)
        test_t6_8_diff_baseline_run_igual_vazio(tmp)
        test_t6_8b_diff_baseline_checkpoint_perdido_aponta_o_checkpoint(tmp)
        test_t6_8c_diff_baseline_via_json_no_disco(tmp)
        test_diff_ignores_volatile_fields(tmp)
        test_diff_reports_genuine_state_differences(tmp)
        test_t6_9_json_produz_apenas_json_em_stdout(tmp)
        test_parse_kv_valor_vazio_nao_e_descartado(tmp)
        test_is_adhoc_error_heuristica(tmp)
    print("ok: trace_parse (T6.1-T6.9 + diff_baseline volatil/review + parse_kv/is_adhoc_error auxiliares)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
