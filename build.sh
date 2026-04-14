#!/usr/bin/env bash
# ================================================================
# One-click build script for the l4s5_Anbo project (Linux / WSL / macOS)
#
# Prerequisites: bash, curl, tar  (standard on any Linux/WSL/macOS)
# Everything else (CMake, Ninja, ARM toolchain, STM32 SDK) is
# auto-downloaded to the tools/ directory on first run.
#
# Usage:
#   ./build.sh                        # configure + build (Debug)
#   ./build.sh clean                  # delete build/ and rebuild
#   ./build.sh release                # build in Release mode
#   ./build.sh version 1.2.3          # set firmware version
#   (--clean / --release / --version also accepted)
# ================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
TOOLS="$ROOT/tools"
BUILD="$ROOT/build"

# ---- Versions (edit to upgrade) ----
CMAKE_VER="4.0.1"
NINJA_VER="1.12.1"

# ---- Parse args ----
CLEAN=0
BUILD_TYPE="Debug"
FW_VER=""
while [ $# -gt 0 ]; do
    case "$1" in
        clean|--clean)     CLEAN=1 ;;
        release|--release) BUILD_TYPE="Release" ;;
        version|--version) shift; FW_VER="$1" ;;
        *)                 echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

# ---- Detect host ----
OS="$(uname -s)"
ARCH="$(uname -m)"

# ================================================================
#  Helper: download + extract if tool missing
#  ensure_tool <name> <exe_path> <url> <archive>
# ================================================================
ensure_tool() {
    local name="$1" exe="$2" url="$3" archive="$4"
    if [ -f "$exe" ]; then return 0; fi

    echo ""
    echo "============================================================"
    echo "  $name not found - downloading ..."
    echo "  URL: $url"
    echo "============================================================"
    echo ""

    mkdir -p "$TOOLS"
    local dl="$TOOLS/$archive"
    curl -fSL --progress-bar -o "$dl" "$url"

    echo "Extracting $archive ..."
    case "$archive" in
        *.tar.gz|*.tgz)  tar -xzf "$dl" -C "$TOOLS" ;;
        *.tar.xz)        tar -xJf "$dl" -C "$TOOLS" ;;
        *.zip)            unzip -qo "$dl" -d "$TOOLS" ;;
    esac
    rm -f "$dl"

    if [ ! -f "$exe" ]; then
        echo "ERROR: $name extraction failed - expected $exe"
        exit 1
    fi
    echo "$name ready."
    echo ""
}

# ================================================================
#  CMake
# ================================================================
if [ "$OS" = "Darwin" ]; then
    if [ "$ARCH" = "arm64" ]; then
        CMAKE_PLAT="macos-universal"
    else
        CMAKE_PLAT="macos-universal"
    fi
    CMAKE_DIR="$TOOLS/cmake-${CMAKE_VER}-${CMAKE_PLAT}"
    CMAKE_EXE="$CMAKE_DIR/CMake.app/Contents/bin/cmake"
    CMAKE_URL="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VER}/cmake-${CMAKE_VER}-${CMAKE_PLAT}.tar.gz"
    CMAKE_ARCHIVE="cmake-${CMAKE_VER}-${CMAKE_PLAT}.tar.gz"
else
    CMAKE_PLAT="linux-${ARCH}"
    CMAKE_DIR="$TOOLS/cmake-${CMAKE_VER}-${CMAKE_PLAT}"
    CMAKE_EXE="$CMAKE_DIR/bin/cmake"
    CMAKE_URL="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VER}/cmake-${CMAKE_VER}-${CMAKE_PLAT}.tar.gz"
    CMAKE_ARCHIVE="cmake-${CMAKE_VER}-${CMAKE_PLAT}.tar.gz"
fi

# Check system first
SYS_CMAKE="$(command -v cmake 2>/dev/null || true)"
if [ -n "$SYS_CMAKE" ]; then
    CMAKE_EXE="$SYS_CMAKE"
    echo "[OK] CMake (system): $CMAKE_EXE"
else
    ensure_tool "CMake" "$CMAKE_EXE" "$CMAKE_URL" "$CMAKE_ARCHIVE"
    echo "[OK] CMake (local):  $CMAKE_EXE"
fi

# ================================================================
#  Ninja
# ================================================================
if [ "$OS" = "Darwin" ]; then
    NINJA_PLAT="mac"
elif [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    NINJA_PLAT="linux-aarch64"
else
    NINJA_PLAT="linux"
fi
NINJA_EXE="$TOOLS/ninja"
NINJA_URL="https://github.com/ninja-build/ninja/releases/download/v${NINJA_VER}/ninja-${NINJA_PLAT}.zip"
NINJA_ARCHIVE="ninja-${NINJA_PLAT}.zip"

SYS_NINJA="$(command -v ninja 2>/dev/null || true)"
if [ -n "$SYS_NINJA" ]; then
    NINJA_EXE="$SYS_NINJA"
    echo "[OK] Ninja (system): $NINJA_EXE"
else
    ensure_tool "Ninja" "$NINJA_EXE" "$NINJA_URL" "$NINJA_ARCHIVE"
    chmod +x "$NINJA_EXE"
    echo "[OK] Ninja (local):  $NINJA_EXE"
fi

# Add tools to PATH
export PATH="$(dirname "$CMAKE_EXE"):$(dirname "$NINJA_EXE"):$PATH"

# ================================================================
#  Clean
# ================================================================
if [ "$CLEAN" -eq 1 ] && [ -d "$BUILD" ]; then
    echo "Cleaning build/ ..."
    rm -rf "$BUILD"
fi

# ================================================================
#  Detect stale cross-platform cache (e.g. Windows build under WSL)
# ================================================================
if [ -f "$BUILD/CMakeCache.txt" ]; then
    if grep -q '[A-Za-z]:/' "$BUILD/CMakeCache.txt" 2>/dev/null; then
        echo "Detected stale Windows CMake cache â€” cleaning build/ ..."
        rm -rf "$BUILD"
    fi
fi

# ================================================================
#  Configure
# ================================================================
TOOLCHAIN="$ROOT/cmake/arm-none-eabi.cmake"

# Force reconfigure when version is specified (CACHE override)
if [ -n "$FW_VER" ] && [ -f "$BUILD/build.ninja" ]; then
    echo "Reconfiguring for version $FW_VER ..."
    rm -f "$BUILD/CMakeCache.txt"
fi

if [ ! -f "$BUILD/build.ninja" ]; then
    echo ""
    echo "Configuring ($BUILD_TYPE) ..."
    VERSION_FLAG=""
    if [ -n "$FW_VER" ]; then VERSION_FLAG="-DFW_VERSION=$FW_VER"; fi
    "$CMAKE_EXE" -B "$BUILD" -G Ninja \
        "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN" \
        "-DCMAKE_BUILD_TYPE=$BUILD_TYPE" \
        $VERSION_FLAG
fi

# ================================================================
#  Build
# ================================================================
echo ""
echo "Building ..."
"$CMAKE_EXE" --build "$BUILD"

# ================================================================
#  Summary
# ================================================================
echo ""
echo "============================================================"
echo "  BUILD SUCCESSFUL"
echo "  Output:"
ls -lh "$BUILD"/l4s5_anbo* 2>/dev/null | awk '{printf "    %-20s %s\n", $NF, $5}'
echo "============================================================"
