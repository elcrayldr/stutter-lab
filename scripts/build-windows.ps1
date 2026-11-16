param(
    [string]$BuildDir = "build",
    [string]$Configuration = "Release",
    [switch]$InstallVST3
)
$ErrorActionPreference = "Stop"

Write-Host "[1/4] Configure STUTTER LAB (JUCE 8.0.6, x64)..." -ForegroundColor Cyan
cmake -S STUTTER_LAB_VST3 -B $BuildDir -A x64 -DCMAKE_BUILD_TYPE=$Configuration -DSTUTTERLAB_BUILD_TESTS=ON

Write-Host "[2/4] Build VST3 and core tests..." -ForegroundColor Cyan
cmake --build $BuildDir --config $Configuration --target STUTTER_LAB_VST3 StutterLabCoreTests --parallel

Write-Host "[3/4] Run core tests..." -ForegroundColor Cyan
ctest --test-dir $BuildDir -C $Configuration --output-on-failure

$Plugin = Join-Path $BuildDir "STUTTER_LAB_artefacts\$Configuration\VST3\STUTTER LAB.vst3"
if (!(Test-Path $Plugin)) { throw "VST3 bundle not found: $Plugin" }
Write-Host "Built bundle: $Plugin" -ForegroundColor Green

if ($InstallVST3) {
    $Destination = Join-Path $env:CommonProgramFiles "VST3\STUTTER LAB.vst3"
    New-Item -ItemType Directory -Path (Split-Path $Destination) -Force | Out-Null
    if (Test-Path $Destination) { Remove-Item -LiteralPath $Destination -Recurse -Force }
    Copy-Item -LiteralPath $Plugin -Destination $Destination -Recurse -Force
    Write-Host "Installed to: $Destination" -ForegroundColor Green
}
