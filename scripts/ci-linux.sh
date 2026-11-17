#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Run from the repository root on Ubuntu 22.04 or Debian Bookworm.
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends build-essential cmake ninja-build git ca-certificates \
    python3 libfltk1.3-dev xvfb xauth
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel 2
ctest --test-dir build/release --output-on-failure
# Verify startup under X11 without ever opening a real device for writing.
result=0
xvfb-run -a timeout 5 build/release/zeal_disk_tool || result=$?
[ "$result" -eq 124 ]
cmake --build build/release --target package
