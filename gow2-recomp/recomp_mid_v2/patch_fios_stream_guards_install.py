#!/usr/bin/env python3
r"""Instala os guards de free-list/alocador que o patch_fios_stream_pump verifica.

Porque existe
-------------
`patch_fios_stream_pump.py` e' um VERIFICADOR PURO (0 escritas) de 4 unidades
de prova, limiar `ok >= 3`:

  1. `[FREELIST-TAG-GUARD]`                  (walk da free-list)
  2. `[ALLOC-NULL-GUARD]`                    (sub-alloc devolve 0)
  3. `[ALLOC-NULL-GUARD]` em >= 2 chunks     (os dois sitios 2550C8 / 2550E8)
  4. `F2B-STREAM-PUMP`                       (pump host apos o open DONE)

Nenhuma delas tinha escritor: eram edicoes MANUAIS de sessao dentro do lift
gitignored. Medido a 2026-07-25 num lift limpo + os 73 patches:

    grep -c '\[FREELIST-TAG-GUARD\]' -> 0      recomp_macos_v2 -> 5
    grep -c '\[ALLOC-NULL-GUARD\]'   -> 0      recomp_macos_v2 -> 2
    grep -c 'F2B-STREAM-PUMP'        -> 0      recomp_macos_v2 -> 3

Este ficheiro instala as unidades 1-3 (7 sitios, ver abaixo), que sao codigo
auto-contido. Passam a 3/4 e o verificador passa no seu proprio limiar, sem
lhe tocar.

Porque a unidade 4 (F2B-STREAM-PUMP) NAO esta' aqui
---------------------------------------------------
O bloco do pump em `func_002B4274` chama a camada host F2B, que tambem e'
orfa e e' de OUTROS patches -- instala-la aqui seria duplicar o escritor deles
e, pior, deixaria o pump a nao fazer nada:

    f2b_fo_mfd_get / f2b_fo_sz_get   0 no lift limpo, 0 escritores
    movie_io_is                      0 no lift limpo (extern do runtime)
    g_f2b_fill_* / f2b_stream_fill   0 no lift limpo; verificados por
                                     patch_f2b_multimb_stream.py
    tabela fo->mfd (F2B-FO-CTOR)     0 no lift limpo; verificada por
                                     patch_fios_f2b_fo_ctor.py

Sem essa tabela, `f2b_fo_mfd_get(_fo)` devolve sempre 0 e o pump nunca corre:
instalar so' o marcador seria satisfazer o grep sem o comportamento -- forjar
resultado (regra 4 do CLAUDE.md). A camada F2B tem escritor proprio
(a 2026-07-25: `recomp_mid_v2/patch_fios_f2b_open_block_install.py`, que
reescreve func_002B4274 e o preambulo f2b_*); com ele no sitio o
patch_fios_stream_pump.py fecha as 4/4 unidades. Este ficheiro entrega as
unidades 1-3 e nao toca em nenhuma funcao desse outro instalador (medido:
0 referencias cruzadas a 00262610/00263178/002550C8/002550E8 vs 002B4274).

O que instala (verbatim do lift de producao recomp_macos_v2)
------------------------------------------------------------
  func_00262610  entrada    arena nula / free-head com bit31 / need > 64MiB
                            -> r3=0 e return (em vez de walk OOB infinito)
  func_00262610  walk       cap de 10000 iteracoes (ciclo/desync) + no' nulo
                            ou < 0x10000 a meio do walk -> aborta p/ 002627EC
  func_00262610  next       [node+4] com bit31 ou 0<nx<0x10000 -> aborta
  func_00263178  next       [node+4] com bit31 -> r3=0 + epilogo do 00263040
  func_002550C8  alloc      sub-alloc 002637D8->00263040 devolveu 0 ->
                            nao carimba tags em EA 0, sai por 00254E84
  func_002550E8  alloc      idem (o outro sitio do mesmo grow)

Sao 5 tags `[FREELIST-TAG-GUARD]` + 2 `[ALLOC-NULL-GUARD]`, que e' exactamente
o que `lift_baseline/MANIFEST.tsv` (linhas 157 e 176) regista como tendo de
sobreviver a um re-lift.

Como foi gerado
---------------
Metodo do `patch_ce03c_introseq_block.py`: clonar o lift limpo, correr
`./apply_all_patches.sh <clone>`, extrair as 4 funcoes do clone e da producao,
diff -> o excedente da producao e' o bloco orfao, e gerar este ficheiro
PROGRAMATICAMENTE com os blocos fatiados verbatim (nunca transcritos a mao).
Nao se substitui o CORPO inteiro das funcoes: o resto do diff contra a
producao e' drift do lifter (callee-save `_cs_*`, `ctx->lr = 0x...`,
`ppc_sraw` vs `>>`, casts) e copiar o corpo antigo por cima regrediria
correccoes do lifter. Insere-se so' o bloco, ancorado em codigo REAL emitido
pelo lifter actual -- o mesmo metodo do `patch_type15_cb56c_highbit.py`.

Ordem: corre depois de `patch_fallthrough_2550c8.py` (que reescreve o epilogo
de 002550C8) e antes de `patch_fios_stream_pump.py`, por ordem alfabetica do
glob do `apply_all_patches.sh`.

Contrato de rc
--------------
  ja' instalado (marcador presente no corpo) -> ALREADY, rc=0
  ancora encontrada 1x                       -> insere, rc=0
  funcao presente mas ancora ausente/dupla   -> RECUSA, rc=2 (nao adivinha)
  alguma funcao ausente de todos os chunks   -> rc=2
A recusa e' ATOMICA: basta um dos 6 sitios recusar para nada ser escrito --
o lift nunca fica meio-patcheado.

Uso: python3 recomp_mid_v2/patch_fios_stream_guards_install.py [LIFT_DIR]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

# (funcao, marcador de idempotencia, ancora, substituicao)
SITES: list[tuple[str, str, str, str]] = [
    ('func_00262610',
     '[FREELIST-TAG-GUARD] 262610 entry',
     'void func_00262610(ppu_context* ctx) {\n',
     'void func_00262610(ppu_context* ctx) {\n        /* FREELIST-TAG-GUARD (alloc walk): same class as 263178. If arena is\n         * null or free-head [arena+4] is a boundary tag (bit31), abort r3=0\n         * instead of infinite OOB walk (hang in 41D5C→BACE8→262610 after\n         * WADLD-BODY SBP_general). */\n        { uint32_t arena=(uint32_t)ctx->gpr[3];\n          uint32_t need=(uint32_t)ctx->gpr[4];\n          uint32_t head = arena ? vm_read32(arena+0x4) : 0u;\n          /* need is guest size in bytes (pre-word-convert). Cap at 64MiB —\n           * observed hang had need=0x687DD790 (~1.7GiB) from corrupt WAD hdr. */\n          if (arena==0u || (head & 0x80000000u) || (head && head < 0x10000u) || need > 0x4000000u) {\n            static int _n=0; if(_n++<32){\n              fprintf(stderr,"[FREELIST-TAG-GUARD] 262610 entry arena=0x%08X head=0x%08X need=0x%X → abort r3=0\\n",\n                arena, head, need); fflush(stderr); }\n            ctx->gpr[3] = 0;\n            return;\n          } }\n'),
    ('func_00262610',
     '[FREELIST-TAG-GUARD] 262610 walk iter>',
     '        ctx->gpr[12] = (int64_t)(int32_t)(0);\nloc_00262650:\n        ctx->gpr[10] = ppc_rldicl(ctx->gpr[3], 0, 32);\n        ctx->gpr[0] = vm_read32(ctx->gpr[10] + 0x0);\n',
     '        ctx->gpr[12] = (int64_t)(int32_t)(0);\n        { /* walk iteration cap — break freelist cycles that never return to arena */ }\n        static int _fl_walk_iters = 0; _fl_walk_iters = 0;\nloc_00262650:\n        /* Fail-fast: 200k iters was ~seconds per corrupt alloc and stalled\n         * B71B8 at CBC20 (0x19C alloc after WAD desync freelist trash). */\n        if (++_fl_walk_iters > 10000) {\n          static int _n=0; if(_n++<16){\n            fprintf(stderr,"[FREELIST-TAG-GUARD] 262610 walk iter>%d node=0x%08X next_cand — cycle/desync → abort r3=0\\n",\n              10000, (uint32_t)ctx->gpr[3]); fflush(stderr); }\n          { g_trampoline_fn = (void(*)(void*))func_002627EC; return; }\n        }\n        ctx->gpr[10] = ppc_rldicl(ctx->gpr[3], 0, 32);\n        /* Null / low node mid-walk — list already dead. */\n        { uint32_t nd=(uint32_t)ctx->gpr[10];\n          if (nd == 0u || nd < 0x10000u) {\n            static int _n=0; if(_n++<16){\n              fprintf(stderr,"[FREELIST-TAG-GUARD] 262610 walk null/low node=0x%08X → abort r3=0\\n",\n                nd); fflush(stderr); }\n            { g_trampoline_fn = (void(*)(void*))func_002627EC; return; }\n          } }\n        ctx->gpr[0] = vm_read32(ctx->gpr[10] + 0x0);\n'),
    ('func_00262610',
     '[FREELIST-TAG-GUARD] 262610 walk next=',
     'loc_0026269C:\n        ctx->gpr[3] = vm_read32(ctx->gpr[10] + 0x4);\n',
     'loc_0026269C:\n        ctx->gpr[3] = vm_read32(ctx->gpr[10] + 0x4);\n        /* FREELIST-TAG-GUARD: next slot holds tag or near-null → list desync.\n         * Without this, walk spins forever on UNCOMMITTED 0x91/0x95/0x80. */\n        { uint32_t nx=(uint32_t)ctx->gpr[3];\n          /* nx==0 is end-of-list only when equal to arena (handled below);\n           * nx in (0, 0x10000) or high tag bit = desync. Also treat repeated\n           * null-ish next while node != arena as dead. */\n          if ((nx & 0x80000000u) || (nx != 0u && nx < 0x10000u)) {\n            static int _n=0; if(_n++<32){\n              fprintf(stderr,"[FREELIST-TAG-GUARD] 262610 walk next=0x%08X node=0x%08X → abort r3=0\\n",\n                nx, (uint32_t)ctx->gpr[10]); fflush(stderr); }\n            { g_trampoline_fn = (void(*)(void*))func_002627EC; return; }\n          } }\n'),
    ('func_00263178',
     '[FREELIST-TAG-GUARD] 263178 next=',
     '        ctx->gpr[28] = vm_read32(ctx->gpr[12] + 0x0);\n        if (((ctx->cr >> 0) & 4)) goto loc_002631FC;\n',
     '        ctx->gpr[28] = vm_read32(ctx->gpr[12] + 0x0);\n        /* FREELIST-TAG-GUARD: [node+4] must be a clean next pointer. When a\n         * boundary-tag (bit31 set, form 0x8xxxxxxx) sits in the next slot,\n         * walk is desynced (asset-map Task 3). Abort split with r3=0 instead\n         * of deref 0x840000xx. Gated always-on (safety); log first hits. */\n        if ((uint32_t)ctx->gpr[28] & 0x80000000u) {\n          { static int _n=0; if(_n++<24){\n            fprintf(stderr,"[FREELIST-TAG-GUARD] 263178 next=0x%08X (tag) node+4@0x%08X → abort\\n",\n              (uint32_t)ctx->gpr[28], (uint32_t)ctx->gpr[12]);\n            fflush(stderr); } }\n          ctx->gpr[3] = 0;\n          /* epilogue of 00263040 null path */\n          ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0xB0);\n          ctx->gpr[26] = vm_read64(ctx->gpr[1] + 0x70);\n          ctx->gpr[27] = vm_read64(ctx->gpr[1] + 0x78);\n          ctx->lr = ctx->gpr[0];\n          ctx->gpr[28] = vm_read64(ctx->gpr[1] + 0x80);\n          ctx->gpr[29] = vm_read64(ctx->gpr[1] + 0x88);\n          ctx->gpr[30] = vm_read64(ctx->gpr[1] + 0x90);\n          ctx->gpr[31] = vm_read64(ctx->gpr[1] + 0x98);\n          ctx->gpr[1] = (int64_t)(int32_t)(ctx->gpr[1] + 0xA0);\n          return;\n        }\n        if (((ctx->cr >> 0) & 4)) goto loc_002631FC;\n'),
    ('func_002550C8',
     '[ALLOC-NULL-GUARD] 2550C8',
     '        ctx->gpr[29] = ctx->gpr[3] | ctx->gpr[3];\n        if (((ctx->cr >> 12) & 2)) goto loc_00255140;\n',
     '        ctx->gpr[29] = ctx->gpr[3] | ctx->gpr[3];\n        /* ALLOC-NULL-GUARD: 002637D8→00263040 can return 0 when a chunk arena\n         * free-list is exhausted. Natural code stamps boundary tags at r3 without\n         * null-check → EA 0 gets tags → freelist [node+4]=0x84xxxxxx → UNCOMMITTED\n         * 0x840000xx (gow2-asset-pipeline-map Task 3b). Abort grow cleanly. */\n        if ((uint32_t)ctx->gpr[3] == 0u) {\n          { static int _n=0; if(_n++<16){\n            fprintf(stderr,"[ALLOC-NULL-GUARD] 2550C8 sub-alloc r3=0 → skip stamp@0\\n");\n            fflush(stderr); } }\n          { g_trampoline_fn = (void(*)(void*))func_00254E84; return; }\n        }\n        if (((ctx->cr >> 12) & 2)) goto loc_00255140;\n'),
    ('func_002550E8',
     '[ALLOC-NULL-GUARD] 2550E8',
     '        ctx->gpr[29] = ctx->gpr[3] | ctx->gpr[3];\n        if (((ctx->cr >> 12) & 2)) goto loc_00255140;\n',
     '        ctx->gpr[29] = ctx->gpr[3] | ctx->gpr[3];\n        /* ALLOC-NULL-GUARD: same as 2550C8 — never stamp tags at EA 0 */\n        if ((uint32_t)ctx->gpr[3] == 0u) {\n          { static int _n=0; if(_n++<16){\n            fprintf(stderr,"[ALLOC-NULL-GUARD] 2550E8 sub-alloc r3=0 → skip stamp@0\\n");\n            fflush(stderr); } }\n          { g_trampoline_fn = (void(*)(void*))func_00254E84; return; }\n        }\n        if (((ctx->cr >> 12) & 2)) goto loc_00255140;\n'),
]


def func_span(text: str, fn: str):
    sig = "void %s(ppu_context* ctx) {" % fn
    i = text.find(sig)
    if i < 0:
        return None
    j = text.find("\nvoid func_", i + len(sig))
    return (i, len(text) if j < 0 else j)


def apply_site(text: str, fn: str, marker: str, anchor: str, repl: str):
    """(novo_texto, estado) com estado em {'skip','already','applied','refuse'}."""
    span = func_span(text, fn)
    if span is None:
        return text, "skip"
    b0, b1 = span
    body = text[b0:b1]
    if marker in body:
        return text, "already"
    if body.count(anchor) != 1:
        return text, "refuse"
    return text[:b0] + body.replace(anchor, repl, 1) + text[b1:], "applied"


def main() -> int:
    paths = [p for p in resolve_lift_paths(
        sys.argv[1:], str(Path(__file__).resolve().parent.parent / "recomp_macos_v2"))
        if p.is_file()]
    if not paths:
        print("ERRO: nenhum chunk de lift legivel", file=sys.stderr)
        return 2

    # 1a passagem: decide TUDO em memoria. So' se nenhum sitio recusar e' que se
    # escreve -- uma recusa nao pode deixar o lift meio-patcheado.
    seen = {fn: False for fn, _, _, _ in SITES}
    refused: list[str] = []
    pending: list[tuple] = []   # (path, texto_novo)
    applied = already = 0

    for p in paths:
        text = p.read_text(encoding="utf-8", errors="replace")
        orig = text
        for fn, marker, anchor, repl in SITES:
            text, state = apply_site(text, fn, marker, anchor, repl)
            if state == "skip":
                continue
            seen[fn] = True
            if state == "applied":
                applied += 1
                print("  APPLIED  %s :: %s" % (p.name, fn))
            elif state == "already":
                already += 1
                print("  ALREADY  %s :: %s" % (p.name, fn))
            else:
                refused.append("%s :: %s" % (p.name, fn))
                print("  REFUSE   %s :: %s (ancora ausente ou nao unica)" % (p.name, fn))
        if text != orig:
            pending.append((p, text))

    missing = sorted(fn for fn, ok in seen.items() if not ok)
    if refused or missing:
        sys.stdout.flush()
        if refused:
            print("ERRO: corpo gerado diferente do esperado em: %s" % ", ".join(refused),
                  file=sys.stderr)
            print("  O lifter mudou. Nao substituo as cegas -- revalida o bloco contra um\n"
                  "  lift de producao conhecido-bom e regenera este script.", file=sys.stderr)
        if missing:
            print("ERRO: funcao(oes) ausente(s) de todos os chunks: %s" % ", ".join(missing),
                  file=sys.stderr)
        print("  NADA foi escrito (recusa atomica).", file=sys.stderr)
        return 2

    # 2a passagem: escrever.
    for p, text in pending:
        try:
            p.write_text(text, encoding="utf-8", newline="\n")
        except TypeError:
            p.write_text(text, encoding="utf-8")
    print("[%s] ok (%d aplicado, %d ja' aplicado)" % ('fios-stream-guards', applied, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
