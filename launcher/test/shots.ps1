# Opens each launcher page briefly and captures it with PrintWindow into ..\shots\.
# Uses a test install folder (default %TEMP%\gow2test\inst, made by a stub build) - never Seth's real install.
param([string]$Inst = "$env:TEMP\gow2test\inst", [string]$Game = "", [string]$Rap = "")
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\Gow2HD-Windows.exe"
$shot = Join-Path $root "tools\shot.ps1"
$out = Join-Path $root "shots"

function Snap([string]$name, [string[]]$argv, [int]$wait = 3) {
  $p = Start-Process $exe -ArgumentList $argv -PassThru
  Start-Sleep -Seconds $wait
  powershell -NoProfile -ExecutionPolicy Bypass -File $shot -ProcessId $p.Id -Out (Join-Path $out "$name.png")
  Stop-Process -Id $p.Id -ErrorAction SilentlyContinue
  Start-Sleep -Milliseconds 500
}

$empty = Join-Path $env:TEMP "gow2test\empty_install"
Snap "01_setup_not_found" @('--install', $empty, '--setup')
if ($Game -and $Rap) { Snap "02_setup_found" @('--install', $empty, '--setup', '--game', $Game, '--rap', $Rap) }
Snap "03_home" @('--install', $Inst)
Snap "04_settings" @('--install', $Inst, '--page', 'settings')
Snap "05_about" @('--install', $Inst, '--page', 'about')
if ($Game -and $Rap) {
  $env:STUB_SECONDS = "40"
  Snap "06_building" @('--install', $Inst, '--page', 'build', '--game', $Game, '--rap', $Rap) 9
  $env:STUB_SECONDS = "3"; $env:STUB_FAIL = "45"
  Snap "07_build_failed" @('--install', $Inst, '--page', 'build', '--game', $Game, '--rap', $Rap) 8
  Remove-Item Env:STUB_FAIL
  Snap "08_build_done" @('--install', $Inst, '--page', 'build', '--game', $Game, '--rap', $Rap) 8
}
