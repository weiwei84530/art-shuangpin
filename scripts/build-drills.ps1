# Rebuilds web\drills.js from drills\lessons.txt.
#
# Two passes: the first tells us which syllables the hand-written lessons
# already reach, make-filler-lessons.py picks dictionary words to cover the
# rest, and the second pass generates the data the tutorial site plays back.
#
#   scripts\build-drills.ps1 [-Data out\data.txt] [-Build build]

[CmdletBinding()]
param(
    [string]$Data = "out\data.txt",
    [string]$Build = "build"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$gen = Join-Path $root "$Build\Release\drill_gen.exe"
if (-not (Test-Path $gen)) {
    Write-Host "building drill_gen..."
    cmake --build $Build --config Release --target drill_gen | Out-Null
}
if (-not (Test-Path $Data)) {
    throw "$Data not found -- run scripts\build-data.ps1 first"
}

$syllables = "out\drill-syllables.txt"
Write-Host "pass 1: what the graded lessons already cover"
& $gen --data $Data --lessons drills\lessons.txt --out out\drills-pass1.js `
       --syllables $syllables | Where-Object { $_ -notmatch '^  note' }
if ($LASTEXITCODE -ne 0) { throw "drill_gen failed" }

Write-Host "`nfilling in the rest"
python scripts\make-filler-lessons.py --data $Data --covered $syllables `
       --out drills\filler.txt
if ($LASTEXITCODE -ne 0) { throw "make-filler-lessons.py failed" }

Write-Host "`npass 2: the real thing"
& $gen --data $Data --lessons drills\lessons.txt --lessons drills\filler.txt `
       --out web\drills.js --coverage |
    Where-Object { $_ -notmatch '^  note' }
if ($LASTEXITCODE -ne 0) { throw "drill_gen failed" }

Remove-Item out\drills-pass1.js -ErrorAction SilentlyContinue
