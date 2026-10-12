#!/usr/bin/env python3
"""Task 5 / ICGLdr unlock: instrument type-map lookup + fix OPD dispatch.

Hypothesis: ICGLdrShader (0xF85F9B1E) is registered into the map at TOC+0xD8C/D90
via func_0018F160, but the lookup path (func_00171244 -> func_0018E814) either
never runs, uses a different map (TOC-0x3A3C), or fails the node+0x10 OPD call
(ps3_indirect_call treats OPD as code).

This patch (idempotent):
  1. Declare ps3_call_opd no chunk que precisa dele
  2. [TYMAP-LK] probe a' entrada de func_0018E814 (map, type, hit/miss, maps)
  3. Substitui o OPD node+0x10 em 18E814 por ps3_call_opd
  4. Substitui o OPD pre-lookup em 171244 por ps3_call_opd
  5. [TYMAP-REG] probe na registracao (func_00321034) comparando map pointers

CORRECCAO 2026-07-25 -- porque o script tinha deixado de aplicar
---------------------------------------------------------------
Quatro causas, todas de FORMA do lift (nenhum check foi enfraquecido):

  chunk-fixo    abria "ppu_recomp_000.cpp"/"ppu_recomp_001.cpp" pelo nome. O
                lifter actual produz 7 chunks (eram 31); passa a aceitar um
                DIRECTORIO (resolve_lift_paths) e a procurar o chunk que DEFINE
                cada funcao.

  shape-TOCFIX  o restauro do TOC depois de uma chamada indirecta passou de
                    ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);
                para
                    ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/
                As agulhas de 18E814 e 171244 tinham a linha antiga literal.
                Agora e' um grupo de regex que aceita as duas formas e a linha
                encontrada e' RE-EMITIDA tal e qual (nao regredimos o TOCFIX do
                lifter novo).

  shape-outro   o epilogo mudou de
                    ctx->gpr[1] = (int64_t)(int32_t)(ctx->gpr[1] + 0x70);
                para
                    ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x70);
                A probe [TYMAP-RET] deixa de depender do corpo do epilogo:
                ancora so' na etiqueta "loc_0018E8BC:", dentro da regiao de
                func_0018E814.

  shape-LR      as chamadas ganharam prefixo "ctx->lr = 0x...;" (+ "/* nop */;"
                a seguir). A agulha de func_00321034 exigia
                "func_0018F160(ctx); DRAIN_TRAMPOLINE(ctx);" cru.

  ORFAO         a mesma agulha de func_00321034 exigia AINDA um bloco de probe
                [SHREG] (PS3_TRACE_SHREG) preexistente. Esse bloco nao existe
                num lift limpo e NENHUM script o instala -- nem sequer esta' no
                lift de producao recomp_macos_v2 (grep SHREG = 0). Era uma
                edicao manual de uma sessao antiga. A correccao NAO instala o
                [SHREG] (nao e' deste patch e nao ha' fonte versionada dele):
                torna-o OPCIONAL na agulha e ancora a insercao do [TYMAP-REG]
                nas 3 leituras do TOC (0xD94/0xD8C/0xD90) que a precedem, que
                sao codigo gerado pelo lifter e existem sempre.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

DEFAULT_LIFT = str(Path(__file__).resolve().parent.parent / "recomp_macos_v2")

DECL_OLD = 'extern "C" void ps3_indirect_call(ppu_context* ctx);'
DECL_NEW = DECL_OLD + '\nextern "C" void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);'

# Restauro do TOC a seguir a uma chamada: forma antiga (ld do frame) ou nova
# (TOCFIX com o valor resolvido estaticamente).
TOC_RESTORE = (
    r"(        ctx->gpr\[2\] = (?:vm_read64\(ctx->gpr\[1\] \+ 0x28\);"
    r"|0x[0-9A-Fa-f]+ULL; /\*TOCFIX[^\n]*)\n)"
)


def find_def_chunk(paths: list[Path], fn: str) -> tuple[Path, str] | None:
    pat = re.compile(r"^void " + fn + r"\(ppu_context\* ctx\) \{", re.M)
    for p in paths:
        if not p.is_file():
            continue
        t = p.read_text(encoding="utf-8", errors="replace")
        if pat.search(t):
            return p, t
    return None


def region_of(t: str, fn: str) -> tuple[int, int]:
    m = re.search(r"^void " + fn + r"\(ppu_context\* ctx\) \{", t, re.M)
    if not m:
        raise SystemExit(f"{fn} nao encontrado")
    nxt = re.compile(r"^void func_[0-9A-Fa-f]+\(ppu_context\* ctx\) \{", re.M)
    m2 = nxt.search(t, m.end())
    return m.start(), (m2.start() if m2 else len(t))


def ensure_decl(t: str, name: str) -> tuple[str, bool]:
    if "void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);" in t:
        print(f"{name}: ps3_call_opd already declared")
        return t, False
    if DECL_OLD not in t:
        raise SystemExit(f"ps3_indirect_call decl missing in {name}")
    print(f"{name}: declared ps3_call_opd")
    return t.replace(DECL_OLD, DECL_NEW, 1), True


# --------------------------------------------------------------- func_0018E814
LK_PROBE = """        /* Task5 TYMAP: type-map lookup (map=r3, type=r4, out=r5). Compare map
         * against TOC+0xD8C (ICGLdr register map) and TOC-0x3A3C (171244 map). */
        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<64){
            uint32_t map=(uint32_t)ctx->gpr[3], ty=(uint32_t)ctx->gpr[4];
            uint32_t d8c=vm_read32(ctx->gpr[2]+0xD8C), d90=vm_read32(ctx->gpr[2]+0xD90);
            uint32_t m3a=vm_read32(ctx->gpr[2]+(uint32_t)(-0x3A3C));
            fprintf(stderr,"[TYMAP-LK] #%d map=0x%08X type=0x%08X d8c=0x%08X d90=0x%08X m3a3c=0x%08X same_d8c=%d same_d90=%d same_m3a=%d\\n",
              n,map,ty,d8c,d90,m3a,map==d8c,map==d90,map==m3a); fflush(stderr);} } }
"""

LK_ANCHOR = re.compile(
    r"(void func_0018E814\(ppu_context\* ctx\) \{\n"
    r"(?:        uint64_t _cs_\d+ = ctx->gpr\[\d+\];\n)*)"
)

OPD_18E814 = re.compile(
    r"(        ctx->gpr\[11\] = vm_read32\(ctx->gpr\[9\] \+ 0x10\);\n)"
    r"        ctx->gpr\[0\] = vm_read32\(ctx->gpr\[11\] \+ 0x0\);\n"
    r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n"
    r"        ctx->ctr = \(uint32_t\)ctx->gpr\[0\];\n"
    r"        ctx->gpr\[2\] = vm_read32\(ctx->gpr\[11\] \+ 0x4\);\n"
    r"        ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    + TOC_RESTORE
)

HIT_BLOCK = """        /* Task5 TYMAP: node+0x10 is an OPD (or direct code EA); use ps3_call_opd. */
        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<32)
            fprintf(stderr,"[TYMAP-HIT] #%d node=0x%08X opd=0x%08X type_key_via_r30_ish\\n",
              n,(uint32_t)ctx->gpr[9],(uint32_t)ctx->gpr[11]); fflush(stderr);} }
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
        ps3_call_opd(ctx, (uint32_t)ctx->gpr[11]); DRAIN_TRAMPOLINE(ctx);
"""


# NOTA: a substituicao usa um CALLABLE, nunca uma string-template. O texto
# inserido tem "\\n" dentro de literais C (fprintf) e re.sub interpretaria "\n"
# do template como newline real, partindo a string C.
def _opd_18e814_repl(m: "re.Match[str]") -> str:
    return m.group(1) + HIT_BLOCK + m.group(2)

RET_ANCHOR = re.compile(r"(loc_0018E8BC:\n)")
RET_PROBE = """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<64)
            fprintf(stderr,"[TYMAP-RET] #%d r3=0x%08X\\n", n, (uint32_t)ctx->gpr[3]); fflush(stderr);} }
"""

# --------------------------------------------------------------- func_00171244
OPD_171244 = re.compile(
    r"        ctx->gpr\[0\] = vm_read32\(ctx->gpr\[9\] \+ 0x0\);\n"
    r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n"
    r"        ctx->ctr = \(uint32_t\)ctx->gpr\[0\];\n"
    r"        ctx->gpr\[2\] = vm_read32\(ctx->gpr\[9\] \+ 0x4\);\n"
    r"        ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    + TOC_RESTORE +
    r"(        ctx->gpr\[4\] = ctx->gpr\[29\] \| ctx->gpr\[29\];\n"
    r"        ctx->gpr\[0\] = \(int64_t\)\(int32_t\)\(0\);\n"
    r"        ctx->gpr\[28\] = vm_read32\(ctx->gpr\[2\] \+ -0x3A3C\);\n)"
)

VT_BLOCK = """        /* Task5 TYMAP: vtable slot is OPD; use ps3_call_opd before map lookup. */
        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<32)
            fprintf(stderr,"[TYMAP-VT] #%d opd_slot=0x%08X type_tag=0x%08X\\n",
              n,(uint32_t)ctx->gpr[9],(uint32_t)ctx->gpr[29]); fflush(stderr);} }
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
        ps3_call_opd(ctx, (uint32_t)ctx->gpr[9]); DRAIN_TRAMPOLINE(ctx);
"""


def _opd_171244_repl(m: "re.Match[str]") -> str:
    # g1 = linha de restauro do TOC (forma antiga OU TOCFIX), g2 = cauda
    return VT_BLOCK + m.group(1) + m.group(2)

# --------------------------------------------------------------- func_00321034
REG_ANCHOR = re.compile(
    r"(        ctx->gpr\[6\] = vm_read32\(ctx->gpr\[2\] \+ 0xD94\);\n"
    r"        ctx->gpr\[3\] = vm_read32\(ctx->gpr\[2\] \+ 0xD8C\);\n"
    r"        ctx->gpr\[4\] = vm_read32\(ctx->gpr\[2\] \+ 0xD90\);\n)"
    r"(        (?:ctx->lr = 0x[0-9A-Fa-f]+; )?func_0018F160\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n)"
)

REG_PROBE = """        /* Task5 TYMAP: dump register-side map pointers vs lookup TOC-0x3A3C. */
        { static int on=-1; if(on<0){extern char* getenv(const char*); on=(getenv("PS3_TRACE_TYMAP")||getenv("PS3_TRACE_SHREG"))?1:0;}
          if(on){ static int n=0; if(n++<8){
            uint32_t d8c=(uint32_t)ctx->gpr[3], d90=(uint32_t)ctx->gpr[4], d94=(uint32_t)ctx->gpr[6];
            uint32_t m3a=vm_read32(ctx->gpr[2]+(uint32_t)(-0x3A3C));
            fprintf(stderr,"[TYMAP-REG] #%d type=0xF85F9B1E d8c=0x%08X d90=0x%08X d94=0x%08X m3a3c=0x%08X d8c_eq_m3a=%d d90_eq_m3a=%d\\n",
              n,d8c,d90,d94,m3a,d8c==m3a,d90==m3a); fflush(stderr);} } }
"""


def patch_18e814(paths: list[Path]) -> None:
    found = find_def_chunk(paths, "func_0018E814")
    if not found:
        raise SystemExit("func_0018E814 nao definido em nenhum chunk")
    p, t = found
    t, _ = ensure_decl(t, p.name)
    a, b = region_of(t, "func_0018E814")
    r = t[a:b]

    if "TYMAP-LK" in r:
        print(f"{p.name}: TYMAP-LK already present")
    else:
        m = LK_ANCHOR.search(r)
        if not m:
            raise SystemExit("18E814 entry needle missing")
        r = r[: m.end(1)] + LK_PROBE + r[m.end(1):]
        print(f"{p.name}: TYMAP-LK entry probe")

    if "TYMAP-HIT" in r:
        print(f"{p.name}: 18E814 OPD already patched")
    else:
        r2, n = OPD_18E814.subn(_opd_18e814_repl, r, count=1)
        if n != 1:
            raise SystemExit("18E814 OPD needle missing in region")
        r = r2
        print(f"{p.name}: 18E814 OPD -> ps3_call_opd + TYMAP-HIT")

    if "TYMAP-RET" in r:
        print(f"{p.name}: TYMAP-RET already present")
    else:
        m = RET_ANCHOR.search(r)
        if not m:
            raise SystemExit("18E814 loc_0018E8BC needle missing")
        r = r[: m.end(1)] + RET_PROBE + r[m.end(1):]
        print(f"{p.name}: TYMAP-RET probe")

    t = t[:a] + r + t[b:]
    p.write_text(t, encoding="utf-8", newline="\n")


def patch_171244(paths: list[Path]) -> None:
    found = find_def_chunk(paths, "func_00171244")
    if not found:
        raise SystemExit("func_00171244 nao definido em nenhum chunk")
    p, t = found
    t, changed = ensure_decl(t, p.name)
    a, b = region_of(t, "func_00171244")
    r = t[a:b]
    if "TYMAP-VT" in r:
        print(f"{p.name}: 171244 OPD already patched")
        if changed:
            p.write_text(t, encoding="utf-8", newline="\n")
        return
    r2, n = OPD_171244.subn(_opd_171244_repl, r, count=1)
    if n != 1:
        raise SystemExit("171244 OPD needle missing")
    t = t[:a] + r2 + t[b:]
    p.write_text(t, encoding="utf-8", newline="\n")
    print(f"{p.name}: 171244 OPD -> ps3_call_opd + TYMAP-VT")


def patch_321034(paths: list[Path]) -> None:
    found = find_def_chunk(paths, "func_00321034")
    if not found:
        raise SystemExit("func_00321034 nao definido em nenhum chunk")
    p, t = found
    a, b = region_of(t, "func_00321034")
    r = t[a:b]
    if "TYMAP-REG" in r:
        print(f"{p.name}: TYMAP-REG already present")
        return
    m = REG_ANCHOR.search(r)
    if not m:
        raise SystemExit("321034 registration needle missing")
    r = r[: m.end(1)] + REG_PROBE + r[m.start(2):]
    t = t[:a] + r + t[b:]
    p.write_text(t, encoding="utf-8", newline="\n")
    print(f"{p.name}: TYMAP-REG probe")


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], DEFAULT_LIFT)
    paths = [p for p in paths if p.is_file()]
    if not paths:
        print("FAILED: nenhum chunk de lift encontrado")
        return 1
    patch_18e814(paths)
    patch_171244(paths)
    patch_321034(paths)
    print("OK: patch_tymap_18e814 applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
