# Audits the hand-written tutorial lessons against the real engine.
#
# The drills are generated, so they cannot drift; the lessons in
# web\tutorials.js are written by hand and can. This replays every lesson
# through mspy::Composer and reports two kinds of rot: a screen the engine
# no longer produces, and a syllable the lesson spends more keys on than it
# needs to. Both make this fail, so "the tutorial teaches what the IME
# actually does" stays true over time.
#
#   scripts\check-tutorials.ps1 [-Data out\data.txt] [-Build build]

[CmdletBinding()]
param(
    [string]$Data = "out\data.txt",
    [string]$Build = "build"
)

# Native tools report through the exit code; "Stop" would turn any stderr
# line into a terminating error before we can read it.
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$repl = Join-Path $root "$Build\Release\repl.exe"
if (-not (Test-Path $repl)) {
    Write-Host "building repl..."
    cmake --build $Build --config Release --target repl | Out-Null
}
if (-not (Test-Path $Data)) {
    throw "$Data not found -- run scripts\build-data.ps1 first"
}
if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    throw "node not found -- the audit script needs Node.js"
}

node scripts\check-tutorials.mjs --repl $repl --data $Data
$code = $LASTEXITCODE

if ($code -eq 0) {
    Write-Host "`nOK：教學課程與引擎一致。" -ForegroundColor Green
} elseif ($code -eq 2) {
    Write-Host "`n教學課程與引擎不符（見上表）。" -ForegroundColor Yellow
} else {
    throw "check-tutorials.mjs failed with exit code $code"
}
exit $code
