#!/usr/bin/env python3
"""Log type-1 stream member names (path that should hit SHGX_*).

CORRECCAO 2026-07-25 (re-lift): a agulha original era o PROLOGO INTEIRO de
func_002B0E78 copiado a' letra (~18 linhas) e o ficheiro era aberto por nome
fixo ("ppu_recomp_001.cpp"). Tres mudancas do lifter partiam isso:
  * shape-callee-save: o prologo passou a comecar com um bloco
    "uint64_t _cs_24 = ctx->gpr[24];" ... (save de callee-saved em locais host);
  * addi passou de "(int64_t)(int32_t)(ctx->gpr[3] + 8)" para
    "ctx->gpr[3] + (int64_t)(8)";
  * shape-LR: a chamada passou a ter prefixo "ctx->lr = 0x002B0EBC; ".
  * chunk-fixo: o lift tem agora 7 chunks (tinha 31); a funcao pode migrar.
Em vez de re-copiar o prologo novo (que voltaria a partir no proximo re-lift),
ancoramos SO' na chamada func_002B2BA0 DENTRO da regiao da funcao-alvo
(assinatura ate' ao proximo "void func_"), exigindo que ela seja unica la'
dentro, e inserimos a sonda IMEDIATAMENTE ANTES da linha da chamada (com ou
sem o prefixo "ctx->lr = ..."). O ponto de insercao e' semanticamente o mesmo
de antes: depois de r3 (ponteiro do nome) e r30 (buffer) estarem carregados,
antes da chamada. Os chunks sao resolvidos por resolve_lift_paths (aceita
DIRECTORIO).
"""
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

MARKER = "WADLD-T1"
FUNC = "func_002B0E78"
SIG = "void " + FUNC + "(ppu_context* ctx) {\n"
# chamada-alvo, tolerante ao prefixo "ctx->lr = 0x........; " (shape-LR)
CALL_RE = re.compile(
    r"^([ \t]*)((?:ctx->lr = 0x[0-9A-Fa-f]+; )?func_002B2BA0\(ctx\); DRAIN_TRAMPOLINE\(ctx\);)",
    re.M)

PROBE = (
    "        /* {m}: nome do membro type-1 antes de func_002B2BA0 */\n"
    "        {{ static int on=-1; if(on<0){{extern char* getenv(const char*); on=getenv(\"PS3_TRACE_TYMAP\")?1:0;}}\n"
    "          if(on){{ static int n=0; if(n++<80){{\n"
    "            uint32_t np=(uint32_t)ctx->gpr[3], bp=(uint32_t)ctx->gpr[30];\n"
    "            char nm[24]; int i; for(i=0;i<20;i++){{ uint8_t c=vm_read8(np+i); if(!c){{nm[i]=0; break;}} nm[i]=(c>=32&&c<127)?(char)c:'.'; }}\n"
    "            nm[20]=0;\n"
    "            fprintf(stderr,\"[{m}] #%d name='%s' namep=0x%08X buf=0x%08X\\n\", n, nm, np, bp); fflush(stderr);}} }} }}\n"
).format(m=MARKER)


def patch_file(p: Path) -> str:
    t = p.read_text(encoding="utf-8", errors="replace")
    i = t.find(SIG)
    if i < 0:
        return "SKIP(func)"
    j = t.find("\nvoid func_", i + len(SIG))
    end = j if j > i else len(t)
    region = t[i:end]
    if MARKER in region:
        return "ALREADY"
    hits = list(CALL_RE.finditer(region))
    if len(hits) != 1:
        raise SystemExit(
            "patch_type1_name: chamada func_002B2BA0 aparece %dx em %s "
            "(esperado 1) -- shape do lift mudou; reveja a needle" % (len(hits), FUNC))
    m = hits[0]
    region = region[:m.start()] + PROBE + region[m.start():]
    p.write_text(t[:i] + region + t[end:], encoding="utf-8", newline="\n")
    return "APPLIED"


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], str(Path(__file__).resolve().parent))
    hit = False
    for p in paths:
        if not p.is_file():
            print("skip %s" % p)
            continue
        r = patch_file(p)
        if r != "SKIP(func)":
            hit = True
            print("%s: %s" % (p.name, r))
    if not hit:
        raise SystemExit("patch_type1_name: %s nao encontrada em nenhum chunk" % FUNC)
    print("OK type1 name probe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
