<#
.SYNOPSIS
  Deterministic regression test for the GigaPets PC port.

.DESCRIPTION
  Runs the exe in GIGAPETS_SELFTEST mode (fixed timestep, fixed RNG seed,
  scripted input, scripted Mod Menu / cheat / filter changes) inside a clean
  temp copy, and prints hashes of the emulated state at fixed frame
  checkpoints. If -Expected is given, compares against that file and exits 1
  on any mismatch.

  Use it before and after a refactor: identical hashes mean identical behavior.
  Requires your own ROM (rom.u7). The hashes are specific to your GPU/driver
  for the 'screen=' column, so record your own baseline rather than sharing one.

.EXAMPLE
  # Record a baseline from the current build
  .\tools\selftest.ps1 -Rom C:\path\to\rom.u7 -Save baseline.txt

  # After changing code, rebuild and compare
  .\tools\selftest.ps1 -Rom C:\path\to\rom.u7 -Expected baseline.txt
#>
param(
    [Parameter(Mandatory = $true)][string]$Rom,
    [string]$Exe = "build\Release\GigaPetsPC.exe",
    [string]$Expected,
    [string]$Save,
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = "Stop"
$exePath = (Resolve-Path $Exe).Path
$exeDir = Split-Path $exePath
$work = Join-Path ([System.IO.Path]::GetTempPath()) ("gigapets_selftest_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path "$work\resources\data" -Force | Out-Null
Copy-Item $exePath $work
Copy-Item (Join-Path $exeDir "resources\hud_icons") "$work\resources\" -Recurse
Copy-Item (Join-Path $exeDir "resources\shaders") "$work\resources\" -Recurse
Copy-Item $Rom "$work\resources\data\rom.u7"

$env:GIGAPETS_SELFTEST = "1"
$proc = Start-Process (Join-Path $work "GigaPetsPC.exe") -PassThru
if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
    $proc.Kill()
    Write-Error "Self-test did not finish within $TimeoutSeconds seconds."
}

$hashFile = Join-Path $work "selftest_hashes.txt"
if (-not (Test-Path $hashFile)) { Write-Error "No selftest_hashes.txt produced (did the exe start and find the ROM?)." }
$actual = Get-Content $hashFile
$actual | ForEach-Object { Write-Host $_ }

if ($Save) { Copy-Item $hashFile $Save -Force; Write-Host "Saved baseline to $Save" }

$exit = 0
if ($Expected) {
    $want = Get-Content $Expected
    if (($actual -join "`n") -eq ($want -join "`n")) {
        Write-Host "SELFTEST PASS: hashes match $Expected" -ForegroundColor Green
    } else {
        Write-Host "SELFTEST FAIL: hashes differ from $Expected" -ForegroundColor Red
        Compare-Object $want $actual | Format-Table -AutoSize | Out-String | Write-Host
        $exit = 1
    }
}

Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
exit $exit
