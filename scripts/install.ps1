# Installs the Art Shuangpin IME (阿特雙拼輸入法) on this machine.
# Ships inside the release package next to x64\ / x86\ payload directories.
#
# Usage (elevated PowerShell):
#   .\install.ps1              install or upgrade
#   .\install.ps1 -Uninstall   unregister and remove
param([switch]$Uninstall)
$ErrorActionPreference = "Stop"

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Run this script from an elevated (Administrator) PowerShell."
}

$dllName = "ArtShuangpin.dll"
$targetDir = Join-Path $env:ProgramFiles "ArtShuangpin"
$is64 = [Environment]::Is64BitOperatingSystem

function Invoke-Regsvr([string]$dll, [bool]$wow64, [switch]$Unregister) {
    # On 64-bit Windows the x86 DLL must go through the SysWOW64 regsvr32.
    $exe = if ($wow64) { "$env:windir\SysWOW64\regsvr32.exe" } else { "$env:windir\System32\regsvr32.exe" }
    $flags = @("/s"); if ($Unregister) { $flags += "/u" }
    & $exe @flags $dll
    if (-not $Unregister -and $LASTEXITCODE -ne 0) {
        throw "regsvr32 failed for $dll (exit $LASTEXITCODE)"
    }
}

function Get-InstalledDlls {
    # Returns @{ Path; Wow64 } entries for DLLs present under $targetDir.
    $list = @()
    $x64 = Join-Path $targetDir "x64\$dllName"
    $x86 = Join-Path $targetDir "x86\$dllName"
    if ($is64) {
        if (Test-Path $x64) { $list += @{ Path = $x64; Wow64 = $false } }
        if (Test-Path $x86) { $list += @{ Path = $x86; Wow64 = $true } }
    } elseif (Test-Path $x86) {
        $list += @{ Path = $x86; Wow64 = $false }
    }
    return $list
}

if ($Uninstall) {
    foreach ($e in Get-InstalledDlls) { Invoke-Regsvr $e.Path $e.Wow64 -Unregister }
    if (Test-Path $targetDir) { Remove-Item -Recurse -Force $targetDir }
    Write-Output "Uninstalled. Sign out and back in to fully clear the input method list."
    exit 0
}

$srcX64 = Join-Path $PSScriptRoot "x64"
$srcX86 = Join-Path $PSScriptRoot "x86"
if ($is64 -and -not (Test-Path (Join-Path $srcX64 $dllName))) {
    throw "Missing $srcX64\$dllName - run this script from the extracted package directory."
}
if (-not $is64 -and -not (Test-Path (Join-Path $srcX86 $dllName))) {
    throw "This is 32-bit Windows and the package has no x86\$dllName."
}

# Files parked by an earlier upgrade (see Copy-Payload). Whatever had them
# mapped has almost certainly exited by now, so sweep them before copying;
# anything still locked simply stays for the next run. Without this they
# accumulate about 8 MB per upgrade.
function Remove-ParkedFiles {
    if (-not (Test-Path $targetDir)) { return }
    $removed = 0
    foreach ($f in Get-ChildItem $targetDir -Recurse -File) {
        if ($f.Name -notmatch '\.old\.[0-9a-f]{8}$') { continue }
        try {
            Remove-Item $f.FullName -Force -ErrorAction Stop
            $removed++
        } catch {
            # Still mapped by a running process; leave it for next time.
        }
    }
    if ($removed -gt 0) { Write-Output "Cleaned up $removed parked file(s) from earlier upgrades." }
}

# Upgrade path: a running TSF host may keep the old DLL mapped; renaming a
# mapped DLL is allowed on Windows, so park it aside before copying.
function Copy-Payload([string]$srcDir, [string]$arch) {
    if (-not (Test-Path $srcDir)) { return }
    $dstDir = Join-Path $targetDir $arch
    New-Item -ItemType Directory -Force $dstDir | Out-Null
    foreach ($f in Get-ChildItem $srcDir -File) {
        $dst = Join-Path $dstDir $f.Name
        try { Copy-Item $f.FullName $dst -Force -ErrorAction Stop }
        catch {
            $old = "$dst.old." + [Guid]::NewGuid().ToString("N").Substring(0, 8)
            Rename-Item $dst $old -Force
            Copy-Item $f.FullName $dst -Force
        }
    }
}

Remove-ParkedFiles
Copy-Payload $srcX64 "x64"
Copy-Payload $srcX86 "x86"

# AppContainer hosts (Start menu search, Edge fields) must be able to read the
# files. S-1-15-2-1 is ALL APPLICATION PACKAGES (locale-independent form).
icacls $targetDir /grant "*S-1-15-2-1:(OI)(CI)(RX)" /T | Out-Null

foreach ($e in Get-InstalledDlls) { Invoke-Regsvr $e.Path $e.Wow64 }

Write-Output "Installed to $targetDir and registered."
Write-Output "The IME appears under the Chinese (Traditional, Taiwan) language;"
Write-Output "add that language in Settings first if it is missing."
Write-Output "If the IME does not show up in the input list, sign out and back in."
