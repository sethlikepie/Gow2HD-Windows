@echo off
rem God of War II HD recompilado — launcher com o ambiente correto
rem Intro em video+audio (ffmpeg/overlay) + jogo rodando por baixo (D3D12)
cd /d "%~dp0"

set PS3_MOVIE_HLE=1
set PS3_NOMOVIES=1
set PS3_PAD_AUTOSTART=1
set PS3_RSX_BACKEND=d3d12
set PS3_RSX_FIFO=1
set PS3_VFS_ROOT=../extracted/USRDIR
set PS3_CELLSYS_REORDER=1

rem --- Intro FSM bring-up (descomente para pular a logo e abrir os WADs
rem     R_LglScA/R_PermA -- recipe validado em bt_intro_wads.sh) ---
rem set PS3_MOVIE_IO=1
rem set PS3_MOVIE_CACHE=../movie_cache
rem set PS3_MOVIE_EOS=1
rem set PS3_VDEC_FORCE_SEQDONE_MS=8000
rem set PS3_VDEC_ASYNC=1
rem set PS3_VM_LOW_MB=512
rem set PS3_VM_STACK_MB=64
rem set PS3_FIX_TBLSIZE=1
rem NOTA 1: NAO usar junto com PS3_NOMOVIES=1 (linha acima) -- conflita com
rem   o movie path do recipe. Remova/comente PS3_NOMOVIES=1 tambem, ou use
rem   rodar_gow2_intro_skip.cmd que ja vem pronto.
rem NOTA 2: PS3_VDEC_ASYNC=1 e' NECESSARIO alem do recipe original do plano
rem   -- o runtime (cellVdec.c) mudou o default de StartSeq para SYNC depois
rem   que a evidencia foi capturada; sem ASYNC=1 a thread que dispara
rem   FORCE SEQDONE nunca roda e os WADs nao abrem.

boot_v2_new.exe ../EBOOT.ELF
pause
