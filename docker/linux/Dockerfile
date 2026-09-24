FROM fedora:44

ENV LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

# Install build dependencies for native Linux KeePassXC (Qt6) development
RUN dnf -y update && dnf -y install --setopt=install_weak_deps=False \
    gcc \
    gcc-c++ \
    cmake \
    ninja-build \
    make \
    git \
    which \
    asciidoctor \
    xauth \
    # Native Linux Qt6 development packages
    qt6-qtbase-devel \
    qt6-qtbase-private-devel \
    qt6-qtsvg-devel \
    qt6-qttools-devel \
    # Native cryptographic and system libraries
    botan3-devel \
    libgcrypt-devel \
    minizip-devel \
    zlib-ng-compat-devel \
    readline-devel \
    pcsc-lite-devel \
    libXi-devel \
    libXtst-devel \
    libX11-devel \
    libxkbcommon-devel \
    libxkbcommon-x11-devel \
    qrencode-devel \
    libargon2-devel \
    libusb1-devel \
    libyubikey-devel \
    ykpers-devel \
    && dnf clean all

# Create developer user matching host UID/GID (1000) for clean file permissions
RUN groupadd -g 1000 developer && \
    useradd -u 1000 -g developer -m -s /bin/bash developer

WORKDIR /workspace/keepassxc

USER developer

CMD ["tail", "-f", "/dev/null"]
