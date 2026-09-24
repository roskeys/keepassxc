#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-win64"
DIST_DIR="${SCRIPT_DIR}/dist-win64"
IMAGE_NAME="keepassxc-win64-builder"
CONTAINER_NAME="keepassxc-win64-build-container"
FALLBACK_CONTAINER="keepassxc-dev-container"
DOCKERFILE="${SCRIPT_DIR}/docker/windows/Dockerfile"

container_running() {
    command -v docker >/dev/null 2>&1 && \
    docker ps --format '{{.Names}}' 2>/dev/null | grep -q "^$1$"
}

# If running on host:
if [ ! -f /.dockerenv ]; then
    if ! command -v docker >/dev/null 2>&1; then
        echo "[Error] Docker is required to cross-compile for Windows."
        exit 1
    fi

    ACTIVE_CONTAINER=""
    if container_running "${CONTAINER_NAME}"; then
        ACTIVE_CONTAINER="${CONTAINER_NAME}"
    elif container_running "${FALLBACK_CONTAINER}"; then
        ACTIVE_CONTAINER="${FALLBACK_CONTAINER}"
    fi

    if [ -n "${ACTIVE_CONTAINER}" ]; then
        echo "[Info] Executing Windows cross-build inside running container '${ACTIVE_CONTAINER}'..."
        exec docker exec -it \
            -u "$(id -u):$(id -g)" \
            -w /workspace/keepassxc \
            "${ACTIVE_CONTAINER}" ./build-win64.sh "$@"
    else
        echo "[Info] Container '${CONTAINER_NAME}' is not running. Launching transient container..."
        if ! docker image inspect "${IMAGE_NAME}" >/dev/null 2>&1; then
            echo "[Info] Building Docker image '${IMAGE_NAME}' from ${DOCKERFILE}..."
            docker build -t "${IMAGE_NAME}" -f "${DOCKERFILE}" "${SCRIPT_DIR}"
        fi

        exec docker run --rm -it \
            -u "$(id -u):$(id -g)" \
            -v "${SCRIPT_DIR}:/workspace/keepassxc" \
            -w /workspace/keepassxc \
            "${IMAGE_NAME}" ./build-win64.sh "$@"
    fi
fi

# ==========================================
# Inside Docker Container Windows Cross Build
# ==========================================
echo "=== [1/2] Cross-compiling KeePassXC for Windows (x86_64-w64-mingw32, Qt6) ==="
mkdir -p "${BUILD_DIR}"
mkdir -p "${DIST_DIR}"

cd "${BUILD_DIR}"
echo "[Info] Configuring CMake with mingw64-cmake..."
mingw64-cmake -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DKEEPASSXC_BUILD_TYPE=Release \
    -DOVERRIDE_VERSION=2.8.0 \
    -DWITH_TESTS=OFF \
    -DWITH_GUI_TESTS=OFF \
    -DWITH_XC_ALL=ON \
    -DWITH_XC_REMOTESYNC=ON \
    -DDEPLOYQT_EXE=/bin/true \
    ..

echo "[Info] Compiling..."
ninja -j$(nproc)

echo "=== [2/2] Packaging Windows Release Artifacts ==="
PACKAGE_NAME="KeePassXC-Win64-Portable"
TARGET_DIR="${DIST_DIR}/${PACKAGE_NAME}"
SRC_DIR="${BUILD_DIR}/src"
SYS_ROOT="/usr/x86_64-w64-mingw32/sys-root/mingw"

rm -rf "${TARGET_DIR}"
mkdir -p "${TARGET_DIR}"

# Copy executable and strip symbols for release
cp "${SRC_DIR}/KeePassXC.exe" "${TARGET_DIR}/" 2>/dev/null || \
    cp "${SRC_DIR}/keepassxc.exe" "${TARGET_DIR}/"
cp "${SRC_DIR}/cli/keepassxc-cli.exe" "${TARGET_DIR}/" 2>/dev/null || \
    cp "${SRC_DIR}/keepassxc-cli.exe" "${TARGET_DIR}/" 2>/dev/null || true
cp "${SRC_DIR}/proxy/keepassxc-proxy.exe" "${TARGET_DIR}/" 2>/dev/null || \
    cp "${SRC_DIR}/keepassxc-proxy.exe" "${TARGET_DIR}/" 2>/dev/null || true
x86_64-w64-mingw32-strip "${TARGET_DIR}/"*.exe

# Copy essential Qt6 plugins
mkdir -p "${TARGET_DIR}/platforms"
mkdir -p "${TARGET_DIR}/imageformats"
mkdir -p "${TARGET_DIR}/iconengines"
mkdir -p "${TARGET_DIR}/styles"
mkdir -p "${TARGET_DIR}/tls"

cp "${SYS_ROOT}/lib/qt6/plugins/platforms/qwindows.dll" "${TARGET_DIR}/platforms/" 2>/dev/null || \
    cp "${SYS_ROOT}/lib64/qt6/plugins/platforms/qwindows.dll" "${TARGET_DIR}/platforms/" 2>/dev/null || true
cp "${SYS_ROOT}/lib/qt6/plugins/imageformats/"*.dll "${TARGET_DIR}/imageformats/" 2>/dev/null || \
    cp "${SYS_ROOT}/lib64/qt6/plugins/imageformats/"*.dll "${TARGET_DIR}/imageformats/" 2>/dev/null || true
cp "${SYS_ROOT}/lib/qt6/plugins/iconengines/"*.dll "${TARGET_DIR}/iconengines/" 2>/dev/null || \
    cp "${SYS_ROOT}/lib64/qt6/plugins/iconengines/"*.dll "${TARGET_DIR}/iconengines/" 2>/dev/null || true
cp "${SYS_ROOT}/lib/qt6/plugins/styles/"*.dll "${TARGET_DIR}/styles/" 2>/dev/null || \
    cp "${SYS_ROOT}/lib64/qt6/plugins/styles/"*.dll "${TARGET_DIR}/styles/" 2>/dev/null || true
cp "${SYS_ROOT}/lib/qt6/plugins/tls/"*.dll "${TARGET_DIR}/tls/" 2>/dev/null || \
    cp "${SYS_ROOT}/lib64/qt6/plugins/tls/"*.dll "${TARGET_DIR}/tls/" 2>/dev/null || true

# Automatically and recursively resolve and copy all runtime DLL dependencies
echo "[Info] Resolving and copying all DLL dependencies..."
while true; do
    missing_found=0
    for file in $(find "${TARGET_DIR}" -type f \( -name "*.exe" -o -name "*.dll" \)); do
        for needed_dll in $(x86_64-w64-mingw32-objdump -p "$file" 2>/dev/null | grep -i "DLL Name:" | awk '{print $3}'); do
            if [ ! -f "${TARGET_DIR}/${needed_dll}" ]; then
                found_dll=""
                if [ -f "${SYS_ROOT}/bin/${needed_dll}" ]; then
                    found_dll="${SYS_ROOT}/bin/${needed_dll}"
                elif [ -f "${SYS_ROOT}/lib/${needed_dll}" ]; then
                    found_dll="${SYS_ROOT}/lib/${needed_dll}"
                fi

                if [ -n "${found_dll}" ]; then
                    cp "${found_dll}" "${TARGET_DIR}/"
                    missing_found=1
                fi
            fi
        done
    done
    if [ "$missing_found" -eq 0 ]; then
        break
    fi
done

# Create zip package
cd "${DIST_DIR}"
rm -f "${PACKAGE_NAME}.zip"
zip -r "${PACKAGE_NAME}.zip" "${PACKAGE_NAME}"

echo "=================================================="
echo "Windows build and packaging complete!"
echo "Release directory: ${DIST_DIR}/${PACKAGE_NAME}"
echo "Portable archive:  ${DIST_DIR}/${PACKAGE_NAME}.zip"
echo "=================================================="
