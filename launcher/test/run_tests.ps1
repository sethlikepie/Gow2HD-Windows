# Functional tests for Gow2HD-Windows.exe with the STUB build.py (bundled by build.cmd by default).
#   powershell -ExecutionPolicy Bypass -File test\run_tests.ps1 -Game <NPUA80491 dir> -Rap <rap file>
# Works in a scratch folder under %TEMP%; the game folder and rap are only read.
param([Parameter(Mandatory)][string]$Game, [Parameter(Mandatory)][string]$Rap, [string]$Work = "$env:TEMP\gow2test")
$ErrorActionPreference = "Continue"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\Gow2HD-Windows.exe"
$inst = Join-Path $Work "inst"
$script:fails = 0
$regBackup = (Get-ItemProperty -Path HKCU:\Software\GoW2-Windows -Name InstallDir -ErrorAction SilentlyContinue).InstallDir

function Check([string]$name, [bool]$ok, [string]$detail = "") {
  if ($ok) { "PASS  $name" } else { "FAIL  $name  $detail"; $script:fails++ }
}
function Silent([string[]]$extra, [hashtable]$envs = @{}) {
  foreach ($k in $envs.Keys) { Set-Item "Env:$k" $envs[$k] }
  $out = Join-Path $Work "silent_out.txt"
  $argv = @('--silent-build', '--game', $Game, '--rap', $Rap, '--out', $inst) + $extra
  $p = Start-Process $exe -ArgumentList $argv -RedirectStandardOutput $out -Wait -PassThru -NoNewWindow
  foreach ($k in $envs.Keys) { Remove-Item "Env:$k" }
  return @{ code = $p.ExitCode; text = (Get-Content $out -Raw) }
}
function PyProcs { @(Get-CimInstance Win32_Process -Filter "Name='python.exe'" | Where-Object { $_.ExecutablePath -like "$inst*" }) }

New-Item -ItemType Directory -Force $Work | Out-Null
$env:STUB_SECONDS = "2"

# 1. full build (downloads Python + tools on first run)
$r = Silent @('--jobs', '2')
Check "silent build succeeds" ($r.code -eq 0) $r.text
Check "python sha256 verified" ($r.text -match "Python verified: sha256 4acbed6d" -or $r.text -notmatch "Downloading Python")
Check "EXE line honoured" ((Get-Content "$inst\launcher_state.ini" -Raw) -match "exe=.*gow2.exe")

# 2. rebuild reuses tools
$r = Silent @()
Check "rebuild reuses tools" ($r.code -eq 0 -and $r.text -match "Tool already present: ninja" -and $r.text -notmatch "Downloading Python") $r.text

# 3. wrong sha256 is refused
$r = Silent @() @{ STUB_BADHASH = "1" }
Check "bad sha256 refused" ($r.code -eq 7 -and $r.text -match "Checksum mismatch") $r.text
Check "no state built after failure" ((Get-Content "$inst\launcher_state.ini" -Raw) -match "built=0")

# 4. build.py failure is reported
$r = Silent @() @{ STUB_FAIL = "50" }
Check "build.py ERROR reported" ($r.code -eq 10 -and $r.text -match "ERROR Stub failure at 70") $r.text

# 5. resume a partial Python download (HTTP Range)
Remove-Item -Recurse -Force "$inst\tools\python", "$inst\tools\.stamps\python.txt" -ErrorAction SilentlyContinue
$full = Join-Path $Work "py_full.zip"
if (-not (Test-Path $full)) { Invoke-WebRequest "https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip" -OutFile $full -UseBasicParsing }
$bytes = [IO.File]::ReadAllBytes($full)
[IO.File]::WriteAllBytes("$inst\downloads\python-3.12.10-embed-amd64.zip.part", $bytes[0..(4MB - 1)])
$r = Silent @()
Check "partial download resumed" ($r.code -eq 0 -and $r.text -match "Resuming python-3.12.10-embed-amd64.zip.part at 4.0 MB") $r.text

# 6. corrupt partial download is detected and restarted
Remove-Item -Recurse -Force "$inst\tools\python", "$inst\tools\.stamps\python.txt" -ErrorAction SilentlyContinue
$junk = New-Object byte[] (4MB); (New-Object Random 1).NextBytes($junk)
[IO.File]::WriteAllBytes("$inst\downloads\python-3.12.10-embed-amd64.zip.part", $junk)
$r = Silent @()
Check "corrupt partial restarted and verified" ($r.code -eq 0 -and $r.text -match "(?s)Checksum mismatch.*starting over") $r.text

# 7. argument and game checks
$p = Start-Process $exe -ArgumentList @('--silent-build', '--game', $Game) -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$Work\o.txt"
Check "missing args -> exit 2" ($p.ExitCode -eq 2)
python (Join-Path $PSScriptRoot "make_fake_layout.py") "$Work\fake" | Out-Null
$p = Start-Process $exe -ArgumentList @('--silent-build', '--game', "$Work\fake\wrongver", '--rap', $Rap, '--out', "$Work\inst_wrong") -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$Work\o.txt"
Check "wrong game version -> exit 3" ($p.ExitCode -eq 3 -and (Get-Content "$Work\o.txt" -Raw) -match "Only NPUA80491 v01.00 US is supported")

# 8. --play starts the built exe from its folder, with port_settings.ini next to it and NO launcher environment
$r = Silent @()
Set-Content -Path "$inst\port_settings.ini" -Value "fps_cap=144`r`nborderless=1`r`nres_scale=2`r`nfps_overlay=1" -Encoding ascii
Remove-Item "$inst\stub_game_report.txt" -ErrorAction SilentlyContinue
Start-Process $exe -ArgumentList @('--play', '--install', $inst) -Wait
Start-Sleep 2
$rep = Get-Content "$inst\stub_game_report.txt" -Raw -ErrorAction SilentlyContinue
Check "--play launched game" ($rep -match "fps_cap=144" -and $rep -match [regex]::Escape("cwd=$inst")) $rep
Check "--play passes no environment" ($rep -match "(?m)^PS3_VFS_ROOT=\r?$" -and $rep -match "(?m)^WINPORT_PORT_SETTINGS=\r?$" -and
                                      $rep -match "(?m)^PYTHONIOENCODING=\r?$") $rep

# 8b. update check with no published release (404) is not an error
$env:GOW2_UPDATE_REPO = "sethlikepie/Gow2HD-Windows-no-such-repo-for-tests"
$p = Start-Process $exe -ArgumentList @('--check-update') -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$Work\o.txt"
Remove-Item Env:GOW2_UPDATE_REPO
Check "no release -> graceful" ($p.ExitCode -eq 0 -and (Get-Content "$Work\o.txt" -Raw) -match "latest=none") (Get-Content "$Work\o.txt" -Raw)

# 9. GUI cancel kills the whole process tree (python + its child)
$env:STUB_HANG = "1"; $env:GOW2_TEST_CANCEL_MS = "8000"
$p = Start-Process $exe -ArgumentList @('--install', $inst, '--page', 'build', '--game', $Game, '--rap', $Rap) -PassThru
Start-Sleep 6
$during = (PyProcs).Count
Start-Sleep 5
$after = (PyProcs).Count
Check "cancel kills python tree" ($during -ge 2 -and $after -eq 0) "during=$during after=$after"
Stop-Process -Id $p.Id -ErrorAction SilentlyContinue
Remove-Item Env:GOW2_TEST_CANCEL_MS

# 10. killing the launcher itself kills the build (job object)
$p = Start-Process $exe -ArgumentList @('--install', $inst, '--page', 'build', '--game', $Game, '--rap', $Rap) -PassThru
Start-Sleep 6
$during = (PyProcs).Count
Stop-Process -Id $p.Id -Force
Start-Sleep 2
$after = (PyProcs).Count
Check "launcher death kills build" ($during -ge 2 -and $after -eq 0) "during=$during after=$after"
Remove-Item Env:STUB_HANG

# 11. remove build: build output and tools go; port_settings.ini, the state and any save folder stay.
#     The save root is put INSIDE movie_cache on purpose: that folder must then be kept.
$r = Silent @()
New-Item -ItemType Directory -Force "$inst\movie_cache\saves_test" | Out-Null
Set-Content "$inst\movie_cache\saves_test\save.dat" "seed" -Encoding ascii
$env:PS3_SAVEDATA_ROOT = "$inst\movie_cache\saves_test"
$p = Start-Process $exe -ArgumentList @('--remove-build', '--install', $inst) -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$Work\o.txt"
$dry = Get-Content "$Work\o.txt" -Raw
Check "remove dry run deletes nothing" ($p.ExitCode -eq 0 -and $dry -match "dry run" -and (Test-Path "$inst\gow2.exe")) $dry
$p = Start-Process $exe -ArgumentList @('--remove-build', '--install', $inst, '--yes') -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$Work\o.txt"
Remove-Item Env:PS3_SAVEDATA_ROOT
$o = Get-Content "$Work\o.txt" -Raw
Check "remove build" ($p.ExitCode -eq 0 -and -not (Test-Path "$inst\gow2.exe") -and -not (Test-Path "$inst\tools") -and
                      -not (Test-Path "$inst\scripts") -and -not (Test-Path "$inst\.build") -and -not (Test-Path "$inst\gow2_port.ini")) $o
Check "remove keeps settings, state, saves" ((Test-Path "$inst\port_settings.ini") -and (Test-Path "$inst\launcher_state.ini") -and
                                             (Test-Path "$inst\movie_cache\saves_test\save.dat")) $o
Check "state says not built" ((Get-Content "$inst\launcher_state.ini" -Raw) -match "built=0")

# restore the registry value the tests changed
if ($regBackup) { Set-ItemProperty HKCU:\Software\GoW2-Windows -Name InstallDir -Value $regBackup }
else { Remove-Item HKCU:\Software\GoW2-Windows -Recurse -ErrorAction SilentlyContinue }
"--- $script:fails failure(s)"
exit $script:fails
