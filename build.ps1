#!/usr/bin/env bash
#
# build.sh - build KeePassXC on Windows from Git Bash or an MSYS2 shell.
#
# This script follows the official Windows build instructions:
#   https://github.com/keepassxreboot/keepassxc/wiki/Building-KeePassXC#windows
#
# It auto-detects Visual Studio (MSVC), Qt, vcpkg, CMake and Ninja.  The MSVC
# toolchain is preferred, but a MinGW Qt installation is used when no usable
# MSVC Qt kit is available.
#
# Usage:
#   ./build.sh                          # auto-detect everything, build Release
#   ./build.sh --qt /c/Qt/6.12.0/msvc2022_64
#   ./build.sh --toolchain mingw --qt /g/QT/6.12.0/mingw_64
#   ./build.sh --clean --package        # clean rebuild + ZIP package
#   ./build.sh --check                  # only print the detected configuration
#   ./build.sh -- --help                # pass extra options to CMake
#
# Environment variables:
#   VCPKG_ROOT   vcpkg installation (bootstrapped into ~/vcpkg if unset)
#   QT_ROOT      Qt installation root (e.g. C:/Qt) or a Qt kit directory
#   MINGW_ROOT   MinGW installation (root or its bin directory), MinGW only
#
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$SCRIPT_DIR"

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
TOOLCHAIN="auto"          # auto | msvc | mingw
BUILD_DIR="$SCRIPT_DIR/build"
BUILD_TYPE="Release"      # Release | Debug | RelWithDebInfo | MinSizeRel
QT_PATH_ARG="${QT_ROOT:-}"
VCPKG_ARG="${VCPKG_ROOT:-${VCPKG_INSTALLATION_ROOT:-}}"
JOBS="$(nproc 2>/dev/null || echo "${NUMBER_OF_PROCESSORS:-4}")"
DO_TESTS=0
DO_GUI_TESTS=0
DO_DOCS="auto"            # auto | 1 | 0
DO_MINIMAL=0
DO_PACKAGE=0
DO_CLEAN=0
CONFIGURE_ONLY=0
PRINT_CONFIG=0
BOOTSTRAP_VCPKG=1
TRIPLET_ARG=""
KPXC_BUILD_TYPE="Release"
EXTRA_CMAKE=()

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
if [ -t 1 ]; then
    C_BLUE=$'\033[1;34m'; C_YELLOW=$'\033[1;33m'; C_RED=$'\033[1;31m'; C_RESET=$'\033[0m'
else
    C_BLUE=""; C_YELLOW=""; C_RED=""; C_RESET=""
fi

log()  { printf '%s==>%s %s\n' "$C_BLUE" "$C_RESET" "$*"; }
warn() { printf '%swarning:%s %s\n' "$C_YELLOW" "$C_RESET" "$*" >&2; }
die()  { printf '%serror:%s %s\n' "$C_RED" "$C_RESET" "$*" >&2; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

# Convert a path to the form native Windows tools (cmake, cpack, ...) accept.
winpath() { cygpath -m "$1"; }
# Convert a Windows path to the form used inside this shell.
unixpath() { cygpath -u "$1"; }

usage() {
    cat <<'EOF'
Build KeePassXC on Windows (run from Git Bash or an MSYS2 shell)

Usage: ./build.sh [options] [-- CMake options...]

Toolchain:
  --toolchain <auto|msvc|mingw>  Toolchain to use (default: auto)
  --qt <path>                    Qt kit directory, or a Qt install root
  --vcpkg <path>                 vcpkg installation directory
  --triplet <triplet>            Override the vcpkg triplet
  --mingw <path>                 MinGW root or bin directory (MinGW only)
  --no-bootstrap                 Do not clone vcpkg when it is missing

Build:
  --build-dir <dir>              Build directory (default: ./build)
  --build-type <type>            Release, Debug, RelWithDebInfo, MinSizeRel
  -j, --jobs <n>                 Parallel build jobs
  --clean                        Remove the build directory first
  --configure-only               Run CMake configure but do not compile
  --tests                        Build unit tests (default: off)
  --gui-tests                    Build GUI tests (implies --tests)
  --docs / --no-docs             Force documentation on/off
  --minimal                      Build with the minimal feature set
  --snapshot / --release         Override KEEPASSXC_BUILD_TYPE
  --package                      Create a ZIP package with CPack
  --check                        Print detected settings and exit
  --                             All remaining arguments go to CMake

Examples:
  ./build.sh
  ./build.sh --qt "C:/Qt/6.12.0/msvc2022_64"
  ./build.sh --toolchain mingw --qt "G:/QT/6.12.0/mingw_64"
  ./build.sh --clean --package
  ./build.sh --check
EOF
}

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
while [ $# -gt 0 ]; do
    case "$1" in
        --toolchain)      [ $# -ge 2 ] || die "--toolchain needs a value"; TOOLCHAIN="$2"; shift 2 ;;
        --qt)             [ $# -ge 2 ] || die "--qt needs a value"; QT_PATH_ARG="$2"; shift 2 ;;
        --vcpkg)          [ $# -ge 2 ] || die "--vcpkg needs a value"; VCPKG_ARG="$2"; shift 2 ;;
        --triplet)        [ $# -ge 2 ] || die "--triplet needs a value"; TRIPLET_ARG="$2"; shift 2 ;;
        --mingw)          [ $# -ge 2 ] || die "--mingw needs a value"; MINGW_ROOT="$2"; shift 2 ;;
        --build-dir)      [ $# -ge 2 ] || die "--build-dir needs a value"; BUILD_DIR="$2"; shift 2 ;;
        --build-type)     [ $# -ge 2 ] || die "--build-type needs a value"; BUILD_TYPE="$2"; shift 2 ;;
        -j|--jobs)        [ $# -ge 2 ] || die "--jobs needs a value"; JOBS="$2"; shift 2 ;;
        --tests)          DO_TESTS=1; shift ;;
        --gui-tests)      DO_TESTS=1; DO_GUI_TESTS=1; shift ;;
        --no-tests)       DO_TESTS=0; DO_GUI_TESTS=0; shift ;;
        --docs)           DO_DOCS=1; shift ;;
        --no-docs)        DO_DOCS=0; shift ;;
        --minimal)        DO_MINIMAL=1; shift ;;
        --clean)          DO_CLEAN=1; shift ;;
        --package)        DO_PACKAGE=1; shift ;;
        --configure-only) CONFIGURE_ONLY=1; shift ;;
        --no-bootstrap)   BOOTSTRAP_VCPKG=0; shift ;;
        --snapshot)       KPXC_BUILD_TYPE="Snapshot"; shift ;;
        --release)        KPXC_BUILD_TYPE="Release"; shift ;;
        --check)          PRINT_CONFIG=1; shift ;;
        -h|--help)        usage; exit 0 ;;
        --)               shift; EXTRA_CMAKE=("$@"); break ;;
        *)                die "unknown option: $1 (try --help)" ;;
    esac
done

case "$TOOLCHAIN" in
    auto|msvc|mingw) ;;
    *) die "invalid --toolchain '$TOOLCHAIN' (use auto, msvc or mingw)" ;;
esac

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) ;;
    *) die "this script targets Windows; run it from Git Bash or an MSYS2 shell" ;;
esac

have cygpath || die "cygpath is required (run this from Git Bash or MSYS2)"

# ---------------------------------------------------------------------------
# Visual Studio / MSVC detection
# ---------------------------------------------------------------------------
VS_PATH=""

find_visual_studio() {
    local vswhere path dir

    for vswhere in \
        "/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe" \
        "/c/Program Files/Microsoft Visual Studio/Installer/vswhere.exe"
    do
        [ -f "$vswhere" ] || continue
        path="$("$vswhere" -latest -products '*' \
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
            -property installationPath 2>/dev/null | tr -d '\r' | head -n1 || true)"
        if [ -n "$path" ] && [ -d "$path" ]; then
            VS_PATH="$(unixpath "$path")"
            return 0
        fi
    done

    # Fallback: search common non-default installation drive locations.
    for dir in /?/MicrosoftVisualStudio/* /?/Microsoft\ Visual\ Studio/*; do
        [ -d "$dir" ] || continue
        if [ -f "$dir/VC/Auxiliary/Build/vcvars64.bat" ]; then
            VS_PATH="$dir"
            return 0
        fi
    done
    return 1
}

import_msvc_environment() {
    [ -n "$VS_PATH" ] || die "Visual Studio with the C++ workload was not found"
    local vcvars="$VS_PATH/VC/Auxiliary/Build/vcvars64.bat"
    [ -f "$vcvars" ] || die "vcvars64.bat not found under $VS_PATH"

    local tmp out line name value
    tmp="$(mktemp -d)"
    {
        printf '@echo off\r\n'
        printf 'call "%s" >nul 2>&1 || exit /b 1\r\n' "$(winpath "$vcvars")"
        printf 'set\r\n'
    } > "$tmp/vsenv.bat"

    if ! out="$(cmd.exe //c "$(winpath "$tmp/vsenv.bat")")"; then
        rm -rf "$tmp"
        die "failed to initialize the MSVC environment"
    fi
    rm -rf "$tmp"

    while IFS= read -r line; do
        line="${line%$'\r'}"
        case "$line" in
            *=*) name="${line%%=*}"; value="${line#*=}" ;;
            *) continue ;;
        esac
        # Only import variables the compiler and CMake actually need.
        case "$name" in
            PATH|INCLUDE|LIB|LIBPATH|VCINSTALLDIR|VCToolsInstallDir|VCToolsVersion|\
            VSINSTALLDIR|WindowsSdkDir|WindowsSDKVersion|UCRTVersion|UniversalCRTSdkDir|\
            VSCMD_ARG_TGT_ARCH|VSCMD_ARG_HOST_ARCH) ;;
            *) continue ;;
        esac
        [ "$name" = "PATH" ] && value="$(cygpath -p "$value")"
        export "$name=$value"
    done <<< "$out"

    have cl || die "MSVC environment was imported, but cl.exe is still not in PATH"
}

# ---------------------------------------------------------------------------
# Qt detection
# ---------------------------------------------------------------------------
qt_config_ok() { [ -f "$1/lib/cmake/Qt6/Qt6Config.cmake" ]; }

qt_search_roots() {
    local roots=() r d n
    for r in "${QTDIR:-}" "$HOME/Qt"; do
        [ -n "$r" ] || continue
        r="$(unixpath "$r")"
        [ -d "$r" ] && roots+=("$r")
    done
    for d in /{c..z}; do
        [ -d "$d" ] || continue
        for n in Qt QT qt; do
            [ -d "$d/$n" ] && roots+=("$d/$n")
        done
    done
    # De-duplicate while preserving order (Windows paths are case-insensitive).
    [ ${#roots[@]} -gt 0 ] && printf '%s\n' "${roots[@]}" | awk '!seen[tolower($0)]++'
}

# Print the newest matching Qt kit, or return 1.
find_qt_kit() {
    local want="$1" pattern candidates=() c root p
    case "$want" in
        msvc)  pattern='msvc*' ;;
        mingw) pattern='mingw*' ;;
        *) die "internal error: find_qt_kit($want)" ;;
    esac

    if [ -n "$QT_PATH_ARG" ]; then
        p="$(unixpath "$QT_PATH_ARG")"
        if qt_config_ok "$p"; then
            case "$(basename "$p")" in
                $pattern) printf '%s\n' "$p"; return 0 ;;
                *) return 1 ;;
            esac
        fi
        # Treat the argument as an installation root.
        for c in "$p"/*/$pattern "$p"/$pattern; do
            [ -d "$c" ] || continue
            if qt_config_ok "$c"; then candidates+=("$c"); fi
        done
        [ ${#candidates[@]} -gt 0 ] || return 1
        printf '%s\n' "${candidates[@]}" | sort -V | tail -n1
        return 0
    fi

    while IFS= read -r root; do
        for c in "$root"/*/$pattern "$root"/$pattern; do
            [ -d "$c" ] || continue
            if qt_config_ok "$c"; then candidates+=("$c"); fi
        done
    done < <(qt_search_roots)

    [ ${#candidates[@]} -gt 0 ] || return 1
    printf '%s\n' "${candidates[@]}" | sort -V | tail -n1
}

# ---------------------------------------------------------------------------
# Toolchain selection
# ---------------------------------------------------------------------------
QT_KIT=""
VS_AVAILABLE=0
MSVC_QT=""
MINGW_QT=""

log "Detecting toolchains and Qt installation..."
find_visual_studio && VS_AVAILABLE=1 || true
MSVC_QT="$(find_qt_kit msvc || true)"
MINGW_QT="$(find_qt_kit mingw || true)"

case "$TOOLCHAIN" in
    auto)
        if [ -n "$MSVC_QT" ]; then
            TOOLCHAIN=msvc
        elif [ -n "$MINGW_QT" ]; then
            TOOLCHAIN=mingw
        elif [ "$VS_AVAILABLE" = 1 ]; then
            TOOLCHAIN=msvc
        else
            die "no C++ toolchain or Qt installation found (install Visual Studio with the C++ workload or MinGW + Qt)"
        fi
        ;;
esac

if [ "$TOOLCHAIN" = msvc ]; then
    [ "$VS_AVAILABLE" = 1 ] || die "MSVC requested, but Visual Studio with the C++ workload was not found"
    QT_KIT="$MSVC_QT"
    import_msvc_environment
else
    QT_KIT="$MINGW_QT"
fi

# ---------------------------------------------------------------------------
# MinGW compiler detection
# ---------------------------------------------------------------------------
MINGW_BIN=""

find_mingw_bin() {
    local candidates=() p
    if [ -n "${MINGW_ROOT:-}" ]; then
        p="$(unixpath "$MINGW_ROOT")"
        [ -x "$p/bin/g++.exe" ] && candidates+=("$p/bin")
        [ -x "$p/g++.exe" ] && candidates+=("$p")
    fi
    if [ -n "$QT_KIT" ]; then
        for p in "$(dirname "$(dirname "$QT_KIT")")"/Tools/mingw*/bin; do
            [ -x "$p/g++.exe" ] && candidates+=("$p")
        done
    fi
    if have g++.exe; then candidates+=("$(dirname "$(command -v g++.exe)")"); fi
    if have g++; then candidates+=("$(dirname "$(command -v g++)")"); fi

    [ ${#candidates[@]} -gt 0 ] || return 1
    printf '%s\n' "${candidates[@]}" | sort -V | tail -n1
}

if [ "$TOOLCHAIN" = mingw ]; then
    MINGW_BIN="$(find_mingw_bin || true)"
    [ -n "$MINGW_BIN" ] || die "MinGW compiler not found (pass --mingw <path> or add g++ to PATH)"
    export PATH="$MINGW_BIN:$PATH"
    have g++.exe || have g++ || die "g++ not found after adding $MINGW_BIN to PATH"
fi

# Qt kit path for the selected toolchain.
if [ -n "$QT_KIT" ]; then
    export PATH="$QT_KIT/bin:$PATH"
    QT_INSTALL_ROOT="$(dirname "$(dirname "$QT_KIT")")"
else
    QT_INSTALL_ROOT=""
    if [ -n "$QT_PATH_ARG" ]; then
        die "the supplied Qt path does not contain a usable $TOOLCHAIN Qt 6 kit: $QT_PATH_ARG"
    fi
    warn "no usable Qt installation found; vcpkg will build Qt 6 (this takes a long time)"
fi

# ---------------------------------------------------------------------------
# CMake and Ninja detection
# ---------------------------------------------------------------------------
CMAKE_EXE=""
find_cmake() {
    local p
    have cmake && { command -v cmake; return 0; }
    for p in \
        "$QT_INSTALL_ROOT"/Tools/CMake_64/bin/cmake.exe \
        "$QT_INSTALL_ROOT"/Tools/CMake/bin/cmake.exe \
        "$QT_INSTALL_ROOT"/Tools/CMake*/bin/cmake.exe \
        "$VS_PATH"/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe
    do
        [ -x "$p" ] && { printf '%s\n' "$p"; return 0; }
    done
    return 1
}
CMAKE_EXE="$(find_cmake || true)"
[ -n "$CMAKE_EXE" ] || die "CMake not found (install CMake or use the copy shipped with Qt/Visual Studio)"

NINJA_EXE=""
find_ninja() {
    local p
    have ninja && { command -v ninja; return 0; }
    for p in \
        "$QT_INSTALL_ROOT"/Tools/Ninja/ninja.exe \
        "$VS_PATH"/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe
    do
        [ -x "$p" ] && { printf '%s\n' "$p"; return 0; }
    done
    return 1
}
NINJA_EXE="$(find_ninja || true)"

if [ -n "$NINJA_EXE" ]; then
    GENERATOR="Ninja"
elif [ "$TOOLCHAIN" = mingw ]; then
    GENERATOR="MinGW Makefiles"
else
    die "Ninja not found (install Ninja or use the copy shipped with Qt/Visual Studio)"
fi

# ---------------------------------------------------------------------------
# vcpkg detection / bootstrap
# ---------------------------------------------------------------------------
VCPKG_DIR=""

find_vcpkg() {
    local candidates=() c
    [ -n "$VCPKG_ARG" ] && candidates+=("$VCPKG_ARG")
    candidates+=("$SCRIPT_DIR/.vcpkg" "$HOME/vcpkg" /c/vcpkg /d/vcpkg /e/vcpkg /g/vcpkg)
    for c in "${candidates[@]}"; do
        [ -n "$c" ] || continue
        c="$(unixpath "$c")"
        # Accept either a vcpkg root, vcpkg.exe, or the toolchain file itself.
        case "$c" in
            */scripts/buildsystems/vcpkg.cmake) c="${c%/scripts/buildsystems/vcpkg.cmake}" ;;
        esac
        [ -f "$c" ] && c="$(dirname "$c")"
        if [ -f "$c/scripts/buildsystems/vcpkg.cmake" ]; then
            printf '%s\n' "$c"
            return 0
        fi
    done
    return 1
}

VCPKG_DIR="$(find_vcpkg || true)"
if [ -z "$VCPKG_DIR" ]; then
    if [ "$PRINT_CONFIG" = 1 ]; then
        # Do not download anything for --check.
        if [ "$BOOTSTRAP_VCPKG" = 1 ]; then
            VCPKG_DIR="$HOME/vcpkg (would be bootstrapped)"
        else
            VCPKG_DIR="<not found>"
        fi
    elif [ "$BOOTSTRAP_VCPKG" = 1 ]; then
        log "vcpkg not found; bootstrapping into $HOME/vcpkg (one-time download)"
        have git || die "git is required to bootstrap vcpkg"
        git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
        cmd.exe //c "$(winpath "$HOME/vcpkg/bootstrap-vcpkg.bat")" -disableMetrics \
            || die "vcpkg bootstrap failed"
        VCPKG_DIR="$HOME/vcpkg"
    else
        die "vcpkg not found (set VCPKG_ROOT, pass --vcpkg, or omit --no-bootstrap)"
    fi
fi

if [ "$PRINT_CONFIG" != 1 ]; then
    [ -f "$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" ] \
        || die "invalid vcpkg installation: $VCPKG_DIR"
    export VCPKG_ROOT="$(winpath "$VCPKG_DIR")"
    export VCPKG_INSTALLATION_ROOT="$VCPKG_ROOT"
fi

# ---------------------------------------------------------------------------
# Build settings
# ---------------------------------------------------------------------------
case "$TOOLCHAIN" in
    msvc)  TRIPLET="${TRIPLET_ARG:-x64-windows}" ;;
    mingw) TRIPLET="${TRIPLET_ARG:-x64-mingw-dynamic}" ;;
esac

if [ "$DO_DOCS" = auto ]; then
    if have asciidoctor; then
        DO_DOCS=1
    else
        DO_DOCS=0
        warn "asciidoctor not found; offline documentation will be disabled"
    fi
fi

on_off() { [ "$1" = 1 ] && printf 'ON' || printf 'OFF'; }

BUILD_DIR="$(unixpath "$BUILD_DIR")"
case "$BUILD_DIR" in
    /*) ;;
    *) BUILD_DIR="$SCRIPT_DIR/$BUILD_DIR" ;;
esac
BUILD_DIR="$(realpath -m -- "$BUILD_DIR")"
case "$BUILD_DIR" in
    ""|"/"|"$SCRIPT_DIR"|/[a-zA-Z]|/[a-zA-Z]/) \
        die "refusing to use build directory '$BUILD_DIR'" ;;
esac

if [ "$PRINT_CONFIG" = 1 ]; then
    cat <<EOF

KeePassXC Windows build configuration
  Source directory : $SCRIPT_DIR
  Build directory  : $BUILD_DIR
  Toolchain        : $TOOLCHAIN
  Generator        : $GENERATOR
  Build type       : $BUILD_TYPE
  Qt               : ${QT_KIT:-<built by vcpkg>}
  vcpkg            : $VCPKG_DIR
  vcpkg triplet    : $TRIPLET
  CMake            : $CMAKE_EXE
  Ninja            : ${NINJA_EXE:-<not found>}
  MinGW bin        : ${MINGW_BIN:-<n/a>}
  Unit tests       : $(on_off "$DO_TESTS")
  GUI tests        : $(on_off "$DO_GUI_TESTS")
  Documentation    : $(on_off "$DO_DOCS")
  Package          : $(on_off "$DO_PACKAGE")

EOF
    exit 0
fi

if [ "$DO_CLEAN" = 1 ]; then
    log "Removing build directory: $BUILD_DIR"
    rm -rf -- "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------
CMAKE_ARGS=(
    -S "$(winpath "$SCRIPT_DIR")"
    -B "$(winpath "$BUILD_DIR")"
    -G "$GENERATOR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_TOOLCHAIN_FILE="$(winpath "$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake")"
    -DVCPKG_TARGET_TRIPLET="$TRIPLET"
    -DWITH_TESTS="$(on_off "$DO_TESTS")"
    -DWITH_GUI_TESTS="$(on_off "$DO_GUI_TESTS")"
    -DKPXC_FEATURE_DOCS="$(on_off "$DO_DOCS")"
)

[ -n "$NINJA_EXE" ] && CMAKE_ARGS+=(-DCMAKE_MAKE_PROGRAM="$(winpath "$NINJA_EXE")")
[ -n "$KPXC_BUILD_TYPE" ] && CMAKE_ARGS+=(-DKEEPASSXC_BUILD_TYPE="$KPXC_BUILD_TYPE")
[ "$DO_MINIMAL" = 1 ] && CMAKE_ARGS+=(-DKPXC_MINIMAL=ON)

# Local vcpkg port overrides (currently fixes readline-win32 on Windows).
VCPKG_OVERLAY_DIR="$SCRIPT_DIR/vcpkg-overlay"
if [ -d "$VCPKG_OVERLAY_DIR" ]; then
    CMAKE_ARGS+=(-DVCPKG_OVERLAY_PORTS="$(winpath "$VCPKG_OVERLAY_DIR")")
fi

if [ -n "$QT_KIT" ]; then
    CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$(winpath "$QT_KIT")")
else
    CMAKE_ARGS+=(-DWITH_BUILD_QT=ON)
fi

if [ "$TOOLCHAIN" = mingw ]; then
    CMAKE_ARGS+=(
        -DCMAKE_C_COMPILER="$(winpath "$MINGW_BIN/gcc.exe")"
        -DCMAKE_CXX_COMPILER="$(winpath "$MINGW_BIN/g++.exe")"
    )
fi

CMAKE_ARGS+=("${EXTRA_CMAKE[@]}")

log "Configuring with $GENERATOR ($TOOLCHAIN, $BUILD_TYPE)"
printf '    %s\n' "$CMAKE_EXE" "${CMAKE_ARGS[@]}"
"$CMAKE_EXE" "${CMAKE_ARGS[@]}"

[ "$CONFIGURE_ONLY" = 1 ] && { log "Configuration complete (--configure-only)"; exit 0; }

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
log "Building KeePassXC with $JOBS jobs"
"$CMAKE_EXE" --build "$(winpath "$BUILD_DIR")" --config "$BUILD_TYPE" --parallel "$JOBS"

# windeployqt copies Qt DLLs next to the executable, but vcpkg libraries are
# not always placed in the same directory (e.g. libqrencode.dll lives in lib/).
VCPKG_RUNTIME_DIRS=("$BUILD_DIR/vcpkg_installed/$TRIPLET/bin")
if [ "$BUILD_TYPE" = "Debug" ]; then
    VCPKG_RUNTIME_DIRS+=("$BUILD_DIR/vcpkg_installed/$TRIPLET/debug/bin"
                         "$BUILD_DIR/vcpkg_installed/$TRIPLET/debug/lib")
else
    VCPKG_RUNTIME_DIRS+=("$BUILD_DIR/vcpkg_installed/$TRIPLET/lib")
fi
for dir in "${VCPKG_RUNTIME_DIRS[@]}"; do
    [ -d "$dir" ] || continue
    find "$dir" -maxdepth 1 -type f -name '*.dll' \
        -exec cp -f {} "$BUILD_DIR/src/" \;
done

[ -f "$BUILD_DIR/src/cli/keepassxc-cli.exe" ] && cp -f "$BUILD_DIR/src/cli/keepassxc-cli.exe" "$BUILD_DIR/src/"
[ -f "$BUILD_DIR/src/proxy/keepassxc-proxy.exe" ] && cp -f "$BUILD_DIR/src/proxy/keepassxc-proxy.exe" "$BUILD_DIR/src/"

EXE="$BUILD_DIR/src/KeePassXC.exe"
if [ -f "$EXE" ]; then
    log "Build finished: $EXE"
else
    warn "build completed, but $EXE was not found"
fi

# ---------------------------------------------------------------------------
# Package
# ---------------------------------------------------------------------------
if [ "$DO_PACKAGE" = 1 ]; then
    CPACK_EXE="$(dirname "$CMAKE_EXE")/cpack.exe"
    [ -x "$CPACK_EXE" ] || CPACK_EXE="$(command -v cpack || true)"
    [ -n "$CPACK_EXE" ] || die "cpack not found"

    log "Creating ZIP package with CPack"
    ( cd "$BUILD_DIR" && "$CPACK_EXE" -G ZIP -C "$BUILD_TYPE" )
    log "Package(s) written to $BUILD_DIR"
fi

log "Done."
