#!/usr/bin/env bash
# ================================================================
#  One-click unit test runner for l4s5_Anbo (Linux / WSL / macOS)
#
#  Fully self-bootstrapping: auto-installs Ruby, gcc, and Ceedling
#  via system package manager if missing. Ceedling gem is installed
#  locally in tools/gems — no global gem pollution.
#
#  Usage:
#      ./test.sh                   run all tests
#      ./test.sh test:anbo_rb      run a single test module
#      ./test.sh clean             clean test build artifacts
# ================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
TOOLS="$ROOT/tools"

# ---- Versions ----
CEEDLING_VER="0.31.1"
MIN_RUBY="2.4"

# ================================================================
#  Helper: detect package manager and install packages
# ================================================================
pkg_install() {
    local pkg_apt="$1"
    local pkg_brew="${2:-$1}"
    local pkg_pacman="${3:-$1}"
    local pkg_dnf="${4:-$1}"

    if command -v apt-get &>/dev/null; then
        echo "  -> sudo apt-get install -y $pkg_apt"
        sudo apt-get update -qq
        sudo apt-get install -y -qq $pkg_apt
    elif command -v brew &>/dev/null; then
        echo "  -> brew install $pkg_brew"
        brew install $pkg_brew
    elif command -v pacman &>/dev/null; then
        echo "  -> sudo pacman -S --noconfirm $pkg_pacman"
        sudo pacman -S --noconfirm $pkg_pacman
    elif command -v dnf &>/dev/null; then
        echo "  -> sudo dnf install -y $pkg_dnf"
        sudo dnf install -y $pkg_dnf
    else
        return 1
    fi
}

# ================================================================
#  1. Ensure Ruby
# ================================================================
if ! command -v ruby &>/dev/null; then
    echo ""
    echo "============================================================"
    echo "  Ruby not found — installing via package manager ..."
    echo "============================================================"
    if ! pkg_install "ruby ruby-dev" "ruby" "ruby" "ruby ruby-devel"; then
        echo ""
        echo "ERROR: No supported package manager found (apt/brew/pacman/dnf)."
        echo "  Please install Ruby >= ${MIN_RUBY} manually."
        exit 1
    fi
    hash -r   # refresh command cache
fi

RUBY_OK=$(ruby -e "puts (Gem::Version.new(RUBY_VERSION) >= Gem::Version.new('${MIN_RUBY}')) ? 'yes' : 'no'")
if [ "$RUBY_OK" != "yes" ]; then
    echo "ERROR: Ruby >= ${MIN_RUBY} required. Current: $(ruby --version)"
    exit 1
fi
echo "[OK] Ruby: $(ruby --version)"

# ================================================================
#  2. Ensure gcc
# ================================================================
if ! command -v gcc &>/dev/null; then
    echo ""
    echo "============================================================"
    echo "  gcc not found — installing via package manager ..."
    echo "============================================================"
    if ! pkg_install "build-essential" "gcc" "base-devel" "gcc gcc-c++"; then
        echo ""
        echo "ERROR: No supported package manager found."
        echo "  Please install gcc manually."
        exit 1
    fi
    hash -r
fi
echo "[OK] gcc: $(gcc --version | head -1)"

# ================================================================
#  3. Install Ceedling gem locally (GEM_HOME = tools/gems)
# ================================================================
export GEM_HOME="$TOOLS/gems"
export GEM_PATH="$GEM_HOME"
export PATH="$GEM_HOME/bin:$PATH"

if ! "$GEM_HOME/bin/ceedling" version &>/dev/null 2>&1; then
    echo ""
    echo "============================================================"
    echo "  Ceedling not found — installing gem v${CEEDLING_VER} ..."
    echo "============================================================"
    gem install ceedling -v "$CEEDLING_VER" --no-document
    echo "Ceedling ready."
fi
echo "[OK] Ceedling: $GEM_HOME/bin/ceedling"

# ================================================================
#  4. Auto-clean stale build cache (cross-platform .o mismatch)
# ================================================================
BUILD_TEST="$ROOT/build/test"
if [ -d "$BUILD_TEST" ]; then
    # Pick any .o file and check if it matches the current host
    stale_obj=$(find "$BUILD_TEST" -name '*.o' -print -quit 2>/dev/null || true)
    if [ -n "$stale_obj" ] && command -v file &>/dev/null; then
        obj_arch=$(file -b "$stale_obj")
        case "$(uname -m)" in
            x86_64|amd64)  expected="ELF 64-bit" ;;
            i*86)          expected="ELF 32-bit" ;;
            aarch64|arm64) expected="ELF 64-bit" ;;
            *)             expected="ELF" ;;
        esac
        if ! echo "$obj_arch" | grep -q "$expected"; then
            echo "[CLEAN] Stale build cache detected (wrong arch) — cleaning..."
            rm -rf "$BUILD_TEST"
        fi
    fi
fi

# ================================================================
#  5. Run Ceedling
# ================================================================
echo ""
cd "$ROOT"

if [ $# -eq 0 ]; then
    echo "Running: ceedling test:all"
    ceedling test:all
else
    echo "Running: ceedling $*"
    ceedling "$@"
fi
