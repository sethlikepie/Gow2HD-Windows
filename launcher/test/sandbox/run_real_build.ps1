# Runs INSIDE Windows Sandbox (clean Windows: no Python, no compiler, no VC runtime, no real GPU).
# Mapped by real_build.ps1: C:\sbx (launcher, read-only), C:\game + C:\rapdir (the player's game and licence,
# read-only), C:\seed (a seed save, read-only), C:\results (writable: everything we report goes there).
$ErrorActionPreference = 'Continue'
$R = 'C:\results'
function Note([string]$s) { "$(Get-Date -Format 'HH:mm:ss') $s" | Add-Content "$R\progress.txt" }
Note 'sandbox started'
cmd /c ver | Out-File "$R\winver.txt"
Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion | Format-List | Out-File "$R\gpu.txt"
$cfg = Get-Content 'C:\sbx\sandbox_args.json' -Raw | ConvertFrom-Json
# the player's way: the launcher copied to a folder of its own, run from there
$L = "$env:USERPROFILE\Desktop\GoW2"
New-Item -ItemType Directory -Force $L | Out-Null
Copy-Item 'C:\sbx\Gow2HD-Windows.exe' $L
$inst = 'C:\GoW2-install'
if ($cfg.pkg) {   # PSN package mode (C:\pkgdir): first what the launcher's check says about the package
  & "$L\Gow2HD-Windows.exe" --detect --game $cfg.pkg | Out-File "$R\detect.txt"
  $src = @('--pkg', $cfg.pkg)
} else { $src = @('--game', $cfg.game) }
Note "silent build: $($src -join ' ') rap=$($cfg.rap)"
$sw = [Diagnostics.Stopwatch]::StartNew()
$argv = (@('--silent-build') + $src + @('--rap', $cfg.rap, '--out', $inst)) | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }
$p = Start-Process "$L\Gow2HD-Windows.exe" -ArgumentList ($argv -join ' ') `
     -RedirectStandardOutput "$R\silent.txt" -Wait -PassThru -NoNewWindow
$sw.Stop()
Note "silent build exit=$($p.ExitCode) seconds=$([int]$sw.Elapsed.TotalSeconds)"
"exit=$($p.ExitCode) seconds=$([int]$sw.Elapsed.TotalSeconds)" | Out-File "$R\build_time.txt"
Copy-Item "$inst\build.log" "$R\launcher_build.log" -ErrorAction SilentlyContinue
Copy-Item "$inst\.build\build.log" "$R\build_py.log" -ErrorAction SilentlyContinue
Copy-Item "$inst\.build\compile.log" "$R\compile.log" -ErrorAction SilentlyContinue
Copy-Item "$inst\build_info.json" "$R\build_info.json" -ErrorAction SilentlyContinue
Copy-Item "$inst\gow2_port.ini" "$R\gow2_port.ini" -ErrorAction SilentlyContinue
if (Test-Path $inst) { "{0:N0} bytes in the install" -f ((Get-ChildItem -Recurse -File $inst | Measure-Object Length -Sum).Sum) | Out-File "$R\disk.txt" }

$exe = "$inst\gow2hd.exe"
if (Test-Path $exe) {
  # hidden, muted, seed-save COPY, Port Settings ini redirected to a temp file
  $w = 'C:\gametest'; New-Item -ItemType Directory -Force $w | Out-Null
  Copy-Item -Recurse 'C:\seed' "$w\saves"
  $env:PS3_SAVEDATA_ROOT = "$w\saves"; $env:PS3_MUTE = '1'; $env:PS3_PAD_AUTOSTART = '1'
  $env:WINPORT_PSET80_INI = "$w\pset80.ini"
  Note 'starting the built game (hidden, muted) for 90 s'
  $g = Start-Process cmd.exe -ArgumentList '/c', "`"`"$exe`" > `"$R\game.log`" 2>&1`"" -WorkingDirectory $inst -PassThru -WindowStyle Hidden
  Start-Sleep -Seconds 90
  $alive = -not $g.HasExited
  $gp = Get-Process gow2hd -ErrorAction SilentlyContinue
  Note "game process alive after 90 s: $([bool]$gp)  (cmd alive: $alive)"
  $gp | Stop-Process -Force -ErrorAction SilentlyContinue
  Start-Sleep 2
  Stop-Process -Id $g.Id -Force -ErrorAction SilentlyContinue
  Get-ChildItem "$env:LOCALAPPDATA\CrashDumps" -ErrorAction SilentlyContinue | Select-Object Name, Length | Out-File "$R\crashdumps.txt"
}
Note 'done'
'done' | Out-File "$R\done.txt"
shutdown /s /t 5
