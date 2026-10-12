#!/usr/bin/env bash
# Corre a intro do GoW2 com o produtor de EOS NATURAL ligado (Task 4 do plano
# macos-movie-eos-fsm; equivalente POSIX do rodar_gow2_intro_skip.cmd do Windows).
#
# A diferenca para o rodar_gow2.sh normal: liga PS3_MOVIE_DONE_MS=auto, que arma
# o EOS depois da duracao REAL do .wav do filme (medida do proprio ficheiro),
# quando o player esta parado em st620>=3. Isto NAO forja progresso -- o arm passa
# pela mesma disciplina do host Windows (movie_eos_should_arm) e so dispara com um
# evento de "stream acabou" real. Ver o comentario em movie_eos_arm.c.
#
# Uso:
#   ./rodar_gow2_intro_skip.sh                 # backend sdl, com janela
#   PS3_RSX_BACKEND=vulkan ./rodar_gow2_intro_skip.sh
#   PS3_NO_RSX=1 ./rodar_gow2_intro_skip.sh    # headless
#
# NOTA: o st620 so avanca alem de 1 se o host tiver folga de RAM. Com a maquina
# em swap pesado o boot fica ~3x mais lento e a FSM nao progride -- nao e o
# launcher, e o ambiente (ver smoke_intro_macos.sh).
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

[ -x ./boot_gow2 ] || { echo "boot_gow2 nao existe -- ./build_macos.sh" >&2; exit 1; }

. "$HERE/env_gow2.sh"

# O produtor de EOS por tempo real do stream. 'auto' mede a duracao do .wav.
export PS3_MOVIE_EOS=1
export PS3_MOVIE_DONE_MS="${PS3_MOVIE_DONE_MS:-auto}"
# Observabilidade da FSM ligada, para se ver o st620 a mexer.
export PS3_TRACE_MOVIEOBJ="${PS3_TRACE_MOVIEOBJ:-1}"

echo "intro-skip: PS3_MOVIE_DONE_MS=$PS3_MOVIE_DONE_MS (arm por duracao real do .wav)"
exec ./boot_gow2 EBOOT.ELF
