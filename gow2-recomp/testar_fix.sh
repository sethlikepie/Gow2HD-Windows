#!/bin/bash
# testar_fix.sh N -- roda o jogo com UMA correcao do documento RPCS3 ligada.
#   1  §1  saidas do vertex shader comecam em (0,0,0,1)  [PS3_RSX_FIX_OUT_W1]
#   2  §2  sem o atalho "UV vazio -> UV cru" (so' nosso)  [PS3_RSX_FIX_NO_TC0_HACK]
#   3  §3  sem semear o UV empacotado no atributo 8       [PS3_RSX_FIX_NO_UV_SEED]
#          (ja' LIGADA por default desde 2026-09-21: o 3 aqui = o 0)
#   4  §6  neblina so' do componente X                     [PS3_RSX_FIX_FOG_X]
#   0      so' os defaults (ja' inclui a 3)
#  -3      a 3 DESLIGADA (comportamento antigo, para comparar)
#  split N  UMA janela: metade ESQUERDA sem a correcao N, DIREITA com ela
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ "$1" = "split" ]; then
  export PS3_RSX_SPLIT="$2" PS3_WINDOW_TAG="SPLIT $2 (esq SEM | dir COM)"
  export JOGAR_LOG="$HERE/claude_runs/jogar_split$2.log"
  echo "[testar_fix] metade esquerda SEM / direita COM a correcao $2"
  exec "$HERE/jogar_g2.sh"
fi
case "$1" in
  1) export PS3_RSX_FIX_OUT_W1=1 ;;
  2) export PS3_RSX_FIX_NO_TC0_HACK=1 ;;
  3) export PS3_RSX_FIX_NO_UV_SEED=1 ;;
  4) export PS3_RSX_FIX_FOG_X=1 ;;
  0) ;;
  -3) export PS3_RSX_FIX_NO_UV_SEED=0 ;;
  *) sed -n '2,10p' "$0"; exit 1 ;;
esac
echo "[testar_fix] correcao $1 ligada"
export JOGAR_LOG="$HERE/claude_runs/jogar_fix$1.log"   # permite dois testes lado a lado
export PS3_WINDOW_TAG="FIX $1"                          # aparece no titulo da janela
exec "$HERE/jogar_g2.sh"
