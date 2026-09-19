# Regenerates the realtime-source signature for the installed Gw2-64.exe when it differs
# from the committed one, and bumps the patch version. Used by .github/workflows/gw2-resign.yml
# on the self-hosted runner; also fine to run by hand from the repo root.
#
# Outputs an object { Changed; Version } and exits non-zero on failure. Requires Binary Ninja's
# Python API (`python -c "import binaryninja"`).
param(
    [string]$GameExe = 'C:\Program Files\Guild Wars 2\Gw2-64.exe',
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$sigs = Join-Path $root 'signatures\arcdps_boon_magnifier_sigs.ini'
$buildFile = Join-Path $root 'signatures\gw2_build.txt'

function Get-PeTimestamp([string]$path) {
    $fs = [System.IO.File]::OpenRead($path)
    try {
        $br = New-Object System.IO.BinaryReader($fs)
        $fs.Seek(0x3C, 'Begin') | Out-Null
        $lfanew = $br.ReadUInt32()
        $fs.Seek($lfanew + 8, 'Begin') | Out-Null
        return $br.ReadUInt32()
    } finally { $fs.Dispose() }
}

if (-not (Test-Path $GameExe)) { throw "Game exe not found: $GameExe" }
$exeTs = Get-PeTimestamp $GameExe
$signedTs = [Convert]::ToUInt32(((Select-String -Path $sigs -Pattern 'pe_timestamp=(\S+)').Matches[0].Groups[1].Value), 16)
Write-Host ("installed exe timestamp 0x{0:x8}, signed 0x{1:x8}" -f $exeTs, $signedTs)

if ($exeTs -eq $signedTs -and -not $Force) {
    Write-Host 'Signatures already match the installed game.'
    return [pscustomobject]@{ Changed = $false; Version = '' }
}

# Regenerate straight from the exe (full analysis; ~10-20 minutes on a 43 MB client).
$tmp = Join-Path $env:TEMP 'arcdps_boon_magnifier_sigs.new.ini'
python (Join-Path $root 'tools\binja\make_sigs.py') $GameExe $tmp --targets (Join-Path $root 'tools\binja\targets_gw2.py') --enable
if ($LASTEXITCODE -ne 0) { throw 'make_sigs.py failed: a locator or signature no longer resolves. Reverse-engineer the change (docs/gw2-buff-internals.md) and update tools/binja/targets_gw2.py.' }
Copy-Item $tmp $sigs -Force

# Record the CDN build id (best effort) and bump the patch version.
try { (Invoke-RestMethod -Uri 'http://assetcdn.101.arenanetworks.com/latest64/101' -TimeoutSec 30).Split(' ')[0].Trim() | Set-Content $buildFile } catch { Write-Warning "CDN build lookup failed: $_" }

$cm = Join-Path $root 'CMakeLists.txt'
$text = Get-Content $cm -Raw
$m = [regex]::Match($text, 'project\((\S+) VERSION (\d+)\.(\d+)\.(\d+)')
$next = "$($m.Groups[2].Value).$($m.Groups[3].Value).$([int]$m.Groups[4].Value + 1)"
$text = $text.Replace($m.Value, "project($($m.Groups[1].Value) VERSION $next")
Set-Content $cm $text -NoNewline
Write-Host "Signatures regenerated; version -> $next"
return [pscustomobject]@{ Changed = $true; Version = $next }
