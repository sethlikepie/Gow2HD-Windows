#!/usr/bin/env python3
"""Instala o bloco INTROSEQ do CE03C: wait-idle do 1o filme + re-Play natural.

Porque existe
-------------
`func_000CE03C` no lift de producao tem 143 linhas; o lifter gera 27. As outras
116 sao uma edicao MANUAL dentro do lift gitignored que NENHUM script repunha --
provado em 2026-07-25 aplicando os 71 patches a um lift limpo: a funcao ficava
nas 27 linhas geradas.

O `patch_ce03c_wait_idle_f2b_movie.py` que existia e' um VERIFICADOR PURO (zero
escritas): confirmava os marcadores e reportava MISSING, sem nunca os instalar.
Era o padrao que escondeu o problema.

O que o bloco faz (comentario do proprio codigo)
------------------------------------------------
  Intro idx-2 natural 2nd Play (guest already calls Play here).
  Wait-idle only: CE03C often races while st620!=0 -> SAI CEDO.
  Pump until natural MovieStop (st=0); arm +0x714 at st>=10 for EOS.
  Clear sticky EOS hook + done timer so re-Play is not poisoned.

Inclui a chamada a movie_done_timebased_reset() -- por isso este script torna
o patch_ce03c_movie_done_reset.py redundante quando corre primeiro (esse
reporta ALREADY). Os dois coexistem de proposito: o pequeno serve o caso em que
o bloco ja' existe mas a chamada nao.

NAO inclui as linhas de probe (SCHEDARM-PROBE / MENUPRESENT-PROBE): essas tem
escritores proprios (patch_sched_arm_probe.py, patch_menu_present_schedule_probe.py)
e sao aplicadas por eles.

MIGRADO PARA MID-ASM (Fase 17, plano 17-03) -- ler antes de mexer
-----------------------------------------------------------------
O bloco wait-idle deste script deixou de ser a fonte de verdade. Vive agora em
codigo host VERSIONADO, `games/gow2/hooks/gow2_midasm_hooks.cpp`
(`gow2_midasm_Ce03cWaitIdle`), e o `ppu_lifter.py` emite a chamada sozinho a
partir da entrada `[[midasm_hook]] address = 0x000CE03C` de
`games/gow2/config/gow2_recomp.toml`. Um lift feito com `--config` ja' vem com
o fix la' dentro -- sem correr patch nenhum. E' esse o ponto da migracao.

Este ficheiro NAO foi apagado (politica A do plano 17-03): a producao ainda
corre lifts ANTIGOS, gerados sem `--config`, e esses continuam a precisar do
texto injectado. O script passou a DISTINGUIR os dois casos:

  - funcao ja' contem `gow2_midasm_Ce03cWaitIdle(` -> MIDASM, nao escreve nada
  - funcao ja' contem o bloco injectado             -> ALREADY, nao escreve nada
  - funcao tem o corpo gerado esperado              -> injecta (lift antigo)

E MIGRADO PARA WEAK OVERRIDE (Fase 19, plano 19-03) -- a outra metade
--------------------------------------------------------------------
O que o mid-asm NAO absorveu era o pad de setjmp/longjmp (`g_ce03c_play_abort`)
a' volta do corpo natural, com `cellVdec_stop_all_for_play_abort()` e a
reconstrucao da freelist do media object. Um `[[midasm_hook]]` corre AO LADO de
uma instrucao, nao ENVOLVE a funcao. Essa metade vive agora em
`games/gow2/hooks/gow2_func_overrides.cpp` (`func_000CE03C`), declarada como
`[[functions_override]] address = 0x000CE03C` no `gow2_recomp.toml`.

Num lift feito com `--config` + `emit_weak_wrappers`, a funcao deixa de ser
emitida na forma `void func_000CE03C(ppu_context* ctx) {` e passa a sair como o
par `PPC_FUNC_IMPL(func_000CE03C)` (corpo) + `PPC_FUNC(func_000CE03C)`
(wrapper fraco). Este script procura a forma ANTIGA por texto; sem a deteccao
abaixo daria "func_000CE03C nao existe" e rc=2 -- um vermelho FALSO, porque o
fix esta' la', vindo do host. Dai' o estado WEAK.

Contrato
--------
- weak override presente -> WEAK (skip), rc=0
- mid-asm ja' presente   -> MIDASM (skip), rc=0
- ja' instalado          -> ALREADY, rc=0
- corpo gerado esperado  -> substitui, rc=0
- corpo diferente do esperado -> RECUSA, rc=2 (nao adivinha: o lifter mudou e o
  bloco tem de ser revalidado a mao antes de ser reaplicado)

Uso:  patch_ce03c_introseq_block.py [LIFT_DIR_OU_FICHEIRO...]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths            # noqa: E402

SIG = "void func_000CE03C(ppu_context* ctx) {\n"
MARKER = "[INTROSEQ] CE03C wait-idle 1st movie"

# A chamada que o ppu_lifter.py emite quando corre com --config (Fase 17).
# Procurada SO' dentro da regiao de func_000CE03C, nunca no ficheiro inteiro: o
# lifter tambem escreve a DECLARACAO `void gow2_midasm_Ce03cWaitIdle(...);` no
# preambulo de TODAS as TUs geradas, e um `in t` global daria MIDASM em chunks
# que nem sequer tem a funcao.
MIDASM_CALL = "gow2_midasm_Ce03cWaitIdle(ctx);"

# Forma que o lifter emite quando a funcao esta' declarada em
# [[functions_override]] com [main].emit_weak_wrappers = true (Fase 19). A
# funcao inteira passou a ser propriedade do host: o corpo liftado vira
# __imp_func_000CE03C e o simbolo func_000CE03C e' o override de
# games/gow2/hooks/gow2_func_overrides.cpp. Procurada no ficheiro inteiro (e
# nao numa regiao) porque e' ela propria o delimitador do inicio da funcao.
WEAK_IMPL = "PPC_FUNC_IMPL(func_000CE03C)"


def func_region(t: str) -> str | None:
    """Fatia [inicio de func_000CE03C, inicio da funcao seguinte).

    Mesma delimitacao que o check_contracts.py e os patches OPD ja' usam.
    """
    i = t.find(SIG)
    if i < 0:
        return None
    j = t.find("void func_", i + 20)
    return t[i:j] if j >= 0 else t[i:]

# Corpo GERADO que este patch espera encontrar (lifter de 2026-07-25).
CLEAN_BODY = 'void func_000CE03C(ppu_context* ctx) {\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x5E44);\n        ctx->gpr[5] = (int64_t)(int32_t)(0x7EFF);\n        ctx->gpr[11] = vm_read32(ctx->gpr[2] + -0x5E48);\n        ctx->gpr[7] = (int64_t)(int32_t)(0);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x5E4C);\n        ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x0);\n        ctx->gpr[4] = vm_read32(ctx->gpr[11] + 0x0);\n        ctx->gpr[0] = ctx->gpr[0] ^ 0x1;\n        ctx->gpr[9] = ppc_sraw(&ctx->xer, (int32_t)ctx->gpr[0], 0x1F);\n        ctx->gpr[6] = ctx->gpr[9] ^ ctx->gpr[0];\n        ctx->gpr[6] = ctx->gpr[6] - ctx->gpr[9];\n        ctx->gpr[6] = ctx->gpr[6] + (int64_t)(-1);\n        ctx->gpr[6] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[6], 1, 31, 31);\n        ctx->gpr[6] = (int64_t)(int32_t)ctx->gpr[6];\n        ctx->lr = 0x000CE078; func_002C00DC(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[3] = (int64_t)(int32_t)(1);\n        ctx->lr = 0x000CE084; func_002BFF00(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000CE08C; func_002C0508(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[30] + 0x0);\n        ctx->gpr[9] = ctx->gpr[9] + (int64_t)(1);\n        vm_write32(ctx->gpr[30] + 0x0, ctx->gpr[9]);\n        { g_trampoline_fn = (void(*)(void*))func_000CE01C; return; }\n}\n'

# Corpo COM o bloco INTROSEQ, extraido verbatim do lift de producao.
PATCHED_BODY = 'void func_000CE03C(ppu_context* ctx) {\n        /* Intro idx→2 natural 2nd Play (guest already calls Play here).\n         * Wait-idle only: CE03C often races while st620≠0 → SAI CEDO.\n         * Pump until natural MovieStop (st=0); arm +0x714 at st≥10 for EOS.\n         * Clear sticky EOS hook + done timer so re-Play is not poisoned.\n         * No soft-clear/soft-park of st620 (not a pass path). No inject. */\n        const uint64_t _sv_r30 = ctx->gpr[30];\n        { uint32_t _mv = vm_read32((uint32_t)ctx->gpr[2] - 0x1124u);\n          uint32_t _st = (_mv >= 0x10000u && _mv < 0x4F000000u) ? vm_read32(_mv + 0x620u) : 0u;\n          if (_st != 0u && _mv >= 0x10000u && _mv < 0x4F000000u) {\n            { static int _n=0; if(_n++<8)\n                fprintf(stderr,"[INTROSEQ] CE03C wait-idle 1st movie st620=%u\\n", _st); }\n            for (int _i = 0; _i < 600; _i++) {\n              if (_st >= 0xAu && vm_read32(_mv + 0x714u) == 0u) {\n                vm_write32(_mv + 0x714u, 1u);\n                { static int _n=0; if(_n++<4)\n                    fprintf(stderr,"[INTROSEQ] CE03C arm +0x714 st=%u\\n", _st); }\n              }\n              for (int _j = 0; _j < 32; _j++) {\n                func_002C0508(ctx); DRAIN_TRAMPOLINE(ctx);\n                _st = vm_read32(_mv + 0x620u);\n                if (_st == 0u) break;\n              }\n              if (_st == 0u) break;\n              ppu_giant_lock_release();\n              usleep(50000);\n              ppu_giant_lock_acquire();\n              if ((_i % 40) == 0) {\n                static int _n=0; if(_n++<16)\n                  fprintf(stderr,"[INTROSEQ] CE03C wait tick st620=%u i=%d\\n", _st, _i);\n              }\n            }\n            { static int _n=0; if(_n++<8)\n                fprintf(stderr,"[INTROSEQ] CE03C wait-idle exit st620=%u\\n", _st); }\n          }\n          /* Sticky EOS from movie#1 must not force-done mid re-Play. */\n          { extern uint32_t g_movie_eos_ea;\n            if (g_movie_eos_ea != 0u) {\n              fprintf(stderr,"[INTROSEQ] CE03C clear sticky EOS hook 0x%08X\\n",\n                      g_movie_eos_ea);\n              fflush(stderr);\n              g_movie_eos_ea = 0u;\n            }\n            if (_mv >= 0x10000u && _mv < 0x4F000000u) {\n              vm_write8(_mv + 0x744u, 0);\n              vm_write8(_mv + 0x745u, 0);\n              vm_write8(_mv + 0x746u, 0);\n            }\n            { extern void movie_vt_clear_overlay_done(void) asm("_movie_vt_clear_overlay_done");\n              movie_vt_clear_overlay_done(); }\n            { extern void movie_done_timebased_reset(void) asm("_movie_done_timebased_reset");\n              movie_done_timebased_reset(); }\n          }\n        }\n        ctx->gpr[30] = _sv_r30;\n        /* Natural guest body (original lift). Arm longjmp pad so path-as-code\n         * FO residual after real StartSeq#2 can abort Play host stack without\n         * PARK/forge of StartSeq (see ps3_indirect_call ICALL-ASCII). */\n        { extern void* g_ce03c_play_abort;\n          jmp_buf _jb;\n          const uint64_t _sv_r2 = ctx->gpr[2]; /* FO hang may trash TOC */\n          g_ce03c_play_abort = (void*)&_jb;\n          if (setjmp(_jb) == 0) {\n            ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x5E44);\n            ctx->gpr[5] = (int64_t)(int32_t)(0x7EFF);\n            ctx->gpr[11] = vm_read32(ctx->gpr[2] + -0x5E48);\n            ctx->gpr[7] = (int64_t)(int32_t)(0);\n            ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x5E4C);\n            ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x0);\n            ctx->gpr[4] = vm_read32(ctx->gpr[11] + 0x0);\n            ctx->gpr[0] = ctx->gpr[0] ^ 0x1;\n            ctx->gpr[9] = (int64_t)(int32_t)((int32_t)ctx->gpr[0] >> 0x1F);\n            ctx->gpr[6] = ctx->gpr[9] ^ ctx->gpr[0];\n            ctx->gpr[6] = ctx->gpr[6] - ctx->gpr[9];\n            ctx->gpr[6] = (int64_t)(int32_t)(ctx->gpr[6] + -1);\n            ctx->gpr[6] = (uint32_t)ppc_rlwinm((uint32_t)ctx->gpr[6], 1, 31, 31);\n            ctx->gpr[6] = (int64_t)(int32_t)ctx->gpr[6];\n            func_002C00DC(ctx); DRAIN_TRAMPOLINE(ctx);\n            /* nop */;\n            ctx->gpr[3] = (int64_t)(int32_t)(1);\n            func_002BFF00(ctx); DRAIN_TRAMPOLINE(ctx);\n            /* nop */;\n            func_002C0508(ctx); DRAIN_TRAMPOLINE(ctx);\n            /* nop */;\n          } else {\n            { static int _n=0; if(_n++<4)\n                fprintf(stderr,"[INTROSEQ] CE03C Play aborted (FO residual after StartSeq#2)\\n"); }\n            g_trampoline_fn = 0;\n            ctx->gpr[2] = _sv_r2; /* restore TOC before any guest mem access */\n            { extern void cellVdec_stop_all_for_play_abort(void)\n                  asm("_cellVdec_stop_all_for_play_abort");\n              cellVdec_stop_all_for_play_abort(); }\n            /* Host-stack abort left guest mid-Play: park player + re-seed\n             * freelist so WAD open after intro does not FREELIST-TAG-GUARD. */\n            { uint32_t toc = (uint32_t)_sv_r2;\n              uint32_t _mv = (toc >= 0x1124u) ? vm_read32(toc - 0x1124u) : 0u;\n              if (_mv >= 0x10000u && _mv < 0x4F000000u) {\n                vm_write32(_mv + 0x620u, 0u);\n                vm_write8(_mv + 0x744u, 0);\n                vm_write8(_mv + 0x745u, 0);\n                vm_write8(_mv + 0x746u, 0);\n              }\n              { extern uint32_t g_movie_eos_ea; g_movie_eos_ea = 0u; }\n              uint32_t slot = (toc >= 0x1460u) ? vm_read32(toc - 0x1460u) : 0u;\n              uint32_t media = (slot && slot < 0x4F000000u) ? vm_read32(slot + 0x118u) : 0u;\n              if (media >= 0x10000u && media < 0x4F000000u) {\n                if (vm_read32(media + 0x16Cu) == 0u) vm_write32(media + 0x16Cu, 1u);\n                const uint32_t offs[8] = {0x250u,0x330u,0x410u,0x4F0u,0x5D0u,0x6B0u,0x790u,0x870u};\n                uint32_t chain = 0;\n                for (int i = 7; i >= 0; i--) {\n                  uint32_t op = media + offs[i];\n                  if (op >= 0x4F000000u) continue;\n                  vm_write32(op + 0x0u, chain);\n                  vm_write32(op + 0x90u, 0u);\n                  vm_write32(op + 0x40u, 0u);\n                  vm_write32(op + 0x44u, 0u);\n                  chain = op;\n                }\n                vm_write32(media + 0x200u, chain);\n                vm_write32(media + 0x218u, 0u);\n                fprintf(stderr,"[INTROSEQ] CE03C post-abort freelist rebuild media=0x%08X\\n", media);\n                fflush(stderr);\n              } else {\n                fprintf(stderr,"[INTROSEQ] CE03C post-abort freelist SKIP toc=0x%08X media=0x%08X\\n",\n                        toc, media);\n                fflush(stderr);\n              }\n            }\n          }\n          g_ce03c_play_abort = 0;\n          ctx->gpr[2] = _sv_r2;\n        }\n        ctx->gpr[30] = _sv_r30;\n        ctx->gpr[9] = vm_read32(ctx->gpr[30] + 0x0);\n        ctx->gpr[9] = (int64_t)(int32_t)(ctx->gpr[9] + 1);\n        vm_write32(ctx->gpr[30] + 0x0, ctx->gpr[9]);\n        { g_trampoline_fn = (void(*)(void*))func_000CE01C; return; }\n        { g_trampoline_fn = (void(*)(void*))func_000CE0A0; return; }\n}\n'


def patch_one(path: Path) -> int:
    """0 aplicado | 1 ja' aplicado | 2 mid-asm | 3 weak | -1 sem a funcao |
    -2 corpo inesperado."""
    t = path.read_text(encoding="utf-8", errors="replace")
    if WEAK_IMPL in t:
        # Lift com [[functions_override]]: a funcao inteira e' host. O pad de
        # setjmp e o resto do bloco vem de gow2_func_overrides.cpp -- injectar
        # texto aqui reporia a duplicacao que a migracao eliminou (e nem casaria,
        # porque a forma `void func_000CE03C(...)` ja' nao existe).
        print(f"  {path.name}: WEAK (override host -- nada a injectar)")
        return 3
    if SIG not in t:
        return -1
    region = func_region(t) or ""
    if MIDASM_CALL in region:
        # Lift gerado com --config: o fix ja' la' esta', emitido pelo lifter, e
        # o corpo vive em games/gow2/hooks/gow2_midasm_hooks.cpp. Reinjectar as
        # 116 linhas por cima seria repor a duplicacao que a migracao eliminou.
        print(f"  {path.name}: MIDASM (hook emitido pelo lifter -- nada a injectar)")
        return 2
    if MARKER in t:
        print(f"  {path.name}: ALREADY")
        return 1
    if CLEAN_BODY not in t:
        return -2
    t = t.replace(CLEAN_BODY, PATCHED_BODY, 1)
    try:
        path.write_text(t, encoding="utf-8", newline="\n")
    except TypeError:
        path.write_text(t, encoding="utf-8")
    print(f"  {path.name}: APPLIED ({PATCHED_BODY.count(chr(10))} linhas)")
    return 0


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], "ppu_recomp_001.cpp") if p.is_file()]
    if not paths:
        print("ERRO: nenhum ficheiro de lift legivel", file=sys.stderr)
        return 3
    applied = already = midasm = weak = unexpected = 0
    for p in paths:
        r = patch_one(p)
        if r == 0:
            applied += 1
        elif r == 1:
            already += 1
        elif r == 2:
            midasm += 1
        elif r == 3:
            weak += 1
        elif r == -2:
            unexpected += 1
    if weak:
        print(f"[ce03c-introseq] SKIP: weak override e' a fonte de verdade neste "
              f"lift ({weak} ficheiro(s) com PPC_FUNC_IMPL(func_000CE03C); "
              f"wait-idle pelo mid-asm, pad pelo override host)")
        return 0
    if midasm:
        print(f"[ce03c-introseq] SKIP: mid-asm e' a fonte de verdade neste lift "
              f"({midasm} ficheiro(s) com gow2_midasm_Ce03cWaitIdle; "
              f"{applied} aplicado, {already} ja' aplicado)")
        return 0
    if applied or already:
        print(f"[ce03c-introseq] ok ({applied} aplicado, {already} ja' aplicado)")
        return 0
    if unexpected:
        print("ERRO: func_000CE03C existe mas o corpo NAO e' o gerado esperado.\n"
              "  O lifter mudou. Nao substituo as cegas -- revalida o bloco\n"
              "  (ver notes/2026-07-25-baseline-depende-de-wip.md) e regenera este\n"
              "  script a partir de um lift de producao conhecido-bom.",
              file=sys.stderr)
        return 2
    print("ERRO: func_000CE03C nao existe em nenhum dos ficheiros dados.",
          file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
