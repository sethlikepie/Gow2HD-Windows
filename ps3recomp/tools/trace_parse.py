#!/usr/bin/env python3
"""tools/trace_parse.py -- Task 6 do plano `bringup-instrumentacao-fase0`.

Parser das linhas canonicas `[PS3T] ...` (contrato G1 do plano, secao
"Restricoes globais") emitidas pelas Tasks 1-5. Produz exactamente as oito
derivacoes exigidas pelo plano de origem (docs/superpowers/plans/
2026-07-24-bringup-instrumentacao-fase0.md, linhas 111-120):

    primeiro_erro | primeiro_ea_invalido | ultima_transicao_estado |
    threads_bloqueadas | dma_pendentes | fios_pendentes |
    ultimo_flip_present | diff_baseline

Mais uma nona chave `linhas_ignoradas` (requisito 5 do brief da Task 6:
robustez -- uma linha malformada nao pode rebentar o parser, tem de contar
para um contador exposto no output).

Formato canonico consumido (G1, ver include/ps3_trace.h):

    [PS3T] <tb> | <thr> | <fn> | <subsys> | <event> | <data>

- separador exacto: " | " (espaco, pipe, espaco); nenhum campo contem "|".
- <fn> pode ser um simbolo C++ mangled com offset (ex.:
  "_Z13func_00263554P11ppu_context+0x1A8") -- tratado como string opaca, nunca
  parseado.
- <data> e' "-" (sem pares) ou pares "k=v" separados por espaco simples; um
  valor pode ser vazio (ex. "name=" quando o guest nao passou nome de
  thread) -- parse_kv() usa str.partition("=") para nunca confundir isso com
  ausencia do par.

Sentinela: o prefixo "[PS3T] " e' o que separa estas linhas das ~64 sondas
ad-hoc pre-existentes no mesmo stderr (interpretacao nossa do requisito de
"logs devem conter..." do plano de origem -- ver G1). Uma linha SEM esse
prefixo nunca e' tratada como malformada -- e' simplesmente uma linha ad-hoc,
usada apenas para a deteccao de `primeiro_erro` abaixo.

--------------------------------------------------------------------------
Heuristica de `primeiro_erro` (ambiguidade resolvida pelo brief da Task 6 --
"documentar, no cabecalho do ficheiro, exactamente que padroes conta como
erro e porque"; "seja conservador e explicito"):

1) Linhas CANONICAS: apenas `MEMORY/OOB` conta como erro. E' o unico evento
   canonico, entre os produzidos pelas Tasks 1-5, que representa
   inequivocamente uma condicao de erro (acesso de memoria guest fora dos
   limites). Especificamente EXCLUIDO: `MEMORY/OOB_TRUNCATED` (e' so' um
   marco periodico de contagem de OOBs ja contados antes, "count=%llu", sem
   `ea=` -- nao e' uma nova ocorrencia de erro) e `THREADS/WAKE
   result=timeout|result=esrch` (sao desfechos legitimos de uma espera
   limitada, nao erros -- ver o comentario em runtime/syscalls/sys_mutex.c
   sobre o WAKE de timeout ser o PAR esperado do BLOCK, nao uma falha).

2) Linhas AD-HOC (sem sentinela): procuramos, case-insensitive, por um
   conjunto pequeno e deliberadamente CURTO de tokens que, auditados via
   `grep` no corpo real das ~64 sondas ad-hoc deste repositorio
   (runtime/, libs/), marcam sempre uma condicao de falha/abort genuina,
   nunca logging rotineiro:

       FATAL     -- "[ppu] FATAL: stuck calling ... -- aborting run"
       CRASH     -- "[SPUCRASH] code=...", "\n[CRASH] code=..."
       DEADLOCK  -- "[lockorder] DEADLOCK: ..."
       PANIC     -- reservado (nenhuma sonda actual usa, mantido por
                    simetria com o vocabulario de erro do resto do C runtime)
       FAILED    -- "[cellVdec] CreateThread failed", "[sys_fs] open FAILED",
                    "[prx] load failed: ...", "VirtualAlloc commit failed"
       INVALIDO  -- "[LWCS] signal EA INVALIDO ...", "[rig] parent invalido"

   Deliberadamente EXCLUIDO: a palavra solta "FAIL" (sem sufixo). E' o
   prefixo usado pelos HARNESSES DE TESTE deste repo (`FAIL: %s\\n`,
   `FAIL %s:%d: `, "%d passed, %d failed" em tools/test_*.py e nos binarios
   .exe de runtime/*/tests/) para reportar falhas de asserção -- ruido de
   ferramenta, nao do boot do jogo. Incluir "FAIL" teria acendido em quase
   todas as linhas desses harnesses (o proprio "heuristica que marca metade
   do log e' inutil" que o brief pede para evitar). "FAILED" (o particulio
   passado, com o sufixo -ED) e' o idioma real usado pelas sondas ad-hoc de
   runtime/libs para falhas operacionais -- tem alta precisao sem essa
   colisao.
--------------------------------------------------------------------------

Robustez: o ficheiro e' lido linha a linha (`for line in f`), NUNCA
`readlines()` -- um log de 100 MB nao e' carregado inteiro em memoria. O
estado mantido em memoria e' O(numero de threads/tags/ops em aberto), nao
O(numero de linhas).
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from typing import Any, Dict, List, Optional

SENTINEL = "[PS3T] "

# Tokens ad-hoc de erro -- ver a heuristica documentada no cabecalho acima.
_ADHOC_ERROR_TOKENS = ("FATAL", "CRASH", "DEADLOCK", "PANIC", "FAILED", "INVALIDO", "INVÁLIDO")
_ADHOC_ERROR_RE = re.compile("|".join(_ADHOC_ERROR_TOKENS), re.IGNORECASE)

# As oito chaves exactas exigidas pelo plano (mais linhas_ignoradas, requisito 5).
DERIVATION_KEYS = (
    "primeiro_erro",
    "primeiro_ea_invalido",
    "ultima_transicao_estado",
    "threads_bloqueadas",
    "dma_pendentes",
    "fios_pendentes",
    "ultimo_flip_present",
    "diff_baseline",
)

# Chaves comparadas por diff_baseline -- as sete derivacoes que descrevem o
# ESTADO do run (exclui diff_baseline, que seria auto-referente, e
# linhas_ignoradas, que e' uma metrica de qualidade DESTE parse, nao uma
# caracteristica do run a comparar com o baseline aprovado).
_DIFF_KEYS = (
    "primeiro_erro",
    "primeiro_ea_invalido",
    "ultima_transicao_estado",
    "threads_bloqueadas",
    "dma_pendentes",
    "fios_pendentes",
    "ultimo_flip_present",
)


def is_adhoc_error(line: str) -> bool:
    """True se `line` (uma linha SEM a sentinela [PS3T]) bate a heuristica
    de erro documentada no cabecalho deste ficheiro."""
    return _ADHOC_ERROR_RE.search(line) is not None


def parse_kv(data: str) -> Dict[str, str]:
    """Faz parse do campo <data> (pares "k=v" separados por espaco, ou "-").

    Um valor vazio (ex. "name=") tem de produzir {"name": ""}, nunca
    excepcao nem perda silenciosa do par -- quirk #2 do brief da Task 6.
    Tokens sem "=" sao ignorados (robustez -- nunca rebenta)."""
    if not data or data == "-":
        return {}
    out: Dict[str, str] = {}
    for tok in data.split(" "):
        if not tok:
            continue
        if "=" in tok:
            k, _, v = tok.partition("=")
            if k:
                out[k] = v
    return out


def parse_canonical_line(body: str):
    """`body` e' a linha JA' sem o prefixo "[PS3T] ". Devolve
    (tb:int, thr:str, fn:str, subsys:str, event:str, data:dict) ou None se a
    linha estiver malformada (menos/mais de 6 campos, ou <tb> nao numerico) --
    o CALLER incrementa linhas_ignoradas nesse caso, nunca rebenta."""
    parts = body.split(" | ", 5)
    if len(parts) != 6:
        return None
    tb_s, thr, fn, subsys, event, data_s = parts
    try:
        tb = int(tb_s)
    except ValueError:
        return None
    if not subsys or not event:
        return None
    return tb, thr, fn, subsys, event, parse_kv(data_s)


def parse_log(path: str) -> Dict[str, Any]:
    """Faz streaming de `path` linha a linha e devolve o dict com as nove
    chaves (oito derivacoes + linhas_ignoradas). `diff_baseline` sai sempre
    None aqui -- quem quiser a oitava derivacao chama compute_diff() depois
    (ver main()), porque precisa de um segundo ficheiro (--baseline)."""
    linhas_ignoradas = 0
    primeiro_erro: Optional[Dict[str, Any]] = None
    primeiro_ea: Optional[Dict[str, Any]] = None
    ultima_cp: Optional[Dict[str, Any]] = None

    # tid -> {"tid","on","obj","tb"} enquanto nao houver WAKE/EXIT desse tid.
    threads_pending: Dict[str, Dict[str, Any]] = {}

    # tag -> entrada aberta (GET/PUT sem DONE); dma_reused acumula as
    # entradas fechadas por REUTILIZACAO de tag (requisito 6).
    dma_pending: Dict[str, Dict[str, Any]] = {}
    dma_reused: List[Dict[str, Any]] = []

    # op -> entrada aberta (OP_BEGIN sem OP_END); fios_reused idem (requisito
    # 6 tambem exige tolerancia a reutilizacao para FIOS, nao so' DMA).
    fios_pending: Dict[str, Dict[str, Any]] = {}
    fios_reused: List[Dict[str, Any]] = []

    flip: Dict[str, Optional[Dict[str, Any]]] = {
        "flip_req": None,
        "flip_done": None,
        "present": None,
    }

    def handle_line(line: str, line_no: int) -> None:
        nonlocal linhas_ignoradas, primeiro_erro, primeiro_ea, ultima_cp

        if line.startswith(SENTINEL):
            rec = parse_canonical_line(line[len(SENTINEL):])
            if rec is None:
                linhas_ignoradas += 1
                return
            tb, thr, fn, subsys, event, data = rec

            if subsys == "MEMORY" and event == "OOB":
                if primeiro_erro is None:
                    primeiro_erro = {"tb": tb, "texto": line, "linha": line_no}
                ea = data.get("ea")
                if ea is not None and (primeiro_ea is None or tb < primeiro_ea["tb"]):
                    primeiro_ea = {"ea": ea, "tb": tb, "thr": thr, "fn": fn}
                return

            if subsys == "CHECKPOINTS":
                ultima_cp = {"nome": event, "tb": tb}
                return

            if subsys == "THREADS":
                tid = data.get("tid")
                if tid is None:
                    return
                if event == "BLOCK":
                    threads_pending[tid] = {
                        "tid": tid, "on": data.get("on"), "obj": data.get("obj"), "tb": tb,
                    }
                elif event in ("WAKE", "EXIT"):
                    threads_pending.pop(tid, None)
                return

            if subsys == "DMA":
                if event in ("GET", "PUT"):
                    tag = data.get("tag")
                    if tag is None:
                        return
                    old = dma_pending.pop(tag, None)
                    if old is not None:
                        old["status"] = "reutilizado"
                        dma_reused.append(old)
                    dma_pending[tag] = {
                        "tag": tag, "ea": data.get("ea"), "lsa": data.get("lsa"),
                        "size": data.get("size"), "status": "pendente",
                    }
                elif event == "DONE":
                    tag = data.get("tag")
                    if tag is not None:
                        dma_pending.pop(tag, None)
                # LIST e TAG_WAIT nunca participam do emparelhamento --
                # quirk #5 do brief. TAG_WAIT nao tem tag= (so' mask=, um
                # bitmask, ver spu_dma.h mfc_tag_wait). LIST TEM tag= (ver
                # spu_dma.h mfc_do_list_transfer) -- a razao para excluir e'
                # outra: LIST descreve so' a transferencia scatter/gather em
                # si (ea_base/list_size), e cada um dos seus elementos
                # reentra em mfc_do_transfer() e emite o seu proprio par
                # GET/PUT+DONE, que ja e' emparelhado normalmente por aqui.
                return

            if subsys == "FIOS":
                if event == "OP_BEGIN":
                    op = data.get("op")
                    if op is None:
                        return
                    old = fios_pending.pop(op, None)
                    if old is not None:
                        old["status"] = "reutilizado"
                        fios_reused.append(old)
                    fios_pending[op] = {
                        "op": op, "path": data.get("path"), "state": data.get("state"),
                        "status": "pendente",
                    }
                elif event == "OP_END":
                    op = data.get("op")
                    if op is not None:
                        fios_pending.pop(op, None)
                # PARTIAL/READ/CALLBACK sao actividade intermedia, nunca
                # abrem/fecham um par OP_BEGIN/OP_END.
                return

            if subsys == "RSX":
                if event == "FLIP_REQ":
                    flip["flip_req"] = {"buf": data.get("buf"), "tb": tb}
                elif event == "FLIP_DONE":
                    flip["flip_done"] = {"buf": data.get("buf"), "tb": tb}
                elif event == "PRESENT":
                    flip["present"] = {"buf": data.get("buf"), "tb": tb}
                return
            return

        # Linha ad-hoc (sem sentinela) -- nunca conta para linhas_ignoradas
        # (nao e' suposto seguir o formato G1); so' interessa a
        # primeiro_erro.
        if primeiro_erro is None and is_adhoc_error(line):
            primeiro_erro = {"tb": None, "texto": line, "linha": line_no}

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line_no, raw in enumerate(f, start=1):
            line = raw.rstrip("\r\n")
            if not line:
                continue
            handle_line(line, line_no)

    def sort_key_tb(entry: Dict[str, Any]):
        tb = entry.get("tb")
        return tb if isinstance(tb, int) else -1

    threads_bloqueadas = sorted(threads_pending.values(), key=sort_key_tb)
    dma_pendentes = dma_reused + list(dma_pending.values())
    fios_pendentes = fios_reused + list(fios_pending.values())

    return {
        "primeiro_erro": primeiro_erro,
        "primeiro_ea_invalido": primeiro_ea,
        "ultima_transicao_estado": ultima_cp,
        "threads_bloqueadas": threads_bloqueadas,
        "dma_pendentes": dma_pendentes,
        "fios_pendentes": fios_pendentes,
        "ultimo_flip_present": flip,
        "diff_baseline": None,
        "linhas_ignoradas": linhas_ignoradas,
    }


def _normalize_primeiro_erro(entry: Optional[Dict[str, Any]]) -> Any:
    """Identidade estavel de `primeiro_erro`: so' a CLASSIFICACAO -- houve
    erro? de que tipo? -- nunca o texto cru (que embrulha `tb`, enderecos e o
    numero de linha, todos volateis entre dois boots distintos). Espelha o
    que o plano de origem pede para `barreira` (linha 672): "comparado por
    forma normalizada, nao por texto exacto com enderecos".

    Canonico (MEMORY/OOB) -- unico evento canonico que dispara primeiro_erro
    -- classifica sempre como "MEMORY/OOB". Ad-hoc -- classifica pelo token
    da heuristica (_ADHOC_ERROR_RE) que bateu primeiro no texto cru; o TOKEN
    e' estavel (e' vocabulario fixo da sonda no codigo-fonte), o texto a
    volta dele (enderecos, ids) nao e'."""
    if entry is None:
        return None
    texto = entry.get("texto") or ""
    if texto.startswith(SENTINEL):
        return {"tipo": "canonico", "classe": "MEMORY/OOB"}
    m = _ADHOC_ERROR_RE.search(texto)
    return {"tipo": "adhoc", "classe": m.group(0).upper() if m else None}


def _normalize_primeiro_ea(entry: Optional[Dict[str, Any]]) -> Any:
    """Identidade estavel de `primeiro_ea_invalido`: (`ea`, `fn`) -- o QUE e
    ONDE, nunca `tb`/`thr`. `fn` e' mantido POR INTEIRO, incluindo o offset
    "+0x...": esse offset vem de `dladdr()` sobre o return address do
    chamador (ver runtime/trace/ps3_mem_trace.c) e e' a distancia, em bytes,
    entre esse RA e o inicio do simbolo (`dli_saddr`) -- ambos deslocados
    igualmente por qualquer base de carregamento do binario, por isso o
    offset em si nao carrega endereco nenhum, e' uma propriedade ESTATICA do
    binario recompilado (qual instrucao, dentro daquela funcao liftada, fez o
    acesso). Para duas execucoes do MESMO binario a atingirem o mesmo estado,
    fn+offset e' bit-a-bit identico -- ao contrario de `obj=`/`ea=` (paths
    abaixo), que sao enderecos GUEST crus e por isso dependentes de ordem de
    alocacao. `ea` fica como esta' (e' precisamente o "onde" que queremos
    comparar)."""
    if entry is None:
        return None
    return (entry.get("ea"), entry.get("fn"))


def _normalize_ultima_transicao(entry: Optional[Dict[str, Any]]) -> Any:
    """Identidade estavel de `ultima_transicao_estado`: so' `nome` (o
    checkpoint atingido). `tb` e' o timebase counter do momento em que o
    checkpoint disparou -- varia por definicao entre dois boots reais (timing
    diferente), mesmo quando os dois atingem exactamente o mesmo checkpoint."""
    if entry is None:
        return None
    return entry.get("nome")


def _normalize_threads_bloqueadas(entries: List[Dict[str, Any]]) -> Any:
    """Identidade estavel de `threads_bloqueadas`: o conjunto de (`tid`,
    `on`). ESCOLHA: `tid`, nao `obj`.

    `tid` (ver runtime/syscalls/sys_ppu_thread.c:233-234, `thread_id = slot +
    1`) e' o indice+1 de uma tabela FIXA (`g_ppu_threads[PPU_THREAD_MAX]`) --
    um HANDLE pequeno atribuido pela ORDEM de criacao das threads guest, nao
    um endereco. Para duas execucoes que seguem o mesmo caminho de codigo
    guest ate' ao mesmo estado, essa ordem e' deterministica (e' logica do
    jogo, nao timing do host) -- o mesmo padrao que o `tag` do DMA (ver
    `_normalize_dma_pendentes` abaixo).

    `obj` e' o `0x%08X` cru do endereco guest do mutex/cond/sema/etc (ver
    `sys_mutex.c:210`, "obj=0x%08X"). E' um endereco GUEST verdadeiro --
    estatico se o objecto for global, mas dinamico (heap) na maioria dos
    casos reais do jogo -- e a ordem de alocacao desses objectos pode
    divergir entre dois boots por causas nada relacionadas com o `bug` que
    diff_baseline quer apanhar (jitter de escalonamento entre threads guest
    concorrentes a alocar). Por isso `obj` fica de fora da identidade,
    exactamente como o achado da review pede.

    `on` (o RECURSO -- "mutex"/"cond"/"sema"/"join"/"eventqueue") entra
    porque e' uma STRING FIXA do codigo-fonte (nunca um endereco) e distingue
    duas threads com o mesmo `tid` bloqueadas em ocasioes diferentes (nao
    acontece na pratica, e' defensivo)."""
    return frozenset((e.get("tid"), e.get("on")) for e in entries)


def _normalize_dma_pendentes(entries: List[Dict[str, Any]]) -> Any:
    """Identidade estavel de `dma_pendentes`: o conjunto de (`tag`,
    `status`). ESCOLHA: `tag`, SEM `ea`.

    `tag` (`spu->mfc_tag`, mascarado 0-31 no MFC_TagID -- ver
    runtime/spu/spu_dma.h:214) e' um id pequeno de canal DMA atribuido pela
    LOGICA do jogo/job SPU, do mesmo genero que `tid` acima -- estavel entre
    execucoes que atingem o mesmo estado.

    `ea` (`spu_dma.h:214`, "ea=0x%08X") e' o endereco GUEST real da
    transferencia (memoria principal) -- mesma classe de volatilidade que
    `obj` em threads_bloqueadas (ordem de alocacao do heap guest, jitter
    entre jobs SPU concorrentes). Fica de fora, conforme o achado da review
    ("tag, e ea SE estavel" -- julgo que nao e').

    `status` ("pendente" vs "reutilizado") entra: distingue uma tag ainda em
    transito de uma que ja foi fechada por reutilizacao antes do DONE -- essa
    e' uma diferenca de COMPORTAMENTO real entre dois runs (nao ruido de
    endereco/tb), por isso deve continuar a ser reportada pelo diff."""
    return frozenset((e.get("tag"), e.get("status")) for e in entries)


def _normalize_fios_pendentes(entries: List[Dict[str, Any]]) -> Any:
    """Identidade estavel de `fios_pendentes`: o conjunto de (`path`,
    `state`). NUNCA `op` (o `fd` do host devolvido por `cellFsOpen` -- ver
    runtime/ppu/ppu_fs.cpp:352, "op=%d path=%s" com `fd` literal -- e' um
    handle atribuido pelo SO/CELL consoante que OUTROS ficheiros estao
    abertos nesse momento, ja sinalizado pela review como dominio-variavel) e
    nunca `tb`. `path` e' o caminho guest resolvido (string fixa determinada
    pelo jogo, nao endereco) -- a identidade mais natural de uma operacao de
    IO. `state` entra porque tambem e' uma string fixa do codigo-fonte
    (open/closed/...) e distingue operacoes diferentes sobre o mesmo path."""
    return frozenset((e.get("path"), e.get("state")) for e in entries)


def _normalize_ultimo_flip_present(entry: Optional[Dict[str, Any]]) -> Any:
    """Identidade estavel de `ultimo_flip_present`: so' PRESENCA/AUSENCIA de
    cada sub-evento (flip_req/flip_done/present) -- "o run chegou ate' ali?"
    -- nunca `tb` nem `buf` (o indice do buffer de flip, que pode variar
    consoante o numero de flips ja' feitos ate' esse ponto, timing-dependente
    entre boots)."""
    if entry is None:
        return {"flip_req": False, "flip_done": False, "present": False}
    return {k: entry.get(k) is not None for k in ("flip_req", "flip_done", "present")}


# Normalizador de cada chave de _DIFF_KEYS -- usado SO' para decidir
# igualdade dentro de compute_diff (a seccao "O fix" do achado da review:
# "Keep the full derivations ... unchanged. Only the comparison ...
# normalizes."). O valor cru (com tb/enderecos) continua a ser o que aparece
# em diffs[key]["baseline"/"atual"] -- so' o CRITERIO de igualdade muda.
_DIFF_NORMALIZERS = {
    "primeiro_erro": _normalize_primeiro_erro,
    "primeiro_ea_invalido": _normalize_primeiro_ea,
    "ultima_transicao_estado": _normalize_ultima_transicao,
    "threads_bloqueadas": _normalize_threads_bloqueadas,
    "dma_pendentes": _normalize_dma_pendentes,
    "fios_pendentes": _normalize_fios_pendentes,
    "ultimo_flip_present": _normalize_ultimo_flip_present,
}


def compute_diff(current: Dict[str, Any], baseline: Dict[str, Any]) -> Dict[str, Any]:
    """Diff estrutural entre o run actual e um `baseline` aprovado (o proprio
    dict que uma execucao anterior de `--json` teria produzido). Para cada
    uma das sete chaves de estado (_DIFF_KEYS), compara pela IDENTIDADE
    ESTAVEL de cada derivacao (ver `_DIFF_NORMALIZERS` acima) -- nao pelo
    dict inteiro. Isto e' deliberado: cada derivacao embrulha `tb`
    (timebase, sempre diferente entre dois boots reais) e/ou enderecos
    guest (`obj`/`ea`/`buf`) que dois boots DISTINTOS a atingir o MESMO
    estado nunca vao repetir bit-a-bit -- comparar os dicts inteiros
    (comportamento anterior) fazia `diff_baseline` acender ruido em quase
    todas as chaves mesmo sem regressao nenhuma (achado da review pos-Task
    6). Quando a identidade normalizada difere, regista {"baseline": <valor
    CRU antigo>, "atual": <valor CRU novo>} -- o valor reportado continua
    completo (com tb/enderecos), so' o CRITERIO de igualdade e' normalizado.
    Vazio {} quando os dois runs sao identicos nessas chaves (T6.8 e o novo
    test_diff_ignores_volatile_fields)."""
    diffs: Dict[str, Any] = {}
    for key in _DIFF_KEYS:
        old = baseline.get(key)
        new = current.get(key)
        normalize = _DIFF_NORMALIZERS[key]
        if normalize(old) != normalize(new):
            diffs[key] = {"baseline": old, "atual": new}
    return diffs


def _load_baseline(path: str) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8") as f:
        obj = json.load(f)
    if not isinstance(obj, dict):
        raise ValueError(f"baseline {path!r} nao e' um objecto JSON")
    return obj


def format_human(result: Dict[str, Any]) -> str:
    lines = ["=== trace_parse: resumo ==="]
    for key in DERIVATION_KEYS:
        lines.append(f"{key}: {json.dumps(result.get(key), ensure_ascii=False)}")
    lines.append(f"linhas_ignoradas: {result.get('linhas_ignoradas')}")
    return "\n".join(lines)


def build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="trace_parse.py",
        description="Parser das linhas canonicas [PS3T] (Task 6, bringup-instrumentacao-fase0).",
    )
    p.add_argument("log", help="caminho do log de run a analisar")
    p.add_argument("--json", action="store_true", help="imprime SO' o objecto JSON em stdout")
    p.add_argument("--baseline", metavar="BASELINE.json",
                    help="run aprovado (JSON produzido por uma execucao anterior com --json) "
                         "para activar a derivacao diff_baseline")
    return p


def main(argv: Optional[List[str]] = None) -> int:
    args = build_arg_parser().parse_args(argv)

    try:
        result = parse_log(args.log)
    except OSError as exc:
        print(f"trace_parse: erro a ler o log {args.log!r}: {exc}", file=sys.stderr)
        return 2

    if args.baseline:
        try:
            baseline = _load_baseline(args.baseline)
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            print(f"trace_parse: erro a ler --baseline {args.baseline!r}: {exc}", file=sys.stderr)
            return 2
        result["diff_baseline"] = compute_diff(result, baseline)

    if args.json:
        # T6.9: NADA mais pode ir para stdout neste modo.
        sys.stdout.write(json.dumps(result))
        sys.stdout.write("\n")
    else:
        print(format_human(result))

    return 0


if __name__ == "__main__":
    sys.exit(main())
