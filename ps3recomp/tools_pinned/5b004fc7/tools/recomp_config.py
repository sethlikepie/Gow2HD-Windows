"""recomp_config.py -- declarative TOML config for the PPU recompiler (Xenon-style).

Parses a per-title TOML file (e.g. gow2-recomp/config/gow2_recomp.toml) into a
RecompConfig used by tools/ppu_lifter.py to drive mid-asm hooks, function
overrides, invalid-instruction ranges and locals optimizations -- instead of
hand-editing the lifter or patch_*.py per game.

Self-test: python tools/test_recomp_config.py (needs tomllib/tomli)
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

try:
    import tomllib  # Python 3.11+
except ModuleNotFoundError:  # pragma: no cover - exercised on <3.11
    import tomli as tomllib  # type: ignore[no-redef]


class RecompConfigError(Exception):
    """Raised for a missing, malformed or invalid recomp TOML config."""


@dataclass
class MidasmHook:
    name: str
    address: int
    registers: list[str] = field(default_factory=list)
    after_instruction: bool = False
    returns: bool = False
    jump_address: int | None = None


@dataclass
class OptFlags:
    skip_lr: bool = False
    skip_msr: bool = False
    ctr_as_local: bool = False
    xer_as_local: bool = False
    reserved_as_local: bool = False
    cr_as_local: bool = False
    non_argument_as_local: bool = False
    non_volatile_as_local: bool = False


@dataclass
class FunctionOverride:
    """Uma funcao guest a substituir INTEIRA por codigo host (Fase 19, XEN-04).

    `address` e' o unico campo consumido: tem de ser o ENDERECO DE INICIO de
    uma funcao liftada (um EA no interior, ou fora de qualquer funcao, e'
    recusado com rc=2 por ppu_lifter.py -- um override declarado que nao
    produzisse wrapper nenhum seria um no-op silencioso).

    `size` e' aceite pelo schema (a Task A1 congelou-o) e REGISTADO, mas nao
    e' consumido: quem decide os limites do corpo envolvido e' o proprio lift
    (`func.start_addr`/`func.end_addr`), que ja pode ter partido a funcao em
    fragmentos. Validar `size` contra esses limites daria falsos negativos em
    exactamente as funcoes que mais interessam. Documentado como nao-consumido
    em docs/RECOMP_CONFIG.md, para nao ser uma surpresa "declarei e nao fez
    nada" -- a classe de erro que este loader recusa noutros sitios.
    """
    address: int
    size: int


@dataclass
class InvalidInstruction:
    data: int
    size: int


@dataclass
class SwitchTable:
    """One config-declared `mtctr rX; bctr` dispatcher (Task B2 -- the network
    for when `ppu_lifter.discover_jump_tables`'s static analysis doesn't reach
    a switch). Loaded from the SEPARATE file `[main].switch_table_file`
    points at (not inline in the main recomp TOML, mirroring how
    `functions_json` is its own file) via `load_switch_tables()` below.

    `dispatch` is the address of the `bctr` instruction itself -- NOT the
    preceding `mtctr` -- because that is what ppu_lifter.py's `_translate`
    keys `self.jump_tables` by (`self.jump_tables.get(insn.addr)` where
    `insn` is the `bctr`, ppu_lifter.py:1534).

    `cases` is already flattened to plain target addresses: the plan's
    proposed `{index, target}` case shape (see `_parse_switch_case`) carries
    an `index` that ppu_lifter.py's emission never uses -- it keys switch
    arms by the target ADDRESS, not position (`case 0x{c:08X}u: goto
    loc_{c:08X};`, ppu_lifter.py:1536-1538) -- so `index` is accepted for
    schema compatibility with the plan and then dropped; only `target`
    affects anything.

    `default` is kept only as a round-trippable record of what was declared;
    today it must be the literal string "indirect" (the only value
    `load_switch_tables` accepts -- see `_parse_switch_table`) because that
    is already unconditionally what ppu_lifter.py's bctr codegen falls back
    to (`default: ps3_indirect_call(ctx); return;`, ppu_lifter.py:1541) --
    there is no internal representation for anything else (the plan's other
    suggested value, an explicit fallthrough EA, has no slot in
    `dict[int, list[int]]`) without a codegen change, which is out of scope
    here.
    """
    dispatch: int
    cases: list[int] = field(default_factory=list)
    default: str = "indirect"


@dataclass
class MainConfig:
    elf_path: str
    functions_json: str
    out_directory: str
    switch_table_file: str | None = None
    # Fase 19 (XEN-04): liga a emissao dos wrappers fracos para as funcoes
    # declaradas em [[functions_override]]. Equivalente ao `--emit-weak-wrappers`
    # do ppu_lifter.py (qualquer um dos dois liga; nenhum liga por omissao).
    # DEFAULT FALSE, e tem de continuar assim: com isto a false o output do
    # lifter e' byte-a-byte o de sempre (golden por diff em
    # tools/test_weak_override_symbols.py).
    emit_weak_wrappers: bool = False


@dataclass
class RecompConfig:
    main: MainConfig
    midasm_hooks: list[MidasmHook] = field(default_factory=list)
    functions_override: list[FunctionOverride] = field(default_factory=list)
    invalid_instructions: list[InvalidInstruction] = field(default_factory=list)
    optimizations: OptFlags = field(default_factory=OptFlags)


def _resolve(base_dir: Path, value: str | None) -> str | None:
    if value is None:
        return None
    p = Path(value)
    return str(p) if p.is_absolute() else str((base_dir / p).resolve())


def _parse_main(base_dir: Path, raw: dict[str, Any], toml_path: Path) -> MainConfig:
    try:
        elf_path = raw["elf_path"]
        functions_json = raw["functions_json"]
        out_directory = raw["out_directory"]
    except KeyError as exc:
        raise RecompConfigError(f"{toml_path}: [main] missing required key {exc}") from exc
    return MainConfig(
        elf_path=_resolve(base_dir, elf_path),
        functions_json=_resolve(base_dir, functions_json),
        out_directory=_resolve(base_dir, out_directory),
        switch_table_file=_resolve(base_dir, raw.get("switch_table_file")),
        emit_weak_wrappers=bool(raw.get("emit_weak_wrappers", False)),
    )


def _parse_midasm_hook(raw: dict[str, Any], toml_path: Path, index: int) -> MidasmHook:
    try:
        name = raw["name"]
        address = raw["address"]
    except KeyError as exc:
        raise RecompConfigError(
            f"{toml_path}: [[midasm_hook]] #{index} missing required key {exc}"
        ) from exc

    returns = bool(raw.get("return", False))
    jump_address = raw.get("jump_address")
    if returns and jump_address is not None:
        raise RecompConfigError(
            f"{toml_path}: [[midasm_hook]] '{name}' cannot set both "
            "return=true and jump_address (mutually exclusive, mirrors XenonRecomp)"
        )

    return MidasmHook(
        name=name,
        address=int(address),
        registers=list(raw.get("registers", [])),
        after_instruction=bool(raw.get("after_instruction", False)),
        returns=returns,
        jump_address=int(jump_address) if jump_address is not None else None,
    )


def load_recomp_config(path: str | Path) -> RecompConfig:
    """Load and validate a recomp TOML config. Raises RecompConfigError on any problem."""
    toml_path = Path(path)
    if not toml_path.is_file():
        raise RecompConfigError(f"{toml_path}: recomp config file not found")

    try:
        with toml_path.open("rb") as fh:
            raw = tomllib.load(fh)
    except tomllib.TOMLDecodeError as exc:
        raise RecompConfigError(f"{toml_path}: invalid TOML -- {exc}") from exc

    if "main" not in raw:
        raise RecompConfigError(f"{toml_path}: missing required [main] section")

    base_dir = toml_path.resolve().parent
    main = _parse_main(base_dir, raw["main"], toml_path)

    hooks = [
        _parse_midasm_hook(h, toml_path, i)
        for i, h in enumerate(raw.get("midasm_hook", []))
    ]
    overrides = [
        FunctionOverride(address=int(o["address"]), size=int(o["size"]))
        for o in raw.get("functions_override", [])
    ]
    invalid = [
        InvalidInstruction(data=int(i["data"]), size=int(i["size"]))
        for i in raw.get("invalid_instructions", [])
    ]
    optimizations = OptFlags(**raw.get("optimizations", {}))

    return RecompConfig(
        main=main,
        midasm_hooks=hooks,
        functions_override=overrides,
        invalid_instructions=invalid,
        optimizations=optimizations,
    )


def _parse_switch_case(raw: Any, table_path: Path, table_index: int, case_index: int) -> int:
    """A case entry is either a bare int (a target address) or the plan's
    `{index:int, target:int}` table. `index`, if present, is READ (so a
    missing/malformed `target` next to a present `index` still errors
    correctly) but otherwise silently discarded -- see the `SwitchTable`
    docstring for why: ppu_lifter.py's emission is address-keyed, not
    index-keyed."""
    if isinstance(raw, dict):
        try:
            return int(raw["target"])
        except KeyError as exc:
            raise RecompConfigError(
                f"{table_path}: [[switch_table]] #{table_index} case #{case_index} "
                f"missing required key {exc}"
            ) from exc
        except (TypeError, ValueError) as exc:
            raise RecompConfigError(
                f"{table_path}: [[switch_table]] #{table_index} case #{case_index} "
                f"has a non-integer target: {raw.get('target')!r}"
            ) from exc
    try:
        return int(raw)
    except (TypeError, ValueError) as exc:
        raise RecompConfigError(
            f"{table_path}: [[switch_table]] #{table_index} case #{case_index} "
            f"is neither an int nor a {{target = ...}} table: {raw!r}"
        ) from exc


def _parse_switch_table(raw: dict[str, Any], table_path: Path, index: int) -> SwitchTable:
    try:
        dispatch = int(raw["dispatch"])
    except KeyError as exc:
        raise RecompConfigError(
            f"{table_path}: [[switch_table]] #{index} missing required key {exc}"
        ) from exc
    except (TypeError, ValueError) as exc:
        raise RecompConfigError(
            f"{table_path}: [[switch_table]] #{index} has a non-integer dispatch: "
            f"{raw.get('dispatch')!r}"
        ) from exc

    # `default`: only "indirect" is representable today (see the SwitchTable
    # docstring). Reject anything else at parse time with a message that
    # explains WHY, rather than silently ignoring a value the author
    # presumably meant to change behaviour -- the whole point of this task is
    # to stop this class of "declared but had no effect" surprise.
    default = raw.get("default", "indirect")
    if default != "indirect":
        raise RecompConfigError(
            f"{table_path}: [[switch_table]] #{index} "
            f"(dispatch=0x{dispatch & 0xFFFFFFFF:08X}) has default={default!r}; "
            f"only the literal string \"indirect\" is accepted. "
            f"ppu_lifter.py's bctr codegen ALREADY falls back to "
            f"ps3_indirect_call(ctx) unconditionally when ctr matches no "
            f"declared case, so \"indirect\" merely documents existing "
            f"behaviour and is a no-op. An explicit fallthrough EA (the "
            f"plan's other suggested default) has no slot in the internal "
            f"dict[int, list[int]] representation and would require a "
            f"codegen change -- out of scope for this loader; fix the TOML "
            f"instead of relying on this being silently accepted."
        )

    cases_raw = raw.get("cases", [])
    cases = [_parse_switch_case(c, table_path, index, i) for i, c in enumerate(cases_raw)]
    return SwitchTable(dispatch=dispatch, cases=cases, default=default)


def load_switch_tables(path: str | Path) -> list[SwitchTable]:
    """Load a `[[switch_table]]` TOML file -- the SEPARATE file that
    `RecompConfig.main.switch_table_file` points at (already resolved to an
    absolute path by `_parse_main` above), not a section inline in the main
    recomp TOML. Mirrors the plan's proposed format:

        [[switch_table]]
        dispatch = 0x00ABCDEF
        default  = "indirect"
        cases = [
          { index = 0, target = 0x00AB0100 },
          { index = 1, target = 0x00AB0200 },
        ]

    Raises RecompConfigError on a missing file, malformed TOML, or a
    `[[switch_table]]` entry that doesn't fit the schema. Callers are
    responsible for merging the result with `ppu_lifter.discover_jump_tables`'s
    output (config wins on conflict) -- this function only parses.
    """
    table_path = Path(path)
    if not table_path.is_file():
        raise RecompConfigError(f"{table_path}: switch table file not found")

    try:
        with table_path.open("rb") as fh:
            raw = tomllib.load(fh)
    except tomllib.TOMLDecodeError as exc:
        raise RecompConfigError(f"{table_path}: invalid TOML -- {exc}") from exc

    return [
        _parse_switch_table(t, table_path, i)
        for i, t in enumerate(raw.get("switch_table", []))
    ]
