# Copies freshly built IME DLLs into out\deploy\ (the registered location).
# If a DLL is locked by a running TSF host, it is renamed aside first
# (renaming a mapped DLL is allowed on Windows); restart the test app afterwards.
param(
    [string]$DllName = "SampleIME.dll",
    # Build output locations; adjusted when the build system changes (M0: msbuild, M1+: CMake).
    [string]$X64Build = "ime\x64\Release",
    # The Win32 configuration writes to ime\Release (no platform subdir).
    [string]$X86Build = "ime\Release"
)
$ErrorActionPreference = "Stop"

$root = Split-Path $PSScriptRoot -Parent
$deployDir = Join-Path $root "out\deploy"

function Deploy-One([string]$srcDir, [string]$arch) {
    $src = Join-Path $root (Join-Path $srcDir $DllName)
    if (-not (Test-Path $src)) { Write-Output "skip ${arch}: $src not built"; return }
    $dstDir = Join-Path $deployDir $arch
    New-Item -ItemType Directory -Force $dstDir | Out-Null
    $dst = Join-Path $dstDir $DllName
    if (Test-Path $dst) {
        $old = "$dst.old"
        if (Test-Path $old) {
            try { Remove-Item $old -Force -ErrorAction Stop }
            catch {
                # Previous .old is still mapped by a process; park under a
                # unique name instead.
                $old = "$dst.old." + [Guid]::NewGuid().ToString("N").Substring(0, 8)
            }
        }
        try { Copy-Item $src $dst -Force -ErrorAction Stop }
        catch {
            Rename-Item $dst $old -Force
            Copy-Item $src $dst -Force
        }
    } else {
        Copy-Item $src $dst -Force
    }
    Write-Output "deployed ${arch}: $dst"
}

Deploy-One $X64Build "x64"
Deploy-One $X86Build "x86"

# SampleIME (M0 stock build) resolves TEXTSERVICE_DIC directly NEXT TO the
# DLL (verified via debug log), so the dictionary goes into the arch root.
$dictSrc = Join-Path $root "ime\SampleIME\Dictionary\SampleIMESimplifiedQuanPin.txt"
if (Test-Path $dictSrc) {
    foreach ($arch in "x64", "x86") {
        $dstDir = Join-Path $deployDir $arch
        if (Test-Path $dstDir) {
            Copy-Item -Force $dictSrc $dstDir
            $legacy = Join-Path $dstDir "Dictionary"
            if (Test-Path $legacy) { Remove-Item -Recurse -Force $legacy }
        }
    }
}
Write-Output "Restart the test app (e.g. Notepad) to load the new DLL."
