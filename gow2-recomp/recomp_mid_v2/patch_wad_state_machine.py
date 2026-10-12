#!/usr/bin/env python3
"""Instrument WAD type-system state machine + fix OPD in func_002B9C80.

Path (discovered):
  type_sys = *TOC-0x122C
  state = type_sys+0x1CC  (1=open, 2=read header, 3=read body)
  func_002BA76C state machine
  state 3 -> func_002BAB88 -> func_002B9C80(header, buf)
  2B9C80: type = *(u16*)header; OPD = table[type]+8; call OPD
  This is how GroupStart/type factories fire for streamed WAD members.

Also switch OPD dispatch to ps3_call_opd (same class of fix as Task4).

CORRECCAO 2026-07-25 (shape-callee-save + shape-LR + shape-TOCFIX + chunk-fixo)
-------------------------------------------------------------------------------
Quatro mudancas do lifter partiram as agulhas literais (medido, nao suposto):

  1. shape-callee-save -- o prologo passou a comecar por
       uint64_t _cs_29 = ctx->gpr[29]; ...
     antes do 1o statement do jogo (agulhas de 2B9C80 e 2BA76C).
  2. shape-LR -- as chamadas levam agora o link register a frente:
       ctx->lr = 0x002B9CA8; func_002B9C68(ctx); DRAIN_TRAMPOLINE(ctx);
  3. shape-TOCFIX -- o restauro do TOC depois da chamada indirecta passou de
       ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);
     para
       ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/
     Esta linha JA' NAO e' tocada por este patch: o substituto e' recortado
     para acabar na chamada, e o restauro do TOC fica como o lifter o emitiu
     (reescreve-lo com a forma antiga seria desfazer a resolucao estatica).
  4. chunk-fixo -- abria ROOT/"ppu_recomp_001.cpp" por nome; com 7 chunks (em
     vez de 31) isso deixou de ser garantia. O chunk e' agora localizado pela
     funcao que contem.

O unico corte deliberado no codigo do jogo continua a ser o mesmo de sempre --
trocar a sequencia ctr/r2 + ps3_indirect_call por ps3_call_opd. Todo o resto
(incluindo o `ctx->lr = ...` das chamadas) e' preservado tal e qual.
"""
from __future__ import annotations
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

# --- matcher tolerante ao shape do lifter -----------------------------------
LR_OPT = r"(?:ctx->lr = 0x[0-9A-Fa-f]{8}; )?"
CS_BLOCK = (r"(?:[ \t]*uint64_t _cs_\d+ = "
            r"(?:ctx->gpr\[\d+\]|vm_read64\(ctx->gpr\[1\] \+ 0x[0-9A-Fa-f]+\));\n)*")
_SIG = re.compile(r"void func_[0-9A-Fa-f]{8}\(ppu_context\* ctx\) \{")
_CALL = re.compile(r"func_[0-9A-Fa-f]{8}\(ctx\); DRAIN_TRAMPOLINE\(ctx\);")
_ADDI = re.compile(
    r"ctx->gpr\[(\d+)\] = \(int64_t\)\(int32_t\)"
    r"\(ctx->gpr\[(\d+)\] \+ (-?(?:0x[0-9A-Fa-f]+|\d+))\);")


def _line_pat(line: str) -> str:
    nl = "\n" if line.endswith("\n") else ""
    core = line[:-1] if nl else line
    body = core.lstrip(" ")
    ind = re.escape(core[: len(core) - len(body)])
    if _SIG.fullmatch(body):
        return ind + re.escape(body) + nl + CS_BLOCK
    if _CALL.fullmatch(body):
        return ind + LR_OPT + re.escape(body) + nl
    m = _ADDI.fullmatch(body)
    if m:
        d, a, imm = m.groups()
        alt = f"ctx->gpr[{d}] = ctx->gpr[{a}] + (int64_t)({imm});"
        return ind + "(?:" + re.escape(body) + "|" + re.escape(alt) + ")" + nl
    return re.escape(core) + nl


def flex(lit: str) -> str:
    return "".join(_line_pat(l) for l in lit.splitlines(keepends=True))


def weave(src: str, head: str, tail: str, ins: str, label: str) -> str:
    """Insere `ins` entre `head` e `tail`, preservando o texto encontrado."""
    ms = list(re.finditer("(" + flex(head) + ")" + flex(tail), src))
    if len(ms) != 1:
        raise SystemExit(f"{label}: ancora aparece {len(ms)}x (esperado 1)")
    cut = ms[0].end(1)
    return src[:cut] + ins + src[cut:]


# --- resolucao de chunks pelo conteudo --------------------------------------
PATHS = resolve_lift_paths(sys.argv[1:], str(Path(__file__).resolve().parent))
if not PATHS:
    raise SystemExit("nenhum ppu_recomp_*.cpp encontrado")


def chunk_with(func: str) -> Path:
    sig = f"void {func}(ppu_context* ctx) {{\n"
    for p in PATHS:
        if sig in p.read_text(encoding="utf-8", errors="replace"):
            return p
    raise SystemExit(f"{func} ausente no lift")


C1 = chunk_with("func_002B9C80")
for other in ("func_002BA76C", "func_002BAB88"):
    if chunk_with(other) != C1:
        raise SystemExit(
            f"{other} vive noutro chunk que nao {C1.name} -- o patch assume os"
            " tres juntos (declaracao de ps3_call_opd partilhada); reveja")
s = C1.read_text(encoding="utf-8", errors="replace")


def region_of(func: str):
    sig = f"void {func}(ppu_context* ctx) {{\n"
    i = s.find(sig)
    if i < 0:
        raise SystemExit(f"{func} ausente em {C1.name}")
    j = s.find("\nvoid func_", i + len(sig))
    return i, (j if j > i else len(s))


# --- declare ps3_call_opd if missing ---
if "void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);" not in s:
    old = 'extern "C" void ps3_indirect_call(ppu_context* ctx);'
    if old not in s:
        raise SystemExit(f"ps3_indirect_call decl missing in {C1.name}")
    s = s.replace(
        old,
        old + '\nextern "C" void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);',
        1,
    )
    print(f"{C1.name}: declared ps3_call_opd")

# --- probe + OPD fix in 2B9C80 ---
# A sequencia substituida e' SO' o carregamento manual de ctr/r2 + a chamada
# indirecta. O restauro do TOC a seguir (hoje "ctx->gpr[2] = 0x00541178ULL;
# /*TOCFIX*/", antes "vm_read64(sp+0x28)") fica INTACTO -- ver nota 3 no topo.
OPD_CALL_RE = re.compile(
    r"        ctx->gpr\[0\] = vm_read32\(ctx->gpr\[9\] \+ 0x0\);\n"
    r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n"
    r"        ctx->ctr = \(uint32_t\)ctx->gpr\[0\];\n"
    r"        ctx->gpr\[2\] = vm_read32\(ctx->gpr\[9\] \+ 0x4\);\n"
    r"        (ctx->lr = 0x[0-9A-Fa-f]{8}; )?"
    r"ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
)

if "WADLD-CALL" not in s:
    lo, hi = region_of("func_002B9C80")
    reg = s[lo:hi]

    reg = weave(
        reg,
        "        ctx->gpr[30] = ctx->gpr[4] | ctx->gpr[4];\n",
        "        func_002B9C68(ctx); DRAIN_TRAMPOLINE(ctx);\n",
        """        /* Task5: WAD member type-loader dispatch. type=u16 header; table[type] OPD. */
        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<64){
            uint32_t ty=(uint32_t)ctx->gpr[3], hdr=(uint32_t)ctx->gpr[31], buf=(uint32_t)ctx->gpr[30];
            fprintf(stderr,"[WADLD-CALL] #%d type=%u hdr=0x%08X buf=0x%08X\\n", n, ty, hdr, buf); fflush(stderr);} } }
""",
        "WADLD-CALL",
    )

    ms = list(OPD_CALL_RE.finditer(reg))
    if len(ms) != 1:
        raise SystemExit(f"2B9C80: sequencia da chamada indirecta aparece "
                         f"{len(ms)}x (esperado 1)")
    m = ms[0]
    lr = m.group(1) or ""
    reg = reg[:m.start()] + (
        """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<64)
            fprintf(stderr,"[WADLD-OPD] #%d opd=0x%08X code_peek=0x%08X\\n",
              n,(uint32_t)ctx->gpr[9], vm_read32(ctx->gpr[9]+0x0)); fflush(stderr);} }
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
"""
        + "        " + lr
        + "ps3_call_opd(ctx, (uint32_t)ctx->gpr[9]); DRAIN_TRAMPOLINE(ctx);\n"
    ) + reg[m.end():]

    s = s[:lo] + reg + s[hi:]
    print("patched 2B9C80")
else:
    print("2B9C80 already patched")

# --- probe state machine 2BA76C ---
if "WADLD-SM" not in s:
    s = weave(
        s,
        """void func_002BA76C(ppu_context* ctx) {
        vm_write64(ctx->gpr[1] + -0xB0, ctx->gpr[1]); ctx->gpr[1] += -0xB0;
        ctx->gpr[0] = ctx->lr;
        vm_write64(ctx->gpr[1] + 0xA8, ctx->gpr[31]);
        ctx->gpr[31] = vm_read32(ctx->gpr[2] + -0x122C);
""",
        "",
        """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<80){
            uint32_t ts=(uint32_t)ctx->gpr[31];
            fprintf(stderr,"[WADLD-SM] #%d ts=0x%08X state=%u rem=0x%X flag1B0=%u\\n",
              n, ts, vm_read32(ts+0x1CC), vm_read32(ts+0x1AC), vm_read32(ts+0x1B0)); fflush(stderr);} } }
""",
        "WADLD-SM",
    )
    print("patched 2BA76C probe")
else:
    print("2BA76C already")

# --- probe 2BAB88 body-done path (before 2B9C80) ---
if "WADLD-BODY" not in s:
    s = weave(
        s,
        """        ctx->gpr[29] = (int64_t)(int32_t)(ctx->gpr[31] + 0x7C);
        ctx->gpr[4] = vm_read32(ctx->gpr[31] + 0x1C4);
        ctx->gpr[29] = ppc_rldicl(ctx->gpr[29], 0, 32);
        ctx->gpr[3] = ctx->gpr[29] | ctx->gpr[29];
""",
        """        func_002B9BFC(ctx); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[3] = ctx->gpr[29] | ctx->gpr[29];
        ctx->gpr[4] = vm_read32(ctx->gpr[31] + 0x1C4);
        func_002B9C80(ctx); DRAIN_TRAMPOLINE(ctx);
""",
        """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<40){
            uint32_t hdr=(uint32_t)ctx->gpr[3], buf=(uint32_t)ctx->gpr[4];
            uint16_t ty=vm_read16(hdr+0x0);
            fprintf(stderr,"[WADLD-BODY] #%d hdr=0x%08X buf=0x%08X type16=%u name4=0x%08X\\n",
              n, hdr, buf, ty, vm_read32(hdr+0x8)); fflush(stderr);} } }
""",
        "WADLD-BODY",
    )
    print("patched WADLD-BODY probe")
else:
    print("BODY already")

C1.write_text(s, encoding="utf-8", newline="\n")
print("OK patch_wad_state_machine")
