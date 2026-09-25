#!/usr/bin/env bash
set -e

# ==============================================================================
# KeePassXC macOS Release Build Script
# ==============================================================================
# Builds and packages a release distribution of KeePassXC for macOS, including:
#  - Standalone, self-contained KeePassXC.app bundle (via macdeployqt)
#  - Distributable macOS disk image (.dmg) with custom background & layout
#  - Portable zip archive (.zip)
#  - Code signing (ad-hoc or Apple Developer ID) and optional notarization
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-mac"
DIST_DIR="${SCRIPT_DIR}/dist-mac"

# Safety check: Ensure script is running on macOS
if [ "$(uname -s)" != "Darwin" ]; then
    echo "[Error] This build script must be run natively on macOS." >&2
    exit 1
fi

# Detect default version from CMakeLists.txt
DETECTED_VER_MAJ=$(grep -E 'set\(KEEPASSXC_VERSION_MAJOR' "${SCRIPT_DIR}/CMakeLists.txt" | sed -E 's/.*"([0-9]+)".*/\1/')
DETECTED_VER_MIN=$(grep -E 'set\(KEEPASSXC_VERSION_MINOR' "${SCRIPT_DIR}/CMakeLists.txt" | sed -E 's/.*"([0-9]+)".*/\1/')
DETECTED_VER_PAT=$(grep -E 'set\(KEEPASSXC_VERSION_PATCH' "${SCRIPT_DIR}/CMakeLists.txt" | sed -E 's/.*"([0-9]+)".*/\1/')
DEFAULT_VERSION="${DETECTED_VER_MAJ}.${DETECTED_VER_MIN}.${DETECTED_VER_PAT}"

# Defaults
VERSION="${DEFAULT_VERSION}"
TARGET_ARCH="$(uname -m)"
MACOS_TARGET="12.0"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
SIGN_IDENTITY=""
NOTARIZE_PROFILE=""
DO_NOTARIZE=false
DO_CLEAN=false
BUILD_DMG=true
BUILD_ZIP=true
WITH_TESTS=false
EXTRA_CMAKE_ARGS=()

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS] [-- CMAKE_OPTIONS]

Build and package a release version of KeePassXC for macOS.

Options:
  -v, --version <ver>      Release version number (default: ${DEFAULT_VERSION})
  -a, --arch <arch>        Target architecture: arm64, x86_64 (default: $(uname -m))
  -t, --target <version>   macOS deployment target (default: 12.0)
  -j, --jobs <N>           Number of parallel compilation jobs (default: ${JOBS})
  -s, --sign <identity>    Code signing identity (e.g. "Developer ID Application: ...")
                           If omitted, ad-hoc signing is used for local execution.
  --notarize <profile>     Notarize DMG using notarytool with keychain profile
  --skip-dmg               Skip creating .dmg installer image
  --skip-zip               Skip creating .zip archive of .app bundle
  --with-tests             Build unit tests (default: disabled for release)
  -c, --clean              Wipe build and dist directories before starting
  -h, --help               Display this help message and exit

Examples:
  ./build-mac.sh
  ./build-mac.sh --clean -v 2.8.0
  ./build-mac.sh -s "Developer ID Application: My Company (TEAMID)" --notarize my-profile
EOF
    exit 0
}

# Parse command line options
while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            ;;
        -v|--version)
            VERSION="$2"
            shift 2
            ;;
        -a|--arch)
            TARGET_ARCH="$2"
            shift 2
            ;;
        -t|--target)
            MACOS_TARGET="$2"
            shift 2
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -s|--sign)
            SIGN_IDENTITY="$2"
            shift 2
            ;;
        --notarize)
            DO_NOTARIZE=true
            NOTARIZE_PROFILE="$2"
            shift 2
            ;;
        --skip-dmg)
            BUILD_DMG=false
            shift
            ;;
        --skip-zip)
            BUILD_ZIP=false
            shift
            ;;
        --with-tests)
            WITH_TESTS=true
            shift
            ;;
        -c|--clean)
            DO_CLEAN=true
            shift
            ;;
        --)
            shift
            EXTRA_CMAKE_ARGS+=("$@")
            break
            ;;
        *)
            echo "[Error] Unknown argument: $1" >&2
            echo "Run '$(basename "$0") --help' for usage." >&2
            exit 1
            ;;
    esac
done

# Check build prerequisites
echo "=== Checking build environment ==="
if ! command -v cmake >/dev/null 2>&1; then
    echo "[Error] 'cmake' is required but not found." >&2
    echo "Install via Homebrew: brew install cmake" >&2
    exit 1
fi

if ! command -v clang++ >/dev/null 2>&1; then
    echo "[Error] Apple Clang toolchain not found. Install Xcode Command Line Tools: xcode-select --install" >&2
    exit 1
fi

# Detect Homebrew environment and Qt
if command -v brew >/dev/null 2>&1; then
    BREW_PREFIX="$(brew --prefix)"
    export PATH="${BREW_PREFIX}/bin:${PATH}"

    # Auto-detect Homebrew Qt path
    if [ -d "${BREW_PREFIX}/opt/qt" ]; then
        QT_PREFIX="${BREW_PREFIX}/opt/qt"
    elif [ -d "${BREW_PREFIX}/opt/qt@6" ]; then
        QT_PREFIX="${BREW_PREFIX}/opt/qt@6"
    fi

    if [ -n "${QT_PREFIX}" ]; then
        export PATH="${QT_PREFIX}/bin:${PATH}"
        EXTRA_CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${QT_PREFIX}")
    fi
fi

# Locate macdeployqt
MACDEPLOYQT_BIN="${DEPLOYQT_EXE:-$(command -v macdeployqt 2>/dev/null || true)}"
if [ -z "${MACDEPLOYQT_BIN}" ] && [ -n "${QT_PREFIX}" ] && [ -x "${QT_PREFIX}/bin/macdeployqt" ]; then
    MACDEPLOYQT_BIN="${QT_PREFIX}/bin/macdeployqt"
fi

if [ -z "${MACDEPLOYQT_BIN}" ]; then
    echo "[Error] 'macdeployqt' not found. Ensure Qt 6 is installed." >&2
    echo "Install via Homebrew: brew install qt" >&2
    exit 1
fi
echo "[Info] Found macdeployqt: ${MACDEPLOYQT_BIN}"

# Detect documentation generator (asciidoctor)
if command -v asciidoctor >/dev/null 2>&1; then
    EXTRA_CMAKE_ARGS+=("-DKPXC_FEATURE_DOCS=ON")
else
    echo "[Warn] 'asciidoctor' not found. Offline documentation generation disabled."
    EXTRA_CMAKE_ARGS+=("-DKPXC_FEATURE_DOCS=OFF")
fi

# Choose CMake generator (Ninja if available, otherwise Unix Makefiles)
if command -v ninja >/dev/null 2>&1; then
    CMAKE_GENERATOR="Ninja"
else
    CMAKE_GENERATOR="Unix Makefiles"
fi

# Clean directories if requested
if [ "${DO_CLEAN}" = true ]; then
    echo "[Info] Cleaning build and dist directories..."
    rm -rf "${BUILD_DIR}" "${DIST_DIR}"
fi

mkdir -p "${BUILD_DIR}"
mkdir -p "${DIST_DIR}"

# ==============================================================================
# [1/3] CMake Configuration
# ==============================================================================
echo ""
echo "=== [1/3] Configuring KeePassXC for macOS (${TARGET_ARCH}, Release) ==="
echo "Version:           ${VERSION}"
echo "Architecture:      ${TARGET_ARCH}"
echo "Deployment target: macOS ${MACOS_TARGET}"
echo "Generator:         ${CMAKE_GENERATOR}"
echo "Jobs:              ${JOBS}"
echo "Build Dir:         ${BUILD_DIR}"
echo "Dist Dir:          ${DIST_DIR}"

CMAKE_CONFIG_ARGS=(
    "-G${CMAKE_GENERATOR}"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DKEEPASSXC_BUILD_TYPE=Release"
    "-DOVERRIDE_VERSION=${VERSION}"
    "-DCMAKE_OSX_ARCHITECTURES=${TARGET_ARCH}"
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=${MACOS_TARGET}"
    "-DWITH_APP_BUNDLE=ON"
    "-DWITH_XC_ALL=ON"
    "-DWITH_XC_REMOTESYNC=ON"
    "-DWITH_TESTS=$( [ "${WITH_TESTS}" = true ] && echo "ON" || echo "OFF" )"
    "-DWITH_GUI_TESTS=$( [ "${WITH_TESTS}" = true ] && echo "ON" || echo "OFF" )"
    "-DDEPLOYQT_EXE=${MACDEPLOYQT_BIN}"
)

# If an Apple Developer signing identity is specified, configure it in CMake
if [ -n "${SIGN_IDENTITY}" ] && [ "${SIGN_IDENTITY}" != "-" ]; then
    CMAKE_CONFIG_ARGS+=("-DWITH_XC_CODESIGN_IDENTITY=${SIGN_IDENTITY}")
    if [ "${DO_NOTARIZE}" = true ] && [ -n "${NOTARIZE_PROFILE}" ]; then
        CMAKE_CONFIG_ARGS+=("-DWITH_XC_NOTARY_KEYCHAIN_PROFILE=${NOTARIZE_PROFILE}")
    fi
fi

cmake -B "${BUILD_DIR}" "${CMAKE_CONFIG_ARGS[@]}" "${EXTRA_CMAKE_ARGS[@]}" "${SCRIPT_DIR}"

# ==============================================================================
# [2/3] Compiling Codebase
# ==============================================================================
echo ""
echo "=== [2/3] Compiling KeePassXC codebase ==="
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

# ==============================================================================
# [3/3] Packaging macOS Release Artifacts
# ==============================================================================
echo ""
echo "=== [3/3] Packaging macOS Release Artifacts ==="

# Staging app bundle
APP_BUNDLE="${DIST_DIR}/KeePassXC.app"
rm -rf "${APP_BUNDLE}"

echo "[Info] Installing application bundle and resources..."
cmake --install "${BUILD_DIR}" --prefix "${DIST_DIR}"

if [ ! -d "${APP_BUNDLE}" ]; then
    echo "[Error] Application bundle not found at ${APP_BUNDLE} after installation." >&2
    exit 1
fi

# Ensure helper utilities exist in the bundle
for bin in keepassxc-cli keepassxc-proxy; do
    if [ ! -f "${APP_BUNDLE}/Contents/MacOS/${bin}" ]; then
        if [ -f "${BUILD_DIR}/src/${bin}" ]; then
            cp "${BUILD_DIR}/src/${bin}" "${APP_BUNDLE}/Contents/MacOS/${bin}"
        elif [ -f "${BUILD_DIR}/src/cli/${bin}" ]; then
            cp "${BUILD_DIR}/src/cli/${bin}" "${APP_BUNDLE}/Contents/MacOS/${bin}"
        elif [ -f "${BUILD_DIR}/src/proxy/${bin}" ]; then
            cp "${BUILD_DIR}/src/proxy/${bin}" "${APP_BUNDLE}/Contents/MacOS/${bin}"
        fi
    fi
done

# Strip binary symbols for release size optimization
echo "[Info] Stripping binary debug symbols..."
strip -x "${APP_BUNDLE}/Contents/MacOS/KeePassXC" 2>/dev/null || true
[ -f "${APP_BUNDLE}/Contents/MacOS/keepassxc-cli" ] && strip -x "${APP_BUNDLE}/Contents/MacOS/keepassxc-cli" 2>/dev/null || true
[ -f "${APP_BUNDLE}/Contents/MacOS/keepassxc-proxy" ] && strip -x "${APP_BUNDLE}/Contents/MacOS/keepassxc-proxy" 2>/dev/null || true

# Code signing step
ENTITLEMENTS="${SCRIPT_DIR}/share/macosx/keepassxc.entitlements"
if [ -n "${SIGN_IDENTITY}" ] && [ "${SIGN_IDENTITY}" != "-" ]; then
    echo "[Info] Signing bundle with Developer ID: ${SIGN_IDENTITY}..."

    # 1. Sign all embedded frameworks and dylibs
    find "${APP_BUNDLE}/Contents/Frameworks" "${APP_BUNDLE}/Contents/PlugIns" \
        -type f \( -name "*.dylib" -o -perm +111 \) 2>/dev/null | while read -r lib; do
        if file "$lib" | grep -q "Mach-O"; then
            codesign --sign "${SIGN_IDENTITY}" --force --options=runtime --timestamp "$lib" 2>/dev/null || true
        fi
    done

    find "${APP_BUNDLE}/Contents/Frameworks" -type d -name "*.framework" 2>/dev/null | while read -r fw; do
        codesign --sign "${SIGN_IDENTITY}" --force --options=runtime --timestamp "$fw" 2>/dev/null || true
    done

    # 2. Sign helper executables
    codesign --sign "${SIGN_IDENTITY}" --force --options=runtime --timestamp \
        "${APP_BUNDLE}/Contents/MacOS/keepassxc-cli"
    codesign --sign "${SIGN_IDENTITY}" --force --options=runtime --timestamp \
        "${APP_BUNDLE}/Contents/MacOS/keepassxc-proxy"

    # 3. Sign main app bundle with entitlements
    codesign --sign "${SIGN_IDENTITY}" --force --options=runtime --timestamp \
        --entitlements "${ENTITLEMENTS}" \
        "${APP_BUNDLE}"
else
    echo "[Info] Signing bundle with ad-hoc identity for local execution..."
    codesign --force --deep -s - "${APP_BUNDLE}"
fi

# Verify codesign
echo "[Info] Verifying code signature..."
codesign -v "${APP_BUNDLE}"
echo "[Info] Bundle verification successful!"

# Test-run binary version output
echo -n "[Info] Application version check: "
"${APP_BUNDLE}/Contents/MacOS/KeePassXC" --version

PACKAGE_BASE="KeePassXC-${VERSION}-${TARGET_ARCH}"

# Create Portable Zip Archive
if [ "${BUILD_ZIP}" = true ]; then
    ZIP_NAME="${PACKAGE_BASE}.zip"
    ZIP_PATH="${DIST_DIR}/${ZIP_NAME}"
    echo "[Info] Creating portable zip archive: ${ZIP_NAME}..."
    rm -f "${ZIP_PATH}"
    (
        cd "${DIST_DIR}"
        # -y preserves symbolic links inside frameworks
        zip -r -y -q "${ZIP_NAME}" "KeePassXC.app"
    )
fi

# Create Distributable DMG Disk Image
if [ "${BUILD_DMG}" = true ]; then
    DMG_NAME="${PACKAGE_BASE}.dmg"
    DMG_PATH="${DIST_DIR}/${DMG_NAME}"
    DMG_STAGING="${DIST_DIR}/.dmg_staging"

    echo "[Info] Creating disk image installer: ${DMG_NAME}..."
    rm -rf "${DMG_STAGING}" "${DMG_PATH}"
    mkdir -p "${DMG_STAGING}/.background"

    # Copy application bundle
    cp -R "${APP_BUNDLE}" "${DMG_STAGING}/"

    # Create link to Applications folder
    ln -s /Applications "${DMG_STAGING}/Applications"

    # Copy custom DMG styling assets if present
    if [ -f "${SCRIPT_DIR}/share/macosx/background.tiff" ]; then
        cp "${SCRIPT_DIR}/share/macosx/background.tiff" "${DMG_STAGING}/.background/"
    fi
    if [ -f "${SCRIPT_DIR}/share/macosx/DS_Store.in" ]; then
        cp "${SCRIPT_DIR}/share/macosx/DS_Store.in" "${DMG_STAGING}/.DS_Store"
    fi
    if [ -f "${SCRIPT_DIR}/share/macosx/keepassxc.icns" ]; then
        cp "${SCRIPT_DIR}/share/macosx/keepassxc.icns" "${DMG_STAGING}/.VolumeIcon.icns"
    fi

    # Set custom volume icon bit if SetFile is available
    if command -v SetFile >/dev/null 2>&1; then
        SetFile -a C "${DMG_STAGING}" 2>/dev/null || true
    fi

    # Build compressed disk image
    hdiutil create \
        -volname "KeePassXC" \
        -srcfolder "${DMG_STAGING}" \
        -ov \
        -format UDZO \
        "${DMG_PATH}"

    rm -rf "${DMG_STAGING}"

    # Sign DMG if certificate provided
    if [ -n "${SIGN_IDENTITY}" ] && [ "${SIGN_IDENTITY}" != "-" ]; then
        echo "[Info] Signing DMG installer..."
        codesign --sign "${SIGN_IDENTITY}" --timestamp "${DMG_PATH}"
    fi

    # Notarize if requested
    if [ "${DO_NOTARIZE}" = true ]; then
        if [ -z "${NOTARIZE_PROFILE}" ]; then
            echo "[Error] --notarize requires a keychain profile name." >&2
            exit 1
        fi
        echo "[Info] Submitting DMG to Apple Notary Service..."
        xcrun notarytool submit "${DMG_PATH}" --keychain-profile "${NOTARIZE_PROFILE}" --wait
        echo "[Info] Stapling notarization ticket..."
        xcrun stapler staple "${DMG_PATH}"
        xcrun stapler validate "${DMG_PATH}"
    fi
fi

# Print final build summary
echo ""
echo "=================================================="
echo "macOS release build and packaging complete!"
echo "--------------------------------------------------"
echo "Application bundle: ${APP_BUNDLE}"
if [ "${BUILD_DMG}" = true ] && [ -f "${DIST_DIR}/${PACKAGE_BASE}.dmg" ]; then
    echo "Disk image (DMG):   ${DIST_DIR}/${PACKAGE_BASE}.dmg ($(du -h "${DIST_DIR}/${PACKAGE_BASE}.dmg" | cut -f1 | tr -d ' '))"
fi
if [ "${BUILD_ZIP}" = true ] && [ -f "${DIST_DIR}/${PACKAGE_BASE}.zip" ]; then
    echo "Portable archive:   ${DIST_DIR}/${PACKAGE_BASE}.zip ($(du -h "${DIST_DIR}/${PACKAGE_BASE}.zip" | cut -f1 | tr -d ' '))"
fi
echo "=================================================="
