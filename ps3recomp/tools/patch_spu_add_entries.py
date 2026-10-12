#!/usr/bin/env python3
"""patch_spu_add_entries.py -- add missing SPU indirect-branch entries to an
existing (older-style) lift without regenerating it.

A lifted SPU image aborts its job when an indirect branch lands on an address
with no registered function ("[SPU] indirect branch to unknown LS address").
Regenerating the whole image with today's lifter rewrites thousands of lines
(a past spu1 re-lift lost entries and was reverted), so this copies ONLY the
functions the old lift lacks from a fresh lift made with those entries:

    tools/spu_lifter.py --auto-functions spu1.elf --symbol-prefix spu1_ \\
        --extra-funcs <every entry of the old lift>,0x6088,... -o NEW
    tools/patch_spu_add_entries.py OLD_DIR NEW_DIR/spu_recomp.c spu1_ 0x6088,...

Only the requested entries are copied, plus any function the old lift lacks
that they reference (the fresh lift also finds extra entries on its own,
some outside the image -- those are NOT imported). The copied functions are
translated to the old file's style (trampoline tail
branches instead of SPU_TAILCALL, SPU_DRAIN after calls). Every function the
old lift already has is left byte-for-byte alone. Idempotent.
"""
import re
import sys
from pathlib import Path


RUNTIME_DECLARATIONS = {
    "spu_halt_if": "void spu_halt_if(spu_context* ctx, int taken, uint32_t pc);\n",
    "spu_stop": "void spu_stop(spu_context* ctx);\n",
}


def funcs(text: str, prefix: str) -> dict:
    out = {}
    for m in re.finditer(r"^void %sspu_func_([0-9A-F]{8})\(spu_context\* ctx\) \{\n.*?^\}\n"
                         % re.escape(prefix), text, re.M | re.S):
        out[int(m.group(1), 16)] = m.group(0)
    return out


def to_old_style(fn: str, prefix: str) -> str:
    p = re.escape(prefix)
    # call: link register then a direct call -> old style drains the trampoline
    fn = re.sub(r"ctx->gpr\[0\] = spu_link\((0x[0-9A-F]+)\); (%sspu_func_[0-9A-F]{8})\(ctx\);" % p,
                r"ctx->gpr[0] = spu_splat_u32(\1); \2(ctx); SPU_DRAIN(ctx);", fn)
    # brsl get-PC idiom (link into a non-r0 register, no call)
    fn = re.sub(r"ctx->gpr\[(\d+)\] = spu_link\((0x[0-9A-F]+)\);",
                r"ctx->gpr[\1] = spu_splat_u32(\2);", fn)
    # tail branch to another lifted function -> trampoline
    fn = re.sub(r"SPU_TAILCALL\((%sspu_func_[0-9A-F]{8})\(ctx\)\);" % p,
                r"g_spu_trampoline_fn = \1; return;", fn)
    return fn


def main(old_dir: str, new_c: str, prefix: str, entries: str) -> int:
    src = Path(old_dir) / "spu_recomp.c"
    hdr = Path(old_dir) / "spu_recomp.h"
    c, h = src.read_text(), hdr.read_text()
    old = funcs(c, prefix)
    new = funcs(Path(new_c).read_text(), prefix)
    want = [int(x, 16) for x in entries.split(",") if x]
    for a in want:
        if a not in new and a not in old:
            print("patch_spu_add_entries: 0x%X not in the fresh lift" % a, file=sys.stderr)
            return 1
    # closure: requested entries + missing functions they reference
    missing, todo = set(), [a for a in want if a not in old]
    while todo:
        a = todo.pop()
        if a in missing or a in old:
            continue
        missing.add(a)
        for r in re.findall(r"%sspu_func_([0-9A-F]{8})" % re.escape(prefix), new[a]):
            r = int(r, 16)
            if r not in old and r not in missing:
                if r not in new:
                    print("patch_spu_add_entries: 0x%X references 0x%X, absent" % (a, r),
                          file=sys.stderr)
                    return 1
                todo.append(r)
    missing = sorted(missing)
    # A second invocation may find that all requested entries are already
    # present, while an older import was made before a runtime helper had a
    # declaration.  Derive declarations from the fresh requested bodies too,
    # so the repair remains idempotent and strict-C clean.
    declaration_source = "\n".join(new[a] for a in want if a in new)
    needed_decls = [decl for name, decl in RUNTIME_DECLARATIONS.items()
                    if name in declaration_source and decl not in h]
    if needed_decls:
        glue = h.index("/* Channel access -- implemented by the runtime")
        h = h[:glue] + "/* Runtime helpers required by imported entries. */\n" + \
            "".join(needed_decls) + "\n" + h[glue:]
        hdr.write_text(h)
    if not missing:
        print("patch_spu_add_entries: nothing to add" +
              ("; added declarations" if needed_decls else ""))
        return 0
    bodies, rows, protos = [], [], []
    for a in missing:
        name = "%sspu_func_%08X" % (prefix, a)
        body = to_old_style(new[a], prefix)
        if "SPU_TAILCALL" in body or "spu_link(" in body:
            print("patch_spu_add_entries: untranslated construct in", name, file=sys.stderr)
            return 1
        bodies.append("/* Added by tools/patch_spu_add_entries.py (indirect-branch entry). */\n" + body)
        rows.append('    { 0x%08Xu, %s, "%s" },\n' % (a, name, name))
        protos.append("void %s(spu_context* ctx);\n" % name)
    # functions before the registration table, rows at its end, prototypes at the header end
    table = c.index("static const spu_func_entry spu_function_table[]")
    c = c[:table] + "\n".join(bodies) + "\n" + c[table:]
    end = c.index("    { 0, NULL, NULL }", c.index("static const spu_func_entry spu_function_table[]"))
    c = c[:end] + "".join(rows) + c[end:]
    imported = "\n".join(bodies)
    reg = h.index("/* Call once at init to register this image's functions")
    h = h[:reg] + "".join(protos) + h[reg:]
    src.write_text(c)
    hdr.write_text(h)
    print("patch_spu_add_entries: added", ", ".join("0x%X" % a for a in missing))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 5:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(*sys.argv[1:]))
