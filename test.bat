@echo off
REM ================================================================
REM  One-click unit test runner for l4s5_Anbo (Windows CMD)
REM
REM  Auto-downloads Ruby (portable) + Ceedling into tools/ if missing.
REM  Uses native gcc (must be on PATH, or auto-downloads w64devkit).
REM
REM  Usage:
REM      test.bat                   run all tests
REM      test.bat test:anbo_rb      run a single test module
REM      test.bat clean             clean test build artifacts
REM ================================================================
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "TOOLS=%ROOT%\tools"

REM ---- Versions ----
set "RUBY_VER=3.3.7-1"
set "CEEDLING_VER=0.31.1"
set "W64DEV_VER=2.0.0"

REM ================================================================
REM  1. Locate or download portable Ruby
REM ================================================================
set "RUBY_DIR=%TOOLS%\ruby-%RUBY_VER%-x64-mingw-ucrt"
set "RUBY_EXE=%RUBY_DIR%\bin\ruby.exe"

if exist "%RUBY_EXE%" goto ruby_ok

REM Check system PATH
set "SYS_RUBY="
where ruby >nul 2>&1 && for /f "delims=" %%P in ('where ruby 2^>nul') do if not defined SYS_RUBY set "SYS_RUBY=%%P"
if defined SYS_RUBY (
    set "RUBY_EXE=%SYS_RUBY%"
    echo [OK] Ruby ^(system^): %SYS_RUBY%
    goto ruby_ok
)

echo.
echo ============================================================
echo   Ruby not found - downloading portable Ruby %RUBY_VER% ...
echo ============================================================
if not exist "%TOOLS%" mkdir "%TOOLS%"

set "RUBY_URL=https://github.com/oneclick/rubyinstaller2/releases/download/RubyInstaller-%RUBY_VER%/rubyinstaller-%RUBY_VER%-x64.7z"
set "RUBY_ARCHIVE=%TOOLS%\ruby.7z"

echo Downloading %RUBY_URL% ...
curl.exe -L -o "%RUBY_ARCHIVE%" "%RUBY_URL%"
if not exist "%RUBY_ARCHIVE%" (
    echo ERROR: Ruby download failed.
    exit /b 1
)

echo Extracting Ruby ...
REM Try 7z first, then tar (Windows 10+ libarchive supports .7z)
set "EXTRACTED=0"
where 7z >nul 2>&1 && (
    7z x "%RUBY_ARCHIVE%" -o"%TOOLS%" -y >nul 2>&1
    if exist "%RUBY_EXE%" set "EXTRACTED=1"
)
if "%EXTRACTED%"=="0" (
    REM Fallback: use PowerShell with .NET compression or tar
    powershell -NoProfile -Command "try { tar -xf '%RUBY_ARCHIVE%' -C '%TOOLS%' } catch { Write-Error $_.Exception.Message; exit 1 }"
    if errorlevel 1 (
        echo ERROR: Could not extract .7z - please install 7z and retry.
        del "%RUBY_ARCHIVE%" 2>nul
        exit /b 1
    )
)

del "%RUBY_ARCHIVE%" 2>nul

REM RubyInstaller extracts as "rubyinstaller-X.Y.Z-N-x64" — rename to simpler name
if not exist "%RUBY_DIR%" (
    for /d %%D in ("%TOOLS%\rubyinstaller-*") do (
        if exist "%%D\bin\ruby.exe" (
            rename "%%D" "ruby-%RUBY_VER%-x64-mingw-ucrt"
        )
    )
)

if not exist "%RUBY_EXE%" (
    echo ERROR: Ruby extraction failed - expected %RUBY_EXE%
    exit /b 1
)
echo Ruby %RUBY_VER% ready.

:ruby_ok
for %%F in ("%RUBY_EXE%") do set "RUBY_BIN=%%~dpF"
set "PATH=%RUBY_BIN%;%PATH%"
echo [OK] Ruby: %RUBY_EXE%

REM ================================================================
REM  2. Locate or install Ceedling gem (local to tools/gems)
REM ================================================================
set "GEM_HOME=%TOOLS%\gems"
set "GEM_PATH=%GEM_HOME%"
set "PATH=%GEM_HOME%\bin;%PATH%"

set "CEEDLING_EXE=%GEM_HOME%\bin\ceedling"
REM On Windows the gem wrapper might be .bat or no extension
if exist "%CEEDLING_EXE%" goto ceedling_ok
if exist "%CEEDLING_EXE%.bat" goto ceedling_ok

echo.
echo ============================================================
echo   Ceedling not found - installing via gem ...
echo ============================================================
echo gem install ceedling -v %CEEDLING_VER% --no-document ...
call gem install ceedling -v %CEEDLING_VER% --no-document
if errorlevel 1 (
    echo ERROR: Ceedling gem install failed.
    exit /b 1
)
echo Ceedling ready.

:ceedling_ok
echo [OK] Ceedling: %GEM_HOME%\bin

REM ================================================================
REM  3. Locate or download portable gcc (w64devkit)
REM ================================================================
set "W64DEV_DIR=%TOOLS%\w64devkit"
set "W64DEV_GCC=%W64DEV_DIR%\bin\gcc.exe"

where gcc >nul 2>&1
if not errorlevel 1 (
    echo [OK] gcc ^(system^): found on PATH
    goto gcc_ok
)

if exist "%W64DEV_GCC%" (
    set "PATH=%W64DEV_DIR%\bin;%PATH%"
    echo [OK] gcc ^(local^): %W64DEV_GCC%
    goto gcc_ok
)

echo.
echo ============================================================
echo   gcc not found - downloading w64devkit %W64DEV_VER% ...
echo ============================================================
if not exist "%TOOLS%" mkdir "%TOOLS%"

set "W64DEV_URL=https://github.com/skeeto/w64devkit/releases/download/v%W64DEV_VER%/w64devkit-%W64DEV_VER%.zip"
set "W64DEV_ZIP=%TOOLS%\w64devkit.zip"

echo Downloading %W64DEV_URL% ...
curl.exe -L -o "%W64DEV_ZIP%" "%W64DEV_URL%"
if not exist "%W64DEV_ZIP%" (
    echo ERROR: w64devkit download failed.
    exit /b 1
)

echo Extracting w64devkit ...
powershell -NoProfile -Command "Expand-Archive -Path '%W64DEV_ZIP%' -DestinationPath '%TOOLS%' -Force"
del "%W64DEV_ZIP%" 2>nul

if not exist "%W64DEV_GCC%" (
    echo ERROR: w64devkit extraction failed - expected %W64DEV_GCC%
    exit /b 1
)
set "PATH=%W64DEV_DIR%\bin;%PATH%"
echo [OK] gcc ^(local^): %W64DEV_GCC%

:gcc_ok

REM ================================================================
REM  4. Auto-clean stale build cache (WSL/Linux .o on Windows or vice versa)
REM ================================================================
set "BUILD_TEST=%ROOT%\build\test"
if not exist "%BUILD_TEST%" goto cache_ok

REM Find any .o file and check if it's a PE/COFF object (Windows native)
set "STALE=0"
for /r "%BUILD_TEST%" %%O in (*.o) do (
    if "!STALE!"=="0" (
        findstr /m "ELF" "%%O" >nul 2>&1 && set "STALE=1"
    )
)
if "!STALE!"=="1" (
    echo [CLEAN] Stale build cache detected ^(Linux .o files^) - cleaning...
    rmdir /s /q "%BUILD_TEST%"
)
:cache_ok

REM ================================================================
REM  5. Run Ceedling
REM ================================================================
echo.
cd /d "%ROOT%"

set RUBYOPT=-rpathname

if "%~1"=="" (
    echo Running: ceedling test:all
    call ceedling test:all
) else (
    echo Running: ceedling %*
    call ceedling %*
)

endlocal
exit /b %ERRORLEVEL%
