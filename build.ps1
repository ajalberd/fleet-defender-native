param(
    [string]$BuildType = "Release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$repoRoot  = Split-Path -Parent $MyInvocation.MyCommand.Path
$recompDir = Join-Path $repoRoot "recomp"
$buildDir  = Join-Path $recompDir "build"

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Cleaning $buildDir ..."
    Remove-Item -Recurse -Force $buildDir
}

if (-not (Test-Path (Join-Path $buildDir "build.ninja"))) {
    Write-Host "Configuring Fleet Defender Gold Native Host ($BuildType)..."
    & cmake -S $recompDir -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$BuildType"
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed!" }
}

Write-Host "Building f14_native.exe..."
& cmake --build $buildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed!" }

$exePath = Join-Path $buildDir "f14_native.exe"
if (-not (Test-Path $exePath)) { throw "Expected output missing: $exePath" }
Write-Host "Built $exePath ($((Get-Item $exePath).Length) bytes)"
