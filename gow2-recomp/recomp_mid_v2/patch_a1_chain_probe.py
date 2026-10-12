#!/usr/bin/env python3
"""A1 chain probes (2026-07-22): entry counts + gate values above 0032E200.

WHY
---
Static RE (notes/2026-07-21-registry-caller-chain-A1.md) proved that the only
direct path to func_0032E200 is:

  00468C3C -> 00330D54 -> 0032DF98  [if *r5 != 0]  -> 0032E200 -> walk

and that 00468C3C always stores literal 0 at sp+0x7C, which becomes *r5 in
0032DF98. That closes the gate *structurally*. This patch only *measures*
in-boot whether:
  - 00468C3C is visited after WAD
  - it reaches 00330D54 / 0032DF98 (or early-exits to 00468DF8)
  - *r5 is always 0 (confirming the static claim in-boot)
  - the E200 branch is never taken

Gated by PS3_TRACE_A1CHAIN (fallback PS3_TRACE_TYMAP). OFF by default.
Read-only (stderr logs only). Idempotent (marker A1-CHAIN). Never forges
gates / CRC / SHADERSRC.

CORRECCAO 2026-07-25 (re-lift)
------------------------------
Tres formas do lift mudaram e partiam as agulhas literais deste script
(medido contra um lift limpo do ppu_lifter.py actual):

  * shape-callee-save: os prologos passaram a comecar por um bloco
    "uint64_t _cs_NN = ctx->gpr[NN];" ANTES do "vm_write64(ctx->gpr[1] + -0xD0,
    ...)". As duas agulhas de ENTRADA (A1-468C3C, A1-30D54) colavam a
    assinatura ao ajuste de stack e davam count=0. Passam a ancorar SO' na
    assinatura da funcao e a inserir logo a seguir a ela -- o sitio e'
    semanticamente equivalente (r3..r6 ainda sao os argumentos de entrada,
    intocados pelo bloco _cs_) e imune a futuras mudancas de prologo.
  * shape-LR: as chamadas ganharam o prefixo "ctx->lr = 0x00468D28; ". As
    agulhas A1-CALL30D54 / A1-CALL30D54B passam a usar um regex que aceita o
    prefixo opcional (casa lift antigo E novo).
  * chunk-fixo: o lift tinha 31 chunks e agora tem 7; C1/C2 eram nomes fixos
    ("ppu_recomp_001.cpp"/"002"). Passa a usar resolve_lift_paths (aceita
    DIRECTORIO) e a encontrar cada funcao no chunk onde ela calhar.

Alem disso todas as insercoes passam a ser SCOPED a' regiao da funcao-alvo
(assinatura ate' ao proximo "void func_"), em vez de contar ocorrencias no
ficheiro inteiro: o resultado nao depende de o lifter juntar ou separar
funcoes por chunk. As agulhas EX1/EX2/32DF98 nao mudaram de forma e ficam
com o texto original, so' que agora validadas dentro da regiao certa.
"""
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

MARKER = "A1-CHAIN"

PROBE_ON = (
    '{ static int on=-1; if(on<0){extern char* getenv(const char*);\n'
    '            const char* e=getenv("PS3_TRACE_A1CHAIN"); '
    'const char* e2=getenv("PS3_TRACE_TYMAP");\n'
    '            on=((e&&*e&&*e!=\'0\')||(e2&&*e2&&*e2!=\'0\'))?1:0;}\n'
)


def _region(t: str, func: str):
    """(i, end, region) da funcao; region vai da assinatura ao proximo 'void func_'."""
    sig = "void %s(ppu_context* ctx) {\n" % func
    i = t.find(sig)
    if i < 0:
        return -1, -1, "", 0
    j = t.find("\nvoid func_", i + len(sig))
    end = j if j > i else len(t)
    return i, end, t[i:end], len(sig)


def _insert_at_entry(t, func, label, block):
    """Insere logo a seguir a' assinatura (imune ao bloco callee-save do prologo)."""
    i, end, region, siglen = _region(t, func)
    if i < 0:
        return t, None
    if "[%s]" % label in region:
        return t, "%s: already present" % label
    pos = i + siglen
    return t[:pos] + block + t[pos:], "%s: probe added" % label


def _insert_before(t, func, label, anchor, block, expect=1):
    """Insere ANTES da ancora, dentro da regiao da funcao (str, nao regex)."""
    i, end, region, _ = _region(t, func)
    if i < 0:
        return t, None
    if "[%s]" % label in region:
        return t, "%s: already present" % label
    c = region.count(anchor)
    if c != expect:
        raise SystemExit("%s: needle count=%d em %s (esperado %d) -- shape do lift mudou"
                         % (label, c, func, expect))
    region = region.replace(anchor, block + anchor, expect)
    return t[:i] + region + t[end:], "%s: probe added" % label


def _insert_before_re(t, func, label, rx, block, expect=1):
    """Insere ANTES da 1a linha que casa `rx`, dentro da regiao da funcao."""
    i, end, region, _ = _region(t, func)
    if i < 0:
        return t, None
    if "[%s]" % label in region:
        return t, "%s: already present" % label
    hits = list(rx.finditer(region))
    if len(hits) != expect:
        raise SystemExit("%s: needle regex count=%d em %s (esperado %d) -- shape do lift mudou"
                         % (label, len(hits), func, expect))
    out = []
    prev = 0
    for m in hits:
        out.append(region[prev:m.start()])
        out.append(block)
        prev = m.start()
    out.append(region[prev:])
    return t[:i] + "".join(out) + t[end:], "%s: probe added" % label


# --- blocos de sonda ---------------------------------------------------------
B_468C3C = (
    "        /* %s: entry */\n"
    "        %s"
    "          if(on){ static int n=0; if(n++<64)\n"
    "            fprintf(stderr,\"[A1-468C3C] #%%d r3=0x%%08X r4=0x%%08X r5=0x%%08X r6=0x%%08X\\n\",\n"
    "              n,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[4],\n"
    "              (uint32_t)ctx->gpr[5],(uint32_t)ctx->gpr[6]); fflush(stderr);} }\n"
) % (MARKER, PROBE_ON)

B_30D54 = (
    "        /* %s: entry */\n"
    "        %s"
    "          if(on){ static int n=0; if(n++<64){\n"
    "            uint32_t r6=(uint32_t)ctx->gpr[6]; uint32_t v=r6?vm_read32(r6):0;\n"
    "            fprintf(stderr,\"[A1-30D54] #%%d r3=0x%%08X r5=0x%%08X r6=0x%%08X *r6=0x%%08X r8=0x%%08X\\n\",\n"
    "              n,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[5],r6,v,(uint32_t)ctx->gpr[8]);\n"
    "            fflush(stderr);} } }\n"
) % (MARKER, PROBE_ON)

B_EX1 = (
    "        /* %s */\n"
    "        %s"
    "          if(on && ((ctx->cr >> 0) & 2)){ static int n=0; if(n++<32)\n"
    "            fprintf(stderr,\"[A1-468-EX1] #%%d early DF8 (field0==0) r5=0x%%08X\\n\",\n"
    "              n,(uint32_t)ctx->gpr[31]); fflush(stderr);} }\n"
) % (MARKER, PROBE_ON)

B_EX2 = (
    "        /* %s */\n"
    "        %s"
    "          if(on && ((ctx->cr >> 0) & 2)){ static int n=0; if(n++<32)\n"
    "            fprintf(stderr,\"[A1-468-EX2] #%%d early DF8 (field8==0) r5=0x%%08X\\n\",\n"
    "              n,(uint32_t)ctx->gpr[31]); fflush(stderr);} }\n"
) % (MARKER, PROBE_ON)

B_CALL = (
    "        /* %s: pre-call 00330D54 */\n"
    "        %s"
    "          if(on){ static int n=0; if(n++<64)\n"
    "            fprintf(stderr,\"[A1-CALL30D54] #%%d sp7c=*r30=0x%%08X r6arg=0x%%08X r8=0x%%08X\\n\",\n"
    "              n,(uint32_t)ctx->gpr[30],(uint32_t)(ctx->gpr[1]+0x7C),\n"
    "              (uint32_t)ctx->gpr[26]); fflush(stderr);} }\n"
) % (MARKER, PROBE_ON)

B_CALLB = (
    "        /* %s: pre-call 00330D54 via 468CEC/D04 */\n"
    "        %s"
    "          if(on){ static int n=0; if(n++<64)\n"
    "            fprintf(stderr,\"[A1-CALL30D54B] #%%d sp7c_r30=0x%%08X\\n\",\n"
    "              n,(uint32_t)ctx->gpr[30]); fflush(stderr);} }\n"
) % (MARKER, PROBE_ON)

B_DF98 = (
    "        /* %s: gate *r5 / r8 */\n"
    "        %s"
    "          if(on){ static int n=0; if(n++<96){\n"
    "            uint32_t r5=(uint32_t)ctx->gpr[5]; uint32_t star=r5?vm_read32(r5):0;\n"
    "            const char* br = (!((ctx->cr >> 0) & 2)) ? \"E200\"\n"
    "              : (((int32_t)ctx->gpr[8]) != 0) ? \"E718\" : \"DEFAULT\";\n"
    "            fprintf(stderr,\"[A1-32DF98] #%%d r5=0x%%08X *r5=0x%%08X r8=0x%%08X br=%%s\\n\",\n"
    "              n,r5,star,(uint32_t)ctx->gpr[8],br); fflush(stderr);} } }\n"
) % (MARKER, PROBE_ON)

# --- ancoras -----------------------------------------------------------------
A_EX1 = (
    "        if (((ctx->cr >> 0) & 2)) { g_trampoline_fn = (void(*)(void*))func_00468DF8; return; }\n"
    "        ctx->gpr[9] = ppc_rldicl(ctx->gpr[9], 0, 32);\n"
)
A_EX2 = (
    "        if (((ctx->cr >> 0) & 2)) { g_trampoline_fn = (void(*)(void*))func_00468DF8; return; }\n"
    "        ctx->gpr[3] = (int64_t)(int32_t)(0x14);\n"
)
# tolerante ao prefixo shape-LR "ctx->lr = 0x........; "
RX_CALL30D54 = re.compile(
    r"[ \t]*(?:ctx->lr = 0x[0-9A-Fa-f]+; )?func_00330D54\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n")
A_DF98 = (
    "        if ((!((ctx->cr >> 0) & 2))) { g_trampoline_fn = (void(*)(void*))func_0032E200; return; }\n"
    "        { int64_t a = (int32_t)ctx->gpr[8]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 4)) | (cr_val << 4); }\n"
    "        if ((!((ctx->cr >> 4) & 2))) { g_trampoline_fn = (void(*)(void*))func_0032E718; return; }\n"
)

# (funcao, aplicador) -- ordem irrelevante, cada um e' scoped a' sua regiao
SITES = [
    ("func_00468C3C", lambda t, f: _insert_at_entry(t, f, "A1-468C3C", B_468C3C)),
    ("func_00468C3C", lambda t, f: _insert_before(t, f, "A1-468-EX1", A_EX1, B_EX1)),
    ("func_00468C3C", lambda t, f: _insert_before(t, f, "A1-468-EX2", A_EX2, B_EX2)),
    ("func_00468C3C", lambda t, f: _insert_before_re(t, f, "A1-CALL30D54", RX_CALL30D54, B_CALL)),
    ("func_00330D54", lambda t, f: _insert_at_entry(t, f, "A1-30D54", B_30D54)),
    ("func_0032DF98", lambda t, f: _insert_before(t, f, "A1-32DF98", A_DF98, B_DF98)),
    ("func_00468CEC", lambda t, f: _insert_before_re(t, f, "A1-CALL30D54B", RX_CALL30D54, B_CALLB)),
    ("func_00468D04", lambda t, f: _insert_before_re(t, f, "A1-CALL30D54B", RX_CALL30D54, B_CALLB)),
]


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], str(
        Path(__file__).resolve().parent.parent / "recomp_macos_v2")) if p.is_file()]
    if not paths:
        raise SystemExit("patch_a1_chain_probe: nenhum ppu_recomp_*.cpp encontrado")
    done = {}
    for p in paths:
        t0 = p.read_text(encoding="utf-8", errors="replace")
        t = t0
        for func, fn in SITES:
            t, note = fn(t, func)
            if note is not None:
                done[func + note.split(":")[0]] = True
                print("%s/%s: %s" % (p.name, func, note))
        if t != t0:
            p.write_text(t, encoding="utf-8", newline="\n")
            print("OK wrote %s" % p.name)
    missing = sorted({f for f, _ in SITES} - {k[:len("func_XXXXXXXX")] for k in done})
    if missing:
        raise SystemExit("patch_a1_chain_probe: funcao(oes) nao encontrada(s) em nenhum "
                         "chunk: %s (shape do lift mudou?)" % ", ".join(missing))
    print("OK patch_a1_chain_probe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
