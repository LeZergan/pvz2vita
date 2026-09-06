param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$SoftfpVitaSdk = $(if (Test-Path 'C:\Users\Max\tools\vitasdk\bin\arm-vita-eabi-gcc.exe') { 'C:\Users\Max\tools\vitasdk' } else { $env:VITASDK }),
    [string]$GameObb
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot 'build-vita-direct'
$outDir = Join-Path $repoRoot 'out'

if (-not (Test-Path (Join-Path $SoftfpVitaSdk 'bin\arm-vita-eabi-gcc.exe'))) {
    throw "Softfp VitaSDK not found at $SoftfpVitaSdk"
}
$env:VITASDK = (Resolve-Path $SoftfpVitaSdk).Path
$env:Path = (Join-Path $env:VITASDK 'bin') + [IO.Path]::PathSeparator + $env:Path

$abi = & (Join-Path $env:VITASDK 'bin\arm-vita-eabi-gcc.exe') -Q --help=target 2>$null |
    Select-String -- '-mfloat-abi=' | Select-Object -First 1
if ($abi -notmatch 'softfp') {
    throw "Selected VitaSDK does not default to softfp: $abi"
}

py -3.12 (Join-Path $PSScriptRoot 'prepare-livearea.py')
if ($LASTEXITCODE -ne 0) { throw "LiveArea preparation failed" }
if ($GameObb) {
    py -3.12 (Join-Path $PSScriptRoot 'build-rsb-index.py') --obb $GameObb
} else {
    py -3.12 (Join-Path $PSScriptRoot 'build-rsb-index.py')
}
if ($LASTEXITCODE -ne 0) { throw "RSB index generation failed" }
cmake -S (Join-Path $repoRoot 'vita\direct') -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw "Vita build failed with exit code $LASTEXITCODE" }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Copy-Item -Force (Join-Path $buildDir 'pvz2-vita-452-60fps.vpk') (Join-Path $outDir 'pvz2-vita-latest.vpk')
Get-FileHash -Algorithm SHA256 (Join-Path $outDir 'pvz2-vita-latest.vpk')
