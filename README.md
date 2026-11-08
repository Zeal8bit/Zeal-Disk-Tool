# Zeal Disk Tool GUI and CLI

Zeal Disk Tool manages ZealFS v2 disk images and physical disks for the Zeal 8-bit Computer. The existing Raylib/Nuklear GUI and the `zdt` CLI both use the same standalone C library, `zealfs_core`.

![Zeal Disk Tool](md_imgs/zeal_disk_tool_screen1.png)

Both frontends support disk discovery, images with an MBR or a raw ZealFS filesystem, partition creation/formatting/deletion, directory browsing/creation/deletion, file import/export, and filesystem usage reporting. The GUI layout and renderer are unchanged.

**All target changes are staged until Apply**, including creating images, creating MBRs, importing files, and deleting files or directories. Pending changes are visible when browsing or exporting. Discard leaves the original image/device unchanged. A new image does not appear at its destination until Apply. Export writes host files/directories immediately.

Image Apply builds and flushes a replacement file alongside the original, then atomically publishes it. Allow enough free space for the resulting image. Physical devices require in-place writes; an interrupted or failed Apply can leave a device partially updated. Such sessions must be reopened before further changes. Back up valuable data before using either frontend.

## Supported targets

| Platform | Architecture | Baseline |
| --- | --- | --- |
| macOS | arm64, x86_64 | macOS 11+ |
| Linux desktop | x86_64 | Ubuntu 22.04+ |
| Raspberry Pi OS | ARMv7 hard-float, AArch64 | Bookworm+, Pi 3/4/5 |
| Windows | x86_64 | Windows 10+, MinGW-w64/UCRT build |

The Linux ARM32 binary targets ARMv7-A with hard-float, usable on the Pi 3, 4, and 5 under a 32-bit OS. Use AArch64 on a 64-bit OS. The GUI requires a desktop environment with X11/XWayland and OpenGL; the CLI runs headlessly. Linux ARM packages use Raylib’s OpenGL 2.1 backend so they also work with the Pi 3’s VC4 desktop driver. CI builds both frontends for every listed architecture and runs image tests, with QEMU for Linux ARM.

Physical devices over 64 GiB are excluded. Device backends currently require 512-byte logical sectors. Linux discovery includes `/dev/sd*`, `/dev/mmcblk*`, `/dev/nvme*`, and `/dev/vd*` whole disks; partition nodes are excluded. macOS uses `/dev/rdiskN`; Windows uses `\\.\PhysicalDriveN`.

Ordinary image operations do not need elevated privileges. Physical device access requires appropriate device permissions (typically root on Linux/macOS or an elevated terminal on Windows). Run `zdt devices` with those permissions when needed; inaccessible devices are omitted. Specify the target explicitly and ensure it is not in use by another program.

## Build

Use CMake 3.20+ and a C11 compiler. **The unchanged GUI renderer requires Raylib 5.5.** Raylib 6.0 is not a compatible drop-in dependency for this renderer; CMake rejects it rather than producing a broken GUI. No Raylib dependency is needed for the core or CLI.

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

Use the installed include directory and library in the GUI configuration above. On Linux ARM, add `-DOPENGL_VERSION=2.1` when configuring Raylib to cover the Pi 3 as well as Pi 4/5. Linux GUI prerequisites are a C compiler plus the OpenGL, X11, Xrandr, Xi, Xinerama, Xcursor, ALSA and pthread development libraries. See [scripts/ci-linux.sh](scripts/ci-linux.sh) for the Ubuntu/Debian package list. macOS needs Xcode command-line tools. Windows native builds use MSYS2 UCRT64 GCC, CMake and Ninja.

Native builds on Raspberry Pi use the same CMake commands. Cross-compilation toolchains are provided for `cmake/toolchain-linux-arm32.cmake`, `cmake/toolchain-linux-arm64.cmake`, and `cmake/toolchain-win64.cmake`. GUI cross-builds need Raylib and system graphics dependencies built for the target architecture; do not point a cross-build at host libraries. Older Linux/Windows x86 toolchain files remain for legacy users but are outside the supported artifact matrix.

## GUI

Start `zeal_disk_tool`, or pass an image filename:

```sh
./build/default/zeal_disk_tool disk.img
```

The existing menus, partition diagram, controls, dialogs and file browser remain in place. An asterisk indicates pending changes. Apply saves all pending operations, and Cancel discards them. New and formatted partitions can be browsed before Apply. Switching targets or refreshing devices requires resolving pending changes. Closing with pending work offers Apply, Discard or Cancel.

## CLI

```sh
# Create an image and an aligned partition (each command prompts before saving)
zdt image create disk.img --size 16MiB --mbr
zdt --image disk.img partition create --size 8MiB
zdt --image disk.img mkdir /programs
zdt --image disk.img import ./hello.bin /programs/hello.bin
zdt --image disk.img ls programs/
zdt --image disk.img export /programs/hello.bin ./hello-copy.bin

# Explicit device targeting, with suitable permissions
zdt devices
zdt --device /dev/sdb info

# Scripts must explicitly opt into committing or previewing
zdt --image disk.img partition format --partition 1 --dry-run
zdt --image disk.img import ./hello.bin /hello.bin --yes
zdt --image disk.img info --json
```

Run `zdt --help` for the complete command list. Options can appear before or after the command. `--image` (`-i`) and `--device` (`-d`) are mutually exclusive. The short flags also work in shell commands such as `open -i disk.img`. Partition numbers are 1–4, defaulting to 1; raw images use partition 1. Sizes accept integer bytes or `B`, `KiB`, `MiB`, and `GiB`. Partition creation defaults to the largest available extent, capped at the ZealFS v2 maximum of 4 GiB, with 1 MiB alignment for MBR images. Raw partitions start at byte 0.

One-shot mutations stage and validate first, then show a summary and ask before Apply. `--yes` skips that prompt; `--dry-run` discards the proposed change. Without an interactive terminal, either flag is required. Export asks before replacing an existing host file unless `--yes` is given. Image creation and filesystem imports never overwrite existing destinations.

### Updating from another image or device

Merge recursively adds source entries and replaces matching files, preserving every target-only file and directory. Identical files are skipped. Any file/directory type conflict aborts the entire merge before backup or target writes. Source and target may have different capacities; the complete merged filesystem must fit.

```sh
# Preview with no backup or target writes
zdt -d /dev/rdisk4 merge -f tf.img --dry-run --json

# Back up the selected partition, then apply the update
zdt -d /dev/rdisk4 merge -f tf.img -b before.img --yes --json

# Restore that partition exactly
zdt -d /dev/rdisk4 restore -f before.img --yes
```

`-f` means `--from-image`; use `--from-device PATH` for a physical source. Merge/restore automatically select the sole ZealFS partition on each side. When ambiguous, specify `--partition N` for the target and `--from-partition N` for a merge source.

`-b` (`--backup`) creates a new raw image of the entire selected target partition, including unused capacity. It never overwrites an existing file. The MBR and other partitions are neither backed up nor changed. Allow host space for this backup, temporary staging, and an entire replacement disk image when updating an image file. Backup creation is flushed and byte-verified before staging the merge. A merge with no changes creates no backup.

Backup is optional: omitting `-b` asks whether to proceed without a backup. `--yes` explicitly accepts proceeding, including without a backup; noninteractive commands without `--yes` cancel with exit code 3. Dry runs and unchanged merges need no confirmation.

In shell `merge`, `-i` aliases `--from-image` and `-d` aliases `--from-device`, since the target is already open. For example, `merge -i tf.img -b before.img` or `merge -d /dev/rdisk5 -b before.img`. The existing `-f` source-image alias remains accepted. Top-level `-i`/`-d` and shell `open -i`/`open -d` still select the target.

In the shell, merge requires a clean target. It creates the requested backup immediately and stages the update; `apply` writes it, while `discard` leaves the backup intact. Without `-b`, shell merge asks for confirmation before staging. Restore also stages until `apply`. Restore requires a raw backup of exactly the selected partition's capacity and can recover damaged filesystem metadata after closing and reopening a failed session.

Mounted physical sources and targets are refused; unmount the whole disk yourself. Device protection remains held until the session closes, including after discard. Physical writes are flushed and read back. A failure may leave a partially written device: keep the backup, reopen the device and restore it explicitly. There is no automatic rollback. Without a backup, there is no saved recovery image.

Transfer JSON includes source/target identities and partition numbers, file/directory counts, copied bytes, conflicts, and `noop`, `backup_created`, `staged`, and `applied` flags. The backup path is present when supplied. ZDE host scripts can invoke these same native commands with local image paths, `--yes`, `--json`, and exit status handling; physical-device updates must run on the host. Downloads and ZDE integration are outside this change.

### Interactive shell

```text
zdt --image disk.img shell
zdt> mkdir /programs
zdt> import "host file.bin" /programs/program.bin
zdt> ls /programs
zdt> status
zdt> apply
zdt> quit
```

Inside the shell, `help` shows the shell command reference. Up/Down browse the last 32 nonempty commands; history stays in memory and is cleared when the shell exits. Down past the newest entry restores the command you were typing. Recalled commands can be edited before Enter. Shell mutations stay pending until `apply` or `discard`. `open --image PATH` and `open --device PATH` switch targets, asking how to handle pending changes. `quit` offers the same choice. End-of-input discards pending work and returns exit code 3. Input supports single/double quoted arguments, without expansion or external command execution. `--partition N` applies to the individual command. JSON, `--yes`, and dry runs are one-shot features, not shell modes.

Filesystem paths are always interpreted from the selected filesystem root. The leading `/` is optional, and trailing slashes are accepted: `games`, `games/`, `/games`, and `/games/` all refer to the same directory. There is no current-directory state or drive-letter prefix. Host import/export paths keep their normal host filesystem meaning. Each name is at most 16 bytes. Recursive host export is supported; recursive filesystem copy/delete and renaming existing filesystem entries are not included; the GUI's import-time name prompt remains available for long host filenames.

### Exporting directories

`export FS_PATH HOST_PATH` copies a file to the specified host filename, or recursively copies a directory's contents into the specified host directory. Exporting `/` copies the entire selected filesystem. Missing destination directories are created, including empty directories from the source. Export sees staged contents and does not require Apply.

```text
zdt> export / /Users/higginsd/Downloads/tf/exports
zdt> export games ./games-export
```

Existing unrelated host files remain. Before any writes, export validates the source tree and checks destination conflicts. Existing files require overwrite confirmation (or `--yes` in one-shot mode); file/directory mismatches, links within the destination tree, unsafe host names and attempts to overwrite the active image are refused. A dry run validates without creating host files or directories. Files publish individually: an I/O failure can leave completed files and directories, so a directory export is not an atomic snapshot.

### JSON and exit status

One-shot `--json` results use `{"ok":true,"data":...}`; failures use `{"ok":false,"error":{"code":"...","message":"..."}}`. Info and partition lists return disk metadata; listings and discovery return arrays. Prompts, diagnostics and progress summaries go to stderr so stdout remains valid JSON.

| Exit code | Meaning |
| --- | --- |
| 0 | Success, including a successful dry run |
| 1 | Filesystem, permission or I/O error |
| 2 | Invalid command or arguments |
| 3 | Confirmation needed/declined, or pending shell changes lost at EOF |

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

## Library and verification

See [docs/API.md](docs/API.md) for ownership, error handling, staging and a C example. Tests operate on temporary images and an in-memory device backend. They cover persistence, rollback, transfer equality, invalid inputs, raw images, large offsets, CLI output, interactive staging, device permission failures, partial writes and flush failures.

## License

Apache-2.0; see [LICENSE](LICENSE). Existing copyright and third-party license notices are retained. Raylib and the bundled Nuklear adapter retain their own licenses.

Contact: contact [at] zeal8bit [dot] com.
