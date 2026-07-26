# Copies freshly built IME DLLs into out\deploy\ (the registered location).
# If a DLL is locked by a running TSF host, it is renamed aside first
# (renaming a mapped DLL is allowed on Windows); restart the test app afterwards.
param(
    [string]$DllName = "SampleIME.dll",
    # Build output locations; adjusted when the build system changes (M0: msbuild, M1+: CMake).
    [string]$X64Build = "ime\x64\Release",
    [string]$X86Build = "ime\Win32\Release"
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
        if (Test-Path $old) { try { Remove-Item $old -Force -ErrorAction Stop } catch {} }
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
Write-Output "Restart the test app (e.g. Notepad) to load the new DLL."
