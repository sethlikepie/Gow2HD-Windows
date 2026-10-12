#!/usr/bin/env python3
"""
patch_diag08_committed_range_revert_test.py -- Fase 8, Plano 08-02, Tarefa 1
(diagnostico condicional, NUNCA fix de producao): reversao cirurgica do commit
f6708cb em runtime/ppu/ppu_loader.cpp, dentro de um git worktree isolado.

O que f6708cb mudou (confirmado por grep -n nesta sessao):
  - ppu_register_committed_range(): ganhou committed_lock()/committed_unlock()
    (spinlock __atomic_exchange_n) a envolver o corpo inteiro, e a publicacao
    de g_committed_count passou de "g_committed_count = slot + 1;" simples
    para __atomic_store_n(..., __ATOMIC_RELEASE).
  - vm_uncommitted(): a leitura de g_committed_count passou de simples para
    __atomic_load_n(..., __ATOMIC_ACQUIRE).

Este script reverte EXACTAMENTE essas duas mudancas, dentro de
ppu_register_committed_range()/vm_uncommitted(), SEM tocar nas definicoes de
committed_lock()/committed_unlock() (ficam definidas mas nao chamadas -- nao e'
um "fix", e' uma reversao de diagnostico).

Guarda (T-08-04, mesmo padrao de patch_diag06_147038_revert_test.py): o
alvo TEM de resolver para dentro de /tmp/ps3recomp_diag08_rt -- aborta caso
contrario, nunca escreve no checkout principal do ps3recomp.

Idempotente/anti-silencio: conta as substituicoes feitas; sai com rc!=0 se o
total nao bater o numero fresco confirmado por grep -n nesta execucao.
"""
from pathlib import Path
import re
import sys

DIAG_ROOT = Path(__file__).resolve().parent.parent / "ps3recomp"
TARGET_ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/tmp/ps3recomp_diag08_rt")
TARGET_ROOT = TARGET_ROOT.resolve()

if "/tmp/ps3recomp_diag08_rt" not in str(TARGET_ROOT):
    raise SystemExit(
        f"ABORTA: alvo resolvido para '{TARGET_ROOT}' nao esta dentro de "
        "/tmp/ps3recomp_diag08_rt -- recusa escrever fora do worktree de "
        "diagnostico isolado (T-08-04)."
    )

TARGET_FILE = TARGET_ROOT / "runtime" / "ppu" / "ppu_loader.cpp"
if not TARGET_FILE.is_file():
    raise SystemExit(f"ABORTA: nao encontrei {TARGET_FILE}")

# Numeros confirmados por grep -n nesta execucao (2026-07-30, Plano 08-02):
# 1 chamada committed_lock() + 3 chamadas committed_unlock() dentro do corpo
# de ppu_register_committed_range() (uma no early-return "ja coberta", uma no
# caminho de sucesso, uma no caminho "array cheio") + 1 __atomic_store_n do
# publish + 1 __atomic_load_n da leitura em vm_uncommitted() = 6 no total.
EXPECTED_LOCK_CALLS = 1
EXPECTED_UNLOCK_CALLS = 3
EXPECTED_STORE_SITES = 1
EXPECTED_LOAD_SITES = 1
EXPECTED_TOTAL = (EXPECTED_LOCK_CALLS + EXPECTED_UNLOCK_CALLS
                  + EXPECTED_STORE_SITES + EXPECTED_LOAD_SITES)

s = TARGET_FILE.read_text(encoding="utf-8", errors="replace")

start_marker = "extern \"C\" void ppu_register_committed_range(uint32_t lo, uint32_t hi)\n{"
i = s.find(start_marker)
if i < 0:
    raise SystemExit("ABORTA: nao encontrei a assinatura de ppu_register_committed_range")
body_start = i + len(start_marker)
close_match = re.search(r"\n\}\n", s[body_start:])
if close_match is None:
    raise SystemExit("ABORTA: nao encontrei o fecho '}' de ppu_register_committed_range no inicio de linha")
body_end = body_start + close_match.start() + 1
region = s[body_start:body_end]

# (1) remove a chamada committed_lock(); (so' linha propria, com indentacao)
region, n_lock = re.subn(r"[ \t]*committed_lock\(\);\n", "", region)

# (2) remove as chamadas committed_unlock();
region, n_unlock = re.subn(r"[ \t]*committed_unlock\(\);\n", "", region)

# (3) volta o publish a um store simples
region, n_store = re.subn(
    r"__atomic_store_n\(&g_committed_count, slot \+ 1, __ATOMIC_RELEASE\);",
    "g_committed_count = slot + 1;",
    region,
)

s = s[:body_start] + region + s[body_end:]

# (4) vm_uncommitted(): leitura simples em vez de __atomic_load_n
s, n_load = re.subn(
    r"__atomic_load_n\(&g_committed_count, __ATOMIC_ACQUIRE\)",
    "g_committed_count",
    s,
)

total = n_lock + n_unlock + n_store + n_load
print(f"committed_lock() removidas: {n_lock}")
print(f"committed_unlock() removidas: {n_unlock}")
print(f"__atomic_store_n revertido: {n_store}")
print(f"__atomic_load_n revertido: {n_load}")
print(f"total: {total}")

if (n_lock != EXPECTED_LOCK_CALLS or n_unlock != EXPECTED_UNLOCK_CALLS
        or n_store != EXPECTED_STORE_SITES or n_load != EXPECTED_LOAD_SITES
        or total != EXPECTED_TOTAL):
    raise SystemExit(
        f"ABORTA: esperava {EXPECTED_LOCK_CALLS} lock + {EXPECTED_UNLOCK_CALLS} unlock "
        f"+ {EXPECTED_STORE_SITES} store + {EXPECTED_LOAD_SITES} load = {EXPECTED_TOTAL} "
        f"total; encontrei {n_lock}+{n_unlock}+{n_store}+{n_load}={total}. "
        "O ppu_loader.cpp pode ter mudado -- nunca aceitar silenciosamente um "
        "numero diferente (licao de 2026-07-26-tocfix-matou-36-conversoes-opd.md)."
    )

TARGET_FILE.write_text(s, encoding="utf-8", newline="\n")
print(f"OK: {TARGET_FILE} actualizado, {total} substituicoes (reversao de f6708cb)")
