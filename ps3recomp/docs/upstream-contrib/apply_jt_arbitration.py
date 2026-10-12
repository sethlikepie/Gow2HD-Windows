#!/usr/bin/env python3
"""Aplica a arbitracao da base de jump table ao ppu_lifter.py do upstream.

Transformacao (nao e' substituicao da funcao deles): o registo-base deixa de ser
ESCOLHIDO pelo primeiro candidato que decodifica e passa a ser ARBITRADO pelo
numero de alvos validados -- o mesmo principio que a funcao ja' aplica aos
candidatos a TOC. Tudo o resto (two-level base, idiom sldi+add de 64 bits,
_read_ptr do ELFv1, contagem pelo maior imediato, scan generoso) e' preservado e
apenas passa a correr uma vez por candidato a base.

Idempotente: se ja' aplicado, sai 0 sem alterar nada.
"""
import sys

path = sys.argv[1]
src = open(path, encoding='utf-8').read()

if 'base_cands' in src:
    print('[jt-arb] ja aplicado, nada a fazer')
    sys.exit(0)

def sub1(old, new, what):
    global src
    n = src.count(old)
    if n != 1:
        sys.exit(f'[jt-arb] ANCORA {what}: esperava 1 ocorrencia, achei {n}')
    src = src.replace(old, new)

# ---- 1. a coleta deixa de ser um slot unico ------------------------------
sub1(
    "        r_val = p[0]\n"
    "        disp = None; r_base = None; base_is_ld = False\n"
    "        disp2 = None; disp2_is_ld = False\n",
    "        r_val = p[0]\n"
    "        # ARBITRATION, not selection: collect EVERY operand that has a\n"
    "        # base-load definition and let the decode loop below keep the one\n"
    "        # that validates the most targets.\n"
    "        base_cands = []          # [(r_base, disp, base_is_ld, disp2, disp2_is_ld)]\n",
    'init')

# ---- 2. cada candidato arranca com o seu proprio estado -------------------
sub1(
    "        for cand in _cands:\n"
    "            # Walk backward to the NEAREST instruction that defines `cand`, and\n",
    "        for cand in _cands:\n"
    "            disp = None; r_base = None; base_is_ld = False\n"
    "            disp2 = None; disp2_is_ld = False\n"
    "            # Walk backward to the NEAREST instruction that defines `cand`, and\n",
    'reset-por-candidato')

# ---- 3. em vez de parar no primeiro, guarda-o e continua ------------------
sub1(
    "                break                           # first definition of cand wins/loses\n"
    "            if disp is not None:\n"
    "                break\n",
    "                break                           # first definition of cand wins/loses\n"
    "            if disp is not None:\n"
    "                base_cands.append((r_base, disp, base_is_ld, disp2, disp2_is_ld))\n",
    'acumula')

# ---- 4. o gate passa a ser sobre a lista ---------------------------------
sub1(
    '        _dbg(all_insns[i].addr, f"disp={disp} disp2={disp2} r_base={r_base} base_is_ld={base_is_ld} toc={toc}")\n'
    "        if disp is None or not toc:\n"
    "            continue\n"
    "        toc_candidates = toc if isinstance(toc, (list, tuple)) else [toc]\n",
    '        _dbg(all_insns[i].addr, f"base_cands={base_cands} toc={toc}")\n'
    "        if not base_cands or not toc:\n"
    "            continue\n"
    "        toc_candidates = toc if isinstance(toc, (list, tuple)) else [toc]\n"
    "        # Ties go to the earlier candidate -- which is the `add`-paired one\n"
    "        # whenever the reordering above fired, so the syntactic preference\n"
    "        # survives as a tie-break instead of being the final decision.\n"
    "        best = []\n"
    "        for r_base, disp, base_is_ld, disp2, disp2_is_ld in base_cands:\n",
    'gate')

# ---- 5. o corpo (is_offset..decode) passa a correr por candidato ----------
lines = src.split('\n')
try:
    lo = next(k for k, l in enumerate(lines)
              if l == '        # offset table iff an `add rC, *, r_base` combines the loaded value + base')
    hi = next(k for k, l in enumerate(lines) if l == '                best = targets')
except StopIteration:
    sys.exit('[jt-arb] ANCORA corpo: nao encontrei o intervalo a re-indentar')

body = lines[lo:hi + 1]
drop = '        best = []'
if body.count(drop) != 1:
    sys.exit(f'[jt-arb] ANCORA best=[]: esperava 1, achei {body.count(drop)}')
body = [l for l in body if l != drop]
body = [('    ' + l) if l.strip() else l for l in body]
lines[lo:hi + 1] = body
src = '\n'.join(lines)

open(path, 'w', encoding='utf-8').write(src)
print('[jt-arb] aplicado')
