#!/usr/bin/env python3
"""Recorte VERBATIM da `discover_jump_tables` do upstream (sp00nznet/ps3recomp).

Existe para o teste diferencial de `tools/test_upstream_jt_contrib.py`: para se
poder afirmar "o upstream descarta este dispatcher" e' preciso correr o codigo
DELES, nao uma reconstrucao nossa do que achamos que eles fazem. O bloco entre os
sentinelas abaixo e' copiado byte a byte de `git show <SHA>:tools/ppu_lifter.py`
e nao pode ser editado, reformatado nem "limpo" -- se for, `--verify` reprova.

Proveniencia
------------
  repo    : https://github.com/sp00nznet/ps3recomp
  ramo    : upstream/fix/fold-merge-dropped-fixes
  sha     : ee97d01c16685e3b963c4bfc88053bb20831c304
  caminho : tools/ppu_lifter.py
  linhas  : 3011-3229 (1-based, inclusive)
  extraido: 2026-08-12
  sha256  : 3a903fc46e1838f31171cef8b8535cd28ea894548152bbbcb692f4a7b771a5f4 (do texto entre os sentinelas)

Licenca
-------
Codigo de terceiros vendorizado sob a licenca MIT do projecto de origem --
Copyright (c) 2026 Ned Heller / sp00nznet. E' a mesma MIT que o `LICENSE` deste
fork ja' carrega, com o mesmo detentor de copyright, pelo que a vendorizacao nao
introduz obrigacao nova alem da atribuicao acima.

Uso
---
  python3 tools/upstream_jt_snapshot.py --verify
      Reconfirma o recorte. O auto-teste offline (texto entre sentinelas vs
      UPSTREAM_FUNC_SHA256) corre SEMPRE e e' duro. A re-extraccao do git so'
      corre se o objecto existir localmente; sem o remote `upstream` a CI imprime
      uma nota e sai rc=0 -- nunca "SKIP:", que classificaria o teste como saltado.

  python3 tools/upstream_jt_snapshot.py --refresh > /tmp/s.py \
      && mv /tmp/s.py tools/upstream_jt_snapshot.py
      Re-extrai do ramo do upstream e escreve o modulo inteiro para STDOUT
      (nunca sobre si proprio). A redireccao acima e' a receita gravada.
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys

UPSTREAM_REPO = "https://github.com/sp00nznet/ps3recomp"
UPSTREAM_REF = "upstream/fix/fold-merge-dropped-fixes"
UPSTREAM_SHA = "ee97d01c16685e3b963c4bfc88053bb20831c304"
UPSTREAM_PATH = "tools/ppu_lifter.py"
UPSTREAM_LINES = (3011, 3229)
UPSTREAM_EXTRACTED = "2026-08-12"
UPSTREAM_FUNC_SHA256 = "3a903fc46e1838f31171cef8b8535cd28ea894548152bbbcb692f4a7b771a5f4"

_BEGIN = "# --- BEGIN VERBATIM UPSTREAM (nao editar) ---"
_END = "# --- END VERBATIM UPSTREAM ---"


# --- BEGIN VERBATIM UPSTREAM (nao editar) ---
def discover_jump_tables(all_insns, read_u32, toc, text_lo, text_hi):
    """Find `mtctr rX; bctr` switch dispatchers and read their jump tables.

    Handles both absolute tables (entry = case address) and base-relative
    offset tables (entry = signed offset, case = table_base + offset — the
    pattern gcc emits, e.g. `lwz r11,disp(r2); lwzx r0,idx,r11; add r0,r0,r11;
    mtctr r0; bctr`). These computed `bctr` targets are invisible to static
    branch-target discovery, so without this the switch cases never get lifted
    and the runtime indirect-call lands on an unlifted address.

    Returns {dispatcher_addr: sorted [case target addrs]}.
    """
    def mem_disp(op):
        try:
            return int(op.split('(')[0].strip(), 0)
        except ValueError:
            return None
    import os as _os
    _DBG = int(_os.environ.get("JT_DEBUG", "0"), 0)
    def _dbg(addr, msg):
        if _DBG and addr == _DBG:
            print(f"  [JT_DEBUG 0x{addr:X}] {msg}")
    tables = {}
    n = len(all_insns)
    for i in range(n):
        if all_insns[i].mnemonic != 'bctr':
            continue
        _dbg(all_insns[i].addr, "bctr found")
        win = all_insns[max(0, i - 30):i]
        # the ctr source register (last mtctr before the bctr)
        rC = None
        for w in reversed(win):
            if w.mnemonic == 'mtctr':
                rC = w.operands.strip(); break
            if w.mnemonic in ('bctr', 'bctrl', 'blr', 'bl', 'blrl'):
                break
        _dbg(all_insns[i].addr, f"rC={rC}")
        if rC is None:
            continue
        # the indexed table load
        lwzx = next((w for w in reversed(win) if w.mnemonic == 'lwzx'), None)
        _dbg(all_insns[i].addr, f"lwzx={'None' if lwzx is None else lwzx.operands}")
        if lwzx is None:
            continue
        p = [x.strip() for x in lwzx.operands.split(',')]
        if len(p) != 3:
            continue
        # `lwzx rD, rA, rB` computes MEM(rA + rB); the table-base register may be
        # EITHER operand — gcc emits both `lwzx rD, base, idx` and the swapped
        # `lwzx rD, idx, base`. Try each candidate; the real base is the one
        # loaded TOC-relative via `lwz base, disp(r2)`. (Hardcoding p[2] as the
        # base silently skipped every dispatcher with the operands swapped.)
        r_val = p[0]
        disp = None; r_base = None; base_is_ld = False
        # Prefer the lwzx operand that the `add ..., base` feeding mtctr combines
        # with the loaded offset. Both operands are often TOC-loaded, so "first
        # candidate with a TOC-load definition" can pick an unrelated index reg
        # whose own stale `lwz idx,d(r2)` reads back a valid-but-wrong base (flow's
        # app-loop state machine @0x89300 decoded from r9's global 0x10157F30
        # instead of r11's real table base 0x00089304 -> the switch fell through
        # to an unresolved bctr and unwound out of the main loop).
        _off = {r_val}
        for w in win:
            if w.mnemonic in ('extsw', 'extsb', 'extsh'):
                a = [x.strip() for x in w.operands.split(',')]
                if len(a) >= 2 and a[1] in _off:
                    _off.add(a[0])
        _cands = [p[1], p[2]]
        for w in win:
            if w.mnemonic != 'add':
                continue
            a = [x.strip() for x in w.operands.split(',')]
            if len(a) != 3:
                continue
            for _o, _b in ((a[1], a[2]), (a[2], a[1])):
                if _o in _off and _b in _cands:
                    _cands = [_b] + [c for c in _cands if c != _b]
                    break
            else:
                continue
            break
        for cand in _cands:
            # Walk backward to the NEAREST instruction that defines `cand`, and
            # accept it as the table base only if that definition is a TOC load
            # (`lwz`/`ld cand, disp(r2)`). Stopping at the first definition is
            # essential: the 30-instruction window bleeds across the function
            # boundary, and the index register (e.g. `rldic r9, r3, 2, 30`) often
            # collides with a stale `lwz r9, disp(r2)` from the PRECEDING function.
            # Blindly grabbing any matching lwz picked the index reg as the base,
            # read an unrelated table, decoded 0 targets, and silently dropped the
            # dispatcher (every dense switch in newlib dtoa fell through -> the
            # guest's printf("%f") spun forever). gcc (PSL1GHT/newlib) loads the
            # base with `ld` (64-bit ELFv1 TOC entry); SN uses `lwz`. Accept both.
            for w in reversed(win):
                a = [x.strip() for x in w.operands.split(',')]
                if not a or a[0] != cand:
                    continue                    # not a definition of cand
                if w.mnemonic in ('lwz', 'ld') and len(a) == 2 and '(r2)' in a[1]:
                    disp = mem_disp(a[1]); r_base = cand
                    base_is_ld = (w.mnemonic == 'ld')
                break                           # first definition of cand wins/loses
            if disp is not None:
                break
        _dbg(all_insns[i].addr, f"disp={disp} r_base={r_base} base_is_ld={base_is_ld} toc={toc}")
        if disp is None or not toc:
            continue
        toc_candidates = toc if isinstance(toc, (list, tuple)) else [toc]
        # offset table iff an `add rC, *, r_base` combines the loaded value + base
        is_offset = any(
            w.mnemonic == 'add' and
            [x.strip() for x in w.operands.split(',')][0] == rC and
            r_base in [x.strip() for x in w.operands.split(',')][1:]
            for w in win)
        # case count from the bound check `cmp[l]wi crN, rIdx, COUNT`. Match the
        # compare on the RAW INDEX register: the lwzx index (rIdx*4) is usually a
        # shift of the raw index (`rldic/clrlsldi rShift, rIdx, ...`), and the raw
        # index is what the switch bounds-checks. Blindly taking the nearest
        # cmpwi grabbed an unrelated `cmpwi r9,0` in a sibling basic block (the
        # 30-insn address window spans both arms of a branch), where r9 is only
        # reused as the shifted index LATER -> count=0 -> a single case decoded ->
        # LBP sub_422A40's 0x2A-case "GMTb" dispatcher fell through to an
        # unresolved indirect call (0x422CA0) and stalled the loader.
        _idx_reg = p[1] if r_base == p[2] else p[2]
        _raw_idx = _idx_reg
        for w in reversed(win):
            a = [x.strip() for x in w.operands.split(',')]
            if a and a[0] == _idx_reg:
                if (w.mnemonic in ('rldic', 'rldicl', 'rldicr', 'rlwinm',
                                   'clrlsldi', 'sldi', 'slwi', 'clrldi')
                        and len(a) >= 2):
                    _raw_idx = a[1]
                break                           # first (nearest) def of idx wins
        # Take the LARGEST immediate compared against the raw index: the switch
        # bounds-check (`cmplwi rIdx, COUNT`) uses the max index, while any
        # per-case `cmpwi rIdx, k` in the window tests a specific smaller case
        # value. Picking the nearest compare grabbed `cmpwi r7,1` (a case test)
        # -> count=1 -> only the default case decoded. Over-counting is safe: the
        # per-entry text-range validation below stops at the first bogus offset.
        count = None
        for w in reversed(win):
            if w.mnemonic in ('cmplwi', 'cmpwi'):
                a = [x.strip() for x in w.operands.split(',')]
                _cmp_reg = a[1] if (a and a[0].startswith('cr')) else (a[0] if a else None)
                if _cmp_reg == _raw_idx:
                    try:
                        _c = int(a[-1], 0)
                    except ValueError:
                        continue
                    if count is None or _c > count:
                        count = _c
        if count is None or count < 0 or count > 4096:
            count = 256
        # The cmp-derived count is a HINT, never a hard cap. It keeps UNDER-
        # counting: the backward window spans sibling basic blocks, so it can
        # latch a per-case test (`cmpwi rIdx, k`) instead of the real bounds
        # check and silently truncate the table. A truncated table drops real
        # cases, and the runtime `bctr` then lands on an unlifted mid-function
        # address -> "unresolved indirect call" -> the caller runs on garbage.
        # (LBP func_0038F380: a 31-entry offset table decoded as 19 because a
        # stray `cmpwi 18` won; case 22 = 0x0038F754 fell through to the global
        # dispatcher -- which only knows function ENTRIES, not mid-function
        # labels -- and the boot died in a storm of vcalls through a job
        # descriptor's name string.) The per-entry validation below already
        # finds the true end, so scan generously and let it terminate.
        scan = min(max(count + 1, 256), 4096)

        # Multi-TOC executables (e.g. LBP: two TOCs, ~3.6k/2.2k functions each)
        # load the table base relative to WHICHEVER r2 their function runs
        # with. We don't track per-function TOCs here, so try each candidate
        # and keep the one whose table decodes to the most in-text case
        # targets (a wrong TOC reads unrelated data and validates 0 targets).
        best = []
        for cand in toc_candidates:
            if not cand:
                continue
            if base_is_ld:
                hi = read_u32((cand + disp) & 0xFFFFFFFF)
                table_base = read_u32((cand + disp + 4) & 0xFFFFFFFF)
                if hi:              # table addresses live in the 32-bit VA space
                    table_base = None
            else:
                table_base = read_u32((cand + disp) & 0xFFFFFFFF)
            _dbg(all_insns[i].addr, f"cand_toc=0x{cand:X} table_base={None if table_base is None else hex(table_base)} count={count} is_offset={is_offset} text=[0x{text_lo:X},0x{text_hi:X})")
            if table_base is None:
                continue
            targets = []
            for k in range(scan):
                ea = (table_base + k * 4) & 0xFFFFFFFF
                # Structural end-of-table: a jump table never overlaps the code
                # it dispatches to, so once the cursor reaches the lowest case
                # target that lies AHEAD of the table, the table has ended. (The
                # gcc/SN pattern puts the table immediately before its cases:
                # LBP func_0038F380's table is 0x38F4F0..0x38F56C and case[0] IS
                # 0x38F56C.) Forward targets only -- a table whose cases branch
                # backwards would otherwise bound at k=0.
                fwd = [t for t in targets if t > table_base]
                if fwd and ea >= min(fwd):
                    _dbg(all_insns[i].addr, f"  stop at k={k}: cursor 0x{ea:X} reached first case 0x{min(fwd):X}")
                    break
                v = read_u32(ea)
                if v is None:
                    break
                if is_offset:
                    off = v - (1 << 32) if (v & 0x80000000) else v
                    t = (table_base + off) & 0xFFFFFFFF
                else:
                    t = v
                if k < 3:
                    _dbg(all_insns[i].addr, f"  entry[{k}] raw=0x{v:X} -> target=0x{t:X} valid={text_lo <= t < text_hi and t % 4 == 0}")
                if text_lo <= t < text_hi and t % 4 == 0:
                    targets.append(t)
                else:
                    break
            _dbg(all_insns[i].addr, f"decoded {len(targets)} targets")
            if len(targets) > len(best):
                best = targets
        if best:
            tables[all_insns[i].addr] = sorted(set(best))
    return tables

# --- END VERBATIM UPSTREAM ---

discover_jump_tables_upstream = discover_jump_tables


def _repo_root():
    """Raiz do repo resolvida de __file__, nunca do cwd."""
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _self_path():
    return os.path.abspath(__file__)


def _sentinel_span(text):
    """Limites do bloco verbatim, ancorados a INICIO DE LINHA.

    A procura tem de ser ancorada: o texto dos sentinelas tambem aparece na
    definicao das constantes `_BEGIN`/`_END` no topo do ficheiro, e um
    `text.index(_BEGIN)` cru casava com essa linha e devolvia um recorte que
    comecava a meio do modulo.
    """
    b = "\n" + _BEGIN + "\n"
    e = "\n" + _END + "\n"
    i = text.index(b) + len(b)
    j = text.index(e, i) + 1
    return i, j


def verbatim_source(path=None):
    """O texto entre os sentinelas -- exactamente o que foi recortado do git."""
    text = open(path or _self_path(), encoding="utf-8").read()
    i, j = _sentinel_span(text)
    cut = text[i:j]
    while cut.endswith("\n\n"):
        cut = cut[:-1]
    return cut


def load_upstream_func(source=None):
    """`exec` do recorte num namespace PROPRIO.

    Namespace isolado de proposito: o modulo deles chama-se `ppu_lifter`, tal
    como o nosso, e importa-lo por cima do nosso no diferencial trocaria os dois
    lados sem aviso.
    """
    ns: dict = {}
    exec(compile(source or verbatim_source(), "<upstream_jt_snapshot>", "exec"), ns)
    return ns["discover_jump_tables"]


def cut_function(text, name="discover_jump_tables"):
    """Recorta `def name(...)` ate ao proximo bloco de coluna zero.

    Mesma tecnica usada para produzir este ficheiro; reaproveitada pelo teste
    para recortar a NOSSA funcao e a de um ficheiro-alvo arbitrario.
    """
    lines = text.splitlines(True)
    start = next(i for i, l in enumerate(lines) if l.startswith(f"def {name}("))
    end = start + 1
    while end < len(lines) and not (lines[end].strip() and not lines[end][0].isspace()):
        end += 1
    while end > start + 1 and not lines[end - 1].strip():
        end -= 1
    return "".join(lines[start:end]), (start + 1, end)


def _git_source(sha=None, path=None):
    """Ficheiro do upstream no sha gravado, ou None se o objecto nao existir.

    Os defaults resolvem-se DENTRO da funcao, nao na assinatura: `sha=UPSTREAM_SHA`
    fixava a constante no momento da definicao, e depois `_verify` reportava o sha
    do modulo enquanto lia o do def -- dois valores que podiam discordar sem aviso.
    """
    sha = sha or UPSTREAM_SHA
    path = path or UPSTREAM_PATH
    root = _repo_root()
    if subprocess.run(["git", "-C", root, "cat-file", "-e", f"{sha}^{{commit}}"],
                      capture_output=True).returncode != 0:
        return None
    r = subprocess.run(["git", "-C", root, "show", f"{sha}:{path}"],
                       capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def _refresh():
    """Re-extrai do ramo do upstream e escreve o modulo inteiro para stdout."""
    root = _repo_root()
    sha = subprocess.run(["git", "-C", root, "rev-parse", UPSTREAM_REF],
                         capture_output=True, text=True).stdout.strip()
    if not sha:
        print(f"[jtsnap] ERRO: {UPSTREAM_REF} nao resolve; correr "
              f"`git fetch upstream --prune`", file=sys.stderr)
        return 1
    src = _git_source(sha)
    if src is None:
        print(f"[jtsnap] ERRO: nao consegui ler {sha}:{UPSTREAM_PATH}", file=sys.stderr)
        return 1
    cut, (lo, hi) = cut_function(src)
    digest = hashlib.sha256(cut.encode()).hexdigest()
    today = __import__("datetime").date.today().isoformat()

    text = open(_self_path(), encoding="utf-8").read()
    i, j = _sentinel_span(text)
    text = text[:i] + cut + "\n" + text[j:]
    subs = {
        r'^UPSTREAM_SHA = ".*"$': f'UPSTREAM_SHA = "{sha}"',
        r'^UPSTREAM_LINES = \(.*\)$': f'UPSTREAM_LINES = ({lo}, {hi})',
        r'^UPSTREAM_EXTRACTED = ".*"$': f'UPSTREAM_EXTRACTED = "{today}"',
        r'^UPSTREAM_FUNC_SHA256 = ".*"$': f'UPSTREAM_FUNC_SHA256 = "{digest}"',
    }
    for pat, rep in subs.items():
        text, n = re.subn(pat, rep, text, count=1, flags=re.M)
        if n != 1:
            print(f"[jtsnap] ERRO: constante {pat} nao casou 1x (casou {n}x)",
                  file=sys.stderr)
            return 1
    for pat, rep in (
            (r'^  sha     : .*$', f'  sha     : {sha}'),
            (r'^  linhas  : .*$', f'  linhas  : {lo}-{hi} (1-based, inclusive)'),
            (r'^  extraido: .*$', f'  extraido: {today}'),
            (r'^  sha256  : .*$',
             f'  sha256  : {digest} (do texto entre os sentinelas)')):
        text, n = re.subn(pat, rep, text, count=1, flags=re.M)
        if n != 1:
            print(f"[jtsnap] ERRO: cabecalho {pat} nao casou 1x (casou {n}x)",
                  file=sys.stderr)
            return 1
    sys.stdout.write(text)
    return 0


def _verify():
    """Auto-teste duro (offline) + re-extraccao do git quando disponivel."""
    cut = verbatim_source()
    got = hashlib.sha256(cut.encode()).hexdigest()
    if got != UPSTREAM_FUNC_SHA256:
        print(f"[jtsnap] FAIL o recorte foi editado: sha256={got} "
              f"esperado={UPSTREAM_FUNC_SHA256}", file=sys.stderr)
        return 1
    print(f"[jtsnap] ok  recorte intacto sha256={got[:16]}... "
          f"({UPSTREAM_LINES[0]}-{UPSTREAM_LINES[1]} de {UPSTREAM_PATH})")

    src = _git_source()
    if src is None:
        print("[jtsnap] nota remote upstream ausente -- snapshot nao reconfirmado")
        return 0
    fresh, span = cut_function(src)
    fresh_sha = hashlib.sha256(fresh.encode()).hexdigest()
    if fresh_sha != UPSTREAM_FUNC_SHA256:
        print(f"[jtsnap] FAIL re-extraccao de {UPSTREAM_SHA[:12]} diverge: "
              f"{fresh_sha} != {UPSTREAM_FUNC_SHA256}", file=sys.stderr)
        return 1
    if span != tuple(UPSTREAM_LINES):
        print(f"[jtsnap] FAIL intervalo de linhas diverge: {span} != "
              f"{tuple(UPSTREAM_LINES)}", file=sys.stderr)
        return 1
    print(f"[jtsnap] ok  re-extraido de {UPSTREAM_SHA[:12]}:{UPSTREAM_PATH} "
          f"linhas {span[0]}-{span[1]} -- verbatim confirmado")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--verify", action="store_true",
                   help="reconfirma o recorte (offline duro + git quando existir)")
    g.add_argument("--refresh", action="store_true",
                   help="re-extrai do upstream e escreve o modulo para stdout")
    a = ap.parse_args()
    return _refresh() if a.refresh else _verify()


if __name__ == "__main__":
    sys.exit(main())
