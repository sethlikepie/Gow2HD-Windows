#!/usr/bin/env bash
# Navegação rápida do clone RPCS3 (_ref_rpcs3) via cscope (call-graph) + rg.
# Ver o mapa curado: gow2-recomp/notes/2026-07-21-rpcs3-reference-map.md
#
# Uso:
#   rpcs3_grep.sh def     <simbolo>   # onde e' DEFINIDO
#   rpcs3_grep.sh callers <simbolo>   # quem CHAMA (arestas de entrada do grafo)
#   rpcs3_grep.sh callees <funcao>    # o que ELA chama (arestas de saida)
#   rpcs3_grep.sh refs    <simbolo>   # todas as referencias
#   rpcs3_grep.sh <regex>             # rg literal no codigo RPCS3
set -euo pipefail

REF="${RPCS3_REF:-/Users/andrebrumcortezferreira/Documents/PESSOAL/_ref_rpcs3}"
[ -f "$REF/cscope.out" ] || { echo "sem cscope.out em $REF -- corre tools/rpcs3_index.sh" >&2; exit 1; }

mode="${1:-}"; shift || true
q="${*:-}"
cs() { ( cd "$REF" && cscope -dL"$1" "$2" -f cscope.out ) | sed "s#^#$REF/#"; }

case "$mode" in
  def)     cs 1 "$q" ;;   # find this definition
  callers) cs 3 "$q" ;;   # find functions calling this
  callees) cs 2 "$q" ;;   # find functions called by this
  refs)    cs 0 "$q" ;;   # find this C symbol (all refs)
  "" )     echo "uso: rpcs3_grep.sh {def|callers|callees|refs} <sym> | <regex>" >&2; exit 2 ;;
  *)       rg -n --type cpp -- "$mode $q" "$REF/rpcs3" "$REF/Utilities" 2>/dev/null || rg -n "$mode" "$REF/rpcs3" "$REF/Utilities" ;;
esac
