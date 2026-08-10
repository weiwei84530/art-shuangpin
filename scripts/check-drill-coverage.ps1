# Audits the typing drills against the whole keyboard.
#
# Walks every key combination the IME accepts -- each first key on its own,
# and every first+second pair, filtered by the dictionary exactly as
# SyllableInput does -- and reports the ones no lesson ever asks for.
# Anything listed in drills\skip-syllables.txt is excused; anything else
# makes this fail, so "the drills cover the keyboard" stays true over time.
#
#   scripts\check-drill-coverage.ps1 [-Data out\data.txt] [-Build build]

[CmdletBinding()]
param(
    [string]$Data = "out\data.txt",
    [string]$Build = "build",
    [string]$Allow = "drills\skip-syllables.txt"
)

# Native tools report through the exit code; "Stop" would turn any stderr
# line into a terminating error before we can read it.
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$gen = Join-Path $root "$Build\Release\drill_gen.exe"
if (-not (Test-Path $gen)) {
    Write-Host "building drill_gen..."
    cmake --build $Build --config Release --target drill_gen | Out-Null
}
if (-not (Test-Path $Data)) {
    throw "$Data not found -- run scripts\build-data.ps1 first"
}

# The audit needs the same lesson set the site ships, filler included.
& $gen --data $Data --lessons drills\lessons.txt --filler drills\filler.txt `
       --out $env:TEMP\drill-audit.js --allow $Allow |
    Where-Object { $_ -notmatch '^\s*note:' -and $_ -notmatch 'chars, .* keystrokes$' }
$code = $LASTEXITCODE
Remove-Item $env:TEMP\drill-audit.js -ErrorAction SilentlyContinue

if ($code -eq 0) {
    Write-Host "`nOK：每一個鍵位組合都練得到。" -ForegroundColor Green
} elseif ($code -eq 2) {
    Write-Host "`n還有鍵位沒有練到（見上表）。要嘛補課文，要嘛寫進 $Allow 並說明理由。" `
        -ForegroundColor Yellow
} else {
    throw "drill_gen failed with exit code $code"
}
exit $code
