#!/usr/bin/env python3
"""Instala a injeccao TYPE15 product+0x70 CLOSE-PRESERVE: funcao + call site.

Porque existe
-------------
`patch_type15_list_preserve.py` REESCREVE o corpo de
`ps3_type15_product_list_reset` -- mas essa funcao nao e' produzida pelo lifter:
e' codigo host injectado a mao no lift gitignored, e nenhum patch_*.py a
instalava. Medido a 2026-07-25 num lift limpo com os 74 patches aplicados:

    FAILED: ps3_type15_product_list_reset nao existe em nenhum dos 7 chunks.

O proprio cabecalho desse patch diz o que falta: "Para isto passar a aplicar e'
preciso primeiro um patch que instale a injeccao (funcao + call sites)". E' este
ficheiro. O verificador/enhancer nao foi tocado.

O que instala
-------------
1. O bloco injectado, recortado VERBATIM de recomp_macos_v2/ppu_recomp_001.cpp
   (comentario + `ps3_type15_list_ptr_bad` + `ps3_type15_product_list_reset`).
   Verificado byte a byte: o recorte e' IDENTICO ao NEW_FN que o
   patch_type15_list_preserve.py ja' carregava -- ou seja, instala-se
   directamente a versao M2/CLOSE-PRESERVE, a unica que existe como fonte de
   verdade (a forma antiga "wipe-only" nao existe em lado nenhum; inventa-la
   para dar trabalho ao enhancer seria forjar). Consequencia esperada e
   declarada: com este instalador a correr primeiro, o
   patch_type15_list_preserve.py passa a reportar ALREADY-APPLIED, rc=0.
2. `extern "C" void ps3_type15_product_list_reset(uint32_t prod);` no chunk do
   func_000CB56C (como na producao, que tem a declaracao no ppu_recomp_000.cpp).
3. O call site em func_000CB56C: a seguir ao par
       ctx->gpr[29] = ppc_rldicl(ctx->gpr[3], 0, 32);
       ctx->gpr[28] = ctx->gpr[3] | ctx->gpr[3];
   (produto devolvido pelo construct) e antes do icall2 de attach -- a mesma
   posicao da producao.

Sobre o filtro do call site
---------------------------
Na producao a chamada vem depois do bloco manual "[POSTINTRO] CB56C reject
product", que rejeita r29 = 0 / fora de 0x10000..0x4F000000 / na banda de
poison 0x20000000..0x40000000. Esse bloco e' um orfao SEPARADO (ninguem o
instala e nenhum patch o verifica), por isso nao entra aqui. Para nao chamar o
helper com um ponteiro que a producao nunca lhe passaria, o call site leva o
MESMO teste, com as mesmas constantes (sao tambem, letra por letra, as de
`ps3_type15_list_ptr_bad` deste bloco). Nao ha' constante inventada.

O que NAO instala (de proposito)
--------------------------------
- A factory TYPE15 host (k_ty15_pin / g_ty15_snap / ty15_vt_looks_live /
  freelist_replenish / repair_if_needed) e o seu call site
  `ps3_type15_product_list_reset(shell)`: sao de OUTRO bloco, com instalador
  proprio -- `patch_factory_block_ty15_install.py`, que corre ANTES deste
  ("factory" < "type15"). Como esse bloco arrasta a MESMA definicao, este
  instalador deteca-a (`extern "C" void ...(uint32_t prod) {`) e nao a duplica:
  reporta "definicao ALREADY" e instala so' a declaracao + o call site do
  CB56C. Se esse instalador nao existir/nao correr, este poe a definicao no
  chunk do CB56C e funciona na mesma (medido nos dois cenarios).
- O bloco manual "[POSTINTRO]" de func_000CB56C (reject / early-out / icall2
  reescrito a mao): orfao sem escritor nem verificador. O caminho natural do
  lifter faz o mesmo icall2; so' o filtro de r29 foi reposto (ver acima).
- Nada de `patch_type15_cb56c_highbit.py` (que corre antes e insere o bloco
  reuse-product imediatamente ANTES do par que serve de ancora aqui).

Ordem dentro do apply_all_patches.sh
------------------------------------
O nome ordena antes de `patch_type15_list_preserve.py` ("list_c" < "list_p") e
depois de `patch_type15_cb56c_highbit.py` ("cb56c" < "list"), que e' a ordem
necessaria: a ancora tem de existir antes, o enhancer tem de correr depois.

Nota sobre o verificador
------------------------
Ao pormos a declaracao + call site no chunk do CB56C (a forma da producao)
apareceu no `patch_type15_list_preserve.py` um falso FAILED: ele escolhia o
chunk por MENCAO do nome, e passava a apanhar o 000 (declaracao/chamada) em vez
do 001 (definicao). Isso ja' acontecia contra o proprio lift de producao. Foi
corrigido la' (ADENDA 2026-07-26): escolhe agora pela DEFINICAO. Severidade
inalterada -- sem definicao em chunk nenhum continua FAILED rc=1.

Como foi gerado
---------------
Por script (scratchpad/gen_type15_installer.py), que recorta o bloco do lift de
producao com a MESMA funcao `find_fn_span` do patch_type15_list_preserve.py e
compara o recorte com o NEW_FN dele (assert de igualdade). Nada transcrito a mao.

Contrato de rc
--------------
- 0 : instalado agora (APPLIED) e/ou ja' presente (ALREADY)
- 2 : func_000CB56C ausente / em mais de um chunk, ou o par rldicl/or que serve
      de ancora nao aparece exactamente 1x no corpo dela. NAO adivinha: o
      lifter mudou de forma e o bloco tem de ser revalidado a mao.
- 3 : nenhum ficheiro de lift legivel

Uso:  patch_type15_list_close_preserve_install.py [LIFT_DIR_OU_FICHEIRO...]
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

ROOT_DEFAULT = str(Path(__file__).resolve().parent.parent / "recomp_macos_v2")

FUNC = "func_000CB56C"
FN_NAME = "ps3_type15_product_list_reset"
DEF_SIG = 'extern "C" void ps3_type15_product_list_reset(uint32_t prod) {'
DECL = 'extern "C" void ps3_type15_product_list_reset(uint32_t prod);\n'

CORE = '/* product+0x70 is an intrusive circular list (sentinel = product+0x70).\n * func_002A5024 inits both links to self. Reused products often keep a\n * NULL-terminated pool walk (12×0xD8 nodes) that made func_002A4FE4 hang\n * (NULL → poison 0x27182818 forever).\n *\n * M2 (2026-07-23, H1 / CLOSE-PRESERVE): do NOT wipe a valid non-sentinel\n * head. Prefer close-tail preserve (same poison rules as 2A4FE4 CLOSE-TAIL).\n * Empty circular only when head is 0/bad (shells / freelist replenish). */\nstatic int ps3_type15_list_ptr_bad(uint32_t p) {\n    if (p == 0u) return 1;\n    if (p < 0x10000u || p >= 0x4F000000u) return 1;\n    /* poison / non-heap band seen on NULL-terminated pool tails */\n    if (p >= 0x20000000u && p < 0x40000000u) return 1;\n    return 0;\n}\nextern "C" void ps3_type15_product_list_reset(uint32_t prod) {\n    if (prod < 0x10000u || prod >= 0x4F000000u) return;\n    uint32_t sent = prod + 0x70u;\n    uint32_t head = vm_read32(sent);\n    /* Already empty circular? */\n    if (head == sent && vm_read32(sent + 4u) == sent) return;\n\n    /* Head is sentinel but prev stale → just fix prev. */\n    if (head == sent) {\n        vm_write32(sent + 4u, sent);\n        return;\n    }\n\n    /* Head invalid → empty circular (shells, freelist template inherit). */\n    if (ps3_type15_list_ptr_bad(head)) {\n        vm_write32(sent + 0u, sent);\n        vm_write32(sent + 4u, sent);\n        { static int _n = 0;\n          if (_n++ < 16)\n            fprintf(stderr,\n                    "[TYPE15] product list RESET prod=0x%08X was_head=0x%08X "\n                    "-> circular empty (2A4FE4-safe)\\n",\n                    prod, head);\n        }\n        return;\n    }\n\n    /* Valid head: walk next pointers; close broken tail onto sentinel.\n     * Do not empty the whole list (H1 fix — preserve freelist/pool nodes). */\n    {\n        uint32_t cur = head;\n        uint32_t prev = sent;\n        uint32_t nodes = 0u;\n        const uint32_t k_cap = 65536u;\n        for (;;) {\n            if (cur == sent) {\n                /* Already circular. Ensure sent->prev = last node. */\n                if (prev != sent)\n                    vm_write32(sent + 4u, prev);\n                { static int _n = 0;\n                  if (_n++ < 16)\n                    fprintf(stderr,\n                            "[TYPE15] product list CLOSE-PRESERVE prod=0x%08X "\n                            "head=0x%08X closed_at=0x%08X nodes=%u (already circular)\\n",\n                            prod, head, prev, nodes);\n                }\n                return;\n            }\n            if (ps3_type15_list_ptr_bad(cur)) {\n                if (prev == sent) {\n                    vm_write32(sent + 0u, sent);\n                    vm_write32(sent + 4u, sent);\n                    { static int _n = 0;\n                      if (_n++ < 16)\n                        fprintf(stderr,\n                                "[TYPE15] product list RESET prod=0x%08X was_head=0x%08X "\n                                "-> circular empty (bad mid-walk)\\n",\n                                prod, head);\n                    }\n                } else {\n                    vm_write32(prev, sent);\n                    vm_write32(sent + 4u, prev);\n                    { static int _n = 0;\n                      if (_n++ < 16)\n                        fprintf(stderr,\n                                "[TYPE15] product list CLOSE-PRESERVE prod=0x%08X "\n                                "head=0x%08X closed_at=0x%08X nodes=%u (bad node)\\n",\n                                prod, head, prev, nodes);\n                    }\n                }\n                return;\n            }\n            uint32_t nx = vm_read32(cur);\n            nodes++;\n            if (nodes >= k_cap) {\n                vm_write32(cur, sent);\n                vm_write32(sent + 4u, cur);\n                { static int _n = 0;\n                  if (_n++ < 16)\n                    fprintf(stderr,\n                            "[TYPE15] product list CLOSE-PRESERVE prod=0x%08X "\n                            "head=0x%08X closed_at=0x%08X nodes=%u (cap)\\n",\n                            prod, head, cur, nodes);\n                }\n                return;\n            }\n            if (ps3_type15_list_ptr_bad(nx)) {\n                /* Close this node onto sentinel (NULL/poison tail). */\n                vm_write32(cur, sent);\n                vm_write32(sent + 4u, cur);\n                { static int _n = 0;\n                  if (_n++ < 16)\n                    fprintf(stderr,\n                            "[TYPE15] product list CLOSE-PRESERVE prod=0x%08X "\n                            "head=0x%08X closed_at=0x%08X nodes=%u\\n",\n                            prod, head, cur, nodes);\n                }\n                return;\n            }\n            prev = cur;\n            cur = nx;\n        }\n    }\n}\n'

PAIR = '        ctx->gpr[29] = ppc_rldicl(ctx->gpr[3], 0, 32);\n        ctx->gpr[28] = ctx->gpr[3] | ctx->gpr[3];\n'

CALL = "        /* TYPE15: sanitiza product+0x70 antes do attach (func_002A4FE4\n         * percorre esta lista como circular). Na producao esta chamada esta'\n         * no mesmo ponto -- a seguir ao par rldicl/or que poe o produto em r29\n         * e antes do icall2 de attach -- mas la' vem depois do bloco\n         * [POSTINTRO] reject, que ja' filtrou r29. Esse bloco e' OUTRO orfao\n         * (sem escritor), por isso o filtro esta' aqui com exactamente as\n         * mesmas condicoes que ele usa: 0 / fora de 0x10000..0x4F000000 /\n         * banda de poison 0x20000000..0x40000000 nao sao produto. */\n        if ((uint32_t)ctx->gpr[29] >= 0x10000u\n            && (uint32_t)ctx->gpr[29] < 0x4F000000u\n            && !((uint32_t)ctx->gpr[29] >= 0x20000000u\n                 && (uint32_t)ctx->gpr[29] < 0x40000000u))\n            ps3_type15_product_list_reset((uint32_t)ctx->gpr[29]);\n"


def fn_span(text: str, name: str):
    m = re.search(rf"^void {name}\(ppu_context\* ctx\) {{", text, re.M)
    if not m:
        return None
    nxt = re.search(r"^void func_", text[m.end():], re.M)
    return m.start(), (m.end() + nxt.start() if nxt else len(text))


def insert_before_first_func(text: str, block: str) -> str:
    m = re.search(r"^void func_", text, re.M)
    if not m:
        raise LookupError("chunk sem nenhuma `void func_`")
    return text[:m.start()] + block + text[m.start():]


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], ROOT_DEFAULT) if p.is_file()]
    if not paths:
        print("ERRO: nenhum ficheiro de lift legivel", file=sys.stderr)
        return 3
    texts = {p: p.read_text(encoding="utf-8", errors="replace") for p in paths}

    hits = [p for p, t in texts.items() if fn_span(t, FUNC)]
    if len(hits) != 1:
        print(f"ERRO: {FUNC} aparece em {len(hits)} chunk(s) (esperado 1): "
              + ", ".join(p.name for p in hits), file=sys.stderr)
        return 2
    cb = hits[0]

    applied = already = 0

    # --- 1) definicao (num chunk qualquer; por omissao o do CB56C) -----------
    have = [p for p, t in texts.items() if DEF_SIG in t]
    if have:
        print(f"  {have[0].name}: definicao ALREADY")
        already += 1
    else:
        try:
            texts[cb] = insert_before_first_func(texts[cb], CORE)
        except LookupError as e:
            print(f"ERRO: {cb.name}: {e}", file=sys.stderr)
            return 2
        print(f"  {cb.name}: definicao APPLIED ({CORE.count(chr(10))} linhas)")
        applied += 1

    # --- 2) declaracao no chunk do CB56C ------------------------------------
    if DECL in texts[cb]:
        print(f"  {cb.name}: declaracao ALREADY")
        already += 1
    else:
        try:
            texts[cb] = insert_before_first_func(texts[cb], DECL)
        except LookupError as e:
            print(f"ERRO: {cb.name}: {e}", file=sys.stderr)
            return 2
        print(f"  {cb.name}: declaracao APPLIED")
        applied += 1

    # --- 3) call site em func_000CB56C --------------------------------------
    lo, hi = fn_span(texts[cb], FUNC)
    region = texts[cb][lo:hi]
    if f"{FN_NAME}((uint32_t)ctx->gpr[29]);" in region:
        print(f"  {FUNC}: call site ALREADY")
        already += 1
    else:
        n = region.count(PAIR)
        if n != 1:
            print(f"ERRO: {FUNC}: o par rldicl/or do produto aparece {n}x no "
                  "corpo (esperado 1) -- o lifter mudou de forma.", file=sys.stderr)
            return 2
        at = region.index(PAIR) + len(PAIR)
        texts[cb] = texts[cb][:lo] + region[:at] + CALL + region[at:] + texts[cb][hi:]
        print(f"  {FUNC}: call site APPLIED")
        applied += 1

    for p, t in texts.items():
        if t != p.read_text(encoding="utf-8", errors="replace"):
            try:
                p.write_text(t, encoding="utf-8", newline="\n")
            except TypeError:
                p.write_text(t, encoding="utf-8")

    print(f"[type15-close-preserve-install] ok ({applied} aplicado, "
          f"{already} ja' aplicado)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
