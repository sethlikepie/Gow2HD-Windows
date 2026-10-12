#!/usr/bin/env bash
# (Re)gera os indices de navegacao do clone RPCS3: universal-ctags (simbolos) +
# cscope (call/xref graph). Corre depois de um git pull no clone _ref_rpcs3.
# Requer: brew install universal-ctags cscope
set -euo pipefail
REF="${RPCS3_REF:-/Users/andrebrumcortezferreira/Documents/PESSOAL/_ref_rpcs3}"
CTAGS="$(command -v uctags || echo /opt/homebrew/bin/ctags)"
cd "$REF"
echo "ctags -> tags ..."
"$CTAGS" -R --languages=C,C++ --kinds-C++=+p --fields=+iaSl --extras=+q -f tags rpcs3 Utilities
echo "  $(wc -l < tags | tr -d ' ') simbolos"
echo "cscope -> cscope.out ..."
find rpcs3 Utilities -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.cc' \) > cscope.files
cscope -bqk -i cscope.files -f cscope.out
echo "  cscope.out $(du -h cscope.out | cut -f1)"
