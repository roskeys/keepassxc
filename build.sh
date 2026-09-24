#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
IMAGE_NAME="keepassxc-linux-dev"
CONTAINER_NAME="keepassxc-linux-dev-container"
FALLBACK_CONTAINER="keepassxc-dev-container"
DOCKERFILE="${SCRIPT_DIR}/docker/linux/Dockerfile"

# Helper function to check if a container is currently running
container_running() {
    command -v docker >/dev/null 2>&1 && \
    docker ps --format '{{.Names}}' 2>/dev/null | grep -q "^$1$"
}

# If running on host:
if [ ! -f /.dockerenv ]; then
    if ! command -v docker >/dev/null 2>&1; then
        echo "[Error] Docker is required to build inside the container."
        exit 1
    fi

    # Ensure the container is running or run via standalone docker container
    ACTIVE_CONTAINER=""
    if container_running "${CONTAINER_NAME}"; then
        ACTIVE_CONTAINER="${CONTAINER_NAME}"
    elif container_running "${FALLBACK_CONTAINER}"; then
        ACTIVE_CONTAINER="${FALLBACK_CONTAINER}"
    fi

    if [ -n "${ACTIVE_CONTAINER}" ]; then
        echo "[Info] Executing Linux build inside running container '${ACTIVE_CONTAINER}'..."
        exec docker exec -it \
            -u "$(id -u):$(id -g)" \
            -w /workspace/keepassxc \
            "${ACTIVE_CONTAINER}" ./build.sh "$@"
    else
        echo "[Info] Container '${CONTAINER_NAME}' is not running. Launching transient container..."
        # Build image if it does not exist
        if ! docker image inspect "${IMAGE_NAME}" >/dev/null 2>&1; then
            echo "[Info] Building Docker image '${IMAGE_NAME}' from ${DOCKERFILE}..."
            docker build -t "${IMAGE_NAME}" -f "${DOCKERFILE}" "${SCRIPT_DIR}"
        fi

        exec docker run --rm -it \
            -u "$(id -u):$(id -g)" \
            -v "${SCRIPT_DIR}:/workspace/keepassxc" \
            -w /workspace/keepassxc \
            "${IMAGE_NAME}" ./build.sh "$@"
    fi
fi

# ==========================================
# Inside Docker Container Build Routine
# ==========================================
echo "=== Building KeePassXC (Native Linux / Qt6) ==="

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "[1/2] Running CMake configuration..."
cmake -GNinja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DWITH_XC_ALL=ON \
    -DWITH_XC_REMOTESYNC=ON \
    ..

echo "[2/2] Compiling codebase..."
ninja -j$(nproc)

echo "=================================================="
echo "Build complete!"
echo "Binary created at: ${BUILD_DIR}/src/keepassxc"
echo "=================================================="
