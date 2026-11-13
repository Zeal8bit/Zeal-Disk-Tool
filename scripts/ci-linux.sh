#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Run from the repository root on Ubuntu 22.04 or Debian Bookworm.
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends build-essential cmake ninja-build git curl ca-certificates \
    python3 libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev libxinerama-dev libxcursor-dev \
    libasound2-dev xvfb xauth
# Pi 3's VC4 desktop driver supports OpenGL 2.1; the same build also runs on Pi 4/5.
case "$(uname -m)" in
    aarch64|arm*) raylib_gl=2.1 ;;
    *) raylib_gl=3.3 ;;
esac
mkdir -p build/deps
curl -fL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz -o build/deps/raylib.tar.gz
tar -xzf build/deps/raylib.tar.gz -C build/deps
cmake -S build/deps/raylib-5.5 -B build/deps/raylib-build -G Ninja \
    -DBUILD_EXAMPLES=OFF -DBUILD_SHARED_LIBS=OFF -DOPENGL_VERSION="$raylib_gl" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PWD/build/deps/raylib-install"
cmake --build build/deps/raylib-build --parallel 2
cmake --install build/deps/raylib-build
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DRAYLIB_INCLUDE_DIR="$PWD/build/deps/raylib-install/include" \
    -DRAYLIB_LIBRARY="$PWD/build/deps/raylib-install/lib/libraylib.a"
cmake --build build/release --parallel 2
ctest --test-dir build/release --output-on-failure
# Verify startup under X11 without ever opening a real device for writing.
result=0
xvfb-run -a timeout 5 build/release/zeal_disk_tool || result=$?
[ "$result" -eq 124 ]
cmake --build build/release --target package
