# Builds out\data.txt from the vendored McBopomofo data sources (data\).
# Mirrors the three-step chain in data\Makefile:
#   frequency_builder -> main_compiler (data-raw.txt) -> postprocess (data.txt)
$ErrorActionPreference = "Stop"

# The curation scripts open files without an explicit encoding; force UTF-8
# so they don't fall back to the system ANSI codepage (cp950 on zh-TW).
$env:PYTHONUTF8 = "1"

$root = Split-Path $PSScriptRoot -Parent
$dataDir = Join-Path $root "data"
$outDir = Join-Path $root "out"
New-Item -ItemType Directory -Force $outDir | Out-Null

Push-Location $dataDir
try {
    # Step 1: PhraseFreq.txt from phrase.occ + exclusion.txt (writes into data\).
    python -m curation.builders.frequency_builder
    if ($LASTEXITCODE -ne 0) { throw "frequency_builder failed" }

    # Step 2: data-raw.txt
    python -m curation.compilers.main_compiler `
        --heterophony1 heterophony1.list `
        --heterophony2 heterophony2.list `
        --heterophony3 heterophony3.list `
        --phrase_freq PhraseFreq.txt `
        --bpmf_mappings BPMFMappings.txt `
        --bpmf_base BPMFBase.txt `
        --punctuations BPMFPunctuations.txt `
        --symbols Symbols.txt `
        --macros Macros.txt `
        --output (Join-Path $outDir "data-raw.txt")
    if ($LASTEXITCODE -ne 0) { throw "main_compiler failed" }

    # Step 3: data.txt
    python -m curation.compilers.postprocess `
        --input (Join-Path $outDir "data-raw.txt") `
        --directive Postprocess.txt `
        --output (Join-Path $outDir "data.txt")
    if ($LASTEXITCODE -ne 0) { throw "postprocess failed" }

    # Step 4: relaxed-tone alias keys for neutral-tone phrases (兒子/什麼
    # reachable without tone digits; see docs/spec.md).
    python (Join-Path $PSScriptRoot "add_neutral_phrase_aliases.py") `
        (Join-Path $outDir "data.txt")
    if ($LASTEXITCODE -ne 0) { throw "add_neutral_phrase_aliases failed" }
}
finally {
    Pop-Location
}

# Normalize to LF: ParselessPhraseDB requires exact "\n" line endings, but
# Python on Windows writes CRLF in text mode.
$dataTxt = Join-Path $outDir "data.txt"
$text = [System.IO.File]::ReadAllText($dataTxt)
[System.IO.File]::WriteAllText($dataTxt, $text.Replace("`r`n", "`n"),
    (New-Object System.Text.UTF8Encoding $false))

Write-Output "OK: $dataTxt"
