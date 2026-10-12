#!/usr/bin/env python3
"""Testes de patch_14a01c_opd.py (D-4.7, Fase 4 do ps3recomp).

Porque existe: `patch_14a01c_opd.py` e' o UNICO dos 87 `patch_*.py` sem nenhum
caminho de saida diferente de zero (medido nesta sessao, ficheiro inteiro lido:
48 linhas, termina em `p.write_text(...); print("OK total", total)` sem
`raise SystemExit` em lado nenhum). O mesmo defeito sistemico ja corrigido em
seis scripts irmaos (`patch_32e200_opd.py`, `patch_39e5d8_opd.py`,
`patch_icg_ctor_opd.py`, `patch_14b1f0_opd.py`, `patch_b71_2f3f0_guard.py`,
`patch_ba808_wadld_eof.py`) e a mesma causa raiz: a agulha do restauro do TOC
so' casa a forma antiga (`vm_read64(ctx->gpr[1] + 0x28)`), e o lift actual
(>=23/07) usa a forma TOCFIX (`0x...ULL; /*TOCFIX...*/`).

Numero medido nesta sessao de planeamento contra `recomp_macos_v3` real:
`func_0014A01C` tem 2 sitios `ps3_call_opd(` (gpr[10] com pre-linha
`ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x18);`, gpr[11] com pre-linha
`ctx->gpr[11] = vm_read32(ctx->gpr[9] + 0x14);`) e `func_0014AD94` tem OUTROS
2 sitios (mesmos registos/offsets) -- 4 no total, nao 2. Ambas as funcoes ja
estao 100% convertidas hoje (probe `[ICG-VCALL]` presente nos 4 sitios,
exclusiva do `patch_icg_ctor_opd.py`) porque o `patch_14a01c_opd.py`, dono
nominal destas 2 funcoes, esta partido.

Fixtures: `FIXTURE_ANTES` e' construida PROGRAMATICAMENTE (nao extraida do
lift real, que ja esta convertido -- nao existe mais um "antes" em disco para
estas 2 funcoes) a partir do ground truth medido nesta sessao: o formato
OPD_BLOCK exacto de `patch_icg_ctor_opd.py` (fonte de verdade estrutural,
confirmada contra o texto JA CONVERTIDO de `recomp_macos_v3`), com o registo
intermedio fixo em `gpr[0]` -- a forma que o `patch_14a01c_opd.py` ORIGINAL
(funcional, anterior ao formato TOCFIX) sempre soube converter, confirmado
por `recomp_macos_v2.pre_v4` mostrar estas 2 funcoes ja convertidas SEM a
probe `[ICG-VCALL]` (ou seja, convertidas por essa versao anterior, nao pelo
`icg_ctor`).

Teste 0 (RED) corre contra um SNAPSHOT CONGELADO do script tal como estava
antes desta correccao (capturado por leitura directa do ficheiro no inicio
desta task, embutido em `OLD_SCRIPT_SRC` abaixo) -- nao contra o script
mutavel `PATCH_PATH`. Isto mantem o Teste 0 valido para sempre como prova do
bug historico, mesmo depois do script real ser corrigido nesta mesma sessao
(Task 2).

Uso: python3 test_patch_14a01c_opd.py   (rc=0 verde, rc=1 falha)
"""
from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH_PATH = HERE / "patch_14a01c_opd.py"


def _pick_python() -> str:
    """Interprete >=3.10 para correr os patches, como o apply_all_patches.sh faz.

    Os patch_*.py usam Path.write_text(..., newline="\\n") -- kwarg que so'
    existe em Python 3.10+. O python3 do sistema no macOS e' o 3.9.6 da Apple,
    que estoura TypeError DEPOIS de todo o trabalho em memoria. Mesma logica
    de `_pick_python()` em test_patch_14b1f0_opd.py.
    """
    import shutil
    if sys.version_info >= (3, 10):
        return sys.executable
    for cand in ("python3.14", "python3.13", "python3.12", "python3.11",
                 "python3.10", "/opt/homebrew/bin/python3", "/usr/local/bin/python3"):
        path = shutil.which(cand) or (cand if Path(cand).exists() else None)
        if not path:
            continue
        try:
            r = subprocess.run(
                [path, "-c", "import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)"],
                capture_output=True)
            if r.returncode == 0:
                return path
        except OSError:
            continue
    return sys.executable          # sem alternativa: falha com diagnostico claro


PY = _pick_python()
REAL_LIFT_DIR = HERE.parent / "recomp_macos_v3"
REAL_LIFT_000 = REAL_LIFT_DIR / "ppu_recomp_000.cpp"

FAILS: list[str] = []


def check(name: str, got, want) -> None:
    if got != want:
        FAILS.append(f"{name}\n    got : {got!r}\n    want: {want!r}")


# ---------------------------------------------------------------------------
# Fixture: construida PROGRAMATICAMENTE a partir do ground truth medido nesta
# sessao contra o texto JA CONVERTIDO de recomp_macos_v3 (ver docstring).
# ---------------------------------------------------------------------------

TOC_ADDR_HEX = "0x00541178"  # medido: endereco de TOC reaproveitado nesta regiao
# reg do OPD -> offset do vm_read32(ctx->gpr[9] + offset) que o carrega
SITE_OFFSETS = {10: "0x18", 11: "0x14"}


def _site_block(reg: int) -> str:
    """Um sitio OPD_BLOCK completo, formato ANTES da conversao (registo
    intermedio fixo em gpr[0], restauro do TOC em forma TOCFIX)."""
    off = SITE_OFFSETS[reg]
    return (
        f"        ctx->gpr[{reg}] = vm_read32(ctx->gpr[9] + {off});\n"
        f"        ctx->gpr[0] = vm_read32(ctx->gpr[{reg}] + 0x0);\n"
        "        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
        "        ctx->ctr = (uint32_t)ctx->gpr[0];\n"
        f"        ctx->gpr[2] = vm_read32(ctx->gpr[{reg}] + 0x4);\n"
        "        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n"
        f"        ctx->gpr[2] = {TOC_ADDR_HEX}ULL; /*TOCFIX ld r2,N(r1)*/\n"
    )


def _func_block(name: str) -> str:
    return f"void {name}(ppu_context* ctx) {{\n" + _site_block(10) + _site_block(11) + "}\n"


FIXTURE_ANTES = (
    _func_block("func_0014A01C")
    + _func_block("func_0014AD94")
    + "void func_0014BEAC(ppu_context* ctx) {\n}\n"
)

# FIXTURE_ANTES_OLD: derivada MECANICAMENTE de FIXTURE_ANTES, trocando as 4
# ocorrencias TOCFIX pela forma antiga (vm_read64). Prova de compatibilidade
# retroactiva, nao uma segunda construcao independente.
FIXTURE_ANTES_OLD, _N_TOCFIX_SUBS = re.subn(
    r"0x[0-9A-Fa-f]+ULL; /\*TOCFIX[^\n]*",
    "vm_read64(ctx->gpr[1] + 0x28);",
    FIXTURE_ANTES,
)
if _N_TOCFIX_SUBS != 4:
    raise SystemExit(
        f"FIXTURE_ANTES_OLD: esperava 4 substituicoes TOCFIX->vm_read64, fiz "
        f"{_N_TOCFIX_SUBS} -- a fixture nao tem o shape esperado, pare e reporte"
    )


def _write_fixture(tmpdir: str, region: str) -> Path:
    p = Path(tmpdir) / "ppu_recomp_000.cpp"
    # open(..., newline="\n") e nao Path.write_text(newline=...): o kwarg de
    # write_text so' existe em Python 3.10+, e o python3 do sistema no macOS e'
    # o 3.9.6 da Apple. O patch pode exigir 3.10 (pick_python resolve isso no
    # apply_all_patches.sh); o TESTE nao pode.
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(region)
    return p


# ---------------------------------------------------------------------------
# Snapshot CONGELADO do script tal como estava ANTES desta correccao
# (capturado por leitura directa do ficheiro no inicio desta task, antes de
# qualquer edicao). O Teste 0 corre contra ESTE snapshot, nao contra
# PATCH_PATH (que a Task 2 corrige).
# ---------------------------------------------------------------------------
OLD_SCRIPT_SRC = r'''#!/usr/bin/env python3
"""Fix OPD sites in constructors that install vtable with 171244 at +0x8."""
from pathlib import Path
import re
import sys

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
p = ROOT / "ppu_recomp_000.cpp"
s = p.read_text(encoding="utf-8", errors="replace")

pat = re.compile(
    r"(        ctx->gpr\[(\d+)\] = vm_read32\(ctx->gpr\[\d+\] \+ 0x[0-9A-Fa-f]+\);\n)"
    r"        ctx->gpr\[0\] = vm_read32\(ctx->gpr\[\2\] \+ 0x0\);\n"
    r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n"
    r"        ctx->ctr = \(uint32_t\)ctx->gpr\[0\];\n"
    r"        ctx->gpr\[2\] = vm_read32\(ctx->gpr\[\2\] \+ 0x4\);\n"
    r"        ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    r"        ctx->gpr\[2\] = vm_read64\(ctx->gpr\[1\] \+ 0x28\);"
)

total = 0
for name in ["func_0014A01C", "func_0014AD94"]:
    i = s.find(f"void {name}")
    if i < 0:
        print(name, "missing")
        continue
    j = s.find("void func_", i + 20)
    region = s[i:j]
    state = {"n": 0}

    def repl(m):
        state["n"] += 1
        reg = m.group(2)
        return (
            m.group(1)
            + "        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
            + f"        ps3_call_opd(ctx, (uint32_t)ctx->gpr[{reg}]); DRAIN_TRAMPOLINE(ctx);\n"
            + "        ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);"
        )

    region2, c = pat.subn(repl, region)
    print(f"{name}: fixed {state['n']} remain_indirect={region2.count('ps3_indirect_call')}")
    total += state["n"]
    s = s[:i] + region2 + s[j:]

p.write_text(s, encoding="utf-8", newline="\n")
print("OK total", total)
'''


# ---------------------------------------------------------------------------
# Testes
# ---------------------------------------------------------------------------

def test_1_fixture_antes_shape() -> None:
    """Teste 1: FIXTURE_ANTES tem exactamente 4 ps3_indirect_call, 4 TOCFIX, 0 ps3_call_opd."""
    check("FIXTURE_ANTES: 4x ps3_indirect_call",
          FIXTURE_ANTES.count("ps3_indirect_call"), 4)
    check("FIXTURE_ANTES: 4x TOCFIX",
          FIXTURE_ANTES.count(f"{TOC_ADDR_HEX}ULL; /*TOCFIX"), 4)
    check("FIXTURE_ANTES: 0x ps3_call_opd", FIXTURE_ANTES.count("ps3_call_opd("), 0)


def test_2_fixture_antes_old_compat() -> None:
    """Teste 2: FIXTURE_ANTES_OLD (forma antiga) preserva os 4 sitios, 0 TOCFIX."""
    check("FIXTURE_ANTES_OLD: 4x ps3_indirect_call",
          FIXTURE_ANTES_OLD.count("ps3_indirect_call"), 4)
    check("FIXTURE_ANTES_OLD: 4x vm_read64(ctx->gpr[1] + 0x28);",
          FIXTURE_ANTES_OLD.count("vm_read64(ctx->gpr[1] + 0x28);"), 4)
    check("FIXTURE_ANTES_OLD: 0x TOCFIX", FIXTURE_ANTES_OLD.count("TOCFIX"), 0)


def test_0_red_bug_medido_no_script_congelado() -> None:
    """Teste 0 (RED): o script tal como estava ANTES da correccao (snapshot
    congelado, nao o PATCH_PATH mutavel) corre contra FIXTURE_ANTES (TOCFIX,
    4 sitios convertiveis), nao converte NADA, e sai rc=0 na mesma -- o
    falso-verde medido em D-4.7, reproduzido por fixture."""
    with tempfile.TemporaryDirectory() as td:
        _write_fixture(td, FIXTURE_ANTES)
        frozen_script = Path(td) / "patch_14a01c_opd_PRE_FIX_snapshot.py"
        frozen_script.write_text(OLD_SCRIPT_SRC, encoding="utf-8")
        result = subprocess.run(
            [PY, str(frozen_script), td],
            capture_output=True, text=True,
        )
        check("script congelado: rc", result.returncode, 0)
        check("script congelado: fixed 0 em func_0014A01C",
              "func_0014A01C: fixed 0" in result.stdout, True)
        check("script congelado: fixed 0 em func_0014AD94",
              "func_0014AD94: fixed 0" in result.stdout, True)
        check("script congelado: OK total 0 (falso-verde)",
              "OK total 0" in result.stdout, True)


def _count_fixed(stdout: str) -> int:
    """Soma o 'fixed N' de cada linha por-funcao (o script corrigido ja nao
    imprime 'OK total N' incondicional -- isso e' precisamente o defeito que
    a Task 2 remove; o total agora so' aparece dentro do veredicto de 3 ramos)."""
    ms = re.findall(r": fixed (\d+) already=\d+", stdout)
    if not ms:
        return -1
    return sum(int(x) for x in ms)


def test_3_green1_convertido_tocfix() -> None:
    """GREEN 1: script corrigido contra FIXTURE_ANTES converte os 4 sitios,
    remaining indirect=0, veredicto CONVERTIDO, rc=0."""
    with tempfile.TemporaryDirectory() as td:
        f = _write_fixture(td, FIXTURE_ANTES)
        result = subprocess.run(
            [PY, str(PATCH_PATH), td], capture_output=True, text=True,
        )
        check("GREEN1 rc", result.returncode, 0)
        check("GREEN1 OK total 4", _count_fixed(result.stdout), 4)
        check("GREEN1 remain_indirect=0 (ambas as funcoes)",
              result.stdout.count("remain_indirect=0"), 2)
        check("GREEN1 veredicto CONVERTIDO",
              "OK patch_14a01c_opd (CONVERTIDO)" in result.stdout, True)
        after = f.read_text(encoding="utf-8")
        check("GREEN1 ps3_call_opd(ctx, aparece 4x", after.count("ps3_call_opd(ctx,"), 4)


def test_4_green2_compat_forma_antiga() -> None:
    """GREEN 2: script corrigido contra FIXTURE_ANTES_OLD converte os mesmos
    4 sitios, reemitindo o TOC VERBATIM na forma antiga (0 TOCFIX inventado)."""
    with tempfile.TemporaryDirectory() as td:
        f = _write_fixture(td, FIXTURE_ANTES_OLD)
        result = subprocess.run(
            [PY, str(PATCH_PATH), td], capture_output=True, text=True,
        )
        check("GREEN2 rc", result.returncode, 0)
        check("GREEN2 OK total 4", _count_fixed(result.stdout), 4)
        after = f.read_text(encoding="utf-8")
        check("GREEN2 ps3_call_opd(ctx, aparece 4x", after.count("ps3_call_opd(ctx,"), 4)
        check("GREEN2 vm_read64 reemitido verbatim 4x",
              after.count("vm_read64(ctx->gpr[1] + 0x28);"), 4)
        check("GREEN2 zero TOCFIX inventado", "TOCFIX" in after, False)


def test_5_green3_idempotencia_ate_terceira_corrida() -> None:
    """GREEN 3: 2a corrida sobre o resultado da 1a converte 0 novos, reporta 4
    ja-aplicados, JA-APLICADO, rc=0; 3a corrida da ficheiro byte-identico a' 2a."""
    with tempfile.TemporaryDirectory() as td:
        f = _write_fixture(td, FIXTURE_ANTES)
        r1 = subprocess.run([PY, str(PATCH_PATH), td], capture_output=True, text=True)
        check("GREEN3 1a corrida rc", r1.returncode, 0)

        r2 = subprocess.run([PY, str(PATCH_PATH), td], capture_output=True, text=True)
        check("GREEN3 2a corrida rc", r2.returncode, 0)
        check("GREEN3 2a corrida OK total 0 (0 conversoes novas)", _count_fixed(r2.stdout), 0)
        check("GREEN3 2a corrida veredicto JA-APLICADO",
              "JA-APLICADO" in r2.stdout, True)
        check("GREEN3 2a corrida reporta 4 sitios ja em ps3_call_opd",
              "4 sitios ja em ps3_call_opd" in r2.stdout, True)
        after2 = f.read_text(encoding="utf-8")

        r3 = subprocess.run([PY, str(PATCH_PATH), td], capture_output=True, text=True)
        after3 = f.read_text(encoding="utf-8")
        check("GREEN3 3a corrida rc", r3.returncode, 0)
        check("GREEN3 3a corrida byte-identica a' 2a (idempotencia por hash/conteudo)",
              after3, after2)


def test_6_green4_sem_efeito_rc3() -> None:
    """GREEN 4 (o coracao da correccao): fixture sem ps3_indirect_call nem
    ps3_call_opd pre-existente em nenhuma das 2 funcoes -- 0 conversoes, 0
    ja-aplicadas, tem de sair rc=3 (SEM-EFEITO), nunca rc=0."""
    fixture_sem_efeito = FIXTURE_ANTES.replace(
        "ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);",
        "chamada_hipotetica_num_terceiro_formato(ctx);",
    )
    check("fixture SEM-EFEITO: zero indirect+drain restantes",
          "ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);" in fixture_sem_efeito, False)
    check("fixture SEM-EFEITO: zero ps3_call_opd pre-existente",
          "ps3_call_opd(ctx," in fixture_sem_efeito, False)
    with tempfile.TemporaryDirectory() as td:
        f = _write_fixture(td, fixture_sem_efeito)
        result = subprocess.run(
            [PY, str(PATCH_PATH), td], capture_output=True, text=True,
        )
        check("GREEN4 rc == RC_NO_EFFECT (3)", result.returncode, 3)
        check("GREEN4 veredicto SEM-EFEITO", "SEM-EFEITO" in result.stdout, True)
        after = f.read_text(encoding="utf-8")
        check("GREEN4 nenhum ps3_call_opd foi criado", after.count("ps3_call_opd(ctx,"), 0)


def test_7_green5_e2e_copia_scratch_do_lift_real() -> None:
    """GREEN 5 (prova mais forte que fixture): aplicado contra uma COPIA
    scratch (nunca o original) de recomp_macos_v3/ppu_recomp_000.cpp real
    (ja convertido por icg_ctor historicamente), o script corrigido reporta
    JA-APLICADO com 4 sitios e rc=0 -- nao tenta reconverter nem falha. O
    ficheiro real de producao fica byte-a-byte intacto (hash antes/depois)."""
    import hashlib
    import shutil

    def sha256(path: Path) -> str:
        return hashlib.sha256(path.read_bytes()).hexdigest()

    if not REAL_LIFT_000.is_file():
        raise SystemExit(f"lift real nao encontrado em {REAL_LIFT_000} -- pare e reporte")

    hash_before = sha256(REAL_LIFT_000)
    with tempfile.TemporaryDirectory() as td:
        scratch = Path(td) / "ppu_recomp_000.cpp"
        shutil.copyfile(REAL_LIFT_000, scratch)
        result = subprocess.run(
            [PY, str(PATCH_PATH), td], capture_output=True, text=True,
        )
        check("GREEN5 rc", result.returncode, 0)
        check("GREEN5 veredicto JA-APLICADO", "JA-APLICADO" in result.stdout, True)
        check("GREEN5 4 sitios ja em ps3_call_opd",
              "4 sitios ja em ps3_call_opd" in result.stdout, True)
        check("GREEN5 0 conversoes novas (OK total 0)", _count_fixed(result.stdout), 0)
    hash_after = sha256(REAL_LIFT_000)
    check("GREEN5 lift real de producao permanece intacto (sha256)",
          hash_after, hash_before)


def main() -> int:
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print(f"FAIL ({len(FAILS)}):", file=sys.stderr)
        for f in FAILS:
            print("  " + f, file=sys.stderr)
        return 1
    print("[test_patch_14a01c_opd] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
