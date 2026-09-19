# Copies the built DLL into the GW2 addons folder. Close the game first: Windows
# keeps a loaded DLL locked. May need an elevated shell if GW2 is under Program Files.
# Usage: .\scripts\deploy.ps1 [-Config Release] [-AddonsDir 'D:\GW2\addons']
param(
    [string]$Config = 'Release',
    [string]$AddonsDir = 'C:\Program Files\Guild Wars 2\addons'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root "build\$Config\arcdps_boon_magnifier.dll"

if (-not (Test-Path $dll)) { throw "Build first: $dll not found (run scripts\build.ps1)." }
if (-not (Test-Path $AddonsDir)) { throw "Addons folder not found: $AddonsDir" }
if (Get-Process -Name 'Gw2-64' -ErrorAction SilentlyContinue) { throw 'Guild Wars 2 is running; close it before deploying.' }

Copy-Item $dll (Join-Path $AddonsDir 'arcdps_boon_magnifier.dll') -Force
# Optional realtime-source signature file (made by tools\binja\make_sigs.py); deployed when present.
$sigs = Join-Path $root "build\$Config\arcdps_boon_magnifier_sigs.ini"
if (Test-Path $sigs) {
    Copy-Item $sigs (Join-Path $AddonsDir 'arcdps_boon_magnifier_sigs.ini') -Force
    Write-Host "Deployed DLL + signature file to $AddonsDir"
} else {
    Write-Host "Deployed DLL to $AddonsDir (no signature file: realtime source stays off)"
}
