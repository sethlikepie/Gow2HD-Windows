#!/usr/bin/env python3
"""Probe do conteudo do "shell" (this pointer da 2a chamada interna) em
func_0039E794, imediatamente antes e depois de `ps3_call_opd(ctx,
(uint32_t)ctx->gpr[10])` -- a chamada que despacha o metodo real de
construct via `vt[+0x14]` do objecto resolvido em `ctx->gpr[11]` (= slot-4,
ver notes/2026-07-31-type15-fl-slot-medido-fix-e-nova-parede.md).

Objectivo (distinguir as duas hipoteses da nota, secao "Hipotese para a
proxima medicao", sem inferencia):

  H1: o "shell" (endereco fixo reusado nas 3 chamadas, 0x40100844-4 nas
      corridas medidas) tem uma flag "ja usado" dentro do PROPRIO objecto
      que muda entre a 1a chamada (sucesso) e a 2a (NULL) -- o construct
      guest consome-a e o REHOME/REPLENISH nao sabe repo-la.
  H2: o shell fica byte-a-byte IDENTICO entre a 1a e a 2a chamada -- nesse
      caso a causa raiz nao esta' no objecto, esta' a montante (o CB56C nao
      devia despachar tipo 0x15 uma 2a vez para este slot).

Mede, sem adivinhar, comparando os dumps pre/post de CADA chamada:

  [TYPE15SHELL] pre#N  this=0x........ words: <16 x u32>
  [TYPE15SHELL] post#N this=0x........ r3=0x........ words: <16 x u32>

Gate (default OFF, regra 6 do CLAUDE.md)
  PS3_TRACE_TYPE15_SHELL=1   -> ON
  ausente / '' / '0' / outro -> OFF, no-op total no baseline

NAO altera fluxo de controlo guest -- so' le ctx/memoria guest e imprime.
Convivencia: insere-se imediatamente ANTES e DEPOIS do par
`ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x14);` / `ps3_call_opd(ctx,
(uint32_t)ctx->gpr[10]); DRAIN_TRAMPOLINE(ctx);` dentro do corpo de
func_0039E794 -- a 2a chamada interna (a 1a, em ctx->gpr[11] lido de
vt+0x4C, e' o popper generico da free-list; esta e' o construct real do
objecto resolvido).

Idempotente: marcador presente -> ALREADY, rc=0.
rc: 0 aplicado ou ja-aplicado; 2 nenhum chunk com a funcao; 3 agulha em
    falta ou forma inesperada (lift mudou).
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARKER = "TYPE15-SHELL-PROBE"
DEFAULT = "recomp_macos_v2/ppu_recomp_001.cpp"
FN_SIG = "void func_0039E794(ppu_context* ctx) {\n"

NEEDLE = ("        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x14);\n"
          "        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
          "        ps3_call_opd(ctx, (uint32_t)ctx->gpr[10]); "
          "DRAIN_TRAMPOLINE(ctx);\n")

HELPER = r'''
/* TYPE15-SHELL-PROBE: conteudo do "shell" (this da 2a chamada interna de
 * func_0039E794) antes/depois do construct real (PS3_TRACE_TYPE15_SHELL=1).
 * Read-only. */
static int ps3_ty15shell_gate_on(void) {
    static int on = -1;
    if (on < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_TYPE15_SHELL");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on;
}
static unsigned long long g_ps3_ty15shell_n = 0;
static void ps3_ty15shell_dump(const char* tag, uint32_t this_ea,
                                unsigned long long n, int have_r3, uint32_t r3) {
    fprintf(stderr, "[TYPE15SHELL] %s#%llu this=0x%08X", tag, n, this_ea);
    if (have_r3) fprintf(stderr, " r3=0x%08X", r3);
    fprintf(stderr, " words:");
    for (uint32_t i = 0; i < 16; i++)
        fprintf(stderr, " %08X", vm_read32(this_ea + i * 4u));
    fprintf(stderr, "\n");
    fflush(stderr);
}
'''


def patch(path: Path) -> tuple[bool, int]:
    """Devolve (mudou_algo, rc)."""
    t = path.read_text(encoding="utf-8", errors="replace")

    if f"/* {MARKER} pre */" in t:
        print(f"  {path.name}: ALREADY (marcador presente)")
        return False, 0

    if FN_SIG not in t:
        print(f"  {path.name}: skip (sem {FN_SIG.strip()})")
        return False, -1

    if t.count(FN_SIG) != 1:
        print(f"ERRO: {FN_SIG.strip()} aparece {t.count(FN_SIG)}x em {path.name} "
              f"(esperado 1)", file=sys.stderr)
        return False, 3

    body_start = t.find(FN_SIG) + len(FN_SIG)
    body_end = t.find("\n}\n", body_start)
    if body_end < 0:
        print(f"ERRO: {path.name} nao tem fim de func_0039E794", file=sys.stderr)
        return False, 3
    body = t[body_start:body_end]

    if body.count(NEEDLE) != 1:
        print(f"ERRO: agulha aparece {body.count(NEEDLE)}x no corpo de "
              f"func_0039E794 em {path.name} (esperado 1) -- lift mudou de "
              f"forma; NAO aplicado", file=sys.stderr)
        return False, 3

    at = body.find(NEEDLE)
    pre = (f"        /* {MARKER} pre */\n"
           "        uint32_t __ty15_shell_this = (uint32_t)ctx->gpr[3];\n"
           "        if (ps3_ty15shell_gate_on()) {\n"
           "          g_ps3_ty15shell_n++;\n"
           "          ps3_ty15shell_dump(\"pre\", __ty15_shell_this, "
           "g_ps3_ty15shell_n, 0, 0u);\n"
           "        }\n")
    post = (f"        /* {MARKER} post */\n"
            "        if (ps3_ty15shell_gate_on()) {\n"
            "          ps3_ty15shell_dump(\"post\", __ty15_shell_this, "
            "g_ps3_ty15shell_n, 1, (uint32_t)ctx->gpr[3]);\n"
            "        }\n")
    body = body[:at] + pre + NEEDLE + post + body[at + len(NEEDLE):]

    t = t[:body_start] + body + t[body_end:]

    anchor_at = t.find("\nvoid func_")
    if anchor_at < 0:
        print(f"ERRO: {path.name} nao tem nenhuma funcao lifted onde ancorar",
              file=sys.stderr)
        return False, 3
    t = t[:anchor_at + 1] + HELPER + t[anchor_at + 1:]

    try:
        path.write_text(t, encoding="utf-8", newline="\n")
    except TypeError:                                # macOS system python3 (3.9.6)
        path.write_text(t, encoding="utf-8")
    print(f"  {path.name}: APPLIED (shell dump pre/post around real construct call)")
    return True, 0


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], DEFAULT)
    print(f"[{MARKER}] alvos: {[str(p) for p in paths]}")

    applied_any = False
    already_any = False
    hard_fail = 0
    n_missing = 0
    for p in paths:
        if not p.exists():
            print(f"  {p}: MISSING")
            n_missing += 1
            continue
        changed, rc = patch(p)
        if rc == 3:
            hard_fail += 1
        elif rc == 0 and changed:
            applied_any = True
        elif rc == 0 and not changed:
            already_any = True
        # rc == -1: func nao esta' neste chunk, ignora

    if hard_fail:
        print(f"[{MARKER}] rc=3 ({hard_fail} falha(s) de forma)")
        return 3
    if n_missing == len(paths):
        print(f"[{MARKER}] rc=2 (nenhum chunk de lift encontrado)")
        return 2
    if applied_any or already_any:
        print(f"[{MARKER}] rc=0")
        return 0
    print(f"[{MARKER}] rc=2 (func_0039E794 nao encontrada em nenhum chunk)")
    return 2


if __name__ == "__main__":
    sys.exit(main())
