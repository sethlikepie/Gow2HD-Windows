#!/usr/bin/env python3
"""Instala os blocos ORFAOS de que `patch_type15_cc9d0_disc.py` depende para
poder aplicar: as declaracoes do giant lock, o bloco de attach TYPE15 de
`func_000CB56C` e o logger `[CC9D0] iter=` de `func_000CC9D0`.

Porque existe
-------------
`patch_type15_cc9d0_disc.py` e' um ESCRITOR, mas so' sabe injectar POR CIMA de
codigo que era edicao MANUAL dentro do lift gitignored. Num lift limpo com
todos os patches aplicados esse codigo nao existe e o disc morre nas
pre-condicoes:

    PRE-CONDICAO AUSENTE: declaracao 'extern "C" void
    ppu_giant_lock_acquire(void);' nao esta neste chunk
    PRE-CONDICAO AUSENTE: bloco de attach CB56C ([POSTINTRO] CB56C after
    2A4FE4 / [TYPE15] CB56C SKIP_ATTACH / ty15_reused) nao existe no lift

Nenhum script versionado repunha esses blocos -- codigo sem dono, nao agulha
partida. Este ficheiro da'-lhes dono; so' depois o disc aplica legitimamente.
O verificador/escritor original nao foi tocado nem enfraquecido.

O que instala (fatiado do lift de producao recomp_macos_v2)
------------------------------------------------------------
1. No(s) chunk(s) que contem `func_000CB56C` / `func_000CC9D0`, a seguir a
   `#include "ppu_recomp.h"`:
       extern "C" void ppu_giant_lock_release(void);
       extern "C" void ppu_giant_lock_acquire(void);
   (producao tem-nas no preambulo do ppu_recomp_000.cpp ln.17-18; ver tambem
   lift_baseline/injected_000.cpp ln.11-12). Sao a ancora onde o disc insere o
   contador `static int g_ps3_type15_disc_n`. Declaracoes puras: nao mudam
   comportamento e re-declarar e' legal em C++, por isso ficam inofensivas se um
   futuro instalador do preambulo tambem as puser.

2. Em `func_000CB56C`, o bloco de attach TYPE15 (gate `PS3_TYPE15_SKIP_ATTACH`),
   que substitui o icall2 cru + `func_002A4FE4` gerados pelo lifter por: icall2
   validado (ent/vt/opd/code, com SKIP quando invalido em vez de saltar para
   lixo) e o ramo legacy SKIP_ATTACH. E' o bloco que LE a variavel
   `ty15_reused` que `patch_type15_cb56c_prefer_product_install.py` deixa
   declarada de proposito ("e' o nome exacto que o futuro escritor do attach
   precisa"). Por default (`PS3_TYPE15_SKIP_ATTACH` unset) `skip_icall2=0`, ou
   seja o caminho e' o attach completo -- o mesmo que o lifter gera, so' que
   validado.

3. Em `func_000CC9D0`, o logger `[CC9D0] iter=` imediatamente antes do ramo
   `func_000CCBF0` -- exactamente onde producao o tem. Probe gated por
   PS3_TRACE_CC9D0 (`e && *e && *e!='0'`), OFF por default.

Dependencia verificada em runtime (RECUSA se faltar)
-----------------------------------------------------
  * `int ty15_reused = 0;` em func_000CB56C, instalado por
    `patch_type15_cb56c_prefer_product_install.py`, que corre antes por ordem
    alfabetica ("cb56c" < "cc9d0"). Sem ele o bloco nao compilaria, por isso o
    script recusa (rc=2) em vez de escrever.

Linhas de producao deliberadamente FORA deste bloco
----------------------------------------------------
  * `ps3_type15_product_list_reset((uint32_t)ctx->gpr[29]);` (e o comentario
    "Always sanitize product+0x70 before attach"): instalado por
    `patch_type15_list_close_preserve_install.py`, que corre depois
    ("cc9d0" < "list") e o poe na mesma posicao da producao -- logo acima deste
    bloco, ancorado no par rldicl/or que aqui fica intacto -- e ainda com o
    filtro de r29 que o bloco `[POSTINTRO] reject` (outro orfao) faria. Duplicar
    a chamada era chamar o helper duas vezes; a regra e' nao incluir linhas que
    outro patch ja' instala. Consequencia: este bloco nao referencia nenhum
    helper host, so' o lifter e a libc.
  * Blocos vizinhos `[POSTINTRO] CB56C icall1`, `[TYPE15] freelist +24=`,
    `CB56C reject product` e `CB56C early-out`: nao sao pre-condicao do disc, e
    o early-out de producao repoe os callee-save a partir da pilha
    (`vm_read64(r1+0xD8)`), forma anterior a' correccao callee-save (`_cs_27`...)
    do lifter actual -- copia-lo poria um epilogo em desacordo com o resto da
    funcao.

Desvio consciente face a producao (documentado, nao inventado)
---------------------------------------------------------------
  * UMA linha: `func_002A4FE4(ctx)` passa a `ctx->lr = 0x000CB6AC;
    func_002A4FE4(ctx)`. O lifter actual carimba `ctx->lr` antes de cada `bl`; o
    lift de producao e' de um lifter anterior que nao o fazia. Copiar a forma
    antiga REGREDIRIA o lifter (mesmo criterio do
    patch_type15_cb56c_prefer_product_install.py: nao se copia o corpo velho por
    cima do novo).
  * O bloco tem fprintf `[POSTINTRO]` sem gate de env (com tecto de 8/12/16
    linhas por processo). E' a forma exacta que producao tem e que a agulha do
    disc exige literalmente; gate-los seria inventar uma variante. Tensao
    assumida com a regra 6 do CLAUDE.md: sao so' escritas em stderr com tecto,
    sem efeito no caminho do jogo.

Como foi gerado
---------------
Metodo do `patch_ce03c_introseq_block.py`, programaticamente por
scratchpad/g3_gen_base_blocks.py: (1) clonar o lift limpo e correr
`./apply_all_patches.sh <clone>`; (2) extrair `func_000CB56C`/`func_000CC9D0` do
clone e da producao; (3) diff -> o excedente da producao e' o bloco orfao; (4) o
texto da producao e' primeiro passado por
`patch_type15_cc9d0_disc.strip_disc()` para obter o estado PRE-DISC (prova de
fidelidade: `strip_disc(prod)` -> `disc.patch()` devolve a producao BYTE A
BYTE); (5) os literais sao embutidos por repr(), nunca transcritos a mao; (6) o
gerador so' termina depois de provar que `disc.patch()` aplica sem excepcao
sobre um lift com estes blocos.

Contrato de rc
--------------
- ja' instalado                          -> ALREADY, rc=0
- contexto gerado esperado + dependencia -> instala, rc=0
- dependencia em falta / corpo diferente -> RECUSA, rc=2 (nao adivinha:
  revalidar os blocos contra um lift de producao conhecido-bom e regenerar este
  script)
- nenhum chunk com func_000CB56C/func_000CC9D0 -> rc=2

Uso:  patch_type15_cc9d0_base_blocks.py [LIFT_DIR_OU_FICHEIRO...]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths            # noqa: E402

SIG_CC9D0 = "void func_000CC9D0(ppu_context* ctx) {\n"
SIG_CB56C = "void func_000CB56C(ppu_context* ctx) {\n"

HDR_ANCHOR = '#include "ppu_recomp.h"\n'
GL_DECLS = 'extern "C" void ppu_giant_lock_release(void);\nextern "C" void ppu_giant_lock_acquire(void);\n'
GL_MARKER = 'extern "C" void ppu_giant_lock_acquire(void);'

# --- func_000CB56C: bloco de attach TYPE15 ----------------------------------
CB_CLEAN = '        ctx->gpr[4] = ctx->gpr[29] | ctx->gpr[29];\n        ctx->gpr[0] = vm_read16(ctx->gpr[29] + 0x2);\n        ctx->gpr[0] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[0], 2, 14, 29);\n        ctx->gpr[11] = vm_read32((ctx->gpr[27] + ctx->gpr[0]));\n        ctx->gpr[3] = ctx->gpr[11] | ctx->gpr[11];\n        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x0);\n        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x18);\n        ctx->gpr[0] = vm_read32(ctx->gpr[10] + 0x0);\n        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n        ctx->ctr = (uint32_t)ctx->gpr[0];\n        ctx->gpr[2] = vm_read32(ctx->gpr[10] + 0x4);\n        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n        ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/\n        ctx->gpr[3] = ctx->gpr[29] | ctx->gpr[29];\n        vm_write32(ctx->gpr[31] + 0x8, ctx->gpr[28]);\n        ctx->lr = 0x000CB6AC; func_002A4FE4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n'     # icall2 cru + 2A4FE4, tal como o lifter gera
CB_ATTACH = '        /* TYPE15 attach (2026-07-23 fix):\n         * Root hang was 2A4FE4 walking a NULL-terminated product+0x70 list as\n         * circular (sentinel=prod+0x70). Sanitize list → run natural icall2\n         * (0039D428) + 2A4FE4. Opt-out old skip: PS3_TYPE15_SKIP_ATTACH=1. */\n        { static int _skip_mode = -1;\n          if (_skip_mode < 0) {\n            const char* e = getenv("PS3_TYPE15_SKIP_ATTACH");\n            _skip_mode = (e && e[0] && e[0] != \'0\') ? 1 : 0;\n          }\n          int skip_icall2 = 0;\n          if (_skip_mode) {\n            skip_icall2 = ty15_reused\n                || ((uint32_t)ctx->gpr[29] >= 0x47D00800u\n                    && (uint32_t)ctx->gpr[29] < 0x47D00C00u);\n          }\n          if (!skip_icall2) {\n            ctx->gpr[4] = ctx->gpr[29] | ctx->gpr[29];\n            ctx->gpr[0] = vm_read16(ctx->gpr[29] + 0x2);\n            ctx->gpr[0] = (uint32_t)ppc_rlwinm((uint32_t)ctx->gpr[0], 2, 14, 29);\n            { uint32_t _tab=(uint32_t)ctx->gpr[27];\n              uint32_t _idx=(uint32_t)ctx->gpr[0];\n              uint32_t _ent = (_tab >= 0x10000u && _tab < 0x4F000000u) ? vm_read32(_tab + _idx) : 0u;\n              uint32_t _vt = (_ent >= 0x10000u && _ent < 0x4F000000u) ? vm_read32(_ent) : 0u;\n              uint32_t _opd = (_vt >= 0x10000u && _vt < 0x4F000000u) ? vm_read32(_vt + 0x18u) : 0u;\n              uint32_t _code = (_opd >= 0x10000u && _opd < 0x4F000000u) ? vm_read32(_opd) : 0u;\n              uint32_t _toc = (_opd >= 0x10000u && _opd < 0x4F000000u) ? vm_read32(_opd + 4u) : 0u;\n              { static int _n=0; if(_n++<12)\n                  fprintf(stderr,"[POSTINTRO] CB56C icall2 tab=0x%08X idx=0x%X ent=0x%08X code=0x%08X\\n",\n                    _tab, _idx, _ent, _code); }\n              ctx->gpr[11] = _ent;\n              ctx->gpr[3] = _ent;\n              ctx->gpr[9] = _vt;\n              ctx->gpr[10] = _opd;\n              ctx->gpr[0] = _code;\n              vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n              if (!_ent || !_vt || !_opd || _code < 0x10000u || _code >= 0x01000000u) {\n                { static int _n=0; if(_n++<16)\n                    fprintf(stderr,"[POSTINTRO] CB56C icall2 SKIP bad\\n"); }\n                ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);\n              } else {\n                ctx->ctr = _code;\n                ctx->gpr[2] = _toc;\n                ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n                ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);\n              }\n            }\n            ctx->gpr[3] = ctx->gpr[29] | ctx->gpr[29];\n            vm_write32(ctx->gpr[31] + 0x8, ctx->gpr[28]);\n            { static int _n=0; if(_n++<8)\n                fprintf(stderr,"[POSTINTRO] CB56C obj+8=product=0x%08X attach=full reused=%d\\n",\n                  (unsigned)(uint32_t)ctx->gpr[29], ty15_reused); }\n            /* 2A4FE4 now safe (list sanitized + CLOSE-TAIL). */\n            ctx->lr = 0x000CB6AC; func_002A4FE4(ctx); DRAIN_TRAMPOLINE(ctx);\n            { static int _n=0; if(_n++<8)\n                fprintf(stderr,"[POSTINTRO] CB56C after 2A4FE4\\n"); }\n          } else {\n            ctx->gpr[3] = ctx->gpr[29] | ctx->gpr[29];\n            vm_write32(ctx->gpr[31] + 0x8, ctx->gpr[28]);\n            { static int _n=0; if(_n++<8)\n                fprintf(stderr,"[TYPE15] CB56C SKIP_ATTACH product=0x%08X (legacy)\\n",\n                  (unsigned)(uint32_t)ctx->gpr[29]); }\n          }\n        }\n'   # bloco de producao (1 linha adaptada: ctx->lr)
CB_MARKER = "/* TYPE15 attach (2026-07-23 fix):"
CB_DEP_VAR = "int ty15_reused = 0;"

# --- func_000CC9D0: logger [CC9D0] iter= ------------------------------------
CC_CLEAN = '        { uint32_t tmp = vm_read32(ctx->gpr[9] + 0x0); float ftmp; memcpy(&ftmp, &tmp, 4); ctx->fpr[31] = ftmp; }\n        if ((!((ctx->cr >> 0) & 2))) { g_trampoline_fn = (void(*)(void*))func_000CCBF0; return; }\n'
CC_PATCHED = '        { uint32_t tmp = vm_read32(ctx->gpr[9] + 0x0); float ftmp; memcpy(&ftmp, &tmp, 4); ctx->fpr[31] = ftmp; }\n        { static int on=-1; if(on<0){extern char* getenv(const char*);\n            const char* e=getenv("PS3_TRACE_CC9D0");\n            on=(e&&*e&&*e!=\'0\')?1:0;}\n          if(on){ static long n=0; static uint32_t last_this=0xFFFFFFFFu;\n            uint32_t th=(uint32_t)ctx->gpr[31];\n            uint8_t f54=(uint8_t)ctx->gpr[0]; uint32_t f4=vm_read32(th+0x4);\n            if(n<40 || th!=last_this || (n%100000)==0){\n              fprintf(stderr,"[CC9D0] iter=%ld this=0x%08X f54=%d f4=%u (prev_this=0x%08X)\\n",\n                n, th, (int)(int8_t)f54, f4, last_this);\n              fflush(stderr);\n            }\n            last_this=th; n++;\n          } }\n        if ((!((ctx->cr >> 0) & 2))) { g_trampoline_fn = (void(*)(void*))func_000CCBF0; return; }\n'
CC_MARKER = "[CC9D0] iter="


def _fn_body_span(t: str, sig: str):
    i = t.find(sig)
    if i < 0:
        return None
    j = t.find("\n}\n", i)
    if j < 0:
        return None
    return i, j + 3


def patch_one(path: Path) -> int:
    """0 aplicado | 1 ja' aplicado | -1 chunk sem as funcoes | -2 recusa."""
    t = path.read_text(encoding="utf-8", errors="replace")
    has_cc, has_cb = SIG_CC9D0 in t, SIG_CB56C in t
    if not has_cc and not has_cb:
        return -1

    changed = False

    # (1) declaracoes de file-scope (ancora do contador do disc)
    if GL_MARKER not in t:
        if HDR_ANCHOR not in t:
            print(f"  {path.name}: RECUSA -- sem ancora de cabecalho {HDR_ANCHOR!r}")
            return -2
        t = t.replace(HDR_ANCHOR, HDR_ANCHOR + GL_DECLS, 1)
        changed = True

    # (2) bloco de attach TYPE15 em func_000CB56C
    if has_cb:
        span = _fn_body_span(t, SIG_CB56C)
        if span is None:
            print(f"  {path.name}: RECUSA -- func_000CB56C sem fim de corpo")
            return -2
        i, j = span
        b = t[i:j]
        if CB_MARKER not in b:
            if CB_DEP_VAR not in b:
                print(f"  {path.name}: RECUSA -- func_000CB56C nao declara "
                      f"'{CB_DEP_VAR}' (instalado por "
                      "patch_type15_cb56c_prefer_product_install.py, que corre antes)")
                return -2
            if b.count(CB_CLEAN) != 1:
                print(f"  {path.name}: RECUSA -- func_000CB56C nao tem o icall2 "
                      f"gerado esperado ({b.count(CB_CLEAN)} ocorrencias, esperada 1)")
                return -2
            t = t[:i] + b.replace(CB_CLEAN, CB_ATTACH, 1) + t[j:]
            changed = True

    # (3) logger [CC9D0] iter= em func_000CC9D0
    if has_cc:
        span = _fn_body_span(t, SIG_CC9D0)
        if span is None:
            print(f"  {path.name}: RECUSA -- func_000CC9D0 sem fim de corpo")
            return -2
        i, j = span
        b = t[i:j]
        if CC_MARKER not in b:
            if b.count(CC_CLEAN) != 1:
                print(f"  {path.name}: RECUSA -- func_000CC9D0 nao tem o contexto "
                      "gerado esperado (ramo CCBF0)")
                return -2
            t = t[:i] + b.replace(CC_CLEAN, CC_PATCHED, 1) + t[j:]
            changed = True

    if changed:
        try:
            path.write_text(t, encoding="utf-8", newline="\n")
        except TypeError:
            path.write_text(t, encoding="utf-8")
        print(f"  {path.name}: APPLIED")
        return 0
    print(f"  {path.name}: ALREADY")
    return 1


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], "ppu_recomp_000.cpp")
             if p.is_file()]
    if not paths:
        print("ERRO: nenhum ficheiro de lift legivel", file=sys.stderr)
        return 3
    applied = already = refused = 0
    for p in paths:
        r = patch_one(p)
        if r == 0:
            applied += 1
        elif r == 1:
            already += 1
        elif r == -2:
            refused += 1
    if refused:
        print("ERRO: pre-condicao/forma inesperada -- nada foi adivinhado.\n"
              "  Revalida os blocos contra um lift de producao conhecido-bom e\n"
              "  regenera este script (scratchpad/g3_gen_base_blocks.py).",
              file=sys.stderr)
        return 2
    if applied or already:
        print(f"[type15-cc9d0-base-blocks] ok ({applied} aplicado, "
              f"{already} ja' aplicado)")
        return 0
    print("ERRO: func_000CB56C/func_000CC9D0 nao existem em nenhum dos ficheiros dados.",
          file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
