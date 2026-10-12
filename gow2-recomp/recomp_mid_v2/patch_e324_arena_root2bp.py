"""E324 (DIAGNOSTICO gated PS3_ARENA_ROOT2BP, OFF por defeito -- NAO e' o fix proprio):
prova que o crash 0x4BFFFE34 e' a alocacao dos objetos do shell da arena Root em vez da BP.

Medido (E324): em func_00262808 (provedor da arena do scope corrente) NOS devolvemos 0x40004020=Root
(index de scope 127) quando a CONSOLA devolve 0x33004020=BP (index 126) -- os shells alocam da Root,
que CONTEM a tabela de objetos (0x40004480) -> colisao -> handles tagged 0x84000013 -> vtable lixo ->
stuck calling 0x4BFFFE34. Este diagnostico redireciona Root->BP (a arena da consola): o crash DESAPARECE
e o boot chega ao menu (filme 330 frames, boot logo, hold cleared guest/menu). PROVA a raiz §4cb-§4cd.
NAO e' o fix proprio (esse e' corrigir o indice do scope 126 vs 127); e' um diagnostico gated OFF.
"""
import sys, pathlib
ROOT = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent / "recomp_macos_e162"
F = ROOT / "ppu_recomp_000.cpp"
s = F.read_text()
if "PS3_ARENA_ROOT2BP" in s:
    print("patch_e324_arena_root2bp: already present"); sys.exit(0)
idx = s.find("void func_00262808(ppu_context* ctx) {")
assert idx > 0, "func_00262808 not found"
anchor = "loc_00262834:\n        ctx->gpr[3] = ppc_rldicl(ctx->gpr[3], 0, 32);\n        return;"
j = s.find(anchor, idx)
assert j > 0, "loc_00262834 anchor not found in func_00262808"
if True:
    repl = ("loc_00262834:\n"
            "        { static int _r2b=-1; if(_r2b<0){extern char* getenv(const char*); const char* _e=getenv(\"PS3_ARENA_ROOT2BP\"); _r2b=(_e&&*_e&&*_e!='0')?1:0;}\n"
            "          if(_r2b && (uint32_t)ctx->gpr[3]==0x40004020u){ static int _k=0; if(_k++<8){fprintf(stderr,\"[ARENA-ROOT2BP] redirect Root->BP\\n\");fflush(stderr);} ctx->gpr[3]=0x43004020u; } }\n"
            "        ctx->gpr[3] = ppc_rldicl(ctx->gpr[3], 0, 32);\n        return;")
    s = s[:j] + repl + s[j+len(anchor):]
    F.write_text(s)
    print("patch_e324_arena_root2bp: applied")
else:
    print("patch_e324_arena_root2bp: already present")
