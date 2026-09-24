FROM fedora:44

ENV LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

# Install build tools & Windows MinGW-w64 cross-compilation toolchain
RUN dnf -y update && dnf -y install --setopt=install_weak_deps=False \
    gcc \
    gcc-c++ \
    cmake \
    ninja-build \
    make \
    git \
    which \
    zip \
    p7zip \
    asciidoctor \
    python3 \
    # Windows MinGW-w64 cross-compilation toolchain
    mingw64-gcc \
    mingw64-gcc-c++ \
    mingw64-binutils \
    mingw64-winpthreads \
    mingw64-filesystem \
    mingw64-qt6-qtbase \
    mingw64-qt6-qtsvg \
    mingw64-qt6-qttools \
    mingw64-zlib \
    mingw64-minizip \
    mingw64-readline \
    && dnf clean all

# Cross-compile Botan 3 for MinGW-w64
RUN curl -sL https://botan.randombit.net/releases/Botan-3.7.1.tar.xz | tar -xJ -C /tmp && \
    cd /tmp/Botan-3.7.1 && \
    python3 ./configure.py \
      --os=mingw \
      --cpu=x86_64 \
      --cc=gcc \
      --cc-bin=x86_64-w64-mingw32-g++ \
      --ar-command=x86_64-w64-mingw32-ar \
      --prefix=/usr/x86_64-w64-mingw32/sys-root/mingw \
      --build-targets=shared \
      --without-documentation && \
    make -j$(nproc) && \
    make install && \
    rm -rf /tmp/Botan-3.7.1 /tmp/botan*

# Cross-compile libqrencode for MinGW-w64
RUN curl -sL https://github.com/fukuchi/libqrencode/archive/refs/tags/v4.1.1.tar.gz | tar -xz -C /tmp && \
    cd /tmp/libqrencode-4.1.1 && \
    mingw64-cmake -GNinja -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DBUILD_SHARED_LIBS=ON -DWITH_TOOLS=OFF -DWITH_TESTS=OFF . && \
    ninja install && \
    cp /usr/x86_64-w64-mingw32/sys-root/mingw/lib/libqrencode.dll /usr/x86_64-w64-mingw32/sys-root/mingw/bin/ && \
    rm -rf /tmp/libqrencode*

# Create developer user matching host UID/GID (1000) for clean file permissions
RUN groupadd -g 1000 developer && \
    useradd -u 1000 -g developer -m -s /bin/bash developer

WORKDIR /workspace/keepassxc

USER developer

CMD ["tail", "-f", "/dev/null"]
