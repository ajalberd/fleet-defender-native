param(
    [string]$Version = "0.1.0"
)

$ErrorActionPreference = "Stop"

$repoRoot     = Split-Path -Parent $MyInvocation.MyCommand.Path
$recompDir    = Join-Path $repoRoot "recomp"
$buildDir     = Join-Path $recompDir "build"
$installerDir = Join-Path $recompDir "installer"
$toolsDir     = Join-Path $recompDir "tools"
$docsDir      = Join-Path $repoRoot "docs"
$distDir      = Join-Path $repoRoot "dist"

# 1. Build f14_native.exe (GUI subsystem)
& (Join-Path $repoRoot "build.ps1") -BuildType Release

# 2. Ensure Quickstart Keyboard SVG & PNG exist
$svgChart = Join-Path $docsDir "F14_Quickstart_Keys.svg"
$pngChart = Join-Path $docsDir "F14_Quickstart_Keys.png"
if (-not (Test-Path $svgChart) -or -not (Test-Path $pngChart)) {
    Write-Host "Generating Quickstart Keyboard SVG and PNG..."
    & python (Join-Path $repoRoot "scripts\gen_quickstart_keys.py")
    if ($LASTEXITCODE -ne 0) { throw "Failed to generate Quickstart Keyboard chart!" }
}

# 3. Build Win32 GUI installer stub (PE Subsystem 2, Common-Controls v6 + asInvoker manifest)
Write-Host "Building native Win32 GUI installer stub..."
$windresExe = (Get-Command windres -ErrorAction Stop).Source
$gccExe     = (Get-Command gcc -ErrorAction Stop).Source

$resObj  = Join-Path $buildDir "installer_res.o"
$stubExe = Join-Path $buildDir "installer_stub.exe"

Push-Location $installerDir
try {
    & $windresExe --use-temp-file -O coff -i installer.rc -o $resObj
    if ($LASTEXITCODE -ne 0) { throw "windres failed compiling installer.rc!" }
} finally {
    Pop-Location
}

& $gccExe -O2 -Wall -Wextra "-DAPP_VERSION_RAW=v$Version" -mwindows -static `
    (Join-Path $installerDir "installer_gui.c") $resObj `
    -luser32 -lkernel32 -lgdi32 -ladvapi32 -lshell32 -lole32 -luuid -lcomctl32 `
    -o $stubExe
if ($LASTEXITCODE -ne 0) { throw "Failed to compile installer_stub.exe!" }

# 4. Pack 1-shot single-file self-extracting installer
New-Item -ItemType Directory -Force -Path $distDir | Out-Null
$setupExe = Join-Path $distDir "FleetDefenderGold-Native-Setup-v$Version.exe"

& python (Join-Path $toolsDir "pack_installer.py") $stubExe $setupExe `
    "f14_native.exe=$buildDir\f14_native.exe" `
    "THIRD_PARTY_LICENSES.txt=$repoRoot\THIRD_PARTY_LICENSES.txt" `
    "README_NATIVE.md=$repoRoot\README.md" `
    "docs\F14_Quickstart_Keys.svg=$svgChart" `
    "docs\F14_Quickstart_Keys.png=$pngChart"
if ($LASTEXITCODE -ne 0) { throw "Failed to pack single-file installer!" }

# 5. Build Portable ZIP
$portDir = Join-Path $buildDir "portable_stage"
if (Test-Path $portDir) { Remove-Item -Recurse -Force $portDir }
New-Item -ItemType Directory -Force -Path (Join-Path $portDir "docs") | Out-Null

Copy-Item (Join-Path $buildDir "f14_native.exe")            (Join-Path $portDir "f14_native.exe")
Copy-Item (Join-Path $repoRoot "THIRD_PARTY_LICENSES.txt")  (Join-Path $portDir "THIRD_PARTY_LICENSES.txt")
Copy-Item (Join-Path $repoRoot "README.md")                 (Join-Path $portDir "README_NATIVE.md")
Copy-Item $svgChart                                         (Join-Path $portDir "docs\F14_Quickstart_Keys.svg")
Copy-Item $pngChart                                         (Join-Path $portDir "docs\F14_Quickstart_Keys.png")

$portZip = Join-Path $distDir "FleetDefenderGold-Native-v$Version-portable.zip"
if (Test-Path $portZip) { Remove-Item -Force $portZip }
Compress-Archive -Path (Join-Path $portDir "*") -DestinationPath $portZip -CompressionLevel Optimal

# 6. Write SHA256SUMS.txt
$sumsPath = Join-Path $distDir "SHA256SUMS.txt"
$h1 = (Get-FileHash $setupExe -Algorithm SHA256).Hash.ToLower()
$h2 = (Get-FileHash $portZip  -Algorithm SHA256).Hash.ToLower()
@(
    "$h1  FleetDefenderGold-Native-Setup-v$Version.exe"
    "$h2  FleetDefenderGold-Native-v$Version-portable.zip"
) | Set-Content -Encoding ASCII $sumsPath

Write-Host "`nRelease artifacts ready in $distDir :"
Get-Item $setupExe, $portZip, $sumsPath | Format-Table Name, Length, LastWriteTime -AutoSize
Get-Content $sumsPath
