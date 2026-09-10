param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$SoftfpVitaSdk = $(if (Test-Path 'C:\Users\Max\tools\vitasdk\bin\arm-vita-eabi-gcc.exe') { 'C:\Users\Max\tools\vitasdk' } else { $env:VITASDK }),
    [string]$GameObb,
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [ValidateRange(0, 65536)]
    [int]$StressReadKiBPerSecond = 0,
    [ValidateRange(0, 100000)]
    [int]$StressReadLatencyUs = 0,
    [switch]$ReplayInput
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = if ($BuildDirectory) { [IO.Path]::GetFullPath($BuildDirectory) } else { Join-Path $repoRoot 'build-vita-direct' }
$outDir = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $repoRoot 'out' }
if (($StressReadKiBPerSecond -or $StressReadLatencyUs) -and
    (-not $BuildDirectory -or -not $OutputDirectory -or
     $buildDir -eq (Join-Path $repoRoot 'build-vita-direct') -or
     $outDir -eq (Join-Path $repoRoot 'out'))) {
    throw 'Read stress requires separate build and output directories; it must not replace the release VPK.'
}
if ($ReplayInput -and (-not $BuildDirectory -or -not $OutputDirectory -or
    $buildDir -eq (Join-Path $repoRoot 'build-vita-direct') -or $outDir -eq (Join-Path $repoRoot 'out'))) {
    throw 'Input replay requires separate build and output directories; it must not replace the release VPK.'
}

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
# Always pass zero defaults so a reused build cache cannot retain stress settings.
cmake -S (Join-Path $repoRoot 'vita\direct') -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" `
    "-DPVZ2_STRESS_READ_KIB=$StressReadKiBPerSecond" "-DPVZ2_STRESS_READ_LATENCY_US=$StressReadLatencyUs" `
    "-DPVZ2_INPUT_REPLAY=$($ReplayInput.IsPresent.ToString().ToUpperInvariant())"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw "Vita build failed with exit code $LASTEXITCODE" }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Copy-Item -Force (Join-Path $buildDir 'pvz2-vita-452-60fps.vpk') (Join-Path $outDir 'pvz2-vita-latest.vpk')
Get-FileHash -Algorithm SHA256 (Join-Path $outDir 'pvz2-vita-latest.vpk')
