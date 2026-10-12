@echo off
rem Builds Gow2HD-Windows.exe (static, no runtime dependencies) with llvm-mingw.
rem
rem   build.cmd [release^|stub] [bundled^|download]
rem
rem   release   (default) packs the REAL build sources of this repository (build.py, LICENSE, NOTICE.md and the
rem             files of ps3recomp\, gow2-recomp\, winport\ that the build reads) with tools\pack_scripts.py:
rem             sorted, fixed timestamps, so the same commit gives the same zip
rem   stub      packs test\stub_scripts (a STUB build.py for the launcher's own tests)
rem   bundled   (default) the zip is put into the exe as a resource: one download, works offline
rem   download  the exe downloads Gow2HD-Windows-scripts-src.zip from the matching GitHub release and checks its
rem             sha256 (upload build\Gow2HD-Windows-scripts-src.zip next to the exe)
rem
rem   LLVM_MINGW  llvm-mingw folder (default: ..\tools-download\llvm-mingw, as `build.py --print-tools` unpacks it)
rem   PYTHON      Python 3 used for packing (default: py -3, else python)
setlocal
cd /d "%~dp0"
if not defined LLVM_MINGW set "LLVM_MINGW=%~dp0..\tools-download\llvm-mingw"
if not exist "%LLVM_MINGW%\bin\x86_64-w64-mingw32-clang++.exe" (
  for /d %%d in ("%LLVM_MINGW%\llvm-mingw-*") do if exist "%%d\bin\x86_64-w64-mingw32-clang++.exe" set "LLVM_MINGW=%%d"
)
set "BIN=%LLVM_MINGW%\bin"
if not exist "%BIN%\x86_64-w64-mingw32-clang++.exe" (
  echo ERROR: llvm-mingw not found in %LLVM_MINGW% ^(set LLVM_MINGW^)
  exit /b 1
)
if not defined PYTHON (
  py -3 -c "1" >nul 2>&1 && set "PYTHON=py -3"
)
if not defined PYTHON set "PYTHON=python"
set "WHAT=%~1"
if "%WHAT%"=="" set "WHAT=release"
set "MODE=%~2"
if "%MODE%"=="" set "MODE=bundled"
echo scripts: %WHAT%   mode: %MODE%
if not exist build mkdir build
del /q build\Gow2HD-Windows-scripts-src.zip build\scripts.rc build\scripts_cfg.h build\app.res.o 2>nul

rem --- zip the build scripts (deterministic)
if /i "%WHAT%"=="stub" (
  %PYTHON% tools\pack_scripts.py --dir test\stub_scripts build\Gow2HD-Windows-scripts-src.zip || exit /b 1
) else (
  %PYTHON% tools\pack_scripts.py .. build\Gow2HD-Windows-scripts-src.zip || exit /b 1
)
for /f "tokens=* delims=" %%h in ('certutil -hashfile build\Gow2HD-Windows-scripts-src.zip SHA256 ^| findstr /v ":"') do if not defined ZIPSHA set "ZIPSHA=%%h"
set "ZIPSHA=%ZIPSHA: =%"
echo scripts zip sha256: %ZIPSHA%

if /i "%MODE%"=="download" (
  > build\scripts_cfg.h echo #define SCRIPTS_BUNDLED 0
  >> build\scripts_cfg.h echo #define SCRIPTS_SHA256 "%ZIPSHA%"
  set "RCDEF="
) else (
  > build\scripts_cfg.h echo #define SCRIPTS_BUNDLED 1
  > build\scripts.rc echo SCRIPTS RCDATA "Gow2HD-Windows-scripts-src.zip"
  set "RCDEF=-DWITH_SCRIPTS"
)

"%BIN%\x86_64-w64-mingw32-windres.exe" -I res -I build %RCDEF% res\app.rc -O coff -o build\app.res.o || exit /b 1

"%BIN%\x86_64-w64-mingw32-clang++.exe" -std=c++17 -Os -municode -mwindows -static ^
  -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wall -Wno-misleading-indentation ^
  -include build\scripts_cfg.h ^
  src\main.cpp src\util.cpp src\net.cpp src\pipeline.cpp build\app.res.o ^
  -o build\Gow2HD-Windows.exe ^
  -Wl,--gc-sections -s -Wl,--no-insert-timestamp ^
  -lwinhttp -lbcrypt -ldwmapi -lole32 -luuid -lshell32 -lmsimg32 -lcomdlg32 || exit /b 1

for %%f in (build\Gow2HD-Windows.exe) do echo built build\Gow2HD-Windows.exe  %%~zf bytes
certutil -hashfile build\Gow2HD-Windows.exe SHA256 | findstr /v ":" > build\Gow2HD-Windows.exe.sha256
exit /b 0
