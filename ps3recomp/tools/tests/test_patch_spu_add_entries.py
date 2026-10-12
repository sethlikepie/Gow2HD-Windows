#!/usr/bin/env python3
"""Regression tests for safe partial SPU-entry imports."""
from __future__ import annotations

import importlib.util
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "patch_spu_add_entries", ROOT / "tools" / "patch_spu_add_entries.py")
assert SPEC and SPEC.loader
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


OLD_HEADER = """#pragma once
#include <stdint.h>
typedef struct spu_context spu_context;
void spu_indirect_branch(spu_context* ctx);
/* Channel access -- implemented by the runtime. */
/* Call once at init to register this image's functions for indirect branches. */
"""

OLD_SOURCE = """#include \"spu_recomp.h\"
typedef struct { uint32_t addr; void (*func)(spu_context*); const char* name; } spu_func_entry;
void spu0_spu_func_00003000(spu_context* ctx) { (void)ctx; }
static const spu_func_entry spu_function_table[] = {
    { 0x00003000u, spu0_spu_func_00003000, \"spu0_spu_func_00003000\" },
    { 0, NULL, NULL }
};
"""

FRESH_SOURCE = """#include \"spu_recomp.h\"
typedef struct { uint32_t addr; void (*func)(spu_context*); const char* name; } spu_func_entry;
void spu0_spu_func_00003000(spu_context* ctx) { (void)ctx; }
void spu0_spu_func_0000391C(spu_context* ctx) {
    spu_halt_if(ctx, 0, 0x391Cu);
}
static const spu_func_entry spu_function_table[] = {
    { 0x00003000u, spu0_spu_func_00003000, \"spu0_spu_func_00003000\" },
    { 0x0000391Cu, spu0_spu_func_0000391C, \"spu0_spu_func_0000391C\" },
    { 0, NULL, NULL }
};
"""


def test_import_adds_required_runtime_declaration() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        lift = Path(tmp) / "old"
        lift.mkdir()
        (lift / "spu_recomp.h").write_text(OLD_HEADER)
        (lift / "spu_recomp.c").write_text(OLD_SOURCE)
        fresh = Path(tmp) / "fresh.c"
        fresh.write_text(FRESH_SOURCE)

        assert PATCH.main(str(lift), str(fresh), "spu0_", "391C") == 0

        header = (lift / "spu_recomp.h").read_text()
        source = (lift / "spu_recomp.c").read_text()
        assert "void spu_halt_if(spu_context* ctx, int taken, uint32_t pc);" in header
        assert "void spu0_spu_func_0000391C(spu_context* ctx);" in header
        assert "spu0_spu_func_0000391C" in source
        assert "{ 0x0000391Cu, spu0_spu_func_0000391C" in source


def main() -> int:
    test_import_adds_required_runtime_declaration()
    print("[patch-spu-add-entries] todos os casos passaram")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
