#!/usr/bin/env python3
"""
patch_diag06_147038_revert_test.py -- Fase 6, Plano 06-02, Tarefa 2 (diagnostico
condicional, NUNCA fix de producao): reversao cirurgica, SO' dentro do corpo de
func_00147038 (thr_auto_load), de duas hipoteses ja registadas:

  (a) ctx->lr = 0x{ENDERECO}; func_{ALVO}(ctx); DRAIN_TRAMPOLINE(ctx);
      -> func_{ALVO}(ctx); DRAIN_TRAMPOLINE(ctx);
      (remove so' a atribuicao explicita a ctx->lr antes de cada bl, mantem a
      chamada e o DRAIN_TRAMPOLINE)

  (b) ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/
      -> ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);
      (repoe o restauro DINAMICO do TOC, como no lift antigo -- ver
      2026-07-26-tocfix-matou-36-conversoes-opd.md)

Guarda (T-06-04): nunca escreve num alvo cujo caminho resolvido nao tenha o
sufixo ".diag06_test" -- aborta em vez de arriscar tocar em recomp_macos_v2 de
producao.

Idempotente: conta as substituicoes feitas; sai com rc!=0 se o total nao bater
com o numero fresco confirmado por grep-n nesta execucao (a licao exacta da
nota 2026-07-26-tocfix-matou-36-conversoes-opd.md -- nunca "fixed 0" com rc=0).
"""
from pathlib import Path
import re
import sys

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent / "recomp_macos_v2.diag06_test"
ROOT = ROOT.resolve()

if ".diag06_test" not in ROOT.name:
    raise SystemExit(
        f"ABORTA: alvo resolvido para '{ROOT}' nao tem sufixo '.diag06_test' -- "
        "recusa escrever fora da copia de teste (T-06-04)."
    )

TARGET_FILE = ROOT / "ppu_recomp_000.cpp"
if not TARGET_FILE.is_file():
    raise SystemExit(f"ABORTA: nao encontrei {TARGET_FILE}")

# O numero esperado de substituicoes foi confirmado por grep -n nesta execucao
# (2026-07-30): 7 sitios ctx->lr= dentro do corpo de func_00147038 (nao 6, como
# a evidencia antiga do plano assumia -- o lift foi regenerado entre o
# planeamento e a execucao) + 2 sitios TOCFIX = 9 no total. Se este numero
# mudar outra vez, o script tem de FALHAR ALTO, nunca aceitar silenciosamente.
EXPECTED_LR_SITES = 7
EXPECTED_TOCFIX_SITES = 2
EXPECTED_TOTAL = EXPECTED_LR_SITES + EXPECTED_TOCFIX_SITES

s = TARGET_FILE.read_text(encoding="utf-8", errors="replace")

start_marker = "void func_00147038(ppu_context* ctx) {"
i = s.find(start_marker)
if i < 0:
    raise SystemExit("ABORTA: nao encontrei 'void func_00147038(ppu_context* ctx) {'")

body_start = i + len(start_marker)
# Fecho da funcao: primeira linha "}" no INICIO de linha (coluna 0), a partir
# do corpo -- nunca um "}" indentado de um bloco interno (if/for/switch). Nunca
# usar re.S guloso (a licao do bug 1 do strip_block, 2026-07-26).
close_match = re.search(r"\n\}\n", s[body_start:])
if close_match is None:
    raise SystemExit("ABORTA: nao encontrei o fecho '}' de func_00147038 no inicio de linha")
body_end = body_start + close_match.start() + 1  # inclui o '\n' antes do '}'

region = s[body_start:body_end]

# (a) remove so' a atribuicao ctx->lr = 0x...; antes de "func_XXXXXXXX(ctx); DRAIN_TRAMPOLINE(ctx);"
lr_pattern = re.compile(
    r"ctx->lr = 0x[0-9A-Fa-f]+(?:ULL)?;\s*(func_[0-9A-Fa-f]{8}\(ctx\); DRAIN_TRAMPOLINE\(ctx\);)"
)
region, n_lr = lr_pattern.subn(r"\1", region)

# (b) repoe o restauro dinamico do TOC
tocfix_pattern = re.compile(
    r"ctx->gpr\[2\] = 0x00541178ULL; /\*TOCFIX ld r2,N\(r1\)\*/"
)
region, n_tocfix = tocfix_pattern.subn("ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);", region)

total = n_lr + n_tocfix
print(f"ctx->lr sites revertidos: {n_lr}")
print(f"TOCFIX sites revertidos: {n_tocfix}")
print(f"total: {total}")

if n_lr != EXPECTED_LR_SITES or n_tocfix != EXPECTED_TOCFIX_SITES or total != EXPECTED_TOTAL:
    raise SystemExit(
        f"ABORTA: esperava {EXPECTED_LR_SITES} sitios ctx->lr + {EXPECTED_TOCFIX_SITES} "
        f"TOCFIX = {EXPECTED_TOTAL} total; encontrei {n_lr}+{n_tocfix}={total}. "
        "O lift pode ter mudado -- nunca aceitar silenciosamente um numero diferente "
        "(licao de 2026-07-26-tocfix-matou-36-conversoes-opd.md)."
    )

s = s[:body_start] + region + s[body_end:]
TARGET_FILE.write_text(s, encoding="utf-8", newline="\n")
print(f"OK: {TARGET_FILE} actualizado, {total} substituicoes dentro de func_00147038")
