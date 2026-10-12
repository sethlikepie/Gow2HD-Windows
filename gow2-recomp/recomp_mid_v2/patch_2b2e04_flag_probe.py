#!/usr/bin/env python3
"""PS3_TRACE_2B2E04 -- sonda read-only do laco pos-thr_auto_load em func_002B2E04.

WHY
---
A main thread pos-`thr_auto_load() end` fica presa para sempre dentro de
`func_002B2E04` (`ppu_recomp_001.cpp`, ~linha 36402), medido por 5 amostras de
lldb em 2 binarios distintos (nota
`../notes/2026-07-31-onde-a-main-para-pos-auto-load-nao-e-deadlock.md`). O
lift, lido directamente (nao parafraseado), e':

    ctx->gpr[31] = vm_read32(ctx->gpr[2] + -0x1488);   // r31 = ponteiro (TOC-fixo)
    ctx->gpr[0]  = vm_read32(ctx->gpr[31] + 0x0);       // r0  = *r31 (a "flag")
    cr = cmp((int32_t)r0, 0);
    if (!EQ) goto sair;                                  // r0 != 0 -> sai JA
loop:
    func_002B2DD0(ctx);
    r0 = vm_read32(r31 + 0);
    if (EQ) goto loop;                                   // r0 == 0 -> repete
sair:
    return;

**A condicao real e' de IGUALDADE (r0==0 continua, r0!=0 sai), nao de sinal**
(um pedido anterior descrevia isto como teste de bit 31/negativo -- essa
leitura nao bate com o C acima, que so' testa o bit EQ do cr, nunca LT). Isto
importa para a hipotese de boundary-tag: um valor tipo `0x8XXXXXXX` e' NAO-ZERO,
logo se aparecesse aqui o laco DEVIA sair imediatamente -- o oposto do que se
observa (o laco nunca sai). A observacao "nunca sai" e' portanto mais
consistente com "r0 fica establemente 0" do que com "r0 e' lixo grande com o
bit alto ligado" -- so' a MEDICAO decide, esta sonda existe para a fazer.

O QUE A SONDA MEDE (read-only, nunca escreve r31/flag/ctx)
------------------------------------------------------------
Site P1 -- logo apos `ctx->gpr[31] = vm_read32(...)`: regista o PONTEIRO r31
  (o endereco da flag em si, dinamico -- resolve-se aqui para depois se poder
  apontar `PS3_WATCH_W32=<esse endereco>` a um escritor).
Site P2 -- a PRIMEIRA leitura de `*r31` (antes de entrar no laco): valor +
  branch (sair/entra).
Site P3 -- CADA leitura de `*r31` dentro do laco (apos `func_002B2DD0`):
  valor + branch (repete/sai), capado (default 400 amostras) para nao
  encher o log num laco que pode correr milhoes de vezes.

Gate: `PS3_TRACE_2B2E04` (qualquer valor nao-vazio e != "0"). OFF por
default -- zero linhas `[2B2E04]` no baseline. Cap configuravel por
`PS3_TRACE_2B2E04_CAP` (default 400, mesmo padrao dos outros *-PROBE deste
directorio).

Uso:  patch_2b2e04_flag_probe.py [DIR_DE_LIFT]     (default: ../recomp_macos_v2)
Reaplicado por ../apply_all_patches.sh apos cada re-lift. Idempotente por
TAG (`2B2E04-PROBE#P1/P2/P3`), a' semelhanca de patch_smpd_probe.py.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

MARKER = "2B2E04-PROBE"
FUNC = "func_002B2E04"

ROOT_DEFAULT = str(Path(__file__).resolve().parent.parent / "recomp_macos_v2")

ON = ("{ static int _on=-1; if(_on<0){extern char* getenv(const char*);\n"
      "            const char* _e=getenv(\"PS3_TRACE_2B2E04\"); _on=(_e&&*_e&&*_e!='0')?1:0;}\n")

CAP_DECL = ("            static int _cap=-1; if(_cap<0){extern char* getenv(const char*);\n"
            "                const char* _c=getenv(\"PS3_TRACE_2B2E04_CAP\");\n"
            "                _cap=(_c&&*_c)?atoi(_c):400;}\n")

# ---- Site P1: r31 resolvido (o proprio ponteiro da flag) --------------------
P1_ANCHOR = "        ctx->gpr[31] = vm_read32(ctx->gpr[2] + -0x1488);\n"
P1_BLOCK = (
    "        /* " + MARKER + "#P1: ponteiro da flag (TOC-0x1488), dinamico por corrida */\n"
    "        " + ON +
    CAP_DECL +
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            fprintf(stderr,\"[2B2E04] P1 flag_ptr=0x%08X\\n\",(uint32_t)ctx->gpr[31]);\n"
    "            fflush(stderr); } } }\n"
)

# ---- Site P2: primeira leitura de *r31 (antes do laco) ----------------------
P2_ANCHOR = (
    "        ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0x0);\n"
    "        { int64_t a = (int32_t)ctx->gpr[0]; int64_t b = (int64_t)0; "
    "uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; "
    "ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
    "        if ((!((ctx->cr >> 0) & 2))) goto loc_002B2E3C;\n"
)
P2_BLOCK = (
    "        /* " + MARKER + "#P2: 1a leitura de *flag_ptr, antes do laco */\n"
    "        " + ON +
    CAP_DECL +
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            fprintf(stderr,\"[2B2E04] P2 flag_ptr=0x%08X val=0x%08X branch=%s\\n\",\n"
    "              (uint32_t)ctx->gpr[31],(uint32_t)ctx->gpr[0],\n"
    "              (((ctx->cr>>0)&2)==0)?\"sair(nonzero)\":\"loop(zero)\");\n"
    "            fflush(stderr); } } }\n"
)

# ---- Site P3: leitura de *r31 dentro do laco ---------------------------------
P3_ANCHOR = (
    "        ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0x0);\n"
    "        { int64_t a = (int32_t)ctx->gpr[0]; int64_t b = (int64_t)0; "
    "uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; "
    "ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
    "        if (((ctx->cr >> 0) & 2)) goto loc_002B2E2C;\n"
)
P3_BLOCK = (
    "        /* " + MARKER + "#P3: leitura de *flag_ptr dentro do laco (pos func_002B2DD0) */\n"
    "        " + ON +
    CAP_DECL +
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            fprintf(stderr,\"[2B2E04] P3 flag_ptr=0x%08X val=0x%08X branch=%s n=%d\\n\",\n"
    "              (uint32_t)ctx->gpr[31],(uint32_t)ctx->gpr[0],\n"
    "              (((ctx->cr>>0)&2)!=0)?\"loop(zero)\":\"sair(nonzero)\",_n);\n"
    "            fflush(stderr); } } }\n"
)


def _func_span(t, func):
    sig = "void %s(ppu_context* ctx) {\n" % func
    i = t.find(sig)
    if i < 0:
        return None
    j = t.find("\nvoid func_", i + len(sig))
    return i, (j if j > i else len(t))


def _edit_after(region, anchor, block, tag):
    uniq = "/* " + MARKER + "#" + tag + ":"
    if uniq in region:
        return region, "%s: ALREADY" % tag
    c = region.count(anchor)
    if c != 1:
        raise SystemExit(
            "patch_2b2e04_flag_probe: ancora de %s aparece %dx em %s (esperado 1) -- "
            "shape do lift mudou; reveja a needle antes de forcar" % (tag, c, FUNC))
    region = region.replace(anchor, anchor + block, 1)
    return region, "%s: APPLIED" % tag


def patch_file(p: Path):
    t = p.read_text(encoding="utf-8", errors="replace")
    sp = _func_span(t, FUNC)
    if sp is None:
        return "SKIP", False
    i, end = sp
    region = t[i:end]
    orig_region = region
    notes = []

    region, s = _edit_after(region, P1_ANCHOR, P1_BLOCK, "P1")
    notes.append(s)
    region, s = _edit_after(region, P2_ANCHOR, P2_BLOCK, "P2")
    notes.append(s)
    region, s = _edit_after(region, P3_ANCHOR, P3_BLOCK, "P3")
    notes.append(s)

    if region == orig_region:
        return "ALREADY | " + " ; ".join(notes), False

    t = t[:i] + region + t[end:]
    p.write_text(t, encoding="utf-8", newline="\n")
    return "APPLIED | " + " ; ".join(notes), True


def main() -> int:
    files = [p for p in resolve_lift_paths(sys.argv[1:], ROOT_DEFAULT) if p.is_file()]
    if not files:
        print("nenhum ppu_recomp_*.cpp em %s" % (sys.argv[1:] or ROOT_DEFAULT))
        return 1
    found = False
    any_hit = False
    for p in files:
        r, changed = patch_file(p)
        if r != "SKIP":
            found = True
            any_hit = True
            print("%s: %s" % (p.name, r))
    if not found:
        raise SystemExit(
            "patch_2b2e04_flag_probe: %s nunca encontrada em nenhum ppu_recomp_*.cpp "
            "(shape do lift mudou?) -- reveja antes de forcar" % FUNC)
    if not any_hit:
        print("SKIP: nada para aplicar")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
