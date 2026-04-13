@echo off
REM ================================================================
REM  One-click build for l4s5_Anbo (Windows CMD / .bat)
REM
REM  Usage:
REM      build.bat                  configure + build (Debug)
REM      build.bat clean            delete build\ and rebuild
REM      build.bat release          Release mode
REM      build.bat clean release    both
REM ================================================================
setlocal

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "TOOLS=%ROOT%\tools"
set "BUILD=%ROOT%\build"
set "CMAKE_VER=4.0.1"
set "NINJA_VER=1.12.1"

REM ---- Parse args ----
set "DO_CLEAN=0"
set "BUILD_TYPE=Debug"
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="clean"   set "DO_CLEAN=1"
if /i "%~1"=="release" set "BUILD_TYPE=Release"
shift
goto parse_args
:args_done

REM ---- Locate or download CMake ----
set "CMAKE_EXE="
where cmake >nul 2>&1 && for /f "delims=" %%P in ('where cmake 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%P"
if defined CMAKE_EXE (
    echo [OK] CMake: %CMAKE_EXE%
    goto cmake_ok
)
set "CMAKE_EXE=%TOOLS%\cmake-%CMAKE_VER%-windows-x86_64\bin\cmake.exe"
if exist "%CMAKE_EXE%" (
    echo [OK] CMake: %CMAKE_EXE%
    goto cmake_ok
)
echo.
echo ============================================================
echo   CMake not found - downloading v%CMAKE_VER% ...
echo ============================================================
if not exist "%TOOLS%" mkdir "%TOOLS%"
set "CMAKE_URL=https://github.com/Kitware/CMake/releases/download/v%CMAKE_VER%/cmake-%CMAKE_VER%-windows-x86_64.zip"
set "CMAKE_ZIP=%TOOLS%\cmake.zip"
powershell -NoProfile -Command "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%CMAKE_URL%' -OutFile '%CMAKE_ZIP%' -UseBasicParsing"
if not exist "%CMAKE_ZIP%" (
    echo ERROR: CMake download failed.
    exit /b 1
)
echo Extracting CMake ...
powershell -NoProfile -Command "Expand-Archive -Path '%CMAKE_ZIP%' -DestinationPath '%TOOLS%' -Force"
del "%CMAKE_ZIP%" 2>nul
if not exist "%CMAKE_EXE%" (
    echo ERROR: CMake extraction failed.
    exit /b 1
)
echo CMake ready.
:cmake_ok

REM ---- Locate or download Ninja ----
set "NINJA_EXE="
where ninja >nul 2>&1 && for /f "delims=" %%P in ('where ninja 2^>nul') do if not defined NINJA_EXE set "NINJA_EXE=%%P"
if defined NINJA_EXE (
    echo [OK] Ninja: %NINJA_EXE%
    goto ninja_ok
)
set "NINJA_EXE=%TOOLS%\ninja.exe"
if exist "%NINJA_EXE%" (
    echo [OK] Ninja: %NINJA_EXE%
    goto ninja_ok
)
echo.
echo ============================================================
echo   Ninja not found - downloading v%NINJA_VER% ...
echo ============================================================
if not exist "%TOOLS%" mkdir "%TOOLS%"
set "NINJA_URL=https://github.com/ninja-build/ninja/releases/download/v%NINJA_VER%/ninja-win.zip"
set "NINJA_ZIP=%TOOLS%\ninja.zip"
powershell -NoProfile -Command "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%NINJA_URL%' -OutFile '%NINJA_ZIP%' -UseBasicParsing"
if not exist "%NINJA_ZIP%" (
    echo ERROR: Ninja download failed.
    exit /b 1
)
echo Extracting Ninja ...
powershell -NoProfile -Command "Expand-Archive -Path '%NINJA_ZIP%' -DestinationPath '%TOOLS%' -Force"
del "%NINJA_ZIP%" 2>nul
if not exist "%NINJA_EXE%" (
    echo ERROR: Ninja extraction failed.
    exit /b 1
)
echo Ninja ready.
:ninja_ok

REM ---- Update PATH ----
for %%F in ("%CMAKE_EXE%") do set "PATH=%%~dpF;%PATH%"
for %%F in ("%NINJA_EXE%") do set "PATH=%%~dpF;%PATH%"

REM ---- Clean ----
if "%DO_CLEAN%"=="1" if exist "%BUILD%" (
    echo Cleaning build\ ...
    rmdir /s /q "%BUILD%"
)

REM ---- Detect stale cross-platform cache (e.g. WSL/Linux build on Windows) ----
if exist "%BUILD%\CMakeCache.txt" (
    findstr /R /C:"CMAKE_C_COMPILER:.*=/" "%BUILD%\CMakeCache.txt" >nul 2>&1 && (
        echo Detected stale Linux CMake cache - cleaning build\ ...
        rmdir /s /q "%BUILD%"
    )
)

REM ---- Configure ----
set "TOOLCHAIN=%ROOT%\cmake\arm-none-eabi.cmake"
if not exist "%BUILD%\build.ninja" (
    echo.
    echo Configuring [%BUILD_TYPE%] ...
    "%CMAKE_EXE%" -B "%BUILD%" -G Ninja "-DCMAKE_TOOLCHAIN_FILE=%TOOLCHAIN%" "-DCMAKE_BUILD_TYPE=%BUILD_TYPE%"
    if errorlevel 1 (
        echo ERROR: CMake configure failed.
        exit /b 1
    )
)

REM ---- Build ----
echo.
echo Building ...
"%CMAKE_EXE%" --build "%BUILD%"
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

REM ---- Summary ----
echo.
echo ============================================================
echo   BUILD SUCCESSFUL
echo   Output:
for %%F in ("%BUILD%\l4s5_anbo.elf" "%BUILD%\l4s5_anbo.bin" "%BUILD%\l4s5_anbo.hex" "%BUILD%\l4s5_anbo.map") do (
    if exist "%%~F" echo     %%~nxF     %%~zF bytes
)
echo ============================================================

endlocal
