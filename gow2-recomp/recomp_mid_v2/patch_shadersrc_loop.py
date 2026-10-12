#!/usr/bin/env python3
"""SHADERSRC loop probes (2026-07-22): what happens after N>0.

func_003CC208 reads N then either early-exits or loops over records:
stream-read key -> parse helpers -> insert via func_003C8578.

With HOSTRES fixed, N sum~=889 natural. This patch measures whether the loop
*runs* and *inserts*, vs bailing on capacity/size gates -- without forging CRC
or registry.

Gated by PS3_TRACE_SHADERSRC (fallback PS3_TRACE_TYMAP). OFF default.
Idempotent marker SS-LOOP. Read-only.

CORRECCAO 2026-07-25 (shape-LR + shape-callee-save + chunk-fixo)
----------------------------------------------------------------
Tres coisas partiram este patch contra o lift actual (medido, nao suposto):

  1. shape-LR -- as chamadas passaram a levar o link register a frente:
       ctx->lr = 0x003CC3A4; func_003C8578(ctx); DRAIN_TRAMPOLINE(ctx);
     (antes so' "func_003C8578(ctx); DRAIN_TRAMPOLINE(ctx);").
  2. shape-callee-save -- prologo passou a capturar os nao-volateis
       uint64_t _cs_21 = ctx->gpr[21];          (funcao inteira)
       uint64_t _cs_21 = vm_read64(ctx->gpr[1] + 0xF8);   (fragmento partido)
     e o epilogo restaura de `_cs_21` em vez de reler a stack. Alem disso o
     `addi` mudou de forma: `(int64_t)(int32_t)(ctx->gpr[28] + 1)` passou a
     `ctx->gpr[28] + (int64_t)(1)`.
  3. chunk-fixo -- o lift deixou de ter 31 chunks (tem 7) e abrir
     "ppu_recomp_001/003/005.cpp" por nome deixou de ser garantia de nada.

Correccao: as agulhas passam por flex() (regex que casa com o shape ANTIGO e
com o NOVO), os chunks sao localizados pela funcao que contem, e as probes sao
INSERIDAS entre duas ancoras em vez de o bloco de codigo do jogo ser reescrito
-- reescrever com o literal antigo apagaria os `ctx->lr = ...` e os `_cs_NN`
que o lifter agora emite, o que seria mudar comportamento dentro de um patch
que so' devia medir.
"""
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

MARKER = "SS-LOOP"

# --- matcher tolerante ao shape do lifter -----------------------------------
LR_OPT = r"(?:ctx->lr = 0x[0-9A-Fa-f]{8}; )?"
CS_BLOCK = (r"(?:[ \t]*uint64_t _cs_\d+ = "
            r"(?:ctx->gpr\[\d+\]|vm_read64\(ctx->gpr\[1\] \+ 0x[0-9A-Fa-f]+\));\n)*")
_SIG = re.compile(r"void func_[0-9A-Fa-f]{8}\(ppu_context\* ctx\) \{")
_CALL = re.compile(r"func_[0-9A-Fa-f]{8}\(ctx\); DRAIN_TRAMPOLINE\(ctx\);")
_CSR = re.compile(r"ctx->gpr\[(\d+)\] = vm_read64\(ctx->gpr\[1\] \+ 0x[0-9A-Fa-f]+\);")
_ADDI = re.compile(
    r"ctx->gpr\[(\d+)\] = \(int64_t\)\(int32_t\)"
    r"\(ctx->gpr\[(\d+)\] \+ (-?(?:0x[0-9A-Fa-f]+|\d+))\);")


def _line_pat(line: str) -> str:
    nl = "\n" if line.endswith("\n") else ""
    core = line[:-1] if nl else line
    body = core.lstrip(" ")
    ind = re.escape(core[: len(core) - len(body)])
    if _SIG.fullmatch(body):                      # assinatura + callee-save novo
        return ind + re.escape(body) + nl + CS_BLOCK
    if _CALL.fullmatch(body):                     # shape-LR
        return ind + LR_OPT + re.escape(body) + nl
    m = _CSR.fullmatch(body)                      # restauro do epilogo
    if m:
        alt = f"ctx->gpr[{m.group(1)}] = _cs_{m.group(1)};"
        return ind + "(?:" + re.escape(body) + "|" + re.escape(alt) + ")" + nl
    m = _ADDI.fullmatch(body)                     # addi mudou de forma
    if m:
        d, a, imm = m.groups()
        alt = f"ctx->gpr[{d}] = ctx->gpr[{a}] + (int64_t)({imm});"
        return ind + "(?:" + re.escape(body) + "|" + re.escape(alt) + ")" + nl
    return re.escape(core) + nl


def flex(lit: str) -> str:
    return "".join(_line_pat(l) for l in lit.splitlines(keepends=True))


def weave(src: str, head: str, tail: str, ins: str, label: str) -> str:
    """Insere `ins` entre `head` e `tail`; o par tem de ser unico em `src`.

    O texto encontrado e' preservado tal e qual -- so' se acrescenta a probe.
    """
    ms = list(re.finditer("(" + flex(head) + ")" + flex(tail), src))
    if len(ms) != 1:
        raise SystemExit(f"{label}: ancora aparece {len(ms)}x (esperado 1)")
    cut = ms[0].end(1)
    return src[:cut] + ins + src[cut:]


# --- resolucao de chunks pelo conteudo, nao pelo numero ---------------------
PATHS = resolve_lift_paths(
    sys.argv[1:], str(Path(__file__).resolve().parent.parent / "recomp_macos_v2"))
if not PATHS:
    raise SystemExit("nenhum ppu_recomp_*.cpp encontrado")

_TEXT: dict = {}


def text(p: Path) -> str:
    if p not in _TEXT:
        _TEXT[p] = p.read_text(encoding="utf-8", errors="replace")
    return _TEXT[p]


def chunk_with(func: str) -> Path:
    """Chunk que define `func` (o lift ja' mudou de 31 para 7 ficheiros)."""
    sig = f"void {func}(ppu_context* ctx) {{\n"
    for p in PATHS:
        if sig in text(p):
            return p
    raise SystemExit(f"{func} ausente no lift ({len(PATHS)} chunks varridos)")


PROBE_ON = (
    '{ static int on=-1; if(on<0){extern char* getenv(const char*);\n'
    '            const char* e=getenv("PS3_TRACE_SHADERSRC"); '
    'const char* e2=getenv("PS3_TRACE_TYMAP");\n'
    '            on=((e&&*e&&*e!=\'0\')||(e2&&*e2&&*e2!=\'0\'))?1:0;}\n'
)

C1 = chunk_with("func_003CC208")
s = text(C1)
changed = False


def add(label, head, tail, ins):
    """Insere a probe `label` entre head/tail no chunk de func_003CC208."""
    global s, changed
    if f"[{label}]" in s:
        print(f"{label}: already present")
        return
    s = weave(s, head, tail, ins, label)
    changed = True
    print(f"{label}: added")


# --- early exit: N too large (cmp vs 0x4EC4, GT -> 003CC4BC) ---
add(
    "SS-BIG",
    "",
    "        if (((ctx->cr >> 0) & 4)) { g_trampoline_fn = (void(*)(void*))func_003CC4BC; return; }\n"
    "        ctx->gpr[11] = vm_read32(ctx->gpr[30] + 0x4);\n",
    f"        /* {MARKER} */\n"
    f"        {PROBE_ON}"
    "          if(on && ((ctx->cr >> 0) & 4)){ static int n=0; if(n++<16)\n"
    "            fprintf(stderr,\"[SS-BIG] #%d N=%d obj=0x%08X (N>0x4EC4)\\n\",\n"
    "              n,(int32_t)ctx->gpr[25],(uint32_t)ctx->gpr[26]); fflush(stderr);} }\n",
)

# --- early exit: capacity / grow fail -> 003CC3F4 ---
add(
    "SS-CAP",
    "",
    "        if (((ctx->cr >> 0) & 8)) { g_trampoline_fn = (void(*)(void*))func_003CC3F4; return; }\n"
    "        { int64_t a = (int32_t)ctx->gpr[25]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n",
    f"        /* {MARKER} */\n"
    f"        {PROBE_ON}"
    "          if(on && ((ctx->cr >> 0) & 8)){ static int n=0; if(n++<16)\n"
    "            fprintf(stderr,\"[SS-CAP] #%d N=%d obj=0x%08X (capacity LT)\\n\",\n"
    "              n,(int32_t)ctx->gpr[25],(uint32_t)ctx->gpr[26]); fflush(stderr);} }\n",
)

# --- N<=0 skip loop ---
add(
    "SS-EMPTY",
    "",
    "        if ((!((ctx->cr >> 0) & 4))) goto loc_003CC3B8;\n"
    "        ctx->gpr[28] = (int64_t)(int32_t)(0);\n",
    f"        /* {MARKER} */\n"
    f"        {PROBE_ON}"
    "          if(on && (!((ctx->cr >> 0) & 4))){ static int n=0; if(n++<32)\n"
    "            fprintf(stderr,\"[SS-EMPTY] #%d N=%d obj=0x%08X skip-loop\\n\",\n"
    "              n,(int32_t)ctx->gpr[25],(uint32_t)ctx->gpr[26]); fflush(stderr);} }\n",
)

# --- record key after first stream read in loop ---
add(
    "SS-REC",
    "        func_001856A8(ctx); DRAIN_TRAMPOLINE(ctx);\n"
    "        /* nop */;\n"
    "        ctx->gpr[0] = vm_read32(ctx->gpr[1] + 0x70);\n"
    "        ctx->gpr[3] = ctx->gpr[22] | ctx->gpr[22];\n"
    "        ctx->gpr[4] = ctx->gpr[27] | ctx->gpr[27];\n"
    "        vm_write32(ctx->gpr[1] + 0xB0, ctx->gpr[0]);\n",
    "        func_003CBB98(ctx); DRAIN_TRAMPOLINE(ctx);\n",
    f"        /* {MARKER}: record key */\n"
    f"        {PROBE_ON}"
    "          if(on){ static int n=0; if(n++<48)\n"
    "            fprintf(stderr,\"[SS-REC] #%d i=%d key=0x%08X N=%d obj=0x%08X\\n\",\n"
    "              n,(int32_t)ctx->gpr[28],(uint32_t)ctx->gpr[0],\n"
    "              (int32_t)ctx->gpr[25],(uint32_t)ctx->gpr[26]); fflush(stderr);} }\n",
)

# --- insert path ---
add(
    "SS-INS",
    "        ctx->gpr[28] = (int64_t)(int32_t)(ctx->gpr[28] + 1);\n",
    "        func_003C8578(ctx); DRAIN_TRAMPOLINE(ctx);\n"
    "        /* nop */;\n"
    "        ctx->gpr[3] = ctx->gpr[31] | ctx->gpr[31];\n"
    "        func_0015DB48(ctx); DRAIN_TRAMPOLINE(ctx);\n",
    f"        /* {MARKER}: insert */\n"
    f"        {PROBE_ON}"
    "          if(on){ static int n=0; static int tot=0; tot++;\n"
    "            if(n++<32 || (tot%100)==0)\n"
    "            fprintf(stderr,\"[SS-INS] #%d tot=%d i=%d key@b0=0x%08X N=%d\\n\",\n"
    "              n,tot,(int32_t)ctx->gpr[28],\n"
    "              vm_read32(ctx->gpr[1]+0xB0),(int32_t)ctx->gpr[25]); fflush(stderr);} }\n",
)

# --- DONE on normal epilogue (only the one in 003CC208: restore after loc_003CC3B8) ---
# The epilogue starts at loc_003CC3B8. There may be similar patterns elsewhere;
# anchor with the unique stack restore size 0x150 of this function.
add(
    "SS-DONE",
    "loc_003CC3B8:\n",
    "        ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0x160);\n"
    "        ctx->gpr[21] = vm_read64(ctx->gpr[1] + 0xF8);\n"
    "        ctx->gpr[22] = vm_read64(ctx->gpr[1] + 0x100);\n",
    f"        /* {MARKER}: loop done */\n"
    f"        {PROBE_ON}"
    "          if(on){ static int n=0; if(n++<32)\n"
    "            fprintf(stderr,\"[SS-DONE] #%d i=%d N=%d obj=0x%08X\\n\",\n"
    "              n,(int32_t)ctx->gpr[28],(int32_t)ctx->gpr[25],\n"
    "              (uint32_t)ctx->gpr[26]); fflush(stderr);} }\n",
)

if changed:
    _TEXT[C1] = s
    C1.write_text(s, encoding="utf-8", newline="\n")
    print(f"OK wrote {C1.name}")
else:
    print(f"{C1.name} unchanged")

# --- entry of insert helper (global count, may be called from elsewhere) ---
# Pode viver noutro chunk que nao o de func_003CC208 -- resolvido por conteudo.
CI = chunk_with("func_003C8578")
si = text(CI)
if "[SS-INSFN]" not in si:
    si = weave(
        si,
        "void func_003C8578(ppu_context* ctx) {\n",
        "        vm_write64(ctx->gpr[1] + -0x140, ctx->gpr[1]); ctx->gpr[1] += -0x140;\n",
        f"        /* {MARKER}: insert helper entry */\n"
        f"        {PROBE_ON}"
        "          if(on){ static int n=0; if(n++<24 || (n%200)==0)\n"
        "            fprintf(stderr,\"[SS-INSFN] #%d tbl=0x%08X flag=%d rec=0x%08X\\n\",\n"
        "              n,(uint32_t)ctx->gpr[3],(int32_t)ctx->gpr[5],\n"
        "              (uint32_t)ctx->gpr[6]); fflush(stderr);} }\n",
        "SS-INSFN",
    )
    _TEXT[CI] = si
    CI.write_text(si, encoding="utf-8", newline="\n")
    print(f"SS-INSFN: added (OK wrote {CI.name})")
else:
    print("SS-INSFN: already present")

# --- Grow path resume lives in a LIFTER SPLIT fragment func_003CC2B8,
# not in 003CC208 body. After SS-CAP -> 003CC3F4 grow, trampoline lands here. ---
C5 = chunk_with("func_003CC2B8")
s5 = text(C5)
ch5 = False

if "[SS-RESUME]" not in s5:
    s5 = weave(
        s5,
        "void func_003CC2B8(ppu_context* ctx) {\n",
        "        { int64_t a = (int32_t)ctx->gpr[25]; int64_t b = (int64_t)0; "
        "uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; "
        "ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n",
        f"        /* {MARKER}: post-grow resume */\n"
        f"        {PROBE_ON}"
        "          if(on){ static int n=0; if(n++<32)\n"
        "            fprintf(stderr,\"[SS-RESUME] #%d N=%d obj=0x%08X base=0x%08X\\n\",\n"
        "              n,(int32_t)ctx->gpr[25],(uint32_t)ctx->gpr[26],\n"
        "              (uint32_t)vm_read32(ctx->gpr[26]+0x4)); fflush(stderr);} }\n",
        "SS-RESUME",
    )
    ch5 = True
    print("SS-RESUME: added")
else:
    print("SS-RESUME: already present")

if "[SS-REC2]" not in s5:
    s5 = weave(
        s5,
        "        func_001856A8(ctx); DRAIN_TRAMPOLINE(ctx);\n"
        "        /* nop */;\n"
        "        ctx->gpr[0] = vm_read32(ctx->gpr[1] + 0x70);\n"
        "        ctx->gpr[3] = ctx->gpr[22] | ctx->gpr[22];\n"
        "        ctx->gpr[4] = ctx->gpr[27] | ctx->gpr[27];\n"
        "        vm_write32(ctx->gpr[1] + 0xB0, ctx->gpr[0]);\n",
        "        func_003CBB98(ctx); DRAIN_TRAMPOLINE(ctx);\n",
        f"        /* {MARKER}: rec on resume fragment */\n"
        f"        {PROBE_ON}"
        "          if(on){ static int n=0; if(n++<48)\n"
        "            fprintf(stderr,\"[SS-REC2] #%d i=%d key=0x%08X N=%d\\n\",\n"
        "              n,(int32_t)ctx->gpr[28],(uint32_t)ctx->gpr[0],\n"
        "              (int32_t)ctx->gpr[25]); fflush(stderr);} }\n",
        "SS-REC2",
    )
    ch5 = True
    print("SS-REC2: added")
else:
    print("SS-REC2: already present")

if "[SS-INS2]" not in s5:
    s5 = weave(
        s5,
        "        ctx->gpr[28] = (int64_t)(int32_t)(ctx->gpr[28] + 1);\n",
        "        func_003C8578(ctx); DRAIN_TRAMPOLINE(ctx);\n"
        "        /* nop */;\n"
        "        ctx->gpr[3] = ctx->gpr[31] | ctx->gpr[31];\n"
        "        func_0015DB48(ctx); DRAIN_TRAMPOLINE(ctx);\n",
        f"        /* {MARKER}: insert on resume fragment */\n"
        f"        {PROBE_ON}"
        "          if(on){ static int n=0; static int tot=0; tot++;\n"
        "            if(n++<32 || (tot%100)==0)\n"
        "            fprintf(stderr,\"[SS-INS2] #%d tot=%d i=%d key=0x%08X N=%d\\n\",\n"
        "              n,tot,(int32_t)ctx->gpr[28],\n"
        "              vm_read32(ctx->gpr[1]+0xB0),(int32_t)ctx->gpr[25]); fflush(stderr);} }\n",
        "SS-INS2",
    )
    ch5 = True
    print("SS-INS2: added")
else:
    print("SS-INS2: already present")

# DONE on resume fragment epilogue -- loc_003CC3B8 only as goto target then return
if "[SS-DONE2]" not in s5:
    # find epilogue of 003CC2B8: after loop exit, restore stack 0x150
    i = s5.find("void func_003CC2B8")
    j = s5.find("\nvoid func_", i + 20)
    region = s5[i:j]
    head = "loc_003CC3B8:\n"
    tail = ("        ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0x160);\n"
            "        ctx->gpr[21] = vm_read64(ctx->gpr[1] + 0xF8);\n"
            "        ctx->gpr[22] = vm_read64(ctx->gpr[1] + 0x100);\n")
    if not re.search(flex(head) + flex(tail), region):
        # maybe different shape -- search
        print("SS-DONE2: epilogue needle missing in 003CC2B8; dump tail:")
        print(region[-800:])
        raise SystemExit("SS-DONE2 needle missing")
    region = weave(
        region, head, tail,
        f"        /* {MARKER}: done on resume fragment */\n"
        f"        {PROBE_ON}"
        "          if(on){ static int n=0; if(n++<32)\n"
        "            fprintf(stderr,\"[SS-DONE2] #%d i=%d N=%d obj=0x%08X\\n\",\n"
        "              n,(int32_t)ctx->gpr[28],(int32_t)ctx->gpr[25],\n"
        "              (uint32_t)ctx->gpr[26]); fflush(stderr);} }\n",
        "SS-DONE2",
    )
    s5 = s5[:i] + region + s5[j:]
    ch5 = True
    print("SS-DONE2: added")
else:
    print("SS-DONE2: already present")

if ch5:
    _TEXT[C5] = s5
    C5.write_text(s5, encoding="utf-8", newline="\n")
    print(f"OK wrote {C5.name}")
else:
    print(f"{C5.name} unchanged")

# Grow path end: trampoline to 003CC2B8 (vive no chunk de func_003CC3F4)
C3 = chunk_with("func_003CC3F4")
s3 = text(C3)
ch3 = False
if "[SS-GROW]" not in s3:
    s3 = weave(
        s3,
        "        vm_write32(ctx->gpr[26] + 0xC, ctx->gpr[0]);\n"
        "        vm_write32(ctx->gpr[26] + 0x8, ctx->gpr[9]);\n"
        "        vm_write32(ctx->gpr[26] + 0x4, ctx->gpr[24]);\n",
        "        { g_trampoline_fn = (void(*)(void*))func_003CC2B8; return; }\n",
        f"        /* {MARKER}: grow done -> resume */\n"
        f"        {PROBE_ON}"
        "          if(on){ static int n=0; if(n++<32)\n"
        "            fprintf(stderr,\"[SS-GROW] #%d N=%d newbase=0x%08X end=0x%08X\\n\",\n"
        "              n,(int32_t)ctx->gpr[25],(uint32_t)ctx->gpr[24],\n"
        "              (uint32_t)ctx->gpr[0]); fflush(stderr);} }\n",
        "SS-GROW",
    )
    ch3 = True
    print("SS-GROW: added")
else:
    print("SS-GROW: already present")

if ch3:
    _TEXT[C3] = s3
    C3.write_text(s3, encoding="utf-8", newline="\n")
    print(f"OK wrote {C3.name}")
else:
    print(f"{C3.name} unchanged")

print("OK patch_shadersrc_loop")
