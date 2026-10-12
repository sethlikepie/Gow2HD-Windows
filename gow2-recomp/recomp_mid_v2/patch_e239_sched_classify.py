import sys,pathlib
ROOT=pathlib.Path(sys.argv[1]) if len(sys.argv)>1 else pathlib.Path(__file__).resolve().parent.parent/"recomp_macos_e162"
NEEDLE="        ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x30);\n        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x0);\n"
PROBE=("        ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x30);\n"
       "        { static int _on=-1; if(_on<0){extern char* getenv(const char*); const char* _e=getenv(\"PS3_TRACE_REG\"); _on=(_e&&*_e&&*_e!='0')?1:0;}\n"
       "          if(_on){ static int _k=0; if(_k++<200) fprintf(stderr,\"[SCHED-CLASSIFY] op=0x%08X op+0x30=0x%08X op+0x40=0x%08X %s\\n\",(uint32_t)ctx->gpr[9],(uint32_t)ctx->gpr[0],vm_read32((uint32_t)ctx->gpr[9]+0x40u),((uint32_t)ctx->gpr[0]&0x200u)?\"->CANCEL\":\"->keep\"); fflush(stderr);} }\n"
       "        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x0);\n")
n=0
for f in sorted(ROOT.glob("ppu_recomp_00*.cpp")):
    s=f.read_text(errors="replace")
    if "SCHED-CLASSIFY" in s: continue
    if NEEDLE in s:
        s=s.replace(NEEDLE,"".join(PROBE),1); f.write_text(s); n+=1; print("E239: instrumentado em",f.name)
print("E239 sitios:",n); sys.exit(0 if n else 2)
