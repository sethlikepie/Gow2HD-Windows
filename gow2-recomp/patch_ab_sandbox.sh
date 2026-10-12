#!/usr/bin/env bash
# patch_ab_sandbox.sh -- Fase 8, Plano 08-01, Tarefa 1.
#
# Extrai um conjunto de recomp_mid_v2/patch_*.py de um git ref (ou do checkout
# actual) para um directorio novo, excluindo por omissao qualquer patch
# classificado PROBE (no_gate=1, coluna 5) em
# games/gow2/lift_baseline/PATCH_CATALOG.tsv -- fix estrutural no.1 do primeiro
# teste da pista dos patches (o diff ficou dominado pelos 2 probes desta
# sessao, [CB56CTY]/[29AF0]). Reutilizavel para qualquer bisseccao futura de
# patches, nao so' esta fase.
#
# Uso:
#   ./patch_ab_sandbox.sh REF OUT_DIR [--include-probe]
#
#   REF        WORKTREE (usa o checkout actual de recomp_mid_v2/patch_*.py)
#              ou qualquer git ref valido no repositorio gow2-recomp (ex.:
#              e6d65a2).
#   OUT_DIR    directorio de destino -- tem de resolver para FORA do proprio
#              repositorio (T-08-01); o script aborta se resolver para dentro
#              de $REPO (recomp_mid_v2, recomp_macos_v2, ou qualquer caminho
#              do checkout).
#   --include-probe   NAO exclui os patches PROBE (usado pelo Plano 08-02 para
#                      fidelidade ao pipeline real de producao).
#
# Imprime no fim: total extraido, total excluido por PROBE, total final em
# OUT_DIR -- deterministico, para quem consome (Tarefa 2 deste plano, ou o
# Plano 08-02) confiar na contagem sem reabrir o catalogo.

set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1
REPO="$PWD"

REF="${1:-}"
OUT_DIR_ARG="${2:-}"
INCLUDE_PROBE=0
for a in "${@:3}"; do
  case "$a" in
    --include-probe) INCLUDE_PROBE=1 ;;
    *) echo "opcao desconhecida: $a" >&2; exit 2 ;;
  esac
done

if [ -z "$REF" ] || [ -z "$OUT_DIR_ARG" ]; then
  echo "uso: $0 REF OUT_DIR [--include-probe]" >&2
  exit 2
fi

# T-08-01: OUT_DIR nunca pode resolver para dentro do proprio repositorio.
# mkdir -p primeiro para o realpath/cd funcionar mesmo se OUT_DIR ainda nao existir.
mkdir -p "$OUT_DIR_ARG" || { echo "ABORTA: nao consegui criar OUT_DIR '$OUT_DIR_ARG'" >&2; exit 1; }
OUT_DIR="$(cd "$OUT_DIR_ARG" && pwd)"
REPO_REAL="$(cd "$REPO" && pwd)"
case "$OUT_DIR/" in
  "$REPO_REAL"/*)
    echo "ABORTA: OUT_DIR '$OUT_DIR' resolve para dentro do repositorio ($REPO_REAL) -- recusa escrever em recomp_mid_v2/recomp_macos_v2 de producao (T-08-01)." >&2
    exit 1
    ;;
esac

# limpa OUT_DIR antes de extrair, para nao misturar restos de uma corrida anterior
rm -f "$OUT_DIR"/patch_*.py

extracted=0
if [ "$REF" = "WORKTREE" ]; then
  for f in "$REPO"/recomp_mid_v2/patch_*.py; do
    [ -f "$f" ] || continue
    cp "$f" "$OUT_DIR/" || { echo "ABORTA: falha ao copiar $f" >&2; exit 1; }
    extracted=$((extracted + 1))
  done
else
  names=$(git -C "$REPO" ls-tree -r --name-only "$REF" -- recomp_mid_v2 2>/dev/null \
    | grep -E '^recomp_mid_v2/patch_[^/]+\.py$')
  if [ -z "$names" ]; then
    echo "ABORTA: git ls-tree nao encontrou nenhum recomp_mid_v2/patch_*.py em REF='$REF'" >&2
    exit 1
  fi
  while IFS= read -r path; do
    [ -z "$path" ] && continue
    name="$(basename "$path")"
    if ! git -C "$REPO" show "$REF:$path" > "$OUT_DIR/$name" 2>/dev/null; then
      echo "ABORTA: git show falhou para $REF:$path" >&2
      exit 1
    fi
    extracted=$((extracted + 1))
  done <<< "$names"
fi

# Fix estrutural (descoberto na Tarefa 1, T-08-01/T-08-03 continuam validas):
# varios patch_*.py importam um modulo partilhado local (ex.: lift_paths.py,
# "from lift_paths import resolve_lift_paths") que o Python resolve pelo
# directorio do proprio script -- se so' os patch_*.py forem copiados/extraidos
# para OUT_DIR, o import falha com ModuleNotFoundError e o patch aparece como
# FAILED (rc!=0) sem qualquer relacao com o conteudo do lift. Deteccao
# generica: para cada "from NOME import" nos ficheiros ja extraidos, se NOME
# nao for um patch_*.py e existir recomp_mid_v2/NOME.py NO MESMO REF, extrai
# tambem esse ficheiro -- para que futuros modulos partilhados no sejam
# precisos editar este script.
shared_modules=$(grep -ohE '^from [A-Za-z_][A-Za-z0-9_]* import' "$OUT_DIR"/patch_*.py 2>/dev/null \
  | awk '{print $2}' | sort -u)
shared_extracted=0
while IFS= read -r modname; do
  [ -z "$modname" ] && continue
  case "$modname" in
    patch_*) continue ;;  # ja' e' um dos ficheiros extraidos acima
  esac
  if [ -f "$OUT_DIR/${modname}.py" ]; then continue; fi  # ja' presente
  if [ "$REF" = "WORKTREE" ]; then
    src="$REPO/recomp_mid_v2/${modname}.py"
    if [ -f "$src" ]; then
      cp "$src" "$OUT_DIR/" && shared_extracted=$((shared_extracted + 1))
    fi
  else
    if git -C "$REPO" show "$REF:recomp_mid_v2/${modname}.py" > "$OUT_DIR/${modname}.py" 2>/dev/null; then
      shared_extracted=$((shared_extracted + 1))
    else
      rm -f "$OUT_DIR/${modname}.py"
    fi
  fi
done <<< "$shared_modules"

excluded=0
if [ "$INCLUDE_PROBE" -eq 0 ]; then
  PS3_ENGINE_ROOT="${PS3_ENGINE_ROOT:-$REPO/../ps3recomp}"
  CATALOG="${PS3_PATCH_CATALOG:-$PS3_ENGINE_ROOT/games/gow2/lift_baseline/PATCH_CATALOG.tsv}"
  if [ -f "$CATALOG" ]; then
    probe_names=$(awk -F'\t' '$5=="1"{print $1}' "$CATALOG")
    while IFS= read -r pname; do
      [ -z "$pname" ] && continue
      # nomes no catalogo podem vir com ou sem sufixo .py -- normaliza
      base="${pname%.py}"
      target="$OUT_DIR/${base}.py"
      if [ -f "$target" ]; then
        rm -f "$target"
        excluded=$((excluded + 1))
      fi
      # nao falha se um nome PROBE de hoje nao existir no REF antigo -- so' ignora.
    done <<< "$probe_names"
  else
    echo "AVISO: PATCH_CATALOG.tsv nao encontrado ($CATALOG) -- nenhum patch excluido por PROBE" >&2
  fi
fi

final=$(ls "$OUT_DIR"/patch_*.py 2>/dev/null | wc -l | tr -d ' ')

echo "extraidos: $extracted"
echo "modulos_partilhados_extraidos: $shared_extracted"
echo "excluidos_por_probe: $excluded"
echo "final_em_${OUT_DIR}: $final"
