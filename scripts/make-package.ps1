# Builds a self-contained install package (zip) for another machine.
# Sources the freshly built DLLs (same locations as deploy-dev.ps1) plus the
# dictionary, renames the DLL to its public name, and bundles install.ps1.
param(
    [string]$Version,
    [string]$X64Build = "ime\x64\Release",
    [string]$X86Build = "ime\Release"
)
$ErrorActionPreference = "Stop"

$root = Split-Path $PSScriptRoot -Parent
if (-not $Version) {
    # The root VERSION file is the whole repository's single version: mac's
    # Makefile stamps the same value into Info.plist, so a release cannot
    # ship a Windows zip and a macOS app that disagree. git describe stays as
    # the fallback, which is what a package built off an untagged commit gets.
    $versionFile = Join-Path $root "VERSION"
    if (Test-Path $versionFile) {
        $Version = "v" + (Get-Content $versionFile -Raw).Trim()
    } else {
        $Version = (& git -C $root describe --tags --always 2>$null)
        if (-not $Version) { $Version = "dev" }
    }
}

$data = Join-Path $root "out\data.txt"
if (-not (Test-Path $data)) { throw "Missing out\data.txt - run scripts\build-data.ps1 first" }

$stage = Join-Path $root "out\package\art-shuangpin"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }

$staged = 0
foreach ($pair in @(@($X64Build, "x64"), @($X86Build, "x86"))) {
    $src = Join-Path $root (Join-Path $pair[0] "SampleIME.dll")
    if (-not (Test-Path $src)) { Write-Output "skip $($pair[1]): $src not built"; continue }
    $dstDir = Join-Path $stage $pair[1]
    New-Item -ItemType Directory -Force $dstDir | Out-Null
    Copy-Item $src (Join-Path $dstDir "ArtShuangpin.dll")
    Copy-Item $data (Join-Path $dstDir "mspy-data.txt")
    $staged++
}
if ($staged -eq 0) { throw "No built DLLs found - build the ime solution first" }
if ($staged -lt 2) { Write-Output "WARN: packaging only $staged architecture(s); 32-bit apps need the x86 DLL" }

Copy-Item (Join-Path $PSScriptRoot "install.ps1") $stage
Copy-Item (Join-Path $PSScriptRoot "install.bat") $stage
Copy-Item (Join-Path $PSScriptRoot "uninstall.bat") $stage

$zip = Join-Path $root "out\art-shuangpin-$Version.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Output "Package: $zip"
