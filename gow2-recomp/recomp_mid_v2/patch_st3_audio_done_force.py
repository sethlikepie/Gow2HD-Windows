#!/usr/bin/env python3
"""HLE A3b: force the state-3 audio gate when host marks stream complete.

WHY
---
Task 3b class A3b: st620 parks at 3 because func_002C0FA0 calls
func_0045B2A8(obj+0x720) and only advances when the return is non-zero
(literal 0 parks; -1 advances). The return is *(session+0x1B8) after
generational resolve. On Mac the guest service loop never sets +0x1B8.

movie_eos_arm.c marks stream-complete after the real .wav duration
(produtor MOVIEDONE) by either writing session+0x1B8 or setting
g_movie_audio_gate_force=1. This patch makes the live site honour that
flag so the FSM advances 3->4 without forging st620 or +0x744.

PLACEMENT
---------
O unico sitio do lift que le `[r30+0x720]` e chama func_0045B2A8 (medido:
1 ocorrencia em todo o lift). A insercao fica logo apos o retorno da chamada e
antes do teste de r3, exactamente como no lift antigo.

Gated by the host variable (always linked); host sets it only when
movie_audio_should_mark_done allows (done producer + st==3 + h720).
Idempotent marker AUDDONE-FORCE.

REPARACAO 2026-07-25 (re-lift com lifter novo)
----------------------------------------------
Duas mudancas de forma partiam a agulha literal:

  1. fragmento->label: `func_002C0FA0` deixou de ser uma funcao propria. O lifter
     novo fundiu o fragmento no corpo de func_002C0508 (a pump da FSM da intro) e
     o sitio passou a ser o label `loc_002C0FA0:`. NAO e' codigo diferente: sao as
     mesmas duas instrucoes guest (lwz r3,0x720(r30); bl 0045B2A8), agora
     alcancadas por `goto` em vez de chamada.
  2. shape-LR: a chamada ganhou o prefixo `ctx->lr = 0x002C0FA8; `.

A agulha passa a regex que aceita AMBAS as ancoras (assinatura de funcao antiga
OU label novo) e o prefixo de LR opcional -- casa com o lift antigo e com o novo.
Alem disso o alvo passa por `resolve_lift_paths` (aceita dir ou ficheiro).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARKER = "AUDDONE-FORCE"

ROOT = (Path(sys.argv[1]) if len(sys.argv) > 1
        else Path(__file__).resolve().parent.parent / "recomp_macos_v2")

NEEDLE_RE = re.compile(
    r"(?:void func_002C0FA0\(ppu_context\* ctx\) \{|loc_002C0FA0:)\n"
    r"        ctx->gpr\[3\] = vm_read32\(ctx->gpr\[30\] \+ 0x720\);\n"
    r"        (?:ctx->lr = 0x[0-9A-Fa-f]+; )?func_0045B2A8\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
)

# Inserido logo a seguir ao bloco casado (a chamada e' reemitida tal e qual, com
# o LR que o lifter tiver posto -- nunca reescrito a mao).
INSERT_TAIL = (
    "        /* " + MARKER + ": A3b HLE -- se host marcou stream-complete, "
    "forca rc!=0 (equiv. sess+0x1B8) */\n"
    "        { extern int g_movie_audio_gate_force;\n"
    "          if (g_movie_audio_gate_force) ctx->gpr[3] = 1; }\n"
)


def patch_file(p: Path) -> str:
    t = p.read_text(encoding="utf-8", errors="replace")
    if MARKER in t:
        return "ALREADY"
    hits = list(NEEDLE_RE.finditer(t))
    if not hits:
        return "SKIP"
    if len(hits) > 1:
        raise SystemExit(
            "%s: agulha AUDDONE ambigua (%d sitios) -- reveja antes de forcar"
            % (p.name, len(hits))
        )
    m = hits[0]
    t = t[: m.end()] + INSERT_TAIL + t[m.end():]
    p.write_text(t, encoding="utf-8")
    return "APPLIED"


def main() -> int:
    files = [p for p in resolve_lift_paths(sys.argv[1:], str(ROOT)) if p.exists()]
    if not files:
        print("nenhum ppu_recomp_*.cpp em %s" % ROOT)
        return 1
    any_hit = False
    for p in files:
        r = patch_file(p)
        if r != "SKIP":
            any_hit = True
            print("%s: %s" % (p.name, r))
    if not any_hit:
        print("SKIP: needle not found (func_002C0FA0)")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
