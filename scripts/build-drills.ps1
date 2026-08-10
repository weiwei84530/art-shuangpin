# Rebuilds web\drills.js from drills\lessons.txt.
#
# The drill never corrects anything, so every line has to come out of the
# sentence walk exactly as written. Hand-written lines that do not are a
# hard error (reword them); generated filler lines are dropped, their word
# is banned, and the filler is re-picked -- which is why this loops.
#
#   scripts\build-drills.ps1 [-Data out\data.txt] [-Build build]

[CmdletBinding()]
param(
    [string]$Data = "out\data.txt",
    [string]$Build = "build",
    [int]$MaxRounds = 8
)

# Native tools report progress on stdout and failures through the exit
# code; "Stop" would turn any stderr line into a terminating error.
$ErrorActionPreference = "Continue"
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

$quiet = { $_ -notmatch '^\s*note:' }
$syllables = "out\drill-syllables.txt"
$reachable = "out\drill-reachable.txt"
$dropped = "out\drill-dropped.txt"
$banned = "out\drill-banned.txt"
$avoid = "drills\avoid-words.txt"
Remove-Item $banned -ErrorAction SilentlyContinue

# The ban list starts with the words we have decided against by hand; the
# rounds below add the ones the sentence walk gets wrong.
if (Test-Path $avoid) {
    $seed = @(Get-Content $avoid -Encoding UTF8 |
              ForEach-Object { ($_ -split '#')[0].Trim() } |
              Where-Object { $_ })
    if ($seed.Count -gt 0) {
        Write-Host "avoiding by hand: $($seed -join ' ')"
        [System.IO.File]::WriteAllLines(
            (Join-Path $root $banned), [string[]]$seed,
            (New-Object System.Text.UTF8Encoding($false)))
    }
}

Write-Host "pass 1: what the graded lessons already cover"
# --reachable is the whole keyboard: every combination the decoder plus the
# dictionary accept, which is what the filler has to fill in.
& $gen --data $Data --lessons drills\lessons.txt --out out\drills-pass1.js `
       --syllables $syllables --reachable $reachable | Where-Object $quiet
if ($LASTEXITCODE -ne 0) { throw "a hand-written line does not convert cleanly" }

for ($round = 1; $round -le $MaxRounds; $round++) {
    Write-Host "`nround ${round}: filling in the rest"
    python scripts\make-filler-lessons.py --data $Data --covered $syllables `
           --targets $reachable --exclude $banned --out drills\filler.txt
    if ($LASTEXITCODE -ne 0) { throw "make-filler-lessons.py failed" }

    & $gen --data $Data --lessons drills\lessons.txt --filler drills\filler.txt `
           --out web\drills.js --dropped $dropped --coverage |
        Where-Object $quiet
    if ($LASTEXITCODE -ne 0) { throw "drill_gen failed" }

    # -Encoding UTF8 is not optional: the default in 5.1 is the ANSI
    # codepage, which turns every Chinese word into question marks.
    $bad = @(Get-Content $dropped -Encoding UTF8 -ErrorAction SilentlyContinue |
             Where-Object { $_.Trim() })
    if ($bad.Count -eq 0) {
        Write-Host "`nevery line converts cleanly."
        break
    }
    Write-Host "banning: $($bad -join ' ')"
    # Plain UTF-8 with no BOM: Add-Content -Encoding utf8 writes one on
    # every append, and the reader would take it as part of the word.
    [System.IO.File]::AppendAllLines(
        (Join-Path $root $banned), [string[]]$bad,
        (New-Object System.Text.UTF8Encoding($false)))
    if ($round -eq $MaxRounds) {
        Write-Warning "still dropping lines after $MaxRounds rounds"
    }
}

Remove-Item out\drills-pass1.js -ErrorAction SilentlyContinue
