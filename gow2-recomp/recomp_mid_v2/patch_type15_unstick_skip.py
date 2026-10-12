#!/usr/bin/env python3
"""Repoe o bloco TYPE15-UNSTICK-SKIP apagado pelo re-lift em func_000CC9D0.

Contexto
--------
`recomp_macos_v2.pre_v4/ppu_recomp_000.cpp` (o lift que gera o binario que
funciona, linhas 163800-163847) tem um bloco que o re-lift actual
(`recomp_macos_v2`) NAO tem -- confirmado por
`grep -c "CC9D0-SKIP\\|CC9D0-LIVE-SKIP"` == 0 em todos os 7 chunks e por
`inventory_lift_markers.py` (ver notes/2026-07-31-fase10-inventario-marcadores.md,
linha "TYPE15-UNSTICK-SKIP ... SEM candidato -- buraco novo").

O proprio comentario do bloco explica o motivo de existir: o produto do
CB56C (shell/reuse) nunca avanca; a lista pai re-tickeia o mesmo objecto para
sempre. Sem este skip, `func_000CC9D0` gira ~7.8M vezes sobre os mesmos dois
objectos e o main nunca chega a `cellPadGetData` nem liberta a giant lock —
e' o motivo directo de `cellPadGetData` e `SetFlip_after_R_Perm` medirem 0
durante todo o boot.

`PS3_TYPE15_UNSTICK` ja' e' exportado pela recipe menu-fast
(`rodar_gow2_menu_fast.sh` / `lib_boot_chain_metrics.sh`) -- ate' agora era
um no-op silencioso porque o codigo que le a variavel simplesmente nao
existia no lift actual.

Este patch NAO reescreve o bloco de memoria: e' o texto VERBATIM extraido de
`recomp_macos_v2.pre_v4/ppu_recomp_000.cpp:163800-163847` (mesmo metodo usado
para os 7 patches FIOS da Fase 9 e o AREAD-HLE da Fase 10). A unica adaptacao
e' o ponto de insercao: como o lift actual escreve os mesmos offsets de stack
(0xB8/0xC0/0xC8/0xD0/0xD8/0xF0) antes do bloco TRACE_CC9D0 (so' difere na
cache local `_cs_28.._cs_31`, que nao interfere com os `vm_read64` do
epilogo deste bloco), o bloco e' inserido imediatamente antes do
TRACE_CC9D0 -- o mesmo lugar relativo que ocupava no pre_v4 (o M3
CC9D0-LIVE-SKIP, quando `patch_cc9d0_live_yield.py` for reaplicado depois
deste, insere-se ENTRE este bloco e o TRACE, pela mesma agulha).

Gate: o proprio bloco já vem gated (`PS3_TYPE15_UNSTICK`, default ON,
`PS3_TYPE15_UNSTICK=0` desliga) -- nao e' um patch condicional, e' reposicao
de codigo que existia.

Achado adicional (mesma classe de buraco, sem marcador -- nao aparece no
`inventory_lift_markers.py` por nao ser um `[TAG-COM-TRACOS]`): o bloco usa
`usleep()`, mas `recomp_macos_v2/ppu_recomp_000.cpp` actual NAO inclui
`<unistd.h>` (o pre_v4 tem `#include <unistd.h>` na linha 15; o chunk 000
actual so' tem stdio/stdlib/signal). Sem o include o chunk simplesmente nao
compila (`error: use of undeclared identifier 'usleep'`) -- este patch repoe
tambem esse include, de forma idempotente, porque e' um pre-requisito directo
do bloco que ele instala (Regra 3 do CLAUDE.md: fix de bloqueio directamente
causado por esta mudanca).

Idempotente: marcador presente -> ALREADY, rc=0.
rc: 0 aplicado ou ja-aplicado; 2 func_000CC9D0 nao encontrada em nenhum
    chunk; 3 agulha em falta ou forma inesperada (lift mudou).
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARKER = "TYPE15-UNSTICK-SKIP"
DEFAULT = "recomp_macos_v2/ppu_recomp_000.cpp"
FN_SIG = "void func_000CC9D0(ppu_context* ctx) {\n"

# Texto imediatamente antes do bloco TRACE_CC9D0 -- ponto de insercao. E' o
# mesmo needle que patch_cc9d0_live_yield.py usa para inserir o M3 a seguir,
# por isso este bloco tem de terminar exactamente com o epilogo que aquele
# patch espera encontrar antes desta agulha.
NEEDLE = (
    '        { static int on=-1; if(on<0){extern char* getenv(const char*);\n'
    '            const char* e=getenv("PS3_TRACE_CC9D0");'
)

# Verbatim de recomp_macos_v2.pre_v4/ppu_recomp_000.cpp:163800-163847.
BLOCK = r'''        /* TYPE15-UNSTICK-SKIP: CB56C shell/reuse product never advances;
         * parent list re-ticks these forever. Skip body + yield so pad/RSX
         * threads can run (menu bring-up). Default ON; PS3_TYPE15_UNSTICK=0. */
        { static int s_us = -1;
          if (s_us < 0) {
            const char* e = getenv("PS3_TYPE15_UNSTICK");
            s_us = (!e || *e != '0') ? 1 : 0;
          }
          if (s_us) {
            uint32_t th = (uint32_t)ctx->gpr[31];
            uint32_t prod = (th >= 0x10000u && th < 0x4F000000u)
                ? vm_read32(th + 0x8u) : 0u;
            /* Only pin-zone shells that never got real attach. Live reused
             * products (e.g. 0x42F85AE4) now complete icall2+2A4FE4 after
             * product+0x70 list sanitize — do not skip their ticks. */
            int skip = (prod >= 0x47D00800u && prod < 0x47D00C00u);
            if (skip) {
              { static int n = 0;
                if (n++ < 8)
                  fprintf(stderr,
                          "[TYPE15] CC9D0-SKIP this=0x%08X prod=0x%08X "
                          "(pin-shell only)\n",
                          th, prod);
              }
              /* Cooperative yield: pure skip still burns main at ~1e6/s and
               * never reaches cellPadGetData. Release giant lock occasionally. */
              { static unsigned y = 0;
                if ((++y & 0x3FFu) == 0u) {
                  ppu_giant_lock_release();
#ifndef _WIN32
                  usleep(500);
#endif
                  ppu_giant_lock_acquire();
                }
              }
              ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0xF0);
              ctx->gpr[28] = vm_read64(ctx->gpr[1] + 0xB8);
              ctx->gpr[29] = vm_read64(ctx->gpr[1] + 0xC0);
              ctx->lr = ctx->gpr[0];
              ctx->gpr[30] = vm_read64(ctx->gpr[1] + 0xC8);
              ctx->gpr[31] = vm_read64(ctx->gpr[1] + 0xD0);
              { uint64_t tmp = vm_read64(ctx->gpr[1] + 0xD8);
                memcpy(&ctx->fpr[31], &tmp, 8); }
              ctx->gpr[1] = (int64_t)(int32_t)(ctx->gpr[1] + 0xE0);
              return;
            }
          }
        }
'''


UNISTD_INCLUDE = "#include <unistd.h>\n"
PPU_RECOMP_INCLUDE = '#include "ppu_recomp.h"\n'


def ensure_unistd(t: str) -> tuple[str, bool]:
    """Garante `#include <unistd.h>` no chunk (pre-requisito do usleep() do
    bloco). Idempotente: no-op se ja presente em qualquer forma."""
    if "#include <unistd.h>" in t:
        return t, False
    if PPU_RECOMP_INCLUDE in t:
        return t.replace(PPU_RECOMP_INCLUDE, PPU_RECOMP_INCLUDE + UNISTD_INCLUDE, 1), True
    # Fallback defensivo: prepende ao ficheiro (nunca deveria disparar --
    # todo chunk gerado pelo lifter comeca com o include de ppu_recomp.h).
    return UNISTD_INCLUDE + t, True


def patch(path: Path) -> tuple[bool, int]:
    t = path.read_text(encoding="utf-8", errors="replace")
    changed_include = False

    if f"/* {MARKER}:" in t:
        t2, changed_include = ensure_unistd(t)
        if changed_include:
            try:
                path.write_text(t2, encoding="utf-8", newline="\n")
            except TypeError:
                path.write_text(t2, encoding="utf-8")
            print(f"  {path.name}: ALREADY (marcador presente) + include <unistd.h> reposto")
            return True, 0
        print(f"  {path.name}: ALREADY (marcador presente)")
        return False, 0

    if FN_SIG not in t:
        print(f"  {path.name}: skip (sem {FN_SIG.strip()})")
        return False, -1

    if t.count(FN_SIG) != 1:
        print(f"ERRO: {FN_SIG.strip()} aparece {t.count(FN_SIG)}x em {path.name} "
              f"(esperado 1)", file=sys.stderr)
        return False, 3

    fn_start = t.find(FN_SIG)
    fn_end = t.find("\n}\n", fn_start)
    if fn_end < 0:
        print(f"ERRO: {path.name} nao tem fim de func_000CC9D0", file=sys.stderr)
        return False, 3
    body = t[fn_start:fn_end]

    n = body.count(NEEDLE)
    if n != 1:
        print(f"ERRO: agulha (pre-TRACE_CC9D0) aparece {n}x no corpo de "
              f"func_000CC9D0 em {path.name} (esperado 1) -- lift mudou de "
              f"forma; NAO aplicado", file=sys.stderr)
        return False, 3

    at = body.find(NEEDLE)
    body = body[:at] + BLOCK + body[at:]

    t = t[:fn_start] + body + t[fn_end:]
    t, _ = ensure_unistd(t)

    try:
        path.write_text(t, encoding="utf-8", newline="\n")
    except TypeError:                                # macOS system python3 (3.9.6)
        path.write_text(t, encoding="utf-8")
    print(f"  {path.name}: APPLIED (repoe TYPE15-UNSTICK-SKIP)")
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
        # rc == -1: funcao nao esta' neste chunk, ignora

    if hard_fail:
        print(f"[{MARKER}] rc=3 ({hard_fail} falha(s) de forma)")
        return 3
    if n_missing == len(paths):
        print(f"[{MARKER}] rc=2 (nenhum chunk de lift encontrado)")
        return 2
    if applied_any or already_any:
        print(f"[{MARKER}] rc=0")
        return 0
    print(f"[{MARKER}] rc=2 (func_000CC9D0 nao encontrada em nenhum chunk)")
    return 2


if __name__ == "__main__":
    sys.exit(main())
