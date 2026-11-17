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

The Linux ARM32 binary targets ARMv7-A with hard-float, usable on the Pi 3, 4, and 5 under a 32-bit OS. Use AArch64 on a 64-bit OS. The GUI requires a desktop environment (X11/XWayland on Linux, or the native macOS/Windows display system) with FLTK; it does not need OpenGL. The CLI runs headlessly. CI builds both frontends for every listed architecture and runs image tests, with QEMU for Linux ARM.

## Build from source

Use CMake 3.20+, a C11 compiler for the core, CLI and tests, and FLTK 1.4 or newer for the GUI. The FLTK frontend is C++17; every other translation unit stays plain C. No GUI dependency is needed for the core or CLI, and CLI-only builds do not require a C++ compiler.

```sh
# Headless core, CLI and tests
cmake -S . -B build/cli -DZEALFS_BUILD_GUI=OFF
cmake --build build/cli
ctest --test-dir build/cli --output-on-failure

# GUI + CLI, using the system FLTK installation
cmake -S . -B build/default
cmake --build build/default
ctest --test-dir build/default --output-on-failure

# Optional: select an FLTK installation outside the default search paths
cmake -S . -B build/default -DFLTK_DIR=/path/to/lib/cmake/fltk
# or point CMAKE_PREFIX_PATH at the FLTK prefix
```

CMake uses FLTK's imported CMake targets; if a 1.4 installation only provides
the `fltk-config` helper, that fallback supplies the include and link flags.
FLTK 1.3 is rejected at configure time because the frontend uses the
`fl_rounded_rect()` drawing API added in 1.4.

The independent options `ZEALFS_BUILD_GUI`, `ZEALFS_BUILD_CLI`, and `ZEALFS_BUILD_TESTS` default to `ON`. The GUI executable remains `zeal_disk_tool`; the CLI is `zdt` (`.exe` on Windows). Version headers, dependencies, icons and packages stay under the build directory.

Install FLTK 1.4 with your platform's package manager or build it from source. Fedora uses `fltk-devel`; macOS can use `brew install fltk`; MSYS2 UCRT64 uses `pacman -S mingw-w64-ucrt-x86_64-fltk`. Debian bookworm and Ubuntu 22.04 still package FLTK 1.3, so build 1.4 from source there and pass `-DCMAKE_PREFIX_PATH` if it is installed outside the default prefix; [scripts/ci-linux.sh](../scripts/ci-linux.sh) does exactly that, and lists the needed X11 development packages. macOS needs Xcode command-line tools. Windows native builds use MSYS2 UCRT64 GCC, CMake and Ninja.

Native builds on Raspberry Pi use the same CMake commands. Cross-compilation toolchains are provided for `cmake/toolchain-linux-arm32.cmake`, `cmake/toolchain-linux-arm64.cmake`, and `cmake/toolchain-win64.cmake`. GUI cross-builds need FLTK and its X11 dependencies built for the target architecture; point `FLTK_DIR` or `CMAKE_PREFIX_PATH` at the target installation and do not point a cross-build at host libraries. Older Linux/Windows x86 toolchain files remain for legacy users but are outside the supported artifact matrix.

### GUI implementation

The FLTK frontend lives in [src/gui/](../src/gui/) and [include/gui/](../include/gui/): a C++17 single-window shell with a native title bar, a classic menu bar, disk sidebar, partition map, details rail and a dual-pane host/disk file browser, plus a plain-C adapter (`model.c`) that wraps the shared API. Its CMake configuration lives in [cmake/gui.cmake](../cmake/gui.cmake). Icons come from [Lucide](https://lucide.dev) (ISC) and are stored as SVG in `packages/icons/lucide/`; `scripts/generate-icons.py` embeds them into `include/ui_icons.h`, and `Fl_SVG_Image` (from FLTK's images library) rasterises and tints them at the requested size. The same script writes `include/app_icon_png.h` with the 256 px application icon used by the About window and embeds the outlined wordmark (`packages/icons/zeal-disk-tool-wordmark.svg`) as `include/app_wordmark.h`; the wordmark outlines were produced from the Righteous typeface with Inkscape and the font itself is not shipped; the generated `include/app_icon.h` supplies the window icon, and `Fl_Native_File_Chooser` provides host file dialogs. FLTK itself remains an external build dependency with its own license.

## Packages and installation

### Install the CLI from source

Like Zeal Native's `meson install`, installation uses a configurable prefix and
places the executable in its `bin` directory. `make install` builds and installs
only `zdt`, without requiring FLTK or installing the GUI:

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

CPack produces `.tar.gz` on Linux/macOS and `.zip` on Windows. macOS archives include `Applications/ZealDiskTool.app` and `bin/zdt`; Windows/Linux contain both executables in `bin`. macOS packages bundle dynamic dependencies when needed and use an ad-hoc signature; Developer ID signing/notarization is not configured. Linux binaries link FLTK and the system graphics libraries; CLI binaries have no graphics dependencies. CI uploads per-platform archives without automatically publishing a release.

The `gui_package` target creates the legacy GUI-specific `.app` or AppImage. The Makefile forwards build/test/package commands to CMake; `make cli` builds the CLI and `make install` installs only that CLI. An unfiltered `cmake --install` installs the complete development/package contents; use `--component CLI` for CLI-only installation.

## Tests

`ctest --test-dir BUILD_DIRECTORY --output-on-failure` runs the tests enabled for that build. The suites use temporary images and simulated devices; terminal-history tests use a pseudo-terminal on macOS/Linux. They cover staging, transfers, merge/restore, failure handling and command output. These tests do not exercise a physical TF card.

See the [shared C API guide](API.md) for embedding the library.


## C formatting

The repository's [.clang-format](../.clang-format) follows the existing C conventions: four-space indentation, function braces on a separate line, attached control-flow braces, and pointer stars attached to the type. The C++ FLTK frontend uses the same style. Use clang-format 18 or newer for the shared core, the frontend and the tests. For example, from the repository root:

```sh
clang-format --style=file -i src/cli/args.c
```

For changes to existing files, format the edited sections. The `.clang-format-ignore` file excludes the generated headers (`include/app_icon.h` and `include/ui_icons.h`). Split declarations manually and use trailing commas to keep initializer entries on separate lines.
