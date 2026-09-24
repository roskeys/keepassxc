#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="${SCRIPT_DIR}/build/src/keepassxc"

# Build first if binary does not exist
if [ ! -f "${BINARY}" ]; then
    echo "[Info] KeePassXC binary not found at ${BINARY}. Running build script..."
    "${SCRIPT_DIR}/build.sh"
fi

export DISPLAY="${DISPLAY:-:0}"
export QT_X11_NO_MITSHM=1

echo "=== Launching KeePassXC ==="
echo "DISPLAY=${DISPLAY}"

# Check running Linux dev container
RUNNING_CONTAINER=""
if [ ! -f /.dockerenv ] && command -v docker >/dev/null 2>&1; then
    if docker ps --format '{{.Names}}' 2>/dev/null | grep -q "^keepassxc-linux-dev-container$"; then
        RUNNING_CONTAINER="keepassxc-linux-dev-container"
    elif docker ps --format '{{.Names}}' 2>/dev/null | grep -q "^keepassxc-dev-container$"; then
        RUNNING_CONTAINER="keepassxc-dev-container"
    fi
fi

# If running on host, sync XAUTHORITY and forward into container
if [ -n "${RUNNING_CONTAINER}" ]; then
    # Automatically locate X11 authority file on the host
    if [ -z "${XAUTHORITY}" ] || [ ! -r "${XAUTHORITY}" ]; then
        XAUTH_FILE=$(find "/run/user/$(id -u)" -name ".*Xwaylandauth*" -o -name ".*Xauthority*" 2>/dev/null | head -n 1)
        if [ -n "${XAUTH_FILE}" ] && [ -r "${XAUTH_FILE}" ]; then
            export XAUTHORITY="${XAUTH_FILE}"
        elif [ -f "${HOME}/.Xauthority" ]; then
            export XAUTHORITY="${HOME}/.Xauthority"
        fi
    fi

    # Sync XAUTHORITY to shared workspace and container /tmp
    if [ -n "${XAUTHORITY}" ] && [ -r "${XAUTHORITY}" ]; then
        cp "${XAUTHORITY}" "${SCRIPT_DIR}/.xauthority" 2>/dev/null || true
        chmod 644 "${SCRIPT_DIR}/.xauthority" 2>/dev/null || true
        cat "${XAUTHORITY}" | docker exec -i "${RUNNING_CONTAINER}" bash -c "cat > /tmp/.Xauthority && chmod 644 /tmp/.Xauthority" 2>/dev/null || true
    fi

    echo "XAUTHORITY=${XAUTHORITY}"
    echo "[Info] Forwarding launch into Docker container '${RUNNING_CONTAINER}'..."
    exec docker exec -it \
        -u developer \
        -e DISPLAY="${DISPLAY}" \
        -e XAUTHORITY="/workspace/keepassxc/.xauthority" \
        "${RUNNING_CONTAINER}" /workspace/keepassxc/build/src/keepassxc "$@"
else
    # Running inside container
    if [ -f "${SCRIPT_DIR}/.xauthority" ]; then
        export XAUTHORITY="${SCRIPT_DIR}/.xauthority"
    elif [ -f "/tmp/.Xauthority" ]; then
        export XAUTHORITY="/tmp/.Xauthority"
    fi
    echo "XAUTHORITY=${XAUTHORITY}"
    exec "${BINARY}" "$@"
fi
