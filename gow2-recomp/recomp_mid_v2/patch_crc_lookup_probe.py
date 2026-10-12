#!/usr/bin/env python3
"""CRC/combination lookup probes (2026-07-22).

After SHADERSRC materializes 889 CFX keys, does CGOWShader's precalc CRC
lookup (func_001655F0 → 001DEC38 hash → 00163088 map) hit anything?

Chain (verified):
  sprintf TEXTURE=…  →  func_001655F0  →  table=*(**(TOC-0x3B10)+0xCC)
  → 001643F8(table, shader, combo_str)
       → 001DEC38(combo_str)  // CRC32
       → 00163088(table, …)   // map lookup
       → store result at shader+0x10

Gated PS3_TRACE_CRCLK (fallback PS3_TRACE_COMBOPROP / PS3_TRACE_SHADERSRC).
OFF default. Read-only. Marker CRC-LK.
"""
from pathlib import Path
import re
import sys

MARKER = "CRC-LK"

# CORRECCAO 2026-07-25 (shape-LR + insercao em vez de reescrita)
# ---------------------------------------------------------------
# O lifter passou a emitir as chamadas com o LR explicito a frente:
#     ctx->lr = 0x0016560C; func_001643F8(ctx); DRAIN_TRAMPOLINE(ctx);
# (antes: so' "func_001643F8(ctx); DRAIN_TRAMPOLINE(ctx);"). As agulhas
# literais deixaram de casar -- medido: "CRC-LK body needle count=0".
#
# Alem de tolerar o novo shape, o patch deixa de REESCREVER o corpo da funcao:
# se substituisse o corpo pelo literal antigo apagava o `ctx->lr = ...` que o
# lifter agora emite (mudanca de comportamento disfarcada de patch de probe).
# Passa a INSERIR blocos em ancoras -- o codigo do jogo fica intacto seja qual
# for o shape.
LR_OPT = r"(?:ctx->lr = 0x[0-9A-Fa-f]{8}; )?"
_CALL_RE = re.compile(r"func_[0-9A-Fa-f]{8}\(ctx\); DRAIN_TRAMPOLINE\(ctx\);")


def flex(lit: str) -> str:
    """Regex tolerante: linhas de chamada aceitam o prefixo `ctx->lr = 0x...;`."""
    out = []
    for line in lit.split("\n"):
        body = line.lstrip(" ")
        indent = line[: len(line) - len(body)]
        if _CALL_RE.fullmatch(body):
            out.append(re.escape(indent) + LR_OPT + re.escape(body))
        else:
            out.append(re.escape(line))
    return "\n".join(out)


def insert_after(region: str, anchor: str, text: str, label: str) -> str:
    """Insere `text` logo apos a ancora (que tem de ser unica na regiao)."""
    ms = list(re.finditer(flex(anchor), region))
    if len(ms) != 1:
        raise SystemExit(f"{label}: ancora aparece {len(ms)}x na regiao (esperado 1)")
    cut = ms[0].end()
    return region[:cut] + text + region[cut:]


def insert_between(region: str, head: str, tail: str, text: str, label: str) -> str:
    """Insere `text` entre dois blocos consecutivos; o par tem de ser unico.

    Serve quando o bloco `head` sozinho nao e' unico (ex.: 4 call sites de
    func_001DEC38 no mesmo chunk) e e' o `tail` que desambigua.
    """
    ms = list(re.finditer("(" + flex(head) + ")" + flex(tail), region))
    if len(ms) != 1:
        raise SystemExit(f"{label}: ancora aparece {len(ms)}x (esperado 1)")
    cut = ms[0].end(1)
    return region[:cut] + text + region[cut:]


def region_of(src: str, func: str, label: str):
    """Delimita o corpo de `func` (assinatura -> proxima definicao)."""
    sig = f"void {func}(ppu_context* ctx) {{\n"
    i = src.find(sig)
    if i < 0:
        raise SystemExit(f"{label}: {func} ausente no lift")
    j = src.find("\nvoid func_", i + len(sig))
    return i, (j if j > i else len(src))

ROOT = (Path(sys.argv[1]) if len(sys.argv) > 1
        else Path(__file__).resolve().parent.parent / "recomp_macos_v2")
C0 = ROOT / "ppu_recomp_000.cpp"

s = C0.read_text(encoding="utf-8", errors="replace")
changed = False

PROBE_ON = (
    '{ static int on=-1; if(on<0){extern char* getenv(const char*);\n'
    '            const char* e=getenv("PS3_TRACE_CRCLK");\n'
    '            const char* e2=getenv("PS3_TRACE_COMBOPROP");\n'
    '            const char* e3=getenv("PS3_TRACE_SHADERSRC");\n'
    '            on=((e&&*e&&*e!=\'0\')||(e2&&*e2&&*e2!=\'0\')'
    '||(e3&&*e3&&*e3!=\'0\'))?1:0;}\n'
)


def guest_cstr_snippet():
    """C snippet: copy up to 48 printable chars from guest EA in variable `str_ea`."""
    return (
        "            char snip[52]; int si=0;\n"
        "            if(str_ea && str_ea < 0xFFF00000u){\n"
        "              for(; si<48; si++){\n"
        "                uint8_t c=vm_read8(str_ea+si);\n"
        "                if(!c) break;\n"
        "                snip[si]=(c>=32&&c<127)?(char)c:\'.\';\n"
        "              }\n"
        "            }\n"
        "            snip[si]=0;\n"
    )


if "[CRC-LK]" not in s:
    lo, hi = region_of(s, "func_001655F0", "CRC-LK")
    reg = s[lo:hi]

    # (1) argumentos de entrada, antes de r3 ser esmagado pelo load da tabela
    reg = insert_after(
        reg,
        "void func_001655F0(ppu_context* ctx) {\n",
        f"        /* {MARKER}: combination CRC lookup */\n"
        "        uint32_t _crc_sh = (uint32_t)ctx->gpr[3];\n"
        "        uint32_t _crc_a4 = (uint32_t)ctx->gpr[4];\n"
        "        uint32_t _crc_str = (uint32_t)ctx->gpr[5];\n",
        "CRC-LK/args",
    )
    # (2) tabela resolvida: *(**(TOC-0x3B10)+0xCC)
    reg = insert_after(
        reg,
        "        ctx->gpr[3] = vm_read32(ctx->gpr[9] + 0xCC);\n",
        "        uint32_t _crc_tbl = (uint32_t)ctx->gpr[3];\n"
        "        uint32_t _crc_root = (uint32_t)ctx->gpr[9];\n"
        "        uint32_t _crc_slot = (uint32_t)ctx->gpr[11];\n",
        "CRC-LK/tbl",
    )
    # (3) resultado do lookup, ja' depositado em shader+0x10 por 001643F8
    reg = insert_after(
        reg,
        "        func_001643F8(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n",
        f"        {PROBE_ON}"
        "          if(on){ static int n=0; static int hits=0, miss=0;\n"
        "            uint32_t res = _crc_sh ? vm_read32(_crc_sh + 0x10) : 0;\n"
        "            if(res) hits++; else miss++;\n"
        "            if(n++<48 || (n%200)==0){\n"
        "              uint32_t str_ea = _crc_str;\n"
        + guest_cstr_snippet() +
        "              fprintf(stderr,\n"
        "                \"[CRC-LK] #%d hit=%u res=0x%08X sh=0x%08X tbl=0x%08X root=0x%08X "
        "slot=0x%08X a4=0x%08X str=0x%08X '%s' hits=%d miss=%d\\n\",\n"
        "                n, res?1:0, res, _crc_sh, _crc_tbl, _crc_root, _crc_tbl,\n"
        "                _crc_a4, _crc_str, snip, hits, miss);\n"
        "              fflush(stderr);\n"
        "            } } }\n",
        "CRC-LK/res",
    )
    s = s[:lo] + reg + s[hi:]
    changed = True
    print("CRC-LK: added on 001655F0")
else:
    print("CRC-LK: already present")

# Hash probe inside 001643F8 after 001DEC38
# CORRECCAO 2026-07-25: mesma ancora, agora tolerante ao prefixo `ctx->lr = ...`
# das duas chamadas (001DEC38 e 00163088) e por insercao -- o bloco de codigo
# do jogo que delimita a ancora nao e' reescrito.
if "[CRC-HASH]" not in s:
    s = insert_between(
        s,
        "        func_001DEC38(ctx); DRAIN_TRAMPOLINE(ctx);\n"
        "        /* nop */;\n",
        "        ctx->gpr[5] = ppc_rldicl(ctx->gpr[3], 0, 32);\n"
        "        ctx->gpr[4] = ppc_rldicl(ctx->gpr[29], 0, 32);\n"
        "        ctx->gpr[3] = ppc_rldicl(ctx->gpr[27], 0, 32);\n"
        "        ctx->gpr[6] = ctx->gpr[28] | ctx->gpr[28];\n"
        "        func_00163088(ctx); DRAIN_TRAMPOLINE(ctx);\n",
        f"        /* {MARKER}: CRC32 of combination string */\n"
        f"        {PROBE_ON}"
        "          if(on){ static int n=0; if(n++<48)\n"
        "            fprintf(stderr,\"[CRC-HASH] #%d crc=0x%08X str=0x%08X tbl=0x%08X\\n\",\n"
        "              n,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[28],\n"
        "              (uint32_t)ctx->gpr[27]); fflush(stderr);} }\n",
        "CRC-HASH",
    )
    changed = True
    print("CRC-HASH: added")
else:
    print("CRC-HASH: already present")

# One-shot dump of CRC table root after SHADERSRC era: hook first CRC-LK only
# already dumps tbl/root. Also dump table head fields if tbl non-null:
# We enhance first log via separate helper on 001655F0 when n==1 — already have tbl.

if changed:
    C0.write_text(s, encoding="utf-8", newline="\n")
    print("OK wrote ppu_recomp_000.cpp")
else:
    print("000 unchanged")
print("OK patch_crc_lookup_probe")
