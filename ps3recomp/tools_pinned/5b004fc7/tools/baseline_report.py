#!/usr/bin/env python3
"""tools/baseline_report.py -- Task 7 do plano `bringup-instrumentacao-fase0`.

Consome os logs de N runs headless de `scripts/baseline_run.sh` e escreve
`baseline.json` com o schema `ps3recomp.baseline/1` exigido pelo brief da
Task 7 (`.superpowers/sdd/task-7-brief.md`) e pelo plano de origem
(`docs/superpowers/plans/2026-07-24-bringup-instrumentacao-fase0.md`,
Task 7, linhas 130-146 do plano ainda mais a montante,
`docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md`).

Importa `tools/trace_parse.py` (Task 6) COMO MODULO -- nunca via subprocess
-- reaproveitando `parse_log()` (as oito derivacoes) e `compute_diff()` (a
comparacao por identidade estavel, nao por texto/tb/enderecos volateis) em
vez de duplicar logica de parsing.

--------------------------------------------------------------------------
As dez metricas por run e a fonte exacta de cada uma (documentado aqui
porque o brief da Task 7 exige a justificacao no cabecalho deste ficheiro):

  duracao_s                 -- medido pelo `scripts/baseline_run.sh` (wall
                                clock do processo `boot_gow2`, PID a PID);
                                lido do ficheiro `run_N.meta` (chave
                                `duration_s`), nunca recalculado aqui.
  primeiro_erro              -- trace_parse.parse_log()["primeiro_erro"]
                                (dict {tb,texto,linha} ou None) tal e qual.
  ultimo_checkpoint           -- trace_parse.parse_log()
                                ["ultima_transicao_estado"]["nome"], ou
                                None se nenhum CHECKPOINTS disparou.
  oob                        -- contagem de linhas canonicas MEMORY/OOB.
                                Ressalva: o emissor (runtime/trace/
                                ps3_mem_trace.c) capa em
                                PS3_MEM_OOB_CANONICAL_CAP=64 linhas OOB
                                "cheias" e depois emite MEMORY/OOB_TRUNCATED
                                com `count=` a cada 64 eventos adicionais
                                (growth schedule). Quando ha pelo menos um
                                OOB_TRUNCATED, `oob` usa o MAIOR `count=`
                                visto (mais preciso que contar so as linhas
                                OOB completas) -- ainda assim pode ficar
                                stale ate CAP-1=63 eventos atras do total
                                real no momento em que o processo morreu
                                (ver o comentario de ps3_mem_trace.c). Sem
                                nenhum OOB_TRUNCATED, `oob` e' a contagem
                                exacta de linhas OOB (esta abaixo do cap).
  imports_nao_resolvidos      -- NAO e' um evento canonico [PS3T] -- nenhum
                                dos dez subsistemas de G2 cobre "imports".
                                A UNICA fonte real e' a linha ad-hoc emitida
                                por runtime/ppu/ppu_hle.cpp:172 em cada NID
                                sem handler HLE: "[hle] unresolved NID
                                0x...". Contamos ocorrencias literais dessa
                                string no log inteiro (nao so linhas
                                [PS3T]). CAVEAT IMPORTANTE: esse call site
                                tem o seu PROPRIO cap (`logged < 40`,
                                ppu_hle.cpp:171) e, ao contrario do
                                MEMORY/OOB, NAO emite nenhum marcador
                                "_TRUNCATED" com o total real acima do cap
                                -- por isso este numero e' EXACTO ate 40 e
                                vira um LIMITE INFERIOR (nao um total) se o
                                run atingir o cap. Reportado como esta, sem
                                extrapolar.
  instrucoes_desconhecidas    -- SEMPRE `null`. Ambiguidade resolvida
                                explicitamente pelo brief da Task 7: esta e'
                                uma grandeza de LIFT-TIME (opcodes PPC nao
                                reconhecidos pelo `ppu_lifter.py` durante a
                                recompilacao estatica), nao de RUN-TIME --
                                nenhum boot log a contem, e nenhum dos dez
                                subsistemas PS3_TRACE_* foi desenhado para
                                a emitir. Fabricar um `0` aqui violaria a
                                regra 4 do CLAUDE.md ("NUNCA forjar
                                resultados"): um `0` pareceria "medido e
                                zero instrucoes desconhecidas", quando na
                                verdade e' "nunca medido por este caminho".
                                Quem quiser este numero tem de o obter do
                                proprio `ppu_lifter.py` (relatorio de lift),
                                fora do escopo de um harness de RUNS.
  threads_criadas             -- contagem de linhas canonicas THREADS/CREATE
                                (sys_ppu_thread.c:338, Task 4). Cada CREATE
                                bem sucedido dispara uma vez -- nao ha cap
                                conhecido neste evento.
  jobs_spu                    -- contagem de linhas canonicas SPURS/TASK_START
                                (cellSpurs.c, Task 4; sitios
                                spurs_pm_dispatch:320 e spurs_task_kick:394).
                                CAVEAT: por comentario explicito do proprio
                                codigo-fonte (cellSpurs.c linha ~311-316),
                                TASK_START/TASK_END delimitam UM "dispatch
                                slice" da task, nao "a task terminou para
                                sempre" -- uma task SPURS persistente
                                re-despachada (re-kicked) produz varios
                                TASK_START para o MESMO task_id. Este numero
                                e' portanto "quantos dispatches de job SPU
                                correram" (a granularidade mais fina que
                                este HLE observa), que pode exceder o numero
                                de jobs logicamente distintos.
  wads_abertos                -- contagem de linhas canonicas FIOS/OP_BEGIN
                                cujo campo `wad=` vale exactamente "1"
                                (movie_hle.c:667, Task 5 -- `movie_io_open`
                                marca wad=1 quando o path tem sufixo
                                ".wad_ps3", distinguindo de filmes .m2v que
                                passam pelo MESMO caminho de codigo com
                                wad=0). CAVEAT: conta ABERTURAS, nao WADs
                                DISTINTOS -- reabrir o mesmo WAD duas vezes
                                no mesmo run conta duas vezes.
  frames_apresentados         -- contagem de linhas canonicas RSX/PRESENT
                                (cellGcmSys.c:1133, Task 5).

                                FIX pos-review (achado DONE_WITH_CONCERNS da
                                Task 7): a Task 7 original confirmou in-boot
                                que PRESENT podia disparar mesmo sob
                                PS3_NO_RSX=1, porque env_gow2.sh deixa
                                PS3_RSX_BACKEND=metal activo no Darwin e
                                cellGcmSys.c tem um SEGUNDO registo de
                                backend, INDEPENDENTE de PS3_NO_RSX
                                (rsx_bridge_mode(), guiado so por
                                PS3_RSX_BACKEND) -- ou seja, um run headless
                                podia mesmo assim abrir uma janela Metal/SDL
                                real ao alcancar o FIFO/flip. `scripts/
                                baseline_run.sh` agora FORCA
                                PS3_RSX_BACKEND=trace (override do "metal"
                                herdado), o que faz rsx_bridge_mode()
                                registar o backend de logging leve
                                (s_trace_backend) -- confirmado in-boot pelo
                                banner "[rsx-bridge] trace backend active (no
                                window)" -- e NUNCA abrir janela nenhuma.

                                Consequencia directa, tambem confirmada
                                in-boot: o backend trace nao implementa
                                present() (so' set_shader/clear/draw_arrays/
                                draw_indexed), por isso RSX/PRESENT NUNCA
                                dispara neste harness -- frames_apresentados
                                fica sempre 0. Isto e' uma MEDICAO REAL ("zero
                                presents aconteceram no host neste run"), NAO
                                um valor fabricado (regra 4 CLAUDE.md) nem
                                equivalente a `null` (ao contrario de
                                instrucoes_desconhecidas, que nao tem fonte
                                NENHUMA -- aqui a fonte existe, so' que o
                                evento que ela conta nao e' emitido por este
                                backend). O sinal real de que o boot concluiu
                                flips headless e' RSX/FLIP_REQ + RSX/FLIP_DONE
                                (emitidos por rsx_process_fifo() e
                                cellGcmSetFlipCommand independentemente do
                                backend escolhido, ANTES de qualquer chamada a
                                present()) -- ambos disparam normalmente sob
                                PS3_RSX_BACKEND=trace, em numero identico um
                                ao outro (confirmado in-boot -- ver
                                .superpowers/sdd/task-7-report.md para as
                                contagens exactas do smoke real, com 0
                                RSX/PRESENT). Se uma task futura
                                precisar do numero REAL de presents (nao so'
                                de flips completados), tem de correr
                                windowed (PS3_RSX_BACKEND=metal/d3d12/null) --
                                o que deixa de ser headless por definicao, e
                                portanto fora do escopo deste harness de
                                baseline.

                                Decisao de schema (Task 7 fix): o schema
                                ps3recomp.baseline/1 do brief da Task 7 e'
                                "exacto" (JSON literal, sem campos extra) e
                                Task 8 valida contra ele -- por isso NAO foi
                                adicionado um campo `flips_completados` (que
                                seria FLIP_DONE contado). A contagem de flips
                                fica documentada aqui e no cabecalho de
                                scripts/baseline_run.sh, mas nao no JSON.
                                Um run onde a FIFO/flip nunca e alcancada
                                tambem produz honestamente frames_apresentados
                                =0 (pelo mesmo motivo -- sem flip, sem
                                present) -- NAO e' tratado como bug por este
                                relatorio nenhum dos dois casos.

--------------------------------------------------------------------------
`barreira.igual_nos_runs` (ambiguidade resolvida pelo brief da Task 7):
verdadeiro sse TODOS os runs pararam no MESMO `ultimo_checkpoint` (nome) E
com o MESMO `primeiro_erro` (por identidade normalizada, nao texto/tb/
enderecos). Implementado chamando `trace_parse.compute_diff(run_i, run_1)`
sobre os dicts CRUS devolvidos por `parse_log()` (nao os campos ja reduzidos
do schema) e verificando que NEM "ultima_transicao_estado" NEM
"primeiro_erro" aparecem no diff -- as outras cinco chaves que compute_diff
tambem compara (threads_bloqueadas, dma_pendentes, fios_pendentes,
ultimo_flip_present, primeiro_ea_invalido) sao ignoradas para este
julgamento, porque o brief da Task 7 define barreira apenas em termos de
checkpoint + erro, nao do resto do estado (que pode divergir por jitter de
timing entre boots sem ser uma "barreira" diferente).

Quando `igual_nos_runs` sai falso, `variacao_explicada` TEM de vir
preenchido (`--variacao-explicada TEXT` na CLI) -- e' um campo que exige
explicacao HUMANA, nao algo que este script possa inferir sozinho (o brief
diz literalmente "tem de ser preenchido por quem corre"). Sem essa flag,
`main()` aborta com um erro explicito em vez de escrever `variacao_explicada:
null` com `igual_nos_runs: false` (proibido pelo schema) ou inventar uma
explicacao (proibido pela regra 4 do CLAUDE.md).
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys
from datetime import datetime, timezone
from typing import Any, Dict, List, Optional, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import trace_parse  # noqa: E402  (import apos sys.path.insert, ver brief)

SCHEMA = "ps3recomp.baseline/1"

# Ver o cabecalho acima: unica fonte de "imports_nao_resolvidos", nao-canonica.
_UNRESOLVED_NID_MARKER = "[hle] unresolved NID"

# Chaves comparadas para decidir barreira.igual_nos_runs (subconjunto de
# trace_parse._DIFF_KEYS -- ver o paragrafo "barreira.igual_nos_runs" acima).
_BARRIER_KEYS = ("ultima_transicao_estado", "primeiro_erro")


class BaselineReportError(Exception):
    """Erro de uso/dados que main() reporta de forma limpa (sem stack trace
    de excepcao nao tratada) -- ex.: barreira falsa sem --variacao-explicada,
    ou ficheiro de meta em falta."""


def _scan_counts(log_path: str) -> Dict[str, Any]:
    """Um segundo passo de streaming sobre o log (o primeiro e' o de
    trace_parse.parse_log(), chamado a parte) para as contagens que
    parse_log() nao produz (ele devolve conjuntos PENDENTES, nunca totais).
    Reaproveita trace_parse.SENTINEL/parse_canonical_line em vez de duplicar
    o parsing da linha G1. Streaming linha a linha (nunca readlines()) --
    mesma disciplina de tools/trace_parse.py para logs grandes."""
    oob = 0
    oob_truncated_max: Optional[int] = None
    threads_criadas = 0
    jobs_spu = 0
    wads_abertos = 0
    frames_apresentados = 0
    imports_nao_resolvidos = 0

    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\r\n")
            if not line:
                continue
            if line.startswith(trace_parse.SENTINEL):
                rec = trace_parse.parse_canonical_line(line[len(trace_parse.SENTINEL):])
                if rec is None:
                    continue
                _tb, _thr, _fn, subsys, event, data = rec
                if subsys == "MEMORY" and event == "OOB":
                    oob += 1
                elif subsys == "MEMORY" and event == "OOB_TRUNCATED":
                    try:
                        n = int(data.get("count", ""))
                    except ValueError:
                        n = None
                    if n is not None:
                        oob_truncated_max = n if oob_truncated_max is None else max(oob_truncated_max, n)
                elif subsys == "THREADS" and event == "CREATE":
                    threads_criadas += 1
                elif subsys == "SPURS" and event == "TASK_START":
                    jobs_spu += 1
                elif subsys == "FIOS" and event == "OP_BEGIN" and data.get("wad") == "1":
                    wads_abertos += 1
                elif subsys == "RSX" and event == "PRESENT":
                    frames_apresentados += 1
                continue
            # Linha ad-hoc: so nos interessa o marcador de NID nao resolvido.
            if _UNRESOLVED_NID_MARKER in line:
                imports_nao_resolvidos += 1

    if oob_truncated_max is not None:
        oob = oob_truncated_max

    return {
        "oob": oob,
        "threads_criadas": threads_criadas,
        "jobs_spu": jobs_spu,
        "wads_abertos": wads_abertos,
        "frames_apresentados": frames_apresentados,
        "imports_nao_resolvidos": imports_nao_resolvidos,
    }


def compute_run_metrics(log_path: str) -> Tuple[Dict[str, Any], Dict[str, Any]]:
    """Devolve (metrics, raw_parse). `metrics` tem as nove chaves do schema
    por-run que sao derivaveis de UM log (falta `idx` e `duracao_s`, que o
    caller preenche a partir do meta do harness). `raw_parse` e' o dict
    completo de trace_parse.parse_log() -- guardado a parte para
    compute_diff() decidir barreira.igual_nos_runs sem reler o ficheiro."""
    raw = trace_parse.parse_log(log_path)
    counts = _scan_counts(log_path)

    ultima = raw.get("ultima_transicao_estado")
    ultimo_checkpoint = ultima.get("nome") if isinstance(ultima, dict) else None

    metrics = {
        "primeiro_erro": raw.get("primeiro_erro"),
        "ultimo_checkpoint": ultimo_checkpoint,
        "oob": counts["oob"],
        "imports_nao_resolvidos": counts["imports_nao_resolvidos"],
        # Ver o cabecalho do ficheiro: grandeza de lift-time, nunca 0 fabricado.
        "instrucoes_desconhecidas": None,
        "threads_criadas": counts["threads_criadas"],
        "jobs_spu": counts["jobs_spu"],
        "wads_abertos": counts["wads_abertos"],
        "frames_apresentados": counts["frames_apresentados"],
    }
    return metrics, raw


def _describe_barrier(igual: bool, raw_runs: List[Dict[str, Any]]) -> str:
    if not raw_runs:
        return "nenhum run fornecido"
    ultima = raw_runs[0].get("ultima_transicao_estado")
    nome = ultima.get("nome") if isinstance(ultima, dict) else None
    erro = raw_runs[0].get("primeiro_erro")
    erro_txt = "sem erro" if erro is None else f"erro em: {erro.get('texto', '?')!r}"
    if igual:
        return f"{len(raw_runs)} run(s) pararam em ultimo_checkpoint={nome!r} ({erro_txt})"
    return (f"os runs NAO pararam todos no mesmo estado (referencia: "
            f"ultimo_checkpoint={nome!r}, {erro_txt}) -- ver variacao_explicada")


def build_baseline(
    jogo: str,
    log_paths: List[str],
    durations: List[float],
    provenance: Dict[str, Any],
    env: Dict[str, str],
    variacao_explicada: Optional[str] = None,
    gerado_em: Optional[str] = None,
) -> Dict[str, Any]:
    """Funcao pura (sem I/O de meta files) -- e' o que tools/test_baseline_report.py
    exercita directamente. `provenance` tem de trazer as chaves
    commit_engine/commit_jogo/binario (dict path/sha256/mtime); `env` e' o
    dict de variaveis PS3_* efectivamente activas no run (G8)."""
    if len(log_paths) != len(durations):
        raise BaselineReportError(
            f"log_paths ({len(log_paths)}) e durations ({len(durations)}) com tamanhos diferentes")
    if not log_paths:
        raise BaselineReportError("nenhum log de run fornecido")

    runs = []
    raw_runs = []
    for idx, (log_path, duration) in enumerate(zip(log_paths, durations), start=1):
        metrics, raw = compute_run_metrics(log_path)
        run_obj = {"idx": idx, "duracao_s": duration}
        run_obj.update(metrics)
        runs.append(run_obj)
        raw_runs.append(raw)

    igual = True
    for raw in raw_runs[1:]:
        diff = trace_parse.compute_diff(raw, raw_runs[0])
        if any(k in diff for k in _BARRIER_KEYS):
            igual = False
            break

    if not igual and not variacao_explicada:
        raise BaselineReportError(
            "barreira.igual_nos_runs = false mas --variacao-explicada nao foi "
            "fornecido -- o brief da Task 7 exige que este campo seja preenchido "
            "por quem corre; recusando escrever um baseline.json com "
            "variacao_explicada:null nesse caso (ver secao 'barreira.igual_nos_runs' "
            "do cabecalho deste ficheiro)")

    barreira = {
        "igual_nos_runs": igual,
        "descricao": _describe_barrier(igual, raw_runs),
        "variacao_explicada": variacao_explicada if not igual else None,
    }

    return {
        "schema": SCHEMA,
        "jogo": jogo,
        "gerado_em": gerado_em or datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "commit_engine": provenance.get("commit_engine"),
        "commit_jogo": provenance.get("commit_jogo"),
        "binario": provenance.get("binario", {}),
        "env": env,
        "runs": runs,
        "barreira": barreira,
    }


# --------------------------------------------------------------------------
# CLI: le os artefactos que scripts/baseline_run.sh escreveu em --out DIR.
# --------------------------------------------------------------------------

def _read_kv_file(path: str) -> Dict[str, str]:
    out: Dict[str, str] = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, _, v = line.partition("=")
            out[k] = v
    return out


_RUN_LOG_RE = re.compile(r"run_(\d+)\.log$")


def _discover_runs(out_dir: str) -> List[Tuple[int, str, str]]:
    """Devolve [(idx, log_path, meta_path), ...] ordenado numericamente por
    idx (glob + sort lexical falharia em run_10.log vindo antes de run_2.log)."""
    found = []
    for log_path in glob.glob(os.path.join(out_dir, "run_*.log")):
        m = _RUN_LOG_RE.search(os.path.basename(log_path))
        if not m:
            continue
        idx = int(m.group(1))
        meta_path = os.path.join(out_dir, f"run_{idx}.meta")
        found.append((idx, log_path, meta_path))
    found.sort(key=lambda t: t[0])
    return found


def _load_from_out_dir(out_dir: str) -> Tuple[List[str], List[float], Dict[str, Any], Dict[str, str]]:
    runs = _discover_runs(out_dir)
    if not runs:
        raise BaselineReportError(f"nenhum run_N.log encontrado em {out_dir!r}")

    log_paths: List[str] = []
    durations: List[float] = []
    for idx, log_path, meta_path in runs:
        if not os.path.isfile(meta_path):
            raise BaselineReportError(f"meta em falta para o run {idx}: {meta_path!r}")
        meta = _read_kv_file(meta_path)
        try:
            duration = float(meta["duration_s"])
        except (KeyError, ValueError) as exc:
            raise BaselineReportError(
                f"run_{idx}.meta sem duration_s valido ({meta_path!r}): {exc}") from exc
        log_paths.append(log_path)
        durations.append(duration)

    prov_path = os.path.join(out_dir, "provenance.meta")
    if not os.path.isfile(prov_path):
        raise BaselineReportError(f"provenance.meta em falta em {out_dir!r}")
    prov_kv = _read_kv_file(prov_path)
    provenance = {
        "commit_engine": prov_kv.get("commit_engine"),
        "commit_jogo": prov_kv.get("commit_jogo"),
        "binario": {
            "path": prov_kv.get("boot_bin"),
            "sha256": prov_kv.get("boot_sha256"),
            "mtime": prov_kv.get("boot_mtime_iso"),
        },
    }

    env_path = os.path.join(out_dir, "env.txt")
    if not os.path.isfile(env_path):
        raise BaselineReportError(f"env.txt em falta em {out_dir!r}")
    env = _read_kv_file(env_path)

    return log_paths, durations, provenance, env


def build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="baseline_report.py",
        description="Gera baseline.json (schema ps3recomp.baseline/1) a partir "
                     "dos artefactos de scripts/baseline_run.sh (Task 7).",
    )
    p.add_argument("--out", required=True,
                    help="directorio --out usado por baseline_run.sh (run_N.log/"
                         "run_N.meta/provenance.meta/env.txt)")
    p.add_argument("--json-out", default=None,
                    help="onde escrever baseline.json (default: <out>/baseline.json)")
    p.add_argument("--jogo", default="gow2")
    p.add_argument("--variacao-explicada", default=None,
                    help="obrigatorio SSE os runs divergirem em checkpoint/erro "
                         "(barreira.igual_nos_runs=false) -- preenchido por quem corre")
    return p


def main(argv: Optional[List[str]] = None) -> int:
    args = build_arg_parser().parse_args(argv)

    try:
        log_paths, durations, provenance, env = _load_from_out_dir(args.out)
        baseline = build_baseline(
            jogo=args.jogo,
            log_paths=log_paths,
            durations=durations,
            provenance=provenance,
            env=env,
            variacao_explicada=args.variacao_explicada,
        )
    except BaselineReportError as exc:
        print(f"baseline_report: {exc}", file=sys.stderr)
        return 2

    json_out = args.json_out or os.path.join(args.out, "baseline.json")
    with open(json_out, "w", encoding="utf-8") as f:
        json.dump(baseline, f, indent=2, ensure_ascii=False, sort_keys=False)
        f.write("\n")

    print(f"baseline_report: escrito {json_out} "
          f"({len(baseline['runs'])} run(s), igual_nos_runs="
          f"{baseline['barreira']['igual_nos_runs']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
