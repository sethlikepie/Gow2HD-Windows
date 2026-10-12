#!/usr/bin/env python3
"""Contracts for observed SPU indirect-branch destinations."""
from __future__ import annotations

import importlib.util
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / (name + ".py"))
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


LIFTER = load("spu_lifter")
VERIFY = load("verify_spu_indirect_entries")


def test_observed_targets_promote_only_indirect_branch_addresses() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        log = Path(tmp) / "spu.log"
        log.write_text(
            "noise 0xAAAA\n"
            "[SPU] indirect branch to unknown LS address 0x0391C (image 1)\n"
            "[SPUBRP] pc=0xD9DC target=0x0391F lr=0xD9E0 image=1\n")
        assert LIFTER.observed_indirect_targets([str(log)]) == [0x391C]


def test_contract_rejects_unregistered_observed_target() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        lift = Path(tmp) / "lift"
        lift.mkdir()
        (lift / "spu_recomp.c").write_text(
            "static const int table[] = { { 0x00003000u, spu0_spu_func_00003000 } };\n")
        observed = Path(tmp) / "observed.txt"
        observed.write_text("target 0x391C\n")
        assert VERIFY.main([str(lift), "spu0_", "--observed", str(observed)]) == 1
        (lift / "spu_recomp.c").write_text(
            "static const int table[] = { { 0x0000391Cu, spu0_spu_func_0000391C } };\n"
            "void f(spu_context* ctx) { ctx->pc = 0x391Cu; spu_indirect_branch(ctx); }\n")
        assert VERIFY.main([str(lift), "spu0_", "--observed", str(observed)]) == 0


def main() -> int:
    test_observed_targets_promote_only_indirect_branch_addresses()
    test_contract_rejects_unregistered_observed_target()
    print("[spu-indirect-entries] todos os casos passaram")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
