#!/usr/bin/env python3
"""Log type-1 name lookup result (r3 after 2B2BA0) for SHGX path.

REPARACAO 2026-07-25 (re-lift com lifter novo)
----------------------------------------------
A agulha literal deixou de casar por DUAS mudancas de forma do lifter, nenhuma
delas semantica:

  1. shape-LR: as chamadas passaram a ter prefixo `ctx->lr = 0x002B0EBC; ` antes
     de `func_002B2BA0(ctx); DRAIN_TRAMPOLINE(ctx);`.
  2. chunk-fixo: o script abria `ppu_recomp_001.cpp` por nome. O lifter passou de
     31 para 7 chunks e a funcao pode migrar de ficheiro a qualquer re-lift.

Correccao: a agulha passa a ser um regex que aceita o prefixo `ctx->lr = 0x...;`
como OPCIONAL (casa com o lift antigo E com o novo) e o alvo passa a ser
resolvido por `resolve_lift_paths`, que expande um DIRECTORIO para todos os
chunks. O texto que envolve o ponto de insercao continua literal (via re.escape),
por isso a probe continua ancorada exactamente no mesmo sitio do fluxo.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARKER = "WADLD-T1R"

# Prefixo de LR que o lifter novo emite antes de cada chamada; opcional para
# manter compatibilidade com lifts antigos que nao o tinham.
_LR = r"(?:ctx->lr = 0x[0-9A-Fa-f]+; )?"

# Cabeca: chamada a 2B2BA0 + as duas leituras que precedem o ponto de insercao.
HEAD = (
    r"        " + _LR + r"func_002B2BA0\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    r"        /\* nop \*/;\n"
    r"        ctx->gpr\[25\] = vm_read32\(ctx->gpr\[28\] \+ 0x4\);\n"
    r"        ctx->gpr\[31\] = ctx->gpr\[3\] \| ctx->gpr\[3\];\n"
)

# Cauda: o teste de tamanho + o desvio para func_002B0FB4. Fica literal (escapado)
# porque e' o que torna o sitio unico dentro do chunk.
TAIL = re.escape(
    "        { int64_t a = (int32_t)ctx->gpr[25]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
    "        ctx->gpr[4] = ppc_rldicl(ctx->gpr[28], 0, 32);\n"
    "        ctx->gpr[29] = ctx->gpr[3] | ctx->gpr[3];\n"
    "        if ((!((ctx->cr >> 0) & 2))) { g_trampoline_fn = (void(*)(void*))func_002B0FB4; return; }\n"
    "        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
    "        if (((ctx->cr >> 0) & 2)) goto loc_002B0F2C;"
)

PATTERN = re.compile("(" + HEAD + ")(" + TAIL + ")")

PROBE = """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<80){
            uint32_t np=(uint32_t)ctx->gpr[26], res=(uint32_t)ctx->gpr[3], sz=(uint32_t)ctx->gpr[25], buf=(uint32_t)ctx->gpr[30];
            char nm[24]; int i; for(i=0;i<20;i++){ uint8_t c=vm_read8(np+i); if(!c){nm[i]=0; break;} nm[i]=(c>=32&&c<127)?(char)c:'.'; }
            nm[20]=0;
            fprintf(stderr,"[WADLD-T1R] #%d name='%s' result=0x%08X size=%u buf=0x%08X hit=%d\\n",
              n, nm, res, sz, buf, res!=0); fflush(stderr);} } }
"""


def patch_file(p: Path) -> str:
    s = p.read_text(encoding="utf-8", errors="replace")
    if MARKER in s:
        return "ALREADY"
    hits = list(PATTERN.finditer(s))
    if not hits:
        return "SKIP"
    if len(hits) > 1:
        raise SystemExit(
            f"{p.name}: agulha T1R ambigua ({len(hits)} sitios) -- reveja antes de forcar"
        )
    m = hits[0]
    s = s[: m.start()] + m.group(1) + PROBE + m.group(2) + s[m.end():]
    p.write_text(s, encoding="utf-8", newline="\n")
    return "APPLIED"


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], str(Path(__file__).resolve().parent))
    applied = already = 0
    for p in paths:
        if not p.exists():
            print(f"skip {p}")
            continue
        r = patch_file(p)
        if r == "SKIP":
            continue
        print(f"{p.name}: {r}")
        applied += r == "APPLIED"
        already += r == "ALREADY"
    if not applied and not already:
        print("needle missing", file=sys.stderr)
        return 1
    print("OK T1R probe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
