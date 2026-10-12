"""Self-check for recomp_config's TOML parser (Xenon-style declarative recomp config).

No pytest dependency (matches this repo's tools/test_*.py convention -- plain
asserts + main()). Needs tomllib (Python 3.11+) or the tomli backport; run with
this repo's .venv (Python 3.14, tomllib built in):

Run: .venv/bin/python3 tools/test_recomp_config.py
"""
import os
import sys
import tempfile
import textwrap
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from recomp_config import RecompConfigError, load_recomp_config, load_switch_tables


def test_load_minimal(tmp_path: Path) -> None:
    p = tmp_path / "r.toml"
    p.write_text(textwrap.dedent("""
        [main]
        elf_path = "EBOOT.ELF"
        functions_json = "functions.json"
        out_directory = "recomp_macos_v2"
        switch_table_file = "config/gow2_switch_tables.toml"

        [optimizations]
        skip_lr = false
        cr_as_local = false
    """), encoding="utf-8")
    cfg = load_recomp_config(p)
    assert cfg.main.elf_path.endswith("EBOOT.ELF"), cfg.main.elf_path
    assert cfg.main.out_directory.endswith("recomp_macos_v2"), cfg.main.out_directory
    assert cfg.optimizations.skip_lr is False
    assert cfg.midasm_hooks == []
    assert cfg.functions_override == []
    assert cfg.invalid_instructions == []


def test_midasm_hook_fields(tmp_path: Path) -> None:
    p = tmp_path / "r.toml"
    p.write_text(textwrap.dedent("""
        [main]
        elf_path = "EBOOT.ELF"
        functions_json = "functions.json"
        out_directory = "out"

        [[midasm_hook]]
        name = "Ce03cWaitIdle"
        address = 0x000CE03C
        registers = ["r2", "r30"]
        after_instruction = false
    """), encoding="utf-8")
    cfg = load_recomp_config(p)
    assert len(cfg.midasm_hooks) == 1
    h = cfg.midasm_hooks[0]
    assert h.name == "Ce03cWaitIdle"
    assert h.address == 0xCE03C
    assert h.registers == ["r2", "r30"]
    assert h.after_instruction is False


def test_functions_override_and_invalid_instructions_parsed(tmp_path: Path) -> None:
    p = tmp_path / "r.toml"
    p.write_text(textwrap.dedent("""
        [main]
        elf_path = "EBOOT.ELF"
        functions_json = "functions.json"
        out_directory = "out"

        [[functions_override]]
        address = 0x100
        size = 64

        [[invalid_instructions]]
        data = 0xDEADBEEF
        size = 4
    """), encoding="utf-8")
    cfg = load_recomp_config(p)
    assert len(cfg.functions_override) == 1
    assert cfg.functions_override[0].address == 0x100
    assert cfg.functions_override[0].size == 64
    assert len(cfg.invalid_instructions) == 1
    assert cfg.invalid_instructions[0].data == 0xDEADBEEF
    assert cfg.invalid_instructions[0].size == 4


def test_emit_weak_wrappers_defaults_to_false(tmp_path: Path) -> None:
    """Fase 19 (XEN-04): `[main].emit_weak_wrappers` liga a emissao dos
    wrappers fracos. A ausencia da chave TEM de dar False -- e' o que garante
    que um `gow2_recomp.toml` ja existente nao passa a emitir wrappers so' por
    o parser ter aprendido a chave (o golden por diff em
    tools/test_weak_override_symbols.py mede a outra ponta disto)."""
    p = tmp_path / "weak_default.toml"
    p.write_text(textwrap.dedent("""
        [main]
        elf_path = "EBOOT.ELF"
        functions_json = "functions.json"
        out_directory = "out"
    """), encoding="utf-8")
    cfg = load_recomp_config(p)
    assert cfg.main.emit_weak_wrappers is False, cfg.main.emit_weak_wrappers


def test_emit_weak_wrappers_true_is_parsed(tmp_path: Path) -> None:
    p = tmp_path / "weak_on.toml"
    p.write_text(textwrap.dedent("""
        [main]
        elf_path = "EBOOT.ELF"
        functions_json = "functions.json"
        out_directory = "out"
        emit_weak_wrappers = true

        [[functions_override]]
        address = 0x2B4274
        size = 0x40
    """), encoding="utf-8")
    cfg = load_recomp_config(p)
    assert cfg.main.emit_weak_wrappers is True, cfg.main.emit_weak_wrappers
    assert [o.address for o in cfg.functions_override] == [0x2B4274]


def test_missing_main_section_raises(tmp_path: Path) -> None:
    p = tmp_path / "bad.toml"
    p.write_text("not_main = true\n", encoding="utf-8")
    try:
        load_recomp_config(p)
    except RecompConfigError:
        pass
    else:
        raise AssertionError("expected RecompConfigError for missing [main]")


def test_midasm_hook_return_and_jump_address_mutually_exclusive(tmp_path: Path) -> None:
    p = tmp_path / "conflict.toml"
    p.write_text(textwrap.dedent("""
        [main]
        elf_path = "EBOOT.ELF"
        functions_json = "functions.json"
        out_directory = "out"

        [[midasm_hook]]
        name = "Bad"
        address = 0x100
        return = true
        jump_address = 0x200
    """), encoding="utf-8")
    try:
        load_recomp_config(p)
    except RecompConfigError:
        pass
    else:
        raise AssertionError("expected RecompConfigError for return+jump_address")


def test_invalid_toml_syntax_raises(tmp_path: Path) -> None:
    p = tmp_path / "broken.toml"
    p.write_text("[main\nelf_path = \n", encoding="utf-8")
    try:
        load_recomp_config(p)
    except RecompConfigError:
        pass
    else:
        raise AssertionError("expected RecompConfigError for malformed TOML")


def test_load_switch_tables_dict_case_form_ignores_index(tmp_path: Path) -> None:
    """Task B2: the plan's proposed case shape is {index, target}; `index` is
    accepted (schema compatibility) but must have NO effect on the parsed
    order/content -- only `target` does. ppu_lifter.py keys switch arms by
    target address, never by index (see the SwitchTable docstring)."""
    p = tmp_path / "switch.toml"
    p.write_text(textwrap.dedent("""
        [[switch_table]]
        dispatch = 0x00ABCDEF
        default  = "indirect"
        cases = [
          { index = 5, target = 0x00AB0200 },
          { index = 0, target = 0x00AB0100 },
        ]
    """), encoding="utf-8")
    tables = load_switch_tables(p)
    assert len(tables) == 1
    t = tables[0]
    assert t.dispatch == 0x00ABCDEF
    assert t.default == "indirect"
    # Order and content come straight from `cases` as written (index=5 first,
    # index=0 second) -- `index` demonstrably does not reorder/dedupe/filter.
    assert t.cases == [0x00AB0200, 0x00AB0100], t.cases


def test_load_switch_tables_bare_int_case_form(tmp_path: Path) -> None:
    """A case may also be a bare int (no {index, target} wrapping needed) --
    the simpler of the two accepted shapes per the B2 brief."""
    p = tmp_path / "switch.toml"
    p.write_text(textwrap.dedent("""
        [[switch_table]]
        dispatch = 0x100
        cases = [0x200, 0x300]
    """), encoding="utf-8")
    tables = load_switch_tables(p)
    assert len(tables) == 1
    assert tables[0].cases == [0x200, 0x300]
    assert tables[0].default == "indirect", "default must default to indirect when omitted"


def test_load_switch_tables_mixed_case_forms(tmp_path: Path) -> None:
    """Both shapes can coexist in the same `cases` array."""
    p = tmp_path / "switch.toml"
    p.write_text(textwrap.dedent("""
        [[switch_table]]
        dispatch = 0x100
        cases = [0x200, { target = 0x300 }, { index = 2, target = 0x400 }]
    """), encoding="utf-8")
    tables = load_switch_tables(p)
    assert tables[0].cases == [0x200, 0x300, 0x400]


def test_load_switch_tables_default_other_than_indirect_raises(tmp_path: Path) -> None:
    """Armadilha 3 (brief): `default` values other than "indirect" (e.g. an
    explicit fallthrough EA) have no representation in dict[int, list[int]]
    and must be rejected at parse time with a readable error, not silently
    accepted and then ignored downstream."""
    p = tmp_path / "switch.toml"
    p.write_text(textwrap.dedent("""
        [[switch_table]]
        dispatch = 0x100
        default = "0x00AB0000"
        cases = [0x200]
    """), encoding="utf-8")
    try:
        load_switch_tables(p)
    except RecompConfigError as exc:
        assert "indirect" in str(exc)
    else:
        raise AssertionError("expected RecompConfigError for default != 'indirect'")


def test_load_switch_tables_missing_dispatch_raises(tmp_path: Path) -> None:
    p = tmp_path / "switch.toml"
    p.write_text(textwrap.dedent("""
        [[switch_table]]
        cases = [0x200]
    """), encoding="utf-8")
    try:
        load_switch_tables(p)
    except RecompConfigError:
        pass
    else:
        raise AssertionError("expected RecompConfigError for missing dispatch")


def test_load_switch_tables_case_missing_target_raises(tmp_path: Path) -> None:
    p = tmp_path / "switch.toml"
    p.write_text(textwrap.dedent("""
        [[switch_table]]
        dispatch = 0x100
        cases = [{ index = 0 }]
    """), encoding="utf-8")
    try:
        load_switch_tables(p)
    except RecompConfigError:
        pass
    else:
        raise AssertionError("expected RecompConfigError for a case missing 'target'")


def test_load_switch_tables_missing_file_raises(tmp_path: Path) -> None:
    try:
        load_switch_tables(tmp_path / "nope.toml")
    except RecompConfigError:
        pass
    else:
        raise AssertionError("expected RecompConfigError for a missing switch table file")


def test_load_switch_tables_empty_section_is_empty_list(tmp_path: Path) -> None:
    """No [[switch_table]] entries at all (e.g. a starter file with only
    comments) parses to an empty list, not an error -- the plan explicitly
    allows starting the file empty."""
    p = tmp_path / "switch.toml"
    p.write_text("# no tables declared yet\n", encoding="utf-8")
    assert load_switch_tables(p) == []


def main() -> int:
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        test_load_minimal(tmp)
        test_midasm_hook_fields(tmp)
        test_functions_override_and_invalid_instructions_parsed(tmp)
        test_emit_weak_wrappers_defaults_to_false(tmp)
        test_emit_weak_wrappers_true_is_parsed(tmp)
        test_missing_main_section_raises(tmp)
        test_midasm_hook_return_and_jump_address_mutually_exclusive(tmp)
        test_invalid_toml_syntax_raises(tmp)
        test_load_switch_tables_dict_case_form_ignores_index(tmp)
        test_load_switch_tables_bare_int_case_form(tmp)
        test_load_switch_tables_mixed_case_forms(tmp)
        test_load_switch_tables_default_other_than_indirect_raises(tmp)
        test_load_switch_tables_missing_dispatch_raises(tmp)
        test_load_switch_tables_case_missing_target_raises(tmp)
        test_load_switch_tables_missing_file_raises(tmp)
        test_load_switch_tables_empty_section_is_empty_list(tmp)
    print("ok: recomp_config TOML parser (main/optimizations/midasm_hook/overrides + "
          "emit_weak_wrappers default-false + validation + Task B2 "
          "switch_table schema/loader)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
