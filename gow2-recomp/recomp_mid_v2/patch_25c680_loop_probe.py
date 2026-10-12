#!/usr/bin/env python3
"""PS3_TRACE_25C680 -- sonda read-only dos 2 lacos limitados-por-contagem em
func_0025C680, candidato ao hang real pos-thr_auto_load.

WHY
---
Medido nesta sessao (patch_2b2e04_flag_probe.py, 2 corridas que completaram
`thr_auto_load() end`, ate' 90s de janela pos-autoload cada): `func_002B2E04`
NUNCA foi entrada (P1 zero hits), e `PS3_TRACE_SCHEDARM=1` mostra que o seu
chamador directo, `func_0025C838` (id=25C838), e' entrado EXACTAMENTE UMA VEZ
(`tot=1`) e nunca mais -- ou seja, a unica invocacao de `func_0025C838` nunca
RETORNA. Como `func_0025C838` e' um despacho LINEAR de 9 sub-chamadas sem
laco proprio (`func_002B37D4, func_00242700, func_002B4F04, func_0025C680,
func_002B76EC, func_002B2EEC, func_002B2E74, func_002B2E04, func_002B2E50`),
e as 3 primeiras (`002B37D4`, `00242700`, `002B4F04`) tem corpos triviais
sem laco/sub-chamadas bloqueantes obvias, `func_0025C680` e' a PRIMEIRA da
sequencia com laco interno proprio (2 lacos `for(r30=0; r30<count; r30++)`,
ambos limitados por um valor lido de `*(struct+0x8)`/`*(struct+0x14)`) --
e ambos chamam `func_00263040`, o MESMO sitio do 6o FREELIST-TAG-GUARD
(`../../ps3recomp` CLAUDE.md, achado do allocator fiel).

Hipotese a discriminar: se o valor de contagem (`*(struct+0x8)` ou
`*(struct+0x14)`) estiver corrompido para algo com o bit alto ligado
(a MESMA assinatura `0x8XXXXXXX` do boundary-tag), o laco
`for(r30=0; r30<count; r30++)` -- BOUNDED no codigo-fonte, mas com um limite
efectivamente gigante -- torna-se indistinguivel de um hang eterno. Se, pelo
contrario, a contagem for pequena (ex.: 12, como o valor literal 0xC visto
no lift antes da 1a chamada a func_00263040), o hang NAO esta' aqui e a
procura continua para func_00263040/func_00263680/func_00262808 (chamadas
DENTRO do corpo do laco, que podem elas proprias bloquear por sys call ou
espera).

O QUE A SONDA MEDE (read-only, nunca escreve struct/ctx alem do que o lift
ja escrevia)
------------------------------------------------------------------------
Site Q1 -- contagem do 1o laco (`loc_0025C724`), lida logo apos o calculo
  em `ppu_recomp_000.cpp:555690-555692` (ANTES do primeiro branch que decide
  saltar o laco).
Site Q2 -- cada iteracao do 1o laco, apos o update de r30 (linha ~555715),
  com o proprio r30 e a contagem-alvo, capado (mas o cap por-omissao e'
  ALTO -- 2000 -- porque queremos DETECTAR se ultrapassa uma contagem
  pequena esperada, nao so' confirmar que corre).
Site Q3 -- contagem do 2o laco (`loc_0025C7BC`), mesma logica que Q1.
Site Q4 -- cada iteracao do 2o laco, mesma logica que Q2.

Gate: `PS3_TRACE_25C680` (nao-vazio e != "0"). OFF por default. Cap
configuravel por `PS3_TRACE_25C680_CAP` (default 2000).

Uso:  patch_25c680_loop_probe.py [DIR_DE_LIFT]     (default: ../recomp_macos_v2)
Reaplicado por ../apply_all_patches.sh apos cada re-lift. Idempotente por
TAG (`25C680-PROBE#Q1..Q4`).
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

MARKER = "25C680-PROBE"
FUNC = "func_0025C680"

ROOT_DEFAULT = str(Path(__file__).resolve().parent.parent / "recomp_macos_v2")

ON = ("{ static int _on=-1; if(_on<0){extern char* getenv(const char*);\n"
      "            const char* _e=getenv(\"PS3_TRACE_25C680\"); _on=(_e&&*_e&&*_e!='0')?1:0;}\n")

CAP_DECL = ("            static int _cap=-1; if(_cap<0){extern char* getenv(const char*);\n"
            "                const char* _c=getenv(\"PS3_TRACE_25C680_CAP\");\n"
            "                _cap=(_c&&*_c)?atoi(_c):2000;}\n")

# ---- Site Q1: contagem do 1o laco --------------------------------------------
Q1_ANCHOR = (
    "        ctx->gpr[0] = vm_read32(ctx->gpr[26] + 0x8);\n"
    "        ctx->gpr[31] = vm_read32(ctx->gpr[26] + 0xC);\n"
    "        { int64_t a = (int32_t)ctx->gpr[0]; int64_t b = (int64_t)0; "
    "uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; "
    "ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
    "        vm_write32(ctx->gpr[26] + 0x10, ctx->gpr[3]);\n"
    "        if (((ctx->cr >> 0) & 2)) goto loc_0025C778;\n"
)
Q1_BLOCK = (
    "        /* " + MARKER + "#Q1: contagem do 1o laco (struct+0x8), antes do branch */\n"
    "        " + ON +
    "          if(_on){ fprintf(stderr,\"[25C680] Q1 struct=0x%08X count=0x%08X (%d)\\n\",\n"
    "            (uint32_t)ctx->gpr[26],(uint32_t)ctx->gpr[0],(int32_t)ctx->gpr[0]);\n"
    "            fflush(stderr); } }\n"
)

# ---- Site Q2: iteracao do 1o laco --------------------------------------------
Q2_ANCHOR = (
    "        ctx->gpr[0] = vm_read32(ctx->gpr[26] + 0x8);\n"
    "        ctx->gpr[30] = ctx->gpr[30] + (int64_t)(1);\n"
    "        { uint64_t a = (uint32_t)ctx->gpr[0]; uint64_t b = (uint32_t)ctx->gpr[27]; "
    "uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; "
    "ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
    "        if (((ctx->cr >> 0) & 4)) goto loc_0025C724;\n"
)
Q2_BLOCK = (
    "        /* " + MARKER + "#Q2: iteracao do 1o laco (loc_0025C724) */\n"
    "        " + ON +
    CAP_DECL +
    "          if(_on){ static long long _n=0; _n++;\n"
    "            if(_n<=_cap || (_n%1000000ll)==0){\n"
    "              fprintf(stderr,\"[25C680] Q2 iter=%lld r30=0x%08X count=0x%08X cont=%s\\n\",\n"
    "                _n,(uint32_t)ctx->gpr[30],(uint32_t)ctx->gpr[0],\n"
    "                (((ctx->cr>>0)&4)!=0)?\"yes\":\"no\");\n"
    "              fflush(stderr); } } }\n"
)

# ---- Site Q3: contagem do 2o laco --------------------------------------------
Q3_ANCHOR = (
    "        ctx->gpr[0] = vm_read32(ctx->gpr[26] + 0x14);\n"
    "        ctx->gpr[31] = vm_read32(ctx->gpr[26] + 0x18);\n"
    "        { int64_t a = (int32_t)ctx->gpr[0]; int64_t b = (int64_t)0; "
    "uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; "
    "ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
    "        vm_write32(ctx->gpr[26] + 0x1C, ctx->gpr[3]);\n"
    "        if (((ctx->cr >> 0) & 2)) goto loc_0025C810;\n"
)
Q3_BLOCK = (
    "        /* " + MARKER + "#Q3: contagem do 2o laco (struct+0x14), antes do branch */\n"
    "        " + ON +
    "          if(_on){ fprintf(stderr,\"[25C680] Q3 struct=0x%08X count=0x%08X (%d)\\n\",\n"
    "            (uint32_t)ctx->gpr[26],(uint32_t)ctx->gpr[0],(int32_t)ctx->gpr[0]);\n"
    "            fflush(stderr); } }\n"
)

# ---- Site Q4: iteracao do 2o laco --------------------------------------------
Q4_ANCHOR = (
    "        ctx->gpr[0] = vm_read32(ctx->gpr[26] + 0x14);\n"
    "        ctx->gpr[30] = ctx->gpr[30] + (int64_t)(1);\n"
    "        { uint64_t a = (uint32_t)ctx->gpr[0]; uint64_t b = (uint32_t)ctx->gpr[27]; "
    "uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; "
    "ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
    "        if (((ctx->cr >> 0) & 4)) goto loc_0025C7BC;\n"
)
Q4_BLOCK = (
    "        /* " + MARKER + "#Q4: iteracao do 2o laco (loc_0025C7BC) */\n"
    "        " + ON +
    CAP_DECL +
    "          if(_on){ static long long _n=0; _n++;\n"
    "            if(_n<=_cap || (_n%1000000ll)==0){\n"
    "              fprintf(stderr,\"[25C680] Q4 iter=%lld r30=0x%08X count=0x%08X cont=%s\\n\",\n"
    "                _n,(uint32_t)ctx->gpr[30],(uint32_t)ctx->gpr[0],\n"
    "                (((ctx->cr>>0)&4)!=0)?\"yes\":\"no\");\n"
    "              fflush(stderr); } } }\n"
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
            "patch_25c680_loop_probe: ancora de %s aparece %dx em %s (esperado 1) -- "
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

    for tag, anchor, block in (
        ("Q1", Q1_ANCHOR, Q1_BLOCK),
        ("Q2", Q2_ANCHOR, Q2_BLOCK),
        ("Q3", Q3_ANCHOR, Q3_BLOCK),
        ("Q4", Q4_ANCHOR, Q4_BLOCK),
    ):
        region, s = _edit_after(region, anchor, block, tag)
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
            "patch_25c680_loop_probe: %s nunca encontrada em nenhum ppu_recomp_*.cpp "
            "(shape do lift mudou?) -- reveja antes de forcar" % FUNC)
    if not any_hit:
        print("SKIP: nada para aplicar")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
