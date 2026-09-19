# Configures and builds the plugin + tests with MSVC (x64) and Ninja, then runs the tests.
# Usage: .\scripts\build.ps1 [-Config Release|Debug] [-SkipTests]
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')] [string]$Config = 'Release',
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw 'Visual Studio (with the C++ workload) is required.' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio install with C++ tools found.' }

Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

$build = Join-Path $root "build\$Config"
cmake -S $root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build
if ($LASTEXITCODE) { exit $LASTEXITCODE }

if (-not $SkipTests) {
    & (Join-Path $build 'core_tests.exe')
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    foreach ($smoke in 'smoke_nexus.exe', 'smoke_arcdps.exe') {
        & (Join-Path $build $smoke) (Join-Path $build 'arcdps_boon_magnifier.dll')
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
    }
}
Write-Host "`nBuilt: $(Join-Path $build 'arcdps_boon_magnifier.dll')"
