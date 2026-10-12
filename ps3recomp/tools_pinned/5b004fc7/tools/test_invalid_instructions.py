#!/usr/bin/env python3
"""Self-check for the invalid_instructions consumer (Task B3).

Plano: docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md:415-434
(licao importada do XenonRecomp). A Task A1 ja fez `tools/recomp_config.py`
parsear `[[invalid_instructions]]` (data:int + size:int) para
`RecompConfig.invalid_instructions`; esta suite cobre o CONSUMIDOR que faltava:
`ppu_disasm.disassemble_bytes` a saltar o padrao, e `ppu_lifter.PPULifter`
a emitir um comentario limpo em vez de `/* TODO: ... */` para esse salto.

IMPORTANTE (a armadilha do brief): `InvalidInstruction` e' um PADRAO DE PALAVRA
(data: a palavra crua de 32 bits) + um tamanho de salto (size: bytes), NAO um
intervalo de enderecos -- a mesma palavra salta onde quer que reapareca no
bloco.

Revisao round 1 (pos-aprovacao) apontou 4 Important, todos da mesma familia
("config malformado passa em silencio") + 3 Minor. Cobertos aqui:
  1. `size <= 0` tinha de deixar de ser indistinguivel de um `size=4` real no
     comentario emitido -- ver test_non_positive_declared_size_forces_4_and_says_so_in_the_comment.
  2. `data` fora de [0, 2**32) (negativo ou >=2**32) tinha de ser mascarado a
     32 bits ao carregar o config, senao a entrada nunca casa e o CLI diz
     "N pattern(s) loaded" sem fazer nada -- coberto end-to-end em
     test_cli_wiring_raw_path_via_config_flag (usa -0x21524111, que mascara
     para 0xDEADBEEF).
  3. As decisoes documentadas (size nao-multiplo-de-4 honrado tal como
     declarado; cauda de 1-3 bytes) nao tinham teste que as fixasse -- ver
     test_declared_size_not_multiple_of_4_is_honoured_verbatim e
     test_tail_bytes_still_undecoded_when_config_is_active.
  4. Nada exercitava o WIRING (main() --config -> dict; _worker_init/
     _worker_lift; o tuplo posicional de 11 itens em _parallel_lift) -- ver
     test_worker_init_and_worker_lift_thread_invalid_instructions (chamada
     directa + _WORKER_STATE) e test_parallel_lift_real_pool_threads_invalid_instructions
     (Pool real de 1 processo, exercita o tuplo initargs literal).

Revisao round 2 apanhou um gap dentro do proprio item 4: a suite so' tinha UM
teste de subprocess do CLI e corria sempre com `--raw`, portanto NUNCA entrava
no ramo ELF de `main()` (`if args.raw: ... else: ...`, tools/ppu_lifter.py
~3707) -- os call sites `:3721` (loop primario sobre PT_LOAD executavel),
`:3729` (fallback quando o loop primario nao encontra nada) e `:3736`
(fallback de excepcao quando o parse do ELF rebenta) ficavam sem nenhum teste
que morresse ao apagar `invalid_instructions=` deles. PIOR: a docstring do
teste antigo afirmava cobrir "qualquer" call site de main(), o que era falso
-- um teste que declara mais cobertura do que tem e' o mesmo defeito que esta
sessao passou o dia a corrigir noutros sitios (ver rounds 1). Fechado com um
fixture ELF64 minimo construido em codigo puro (struct.pack, sem dependencias
novas -- ver _build_minimal_elf64/_run_lifter_cli) e quatro testes de
subprocess, um por call site de disassemble_bytes dentro de main():
  - test_cli_wiring_raw_path_via_config_flag       -> :3708 (--raw)
  - test_cli_wiring_elf_primary_call_site          -> :3721 (PT_LOAD X)
  - test_cli_wiring_elf_fallback_call_site         -> :3729 (sem PT_LOAD X)
  - test_cli_wiring_elf_exception_fallback_call_site -> :3736 (ELF invalido)
Juntos com as duas guardas do caminho worker/paralelo do item 4 original,
TODOS os cinco call sites REAIS de disassemble_bytes em tools/ppu_lifter.py
(4 em main() + _worker_lift -- _parallel_lift NAO chama disassemble_bytes
directamente, despacha para _worker_lift via pool.imap_unordered, portanto
e' o MESMO call site exercitado por dois mecanismos de invocacao diferentes,
nao um sexto -- correcao round 3, era uma contagem errada no roll-up, nao
uma lacuna de cobertura) tem agora pelo menos um teste dedicado que morre se
`invalid_instructions=` for apagado dele (5 call sites, 6 guardas: :3531 tem
DUAS, uma por chamada directa a _worker_init/_worker_lift e outra pelo Pool
real de _parallel_lift, porque testam falhas distintas e legitimas -- o
wiring directo e o tuplo initargs do Pool) -- mas cada docstring de teste
individual so' afirma o que ESSE teste prova, nunca "qualquer call site",
para nao repetir o erro apanhado no round 2.

Sem pytest (segue a convencao tools/test_*.py deste repo -- so' stdlib, asserts
simples + main()). Corre com o `.venv` deste repo:

Run: .venv/bin/python3 tools/test_invalid_instructions.py
"""
import json
import os
import struct
import subprocess
import sys
import tempfile
import textwrap
from pathlib import Path

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)

import ppu_disasm
import ppu_lifter as PL
from ppu_disasm import INVALID_SKIP_MNEMONIC, disassemble_bytes
from ppu_lifter import LiftedFunction, PPULifter

FAILS: list[str] = []


def check(label: str, cond: bool, detail: str = "") -> None:
    if cond:
        print(f"[invalid-insn] ok   {label}")
    else:
        print(f"[invalid-insn] FAIL {label}  {detail}")
        FAILS.append(label)


def w(*words: int) -> bytes:
    """Pack big-endian 32-bit words into bytes (PPU is always BE here)."""
    return b"".join(struct.pack(">I", x & 0xFFFFFFFF) for x in words)


# A real instruction word so the "no config" / "no match" baseline decodes to
# something other than the unknown ".word" fallback: `li r3, 1`
# (opcd=14, rd=3, ra=0, SIMM=1).
LI_R3_1 = (14 << 26) | (3 << 21) | (0 << 16) | 1
NOP = (24 << 26)  # ori r0, r0, 0 == canonical nop encoding
BLR = 0x4E800020  # bclr 20,0 unconditional return


def _build_minimal_elf64(code: bytes, vaddr: int, exec_flag: bool = True) -> bytes:
    """The smallest ELF64 tools/elf_parser.py's ELFFile.load() will accept:
    a 64-byte Ehdr, ONE 56-byte Phdr (PT_LOAD), then the raw code bytes --
    no section headers (e_shoff=0/e_shnum=0, which _parse_section_headers
    short-circuits on), no PRX module info (e_type=ET_EXEC, not
    ET_SCE_PPURELEXEC), no relocations (no PT_SCE_PPURELA). Not a realistic
    PPC executable -- just enough structure for main()'s ELF branch
    (tools/ppu_lifter.py, `if args.raw: ... else: ...`) to reach its
    disassemble_bytes call sites (review round 2, closing the gap on the
    only branch the CLI-subprocess tests never took).

    exec_flag=True sets p_flags=PF_R|PF_X (5) so `ph.p_flags & 1` is true --
    the PRIMARY loop's condition (main() ~:3719). exec_flag=False sets
    p_flags=PF_R only (4), so the primary loop finds nothing and main()'s
    `if not all_insns:` FALLBACK loop (~:3726, `ph.p_filesz > 0` only) is
    what actually picks up the segment.
    """
    EHDR_SIZE = 64
    PHDR_SIZE = 56
    e_ident = bytes([0x7F, 0x45, 0x4C, 0x46,  # \x7fELF
                     2,                        # EI_CLASS = ELFCLASS64
                     2,                        # EI_DATA = ELFDATA2MSB (BE)
                     1, 0]) + b"\x00" * 8       # EI_VERSION=1, pad
    e_phoff = EHDR_SIZE
    ehdr = e_ident + struct.pack(
        ">HHIQQQIHHHHHH",
        2,          # e_type = ET_EXEC (NOT ET_SCE_PPURELEXEC -- skip PRX parsing)
        21,         # e_machine = EM_PPC64
        1,          # e_version
        0,          # e_entry
        e_phoff,    # e_phoff
        0,          # e_shoff = 0 -> no section headers
        0,          # e_flags
        EHDR_SIZE,  # e_ehsize
        PHDR_SIZE,  # e_phentsize
        1,          # e_phnum = 1 (a single PT_LOAD)
        0,          # e_shentsize
        0,          # e_shnum
        0,          # e_shstrndx
    )
    assert len(ehdr) == EHDR_SIZE, len(ehdr)
    p_offset = EHDR_SIZE + PHDR_SIZE
    p_flags = 5 if exec_flag else 4   # PF_R|PF_X vs PF_R-only
    phdr = struct.pack(
        ">IIQQQQQQ",
        1,          # p_type = PT_LOAD
        p_flags,
        p_offset,   # p_offset
        vaddr,      # p_vaddr
        vaddr,      # p_paddr
        len(code),  # p_filesz
        len(code),  # p_memsz
        4,          # p_align
    )
    assert len(phdr) == PHDR_SIZE, len(phdr)
    return ehdr + phdr + code


def _run_lifter_cli(input_bytes: bytes, config_toml: str,
                     functions_bounds: list[tuple[int, int]],
                     extra_args: tuple[str, ...] = ()
                     ) -> tuple["subprocess.CompletedProcess[str]", str | None]:
    """Write input_bytes/functions.json/cfg.toml to a fresh tmpdir and run
    the REAL `ppu_lifter.py` CLI against them. Shared by every subprocess-
    based wiring guard below (one per main()-level disassemble_bytes call
    site). --allow-jt-failure is always passed: a minimal/synthetic fixture
    (raw bytes OR a degenerate ELF with no real TOC/jump-table shape) is
    exactly the case that flag exists for -- jump-table discovery failing on
    it is expected and irrelevant to what these tests check; without the
    flag main() would sys.exit(3) on that unrelated failure by default and
    every test here would falsely read as broken. --jobs 1 keeps the run
    serial and deterministic (the worker/parallel path already has its own
    dedicated tests above).

    Returns (CompletedProcess, generated_source_text_or_None).
    """
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        input_path = tmp / "input.bin"
        input_path.write_bytes(input_bytes)

        functions_path = tmp / "functions.json"
        functions_path.write_text(json.dumps(
            [{"start": s, "end": e} for s, e in functions_bounds]))

        config_path = tmp / "cfg.toml"
        config_path.write_text(config_toml, encoding="utf-8")

        out_dir = tmp / "out"
        proc = subprocess.run(
            [sys.executable, os.path.join(TOOLS_DIR, "ppu_lifter.py"),
             str(input_path), "--config", str(config_path),
             "--functions", str(functions_path), "--single-file",
             "--output", str(out_dir), "--jobs", "1", "--allow-jt-failure",
             *extra_args],
            capture_output=True, text=True, timeout=60)

        src_path = out_dir / "ppu_recomp.c"
        src = src_path.read_text() if src_path.is_file() else None
        return proc, src


def test_no_config_matches_pre_existing_behaviour() -> None:
    """Without invalid_instructions, decode is byte-for-byte the old scan."""
    data = w(LI_R3_1, 0xDEADBEEF, NOP)

    baseline = []
    fmt = ">I"
    for off in range(0, len(data) - 3, 4):
        raw = struct.unpack_from(fmt, data, off)[0]
        baseline.append(ppu_disasm.decode(raw, off))

    got_default = disassemble_bytes(data, 0, True)
    got_explicit_none = disassemble_bytes(data, 0, True, invalid_instructions=None)
    got_empty_dict = disassemble_bytes(data, 0, True, invalid_instructions={})

    for label, got in (("default", got_default), ("explicit None", got_explicit_none),
                       ("empty dict", got_empty_dict)):
        check(f"no-config ({label}): same instruction count as old scan",
              len(got) == len(baseline), f"{len(got)} vs {len(baseline)}")
        check(f"no-config ({label}): 0xDEADBEEF decoded as an instruction, "
              "not skipped (no pattern declared)",
              all(i.mnemonic != INVALID_SKIP_MNEMONIC for i in got))
        mnemonics = [i.mnemonic for i in got]
        baseline_mnemonics = [i.mnemonic for i in baseline]
        check(f"no-config ({label}): mnemonic sequence matches old scan exactly",
              mnemonics == baseline_mnemonics,
              f"{mnemonics} vs {baseline_mnemonics}")


def test_declared_pattern_is_skipped_not_decoded() -> None:
    """A declared {data: 0xDEADBEEF, size: 4} pattern is NOT decoded; the scan
    advances by `size` bytes and records a skip marker instead."""
    data = w(LI_R3_1, 0xDEADBEEF, LI_R3_1)
    got = disassemble_bytes(data, 0x1000, True,
                             invalid_instructions={0xDEADBEEF: 4})

    check("3 words in -> 3 Instructions out (1 real + 1 skip-marker + 1 real)",
          len(got) == 3, f"got {len(got)}")
    if len(got) == 3:
        real0, skip, real1 = got
        check("word 0 decoded normally (li)", real0.mnemonic == "li")
        check("word 1 NOT decoded as a real instruction",
              skip.mnemonic == INVALID_SKIP_MNEMONIC, skip.mnemonic)
        check("skip marker sits at the matched word's address",
              skip.addr == 0x1004, hex(skip.addr))
        check("skip marker carries the raw matched word",
              skip.raw == 0xDEADBEEF, hex(skip.raw))
        check("skip advanced exactly `size` (4) bytes -> next word at +8",
              real1.addr == 0x1008, hex(real1.addr))
        check("word 2 decoded normally again (li) -- scan resumed after skip",
              real1.mnemonic == "li")


def test_skip_size_larger_than_one_word_advances_multiple_words() -> None:
    """size > 4: the ONE matching word signals a multi-word skip; the words it
    covers are never independently checked against the pattern or decoded --
    this is a word-pattern-triggered BYTE-COUNT skip, not a per-word filter."""
    # data word only appears ONCE; size=12 must skip over the next two
    # (otherwise-real) instruction words too.
    data = w(0xCAFEBABE, LI_R3_1, LI_R3_1, NOP)
    got = disassemble_bytes(data, 0, True, invalid_instructions={0xCAFEBABE: 12})

    check("skip(size=12) + 1 trailing real word = 2 Instructions",
          len(got) == 2, f"got {len(got)}")
    if len(got) == 2:
        skip, real = got
        check("skip marker at addr 0", skip.addr == 0, hex(skip.addr))
        check("skip comment mentions 12 bytes", "12 bytes" in skip.comment, skip.comment)
        check("cursor resumed at +12 (skipped the 2 li words in between)",
              real.addr == 12, real.addr)
        check("word at +12 decoded normally (nop)", real.mnemonic == "nop")


def test_size_overflowing_block_end_is_clamped_not_silent() -> None:
    """Boundary case (brief 2026-07-23 plan, point 1): a declared `size` that
    runs past the end of the block must not silently overflow -- the scan
    must terminate cleanly and the comment must say so, with actual numbers
    (review round 1: the clamp message must not mix ambiguous units)."""
    data = w(0xABADCAFE, LI_R3_1)  # 8 bytes total
    got = disassemble_bytes(data, 0, True, invalid_instructions={0xABADCAFE: 64})

    check("only the skip marker is produced (declared size overruns the block, "
          "so the trailing word is consumed by the clamp, not decoded)",
          len(got) == 1, f"got {len(got)}: {[ (i.mnemonic, i.addr) for i in got ]}")
    if got:
        skip = got[0]
        check("comment reports the DECLARED size (64), not the clamped one",
              "64 bytes" in skip.comment, skip.comment)
        check("comment flags the clamp explicitly (not a silent overflow)",
              "clamped" in skip.comment.lower(), skip.comment)
        check("clamp comment states how many bytes were actually available",
              "only 8 byte(s) left" in skip.comment, skip.comment)
        check("clamp comment reports the block's end as an ABSOLUTE address "
              "(0x00000008), not an ambiguous block-relative offset",
              "0x00000008" in skip.comment, skip.comment)


def test_non_positive_declared_size_forces_4_and_says_so_in_the_comment() -> None:
    """Review round 1, Important #1: a malformed config (size <= 0) must
    never stall/reverse the scan AND must never emit a comment that reads
    identically to a genuine `size=4` declaration -- the forcing must be
    visible."""
    data = w(0x11111111, LI_R3_1)
    got_zero = disassemble_bytes(data, 0, True, invalid_instructions={0x11111111: 0})
    got_neg = disassemble_bytes(data, 0, True, invalid_instructions={0x11111111: -8})
    for label, got, declared in (("size=0", got_zero, 0), ("size=-8", got_neg, -8)):
        check(f"{label}: scan still terminates and makes forward progress "
              "(2 instructions: skip marker + trailing li)",
              len(got) == 2, f"got {len(got)}")
        if len(got) == 2:
            skip, real = got
            check(f"{label}: first entry is the skip marker",
                  skip.mnemonic == INVALID_SKIP_MNEMONIC)
            check(f"{label}: cursor advanced past the matched word (not stalled)",
                  real.addr > skip.addr, [i.addr for i in got])
            check(f"{label}: comment says 4 bytes were actually skipped",
                  "invalid skip 4 bytes" in skip.comment, skip.comment)
            check(f"{label}: comment EXPLICITLY flags the forcing -- must NOT "
                  "read like a genuine declared size=4",
                  f"declared {declared}, forced to 4" in skip.comment, skip.comment)


def test_declared_size_not_multiple_of_4_is_honoured_verbatim() -> None:
    """Review round 1, Important #3: size=6 must land the next instruction at
    +6 EXACTLY -- not rounded to +4 or +8. This is precisely the "fix" a
    future refactor would be tempted to apply on seeing a misaligned lift;
    the docstring's claim needs a test that would catch that regression."""
    data = struct.pack(">I", 0x99999999) + b"\x00\x00" + struct.pack(">I", LI_R3_1)
    got = disassemble_bytes(data, 0, True, invalid_instructions={0x99999999: 6})

    check("2 instructions: skip marker (size=6) + the real word starting at +6",
          len(got) == 2, [(i.mnemonic, i.addr) for i in got])
    if len(got) == 2:
        skip, real = got
        check("skip marker at addr 0", skip.addr == 0, skip.addr)
        check("skip comment says 6 bytes (not rounded to 4 or 8)",
              "6 bytes" in skip.comment, skip.comment)
        check("next instruction decoded at +6 EXACTLY (byte-precise skip, "
              "not word-rounded)", real.addr == 6, real.addr)
        check("word at +6 decodes as the real trailing instruction (li) -- "
              "proves the bytes actually consumed were [0:6), not [0:4) or [0:8)",
              real.mnemonic == "li", real.mnemonic)


def test_tail_bytes_still_undecoded_when_config_is_active() -> None:
    """Review round 1, Important #3: the docstring's tail-byte guarantee
    ("same as before this parameter existed") is exercised specifically WITH
    a non-empty invalid_instructions table (an unrelated pattern), not only
    in the no-config baseline covered above."""
    data = w(LI_R3_1) + b"\x00\x00"  # one full word + a 2-byte tail
    got = disassemble_bytes(data, 0, True, invalid_instructions={0xDEADBEEF: 4})
    check("6-byte block with an unrelated pattern declared: only the leading "
          "full word decodes; the 2-byte tail is left alone, matching the "
          "no-config baseline byte-for-byte",
          len(got) == 1 and got[0].mnemonic == "li",
          [(i.mnemonic, i.addr) for i in got])


def test_lifter_emits_clean_comment_not_todo_spam() -> None:
    """The whole point of this task: the lifter must not spam `/* TODO: ... */`
    for declared padding/data-in-text -- it emits one clean comment instead."""
    lifter = PPULifter()
    func = LiftedFunction(name="f", start_addr=0, end_addr=0x10)

    data = w(0xDEADBEEF, LI_R3_1)
    insns = disassemble_bytes(data, 0, True, invalid_instructions={0xDEADBEEF: 4})
    skip_insn = insns[0]
    check("fixture sanity: first instruction is the skip marker",
          skip_insn.mnemonic == INVALID_SKIP_MNEMONIC)

    c_line = lifter._translate(skip_insn, func)
    check("lifter emits the plan's exact comment shape "
          "(\"/* invalid skip N bytes */\")",
          c_line.strip() == "/* invalid skip 4 bytes */;", repr(c_line))
    check("lifter does NOT emit a TODO for a declared invalid-instruction skip",
          "TODO" not in c_line, c_line)

    # Full lift_function() path: the marker instruction must reach the body
    # via the normal per-instruction translation loop, not just _translate()
    # called directly.
    lifted = lifter.lift_function(insns, 0, 0x10)
    joined = "\n".join(lifted.body_lines)
    check("lift_function()'s emitted body contains the clean comment",
          "/* invalid skip 4 bytes */" in joined, joined)
    check("lift_function()'s emitted body has no TODO spam for the skip",
          "TODO" not in joined, joined)


def test_worker_init_and_worker_lift_thread_invalid_instructions() -> None:
    """Review round 1, Important #4 (guard 1/3): the parallel path's own
    consumer (_worker_lift, fed via _worker_init/_WORKER_STATE) is exercised
    directly. Deleting invalid_instructions from _worker_init's storage, or
    from _worker_lift's disassemble_bytes call, breaks this."""
    seg_addr = 0x2000
    data = w(0xDEADBEEF, LI_R3_1, BLR)
    PL._worker_init(segs=[(seg_addr, data)], big_endian=True, name_map={},
                     prefix="", invalid_instructions={0xDEADBEEF: 4})
    check("_worker_init stores invalid_instructions in _WORKER_STATE",
          PL._WORKER_STATE.get("invalid_instructions") == {0xDEADBEEF: 4},
          PL._WORKER_STATE.get("invalid_instructions"))

    idx0, results, _ct, _bt = PL._worker_lift((0, [(seg_addr, seg_addr + 12)]))
    check("_worker_lift produced exactly one function", len(results) == 1, len(results))
    if results:
        _name, _s, _e, body, _calls, _ft = results[0]
        joined = "\n".join(body)
        check("worker path emits the clean invalid-skip comment (not decoded, not TODO)",
              "/* invalid skip 4 bytes */" in joined, joined)
        check("worker path does not TODO-spam the declared pattern",
              "TODO" not in joined, joined)


def test_parallel_lift_real_pool_threads_invalid_instructions() -> None:
    """Review round 1, Important #4 (guard 2/3, the reviewer's strongest ask):
    run the REAL _parallel_lift with a 1-process Pool. This literally
    executes _parallel_lift's 11-item positional initargs tuple; deleting
    invalid_instructions from that tuple, or desyncing it from
    _worker_init's parameter order, breaks this test -- unlike calling
    _worker_init directly (previous test), which can't catch a tuple/
    signature desync since it bypasses _parallel_lift's own source."""
    data = w(0xDEADBEEF, LI_R3_1, BLR)
    lifter = PPULifter()
    lifter.invalid_instructions = {0xDEADBEEF: 4}
    PL._parallel_lift(lifter, [(0, 12)], [(0, data)], True, 1)
    check("parallel lift (real Pool, jobs=1) produced exactly one function",
          len(lifter.functions) == 1, len(lifter.functions))
    if lifter.functions:
        joined = "\n".join(lifter.functions[0].body_lines)
        check("parallel path (real Pool) emits the clean invalid-skip comment",
              "/* invalid skip 4 bytes */" in joined, joined)
        check("parallel path (real Pool) does not TODO-spam the declared pattern",
              "TODO" not in joined, joined)


# Minimal [main] section every config.toml below needs (Task A1's schema
# requires it; none of these three keys are actually read by this CLI for
# --config's purposes -- see the --config --help text added in review round 1
# Minor #3). Paths are never opened, so they don't need to exist.
_DUMMY_MAIN_TOML = """
[main]
elf_path = "unused.elf"
functions_json = "unused.json"
out_directory = "unused_out"
"""


def test_cli_wiring_raw_path_via_config_flag() -> None:
    """Review round 1, Important #4 (guard 3/4, via a real subprocess) +
    regression cover for #1/#2: a real `ppu_lifter.py --raw --config ...`
    run, checking the GENERATED C for the clean comments. This is the only
    test that goes through main()'s --config loading, the 32-bit data mask,
    the size<=0 load-time warning, AND the --raw disassemble_bytes call site
    (tools/ppu_lifter.py ~:3708) all at once. It proves ONLY the --raw
    branch -- see test_cli_wiring_elf_{primary,fallback,exception_fallback}_
    call_site below for the ELF branch's three call sites (review round 2:
    the previous version of this docstring claimed to cover "any" of
    main()'s call sites, which was false, since main() has an entire
    untested ELF branch -- corrected here to say only what this test
    actually exercises)."""
    # -0x21524111 has no useful meaning on its own; it is chosen because it is
    # exactly what 0xDEADBEEF looks like re-interpreted as a NEGATIVE 32-bit
    # value promoted to Python's arbitrary-precision int (`-0x21524111 &
    # 0xFFFFFFFF == 0xDEADBEEF`) -- i.e. a TOML author who copy-pasted a
    # signed disassembler dump instead of the unsigned hex word. TOML forbids
    # a sign on `0x...` literals, so this is written as a decimal integer.
    negative_data_needing_mask = -0x21524111
    forced_pattern = 0x99999999
    raw_bytes = w(0xDEADBEEF, forced_pattern, LI_R3_1, BLR)

    config_toml = _DUMMY_MAIN_TOML + textwrap.dedent(f"""
        [[invalid_instructions]]
        data = {negative_data_needing_mask}
        size = 4

        [[invalid_instructions]]
        data = {forced_pattern}
        size = 0
    """)
    proc, src = _run_lifter_cli(
        raw_bytes, config_toml, [(0, len(raw_bytes))], extra_args=("--raw",))

    check("CLI exits 0", proc.returncode == 0,
          f"rc={proc.returncode}\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}")
    check("CLI reports 2 invalid_instructions patterns loaded",
          "2 invalid_instructions pattern(s) loaded" in proc.stdout, proc.stdout)
    check("CLI warns about the size<=0 entry at config-load time",
          "size=0" in proc.stderr and "forced" in proc.stderr, proc.stderr)
    check("generated single-file source exists", src is not None)
    if src is not None:
        # Count-based, not substring-based: with the mask bug present,
        # word 0 (0xDEADBEEF) never matches the unmasked negative key and
        # gets DECODED AS A REAL INSTRUCTION instead (observed in the red
        # run: a bogus store-update), so only word 1's skip fires -> 1
        # marker, not 2. A plain substring check for "/* invalid skip 4
        # bytes */" would falsely "pass" on word 1's marker alone (its
        # comment, under the pre-fix code, reads identically to a clean
        # size=4 match -- this IS Important #1's bug) even with the mask
        # bug still present; counting both markers is what actually pins
        # down that BOTH configured patterns matched.
        check("exactly 2 invalid-skip markers emitted (both configured "
              "patterns matched: the masked negative `data` AND the "
              "forced size<=0 word) -- a COUNT, not a substring check: "
              "a single unqualified marker text is ambiguous between "
              "'word0 matched cleanly' and 'word1's old bug produced "
              "the same text', so only the count (2 vs 1) actually "
              "proves the mask fix let word0 match at all",
              src.count("invalid skip") == 2, src)
        check("size<=0 entry's skip is forced to 4 AND flagged as such in "
              "the generated C (not indistinguishable from a real "
              "size=4) -- combined with the count above (2, not 1), the "
              "OTHER marker is therefore word0's clean, unqualified "
              "size=4 match, proving the mask fix end to end",
              "invalid skip 4 bytes (declared 0, forced to 4)" in src, src)
        check("no TODO spam for either declared pattern", "TODO" not in src, src)


def test_cli_wiring_elf_primary_call_site() -> None:
    """Review round 2 (closing the A4/Important #4 gap): a real, non-raw
    `ppu_lifter.py --config ...` run against a MINIMAL synthetic ELF64 (see
    _build_minimal_elf64) whose one PT_LOAD is executable (PF_X set) --
    exercising main()'s PRIMARY ELF loop (`if ph.p_type == PT_LOAD and
    (ph.p_flags & 1):`, ~:3719/:3721), the call site NONE of the previous
    subprocess tests reached (they all used --raw). Deleting
    invalid_instructions= from that specific call breaks this."""
    vaddr = 0x10000
    raw_bytes = w(0xDEADBEEF, LI_R3_1, BLR)
    elf_bytes = _build_minimal_elf64(raw_bytes, vaddr, exec_flag=True)
    config_toml = _DUMMY_MAIN_TOML + textwrap.dedent("""
        [[invalid_instructions]]
        data = 0xDEADBEEF
        size = 4
    """)
    proc, src = _run_lifter_cli(
        elf_bytes, config_toml, [(vaddr, vaddr + len(raw_bytes))])

    check("CLI exits 0 against the minimal executable-PT_LOAD ELF",
          proc.returncode == 0,
          f"rc={proc.returncode}\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}")
    check("generated single-file source exists", src is not None)
    if src is not None:
        check("ELF primary-loop call site emits the clean invalid-skip "
              "comment (not decoded, not TODO)",
              "/* invalid skip 4 bytes */" in src, src)
        check("no TODO spam", "TODO" not in src, src)


def test_cli_wiring_elf_fallback_call_site() -> None:
    """Review round 2: the same minimal ELF, but with PF_X CLEARED on its one
    PT_LOAD (exec_flag=False). main()'s primary loop's `ph.p_flags & 1` check
    then matches nothing, `all_insns` stays empty, and the `if not
    all_insns:` FALLBACK loop (`ph.p_filesz > 0` only, ~:3726/:3729) is what
    actually disassembles the segment -- a DIFFERENT call site than the
    primary-loop test above. Deleting invalid_instructions= from that
    specific fallback call breaks this."""
    vaddr = 0x10000
    raw_bytes = w(0xDEADBEEF, LI_R3_1, BLR)
    elf_bytes = _build_minimal_elf64(raw_bytes, vaddr, exec_flag=False)
    config_toml = _DUMMY_MAIN_TOML + textwrap.dedent("""
        [[invalid_instructions]]
        data = 0xDEADBEEF
        size = 4
    """)
    proc, src = _run_lifter_cli(
        elf_bytes, config_toml, [(vaddr, vaddr + len(raw_bytes))])

    check("CLI exits 0 against the minimal non-executable-PT_LOAD ELF",
          proc.returncode == 0,
          f"rc={proc.returncode}\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}")
    check("generated single-file source exists", src is not None)
    if src is not None:
        check("ELF fallback-loop call site emits the clean invalid-skip "
              "comment (not decoded, not TODO)",
              "/* invalid skip 4 bytes */" in src, src)
        check("no TODO spam", "TODO" not in src, src)


def test_cli_wiring_elf_exception_fallback_call_site() -> None:
    """Review round 2: pass a file that is NOT a valid ELF (no ELF magic)
    WITHOUT --raw. main()'s `from elf_parser import ELFFile; elf.load()`
    raises inside the `else:` branch's try/except, main() prints "Warning:
    ELF parse failed ... treating as raw" and falls through to the
    exception-handler's OWN disassemble_bytes call (~:3736) -- a THIRD,
    distinct call site from both ELF-loop tests above (same effective
    behaviour as --raw, but reached via a completely different source line;
    deleting invalid_instructions= from :3736 specifically would not be
    caught by the --raw test, which never executes that line). Jump-table
    discovery also fails on this input (elf.elf_header is None after the
    failed load) -- expected and irrelevant here, hence --allow-jt-failure
    in _run_lifter_cli."""
    raw_bytes = w(0xDEADBEEF, LI_R3_1, BLR)  # deliberately has NO ELF magic
    config_toml = _DUMMY_MAIN_TOML + textwrap.dedent("""
        [[invalid_instructions]]
        data = 0xDEADBEEF
        size = 4
    """)
    proc, src = _run_lifter_cli(raw_bytes, config_toml, [(0, len(raw_bytes))])

    check("CLI exits 0 despite the bogus (non-ELF) input",
          proc.returncode == 0,
          f"rc={proc.returncode}\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}")
    check("CLI reported the ELF-parse failure and fell back",
          "ELF parse failed" in proc.stderr, proc.stderr)
    check("generated single-file source exists", src is not None)
    if src is not None:
        check("exception-fallback call site emits the clean invalid-skip "
              "comment (not decoded, not TODO)",
              "/* invalid skip 4 bytes */" in src, src)
        check("no TODO spam", "TODO" not in src, src)


def main() -> int:
    test_no_config_matches_pre_existing_behaviour()
    test_declared_pattern_is_skipped_not_decoded()
    test_skip_size_larger_than_one_word_advances_multiple_words()
    test_size_overflowing_block_end_is_clamped_not_silent()
    test_non_positive_declared_size_forces_4_and_says_so_in_the_comment()
    test_declared_size_not_multiple_of_4_is_honoured_verbatim()
    test_tail_bytes_still_undecoded_when_config_is_active()
    test_lifter_emits_clean_comment_not_todo_spam()
    test_worker_init_and_worker_lift_thread_invalid_instructions()
    test_parallel_lift_real_pool_threads_invalid_instructions()
    test_cli_wiring_raw_path_via_config_flag()
    test_cli_wiring_elf_primary_call_site()
    test_cli_wiring_elf_fallback_call_site()
    test_cli_wiring_elf_exception_fallback_call_site()

    print()
    if FAILS:
        print(f"[invalid-insn] FAILED: {', '.join(FAILS)}")
        return 1
    print("ok: invalid_instructions consumer (ppu_disasm skip + ppu_lifter clean "
          "comment; all 5 disassemble_bytes call sites in tools/ppu_lifter.py "
          "covered by 6 dedicated regression guards, incl. --raw + all 3 "
          "ELF-branch sub-paths + worker/parallel wiring)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
