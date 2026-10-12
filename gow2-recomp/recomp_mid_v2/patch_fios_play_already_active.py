#!/usr/bin/env python3
"""When Play is re-entered with st620!=0, keep the in-flight FIOS open.

WHY
---
Guest EBOOT at 0x002C0498 does:
  bl  0x002BFF88   # teardown container (cancel + clear +8)
  b   0x002C0124   # re-enter full Play body -> re-open m2v

On the recompiler the teardown/re-open races the FIOS op pool:
  open#1 succeeds and writes container+8
  Play#2 teardowns, open#2 hits SEM OP LIVRE, writes 0
  state-1 poll sees io=0 forever

Hardware can free the cancelled op between teardown and re-open; under the
giant lock that window often does not exist.

FIX
---
Replace 002C0498 with Play's epilogue (same stack layout as 0x002C03FC):
return to the caller without teardown/re-open, preserving the in-flight
open for the state-1 poller.

Marker: FIOS-PLAY-ALREADY-ACTIVE. Idempotent.

MIGRADO PARA WEAK OVERRIDE (Fase 19, plano 19-03) -- ler antes de mexer
-----------------------------------------------------------------------
Este corpo deixou de ser a fonte de verdade. Vive agora em codigo host
VERSIONADO, `games/gow2/hooks/gow2_func_overrides.cpp` (`func_002C0498`),
declarado como `[[functions_override]] address = 0x002C0498` em
`games/gow2/config/gow2_recomp.toml`. Num lift feito com `--config` +
`emit_weak_wrappers` o simbolo `func_002C0498` que o jogo chama e' o do host --
sem correr patch nenhum.

E' o unico dos tres overrides da Fase 19 em que o host NAO chama `__imp_`: o
fix e' precisamente nao correr as duas instrucoes naturais (teardown + salto de
volta ao Play). Chamar o corpo original reporia o bug.

O ficheiro NAO foi apagado (politica A do 17-03): a producao ainda corre lifts
ANTIGOS, gerados sem `--config`, e esses continuam a precisar do texto
injectado. Sem a deteccao WEAK abaixo o script devolveria
"SKIP: func_002C0498 not found" e rc=1 -- vermelho FALSO, porque o fix esta'
la', vindo do host.
"""
from pathlib import Path
import re
import sys

MARKER = "FIOS-PLAY-ALREADY-ACTIVE"

ROOT = (Path(sys.argv[1]) if len(sys.argv) > 1
        else Path(__file__).resolve().parent.parent / "recomp_macos_v2")

NEW = '''/* %s:
 * Guest re-enters Play while st620!=0. Skip teardown+re-open so the
 * in-flight FIOS open (container+8) survives for state-1 poll. */
void func_002C0498(ppu_context* ctx) {
        ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0x2D0);
        ctx->gpr[25] = vm_read64(ctx->gpr[1] + 0x288);
        ctx->gpr[26] = vm_read64(ctx->gpr[1] + 0x290);
        ctx->lr = ctx->gpr[0];
        ctx->gpr[27] = vm_read64(ctx->gpr[1] + 0x298);
        ctx->gpr[28] = vm_read64(ctx->gpr[1] + 0x2A0);
        ctx->gpr[29] = vm_read64(ctx->gpr[1] + 0x2A8);
        ctx->gpr[30] = vm_read64(ctx->gpr[1] + 0x2B0);
        ctx->gpr[31] = vm_read64(ctx->gpr[1] + 0x2B8);
        ctx->gpr[1] = (int64_t)(int32_t)(ctx->gpr[1] + 0x2C0);
        ctx->gpr[3] = (int64_t)(int32_t)(0);
        return;
}
''' % MARKER


# Forma que o lifter emite quando a funcao esta' declarada em
# [[functions_override]] com [main].emit_weak_wrappers = true (Fase 19).
# Testada ANTES do "void func_002C0498", porque essa string tambem casa com a
# DECLARACAO que o lifter escreve no preambulo de todas as TUs.
WEAK_IMPL = "PPC_FUNC_IMPL(func_002C0498)"


def patch_file(p: Path) -> str:
    t = p.read_text(encoding="utf-8", errors="replace")
    if WEAK_IMPL in t:
        return "WEAK"
    if MARKER in t:
        return "ALREADY"
    if "void func_002C0498" not in t:
        return "SKIP"
    # Match original or any previously patched body of 002C0498.
    pat = re.compile(
        r'(?:/\*[^*]*FIOS[^*]*\*/\n'
        r'(?:extern "C" void [^\n]+\n)*\n)?'
        r'void func_002C0498\(ppu_context\* ctx\) \{.*?\n\}',
        re.S,
    )
    m = pat.search(t)
    if not m:
        return "SKIP"
    t = t[:m.start()] + NEW + t[m.end():]
    p.write_text(t, encoding="utf-8")
    return "APPLIED"


def main() -> int:
    files = sorted(ROOT.glob("ppu_recomp_*.cpp"))
    if not files:
        print("nenhum ppu_recomp_*.cpp em %s" % ROOT)
        return 1
    any_hit = False
    weak = False
    for p in files:
        r = patch_file(p)
        if r == "WEAK":
            weak = any_hit = True
            print("%s: WEAK (override host -- nada a injectar)" % p.name)
        elif r != "SKIP":
            any_hit = True
            print("%s: %s" % (p.name, r))
    if weak:
        print("[fios-play-already-active] SKIP: weak override e' a fonte de "
              "verdade neste lift (games/gow2/hooks/gow2_func_overrides.cpp)")
        return 0
    if not any_hit:
        print("SKIP: func_002C0498 not found")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
