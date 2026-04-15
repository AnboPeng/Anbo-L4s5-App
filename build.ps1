<# .SYNOPSIS
    One-click build script for the l4s5_Anbo project.

    Prerequisites: PowerShell 5.1+ and internet access.
    Everything else (CMake, Ninja, ARM toolchain, STM32 SDK) is
    auto-downloaded to the tools/ directory on first run.

    Usage:
        .\build.ps1                        # configure + build (Debug)
        .\build.ps1 -Clean                 # delete build/ and rebuild
        .\build.ps1 -Release               # build in Release mode
        .\build.ps1 -Version 1.2.3         # set firmware version
#>
param(
    [switch]$Clean,
    [switch]$Release,
    [string]$Version = ''
)
$ErrorActionPreference = 'Stop'

$ROOT      = $PSScriptRoot
$TOOLS     = Join-Path $ROOT 'tools'
$BUILD     = Join-Path $ROOT 'build'

# ---- Versions (edit to upgrade) ----
$CMAKE_VER = '4.0.1'
$NINJA_VER = '1.12.1'

# ================================================================
#  Helper: ensure a tool exists, download if missing
# ================================================================
function Ensure-Tool {
    param(
        [string]$Name,
        [string]$ExePath,
        [string]$Url,
        [string]$ArchiveName
    )
    if (Test-Path $ExePath) { return }

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "  $Name not found - downloading ..."
    Write-Host "  URL: $Url"
    Write-Host "============================================================"
    Write-Host ""

    New-Item -ItemType Directory -Force $TOOLS | Out-Null
    $archive = Join-Path $TOOLS $ArchiveName

    Write-Host "Downloading using native curl.exe..."
    curl.exe -L -o $archive $Url
    if ($LASTEXITCODE -ne 0) { 
        throw "$Name download failed via curl.exe" 
    }

    Write-Host "Extracting $ArchiveName ..."
    Expand-Archive -Path $archive -DestinationPath $TOOLS -Force
    Remove-Item $archive

    if (-not (Test-Path $ExePath)) {
        throw "$Name extraction failed - expected $ExePath"
    }
    Write-Host "$Name ready."
    Write-Host ""
}

# ---- CMake ----
$cmakeDir = Join-Path $TOOLS "cmake-${CMAKE_VER}-windows-x86_64"
$cmakeExe = Join-Path $cmakeDir 'bin\cmake.exe'
$cmakeUrl = "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VER}/cmake-${CMAKE_VER}-windows-x86_64.zip"

# Check system PATH first
$sysCmake = Get-Command cmake -ErrorAction SilentlyContinue
if ($sysCmake) {
    $cmakeExe = $sysCmake.Source
    Write-Host "[OK] CMake (system): $cmakeExe"
} else {
    Ensure-Tool 'CMake' $cmakeExe $cmakeUrl "cmake-${CMAKE_VER}-windows-x86_64.zip"
    Write-Host "[OK] CMake (local):  $cmakeExe"
}

# ---- Ninja ----
$ninjaExe = Join-Path $TOOLS 'ninja.exe'
$ninjaUrl = "https://github.com/ninja-build/ninja/releases/download/v${NINJA_VER}/ninja-win.zip"

$sysNinja = Get-Command ninja -ErrorAction SilentlyContinue
if ($sysNinja) {
    $ninjaExe = $sysNinja.Source
    Write-Host "[OK] Ninja (system): $ninjaExe"
} else {
    Ensure-Tool 'Ninja' $ninjaExe $ninjaUrl 'ninja-win.zip'
    Write-Host "[OK] Ninja (local):  $ninjaExe"
}

# ---- Add tools to PATH for this session ----
$env:PATH = "$(Split-Path $cmakeExe);$(Split-Path $ninjaExe);$env:PATH"

# ---- Clean ----
if ($Clean -and (Test-Path $BUILD)) {
    Write-Host "Cleaning build/ ..."
    Remove-Item -Recurse -Force $BUILD
}

# ---- Configure ----
$buildType = if ($Release) { 'Release' } else { 'Debug' }
$toolchain = Join-Path $ROOT 'cmake\arm-none-eabi.cmake'

# Force reconfigure when version is specified (CACHE override)
if ($Version -and (Test-Path (Join-Path $BUILD 'build.ninja'))) {
    Write-Host "Reconfiguring for version $Version ..."
    Remove-Item (Join-Path $BUILD 'CMakeCache.txt') -ErrorAction SilentlyContinue
}

$versionFlag = @()
if ($Version) { $versionFlag = @("-DFW_VERSION=$Version") }

if (-not (Test-Path (Join-Path $BUILD 'build.ninja'))) {
    Write-Host ""
    Write-Host "Configuring ($buildType) ..."
    & $cmakeExe -B $BUILD -G Ninja `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
        "-DCMAKE_BUILD_TYPE=$buildType" `
        @versionFlag
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
}

# ---- Build ----
Write-Host ""
Write-Host "Building ..."
& $cmakeExe --build $BUILD
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# ---- Summary ----
Write-Host ""
Write-Host "============================================================"
Write-Host "  BUILD SUCCESSFUL"
Write-Host "  Output:"
Get-ChildItem (Join-Path $BUILD 'l4s5_anbo*') |
    ForEach-Object { Write-Host ("    {0,-20} {1,10:N0} bytes" -f $_.Name, $_.Length) }
Write-Host "============================================================"
