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

Use CMake 3.20+ and a C11 compiler. The GUI uses the Raylib installation available on your system, without an exact version restriction. No Raylib dependency is needed for the core or CLI.

```sh
# Headless core, CLI and tests
cmake -S . -B build/cli -DZEALFS_BUILD_GUI=OFF
cmake --build build/cli
ctest --test-dir build/cli --output-on-failure

# GUI + CLI, using the system Raylib installation
cmake -S . -B build/default
cmake --build build/default
ctest --test-dir build/default --output-on-failure

# Optional: select a Raylib installation outside the default search paths
cmake -S . -B build/default \
  -DRAYLIB_INCLUDE_DIR=/path/to/raylib/include \
  -DRAYLIB_LIBRARY=/path/to/raylib/lib/libraylib.a
```

The independent options `ZEALFS_BUILD_GUI`, `ZEALFS_BUILD_CLI`, and `ZEALFS_BUILD_TESTS` default to `ON`. The GUI executable remains `zeal_disk_tool`; the CLI is `zdt` (`.exe` on Windows). Version headers, dependencies, icons and packages stay under the build directory.

Install Raylib using your platform's package manager or build it from its source. On Linux ARM, add `-DOPENGL_VERSION=2.1` when configuring Raylib to cover the Pi 3 as well as Pi 4/5. Linux GUI prerequisites are a C compiler plus the OpenGL, X11, Xrandr, Xi, Xinerama, Xcursor, ALSA and pthread development libraries. See [scripts/ci-linux.sh](../scripts/ci-linux.sh) for the Ubuntu/Debian package list. macOS needs Xcode command-line tools. Windows native builds use MSYS2 UCRT64 GCC, CMake and Ninja.

Native builds on Raspberry Pi use the same CMake commands. Cross-compilation toolchains are provided for `cmake/toolchain-linux-arm32.cmake`, `cmake/toolchain-linux-arm64.cmake`, and `cmake/toolchain-win64.cmake`. GUI cross-builds need Raylib and system graphics dependencies built for the target architecture; do not point a cross-build at host libraries. Older Linux/Windows x86 toolchain files remain for legacy users but are outside the supported artifact matrix.

### Bundled GUI dependencies

The vendored headers are copied from these upstream revisions:

| Header | Upstream revision |
| --- | --- |
| `include/nuklear.h` | [Nuklear 4.13.4, `bab1849`](https://github.com/Immediate-Mode-UI/Nuklear/commit/bab18497df988c171fd0d82baad798e1e0e34259) |
| `include/raylib-nuklear.h` | [raylib-nuklear 6.0.1, `cf63c69`](https://github.com/RobLoach/raylib-nuklear/commit/cf63c69bde3db51851832b01b63d600803e14168) |

The adapter includes the upstream [clipping reset fix](https://github.com/RobLoach/raylib-nuklear/pull/108).
Although its header lists Raylib 6.0+, Disk Tool also builds with Raylib 5.5.
The GUI retains its embedded ProggyClean font and loads it with Raylib's public
`LoadFontFromMemory()` API, avoiding a dependency on internal font-packing symbols
that some shared Raylib libraries do not export. Keep upstream headers unformatted
when updating them and preserve their license notices.

The GUI enables Raylib's High-DPI flag so framebuffer dimensions and clipping
use the GUI's logical window coordinates on scaled displays. Window dimensions are
explicitly nonzero at creation because newer Raylib versions interpret zero
dimensions as a fullscreen request.

## Packages and installation

### Install the CLI from source

Like Zeal Native's `meson install`, installation uses a configurable prefix and
places the executable in its `bin` directory. `make install` builds and installs
only `zdt`, without requiring Raylib or installing the GUI:

```sh
make install

# Optional custom prefix, for example a user-writable installation
make install PREFIX="$HOME/.local"
```

For native builds, CMake uses the prefix of an existing `zeal-native` on `PATH`
when it is installed in a `bin` directory. Otherwise, the default prefix is
`/opt/homebrew` on Apple Silicon macOS, `/usr/local` on Intel macOS and Linux,
or CMake's standard Program Files prefix on Windows. For example, installing
beside `/opt/homebrew/bin/zeal-native` places `zdt` in `/opt/homebrew/bin/zdt`.
If Zeal Native uses another layout, pass your preferred prefix explicitly.
An existing build retains its configured prefix; `PREFIX` overrides it at install
time. A protected destination requires administrator access; installation does
not request elevation automatically.

For an existing CMake build, select the `CLI` install component explicitly:

```sh
cmake --build build/default --target zdt
cmake --install build/default --component CLI

# Override the installation prefix without rebuilding
cmake --install build/default --component CLI --prefix "$HOME/.local"
```

On Windows, run the equivalent commands in PowerShell (with CMake and a C
compiler available):

```powershell
cmake -S . -B build/cli -DZEALFS_BUILD_GUI=OFF
cmake --build build/cli --config Release --target zdt
cmake --install build/cli --config Release --component CLI
```

Use `--prefix` to match the prefix used for your Zeal Native installation. Ensure
the prefix's `bin` directory is on `PATH`; neither install command modifies your
shell configuration or Windows `PATH`. Run `zdt --version` to check installation.
The release archive also contains the GUI; place its `.app`, `.exe`, or Linux
executable wherever you prefer.

### Release archives and GUI bundles

```sh
# Archive with GUI, CLI, public header and static core library
cmake --build build/default --target package

# macOS .app, created in the build directory
cmake --build build/default --target mac_app

# Linux GUI AppImage (requires linuxdeploy for the target architecture)
cmake --build build/default --target linux_appimage
```

CPack produces `.tar.gz` on Linux/macOS and `.zip` on Windows. macOS archives include `Applications/ZealDiskTool.app` and `bin/zdt`; Windows/Linux contain both executables in `bin`. macOS packages bundle dynamic dependencies when needed and use an ad-hoc signature; Developer ID signing/notarization is not configured. Linux binaries link Raylib statically in CI and use system graphics libraries; CLI binaries have no graphics dependencies. CI uploads per-platform archives without automatically publishing a release.

The `gui_package` target creates the legacy GUI-specific `.app` or AppImage. The Makefile forwards build/test/package commands to CMake; `make cli` builds the CLI and `make install` installs only that CLI. An unfiltered `cmake --install` installs the complete development/package contents; use `--component CLI` for CLI-only installation.

## Tests

`ctest --test-dir BUILD_DIRECTORY --output-on-failure` runs the tests enabled for that build. The suites use temporary images and simulated devices; terminal-history tests use a pseudo-terminal on macOS/Linux. They cover staging, transfers, merge/restore, failure handling and command output. These tests do not exercise a physical TF card.

See the [shared C API guide](API.md) for embedding the library.


## C formatting

The repository's [.clang-format](../.clang-format) follows the existing C conventions: four-space indentation, function braces on a separate line, attached control-flow braces, and pointer stars attached to the type. Use clang-format 15 or newer for the shared core, frontend code and C tests. For example, from the repository root:

```sh
clang-format --style=file -i src/cli/args.c
```

For changes to existing files, format the edited sections. Keep bundled third-party headers and untouched renderer code in their existing format.
