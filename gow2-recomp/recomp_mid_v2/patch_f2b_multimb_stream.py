#!/usr/bin/env python3
"""F2B multi-MB stream refill (post-SBP hang fix) + same-class audit.

Problem (2026-07-22): after WADLD-BODY #1 SBP_general (~1.1MB) with a 256KB
ring, body steps hit avail==0 and BA9F0 YIELDs without refilling. rem/header
accounting advances while the ring stays empty/desynced → next header garbage
→ BACE8 need=0x687DD790 → freelist hang.

Same-class follow-ups (audit 2026-07-22):
  - BA9BC / BA76C also waited without ensure (plateau ~1.5MB)
  - BA9F4 bare entry (sibling of BA9F0 body wait)
  - 002E1228 / 11EC / 1290 consume without refill/clamp (siblings of 1480)
  - state=3 rem=0 at FO FULL → eof_try_complete → state=0

Fix esperado no lift (re-aplicar apos re-lift):
  - f2b_stream_fill: loop until ring full or min_need met
  - f2b_stream_ensure(type_sys): refill when avail short
  - f2b_stream_eof_try_complete(type_sys): idle SM at FO EOF
  - f2b_stream_pre_consume: shared refill+clamp for ring consumers
  - ensure(+eof): BA76C, BA9BC, BAB88, BA9F0, BA9F4
  - pre_consume: 002E11EC, 002E1228, 002E1290, 002E1480

Markers: F2B multi-MB fix, F2B-STREAM-ENSURE, F2B-STREAM-CLAMP,
         F2B-STREAM-EOF-DONE, f2b_stream_pre_consume
Usage: python3 recomp_mid_v2/patch_f2b_multimb_stream.py [DIR_DE_LIFT]

ESTADO 2026-07-25 -- VERIFICADOR PURO, ORFAO SEM ESCRITOR
--------------------------------------------------------
Este ficheiro NAO escreve nada (grep write_text/.write(/open-w = 0): so'
confirma marcadores e presenca de chamadas dentro de corpos de funcao. O
comportamento que verifica NUNCA teve script escritor -- foi edicao manual de
sessao no lift gitignored:

  grep -l 'f2b_stream_ensure' recomp_mid_v2/*.py -> so' este ficheiro
  grep -rl 'f2b_stream_ensure' <repo>            -> este ficheiro, notes/*.md,
       lift_baseline/MANIFEST.tsv e lift_baseline/injected_001.cpp/002.cpp
       (copia FORENSE das edicoes manuais, nao um patch executavel)
  recomp_macos_v2 (lift de producao, 31 chunks)  -> presente
  lift limpo do ppu_lifter.py actual (7 chunks)  -> AUSENTE (score ok=0 fail=21)

Logo, num lift limpo estes marcadores NAO existem e este verificador TEM de
falhar (rc=2). Fazer o check passar sem o comportamento existir seria forjar
resultado (regra 4 do CLAUDE.md). O check FICA com a mesma forca: mesma lista
de marcadores, mesmas funcoes, mesmo limiar (fail==0 e ok>=12).

Correccao aplicada: so' o "chunk-fixo"
--------------------------------------
O script abria "ppu_recomp_001.cpp"/"002" pelo NOME (rc=1 se 001 faltasse) e
procurava os marcadores e as PRE_FUNCS SO' em 001. O lifter passou de 31 para
7 chunks e as funcoes migram de ficheiro a cada re-lift -- 002E1480 podia
nascer em 003 e o veredicto ficaria errado por motivo de layout, nao de
comportamento. Passa a usar resolve_lift_paths (aceita DIRECTORIO) e a
avaliar a UNIAO dos chunks: marcadores procurados em qualquer chunk, corpo de
cada funcao procurado no chunk onde ela estiver. Verificado nos dois sentidos:
lift limpo -> ok=0 fail=21 rc=2; ../recomp_macos_v2 -> rc=0.

Corrigido tambem o fim-de-regiao de _fn_body: quando a funcao era a ULTIMA do
chunk, o corpo era truncado nos primeiros 400 caracteres (numero magico), o
que podia dar "MISSING" a uma chamada que existia mais abaixo. Passa a ir ate'
ao fim do texto -- mais fiel, nunca mais permissivo do que a regiao real.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

ROOT_DEFAULT = str(Path(__file__).resolve().parent.parent / "recomp_macos_v2")

MARKERS = (
    "F2B multi-MB fix",
    "f2b_stream_ensure",
    "f2b_stream_eof_try_complete",
    "f2b_stream_pre_consume",
    "F2B-STREAM-ENSURE",
    "F2B-STREAM-CLAMP",
    "F2B-STREAM-EOF-DONE",
    "Always top-up",
)

# Wait/dispatch sites that must call ensure (type_sys in r31)
ENSURE_FUNCS = (
    "func_002BA76C",
    "func_002BA9BC",
    "func_002BAB88",
    "func_002BA9F0",
    "func_002BA9F4",
)
# Ring consumers (r3=stream, r4=need) that must pre_consume
PRE_FUNCS = (
    "func_002E11EC",
    "func_002E1228",
    "func_002E1290",
    "func_002E1480",
)
EOF_FUNCS = ("func_002BA9F0", "func_002BA9F4", "func_002BA76C", "func_002BAB88")
# Produce/init — must NOT look like wait-without-refill (documented only)
PRODUCE_OR_INIT = (
    "func_002E1254",  # avail += need (producer)
    "func_002E13C8",  # ring init
    "func_002E1424",  # ring init
)


def _fn_body(texts: dict, name: str) -> str | None:
    """Corpo da funcao no chunk onde ela existir (assinatura -> proxima 'void func_')."""
    for text in texts.values():
        m = re.search(rf"^void {name}\(ppu_context\* ctx\) \{{", text, re.M)
        if not m:
            continue
        nxt = re.search(r"^void func_", text[m.end():], re.M)
        end = m.end() + nxt.start() if nxt else len(text)
        return text[m.start():end]
    return None


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], ROOT_DEFAULT) if p.is_file()]
    if not paths:
        print("FAILED: nenhum chunk de lift encontrado")
        return 1
    texts = {p.name: p.read_text(encoding="utf-8", errors="replace") for p in paths}

    ok = 0
    fail = 0

    for m in MARKERS:
        where = [n for n, t in texts.items() if m in t]
        if where:
            print("lift: %s present (%s)" % (m, ",".join(where)))
            ok += 1
        else:
            print("lift: %s MISSING" % m)
            fail += 1

    for name in ENSURE_FUNCS:
        body = _fn_body(texts, name)
        if body is None:
            print(f"ENSURE {name}: MISSING fn")
            fail += 1
            continue
        has = "f2b_stream_ensure" in body
        print(f"ENSURE {name}: {'ok' if has else 'MISSING'}")
        if has:
            ok += 1
        else:
            fail += 1

    for name in PRE_FUNCS:
        body = _fn_body(texts, name)
        if body is None:
            print(f"PRE {name}: MISSING fn")
            fail += 1
            continue
        has = "f2b_stream_pre_consume" in body
        print(f"PRE {name}: {'ok' if has else 'MISSING'}")
        if has:
            ok += 1
        else:
            fail += 1

    # BA9F0 + BA9F4 should also eof-complete (body wait at FO end)
    for name in EOF_FUNCS:
        body = _fn_body(texts, name)
        if body and "f2b_stream_eof_try_complete" in body:
            print(f"EOF {name}: ok")
            ok += 1
        else:
            print(f"EOF {name}: MISSING")
            fail += 1

    for name in PRODUCE_OR_INIT:
        body = _fn_body(texts, name)
        if body is None:
            print(f"DOC {name}: fn missing (ok if re-lift renamed)")
            continue
        if "f2b_stream_pre_consume" in body:
            print(f"DOC {name}: unexpectedly has pre_consume (produce/init?)")
        else:
            print(f"DOC {name}: produce/init — no pre_consume (expected)")

    print(f"score ok={ok} fail={fail}")
    if fail:
        print("  ORFAO SEM ESCRITOR: nenhum patch_*.py instala f2b_stream_*; copia")
        print("  forense em ../recomp_macos_v2 e lift_baseline/injected_00{1,2}.cpp.")
        print("  O check NAO foi enfraquecido -- ver cabecalho deste ficheiro.")
    return 0 if fail == 0 and ok >= 12 else 2


if __name__ == "__main__":
    raise SystemExit(main())
