# Registers (or unregisters) the dev-build IME DLLs. Run as Administrator.
# Registration stores the DLL *path*; rebuilds only need deploy-dev.ps1 afterwards.
param(
    [switch]$Unregister,
    # DLL base name; SampleIME during M0, MspyIME from M3 on.
    [string]$DllName = "SampleIME.dll"
)
$ErrorActionPreference = "Stop"

$root = Split-Path $PSScriptRoot -Parent
$deployDir = Join-Path $root "out\deploy"
$x64Dll = Join-Path $deployDir "x64\$DllName"
$x86Dll = Join-Path $deployDir "x86\$DllName"

if ($Unregister) {
    if (Test-Path $x64Dll) { & regsvr32.exe /u /s $x64Dll }
    if (Test-Path $x86Dll) { & "$env:windir\SysWOW64\regsvr32.exe" /u /s $x86Dll }
    Write-Output "Unregistered."
    exit 0
}

# AppContainer hosts (Start menu search, Edge fields) must be able to read the DLL.
icacls $deployDir /grant "ALL APPLICATION PACKAGES:(OI)(CI)(RX)" /T | Out-Null

if (-not (Test-Path $x64Dll)) { throw "Missing $x64Dll - run deploy-dev.ps1 first" }
& regsvr32.exe /s $x64Dll
if ($LASTEXITCODE -ne 0) { throw "x64 registration failed ($LASTEXITCODE)" }
Write-Output "Registered x64: $x64Dll"

if (Test-Path $x86Dll) {
    & "$env:windir\SysWOW64\regsvr32.exe" /s $x86Dll
    if ($LASTEXITCODE -ne 0) { throw "x86 registration failed ($LASTEXITCODE)" }
    Write-Output "Registered x86: $x86Dll"
}
