@echo off
rem God of War II HD recompilado — launcher com o recipe de intro-skip LIGADO
rem Pula a logo (FORCE SEQDONE) e abre os WADs R_LglScA/R_PermA de forma
rem reproduzivel (ver bt_intro_wads.sh). PS3_MOVIE_HLE fica ligado para o
rem overlay ffmpeg da intro continuar visivel enquanto o jogo carrega por
rem baixo (D3D12).
cd /d "%~dp0"

set PS3_MOVIE_HLE=1
set PS3_PAD_AUTOSTART=1
set PS3_RSX_BACKEND=d3d12
set PS3_RSX_FIFO=1
set PS3_VFS_ROOT=../extracted/USRDIR
set PS3_CELLSYS_REORDER=1

rem --- Intro FSM bring-up (recipe validado em bt_intro_wads.sh) ---
set PS3_MOVIE_IO=1
set PS3_MOVIE_CACHE=../movie_cache
set PS3_MOVIE_EOS=1
set PS3_VDEC_FORCE_SEQDONE_MS=8000
set PS3_VDEC_ASYNC=1
set PS3_VM_LOW_MB=512
set PS3_VM_STACK_MB=64
set PS3_FIX_TBLSIZE=1

boot_v2_new.exe ../EBOOT.ELF
pause
