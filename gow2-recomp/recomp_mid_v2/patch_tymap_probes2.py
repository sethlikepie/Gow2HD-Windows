#!/usr/bin/env python3
"""Extra probes: richer TYDISP descriptor dump + 171244/GroupStart entry.

CORRECCAO 2026-07-25 (chunk-fixo + shape-callee-save + agulha do produtor)
--------------------------------------------------------------------------
Tres coisas partiram este script contra o lift actual (medido, nao suposto):

  1. chunk-fixo -- abria ROOT/"ppu_recomp_000.cpp" e ROOT/"ppu_recomp_001.cpp"
     por nome. O lifter passou de 31 para 7 chunks e as funcoes mudaram de
     ficheiro: func_00294004 esta' HOJE no chunk 001, nao no 000, por isso a
     Parte 3 abortava com "294004 missing". Agora cada chunk e' localizado
     pela funcao que contem (resolve_lift_paths + procura da assinatura).
  2. shape-callee-save -- o prologo passou a comecar por
         uint64_t _cs_27 = ctx->gpr[27];  ...  uint64_t _cs_31 = ctx->gpr[31];
     antes do 1o statement do jogo, e a agulha literal da Parte 2
     ("void func_00171244... vm_write64(...-0xC0...)") deixou de casar
     ("171 entry needle missing"). O bloco de callee-save passa a ser
     opcional na agulha, e a probe e' inserida DEPOIS dele.
  3. agulha do produtor do [TYDISP] -- a Parte 1 enriquece um probe que quem
     instala e' o patch_jumptable_2b11b8.py. Esse produtor emite hoje
     "if(n++<40){ ... fflush(stderr); } } }" e a agulha aqui esperava a forma
     antiga "if(n++<40) ... } }". Passam a aceitar-se as DUAS formas (tokens
     separados por \\s*). Se mesmo assim nao houver probe [TYDISP] no lift
     (porque o patch_jumptable_2b11b8.py ainda nao correu, ou o
     patch_tydisp_lr.py ja' consumiu a agulha), continua a ser um SKIP
     declarado e nao-fatal -- nunca um check enfraquecido.
"""
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

PATHS = resolve_lift_paths(sys.argv[1:], str(Path(__file__).resolve().parent))
if not PATHS:
    raise SystemExit("nenhum ppu_recomp_*.cpp encontrado")

_TEXT: dict = {}


def text(p: Path) -> str:
    if p not in _TEXT:
        _TEXT[p] = p.read_text(encoding="utf-8", errors="replace")
    return _TEXT[p]


def chunk_with_func(func: str, label: str) -> Path:
    sig = f"void {func}(ppu_context* ctx) {{\n"
    for p in PATHS:
        if sig in text(p):
            return p
    raise SystemExit(f"{label}: {func} ausente no lift")


def chunk_with_text(frag: str):
    for p in PATHS:
        if frag in text(p):
            return p
    return None


def tokens(lit: str) -> str:
    """Agulha tolerante a indentacao/quebras de linha (tokens com \\s* entre)."""
    return r"\s*".join(re.escape(t) for t in lit.split())


# ---------------------------------------------------------------- Parte 1
# Enriquecer o [TYDISP] instalado pelo patch_jumptable_2b11b8.py.
# Bloco enriquecido (o mesmo de sempre -- a cauda "); } } }" e' o que o
# patch_tydisp_lr.py procura a seguir, por isso a forma nao muda).
NEW_TYDISP = """          { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYDISP")?1:0;}
            if(on){ static int n=0; if(n++<40){
              uint32_t raw=vm_read32(ctx->gpr[4]+0x0);
              uint32_t w1=vm_read32(ctx->gpr[4]+0x4);
              uint32_t w2=vm_read32(ctx->gpr[4]+0x8);
              fprintf(stderr,"[TYDISP] type=%u -> 0x%08X desc=0x%08X raw=0x%08X w1=0x%08X w2=0x%08X r3=0x%08X\\n",
                ty, tgt, (uint32_t)ctx->gpr[4], raw, w1, w2, (uint32_t)ctx->gpr[3]); } } }"""

# Forma actual do produtor (com chaveta no if e fflush).
OLD_TYDISP_BRACED = """{ static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYDISP")?1:0;}
            if(on){ static int n=0; if(n++<40){
              fprintf(stderr,"[TYDISP] type=%u -> 0x%08X\\n", ty, tgt); fflush(stderr); } } }"""
# Forma antiga (sem chaveta, sem fflush) -- mantida para lifts anteriores.
OLD_TYDISP_PLAIN = """{ static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYDISP")?1:0;}
            if(on){ static int n=0; if(n++<40)
              fprintf(stderr,"[TYDISP] type=%u -> 0x%08X\\n", ty, tgt); } }"""

P_TYDISP = chunk_with_text("[TYDISP] type=%u")
if P_TYDISP is None:
    print("TYDISP probe ausente no lift (patch_jumptable_2b11b8.py ainda nao "
          "correu) -- skip, non-fatal")
elif "desc=0x%08X raw=" in text(P_TYDISP):
    print("TYDISP already enhanced")
else:
    s1 = text(P_TYDISP)
    for shape, lit in (("braced", OLD_TYDISP_BRACED), ("plain", OLD_TYDISP_PLAIN)):
        m = re.search(tokens(lit), s1)
        if m:
            s1 = s1[:m.start()] + NEW_TYDISP.lstrip() + s1[m.end():]
            _TEXT[P_TYDISP] = s1
            P_TYDISP.write_text(s1, encoding="utf-8", newline="\n")
            print(f"TYDISP enhanced ({shape}) em {P_TYDISP.name}")
            break
    else:
        # NON-FATAL (2026-07-21, Task 1 fix): patch_tydisp_lr.py is an ALTERNATE
        # enhancement of this same [TYDISP] fprintf, not a prerequisite. When
        # tydisp_lr runs first (alphabetical order) it consumes this exact
        # needle and leaves the "lr" variant instead of the "desc/raw/w1/w2"
        # variant this block wants -- expected, harmless. Previously this
        # branch aborted the WHOLE script before Parts 2 and 3 (the probes this
        # script exists to install).
        print("TYDISP needle missing (superseded by patch_tydisp_lr.py order) "
              "-- skip, non-fatal")

# ---------------------------------------------------------------- Parte 2
# Entrada de func_00171244. O bloco de callee-save do prologo novo fica ENTRE
# a assinatura e o 1o statement do jogo -- a probe entra depois dele.
CS_BLOCK = (r"(?:[ \t]*uint64_t _cs_\d+ = "
            r"(?:ctx->gpr\[\d+\]|vm_read64\(ctx->gpr\[1\] \+ 0x[0-9A-Fa-f]+\));\n)*")

P171 = chunk_with_func("func_00171244", "171")
s0 = text(P171)
if "TYMAP-171" in s0:
    print("171 already")
else:
    head = "void func_00171244(ppu_context* ctx) {\n"
    tail = "        vm_write64(ctx->gpr[1] + -0xC0, ctx->gpr[1]); ctx->gpr[1] += -0xC0;\n"
    pat = re.compile("(" + re.escape(head) + CS_BLOCK + ")" + re.escape(tail))
    m = pat.search(s0)
    if m is None:
        raise SystemExit("171 entry needle missing")
    probe = (
        '        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}\n'
        '          if(on){ static int n=0; if(n++<32)\n'
        '            fprintf(stderr,"[TYMAP-171] #%d r3=0x%08X r4=0x%08X\\n", n,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[4]); fflush(stderr);} }\n'
    )
    cut = m.end(1)
    s0 = s0[:cut] + probe + s0[cut:]
    _TEXT[P171] = s0
    P171.write_text(s0, encoding="utf-8", newline="\n")
    print(f"171 entry probe em {P171.name}")

# ---------------------------------------------------------------- Parte 3
# GroupStart ctor. Pode viver noutro chunk que nao o do 171244 (hoje vive).
PGS = chunk_with_func("func_00294004", "294004")
sg = text(PGS)
if "TYMAP-GS" in sg:
    print("GS already")
else:
    head = "void func_00294004(ppu_context* ctx) {\n"
    pat = re.compile("(" + re.escape(head) + CS_BLOCK + ")")
    m = pat.search(sg)
    if m is None:
        raise SystemExit("294004 missing")
    probe = (
        '        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}\n'
        '          if(on){ static int n=0; if(n++<32)\n'
        '            fprintf(stderr,"[TYMAP-GS] #%d GroupStartCtor r3=0x%08X r4=0x%08X\\n",\n'
        '              n,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[4]); fflush(stderr);} }\n'
    )
    cut = m.end(1)
    sg = sg[:cut] + probe + sg[cut:]
    _TEXT[PGS] = sg
    PGS.write_text(sg, encoding="utf-8", newline="\n")
    print(f"GS ctor probe em {PGS.name}")

print("OK probes2")
