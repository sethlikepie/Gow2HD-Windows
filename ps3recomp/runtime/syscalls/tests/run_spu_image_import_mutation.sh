#!/usr/bin/env bash
# run_spu_image_import_mutation.sh -- prova por MUTACAO do teste do LV2S-01.
#
# O que este script prova, e porque e' preciso: um teste verde so' diz que a
# arvore actual passa. Nao diz que o teste APANHARIA o defeito antigo. Aqui
# repomos o comportamento anterior (`vm_write_be32(seg + 0x10, 0)` no ramo
# COPY) numa COPIA fora do repositorio e exigimos que o MESMO ficheiro de
# teste fique VERMELHO. Nos dois sentidos:
#
#   sentido_mutado  -> a copia com o defeito reposto tem de dar rc != 0
#   sentido_limpo   -> a MESMA copia, sem a mutacao, tem de dar rc == 0
#
# O segundo sentido nao e' cerimonia: uma copia que so' foi vista vermelha
# podia estar vermelha por causa do ambiente de compilacao (um -I em falta, um
# sysroot errado) e nao por causa da mutacao. So' com o controlo positivo e'
# que o vermelho fica atribuido a mutacao.
#
# SEGURANCA (ameaca T-23-07 do plano): a mutacao NUNCA toca na arvore real. A
# copia vive num `mktemp -d` fora do repositorio, um `trap ... EXIT` remove-a,
# e o sha256 de runtime/syscalls/lv2_register.c e' comparado antes/depois --
# se diferir, o script FALHA.
#
# Nivel de evidencia: offline-unit. Nada aqui e' um aceite in-boot.
#
# Variaveis honradas:
#   CC      compilador C (default: cc). O CMake passa CMAKE_C_COMPILER via
#           set_tests_properties(... ENVIRONMENT ...), para a CI compilar com o
#           mesmo compilador que configurou o build.
#   PYTHON  interprete usado para aplicar a mutacao como substituicao LITERAL
#           (default: python3). O CMake passa Python3_EXECUTABLE, o mesmo
#           padrao ja' usado pela suite.runtime_spu. Literal e nao regex de
#           proposito: o alvo contem '+' e parenteses, e um sed mal escapado
#           podia nao substituir nada e dar uma "prova" silenciosamente vazia.

set -uo pipefail

# --- 1. Raiz do repositorio, a partir do caminho do proprio script ----------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

PROD="$ROOT/runtime/syscalls/lv2_register.c"
TESTE="$ROOT/runtime/syscalls/tests/test_spu_image_import.c"

for f in "$PROD" "$TESTE"; do
    if [ ! -f "$f" ]; then
        echo "ERRO: nao encontrado: $f" >&2
        exit 2
    fi
done

sha256_de() { shasum -a 256 "$1" | cut -d' ' -f1; }

SHA_ANTES="$(sha256_de "$PROD")"
echo "producao sha256 ANTES : $SHA_ANTES"

# --- 2. Directorio temporario FORA do repositorio, removido no EXIT --------
TMPD="$(mktemp -d "${TMPDIR:-/tmp}/lv2s_mut.XXXXXX")"
trap 'rm -rf "$TMPD"' EXIT
echo "sandbox               : $TMPD"

# --- 3. Copia + verificacao de que o alvo da mutacao e' inequivoco ---------
LIMPO="$TMPD/lv2_register_limpo.c"
MUTADO="$TMPD/lv2_register_mutado.c"
cp "$PROD" "$LIMPO"

ALVO='vm_write_be32(seg + 0x10, src_ea + p_off)'
N_ALVO="$(grep -cF "$ALVO" "$LIMPO")"
if [ "$N_ALVO" -ne 1 ]; then
    echo "ERRO: encontrei $N_ALVO ocorrencias de '$ALVO' (esperava exactamente 1)." >&2
    if [ "$N_ALVO" -eq 0 ]; then
        echo "       Zero significa que o fix do LV2S-01 nao esta na arvore." >&2
    else
        echo "       Mais do que uma torna a mutacao nao-discriminadora." >&2
    fi
    exit 2
fi

# --- 4. Mutacao: repor o comportamento antigo (so' no ramo COPY) -----------
# O ramo FILL ja' escreve 0 e nao bate o padrao procurado, por isso fica
# intacto -- a mutacao e' exactamente o defeito antigo, nem mais nem menos.
cp "$LIMPO" "$MUTADO"
PY_BIN="${PYTHON:-python3}"
if ! command -v "$PY_BIN" >/dev/null 2>&1; then
    echo "ERRO: interprete Python '$PY_BIN' nao encontrado (define PYTHON=...)." >&2
    exit 2
fi
"$PY_BIN" - "$MUTADO" <<'PY'
import io, sys
p = sys.argv[1]
s = io.open(p, encoding='utf-8').read()
alvo = 'vm_write_be32(seg + 0x10, src_ea + p_off)'
assert s.count(alvo) == 1, s.count(alvo)
io.open(p, 'w', encoding='utf-8').write(s.replace(alvo, 'vm_write_be32(seg + 0x10, 0)'))
PY
if [ $? -ne 0 ]; then
    echo "ERRO: a mutacao falhou a aplicar." >&2
    exit 2
fi
# Confirmar que a mutacao MUDOU mesmo o ficheiro: uma substituicao que nao
# substituisse nada daria um "sentido_mutado" que so' repetia o sentido limpo.
if grep -qF "$ALVO" "$MUTADO"; then
    echo "ERRO: o alvo ainda esta presente na copia mutada -- mutacao sem efeito." >&2
    exit 2
fi
echo "mutacao aplicada      : '$ALVO' -> 'vm_write_be32(seg + 0x10, 0)'"

# --- 5. Linha de compilacao ------------------------------------------------
# A copia vive FORA da arvore, por isso os includes com aspas dela
# ("sys_ppu_thread.h", "../ppu/ppu_context.h", "../../include/ps3emu/...")
# deixariam de resolver pela busca relativa ao ficheiro: sao estes -I que os
# repoem. Em Darwin, um `cc` nu falha logo em <stdlib.h> sem -isysroot
# (medido nesta maquina; e' a mesma causa das duas falhas de toolchain ja'
# declaradas como divida na Fase 22), por isso resolvemos o SDK com xcrun.
CC_BIN="${CC:-cc}"
CFLAGS=(-std=c11 -UNDEBUG
        -I "$ROOT/runtime/syscalls" -I "$ROOT/runtime" -I "$ROOT/include")

if [ "$(uname -s)" = "Darwin" ]; then
    SDK="$(xcrun --show-sdk-path 2>/dev/null)"
    if [ -n "${SDK:-}" ] && [ -d "$SDK" ]; then
        CFLAGS+=(-isysroot "$SDK")
        echo "sysroot (Darwin)      : $SDK"
    else
        echo "AVISO: xcrun nao resolveu um SDK; a compilar sem -isysroot." >&2
    fi
fi

compila_e_corre() {
    # $1 = caminho da copia do lv2_register.c ; $2 = etiqueta
    local fonte="$1" etiqueta="$2"
    local exe="$TMPD/test_$etiqueta"
    echo "--- $etiqueta ---"
    echo "cc: $CC_BIN ${CFLAGS[*]} -DLV2_REGISTER_C=\"\\\"$fonte\\\"\" -o $exe $TESTE"
    if ! "$CC_BIN" "${CFLAGS[@]}" -DLV2_REGISTER_C="\"$fonte\"" -o "$exe" "$TESTE" \
            > "$TMPD/build_$etiqueta.log" 2>&1; then
        echo "ERRO: a compilacao de '$etiqueta' falhou:" >&2
        cat "$TMPD/build_$etiqueta.log" >&2
        return 125
    fi
    "$exe" > "$TMPD/run_$etiqueta.log" 2>"$TMPD/run_$etiqueta.err"
    local rc=$?
    grep -E '^(FAIL|test_spu_image_import:)' "$TMPD/run_$etiqueta.log" || true
    echo "rc($etiqueta) = $rc"
    return $rc
}

# --- 6. Sentido mutado: TEM de ficar vermelho ------------------------------
compila_e_corre "$MUTADO" "mutado"
RC_MUTADO=$?

# --- 7. Controlo positivo: a MESMA copia, sem a mutacao, tem de ficar verde -
compila_e_corre "$LIMPO" "limpo"
RC_LIMPO=$?

# --- 8. A producao tem de estar intacta ------------------------------------
SHA_DEPOIS="$(sha256_de "$PROD")"
echo "producao sha256 DEPOIS: $SHA_DEPOIS"
if [ "$SHA_ANTES" = "$SHA_DEPOIS" ]; then
    INTACTA="sim"
else
    INTACTA="nao"
fi

# --- 9. Veredicto ----------------------------------------------------------
echo "MUTACAO: sentido_mutado=$RC_MUTADO sentido_limpo=$RC_LIMPO producao_intacta=$INTACTA"

FALHOU=0
if [ "$RC_MUTADO" -eq 125 ] || [ "$RC_LIMPO" -eq 125 ]; then
    echo "FALHA: uma das compilacoes nao correu -- a prova nao foi feita." >&2
    FALHOU=1
fi
if [ "$RC_MUTADO" -eq 0 ]; then
    echo "FALHA: com o defeito antigo reposto o teste PASSOU. O teste nao discrimina" >&2
    echo "       o defeito e portanto nao serve de prova do LV2S-01." >&2
    FALHOU=1
fi
if [ "$RC_LIMPO" -ne 0 ]; then
    echo "FALHA: a copia SEM mutacao ficou vermelha (rc=$RC_LIMPO) -- o vermelho do" >&2
    echo "       sentido mutado nao pode ser atribuido a mutacao." >&2
    FALHOU=1
fi
if [ "$INTACTA" != "sim" ]; then
    echo "FALHA: $PROD mudou durante a corrida (sha256 antes != depois)." >&2
    FALHOU=1
fi

if [ "$FALHOU" -ne 0 ]; then
    exit 1
fi
echo "MUTACAO_OK: o teste apanha o defeito antigo e a producao ficou intacta."
exit 0
