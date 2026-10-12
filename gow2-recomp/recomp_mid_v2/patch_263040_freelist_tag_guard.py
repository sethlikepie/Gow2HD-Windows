#!/usr/bin/env python3
r"""FREELIST-TAG-GUARD, 6.o sitio: func_00263040 (o find-fit da chunk arena).

Porque existe
--------------
`patch_fios_stream_guards_install.py` ja' instala 5 sitios do mesmo guard
(`func_00262610` x3, `func_00263178` x1, `func_002550C8`/`func_002550E8` x2 —
`[ALLOC-NULL-GUARD]`) contra o MESMO padrao de corrupcao: a chunk arena da
PS3 SDK tem o seu free-list "next" ([node+4]) stream-stomped por escritas
FIOS/WAD, deixando ali uma boundary-tag (bit31 set, forma 0x8xxxxxxx) ou um
endereco quase-nulo em vez de um ponteiro valido. `func_00263040` (o
"find-fit": percorre a free-list circular a' procura de um bloco >= tamanho
pedido) e' o MESMO allocator, mas o seu proprio walk (loc_002630D8) nunca
tinha sido protegido — nem a entrada (a sentinela `this`/head, r31) nem o
"next" lido a meio do loop (loc_00263120).

Medido em 2026-07-31, sessao "ultima parede antes do thr_auto_load": a 2.a
chamada a `func_0039E794` (fabrica TYPE15) entra no metodo real do guest
(`vt[+0x14]`) e NUNCA RETORNA. `lldb` com amostragem de thread (5 amostras,
attach/detach, ~26s) mostra a PC sempre dentro de `func_00263040`, chamada
sempre do MESMO sitio em `func_00263680+592` — nao e' progresso, e' um loop.
Dump do `ppu_context` (via `ctx = *(fp-0x18)`, layout `gpr[32]` no offset 0
de `ppu_context`) na amostra: `gpr[31]=0x00000168` (a sentinela/head, nunca
um endereco valido de heap PS3) e `gpr[3]=gpr[7]=0x84000005`,
`gpr[9]=0x84000009` — exactamente a forma boundary-tag (bit31 set) que os
outros 5 sitios ja' existentes foram escritos para apanhar.

O que instala (2 sitios, ambos idempotentes)
---------------------------------------------
  func_00263040  entrada    head (r31, "this") nulo, < 0x10000, ou com bit31
                            -> trata como "sem bloco livre" (goto ao label
                            de saida natural loc_00263130, r3=0), em vez de
                            fazer `vm_read32(r31+4)` sobre uma sentinela
                            invalida e nunca convergir de volta a si propria.
  func_00263040  walk next  [node+4] com bit31 ou 0<next<0x10000 -> mesmo
                            destino (loc_00263130, r3=0) em vez de continuar
                            o loop com um "next" que nunca vai bater na
                            sentinela real.

Ambos saltam para o `loc_00263130` JA' EXISTENTE na propria funcao (o
epilogo natural de "nao encontrado, devolve null") — nao se inventa um
epilogo novo (ao contrario do `263178`, que precisa de replicar o epilogo
porque e' outra funcao/trampolim).

O que isto NAO resolve (medido, nao inferido)
-----------------------------------------------
Com o guard, a 2.a chamada a `func_0039E794` DEIXA DE FICAR PRESA — mede-se
`[POSTINTRO] CB56C after 2A4FE4` (nunca visto antes desta sessao) e uma 3.a
chamada de construct completa a seguir. Mas o `thr_auto_load` continua em
0/6 no gate de 6 corridas (`smoke_chain_gate.sh --bin boot_gow2_f263040guard
6`): o guest nao faz null-check do "sem bloco livre" e uma rajada de `[vm]
OOB access` sucede-se antes de o processo estabilizar de novo em tiques de
fundo (SPUJOB/MOVIEFSM), sem mais progresso visivel na janela de 90s. E' uma
parede NOVA e MAIS TARDIA, nao o fecho do REG-03/marco v1.1.

Fontes
------
- Fix testado e medido em: `recomp_macos_v2.f263040guard` /
  `boot_gow2_f263040guard` (clones/binarios de teste; producao
  `recomp_macos_v2`/`boot_gow2` NAO tocados por este teste).
- Sessao/medicao: notas desta sessao (2026-07-31, "ultima parede antes do
  thr_auto_load").

Uso: python3 recomp_mid_v2/patch_263040_freelist_tag_guard.py [LIFT_DIR]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

# (funcao, marcador de idempotencia, ancora, substituicao)
SITES: list[tuple[str, str, str, str]] = [
    ('func_00263040',
     '[FREELIST-TAG-GUARD] 263040 entry',
     '        if ((!((ctx->cr >> 0) & 4))) { g_trampoline_fn = (void(*)(void*))func_00263160; return; }\n'
     '        ctx->gpr[3] = vm_read32(ctx->gpr[31] + 0x4);\n'
     '        ctx->gpr[11] = ctx->gpr[31] | ctx->gpr[31];\n',
     '        if ((!((ctx->cr >> 0) & 4))) { g_trampoline_fn = (void(*)(void*))func_00263160; return; }\n'
     '        /* FREELIST-TAG-GUARD: same class as 262610/263178 (chunk-arena free\n'
     '         * list corrupted by FIOS/WAD stream stomp or a synthetic head that\n'
     '         * never had a real circular list built). If the arena head (r31,\n'
     '         * "this") is null, below the guest reserved-page floor, or itself\n'
     '         * looks like a boundary tag (bit31 set), the walk below can never\n'
     '         * cycle back to its own sentinel -> infinite OOB walk. Treat as\n'
     '         * "no fit found" (r3=0) instead of hanging (measured: 2nd TYPE15\n'
     '         * construct call, head=0x168, next devolved 0x84000005/0x84000009). */\n'
     '        { uint32_t _head=(uint32_t)ctx->gpr[31];\n'
     '          if (_head == 0u || _head < 0x10000u || (_head & 0x80000000u)) {\n'
     '            static int _n=0; if(_n++<32){\n'
     '              fprintf(stderr,"[FREELIST-TAG-GUARD] 263040 entry head=0x%08X -> abort r3=0\\n",\n'
     '                _head); fflush(stderr); }\n'
     '            goto loc_00263130;\n'
     '          } }\n'
     '        ctx->gpr[3] = vm_read32(ctx->gpr[31] + 0x4);\n'
     '        ctx->gpr[11] = ctx->gpr[31] | ctx->gpr[31];\n'),
    ('func_00263040',
     '[FREELIST-TAG-GUARD] 263040 walk next=',
     'loc_00263120:\n'
     '        ctx->gpr[9] = ppc_rldicl(ctx->gpr[9], 0, 32);\n'
     '        ctx->gpr[3] = vm_read32(ctx->gpr[9] + 0x0);\n'
     '        { int64_t a = (int32_t)ctx->gpr[11]; int64_t b = (int32_t)ctx->gpr[3]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n'
     '        if ((!((ctx->cr >> 0) & 2))) goto loc_002630D8;\n',
     'loc_00263120:\n'
     '        ctx->gpr[9] = ppc_rldicl(ctx->gpr[9], 0, 32);\n'
     '        ctx->gpr[3] = vm_read32(ctx->gpr[9] + 0x0);\n'
     '        /* FREELIST-TAG-GUARD: [node+4] must be a clean next pointer back into\n'
     '         * the same circular list. A boundary tag (bit31) or near-null value\n'
     '         * means the list is desynced and will never compare equal to the\n'
     '         * sentinel (r11) again -> infinite walk. Same class as 263178. */\n'
     '        { uint32_t _nx=(uint32_t)ctx->gpr[3];\n'
     '          if ((_nx & 0x80000000u) || (_nx != 0u && _nx < 0x10000u)) {\n'
     '            static int _n=0; if(_n++<32){\n'
     '              fprintf(stderr,"[FREELIST-TAG-GUARD] 263040 walk next=0x%08X node=0x%08X -> abort r3=0\\n",\n'
     '                _nx, (uint32_t)ctx->gpr[9]); fflush(stderr); }\n'
     '            ctx->gpr[3] = 0;\n'
     '            goto loc_00263130;\n'
     '          } }\n'
     '        { int64_t a = (int32_t)ctx->gpr[11]; int64_t b = (int32_t)ctx->gpr[3]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n'
     '        if ((!((ctx->cr >> 0) & 2))) goto loc_002630D8;\n'),
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

    seen = {fn: False for fn, _, _, _ in SITES}
    refused: list[str] = []
    pending: list[tuple] = []
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

    for p, text in pending:
        try:
            p.write_text(text, encoding="utf-8", newline="\n")
        except TypeError:
            p.write_text(text, encoding="utf-8")
    print("[%s] ok (%d aplicado, %d ja' aplicado)" % ('263040-freelist-tag-guard', applied, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
