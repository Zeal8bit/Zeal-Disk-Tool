# SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
# SPDX-License-Identifier: Apache-2.0
# CMake owns all source lists, dependencies, and platform selection.
BUILD_DIR ?= build/default
CMAKE_FLAGS ?=
.PHONY: all configure test package install clean linux windows macosx cli
all: configure
	cmake --build $(BUILD_DIR)
configure:
	cmake -S . -B $(BUILD_DIR) $(CMAKE_FLAGS)
test: all
	ctest --test-dir $(BUILD_DIR) --output-on-failure
package: all
	cmake --build $(BUILD_DIR) --target package
install: all
	cmake --install $(BUILD_DIR)
cli:
	cmake -S . -B build/cli -DZEALFS_BUILD_GUI=OFF $(CMAKE_FLAGS)
	cmake --build build/cli
##########################
#       Shortcuts        #
##########################
linux macosx: all
############################
# Build the Windows binary #
############################
windows:
	cmake -S . -B build/win64 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-win64.cmake $(CMAKE_FLAGS)
	cmake --build build/win64
clean:
	cmake --build $(BUILD_DIR) --target clean
