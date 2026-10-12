#!/usr/bin/env python3
"""Testes de patch_14b1f0_opd.py (D-3.7, Fase 3 do ps3recomp).

Porque existe: `patch_14b1f0_opd.py` corre hoje contra `func_0014B1F0` e imprime
"replaced pat1=0 pat2=0" -- e sai rc=0 na mesma. E' o mesmo defeito sistemico
medido em 03-CONTEXT.md para os outros 24 patches OPD ja corrigidos: um patch
que corre e nao converte nada devolve rc=0, e o `apply_all_patches.sh` classifica
isso como ALREADY-APPLIED em vez de FALHA.

Causa medida (grep directo contra o lift real nesta sessao): as agulhas do
script terminam SO na forma antiga do restauro do TOC apos o `bctrl`
(`ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);`), mas os 12 sitios reais em
`func_0014B1F0` usam hoje, sem excepcao, a forma nova que o lifter passou a
emitir (`ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/`).

Fixtures: extraidas LITERALMENTE de
`../gow2-recomp/recomp_macos_v2/ppu_recomp_000.cpp` (nunca hand-typed) -- ver
`_extract_region_from_real_lift()`. `REGION_OLD` e' derivada mecanicamente de
`REGION_NEW` trocando as 12 ocorrencias TOCFIX pela forma antiga, para provar
que a correccao aceita AMBOS os formatos (nao troca um "OR" que so funciona
no novo).

Teste 0 (RED) corre contra um SNAPSHOT CONGELADO do script tal como estava
antes desta correccao (capturado via `git show HEAD:...` na sessao de
planeamento, embutido em `OLD_SCRIPT_SRC` abaixo) -- nao contra o script
mutavel `PATCH_PATH`. Isto mantem o Teste 0 valido para sempre como prova do
bug historico, mesmo depois do script real ser corrigido nesta mesma sessao
(Task 2): sem isto, reexecutar o ficheiro de testes depois da correccao
faria o Teste 0 falhar por o script ja converter -- o que nao seria uma
regressao, so' uma consequencia do proprio fix.

Uso: python3 test_patch_14b1f0_opd.py   (rc=0 verde, rc=1 falha)
"""
from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH_PATH = HERE / "patch_14b1f0_opd.py"


def _pick_python() -> str:
    """Interprete >=3.10 para correr os patches, como o apply_all_patches.sh faz.

    Os patch_*.py usam Path.write_text(..., newline="\\n") -- kwarg que so' existe
    em Python 3.10+ e que NAO deve ser removido: e' ele que evita CRLF no lado
    Windows do port. O python3 do sistema no macOS e' o 3.9.6 da Apple, que
    estoura TypeError DEPOIS de todo o trabalho em memoria.

    O apply_all_patches.sh resolve isto com um pick_python(); o teste tem de
    fazer o mesmo, senao passa na maquina de quem tem 3.12 no PATH e falha na de
    quem so' tem o do sistema. Medido em 2026-07-26:
      TypeError: write_text() got an unexpected keyword argument 'newline'
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
REAL_LIFT_DIR = HERE.parent / "recomp_macos_v2"
REAL_LIFT_000 = REAL_LIFT_DIR / "ppu_recomp_000.cpp"

FAILS: list[str] = []


def check(name: str, got, want) -> None:
    if got != want:
        FAILS.append(f"{name}\n    got : {got!r}\n    want: {want!r}")


# ---------------------------------------------------------------------------
# Fixture: extraida do lift REAL de producao, nunca hand-typed.
# ---------------------------------------------------------------------------

def _extract_region_from_real_lift() -> str:
    print(f"[fixture] lendo lift real de: {REAL_LIFT_000}")
    if not REAL_LIFT_000.is_file():
        raise SystemExit(
            f"lift real nao encontrado em {REAL_LIFT_000} -- a fixture nao "
            "pode ser hand-typed, tem de vir de disco. Aborte e reporte."
        )
    s = REAL_LIFT_000.read_text(encoding="utf-8", errors="replace")
    i = s.find("void func_0014B1F0")
    if i < 0:
        raise SystemExit("func_0014B1F0 nao encontrada no lift real -- a regiao mudou de forma, pare e reporte")
    j = s.find("void func_0014BEAC", i)
    if j < 0:
        raise SystemExit("func_0014BEAC nao encontrada apos func_0014B1F0 -- a regiao mudou de forma, pare e reporte")
    return s[i:j]


REGION_NEW = _extract_region_from_real_lift()

# REGION_OLD: derivada MECANICAMENTE de REGION_NEW, trocando as 12 ocorrencias
# TOCFIX pela forma antiga (vm_read64). Prova de compatibilidade retroactiva,
# nao uma segunda extraccao independente do disco.
REGION_OLD, _N_TOCFIX_SUBS = re.subn(
    r"0x[0-9A-Fa-f]+ULL; /\*TOCFIX[^\n]*",
    "vm_read64(ctx->gpr[1] + 0x28);",
    REGION_NEW,
)
if _N_TOCFIX_SUBS != 12:
    raise SystemExit(
        f"REGION_OLD: esperava 12 substituicoes TOCFIX->vm_read64, fiz {_N_TOCFIX_SUBS} "
        "-- a fixture nao tem o shape esperado, pare e reporte"
    )

# Funcao seguinte minima, so' para delimitar a regiao quando escrita como
# ficheiro standalone (o script procura "void func_0014BEAC" para saber onde
# a funcao acaba).
_NEXT_FUNC_STUB = "void func_0014BEAC(ppu_context* ctx) {\n}\n"

# Preambulo sintetico minimo: o script CORRIGIDO (Task 2) chama ensure_decl(s)
# antes de tudo, que exige a declaracao de ps3_indirect_call dentro de
# s[:80000] (identico ao que o lift real ja tem, medido a offset 37408). Sem
# isto as fixtures GREEN (que so' contem a regiao da funcao) fariam
# ensure_decl() abortar com "no ps3_indirect_call decl in 000" -- nao e' o que
# se quer testar aqui, por isso as duas declaracoes vao pre-inseridas, tal
# como estao no chunk real, e ensure_decl() e' um no-op nestas fixtures.
_PREAMBLE = (
    '/* preambulo sintetico para as fixtures deste teste */\n'
    'extern "C" void ps3_indirect_call(ppu_context* ctx);\n'
    'extern "C" void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);\n'
)


def _write_fixture(tmpdir: str, region: str, preamble: bool = True) -> Path:
    p = Path(tmpdir) / "ppu_recomp_000.cpp"
    content = (_PREAMBLE if preamble else "") + region + _NEXT_FUNC_STUB
    # open(..., newline="\n") e nao Path.write_text(newline=...): o kwarg de
    # write_text so' existe em Python 3.10+, e o python3 do sistema no macOS e'
    # o 3.9.6 da Apple. O patch em si pode dar-se ao luxo de exigir 3.10 porque
    # o apply_all_patches.sh escolhe um interprete >=3.10 (pick_python); o TESTE
    # nao pode -- corre pelo scripts/test_python.sh e pelo CTest, que usam o
    # interpretador que estiver a' mao. Medido: rebentava com
    #   TypeError: write_text() got an unexpected keyword argument 'newline'
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    return p


# ---------------------------------------------------------------------------
# Snapshot CONGELADO do script tal como estava ANTES desta correccao
# (capturado por `git show HEAD:recomp_mid_v2/patch_14b1f0_opd.py` na sessao
# de planeamento/execucao desta task, antes de qualquer edicao). O Teste 0
# corre contra ESTE snapshot, nao contra PATCH_PATH (que a Task 2 corrige).
# ---------------------------------------------------------------------------
OLD_SCRIPT_SRC = r'''#!/usr/bin/env python3
"""
Fix all OPD indirect calls in func_0014B1F0 (asset/component batch dispatcher).

Evidence:
- Loads TOC-0x3D9C -> OPD 0x5227F0 -> func_00162150 (SHADERSRC type loader)
- 12x ps3_indirect_call on vtable OPDs -- same broken class as factory/GroupEnd
- SHADERSRC already runs somehow, but nested OPDs in this dispatcher may skip
  the path that would reach type-map / ICGLdr (0xF85F9B1E via 171244).
"""
from pathlib import Path
import re
import sys

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
p = ROOT / "ppu_recomp_000.cpp"
s = p.read_text(encoding="utf-8", errors="replace")

i = s.find("void func_0014B1F0")
if i < 0:
    raise SystemExit("func_0014B1F0 missing")
j = s.find("void func_0014BEAC", i)  # next known func
if j < 0:
    j = s.find("void func_", i + 20)
region = s[i:j]

# Generic OPD block: load opd into gpr[N], code into gpr[0], set ctr/toc, indirect
pat = re.compile(
    r"(        ctx->gpr\[(\d+)\] = vm_read32\(ctx->gpr\[\d+\] \+ 0x[0-9A-Fa-f]+\);\n)"
    r"        ctx->gpr\[0\] = vm_read32\(ctx->gpr\[\2\] \+ 0x0\);\n"
    r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n"
    r"        ctx->ctr = \(uint32_t\)ctx->gpr\[0\];\n"
    r"        ctx->gpr\[2\] = vm_read32\(ctx->gpr\[\2\] \+ 0x4\);\n"
    r"        ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    r"        ctx->gpr\[2\] = vm_read64\(ctx->gpr\[1\] \+ 0x28\);"
)

# Alternate order: ctr after toc
pat2 = re.compile(
    r"(        ctx->gpr\[(\d+)\] = vm_read32\(ctx->gpr\[\d+\] \+ 0x[0-9A-Fa-f]+\);\n)"
    r"        ctx->gpr\[0\] = vm_read32\(ctx->gpr\[\2\] \+ 0x0\);\n"
    r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n"
    r"        ctx->gpr\[2\] = vm_read32\(ctx->gpr\[\2\] \+ 0x4\);\n"
    r"        ctx->ctr = \(uint32_t\)ctx->gpr\[0\];\n"
    r"        ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    r"        ctx->gpr\[2\] = vm_read64\(ctx->gpr\[1\] \+ 0x28\);"
)

n = 0

def repl(m):
    global n
    n += 1
    reg = m.group(2)
    return (
        m.group(1)
        + f"        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
        + f"        ps3_call_opd(ctx, (uint32_t)ctx->gpr[{reg}]); DRAIN_TRAMPOLINE(ctx);\n"
        + f"        ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);"
    )

region2, c1 = pat.subn(repl, region)
region3, c2 = pat2.subn(repl, region2)
print(f"replaced pat1={c1} pat2={c2} total_opd_sites={n}")
print(f"remaining indirect in region: {region3.count('ps3_indirect_call')}")

# Add entry probe once
if "WADLD-BATCH" not in region3:
    needle = "void func_0014B1F0(ppu_context* ctx) {\n"
    probe = (
        "void func_0014B1F0(ppu_context* ctx) {\n"
        "        { static int on=-1; if(on<0){extern char* getenv(const char*); "
        "on=(getenv(\"PS3_TRACE_TYMAP\")||getenv(\"PS3_TRACE_LDRSH\"))?1:0;}\n"
        "          if(on){ static int n=0; if(n++<8)\n"
        "            fprintf(stderr,\"[WADLD-BATCH] #%d enter r3=0x%08X r4=0x%08X\\n\",\n"
        "              n,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[4]); fflush(stderr);} }\n"
    )
    if needle in region3:
        region3 = region3.replace(needle, probe, 1)
        print("added BATCH entry probe")

s = s[:i] + region3 + s[j:]
p.write_text(s, encoding="utf-8", newline="\n")
print("OK patch_14b1f0_opd")
'''


# ---------------------------------------------------------------------------
# Testes
# ---------------------------------------------------------------------------

def test_formato_real_confirmado() -> None:
    """Teste 2 do PLAN: a fixture NOVA tem 12 indirect+drain e 12 TOCFIX."""
    check("REGION_NEW: 12x indirect+drain",
          REGION_NEW.count("ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);"), 12)
    check("REGION_NEW: 12x TOCFIX",
          REGION_NEW.count("0x00541178ULL; /*TOCFIX"), 12)


def test_regiao_old_derivada_compat() -> None:
    """Teste 3 do PLAN: REGION_OLD (derivada) preserva os 12 sitios, forma antiga."""
    check("REGION_OLD: 12x indirect+drain",
          REGION_OLD.count("ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);"), 12)
    check("REGION_OLD: 12x vm_read64 (forma antiga)",
          REGION_OLD.count("vm_read64(ctx->gpr[1] + 0x28);"), 12)
    check("REGION_OLD: 0x TOCFIX (nao deve sobrar nenhum)",
          REGION_OLD.count("TOCFIX"), 0)


def test_extraccao_fiel_da_regiao_real() -> None:
    """Teste 1 do PLAN: escrever a fixture e re-le-la reproduz REGION_NEW
    exactamente (a escrita/leitura em disco nao corrompe nada); e uma
    re-extraccao independente do ficheiro real (nova leitura, from scratch)
    da' byte-a-byte o mesmo texto que REGION_NEW."""
    with tempfile.TemporaryDirectory() as td:
        p = _write_fixture(td, REGION_NEW)
        roundtrip = p.read_text(encoding="utf-8")
        check("roundtrip fixture == preambulo + REGION_NEW + stub",
              roundtrip, _PREAMBLE + REGION_NEW + _NEXT_FUNC_STUB)

    reextracted = _extract_region_from_real_lift()
    check("re-extraccao independente == REGION_NEW", reextracted, REGION_NEW)


def test_red_bug_medido_no_script_congelado() -> None:
    """Teste 0 (RED): o script tal como estava ANTES da correccao (snapshot
    congelado de git show HEAD, nao o PATCH_PATH mutavel) corre contra a
    fixture TOCFIX real, nao converte nada, e sai rc=0 na mesma -- o
    falso-verde medido em 03-CONTEXT.md, reproduzido por fixture."""
    with tempfile.TemporaryDirectory() as td:
        _write_fixture(td, REGION_NEW)
        frozen_script = Path(td) / "patch_14b1f0_opd_PRE_FIX_snapshot.py"
        frozen_script.write_text(OLD_SCRIPT_SRC, encoding="utf-8")
        result = subprocess.run(
            [PY, str(frozen_script), td],
            capture_output=True, text=True,
        )
        check("script congelado: rc", result.returncode, 0)
        check("script congelado: replaced pat1=0 pat2=0 (0 conversoes)",
              "replaced pat1=0 pat2=0" in result.stdout, True)
        check("script congelado: remaining indirect in region: 12",
              "remaining indirect in region: 12" in result.stdout, True)
        check("script congelado: sai OK (falso-verde)",
              "OK patch_14b1f0_opd" in result.stdout, True)


def _count_replaced(stdout: str) -> int:
    m = re.search(r"replaced pat1=(\d+) pat2=(\d+)", stdout)
    if not m:
        return -1
    return int(m.group(1)) + int(m.group(2))


def test_green1_convertido_regiao_nova() -> None:
    """GREEN 1: contra REGION_NEW (TOCFIX, formato actual), converte os 12
    sitios, remaining indirect fica 0, rc=0, veredicto CONVERTIDO."""
    with tempfile.TemporaryDirectory() as td:
        f = _write_fixture(td, REGION_NEW)
        result = subprocess.run(
            [PY, str(PATCH_PATH), td], capture_output=True, text=True,
        )
        check("GREEN1 rc", result.returncode, 0)
        check("GREEN1 pat1+pat2 == 12", _count_replaced(result.stdout), 12)
        check("GREEN1 remaining indirect in region: 0",
              "remaining indirect in region: 0" in result.stdout, True)
        check("GREEN1 veredicto CONVERTIDO",
              "OK patch_14b1f0_opd (CONVERTIDO)" in result.stdout, True)
        after = f.read_text(encoding="utf-8")
        check("GREEN1 ps3_call_opd(ctx, aparece 12x", after.count("ps3_call_opd(ctx,"), 12)


def test_green2_compat_regiao_antiga() -> None:
    """GREEN 2: contra REGION_OLD (vm_read64, formato antigo), converte
    igualmente os 12 sitios, reemitindo o restauro do TOC VERBATIM na forma
    antiga (nunca convertido para TOCFIX nem inventado)."""
    with tempfile.TemporaryDirectory() as td:
        f = _write_fixture(td, REGION_OLD)
        result = subprocess.run(
            [PY, str(PATCH_PATH), td], capture_output=True, text=True,
        )
        check("GREEN2 rc", result.returncode, 0)
        check("GREEN2 pat1+pat2 == 12", _count_replaced(result.stdout), 12)
        after = f.read_text(encoding="utf-8")
        check("GREEN2 ps3_call_opd(ctx, aparece 12x", after.count("ps3_call_opd(ctx,"), 12)
        check("GREEN2 vm_read64 reemitido verbatim 12x",
              after.count("vm_read64(ctx->gpr[1] + 0x28);"), 12)
        check("GREEN2 zero TOCFIX inventado", "TOCFIX" in after, False)


def test_green3_idempotencia_ate_terceira_corrida() -> None:
    """GREEN 3: uma segunda corrida sobre o resultado da primeira converte 0
    sitios NOVOS, reporta os 12 ja-aplicados, rc=0, veredicto JA-APLICADO --
    e uma terceira corrida produz um ficheiro byte-identico a' segunda
    (idempotencia real, nao so' 'nao piora')."""
    with tempfile.TemporaryDirectory() as td:
        f = _write_fixture(td, REGION_NEW)
        r1 = subprocess.run(
            [PY, str(PATCH_PATH), td], capture_output=True, text=True,
        )
        check("GREEN3 1a corrida rc", r1.returncode, 0)

        r2 = subprocess.run(
            [PY, str(PATCH_PATH), td], capture_output=True, text=True,
        )
        check("GREEN3 2a corrida rc", r2.returncode, 0)
        check("GREEN3 2a corrida 0 conversoes novas", _count_replaced(r2.stdout), 0)
        check("GREEN3 2a corrida veredicto JA-APLICADO",
              "JA-APLICADO" in r2.stdout, True)
        check("GREEN3 2a corrida reporta 12 sitios ja em ps3_call_opd",
              "12 sitios ja em ps3_call_opd" in r2.stdout, True)
        after2 = f.read_text(encoding="utf-8")

        r3 = subprocess.run(
            [PY, str(PATCH_PATH), td], capture_output=True, text=True,
        )
        after3 = f.read_text(encoding="utf-8")
        check("GREEN3 3a corrida rc", r3.returncode, 0)
        check("GREEN3 3a corrida byte-identica a' 2a (idempotencia por hash/conteudo)",
              after3, after2)


def test_green4_sem_efeito_rc3() -> None:
    """GREEN 4 (o coracao da correccao): fixture onde nem o formato antigo
    nem o TOCFIX nem ps3_call_opd pre-existente aparecem -- 0 conversoes, 0
    ja-aplicadas, tem de sair rc=3 (SEM-EFEITO), nunca rc=0."""
    region_sem_match = REGION_NEW.replace(
        "ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);",
        "chamada_hipotetica_num_terceiro_formato(ctx);",
    )
    check("fixture SEM-EFEITO: zero indirect+drain restantes",
          "ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);" in region_sem_match, False)
    check("fixture SEM-EFEITO: zero ps3_call_opd pre-existente",
          "ps3_call_opd(ctx," in region_sem_match, False)
    with tempfile.TemporaryDirectory() as td:
        f = _write_fixture(td, region_sem_match)
        result = subprocess.run(
            [PY, str(PATCH_PATH), td], capture_output=True, text=True,
        )
        check("GREEN4 rc == RC_NO_EFFECT (3)", result.returncode, 3)
        check("GREEN4 veredicto SEM-EFEITO", "SEM-EFEITO" in result.stdout, True)
        after = f.read_text(encoding="utf-8")
        check("GREEN4 nenhum ps3_call_opd foi criado", after.count("ps3_call_opd(ctx,"), 0)


def test_green5_e2e_copia_scratch_do_chunk_real() -> None:
    """GREEN 5 (prova mais forte que fixture): aplicado contra uma COPIA
    scratch (nunca o original) do chunk 000 real, o total de
    ps3_call_opd(ctx, nesse chunk sobe exactamente 12 -- e o ficheiro real
    de producao fica byte-a-byte intacto (hash antes/depois)."""
    import hashlib
    import shutil

    def sha256(path: Path) -> str:
        return hashlib.sha256(path.read_bytes()).hexdigest()

    hash_before = sha256(REAL_LIFT_000)
    with tempfile.TemporaryDirectory() as td:
        scratch = Path(td) / "ppu_recomp_000.cpp"
        shutil.copyfile(REAL_LIFT_000, scratch)
        before_count = scratch.read_text(encoding="utf-8", errors="replace").count(
            "ps3_call_opd(ctx,"
        )
        result = subprocess.run(
            [PY, str(PATCH_PATH), td], capture_output=True, text=True,
        )
        check("GREEN5 rc", result.returncode, 0)
        after_count = scratch.read_text(encoding="utf-8", errors="replace").count(
            "ps3_call_opd(ctx,"
        )
        check("GREEN5 delta == 12 no chunk 000 (copia scratch)",
              after_count - before_count, 12)
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
    print("[test_patch_14b1f0_opd] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
