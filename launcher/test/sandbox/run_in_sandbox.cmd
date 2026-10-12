@echo off
rem Runs inside Windows Sandbox (clean Windows, no Python, no VC runtime). C:\sbx is mapped from the host.
setlocal
set "S=C:\sbx"
set "R=%S%\results"
if not exist "%R%" mkdir "%R%"
echo started %date% %time% > "%R%\progress.txt"
ver > "%R%\winver.txt"
rem the fake layout is made on the host; check the exe runs at all
"%S%\Gow2HD-Windows.exe" --detect --game "%S%\fake\RPCS3\dev_hdd0\game\NPUA80491" > "%R%\detect.txt"
rem also as a player would: the exe dropped into the RPCS3 folder
copy /y "%S%\Gow2HD-Windows.exe" "%S%\fake\RPCS3\" > nul
"%S%\fake\RPCS3\Gow2HD-Windows.exe" --detect > "%R%\detect_rpcs3.txt"
echo detect exit %errorlevel% >> "%R%\progress.txt"
set STUB_SECONDS=3
"%S%\Gow2HD-Windows.exe" --silent-build --game "%S%\fake\RPCS3\dev_hdd0\game\NPUA80491" --rap "%S%\fake\RPCS3\dev_hdd0\home\00000001\exdata\UP9000-NPUA80491_00-GODOFWARIIHDUS00.rap" --out "%LOCALAPPDATA%\GoW2-Windows" > "%R%\silent.txt"
echo silent exit %errorlevel% >> "%R%\progress.txt"
copy /y "%LOCALAPPDATA%\GoW2-Windows\build.log" "%R%\build.log" > nul
rem open the launcher window (home page now) and capture it
start "" "%S%\Gow2HD-Windows.exe"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Sleep 4; $p = Get-Process Gow2HD-Windows | Select-Object -First 1; & '%S%\shot.ps1' -ProcessId $p.Id -Out '%R%\sandbox_home.png'; Stop-Process -Id $p.Id" >> "%R%\progress.txt" 2>&1
echo done >> "%R%\progress.txt"
echo done > "%R%\done.txt"
