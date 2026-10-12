#!/usr/bin/env python3
"""Hook func_001E7B50 to host_res_inflate (EBOOT gzip HOSTRES path)."""
from pathlib import Path
import sys

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
p = ROOT / "ppu_recomp_000.cpp"
s = p.read_text(encoding="utf-8", errors="replace")
if "host_res_inflate" in s:
    print("already")
    raise SystemExit(0)

# Declare next to other extern C helpers if present, else before first use.
decl = 'extern "C" int host_res_inflate(ppu_context* ctx);\n'
if 'extern "C" void ps3_indirect_call' in s and decl not in s:
    s = s.replace(
        'extern "C" void ps3_indirect_call(ppu_context* ctx);',
        'extern "C" void ps3_indirect_call(ppu_context* ctx);\n' + decl,
        1,
    )
elif decl not in s:
    # fallback: insert after includes
    s = s.replace('#include <math.h>\n', '#include <math.h>\n\n' + decl, 1)

old = """void func_001E7B50(ppu_context* ctx) {
"""
# Prefer matching the real lifted prologue if present
idx = s.find("void func_001E7B50(ppu_context* ctx)")
if idx < 0:
    raise SystemExit("func_001E7B50 not found")
# find opening brace body start
brace = s.find("{", idx)
if brace < 0:
    raise SystemExit("no body")
# insert early-return host path right after '{'
insert = (
    "{\n"
    "        /* Host gzip path for embedded EBOOT resources (see host_res_inflate.c). */\n"
    "        { static int on=-1; if(on<0){extern char* getenv(const char*);\n"
    "            const char* e=getenv(\"PS3_TRACE_HOSTRES\"); on=(e&&*e&&*e!='0')?1:0;}\n"
    "          if(on){ static int n=0; if(n++<48)\n"
    "            fprintf(stderr,\"[HOSTRES-ENTER] #%d dst=0x%08X src=0x%08X clen=0x%X\\n\",\n"
    "              n,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[5],(uint32_t)ctx->gpr[6]);\n"
    "            fflush(stderr);} }\n"
    "        if (host_res_inflate(ctx)) return;\n"
)
# replace "void func...{\n" with insert version
end_line = s.find("\n", brace) + 1
old_block = s[idx:end_line]
new_block = s[idx:brace] + insert
if "host_res_inflate(ctx)" in s[idx:idx+400]:
    print("already in body")
    raise SystemExit(0)
s = s[:idx] + new_block + s[end_line:]
p.write_text(s, encoding="utf-8", newline="\n")
print("OK host_res_inflate hook")
