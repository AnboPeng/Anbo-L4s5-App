<# .SYNOPSIS
    One-click unit test runner for the l4s5_Anbo project.

    Auto-downloads portable Ruby and installs Ceedling gem locally
    into tools/ on first run.  Requires native gcc on PATH.

    Usage:
        .\test.ps1                     # run all tests
        .\test.ps1 test:anbo_rb        # run a single module
        .\test.ps1 clean               # clean test artifacts
#>
param(
    [Parameter(ValueFromRemainingArguments)]
    [string[]]$CeedlingArgs
)
$ErrorActionPreference = 'Stop'

$ROOT  = $PSScriptRoot
$TOOLS = Join-Path $ROOT 'tools'

# ---- Versions ----
$RUBY_VER     = '3.3.7-1'
$CEEDLING_VER = '0.31.1'
$W64DEV_VER   = '2.0.0'

# ================================================================
#  Helper: download + extract (reused from build.ps1)
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

    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $Url -OutFile $archive -UseBasicParsing

    Write-Host "Extracting $ArchiveName ..."
    if ($ArchiveName -match '\.7z$') {
        # Try 7z, then tar (Windows 10+ libarchive)
        $sevenZ = Get-Command 7z -ErrorAction SilentlyContinue
        if ($sevenZ) {
            & 7z x $archive -o"$TOOLS" -y | Out-Null
        } else {
            & tar -xf $archive -C $TOOLS
        }
    } else {
        Expand-Archive -Path $archive -DestinationPath $TOOLS -Force
    }
    Remove-Item $archive -ErrorAction SilentlyContinue

    if (-not (Test-Path $ExePath)) {
        throw "$Name extraction failed - expected $ExePath"
    }
    Write-Host "$Name ready."
    Write-Host ""
}

# ================================================================
#  1. Portable Ruby
# ================================================================
$rubyDir = Join-Path $TOOLS "ruby-${RUBY_VER}-x64-mingw-ucrt"
$rubyExe = Join-Path $rubyDir 'bin\ruby.exe'
$rubyUrl = "https://github.com/oneclick/rubyinstaller2/releases/download/RubyInstaller-${RUBY_VER}/rubyinstaller-${RUBY_VER}-x64.7z"

# Check system PATH first
$sysRuby = Get-Command ruby -ErrorAction SilentlyContinue
if ($sysRuby) {
    $rubyExe = $sysRuby.Source
    Write-Host "[OK] Ruby (system): $rubyExe"
} else {
    # RubyInstaller extracts as rubyinstaller-X.Y.Z-N-x64 — rename
    Ensure-Tool 'Ruby' $rubyExe $rubyUrl "ruby-${RUBY_VER}.7z"

    # Handle rename if needed
    if (-not (Test-Path $rubyExe)) {
        $candidate = Get-ChildItem $TOOLS -Directory -Filter 'rubyinstaller-*' |
                     Where-Object { Test-Path (Join-Path $_.FullName 'bin\ruby.exe') } |
                     Select-Object -First 1
        if ($candidate) {
            Rename-Item $candidate.FullName $rubyDir
        }
    }
    if (-not (Test-Path $rubyExe)) {
        throw "Ruby extraction failed - expected $rubyExe"
    }
    Write-Host "[OK] Ruby (local):  $rubyExe"
}

# Add Ruby to PATH
$env:PATH = "$(Split-Path $rubyExe);$env:PATH"

# ================================================================
#  2. Ceedling gem (local to tools/gems)
# ================================================================
$gemHome = Join-Path $TOOLS 'gems'
$env:GEM_HOME = $gemHome
$env:GEM_PATH = $gemHome
$env:PATH     = "$gemHome\bin;$env:PATH"

$ceedlingExe = Join-Path $gemHome 'bin\ceedling'
$ceedlingBat = Join-Path $gemHome 'bin\ceedling.bat'

if (-not ((Test-Path $ceedlingExe) -or (Test-Path $ceedlingBat))) {
    Write-Host ""
    Write-Host "============================================================"
    Write-Host "  Ceedling not found - installing via gem ..."
    Write-Host "============================================================"
    & gem install ceedling -v $CEEDLING_VER --no-document
    if ($LASTEXITCODE -ne 0) { throw "Ceedling gem install failed" }
    Write-Host "Ceedling ready."
}
Write-Host "[OK] Ceedling: $gemHome\bin"

# ================================================================
#  3. Locate or download portable gcc (w64devkit)
# ================================================================
$w64devDir = Join-Path $TOOLS 'w64devkit'
$w64devGcc = Join-Path $w64devDir 'bin\gcc.exe'
$w64devUrl = "https://github.com/skeeto/w64devkit/releases/download/v${W64DEV_VER}/w64devkit-${W64DEV_VER}.zip"

$gccCmd = Get-Command gcc -ErrorAction SilentlyContinue
if ($gccCmd) {
    Write-Host "[OK] gcc (system): $($gccCmd.Source)"
} elseif (Test-Path $w64devGcc) {
    $env:PATH = "$w64devDir\bin;$env:PATH"
    Write-Host "[OK] gcc (local):  $w64devGcc"
} else {
    Write-Host ""
    Write-Host "============================================================"
    Write-Host "  gcc not found - downloading w64devkit ${W64DEV_VER} ..."
    Write-Host "============================================================"

    Ensure-Tool 'w64devkit' $w64devGcc $w64devUrl "w64devkit-${W64DEV_VER}.zip"
    $env:PATH = "$w64devDir\bin;$env:PATH"
    Write-Host "[OK] gcc (local):  $w64devGcc"
}

# ================================================================
#  4. Auto-clean stale build cache (cross-platform .o mismatch)
# ================================================================
$buildTest = Join-Path $ROOT 'build\test'
if (Test-Path $buildTest) {
    $staleObj = Get-ChildItem -Path $buildTest -Filter '*.o' -Recurse -ErrorAction SilentlyContinue |
                Select-Object -First 1
    if ($staleObj) {
        # Windows-compiled .o are PE/COFF; Linux .o start with ELF magic (0x7F 'E' 'L' 'F')
        $bytes = [System.IO.File]::ReadAllBytes($staleObj.FullName)
        if ($bytes.Length -ge 4 -and $bytes[0] -eq 0x7F -and $bytes[1] -eq 0x45 -and
            $bytes[2] -eq 0x4C -and $bytes[3] -eq 0x46) {
            Write-Host "[CLEAN] Stale build cache detected (Linux .o files) - cleaning..."
            Remove-Item -Recurse -Force $buildTest
        }
    }
}

# ================================================================
#  5. Run Ceedling
# ================================================================
Write-Host ""
Set-Location $ROOT

if (-not $CeedlingArgs -or $CeedlingArgs.Count -eq 0) {
    Write-Host "Running: ceedling test:all"
    & ceedling test:all
} else {
    Write-Host "Running: ceedling $($CeedlingArgs -join ' ')"
    & ceedling @CeedlingArgs
}

if ($LASTEXITCODE -ne 0) { throw "Ceedling failed with exit code $LASTEXITCODE" }
