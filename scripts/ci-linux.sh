#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Run from the repository root on Ubuntu 22.04 or Debian Bookworm.
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends build-essential cmake ninja-build git ca-certificates \
    python3 curl libx11-dev libxext-dev libxft-dev libxinerama-dev libxcursor-dev \
    libxfixes-dev libxrender-dev libfontconfig1-dev libfreetype-dev libpng-dev \
    libjpeg-dev zlib1g-dev xvfb xauth

# The distribution packages are still FLTK 1.3, but the frontend uses the
# rounded-rectangle drawing API introduced in 1.4, so build a current release.
FLTK_VERSION=1.4.5
curl -fsSL "https://github.com/fltk/fltk/archive/refs/tags/release-${FLTK_VERSION}.tar.gz" -o /tmp/fltk.tar.gz
mkdir -p /tmp/fltk-src
tar -xzf /tmp/fltk.tar.gz -C /tmp/fltk-src --strip-components=1
cmake -S /tmp/fltk-src -B /tmp/fltk-build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DFLTK_BUILD_SHARED_LIBS=OFF \
    -DFLTK_BUILD_TEST=OFF \
    -DFLTK_BUILD_EXAMPLES=OFF \
    -DFLTK_BUILD_FLUID=OFF \
    -DFLTK_BUILD_FLTK_OPTIONS=OFF \
    -DFLTK_BUILD_FORMS=OFF \
    -DFLTK_BUILD_GL=OFF
cmake --build /tmp/fltk-build --parallel 2
cmake --install /tmp/fltk-build

cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel 2
ctest --test-dir build/release --output-on-failure
# Verify startup under X11 without ever opening a real device for writing.
result=0
xvfb-run -a timeout 5 build/release/zeal_disk_tool || result=$?
[ "$result" -eq 124 ]
cmake --build build/release --target package
