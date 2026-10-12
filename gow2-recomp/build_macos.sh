#!/usr/bin/env bash
# Build the God of War II HD native macOS/arm64 host (phase F4 of
# ps3recomp/docs/MACOS_PORT_PLAN.md). The POSIX counterpart of build_d3d.sh.
#
# Assumes the lift already ran:
#   python ../ps3recomp/tools/ppu_lifter.py EBOOT.ELF --functions functions.json \
#          --output recomp_macos
#
# Usage: ./build_macos.sh [lift-dir]        (default: recomp_macos_v2)
#
# Opt levels / output (perf A/B):
#   LIFT_OPT=-O0|-O1|-O2|-O3|-Os  optimization for lifted ppu_recomp_*.cpp (default -O1)
#   LIFT_CFLAGS='...'         extra clang++ flags for lift TUs only
#                             (PGO use: -fprofile-use=file.profdata)
#   LINK_CFLAGS='...'         extra flags on the final link (PGO gen: -fprofile-generate)
#   LIFT_OBJ_TAG=tag          extra object suffix so PGO gen/use coexist with -O1
#                             (ppu_recomp_000.cpp.1.tag.o)
#   HOST_OPT=-O0|-O1|-O2|-O3|-Os  host/runtime objects + link (default -O2; SPU stays -O1).
#                             -O0 continua disponivel para depurar.
#   OUT=/path/to/boot_gow2    binary path (default $HERE/boot_gow2)
#   FORCE_REBUILD_LIFT=1      ignore stale .o and rebuild all lift chunks
#   RELIFT=1  regenera o lift a partir do EBOOT.ELF/functions.json num
#             directorio NOVO (default recomp_macos_v3) antes de compilar;
#             NUNCA escreve em recomp_macos_v2 (aborta se o alvo resolver
#             para la)
#
# Examples:
#   ./build_macos.sh
#   LIFT_OPT=-O1 OUT=./boot_gow2_O1 ./build_macos.sh
#   LIFT_OPT=-O1 HOST_OPT=-O1 FORCE_REBUILD_LIFT=1 OUT=./boot_gow2_O1 ./build_macos.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# Raiz do motor. Override por PS3_ENGINE_ROOT para compilar contra uma worktree
# git do ps3recomp em vez do checkout ao lado -- necessario quando duas sessoes
# partilham o checkout principal e cada uma tem o seu branch. Sem a variavel o
# comportamento e' exactamente o de antes.
PS3="${PS3_ENGINE_ROOT:-$HERE/../ps3recomp}"
# Python das ferramentas: PY, senao o .venv do motor (checkout de dev), senao
# python3. O motor do kit de release nao traz .venv.
PYBIN="${PY:-$PS3/.venv/bin/python}"
[ -x "$PYBIN" ] || PYBIN="$(command -v python3)"
# Keep the Metal ring-fence fix reproducible across runtime rebuilds. The patch
# is idempotent and preserves PS3_METAL_FRAME_FENCE=0 as an explicit unsafe A/B.
"$PYBIN" "$HERE/recomp_mid_v2/patch_metal_frame_fence_default.py"
"$PYBIN" "$HERE/recomp_mid_v2/patch_metal_varace_underflow.py"
# The Xcode clang selected on this host does not infer the active SDK when it
# is invoked directly.  Without SDKROOT every lifted C++ TU fails at the first
# standard-library include (for example, <atomic>).  Keep an explicitly
# supplied SDKROOT intact, but make the documented macOS build self-contained.
if [ "$(uname -s)" = "Darwin" ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT="$(xcrun --show-sdk-path)"
    export SDKROOT
fi
# Default recomp_macos_v2: e o lift em que o apply_all_patches.sh opera (tambem
# tem esse default). recomp_macos e' um lift ANTIGO sem os patches da sessao --
# nomeadamente sem o ps3_indirect_tail (fix do bctr), sem o qual o pump da intro
# bate na recursao de host. Compilar o lift errado dava um boot sem os fixes.
LIFT="${1:-$HERE/recomp_macos_v2}"
OUT="${OUT:-$HERE/boot_gow2}"
LIFT_OPT="${LIFT_OPT:--O1}"
LIFT_CFLAGS="${LIFT_CFLAGS:-}"
LINK_CFLAGS="${LINK_CFLAGS:-}"
# Flags extra so' para os objectos HOST/runtime (nao para os chunks liftados,
# que sao enormes). Serve para o ThreadSanitizer: instrumentar o runtime e o
# HLE, que e' onde vivem as estruturas partilhadas entre threads guest, sem
# pagar o custo de instrumentar 50 000 funcoes liftadas.
HOST_CFLAGS="${HOST_CFLAGS:-}"
LIFT_OBJ_TAG="${LIFT_OBJ_TAG:-}"
HOST_OPT="${HOST_OPT:--O2}"
# Imagens SPU liftadas: a computacao SPU domina o perfil do gameplay, entao o
# nivel delas e' um botao proprio (era -O1 fixo).
SPU_OPT="${SPU_OPT:--O1}"
# Atomicos LSE do Apple Silicon: sem -mcpu, o Clang emite todo CAS como laco
# ldaxr/stlxr; com ele vira casal/swpal numa instrucao. Toca o stwcx. inteiro
# (ppu_res_stwcx), o spinlock de reserva e o lockline do SPU.
# PS3_MCPU=  (vazio) desliga, para A/B.
MCPU="${PS3_MCPU--mcpu=apple-m1}"
FORCE_REBUILD_LIFT="${FORCE_REBUILD_LIFT:-0}"

# RELIFT=1: regenera o lift a partir do EBOOT.ELF/functions.json num
# directorio SEMPRE novo (nunca recomp_macos_v2, que e' produção) antes de
# seguir para as seccoes normais de compilacao/link. Sob RELIFT=1 o
# positional $1 passa a significar "directorio NOVO a criar" em vez de
# "directorio existente a compilar" -- essa mudanca de significado so' se
# aplica dentro deste ramo condicional (D-3.5, 03-CONTEXT.md).
if [ "${RELIFT:-0}" = "1" ]; then
    LIFT="${1:-$HERE/recomp_macos_v3}"

    # Guarda anti-v2 (D-3.5): resolve o caminho absoluto canonico do alvo
    # ANTES de qualquer mkdir/escrita e aborta se resolver para
    # recomp_macos_v2. dirname existe sempre, mesmo que $LIFT ainda nao
    # exista -- por isso o resolve funciona mesmo em directorio novo.
    _lift_parent="$(cd "$(dirname "$LIFT")" 2>/dev/null && pwd)"
    _lift_abs="$_lift_parent/$(basename "$LIFT")"
    _v2_abs="$HERE/recomp_macos_v2"
    if [ "$_lift_abs" = "$_v2_abs" ]; then
        echo "RELIFT=1 nunca escreve em recomp_macos_v2 (produção) -- escolha outro directorio de saida" >&2
        exit 1
    fi

    EBOOT="${PS3_EBOOT:-$HERE/EBOOT.ELF}"
    FUNCS="${PS3_FUNCTIONS_JSON:-$HERE/functions.json}"
    if [ ! -f "$EBOOT" ]; then
        echo "RELIFT=1: EBOOT nao encontrado em $EBOOT (defina PS3_EBOOT)" >&2
        exit 1
    fi
    if [ ! -f "$FUNCS" ]; then
        echo "RELIFT=1: functions.json nao encontrado em $FUNCS (defina PS3_FUNCTIONS_JSON)" >&2
        exit 1
    fi

    # --config: sem ele o lifter corre "como antes" e NAO emite os mid-asm
    # hooks -- ou seja, um RELIFT=1 produzia um lift SEM o wait-idle do CE03C
    # e o build seguinte compilava um gow2_midasm_hooks.o que ninguem chama
    # (medido no plano 17-03: e' o bug que faz o piloto parecer migrado sem o
    # estar). PS3_RECOMP_CONFIG permite apontar para outro TOML; vazio ou
    # ficheiro ausente = comportamento legado, sem hooks.
    RECOMP_CFG="${PS3_RECOMP_CONFIG:-$HERE/config/gow2_recomp.toml}"
    CFG_ARGS=()
    if [ -f "$RECOMP_CFG" ]; then
        CFG_ARGS=(--config "$RECOMP_CFG")
        echo "  RELIFT: --config $RECOMP_CFG"
    else
        echo "  RELIFT: sem --config (nao existe $RECOMP_CFG) -- lift SEM mid-asm hooks"
    fi

    mkdir -p "$LIFT"
    echo "=== 0. RELIFT=1: regenerando lift em $LIFT (a partir de $EBOOT) ==="
    python3 "$PS3/tools/ppu_lifter.py" "$EBOOT" --functions "$FUNCS" \
        ${CFG_ARGS[@]+"${CFG_ARGS[@]}"} -o "$LIFT" -j 4
    _relift_rc=$?
    if [ "$_relift_rc" != "0" ]; then
        exit "$_relift_rc"
    fi
fi

case "$LIFT_OPT" in -O0|-O1|-O2|-O3|-Os) ;; *)
    echo "LIFT_OPT must be -O0|-O1|-O2|-O3|-Os (got '$LIFT_OPT')" >&2; exit 1 ;;
esac
case "$HOST_OPT" in -O0|-O1|-O2|-O3|-Os) ;; *)
    echo "HOST_OPT must be -O0|-O1|-O2|-O3|-Os (got '$HOST_OPT')" >&2; exit 1 ;;
esac
case "$LIFT_OBJ_TAG" in
    ""|[A-Za-z0-9][A-Za-z0-9._-]*) ;;
    *) echo "LIFT_OBJ_TAG must be empty or [A-Za-z0-9][A-Za-z0-9._-]* (got '$LIFT_OBJ_TAG')" >&2; exit 1 ;;
esac

if [ ! -f "$LIFT/ppu_recomp.h" ]; then
    echo "no lift output in $LIFT -- run ppu_lifter.py first" >&2
    exit 1
fi

RUNTIME_LIB="${RUNTIME_LIB:-$PS3/build-macos/libps3recomp_runtime.a}"
if [ ! -f "$RUNTIME_LIB" ]; then
    echo "runtime library missing; build it first:" >&2
    echo "  cmake -B $PS3/build-macos -G Ninja -DCMAKE_BUILD_TYPE=Release $PS3" >&2
    echo "  cmake --build $PS3/build-macos" >&2
    exit 1
fi

INC=(-I "$LIFT"
     -I "$PS3/include"
     -I "$PS3/runtime/ppu"
     -I "$PS3/runtime/syscalls"
     -I "$PS3/runtime/spu"
     -I "$PS3/runtime/prx"
     -I "$PS3/runtime/memory"
     -I "$PS3/libs/system" -I "$PS3/libs/spurs" -I "$PS3/libs/sync"
     -I "$PS3/libs/video"  -I "$PS3/libs/audio" -I "$PS3/libs/network"
     -I "$PS3/libs/codec")

JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
JOBS=$(( JOBS > 6 ? 6 : JOBS ))   # each chunk peaks near 1 GB of compiler RSS

# Tag objects by lift opt so -O0 and -O1 can coexist without clobbering each
# other when A/B benchmarking. Default -O0 keeps historical bare *.cpp.o names
# (no mass rebuild of existing trees). Non-default: ppu_recomp_NNN.cpp.1.o
lift_obj() {
    local f=$1
    local o
    if [ "$LIFT_OPT" = "-O0" ]; then
        o="$f.o"
    else
        o="${f}.${LIFT_OPT#-O}.o"
    fi
    if [ -n "$LIFT_OBJ_TAG" ]; then
        echo "${o%.o}.${LIFT_OBJ_TAG}.o"
    else
        echo "$o"
    fi
}

echo "=== 1. lifted chunks -> .o  LIFT_OPT=$LIFT_OPT HOST_OPT=$HOST_OPT OUT=$OUT tag=${LIFT_OBJ_TAG:-none} (-P $JOBS) ==="
echo "  lift=$LIFT"
cd "$LIFT"
t0=$(date +%s)
# Only rebuild chunks whose object is missing or stale. FORCE_REBUILD_LIFT=1
# rebuilds every chunk (needed when switching LIFT_OPT with shared bare .o
# names, or after patch re-apply without mtime bump).
{
    for f in ppu_recomp_*.cpp ppu_stubs.cpp; do
        [ -f "$f" ] || continue
        o=$(lift_obj "$f")
        if [ "$FORCE_REBUILD_LIFT" = "1" ] || [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
            echo "$f"
        fi
    done
} | LIFT_CFLAGS="$LIFT_CFLAGS" LIFT_OBJ_TAG="$LIFT_OBJ_TAG" xargs -P "$JOBS" -I {} sh -c \
    'src="$1"; ps3="$2"; opt="$3"
     if [ "$opt" = "-O0" ]; then o="$src.o"; else o="${src}.${opt#-O}.o"; fi
     if [ -n "$LIFT_OBJ_TAG" ]; then o="${o%.o}.${LIFT_OBJ_TAG}.o"; fi
     clang++ -std=c++20 "$opt" $MCPU $LIFT_CFLAGS -w -c -I . -I "$ps3/include" -I "$ps3/runtime/ppu" \
         "$src" -o "$o" 2> "$src.cclog"' \
    _ {} "$PS3" "$LIFT_OPT"
NOBJ=0
for f in ppu_recomp_*.cpp ppu_stubs.cpp; do
    [ -f "$f" ] || continue
    o=$(lift_obj "$f")
    [ -f "$o" ] && NOBJ=$((NOBJ + 1))
done
echo "  dur=$(( $(date +%s) - t0 ))s objs=$NOBJ errors=$(cat ./*.cclog 2>/dev/null | grep -c 'error:' || true)"

echo "=== 2. runtime PPU sources -> .o (HOST_OPT=$HOST_OPT) ==="
cd "$HERE"
for src in ppu_loader ppu_imports ppu_hle ppu_sysprx ppu_fs; do
    clang++ -std=c++20 $HOST_OPT $MCPU $HOST_CFLAGS -w -c "${INC[@]}" "$PS3/runtime/ppu/$src.cpp" -o "$LIFT/$src.o"
done
clang -std=c11 $HOST_OPT $MCPU $HOST_CFLAGS -w -c "${INC[@]}" "$PS3/runtime/ppu/ppu_icall_ascii.c" -o "$LIFT/ppu_icall_ascii.o"
clang -std=c11 $HOST_OPT $MCPU $HOST_CFLAGS -w -c "${INC[@]}" "$PS3/runtime/ppu/ppu_vm_fast_policy.c" -o "$LIFT/ppu_vm_fast_policy.o"
clang -std=c11 $HOST_OPT $MCPU $HOST_CFLAGS -w -c "${INC[@]}" "$PS3/runtime/ppu/ppu_p10_ctr.c" -o "$LIFT/ppu_p10_ctr.o"
# host_gow2_factory: subsistema factory/TYPE15 extraido do lift para ficheiro
# versionado (games/gow2/host_gow2_factory.cpp, commit 1f651aa no irmao
# gow2-recomp; porte para o monorepo, criterio 3 do ROADMAP da Fase 2). O
# criterio 4 (um lift novo, sem patches, mais estas fontes versionadas, LINKA)
# ja' esta medido tres vezes e promovido a producao -- boot_gow2_v3/v4 linkaram
# e o lift regenerado deu st620=11 em 6/6 (gow2-recomp commit 57b418f,
# games/gow2/lift_baseline/counters_pre.tsv: boot_lifted_functions=56223,
# lift_function_table_count=56072, imp_modules=13, imp_imports=151,
# orfaos_pos_run=0). Esta task (02-03) NAO re-prova o criterio 4 (D-2.6,
# 02-CONTEXT.md) -- so' liga o mecanismo de compilacao condicional que ja'
# funcionou a esse lift.
# Compila-se SO' quando o lift NAO trouxer a definicao (corpo, chaveta) dentro
# dele -- o lift antigo tem-na escrita a mao, e linkar as duas dava "duplicate
# symbol". Um lift regenerado so' tem a declaracao (patch_zz_host_api_decls.py,
# que termina em `;`) e precisa deste objecto.
if [ -f "$HERE/host_gow2_factory.cpp" ]; then
    # Procura a DEFINICAO (corpo, chaveta) e nao o prototipo: o patch_zz injecta
    # `...(uint32_t obj);` no preambulo, e um grep que casasse com isso mandaria
    # saltar a compilacao mesmo sem definicao nenhuma -> undefined symbol.
    if grep -lq 'extern "C" int ps3_factory_repair_vt(uint32_t obj) *{' "$LIFT"/ppu_recomp_*.cpp 2>/dev/null; then
        echo "  host_gow2_factory: definicoes ja' no lift -- nao compilar (evita duplicate symbol)"
        rm -f "$LIFT/host_gow2_factory.o"
    else
        clang++ -std=c++20 $HOST_OPT $MCPU $HOST_CFLAGS -w -c "${INC[@]}" \
            "$HERE/host_gow2_factory.cpp" -o "$LIFT/host_gow2_factory.o"
        echo "  host_gow2_factory: compilado"
    fi
fi
# host_gow2_f2b: subsistema F2B (mapa file-object->mfd/tamanho + preenchimento
# de stream FIOS do movie player) extraido POR SIMBOLO (D-2.3/D-2.4,
# 02-CONTEXT.md) para games/gow2/host_gow2_f2b.c (plano 02-02). Mesma logica
# de guard anti-duplicate-symbol do host_gow2_factory acima, MAS com uma
# diferenca critica medida nesta sessao (02-03): dos 17 simbolos F2B, so' 2
# funcoes (f2b_stream_ensure, f2b_stream_eof_try_complete) tem linkagem
# EXTERNA (`extern "C"`) no lift de producao actual -- as outras 5 funcoes E
# os 10 globais sao `static` (linkagem INTERNA, ex.: `static void
# f2b_fo_mfd_put(...)`, `static uint32_t g_f2b_fo_mfd_fo[8];`). Um guard que
# so' detectasse a forma `extern "C" ... *{` (como o do factory acima) nunca
# apanharia um lift onde esses 5/10 simbolos `static` continuassem la' mas os
# 2 `extern` tivessem desaparecido: nesse caso NAO haveria "duplicate symbol"
# nenhum (linkagens diferentes nunca colidem no link), mas haveria ESTADO
# DUPLICADO EM SILENCIO -- o chunk continuaria a usar a sua copia `static`
# interna, e o host_gow2_f2b.o ficaria morto no binario, sem erro nenhum que
# avisasse. Por isso o guard abaixo tem TRES condicoes, nao uma: (1) a
# definicao do simbolo-ancora `f2b_stream_ensure` independentemente do prefixo
# de linkagem (static/extern "C"/nenhum -- so' a assinatura + chaveta de
# abertura, nunca o `;` do prototipo); (2) qualquer uma das 5 funcoes `static`
# conhecidas; (3) qualquer um dos 10 globais `static` conhecidos. Presente
# qualquer uma -> salta a compilacao (hoje, medido: as tres batem no lift de
# producao actual -- portanto NAO compila).
if [ -f "$HERE/host_gow2_f2b.c" ]; then
    if grep -Eq 'f2b_stream_ensure\(uint32_t type_sys\) *\{' "$LIFT"/ppu_recomp_*.cpp 2>/dev/null || \
       grep -Eq '^static .*\bf2b_(fo_mfd_put|fo_mfd_get|fo_sz_get|stream_fill|stream_pre_consume)\b' "$LIFT"/ppu_recomp_*.cpp 2>/dev/null || \
       grep -Eq '^static .*\bg_f2b_(fo_mfd_fo|fo_mfd_fd|fo_mfd_sz|fo_mfd_n|natural_movie_fo|fill_fo|fill_mfd|fill_sz|fill_file_pos|fill_stream)\b' "$LIFT"/ppu_recomp_*.cpp 2>/dev/null; then
        echo "  host_gow2_f2b: definicao (static ou extern) ja' no lift -- nao compilar (evita duplicate symbol OU estado duplicado em silencio)"
        rm -f "$LIFT/host_gow2_f2b.o"
    else
        clang -std=c11 $HOST_OPT $MCPU $HOST_CFLAGS -w -c "${INC[@]}" \
            "$HERE/host_gow2_f2b.c" -o "$LIFT/host_gow2_f2b.o"
        echo "  host_gow2_f2b: compilado"
    fi
fi
# gow2_midasm_hooks: corpos HOST dos mid-asm hooks da Fase 17 (XEN-02). Um
# [[midasm_hook]] no config/gow2_recomp.toml faz o ppu_lifter.py emitir
# `gow2_midasm_<Name>(ctx);` ao lado da instrucao ancorada (17-01); a definicao
# do simbolo vem deste objecto. No plano 17-02 os corpos sao NO-OP -- o que se
# liga aqui e' o caminho de link, nao comportamento.
#
# GUARD ANTI-DUPLICATE-SYMBOL -- porque nao e' o mesmo grep dos dois blocos
# acima. Duas diferencas MEDIDAS (2026-08-02), nao assumidas por analogia:
#
#  (1) O lifter emite, no PREAMBULO da TU gerada, uma DECLARACAO por hook:
#      `void gow2_midasm_<Name>(ppu_context* ctx);` (ppu_lifter.py,
#      _preamble_lines). Um guard que casasse com o nome do simbolo -- como
#      `grep -q gow2_midasm` -- passaria a saltar a compilacao assim que o
#      primeiro lift com --config existisse, e o link morreria em "undefined
#      symbol" sem ninguem perceber porque. O guard tem de casar com a
#      DEFINICAO (assinatura + chaveta), nunca com o prototipo nem com a
#      chamada `gow2_midasm_X(ctx);` (essa acaba em `;`).
#
#  (2) Licao do host_gow2_f2b (marco v1.0): 5 das 7 funcoes F2B eram `static`
#      no lift real, e um guard so'-`extern "C"` teria deixado ESTADO
#      DUPLICADO EM SILENCIO em vez de um erro de link. Por isso as duas
#      expressoes abaixo nao ancoram em prefixo de linkagem NENHUM: apanham
#      `static`, `extern "C"` ou nada. A segunda cobre a definicao com a
#      chaveta na linha seguinte (estilo de codigo host colado a mao; o lifter
#      poe sempre na mesma linha, mas o corpo do CE03C hoje e' texto injectado
#      por patch e nao tem de seguir o estilo do lifter).
#
# Estado medido do lift de producao nesta corrida: ZERO ocorrencias de
# gow2_midasm_ em recomp_macos_v2/ppu_recomp_*.cpp -- logo o guard nao dispara
# e o objecto e' compilado. E' isso que faz o `nm` do aceite XEN-02 ter algo
# para encontrar.
if [ -f "$HERE/hooks/gow2_midasm_hooks.cpp" ]; then
    _midasm_def_same_line='(^|[^A-Za-z0-9_])gow2_midasm_[A-Za-z0-9_]+[[:space:]]*\([^;]*\)[[:space:]]*\{'
    _midasm_def_next_line='(^|[^A-Za-z0-9_])gow2_midasm_[A-Za-z0-9_]+[[:space:]]*\([^;]*\)[[:space:]]*$'
    if grep -Eq "$_midasm_def_same_line" "$LIFT"/ppu_recomp_*.cpp 2>/dev/null || \
       grep -Eq "$_midasm_def_next_line" "$LIFT"/ppu_recomp_*.cpp 2>/dev/null; then
        echo "  gow2_midasm_hooks: DEFINICAO (static ou extern) ja' no lift -- nao compilar"
        echo "                     (evita duplicate symbol OU estado duplicado em silencio)"
        rm -f "$LIFT/gow2_midasm_hooks.o"
    else
        clang++ -std=c++20 $HOST_OPT $MCPU $HOST_CFLAGS -w -c "${INC[@]}" -I "$HERE/hooks" \
            "$HERE/hooks/gow2_midasm_hooks.cpp" -o "$LIFT/gow2_midasm_hooks.o"
        echo "  gow2_midasm_hooks: compilado"
    fi
fi
# gow2_func_overrides: corpos HOST dos WEAK OVERRIDES da Fase 19 (XEN-04). Uma
# entrada [[functions_override]] no config/gow2_recomp.toml, com
# [main].emit_weak_wrappers = true, faz o ppu_lifter.py emitir a funcao liftada
# como um PAR -- `PPC_FUNC_IMPL(func_X)` (corpo, simbolo FORTE __imp_func_X) e
# `PPC_FUNC(func_X)` (wrapper FRACO que so' o chama). A definicao FORTE de
# func_X vem deste objecto e GANHA o link (medido no 19-01: nm -m + execucao).
#
# GUARD ANTI-DUPLICATE-SYMBOL -- porque nao e' o mesmo grep do bloco mid-asm
# acima, apesar de o problema parecer o mesmo. Tres diferencas MEDIDAS:
#
#  (1) A LISTA DE SIMBOLOS SAI DO PROPRIO .cpp, nao esta' escrita aqui. Um
#      override novo em 19-03 nao pode depender de alguem se lembrar de editar
#      tambem este script -- essa e' exactamente a forma como um guard apodrece
#      e deixa passar o erro que existia para apanhar. Extrai-se por DUAS
#      expressoes (licao do host_gow2_f2b: nunca ancorar numa so' forma): a
#      macro GOW2_FUNC_OVERRIDE(func_X) e, em alternativa, uma definicao
#      escrita a mao `func_X(ppu_context* ctx)` sem a macro.
#
#  (2) O QUE SE PROCURA NO LIFT NAO E' O NOME, E' A FORMA DA DEFINICAO. As TUs
#      liftadas trazem, alem da definicao, DECLARACOES (`void func_X(ppu_context*
#      ctx);` -- terminam em `;`), CHAMADAS (`func_X(ctx);`), entradas da
#      `function_table` (`{ 0x...ULL, func_X, "func_X" },`) e o wrapper
#      `PPC_FUNC(func_X) {`. Um grep pelo nome casava com todas e mandava saltar
#      a compilacao para sempre. Por isso as duas expressoes abaixo exigem
#      `(...)` SEM `;` la' dentro seguido de `{` (ou de fim de linha, para o
#      estilo com a chaveta na linha seguinte) -- e nenhuma delas casa com
#      `PPC_FUNC_IMPL(func_X) {` nem com `PPC_FUNC(func_X) {`, onde o que vem a
#      seguir ao nome e' `)` e nao `(`. Isso e' o que separa o lift COM
#      wrappers (compilar) do lift SEM (nao compilar).
#
#  (3) HA' UM TERCEIRO ESTADO, e e' o silencioso. Se o lift nao tiver nem a
#      definicao forte nem o wrapper fraco (EA fora do lift, TOML sem a
#      entrada, emissao desligada num so' dos dois sitios), o objecto compila e
#      liga sem erro nenhum, define um simbolo que ninguem chama, e o override
#      e' um NO-OP INVISIVEL -- a mesma classe de falha que o guard do
#      host_gow2_f2b existe para apanhar. Aqui isso da' AVISO explicito.
if [ -f "$HERE/hooks/gow2_func_overrides.cpp" ]; then
    _ovr_src="$HERE/hooks/gow2_func_overrides.cpp"
    _ovr_names=$( { grep -oE 'GOW2_FUNC_OVERRIDE[[:space:]]*\([[:space:]]*func_[0-9A-Fa-f]{8}' "$_ovr_src" || true
                    grep -oE '(^|[^A-Za-z0-9_])func_[0-9A-Fa-f]{8}[[:space:]]*\([[:space:]]*ppu_context[[:space:]]*\*[[:space:]]*ctx[[:space:]]*\)[[:space:]]*(\{|$)' "$_ovr_src" || true
                  } | grep -oE 'func_[0-9A-Fa-f]{8}' | sort -u | tr '\n' ' ' )
    _ovr_strong=""
    _ovr_noweak=""
    for _n in $_ovr_names; do
        if grep -Eq "(^|[^A-Za-z0-9_])${_n}[[:space:]]*\([^;]*\)[[:space:]]*\{" "$LIFT"/ppu_recomp_*.cpp 2>/dev/null || \
           grep -Eq "(^|[^A-Za-z0-9_])${_n}[[:space:]]*\([^;]*\)[[:space:]]*$" "$LIFT"/ppu_recomp_*.cpp 2>/dev/null; then
            _ovr_strong="$_ovr_strong $_n"
        elif ! grep -Eq "PPC_FUNC[[:space:]]*\([[:space:]]*${_n}[[:space:]]*\)" "$LIFT"/ppu_recomp_*.cpp 2>/dev/null; then
            _ovr_noweak="$_ovr_noweak $_n"
        fi
    done
    if [ -z "${_ovr_names// /}" ]; then
        echo "  gow2_func_overrides: nenhum override declarado no .cpp -- nao compilar"
        rm -f "$LIFT/gow2_func_overrides.o"
    elif [ -n "$_ovr_strong" ]; then
        echo "  gow2_func_overrides: DEFINICAO FORTE ja' no lift para:$_ovr_strong -- nao compilar"
        echo "                       (lift gerado SEM [main].emit_weak_wrappers + [[functions_override]];"
        echo "                        compilar daria duplicate symbol. OVERRIDES INACTIVOS neste binario.)"
        rm -f "$LIFT/gow2_func_overrides.o"
    else
        if [ -n "$_ovr_noweak" ]; then
            echo "  gow2_func_overrides: AVISO -- o lift nao tem wrapper fraco para:$_ovr_noweak"
            echo "                       (o override compila e liga, mas ninguem o chama: NO-OP SILENCIOSO)"
        fi
        clang++ -std=c++20 $HOST_OPT $MCPU $HOST_CFLAGS -w -c "${INC[@]}" -I "$HERE/hooks" \
            "$_ovr_src" -o "$LIFT/gow2_func_overrides.o"
        echo "  gow2_func_overrides: compilado (overrides: $_ovr_names)"
    fi
fi
# host_res_inflate: runtime/ppu is excluded from libps3recomp_runtime.a (same as
# ppu_loader). Required for EBOOT gzip HOSTRES (gowshader.cfx, *.ctxr) after
# patch_host_res_inflate.py hooks func_001E7B50. Uses rsx_host_content + stbi
# from the runtime .a — portable Mac/Win.
if [ -f "$PS3/runtime/ppu/host_res_inflate.c" ]; then
    clang -std=c11 $HOST_OPT $MCPU $HOST_CFLAGS -w -c "${INC[@]}" -I "$PS3/libs/video" \
        "$PS3/runtime/ppu/host_res_inflate.c" -o "$LIFT/host_res_inflate.o"
fi
# WAD ~texture packages after WADLD-T1R (patch_wad_tex_capture → force-bind).
if [ -f "$PS3/runtime/ppu/host_wad_tex.c" ]; then
    clang -std=c11 $HOST_OPT $MCPU $HOST_CFLAGS -w -c "${INC[@]}" -I "$PS3/libs/video" \
        "$PS3/runtime/ppu/host_wad_tex.c" -o "$LIFT/host_wad_tex.o"
fi

echo "=== 3. HLE NID table -> .o ==="
# ppu_hle_register_all() is a weak no-op in the runtime; without a strong
# override every firmware call the game makes logs "unresolved NID" and returns
# nothing. Regenerated from the /* NID */ annotations in the HLE sources.
mkdir -p "$LIFT/gen"
# Espelha a exclusao do CMakeLists do motor: sceNpCommerce.c colide com
# sceNpCommerce2.c e ficou fora da biblioteca (ver o comentario la para o que
# se perde). O gerador varre o disco, nao o que o CMake compila, entao tem de
# saltar o ficheiro tambem -- senao declara simbolos que nao existem na .a.
LIBS=$(ls "$PS3"/libs/*/*.c | xargs -n1 basename | sed 's/\.c$//' | sort -u \
       | grep -vx 'sceNpCommerce')
# shellcheck disable=SC2086
"$PYBIN" "$PS3/tools/gen_hle_nids.py" \
    --out "$LIFT/gen/ppu_hle_nids.cpp" $LIBS > /dev/null
clang++ -std=c++20 $HOST_OPT $MCPU $HOST_CFLAGS -w -c "${INC[@]}" -I "$PS3/libs" "$LIFT/gen/ppu_hle_nids.cpp" -o "$LIFT/ppu_hle_nids.o"

echo "=== 3b. imagens SPU liftadas do GoW2 -> .o ==="
# spu_lifted/spu{0..3}_v2 ja vem com simbolos prefixados (spu0_, spu1_, ...),
# logo os quatro coexistem no mesmo binario. gow2_spu_register.c regista-os no
# dispatcher por fingerprint; spu0 entra sempre, spu1/2/3 sao opt-in por env
# (PS3_SPU1/2/3, PS3_SPU_ALL).
#
# Cuidado herdado do Windows: o comentario do gow2_spu_register.c diz que um job
# que rebenta mata so a thread "gracas a SEH isolation" -- isso e Windows. Aqui a
# proteccao e o setjmp/longjmp do spu_lifted_job.h do master, que apanha a saida
# do job mas NAO um SIGBUS/SIGSEGV: um job que falta leva o processo inteiro.
# E por isso que spu1/2/3 continuam opt-in.
SPU_OBJS=()
SPU_FLAGS_FILE="$LIFT/.spu_build_flags"
SPU_FLAGS_VALUE="SPU_OPT=$SPU_OPT MCPU=$MCPU HOST_CFLAGS=$HOST_CFLAGS"
SPU_FLAGS_STALE=1
if [ -f "$SPU_FLAGS_FILE" ] && [ "$(cat "$SPU_FLAGS_FILE")" = "$SPU_FLAGS_VALUE" ]; then
    SPU_FLAGS_STALE=0
fi
for d in "$HERE"/spu_lifted/spu?_v2; do
    [ -f "$d/spu_recomp.c" ] || continue
    n=$(basename "$d")
    o="$LIFT/${n}_spu_recomp.o"
    # SPU0_DIR=<dir> (A/B, opt-in): another spu0 lift, with its own object so
    # the shared spu0_v2 object other builds link stays untouched.
    if [ "$n" = spu0_v2 ] && [ -n "${SPU0_DIR:-}" ]; then
        d="$SPU0_DIR"; o="$LIFT/spu0_ab_$(basename "$SPU0_DIR")_spu_recomp.o"
    fi
    # brsl/bih* conditions test the preferred halfword (idempotent; only the
    # lifts made with the 2026-07-21..09-23 lifter change).
    "$PYBIN" "$HERE/recomp_mid_v2/patch_spu_halfword_cond.py" "$d" > /dev/null
    if [ "$n" = spu0_v2 ]; then
        # Re-lift the captured image in a temporary directory and import every
        # observed bi/bisl destination.  SPU0_OBSERVED_LOG may point at a fresh
        # runtime log; the tracked manifest remains the baseline evidence.
        "$PYBIN" "$HERE/recomp_mid_v2/patch_spu0_observed_entries.py" "$d"
        SPU0_CONTRACT_ARGS=(
            --observed "$HERE/recomp_mid_v2/spu0_observed_indirect_targets.lst"
        )
        if [ -n "${SPU0_OBSERVED_LOG:-}" ]; then
            SPU0_CONTRACT_ARGS+=(--observed "$SPU0_OBSERVED_LOG")
        fi
        "$PYBIN" "$PS3/tools/verify_spu_indirect_entries.py" "$d" spu0_ \
            "${SPU0_CONTRACT_ARGS[@]}"
    elif [ "$n" = spu1_v2 ]; then
        "$PYBIN" "$HERE/recomp_mid_v2/patch_spu1_observed_entries.py" "$d"
        if [ -n "${SPU1_OBSERVED_LOG:-}" ]; then
            "$PYBIN" "$PS3/tools/verify_spu_indirect_entries.py" "$d" spu1_ \
                --observed "$HERE/recomp_mid_v2/spu1_observed_indirect_targets.lst" \
                --observed "$SPU1_OBSERVED_LOG"
        else
            "$PYBIN" "$PS3/tools/verify_spu_indirect_entries.py" "$d" spu1_ \
                --observed "$HERE/recomp_mid_v2/spu1_observed_indirect_targets.lst"
        fi
    else
        "$PYBIN" "$PS3/tools/verify_spu_indirect_entries.py" "$d" "${n%_v2}_"
    fi
    # Os helpers de semantica do SPU sao header-only: sem estas dependencias,
    # editar runtime/spu/*.h produzia um binario novo com o codigo velho do SPU.
    stale=0
    [ -f "$o" ] || stale=1
    [ "$SPU_FLAGS_STALE" = 1 ] && stale=1
    [ "$d/spu_recomp.c" -nt "$o" ] && stale=1
    for h in "$PS3"/runtime/spu/*.h "$PS3"/include/ps3emu/ps3types.h; do
        [ -f "$h" ] && [ "$h" -nt "$o" ] && stale=1
    done
    if [ "$stale" = 1 ]; then
        clang -std=c11 $SPU_OPT $MCPU -w -c -I "$d" -I "$PS3/runtime/spu" -I "$PS3/include" \
              "$d/spu_recomp.c" -o "$o"
    fi
    SPU_OBJS+=("$o")
done
printf '%s' "$SPU_FLAGS_VALUE" > "$SPU_FLAGS_FILE"
if [ ${#SPU_OBJS[@]} -gt 0 ]; then
    clang -std=c11 $SPU_OPT $MCPU -w -c -I "$PS3/runtime/spu" -I "$PS3/include" \
          "$HERE/recomp_mid_v2/gow2_spu_register.c" -o "$LIFT/gow2_spu_register.o"
    SPU_OBJS+=("$LIFT/gow2_spu_register.o")
fi
echo "  imagens SPU: ${#SPU_OBJS[@]} objecto(s)"

echo "=== 4. boot host -> .o ==="
clang++ -std=c++20 $HOST_OPT $MCPU $HOST_CFLAGS -w -c "${INC[@]}" "$HERE/boot_macos.cpp" -o "$LIFT/boot_macos.o"
# Amostrador do movie player ([MOVIEFSM]), gated por PS3_TRACE_MOVIEOBJ /
# PS3_MOVIE_EOS / PS3_PERF_FSM. C puro e portatil de proposito.
clang -std=c11 $HOST_OPT $MCPU $HOST_CFLAGS -w -c -I "$HERE" -I "$PS3/libs/video" "$HERE/movie_eos_arm.c" -o "$LIFT/movie_eos_arm.o"
# Diagnosticos do overlay de runtime (copia snapshots do host; sem ponteiro guest).
clang -std=c11 $HOST_OPT $MCPU $HOST_CFLAGS -w -c -I "$HERE" -I "$PS3/libs/video" \
      "$HERE/gow2_overlay_provider.c" -o "$LIFT/gow2_overlay_provider.o"

echo "=== 5. link ==="
SDL_FLAGS="${SDL_FLAGS-$(pkg-config --libs sdl2)}"
# Sem SDL (corrida headless com TSan): compila os stubs no lugar da biblioteca.
SDL_STUB_OBJ=""
if [ -z "$SDL_FLAGS" ]; then
    clang -std=c11 $HOST_OPT $MCPU $HOST_CFLAGS -w -c \
        "$HERE/recomp_mid_v2/sdl_stubs_headless.c" -o "$LIFT/sdl_stubs_headless.o"
    SDL_STUB_OBJ="$LIFT/sdl_stubs_headless.o"
    echo "  SDL: stubs headless (sem libSDL2)"
fi   # SDL_FLAGS="" para um binario sem SDL (corridas headless com TSan)
VK_FLAGS=""
if [ -f /opt/homebrew/lib/libvulkan.dylib ]; then
    VK_FLAGS="-L/opt/homebrew/lib -lvulkan"
fi

# Collect lift objects matching this LIFT_OPT (and LIFT_OBJ_TAG).
LIFT_OBJS=()
for f in "$LIFT"/ppu_recomp_*.cpp "$LIFT"/ppu_stubs.cpp; do
    [ -f "$f" ] || continue
    base=$(basename "$f")
    LIFT_OBJS+=("$LIFT/$(lift_obj "$base")")
done

# The guest's CRT recurses deeply under -O0; the default 8 MB main-thread stack
# is not enough. Windows uses -Wl,--stack,33554432 for the same reason.
# NB (Task 5, macos-movie-eos plan): 0x2000000 (32 MB) is NOT bumped for the
# "SIGBUS after recursion cap @0x0045CB90". That was diagnosed as a host stack
# overflow, but PS3_TRACE_HOST_STACK=1 measures the capped depth-4000 recursion
# using only ~2 MB of host stack (~514 B/level) -- 32 MB is never threatened.
# The real SIGBUS is the cap's skip returning a corrupt result (r3=0x84010002,
# an unmapped guest EA) that the caller derefs; the committed bctr-tail fix
# already keeps that poll from reaching the cap. See runtime/ppu/ppu_loader.cpp.
clang++ -std=c++20 $HOST_OPT $MCPU $HOST_CFLAGS $LINK_CFLAGS \
    "${LIFT_OBJS[@]}" \
    "$LIFT"/ppu_loader.o "$LIFT"/ppu_imports.o "$LIFT"/ppu_hle.o \
    "$LIFT"/ppu_sysprx.o "$LIFT"/ppu_fs.o "$LIFT"/ppu_icall_ascii.o \
    "$LIFT"/ppu_vm_fast_policy.o "$LIFT"/ppu_p10_ctr.o \
    $([ -f "$LIFT/host_gow2_factory.o" ] && echo "$LIFT/host_gow2_factory.o") \
    $([ -f "$LIFT/host_gow2_f2b.o" ] && echo "$LIFT/host_gow2_f2b.o") \
    $([ -f "$LIFT/gow2_midasm_hooks.o" ] && echo "$LIFT/gow2_midasm_hooks.o") \
    $([ -f "$LIFT/gow2_func_overrides.o" ] && echo "$LIFT/gow2_func_overrides.o") \
    $([ -f "$LIFT/host_res_inflate.o" ] && echo "$LIFT/host_res_inflate.o") \
    $([ -f "$LIFT/host_wad_tex.o" ] && echo "$LIFT/host_wad_tex.o") \
    "$LIFT"/ppu_hle_nids.o "$LIFT"/boot_macos.o "$LIFT"/movie_eos_arm.o \
    "$LIFT"/gow2_overlay_provider.o \
    ${SPU_OBJS[@]+"${SPU_OBJS[@]}"} \
    "$RUNTIME_LIB" \
    -framework Metal -framework MetalFX -framework MetalPerformanceShaders -framework QuartzCore -framework Foundation \
    -framework Cocoa -framework CoreText \
    -framework AVFoundation -framework CoreMedia -framework CoreVideo -framework VideoToolbox \
    -framework AudioToolbox -framework CoreAudio \
    -framework GameController -framework CoreHaptics \
    $SDL_FLAGS $VK_FLAGS $SDL_STUB_OBJ -lm \
    -Wl,-stack_size,0x2000000 \
    -o "$OUT"

echo
ls -lh "$OUT"
echo "*** $OUT built (LIFT_OPT=$LIFT_OPT HOST_OPT=$HOST_OPT) ***"
echo
echo "run it with:  ./rodar_gow2.sh"
