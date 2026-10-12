#!/usr/bin/env python3
"""Idempotent M2 fix: TYPE15 product+0x70 list CLOSE-PRESERVE (H1 survivor).

Problem (M1):
  ps3_type15_product_list_reset always wiped product+0x70/+0x74 to empty
  circular. Prior was_head (often 0x42F85F10) was discarded → child_head ==
  prod+0x70 after attach → CC9D0 ticks two objs with f4=f54=0 forever (H1).

Real hang fix was already in func_002A4FE4 (CLOSE-TAIL + LOOP-CAP). The
preemptive full RESET is over-aggressive.

Fix (always-on, not env-gated):
  - head 0 / poison / OOB  → empty circular (old safe path; freelist shells)
  - head already sentinel  → ensure prev=sent; return
  - head valid guest ptr   → walk next@+0; on 0/poison/OOB write next=sentinel
                            (preserve nodes); cap 64k; do NOT empty list
  - Log CLOSE-PRESERVE / RESET accordingly

Targets local lift (gitignored):
  recomp_macos_v2/ppu_recomp_001.cpp  (ps3_type15_product_list_reset)

Usage:
  python3 recomp_mid_v2/patch_type15_list_preserve.py
  python3 recomp_mid_v2/patch_type15_list_preserve.py recomp_macos_v2/ppu_recomp_001.cpp

ESTADO 2026-07-25 -- ENHANCEMENT DE 2a GERACAO SOBRE UM ORFAO SEM ESCRITOR
--------------------------------------------------------------------------
Este script REESCREVE o corpo de ps3_type15_product_list_reset. Essa funcao NAO
e' produzida pelo lifter: e' codigo host injectado a mao no lift durante uma
sessao, e nenhum patch_*.py a instala.

  grep -l 'ps3_type15_product_list_reset' recomp_mid_v2/*.py -> so' este ficheiro
  lift limpo (7 chunks)                                      -> ausente
  recomp_macos_v2 (lift de producao, 31 chunks)              -> presente (manual)
  copias versionadas do comportamento, NAO ligadas ao build:
      ../host_gow2_factory.cpp        (ja' com o corpo CLOSE-PRESERVE)
      ../lift_baseline/injected_001.cpp

Consequencia: a partir de um lift limpo nao ha' nada para reescrever, e o patch
NAO PODE aplicar. Injectar aqui a definicao "so' para dar APPLIED" seria falso:
os call sites (ps3_type15_product_list_reset(shell) dentro da factory) tambem
sao edicao manual sem escritor, logo a funcao ficaria definida e nunca chamada
-- comportamento zero com relatorio verde. Nao se faz (regra 4 do CLAUDE.md).
Para isto passar a aplicar e' preciso primeiro um patch que instale a injeccao
(funcao + call sites), a partir de host_gow2_factory.cpp / injected_001.cpp.

O que se corrigiu aqui foi so' o efeito de "chunk-fixo": o lifter passou de 31
para 7 chunks, e como resolve_lift_paths expande o DIRECTORIO para todos eles, o
script avaliava chunk-a-chunk e imprimia 7 linhas FAILED identicas para uma
unica funcao que vive (quando existe) num unico chunk. Passa a procurar a funcao
na UNIAO dos chunks e a dar um unico veredicto. Severidade inalterada: sem a
funcao, rc=1.

ADENDA 2026-07-26 -- o instalador ja' existe, e a escolha de chunk era por MENCAO
--------------------------------------------------------------------------------
O escritor que faltava passou a existir:
`patch_type15_list_close_preserve_install.py` (funcao + declaracao + call site em
func_000CB56C). Ao aplica-lo apareceu um falso FAILED aqui, com uma causa que ja'
existia contra o proprio lift de PRODUCAO:

  a escolha do chunk era `if FN_NAME in t` -- uma MENCAO. Na producao o nome
  aparece primeiro no ppu_recomp_000.cpp (declaracao extern na linha 5 + call
  site em func_000CB56C) e a DEFINICAO vive no ppu_recomp_001.cpp. Com um
  directorio em argv, escolhia-se o 000, find_fn_span devolvia None e o script
  morria com "not found (lift shape changed?)" -- mesmo com o comportamento
  todo presente.

Passa a escolher o chunk pela DEFINICAO ('...(uint32_t prod) {'). Isto NAO
enfraquece o teste: sem definicao em chunk nenhum continua a ser FAILED rc=1
(medido nos dois sentidos). So' deixa de confundir uma declaracao/chamada com a
definicao -- e' a mesma classe de correccao do "chunk-fixo" acima.
"""
from __future__ import annotations

from pathlib import Path
import re
import sys
from lift_paths import resolve_lift_paths

MARKER = "CLOSE-PRESERVE"
FN_NAME = "ps3_type15_product_list_reset"
# Assinatura da DEFINICAO (o `{` distingue-a da declaracao extern e do call site).
DEF_SIG = 'extern "C" void ' + FN_NAME + "(uint32_t prod) {"

# Full replacement body of the helper (keeps same signature / linkage).
NEW_FN = r'''/* product+0x70 is an intrusive circular list (sentinel = product+0x70).
 * func_002A5024 inits both links to self. Reused products often keep a
 * NULL-terminated pool walk (12×0xD8 nodes) that made func_002A4FE4 hang
 * (NULL → poison 0x27182818 forever).
 *
 * M2 (2026-07-23, H1 / CLOSE-PRESERVE): do NOT wipe a valid non-sentinel
 * head. Prefer close-tail preserve (same poison rules as 2A4FE4 CLOSE-TAIL).
 * Empty circular only when head is 0/bad (shells / freelist replenish). */
static int ps3_type15_list_ptr_bad(uint32_t p) {
    if (p == 0u) return 1;
    if (p < 0x10000u || p >= 0x4F000000u) return 1;
    /* poison / non-heap band seen on NULL-terminated pool tails */
    if (p >= 0x20000000u && p < 0x40000000u) return 1;
    return 0;
}
extern "C" void ps3_type15_product_list_reset(uint32_t prod) {
    if (prod < 0x10000u || prod >= 0x4F000000u) return;
    uint32_t sent = prod + 0x70u;
    uint32_t head = vm_read32(sent);
    /* Already empty circular? */
    if (head == sent && vm_read32(sent + 4u) == sent) return;

    /* Head is sentinel but prev stale → just fix prev. */
    if (head == sent) {
        vm_write32(sent + 4u, sent);
        return;
    }

    /* Head invalid → empty circular (shells, freelist template inherit). */
    if (ps3_type15_list_ptr_bad(head)) {
        vm_write32(sent + 0u, sent);
        vm_write32(sent + 4u, sent);
        { static int _n = 0;
          if (_n++ < 16)
            fprintf(stderr,
                    "[TYPE15] product list RESET prod=0x%08X was_head=0x%08X "
                    "-> circular empty (2A4FE4-safe)\n",
                    prod, head);
        }
        return;
    }

    /* Valid head: walk next pointers; close broken tail onto sentinel.
     * Do not empty the whole list (H1 fix — preserve freelist/pool nodes). */
    {
        uint32_t cur = head;
        uint32_t prev = sent;
        uint32_t nodes = 0u;
        const uint32_t k_cap = 65536u;
        for (;;) {
            if (cur == sent) {
                /* Already circular. Ensure sent->prev = last node. */
                if (prev != sent)
                    vm_write32(sent + 4u, prev);
                { static int _n = 0;
                  if (_n++ < 16)
                    fprintf(stderr,
                            "[TYPE15] product list CLOSE-PRESERVE prod=0x%08X "
                            "head=0x%08X closed_at=0x%08X nodes=%u (already circular)\n",
                            prod, head, prev, nodes);
                }
                return;
            }
            if (ps3_type15_list_ptr_bad(cur)) {
                if (prev == sent) {
                    vm_write32(sent + 0u, sent);
                    vm_write32(sent + 4u, sent);
                    { static int _n = 0;
                      if (_n++ < 16)
                        fprintf(stderr,
                                "[TYPE15] product list RESET prod=0x%08X was_head=0x%08X "
                                "-> circular empty (bad mid-walk)\n",
                                prod, head);
                    }
                } else {
                    vm_write32(prev, sent);
                    vm_write32(sent + 4u, prev);
                    { static int _n = 0;
                      if (_n++ < 16)
                        fprintf(stderr,
                                "[TYPE15] product list CLOSE-PRESERVE prod=0x%08X "
                                "head=0x%08X closed_at=0x%08X nodes=%u (bad node)\n",
                                prod, head, prev, nodes);
                    }
                }
                return;
            }
            uint32_t nx = vm_read32(cur);
            nodes++;
            if (nodes >= k_cap) {
                vm_write32(cur, sent);
                vm_write32(sent + 4u, cur);
                { static int _n = 0;
                  if (_n++ < 16)
                    fprintf(stderr,
                            "[TYPE15] product list CLOSE-PRESERVE prod=0x%08X "
                            "head=0x%08X closed_at=0x%08X nodes=%u (cap)\n",
                            prod, head, cur, nodes);
                }
                return;
            }
            if (ps3_type15_list_ptr_bad(nx)) {
                /* Close this node onto sentinel (NULL/poison tail). */
                vm_write32(cur, sent);
                vm_write32(sent + 4u, cur);
                { static int _n = 0;
                  if (_n++ < 16)
                    fprintf(stderr,
                            "[TYPE15] product list CLOSE-PRESERVE prod=0x%08X "
                            "head=0x%08X closed_at=0x%08X nodes=%u\n",
                            prod, head, cur, nodes);
                }
                return;
            }
            prev = cur;
            cur = nx;
        }
    }
}
'''


def find_fn_span(t: str) -> tuple[int, int] | None:
    """Return [start, end) of ps3_type15_product_list_reset definition incl. comment."""
    # Prefer the block starting at the product+0x70 comment above the fn.
    m = re.search(
        r"/\* product\+0x70 is an intrusive circular list.*?^extern \"C\" void "
        + re.escape(FN_NAME)
        + r"\(uint32_t prod\) \{",
        t,
        flags=re.M | re.S,
    )
    if not m:
        m = re.search(
            r"^extern \"C\" void " + re.escape(FN_NAME) + r"\(uint32_t prod\) \{",
            t,
            flags=re.M,
        )
        if not m:
            return None
        start = m.start()
    else:
        start = m.start()

    # Brace-match from the '{' of the function (last '{' of the match).
    brace_at = t.find("{", m.end() - 2)
    if brace_at < 0:
        return None
    depth = 0
    i = brace_at
    while i < len(t):
        c = t[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                # swallow trailing newline
                if end < len(t) and t[end] == "\n":
                    end += 1
                return start, end
        i += 1
    return None


def patch(t: str) -> str:
    if MARKER in t and FN_NAME in t:
        # Already has CLOSE-PRESERVE body — treat as applied.
        # Re-apply if signature still present but body is the old wipe-only form
        # without the preserve walk (should not happen if MARKER present).
        return t

    span = find_fn_span(t)
    if span is None:
        raise SystemExit(
            f"{FN_NAME} not found (lift shape changed?). "
            "Expected in ppu_recomp_001.cpp helpers."
        )
    start, end = span
    new_t = t[:start] + NEW_FN + t[end:]
    if MARKER not in new_t:
        raise SystemExit("internal: NEW_FN missing CLOSE-PRESERVE marker")
    return new_t


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2/ppu_recomp_001.cpp")
    existing = [p for p in paths if p.exists()]
    for p in paths:
        if not p.exists():
            print(f"skip {p} (missing)")
    if not existing:
        print("FAILED: nenhum chunk de lift encontrado")
        return 1

    # A funcao vive (quando existe) num UNICO chunk -- procura-se na uniao e
    # da-se um unico veredicto, em vez de um FAILED por chunk (chunk-fixo).
    # A busca e' pela DEFINICAO, nao pelo nome: uma declaracao extern ou um call
    # site noutro chunk (e' o que a producao tem no ppu_recomp_000.cpp) nao e' o
    # sitio para reescrever o corpo. Ver ADENDA 2026-07-26 no cabecalho.
    target = None
    for p in existing:
        t = p.read_text()
        if DEF_SIG in t:
            target = (p, t)
            break
    if target is None:
        print(f"FAILED: {FN_NAME} nao esta' DEFINIDA em nenhum dos "
              f"{len(existing)} chunks.")
        print("  A injeccao host (funcao + call site) e' instalada por")
        print("  patch_type15_list_close_preserve_install.py, que corre antes")
        print("  deste no apply_all_patches.sh. Fonte versionada do bloco:")
        print("  ../host_gow2_factory.cpp e ../lift_baseline/injected_001.cpp.")
        print("  Nada foi injectado aqui: sem os call sites o APPLIED seria falso.")
        return 1

    p, t = target
    try:
        t2 = patch(t)
    except SystemExit as e:
        print(f"FAILED {p}: {e}")
        return 1
    if t2 != t:
        p.write_text(t2)
        print(f"APPLIED {p}")
    else:
        print(f"ALREADY-APPLIED {p}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
