# Host side of the Windows Sandbox end-to-end test with the REAL build (downloads + full build + game start).
#   powershell -ExecutionPolicy Bypass -File test\sandbox\real_build.ps1 -Exe build\Gow2HD-Windows.exe `
#       (-Game <NPUA80491 dir> | -Pkg <PSN .pkg>) -Rap <.rap file> -Seed <seed save folder> -Work <empty scratch folder>
#       [-TimeoutMin 90]
# The game folder (or the folder holding the .pkg), the .rap's folder and the seed save are mapped READ-ONLY; only
# <Work>\results is writable. With -Pkg, put the .pkg and the .rap in a folder of their own (nothing else is mapped).
# Waits for results\done.txt (the sandbox shuts itself down), then makes sure the sandbox is closed.
param([string]$Game, [string]$Pkg, [Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$Rap,
      [Parameter(Mandatory)][string]$Seed, [Parameter(Mandatory)][string]$Work, [int]$TimeoutMin = 90, [int]$MemoryMB = 16384)
if ([bool]$Game -eq [bool]$Pkg) { throw 'give -Game or -Pkg' }
$sbx = Join-Path $Work 'sbx'; $res = Join-Path $Work 'results'
New-Item -ItemType Directory -Force $sbx, $res | Out-Null
Copy-Item $Exe "$sbx\Gow2HD-Windows.exe" -Force
Copy-Item (Join-Path $PSScriptRoot 'run_real_build.ps1') $sbx -Force
$rapDir = Split-Path (Resolve-Path $Rap) -Parent
function Map([string]$h, [string]$s, [bool]$ro) {
  "    <MappedFolder><HostFolder>$h</HostFolder><SandboxFolder>$s</SandboxFolder><ReadOnly>$($ro.ToString().ToLower())</ReadOnly></MappedFolder>"
}
$maps = @((Map $sbx 'C:\sbx' $true))
if ($Pkg) {
  $pkgDir = Split-Path (Resolve-Path $Pkg) -Parent
  $cfg = @{ pkg = 'C:\pkgdir\' + (Split-Path $Pkg -Leaf) }
  $maps += Map $pkgDir 'C:\pkgdir' $true
  if ($pkgDir -ne $rapDir) { $maps += Map $rapDir 'C:\rapdir' $true; $cfg.rap = 'C:\rapdir\' + (Split-Path $Rap -Leaf) }
  else { $cfg.rap = 'C:\pkgdir\' + (Split-Path $Rap -Leaf) }
} else {
  $cfg = @{ game = 'C:\game\' + (Split-Path $Game -Leaf); rap = 'C:\rapdir\' + (Split-Path $Rap -Leaf) }
  $maps += Map (Resolve-Path $Game) ('C:\game\' + (Split-Path $Game -Leaf)) $true
  $maps += Map $rapDir 'C:\rapdir' $true
}
$maps += Map (Resolve-Path $Seed) 'C:\seed' $true
$maps += Map $res 'C:\results' $false
$cfg | ConvertTo-Json | Set-Content "$sbx\sandbox_args.json" -Encoding ascii
$wsb = @"
<Configuration>
  <Networking>Enable</Networking>
  <MemoryInMB>$MemoryMB</MemoryInMB>
  <MappedFolders>
$($maps -join "`r`n")
  </MappedFolders>
  <LogonCommand>
    <Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\sbx\run_real_build.ps1</Command>
  </LogonCommand>
</Configuration>
"@
$wsbPath = Join-Path $Work 'gow2_real_build.wsb'
Set-Content $wsbPath $wsb -Encoding utf8
Remove-Item "$res\done.txt" -ErrorAction SilentlyContinue
$sw = [Diagnostics.Stopwatch]::StartNew()
Start-Process (Join-Path $env:WINDIR 'System32\WindowsSandbox.exe') -ArgumentList "`"$wsbPath`""
while (-not (Test-Path "$res\done.txt") -and $sw.Elapsed.TotalMinutes -lt $TimeoutMin) { Start-Sleep 15 }
"sandbox wall time (boot + downloads + build + game run): $([int]$sw.Elapsed.TotalSeconds) s; done=$(Test-Path "$res\done.txt")"
Start-Sleep 30
Get-Process WindowsSandbox, WindowsSandboxClient, WindowsSandboxRemoteSession -ErrorAction SilentlyContinue | Stop-Process -Force
