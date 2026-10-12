# Env canonico do God of War II HD (NPUA80491) -- fonte unica.
#
# Sourced por rodar_gow2.sh e smoke_boot_mac.sh. O equivalente Windows vive em
# recomp_mid_v2/rodar_gow2.cmd; manter os dois em sincronia, porque o plano
# exige que Mac e Windows classifiquem o mesmo wall (Phase 1 Task 1.4 de
# ../ps3recomp/docs/superpowers/plans/2026-07-20-gow2-full-bringup.md).
#
# Nao e executavel: e para `source`. Todas as variaveis respeitam valores ja
# definidos pelo chamador, para o smoke poder sobrepor sem editar este ficheiro.
#
# Uso:
#   . "$(dirname "$0")/env_gow2.sh"

# Raiz do VFS: onde os mount points do PS3 caem no host.
: "${PS3_VFS_ROOT:=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)/extracted/USRDIR}"
export PS3_VFS_ROOT

# Filmes por HLE em vez de decode real. PS3_VDEC_ASYNC=1 e obrigatorio quando
# se liga decode de video a serio (ver o CLAUDE.md do motor).
# Overlay VT do host DESLIGADO (2026-09-23): com o cellVdec a responder BUSY
# (ps3recomp c1c1bb7a) e a entregar RGBA (4f803bb3) o jogo recebe todas as
# imagens e desenha o proprio video; o overlay por cima fazia a intro aparecer
# duas vezes. Medido sem ele: intro ate' ao menu, cutscene in-game inteira com
# fim natural. PS3_MOVIE_HLE=1 repoe o overlay.
: "${PS3_MOVIE_HLE:=0}";        export PS3_MOVIE_HLE
# Fila de logos do host (aviso legal / SCEA / Bluepoint) DESLIGADA junto com o
# overlay: ela so' liberava a imagem do aviso no fim do overlay de filme, e sem
# overlay o aviso ficava na tela para sempre com o jogo a correr por baixo. O
# proprio jogo desenha essas telas. PS3_BOOT_LOGO_QUEUE=1 repoe.
: "${PS3_BOOT_LOGO_QUEUE:=0}";  export PS3_BOOT_LOGO_QUEUE
# Workers de SPU: o GoW2 pede um SPURS de 2 SPUs, mas o PS3 da' 6 ao jogo. Com 2
# as tasks do descompactador (spu0) ocupavam os dois e o mixer SCREAM ficava sem
# vez -- som mudo no gameplay (medido 2026-09-23 por sample).
: "${PS3_SPU_WORKERS:=6}";      export PS3_SPU_WORKERS
# Ritmo de flip por relogio proprio de 60 Hz DESLIGADO: a camada Metal ja'
# espera o vsync da tela (displaySyncEnabled) e os dois relogios brigavam --
# medido na intro (PS3_TRACE_VDEC_PICTIME/PRESENTTIME): imagens 35,7 +- 25,9 ms
# com ele, 33,1 +- 2,2 ms sem ele. PS3_GCM_VSYNC=1 repoe.
: "${PS3_GCM_VSYNC:=0}";        export PS3_GCM_VSYNC

# PS3_NOMOVIES deixou de ser 1 por default. Era, quando o macOS nao tinha
# caminho de dados nenhum: o movie_hle.c inteiro era #ifdef _WIN32 e os
# movie_io_* eram stubs no-op. Com o porte POSIX (0318caa) e os membros
# extraidos do psarc para movie_cache/, o titulo passa a ler WADs a serio --
# e o R_PermA e' o que alimenta o registry de shaders. Continuar com
# NOMOVIES=1 mataria exactamente a rota que se quer exercitar (Plano 04
# Task 3 diz isto por palavras). Quem quiser saltar a intro poe =1 a mao.
: "${PS3_NOMOVIES:=0}";         export PS3_NOMOVIES

# Caminho de I/O dos membros do psarc ja extraidos (R_PermA, R_LglScA e a
# intro). Sem MOVIE_IO=1 o movie_io_open nem e' consultado e o jogo cai no
# cellFs normal, que nao acha estes nomes.
: "${PS3_MOVIE_IO:=1}";         export PS3_MOVIE_IO
: "${PS3_MOVIE_CACHE:=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)/movie_cache}"
export PS3_MOVIE_CACHE
: "${PS3_MOVIE_EOS:=1}";        export PS3_MOVIE_EOS

# Decode de video assincrono. Obrigatorio no caminho da intro (o sync voltou a
# ser default noutro commit; ver CLAUDE.md do motor). Respeitado se ja definido.
: "${PS3_VDEC_ASYNC:=1}";       export PS3_VDEC_ASYNC

# Pad ligado a arranque, senao o jogo espera input que nunca chega.
: "${PS3_PAD_AUTOSTART:=1}";    export PS3_PAD_AUTOSTART

# Som: MUDO por default nas corridas de desenvolvimento. O ciclo de
# medicao repete a mesma intro dezenas de vezes por sessao e o audio
# torna-se insuportavel. Uma unica variavel cobre os DOIS caminhos de
# audio (cellAudio e o WAV do overlay do filme). Para ouvir:
#   PS3_MUTE= ./rodar_gow2_menu_fast.sh
: "${PS3_MUTE:=1}";            export PS3_MUTE
[ -n "${PS3_MUTE:-}" ] || unset PS3_MUTE

# SPU1 = dearch / EDGE-zlib (fp 0x2A5C4E67A14505B8). Hit limpo in-boot
# (2026-07-22: HIT>=1, MISS=0, SPUJOB clean). Necessario para consumo
# real de WAD apos R_Perm; sem isto o dispatch fica MISS e o path de
# texturas WAD/UI nao avanca. spu2/3 continuam opt-in (PS3_SPU2/3).
: "${PS3_SPU1:=1}";             export PS3_SPU1
# Policy module do SCREAM (fala e efeitos). Sem isto o dispatcher descarta
# a imagem 0xCEDB9A67 e a porta cellAudio fica sem PCM. PS3_SPU6=0 desliga.
: "${PS3_SPU6:=1}";             export PS3_SPU6

# FIFO do RSX consumido pelo backend.
: "${PS3_RSX_FIFO:=1}";         export PS3_RSX_FIFO

# Backend RSX (M10): no Darwin, default Metal com janela. Headless usa
# PS3_NO_RSX=1 e nao precisa de backend. Respeita valor ja definido.
if [ -z "${PS3_NO_RSX:-}" ]; then
    case "$(uname -s 2>/dev/null)" in
        Darwin) : "${PS3_RSX_BACKEND:=metal}" ;;
        *)      : "${PS3_RSX_BACKEND:=sdl}" ;;
    esac
    export PS3_RSX_BACKEND
fi

# Reordenacao do cellSysutil exigida pela ordem de init do titulo.
: "${PS3_CELLSYS_REORDER:=1}";  export PS3_CELLSYS_REORDER

# Pos-intro sem FREELIST-TAG-GUARD (E466 vis / 240 s medido): callback GCM,
# lwmutex real, sticky FIOS, produtor de EOS. PS3_GCM_CB=0 / LWMUTEX_STUB=1
# restauram o legado que aborta no open do WAD.
: "${PS3_GCM_CB:=1}";              export PS3_GCM_CB
: "${PS3_LWMUTEX_REAL:=1}";        export PS3_LWMUTEX_REAL
: "${PS3_FIOS_STICKY_OWNER:=1}";   export PS3_FIOS_STICKY_OWNER
: "${PS3_MOVIE_DONE_MS:=3000}";    export PS3_MOVIE_DONE_MS

# E466: sem isto os draws caem num só RT (texturas amassadas, sem 3D).
: "${PS3_METAL_PER_DRAW_RT:=1}";   export PS3_METAL_PER_DRAW_RT
# Host present: MetalFX spatial 720p→Retina, in-encoder clears, GPU Morton,
# vsync/ProMotion. HDR EDR off: estourava o manto do Colosso em ciano.
# PS3_METAL_HDR=1 religa. PS3_METALFX=0 / PASS_MERGE=0 / GPU_DESWIZZLE=0
# desligam cada um. VSync NAO e' forcado aqui: sem PS3_METAL_VSYNC do chamador
# vale o arquivo de configuracoes do overlay (default ligado); PS3_METAL_VSYNC=0
# ou =1 exportado por quem chama continua a mandar.
: "${PS3_METALFX:=1}";                 export PS3_METALFX
: "${PS3_METAL_PASS_MERGE:=1}";        export PS3_METAL_PASS_MERGE
: "${PS3_METAL_GPU_DESWIZZLE:=1}";     export PS3_METAL_GPU_DESWIZZLE
if [ -n "${PS3_METAL_VSYNC:-}" ]; then export PS3_METAL_VSYNC; fi
: "${PS3_METAL_HDR:=0}";               export PS3_METAL_HDR
# Diagnostic: Always/no-write. After BEGIN-coalesce the 3D is stable; this
# left every triangle visible and additive-white. PS3_METAL_DEBUG_NODEPTH=1
# restores the E466 probe.
: "${PS3_METAL_DEBUG_NODEPTH:=0}"; export PS3_METAL_DEBUG_NODEPTH
# Class B depth diagnostics (default B0 = RSX). always / lequal / gequal / invert.
: "${PS3_METAL_DEBUG_DEPTH:=rsx}"; export PS3_METAL_DEBUG_DEPTH
# Diagnostic: black-clear the flip's color target once per frame (first 3D pass).
: "${PS3_METAL_DEBUG_CLEAR_FRAME:=0}"; export PS3_METAL_DEBUG_CLEAR_FRAME
