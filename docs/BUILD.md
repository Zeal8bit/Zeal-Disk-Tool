# Building and installing

[README](../README.md) · [CLI guide](CLI.md) · [GUI guide](GUI.md)

Run the commands below from the repository root.

## Supported targets

| Platform | Architecture | Baseline |
| --- | --- | --- |
| macOS | arm64, x86_64 | macOS 11+ |
| Linux desktop | x86_64 | Ubuntu 22.04+ |
| Raspberry Pi OS | ARMv7 hard-float, AArch64 | Bookworm+, Pi 3/4/5 |
| Windows | x86_64 | Windows 10+, MinGW-w64/UCRT build |

The Linux ARM32 binary targets ARMv7-A with hard-float, usable on the Pi 3, 4, and 5 under a 32-bit OS. Use AArch64 on a 64-bit OS. The GUI requires a desktop environment with X11/XWayland and OpenGL; the CLI runs headlessly. Linux ARM packages use Raylib’s OpenGL 2.1 backend so they also work with the Pi 3’s VC4 desktop driver. CI builds both frontends for every listed architecture and runs image tests, with QEMU for Linux ARM.

## Build from source

Use CMake 3.20+ and a C11 compiler. **The GUI requires Raylib 5.5.** Raylib 6.0 is not a compatible drop-in dependency for this renderer; CMake rejects it rather than producing a broken GUI. No Raylib dependency is needed for the core or CLI.

```sh
# Headless core, CLI and tests
cmake -S . -B build/cli -DZEALFS_BUILD_GUI=OFF
cmake --build build/cli
ctest --test-dir build/cli --output-on-failure

# GUI + CLI, with an existing Raylib 5.5 installation
cmake -S . -B build/default \
  -DRAYLIB_INCLUDE_DIR=/path/to/raylib/include \
  -DRAYLIB_LIBRARY=/path/to/raylib/lib/libraylib.a
cmake --build build/default
ctest --test-dir build/default --output-on-failure
```

The independent options `ZEALFS_BUILD_GUI`, `ZEALFS_BUILD_CLI`, and `ZEALFS_BUILD_TESTS` default to `ON`. The GUI executable remains `zeal_disk_tool`; the CLI is `zdt` (`.exe` on Windows). Version headers, dependencies, icons and packages stay under the build directory.

To build Raylib 5.5 without changing a system installation:

```sh
mkdir -p build/deps
curl -fL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz -o build/deps/raylib.tar.gz
tar -xzf build/deps/raylib.tar.gz -C build/deps
cmake -S build/deps/raylib-5.5 -B build/deps/raylib-build \
  -DBUILD_EXAMPLES=OFF -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/deps/raylib-install"
cmake --build build/deps/raylib-build
cmake --install build/deps/raylib-build
```

Use the installed include directory and library in the GUI configuration above. On Linux ARM, add `-DOPENGL_VERSION=2.1` when configuring Raylib to cover the Pi 3 as well as Pi 4/5. Linux GUI prerequisites are a C compiler plus the OpenGL, X11, Xrandr, Xi, Xinerama, Xcursor, ALSA and pthread development libraries. See [scripts/ci-linux.sh](../scripts/ci-linux.sh) for the Ubuntu/Debian package list. macOS needs Xcode command-line tools. Windows native builds use MSYS2 UCRT64 GCC, CMake and Ninja.

Native builds on Raspberry Pi use the same CMake commands. Cross-compilation toolchains are provided for `cmake/toolchain-linux-arm32.cmake`, `cmake/toolchain-linux-arm64.cmake`, and `cmake/toolchain-win64.cmake`. GUI cross-builds need Raylib and system graphics dependencies built for the target architecture; do not point a cross-build at host libraries. Older Linux/Windows x86 toolchain files remain for legacy users but are outside the supported artifact matrix.

## Packages and installation

```sh
# Archive with GUI, CLI, public header and static core library
cmake --build build/default --target package

# Standard prefix installation
cmake --install build/default --prefix "$HOME/.local"

# macOS .app, created in the build directory
cmake --build build/default --target mac_app

# Linux GUI AppImage (requires linuxdeploy for the target architecture)
cmake --build build/default --target linux_appimage
```

CPack produces `.tar.gz` on Linux/macOS and `.zip` on Windows. macOS archives include `Applications/ZealDiskTool.app` and `bin/zdt`; Windows/Linux contain both executables in `bin`. macOS packages bundle dynamic dependencies when needed and use an ad-hoc signature; Developer ID signing/notarization is not configured. Linux binaries link Raylib statically in CI and use system graphics libraries; CLI binaries have no graphics dependencies. CI uploads per-platform archives without automatically publishing a release.

The `gui_package` target creates the legacy GUI-specific `.app` or AppImage. The Makefile now forwards build/test/package/install commands to CMake; use `make cli` for a headless build.

## Tests

`ctest --test-dir BUILD_DIRECTORY --output-on-failure` runs the tests enabled for that build. The suites use temporary images and simulated devices; terminal-history tests use a pseudo-terminal on macOS/Linux. They cover staging, transfers, merge/restore, failure handling and command output. These tests do not exercise a physical TF card.

See the [shared C API guide](API.md) for embedding the library.


## C formatting

The repository's [.clang-format](../.clang-format) follows the existing C conventions: four-space indentation, function braces on a separate line, attached control-flow braces, and pointer stars attached to the type. Use clang-format 15 or newer for the shared core, frontend code and C tests. For example, from the repository root:

```sh
clang-format --style=file -i src/cli/args.c
```

For changes to existing files, format the edited sections. Keep bundled third-party headers and untouched renderer code in their existing format.
