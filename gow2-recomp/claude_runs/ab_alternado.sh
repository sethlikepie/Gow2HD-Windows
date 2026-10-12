#!/bin/bash
# ab_alternado.sh BINARY SHA256 PREFIX SECS ENV_NAME
# A/B do MESMO binario alternando ENV_NAME=1 e ENV_NAME=0, quatro corridas na ordem
# on,off,on,off dentro da MESMA janela de tempo.
#
# Porque alternado e nao "todas as ON depois todas as OFF": o fps absoluto depende do
# estado da maquina -- o mesmo binario mediu 20/22 com builds a correr e 28/26 parado
# (2026-09-17). Corridas agrupadas medem a deriva da maquina, nao o gate. Alternar
# distribui a deriva pelos dois lados.
#
# Compara a mediana de DRAWS primeiro: se diferir mais que uns poucos por cento, as
# corridas nao viram a mesma cena e os fps nao sao comparaveis.
set -u
G="$HOME/Documents/PESSOAL/gow2-recomp"
R="$G/claude_runs"
BIN="$1"; WANT="$2"; PREFIX="$3"; SECS="$4"; VAR="$5"
case "$BIN" in boot_gow2_[a-z0-9]*) ;; *) echo "bad binary name"; exit 2 ;; esac
case "$PREFIX" in [a-z][a-z0-9_]*) ;; *) echo "bad prefix"; exit 2 ;; esac
[[ "$SECS" =~ ^[0-9]{1,3}$ ]] || { echo "bad secs"; exit 2; }
[[ "$WANT" =~ ^[a-f0-9]{64}$ ]] || { echo "bad sha256"; exit 2; }
[[ "$VAR" =~ ^PS3_[A-Z0-9_]+$ ]] || { echo "bad env name"; exit 2; }

echo "=== A/B alternado de $VAR ($BIN, ${SECS}s por corrida) ==="
for round in 1 2; do
  for val in 1 0; do
    tag="${PREFIX}_${val}_${round}"
    bash "$R/run_for.sh" "$BIN" "$WANT" "$tag" "$SECS" "${VAR}=${val}" > /dev/null 2>&1
    printf '%-14s %s=%s  ' "$tag" "$VAR" "$val"
    /usr/bin/python3 "$R/fps_aligned.py" "$R/$tag.log" 2>/dev/null || echo "(sem janela de gameplay)"
  done
done
exit 0
