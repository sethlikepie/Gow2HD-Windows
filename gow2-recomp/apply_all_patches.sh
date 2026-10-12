#!/usr/bin/env bash
# apply_all_patches.sh — reaplica TODOS os patches de lift sobre um diretorio de lift.
#
# Regra 4 do CLAUDE.md: fixes nos ppu_recomp_XXX.cpp (gitignored, regeneraveis) vivem
# como scripts idempotentes `recomp_mid_v2/patch_*.py` e DEVEM ser reaplicados apos
# cada re-lift. Este script e o catalogo unico dessa reaplicacao.
#
# Uso:
#   ./apply_all_patches.sh [DIR_DE_LIFT]                # aplica tudo (default: recomp_macos_v2)
#   ./apply_all_patches.sh --check [DIR]                # so verifica os marcadores, nao escreve
#   ./apply_all_patches.sh [DIR] --status ARQUIVO.tsv   # tambem escreve patch/status/classe em TSV
#   ./apply_all_patches.sh --print-order                # ordem efectiva de aplicacao, nao toca no lift
#
# ORDEM: declarada em lift_baseline/PATCH_DEPS.tsv (patch/depende_de/razao),
# resolvida por lift_baseline/order_patches.py (topo-sort estavel). O glob
# alfabetico nao serve -- 5 patches dependem de ancoras escritas por patches
# que vinham depois deles (medido 2026-08-03, vale 2 elos da cadeia de boot).
#
# Idempotente: a 2a corrida seguida deve deixar a arvore identica e reportar
# tudo como ALREADY-APPLIED. Com a ordem correcta isso vale ja' na 1a passagem --
# uma arvore que so' converge a 2a vez nunca chegou ao binario testado.
#
# Classificacao por patch em SEIS estados (D-4.1, 04-05-PLAN.md) -- a verdade
# vem da POS-CONDICAO em CONTRACTS.tsv (D-4.2), nunca do rc autodeclarado
# do patch quando ele nao escreve nada:
#   APPLIED         -> rc=0 e o conteudo dos ppu_recomp_*.cpp mudou
#   ALREADY-APPLIED -> rc=0, conteudo nao mudou, E a pos-condicao E VERDADE
#   NO-MATCH        -> rc=0, conteudo nao mudou, mas a pos-condicao E FALSA
#                       (nao casou nada -- NAO e verde, conta para o gate)
#   UNVERIFIED      -> rc=0, conteudo nao mudou, SEM contrato declarado para
#                       este patch (nao da para saber -- NAO e verde)
#   FAILED          -> rc!=0 (needle nao encontrada / shape do lift mudou)
#   SKIPPED         -> patch listado em SKIP_LIST (nao roda neste host)
#
# NO-MATCH e UNVERIFIED so contam para o rc final se o patch NAO for classe
# PROBE em PATCH_CATALOG.tsv (D-4.4) -- PROBE fica fora do gate, mas dentro
# do relatorio, com o sufixo "(PROBE, fora do gate)".
#
# Um FAILED nao e forcado: pode significar que o patch ficou obsoleto para o
# novo shape do lift. Investigar, nunca forcar.
# FAILED-PARTIAL = o script escreveu um chunk e so depois falhou noutro.
#
# No fim, chama verify_lift.sh (D-4.6) e incorpora o seu rc no calculo final.
#
# Saida: 0 se nada falhou (fora de PROBE) E os checks passaram E verify_lift.sh
# passou; 1 caso contrario. Historicamente os FAILED abaixo eram todos PROBE
# (diagnostico gated por env, OFF por default), nenhum altera o boot default:
#
#   patch_2b0fb4_trace.py    needle exige uma probe [WADLD-T1SZ] preexistente em
#                            func_002B0FB4 que NENHUM script instala (0 no lift
#                            fresco). Enhancement de 2a geracao orfao: a 1a
#                            geracao foi feita a mao numa sessao e nunca virou
#                            script. Insatisfazivel a partir de um lift limpo.
#   patch_factory_opd_gate.py  SUPERSEDED na parte funcional: os 2 OPD sites de
#                            func_0039E6B4 que ele existe para corrigir ja sao
#                            convertidos por patch_39e5d8_opd.py, que corre antes
#                            (indirect=0, call_opd=2 no lift). So faltam as probes
#                            [WADLD-FACT]/[WADLD-GATE].
#   patch_tymap_18e814.py    a metade do chunk 000 aplica-se toda (TYMAP-LK/HIT/
#                            RET/VT + 18E814 e 171244 -> ps3_call_opd). Falha so
#                            na cauda: a probe [TYMAP-REG] em func_00321034 (001)
#                            exige um bloco PS3_TRACE_SHREG preexistente — mesma
#                            classe de orfao do 2b0fb4_trace.
#   patch_tymap_probes2.py   conflito de ordem com patch_tydisp_lr.py: ambos
#                            reescrevem o MESMO fprintf [TYDISP] e sao variantes
#                            mutuamente exclusivas. tydisp_lr corre antes (t<y) e
#                            consome a needle; ficou a variante lr (lr/r3/r4) em
#                            vez da desc/raw/w1/w2. Alternativas, nao sequencia.

set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1
REPO="$PWD"
PATCH_DIR="${PS3_PATCH_DIR:-$REPO/recomp_mid_v2}"

MODE=apply
LIFT_ARG=""
STATUS_TSV=""
_want_status_path=0
for a in "$@"; do
  if [ "$_want_status_path" -eq 1 ]; then
    STATUS_TSV="$a"
    _want_status_path=0
    continue
  fi
  case "$a" in
    --check) MODE=check ;;
    --print-order) MODE=print-order ;;
    --status) _want_status_path=1 ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    -*) echo "opcao desconhecida: $a" >&2; exit 2 ;;
    *) LIFT_ARG="$a" ;;
  esac
done
if [ "$_want_status_path" -eq 1 ]; then
  echo "ERRO: --status precisa de um caminho" >&2
  exit 2
fi
if [ -n "$STATUS_TSV" ]; then
  printf 'patch\tstatus\tclasse\n' > "$STATUS_TSV"
fi

LIFT_REL="${LIFT_ARG:-recomp_macos_v2}"
LIFT="$REPO/${LIFT_REL#./}"
# --print-order so' inspecciona a ordem de aplicacao: nao le nem escreve o lift.
if [ "$MODE" != print-order ] && [ ! -d "$LIFT" ]; then
  echo "ERRO: dir de lift nao existe: $LIFT" >&2
  exit 2
fi

# ---- raiz do motor (ps3recomp) + catalogo de classes (D-4.1/D-4.4) -----------
# apply_all_patches.sh e' um script do irmao gow2-recomp, mas PATCH_CATALOG.tsv,
# CONTRACTS.tsv, check_contracts.py e verify_lift.sh (D-4.6) vivem no monorepo
# do motor -- PS3_ENGINE_ROOT e' a ponte entre os dois repos.
#
# NUNCA usar `declare -A` aqui: o /bin/bash de fabrica do macOS (3.2.57, sem
# homebrew bash em PATH) nao suporta array associativo -- `declare -A` falha
# com "invalid option" e um subscrito de string num array indexado implicito
# ($1 tratado como expressao aritmetica) explode com "invalid arithmetic
# operator". classe_de() faz lookup por linha via awk, portavel ao bash 3.2.
PS3_ENGINE_ROOT="${PS3_ENGINE_ROOT:-$REPO/../ps3recomp}"
CATALOG="${PS3_PATCH_CATALOG:-$PS3_ENGINE_ROOT/games/gow2/lift_baseline/PATCH_CATALOG.tsv}"
if [ ! -f "$CATALOG" ]; then
  echo "AVISO: PATCH_CATALOG.tsv nao encontrado ($CATALOG) -- todos os patches tratados como FUNCIONAL (nenhuma excecao PROBE)" >&2
fi

classe_de() {
  local name="$1" found
  if [ ! -f "$CATALOG" ]; then
    echo "FUNCIONAL"
    return
  fi
  found="$(awk -F'\t' -v p="$name" '$1==p{print $3; exit}' "$CATALOG")"
  if [ -n "$found" ]; then printf '%s\n' "$found"; else echo "FUNCIONAL"; fi
}

is_probe() { [ "$(classe_de "$1")" = "PROBE" ]; }

CONTRACTS="${PS3_CONTRACTS:-$PS3_ENGINE_ROOT/games/gow2/lift_baseline/CONTRACTS.tsv}"
CHECK_CONTRACTS="$PS3_ENGINE_ROOT/games/gow2/lift_baseline/check_contracts.py"

# ---- interprete python -------------------------------------------------------
# Os patch_*.py usam Path.write_text(..., newline="\n") — kwarg que so existe a
# partir do Python 3.10 (e que NAO deve ser removido: e ele que evita CRLF no
# lado Windows do port). O python3 do sistema no macOS e o 3.9.6 da Apple, que
# estoura TypeError na escrita DEPOIS de todo o trabalho em memoria. Escolhemos
# aqui um interprete >= 3.10 em vez de mexer nos 22 scripts.
pick_python() {
  local c
  for c in python3.14 python3.13 python3.12 python3.11 python3.10 python3 \
           /opt/homebrew/bin/python3 /usr/local/bin/python3; do
    command -v "$c" >/dev/null 2>&1 || continue
    if "$c" -c 'import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)' 2>/dev/null; then
      command -v "$c"
      return 0
    fi
  done
  return 1
}
PY="${PS3_PATCH_PYTHON:-$(pick_python)}"
if [ -z "$PY" ]; then
  echo "ERRO: nenhum python3 >= 3.10 encontrado (os patches usam" >&2
  echo "      Path.write_text(newline=...)). Instale um, ou aponte PS3_PATCH_PYTHON." >&2
  exit 2
fi

# ---- ORDEM DE APLICACAO (declarativa, PATCH_DEPS.tsv) ------------------------
# O glob alfabetico NAO e' uma ordem valida: medido em 2026-08-03
# (games/gow2/notes/2026-08-03-seis-patches-que-nao-reaplicam.md), cinco dos seis
# patches que nao reaplicam a um lift fresco falham SO' porque a agulha deles e'
# texto que outro patch escreve mais tarde no alfabeto -- e o rebuild medido em
# 2026-08-03-rebuild-e2e-medicao.md provou que tres dessas relacoes valem DOIS
# ELOS da cadeia de boot (elo 2 -> elo 4). A 2a passagem do script curava-as por
# acidente, o que e' precisamente o buraco de gate da perna 3 do accept_relift.sh:
# uma arvore que so' converge a segunda vez nunca chegou ao binario testado.
#
# A ordem vive DECLARADA em lift_baseline/PATCH_DEPS.tsv (patch/depende_de/razao)
# -- um sitio obvio, uma linha por relacao -- e e' resolvida por
# lift_baseline/order_patches.py com topo-sort ESTAVEL: sem relacoes a ordem e'
# exactamente a do glob; cada relacao move o minimo necessario.
#
# Inspeccionar:  ./apply_all_patches.sh --print-order
PATCH_DEPS="${PS3_PATCH_DEPS:-$PS3_ENGINE_ROOT/games/gow2/lift_baseline/PATCH_DEPS.tsv}"
ORDER_TOOL="${PS3_ORDER_PATCHES:-$PS3_ENGINE_ROOT/games/gow2/lift_baseline/order_patches.py}"

PATCH_NAMES_GLOB="$(for _p in "$PATCH_DIR"/patch_*.py; do basename "$_p"; done)"
if [ -f "$PATCH_DEPS" ] && [ -f "$ORDER_TOOL" ]; then
  PATCH_NAMES="$(printf '%s\n' "$PATCH_NAMES_GLOB" | "$PY" "$ORDER_TOOL" --deps "$PATCH_DEPS")"
  order_rc=$?
  if [ "$order_rc" -ne 0 ]; then
    # Ciclo declarado: nunca escolher um lado em silencio (rc=1 do order_patches).
    echo "ERRO: order_patches.py falhou (rc=$order_rc) -- ordem de aplicacao indefinida, nao vou aplicar nada" >&2
    exit 2
  fi
else
  echo "AVISO: PATCH_DEPS.tsv ou order_patches.py ausentes ($PATCH_DEPS / $ORDER_TOOL)" >&2
  echo "       -- a cair para a ordem do glob, que e' comprovadamente errada para 5 patches." >&2
  PATCH_NAMES="$PATCH_NAMES_GLOB"
fi

if [ "$MODE" = print-order ]; then
  printf '%s\n' "$PATCH_NAMES"
  exit 0
fi

# ---- hash helper (macOS md5 / Linux md5sum / fallback shasum) ----------------
if command -v md5 >/dev/null 2>&1; then
  hash_file() { md5 -q "$1"; }
elif command -v md5sum >/dev/null 2>&1; then
  hash_file() { md5sum "$1" | cut -d' ' -f1; }
else
  hash_file() { shasum -a 1 "$1" | cut -d' ' -f1; }
fi

# ---- patches nao aplicaveis a este host --------------------------------------
# host_wad_tex + patch_wad_tex_capture: reactivados no Darwin (2026-07-22).
# build_macos.sh compila host_wad_tex.c e linka; force-bind via rsx_host_content.
# WAD ~tex packages apos WADLD-T1R → [WADTEX] + unit 1+ bind (nao e aceite de
# menu natural, mas desbloqueia pixels WAD no path de bind).
SKIP_LIST=""

is_skipped() {
  case " $SKIP_LIST " in *" $1 "*) return 0 ;; *) return 1 ;; esac
}

# Arquivos vigiados = uniao dos alvos citados pelos proprios patches (auto-mantido).
TRACKED="$(grep -ohE 'ppu_recomp_[0-9]+\.(cpp|h)' "$PATCH_DIR"/patch_*.py | sort -u)"

snapshot() {
  local f
  for f in $TRACKED; do
    if [ -f "$LIFT/$f" ]; then printf '%s %s\n' "$f" "$(hash_file "$LIFT/$f")"; fi
  done
}

# ---- verificacao de marcadores (o "teste") ----------------------------------
# Falha (rc!=0) enquanto os fixes NAO estiverem no lift. E o vermelho do TDD.
run_checks() {
  "$PY" - "$LIFT" <<'PY'
import sys, pathlib

lift = pathlib.Path(sys.argv[1])
fails = []

def src(name):
    return (lift / name).read_text(encoding="utf-8", errors="replace")

def report(name, ok, detail=""):
    print("  [%s] %s%s" % ("PASS" if ok else "FAIL", name, "" if ok else "\n         -> " + detail))
    if not ok:
        fails.append(name)

# 1) Parede [C]: o epilogo de func_002550C8 cai no fallthrough guest 0x255178
#    (fase fill/next), NUNCA em 0x2550E8 (reentrada do corpo do grow).
#    Sem isto o stream do R_PermA congela em 5.619.712 de 20.169.344 bytes.
try:
    s = src("ppu_recomp_000.cpp")
    i = s.find("void func_002550C8")
    j = s.find("void func_00255178", i)
    if i < 0 or j < 0:
        report("wall-C fallthrough 2550C8->255178", False,
               "func_002550C8 ou func_00255178 nao existem no lift")
    else:
        region = s[i:j]
        bad = "g_trampoline_fn = (void(*)(void*))func_002550E8; return;" in region
        good = "g_trampoline_fn = (void(*)(void*))func_00255178; return;" in region
        report("wall-C fallthrough 2550C8->255178", (not bad) and good,
               "epilogo ainda aponta para func_002550E8 (lift nao-patcheado)"
               if bad else "nao ha trampolim para func_00255178 no corpo de 2550C8")
except Exception as e:
    report("wall-C fallthrough 2550C8->255178", False, "erro: %r" % (e,))

# 2) Stream-reader OPD de func_001856A8 (SHADERSRC/ICGLdr): sem isto o
#    SHADERSRC le sempre N=0 (chama a OPD como se fosse codigo cru).
try:
    s = src("ppu_recomp_000.cpp")
    i = s.find("void func_001856A8")
    if i < 0:
        report("1856A8 stream OPD (SHADERSRC N>0)", False, "func_001856A8 ausente no lift")
    else:
        j = s.find("void func_", i + 10)
        region = s[i:j]
        report("1856A8 stream OPD (SHADERSRC N>0)",
               "ps3_call_opd(ctx, (uint32_t)ctx->gpr[9])" in region,
               "ainda usa ps3_indirect_call (OPD tratada como codigo cru)")
except Exception as e:
    report("1856A8 stream OPD (SHADERSRC N>0)", False, "erro: %r" % (e,))

# 3) Declaracao do helper de OPD presente (pre-requisito dos patches de OPD).
try:
    s = src("ppu_recomp_000.cpp")
    report("decl ps3_call_opd em 000",
           "void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);" in s[:80000],
           "declaracao ausente no topo de ppu_recomp_000.cpp")
except Exception as e:
    report("decl ps3_call_opd em 000", False, "erro: %r" % (e,))

# 4) `bctr` (salto, SEM link) NAO pode virar chamada host: era isso que fazia o
#    pump da FSM da intro (func_002C0508, jump table em 0x002C0594) crescer uma
#    frame host por iteracao ate' ao tecto de recursao de 4000, matando a sonda
#    de [op+0x90] antes de a "fios scheduler" completar o open (16/16 boots).
try:
    bad = []
    for name in ("ppu_recomp_000.cpp", "ppu_recomp_001.cpp", "ppu_recomp_002.cpp",
                 "ppu_recomp_003.cpp", "ppu_recomp_004.cpp", "ppu_recomp_005.cpp",
                 "ppu_recomp_006.cpp"):
        try:
            s = src(name)
        except FileNotFoundError:
            continue
        if "ps3_indirect_call(ctx); return;" in s:
            bad.append(name)
        elif "ps3_indirect_tail" not in s:
            bad.append(name + "(sem decl)")
    report("bctr como salto (ps3_indirect_tail)", not bad,
           "ainda em chamada host: " + ", ".join(bad))
except Exception as e:
    report("bctr como salto (ps3_indirect_tail)", False, "erro: %r" % (e,))

# 5) Sticky do done-word FIOS (patch_fios_sticky.py). O open completa e o produtor
#    (func_00306534) grava [op+0x90]=1, mas sob o giant lock unico o escalonador
#    corre todo o complete (e um clear) antes de o poll do estado 1 (func_002B4224)
#    ver done!=0 -> a FSM da intro fica presa em st620==1. O sticky publica/re-
#    materializa/consome a palavra para fechar essa janela. As 4 insercoes vivem
#    no lift; a metade de runtime (ps3_fios_sticky_*) ja' esta' em ppu_loader.cpp.
try:
    s = src("ppu_recomp_001.cpp")
    need = [
        ('decl publish',   'extern "C" void ps3_fios_sticky_publish(uint32_t op);'),
        ('STICKY-RESTORE (poll do estado 1, func_002B4224)', 'STICKY-RESTORE'),
        ('STICKY-CONSUME (ramo done, func_002B4274)',        'STICKY-CONSUME'),
        ('publish caminho A (produtor, func_00306534)',
         'if (((uint32_t)ctx->gpr[11]) != 0u)\n'
         '            ps3_fios_sticky_publish((uint32_t)ctx->gpr[31]);'),
    ]
    missing = [n for n, sub in need if sub not in s]
    report("FIOS sticky done-word (4 insercoes)", not missing,
           "faltam no lift: " + ", ".join(missing))
except Exception as e:
    report("FIOS sticky done-word (4 insercoes)", False, "erro: %r" % (e,))

# 6) Parede [4]: poda por subtag no outer walk (patch_outer_subtag_prune.py).
#    Sem isto o walk le' matrix[0][3] (um float 0.0f) como cabeca de lista
#    ligada e o loop nao termina: no_rep medido em 179.459.342 iteracoes.
#    Com o fix, no_rep = 0 em 3 de 3 corridas (ver docs/re_sessions/
#    2026-08-04-E5-aceite-parede-4.md).
try:
    n = src("ppu_recomp_001.cpp").count("Fase 11-03 FIX v2")
    report("parede 4: poda por subtag no outer walk (1 sitio)", n == 1,
           "esperado 1 ocorrencia em ppu_recomp_001.cpp, encontrado %d%s" % (
               n, " -- fix AUSENTE do lift" if n == 0 else
                  " -- lift CONTAMINADO (catalogo aplicado mais de uma vez)"))
except Exception as e:
    report("parede 4: poda por subtag no outer walk (1 sitio)", False, "erro: %r" % (e,))

# 7) Parede [1]: skip por subtag no dispatch de func_0024E1E8
#    (patch_24e1e8_wall1_subtag_skip.py). Sem isto o boot aborta com
#    "[ppu] FATAL: stuck calling 0x00514E80" -- medido em 6 de 6 corridas.
#    Com o fix o FATAL desaparece em 0 de 6 (ver 2026-08-05-E7-*.md).
#
#    A contagem e' EXACTA e por ficheiro, de proposito. Um `>= 1` passaria
#    numa copia contaminada: em 2026-08-05 o recomp_macos_e6fix chegou a 16
#    marcadores (o dobro) por o catalogo ter sido aplicado varias vezes sobre
#    a mesma copia de trabalho, e nenhum teste deu conta. Se um re-lift mudar
#    o numero de sitios, este check falha alto -- e' o comportamento desejado:
#    alguem tem de reconferir e actualizar a contagem com medicao.
try:
    want = {"ppu_recomp_000.cpp": 1, "ppu_recomp_002.cpp": 5, "ppu_recomp_004.cpp": 2}
    got = {f: src(f).count("PAREDE1-E6-SUBTAG-SKIP-FIX") for f in want}
    total = sum(got.values())
    ok = got == want
    if total == 0:
        why = "fix AUSENTE do lift"
    elif total > sum(want.values()):
        why = "lift CONTAMINADO (catalogo aplicado mais de uma vez)"
    else:
        why = "distribuicao inesperada"
    report("parede 1: skip por subtag em 0024E1E8 (8 sitios)", ok,
           "esperado %r, encontrado %r (total %d) -- %s" % (want, got, total, why))
except Exception as e:
    report("parede 1: skip por subtag em 0024E1E8 (8 sitios)", False, "erro: %r" % (e,))

print()
if fails:
    print("CHECKS: %d FALHA(S) -> %s" % (len(fails), ", ".join(fails)))
    sys.exit(1)
print("CHECKS: todos passaram")
PY
}

echo "=============================================================="
echo " apply_all_patches.sh"
echo " lift    : $LIFT"
echo " patches : $PATCH_DIR/patch_*.py"
echo " modo    : $MODE"
echo "=============================================================="
echo

if [ "$MODE" = check ]; then
  echo "-- verificacao de marcadores --"
  run_checks
  exit $?
fi

n_applied=0
n_already=0
n_failed=0
n_skipped=0
n_nomatch=0
n_unverified=0
n_failed_gate=0
n_nomatch_gate=0
n_unverified_gate=0
failed_list=""
nomatch_list=""
unverified_list=""

for name in $PATCH_NAMES; do
  p="$PATCH_DIR/$name"
  if is_skipped "$name"; then
    printf '%-16s %s\n' "SKIPPED" "$name"
    echo "                   | host helper so existe no backend D3D12 (Windows); nao linka no macOS"
    n_skipped=$((n_skipped + 1))
    continue
  fi
  before="$(snapshot)"
  out="$("$PY" "$p" "$LIFT" 2>&1)"
  rc=$?
  after="$(snapshot)"

  if [ "$rc" -ne 0 ]; then
    # Varios patches escrevem um chunk e so depois falham noutro: distinguir a
    # falha limpa (nada escrito) da parcial (parte do patch ja esta no lift).
    if [ "$before" != "$after" ]; then
      status="FAILED-PARTIAL"
    else
      status="FAILED"
    fi
    n_failed=$((n_failed + 1))
    failed_list="$failed_list $name"
  elif [ "$before" != "$after" ]; then
    status="APPLIED"
    n_applied=$((n_applied + 1))
  else
    # rc==0 e conteudo inalterado: DEIXA de ser automaticamente ALREADY-APPLIED
    # (D-4.1/D-4.2) -- a verdade vem da POS-CONDICAO declarada em CONTRACTS.tsv,
    # verificada de forma independente do que o patch imprimiu, nunca do rc dele.
    contract_out="$("$PY" "$CHECK_CONTRACTS" "$CONTRACTS" "$LIFT" --patch "$name" 2>&1)"
    contract_rc=$?
    case "$contract_rc" in
      0) status="ALREADY-APPLIED"; n_already=$((n_already + 1)) ;;
      1) status="NO-MATCH"; n_nomatch=$((n_nomatch + 1)) ;;
      2) status="UNVERIFIED"; n_unverified=$((n_unverified + 1)) ;;
      *) status="UNVERIFIED"; n_unverified=$((n_unverified + 1)) ;;
    esac
    out="$out
$contract_out"
  fi

  probe_suffix=""
  if is_probe "$name"; then
    probe_suffix=" (PROBE, fora do gate)"
  else
    case "$status" in
      FAILED|FAILED-PARTIAL) n_failed_gate=$((n_failed_gate + 1)) ;;
      NO-MATCH) n_nomatch_gate=$((n_nomatch_gate + 1)); nomatch_list="$nomatch_list $name" ;;
      UNVERIFIED) n_unverified_gate=$((n_unverified_gate + 1)); unverified_list="$unverified_list $name" ;;
    esac
  fi
  if [ -n "$STATUS_TSV" ]; then
    printf '%s\t%s\t%s\n' "$name" "$status" "$(classe_de "$name")" >> "$STATUS_TSV"
  fi

  printf '%-16s %s%s\n' "$status" "$name" "$probe_suffix"
  # Detalhe do script so quando interessa (falha) ou quando mudou algo.
  if [ "$rc" -ne 0 ] || [ "$status" = "APPLIED" ]; then
    printf '%s\n' "$out" | sed 's/^/                   | /'
  fi
done

echo
echo "--------------------------------------------------------------"
printf 'TOTAL: %d patches | APPLIED=%d ALREADY-APPLIED=%d NO-MATCH=%d UNVERIFIED=%d FAILED=%d SKIPPED=%d\n' \
  "$((n_applied + n_already + n_nomatch + n_unverified + n_failed + n_skipped))" \
  "$n_applied" "$n_already" "$n_nomatch" "$n_unverified" "$n_failed" "$n_skipped"
if [ -n "$failed_list" ]; then
  echo "FALHARAM:$failed_list"
  echo "(NAO forcar: pode ser patch obsoleto para o novo shape do lift)"
fi
if [ -n "$nomatch_list" ]; then
  echo "NO-MATCH (pos-condicao falsa, D-4.2):$nomatch_list"
fi
if [ -n "$unverified_list" ]; then
  echo "UNVERIFIED (sem contrato declarado):$unverified_list"
fi
echo "--------------------------------------------------------------"
echo
echo "-- verificacao de marcadores --"
run_checks
checks_rc=$?

echo
echo "-- verify_lift.sh (D-4.6) --"
VERIFY_LIFT="$PS3_ENGINE_ROOT/games/gow2/verify_lift.sh"
if [ -x "$VERIFY_LIFT" ]; then
  "$VERIFY_LIFT" "$LIFT"
  verify_lift_rc=$?
else
  echo "AVISO: verify_lift.sh nao encontrado ou nao executavel ($VERIFY_LIFT) -- gate D-4.6 nao verificado" >&2
  verify_lift_rc=2
fi

echo
echo "-- UNVERIFIED por DELTA (nao por contagem absoluta) --"
# Porque isto existe: o rc global somava n_unverified_gate por contagem ABSOLUTA,
# ao contrario dos tres checks do verify_lift.sh, que usam delta (D-4.5). Logo o
# exit code ficava 1 PARA SEMPRE ate' o CONTRACTS.tsv ter cobertura total, mesmo
# com zero regressoes novas -- e um gate cronicamente vermelho e' um gate que
# ninguem le'. Foi o mesmo defeito de limiar absoluto que este projecto ja'
# corrigiu tres vezes: no PREAMBLE ps3_indirect_call (falhava quando o lift
# MELHOROU), no grupo opd-dispatch (somava lixo do lift antigo com codigo
# recuperado) e nos contadores 56072/56223 (do lift antigo).
# Apontado pelo verificador da Fase 4 em 2026-07-29.
#
# Agora: um UNVERIFIED NOVO (patch novo sem contrato, ou patch que perdeu o
# contrato) FALHA. Menos UNVERIFIED (contrato acrescentado) NUNCA falha.
UNVER_BASELINE="$PS3_ENGINE_ROOT/games/gow2/lift_baseline/UNVERIFIED_BASELINE.json"
DELTA_TOOL="$PS3_ENGINE_ROOT/games/gow2/lift_baseline/baseline_delta.py"
unverified_rc=0
if [ -f "$UNVER_BASELINE" ] && [ -f "$DELTA_TOOL" ]; then
  _unv_cur="$(mktemp -t unver_cur)"
  {
    printf '{"ids": ['
    _first=1
    for _n in $unverified_list; do
      [ "$_first" = 1 ] || printf ', '
      printf '"%s"' "$_n"
      _first=0
    done
    printf ']}\n'
  } > "$_unv_cur"
  "$PY" "$DELTA_TOOL" "$_unv_cur" "$UNVER_BASELINE" --label UNVERIFIED
  unverified_rc=$?
  rm -f "$_unv_cur"
else
  echo "AVISO: baseline de UNVERIFIED ausente ($UNVER_BASELINE) -- a cair para" >&2
  echo "       contagem absoluta, que fica vermelha enquanto houver divida." >&2
  unverified_rc=$([ "$n_unverified_gate" -ne 0 ] && echo 1 || echo 0)
fi

if [ "$n_failed_gate" -ne 0 ] || [ "$n_nomatch_gate" -ne 0 ] || [ "$unverified_rc" -ne 0 ] || [ "$checks_rc" -ne 0 ] || [ "$verify_lift_rc" -ne 0 ]; then
  exit 1
fi
exit 0
