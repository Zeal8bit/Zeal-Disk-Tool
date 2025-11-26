# Zeal Disk Tool

<center>
    <img src="md_imgs/zeal_disk_tool_screen1.png" alt="Zeal Disk Tool screenshot" />
</center>

**Zeal Disk Tool** is a utility to create ZealFS(v2) partitions on disks, such as TF/microSD or CF cards, so that they can be used on Zeal 8-bit Computer.
It provides a GUI to view available disks, view its partitions, and create new partitions.

The current version is compatible with Windows and Linux.

## Features

- View all available disks
- View existing partitions
- Create new ZealFSv2 partitions
- **Changes are cached** and only saved to disk when explicitly applied — prevents accidental data loss
- Cross-platform (Linux and Windows)
- Simple graphical interface built with [Raylib](https://www.raylib.com/) and Nuklear
- To protect internal/unrelated disks, disks over 64GB will be hidden and cannot be modified

> ⚠️ Disclaimer: **Use at your own risk.** Zeal Disk Tool modifies disk images and may interact with physical drives if misused. I am not responsible for any data loss, disk corruption, or damage caused by the use or misuse of this tool. Always back up important data before working with disk images.

## IMPORTANT

On Windows, the program must be executed as Administrator in order to have access to the disks.

Similarly on Linux, the program must be run as root, also to have access to the disks. The program will look for the disks named `/dev/sdx`, make sure the disk you want to manage is mounted under this path in your system.

## Project Goals

The goal of Zeal Disk Tool is to offer a user-friendly, cross-platform graphical interface for creating custom ZealFS partitions on disks. Other partiton/disk management programs don't offer the possibility to create partitions with a custom type (MBR custom type), nor format the partition with a custom file system.

---

## Building

This project has been developed for Linux/Mac hosts initially, but it can also be cross-compiled to target Windows thanks to `i686-w64-mingw32-gcc` compiler.

### Prerequisites

#### Linux (dynamic binary)

- Install Raylib by downloading the release v5.5. from the [official Github release page](https://github.com/raysan5/raylib/releases/tag/5.5)
- Extract the downloaded archive in `raylib/linux32` or `raylib/linux` directory, for 32-bit or 64-bit version respectively. The structure should look like:

```shell
├── LICENSE
├── Makefile
├── raylib
│   ├── linux
│   │   ├── include
│   │   └── lib
│   └── linux32
│       ├── include
│       └── lib
├── README.md
├── src
...
```

- Install `gcc` if you don't have it

#### MacOS / Darwin

- Install Raylib via home

```shell
homebrew install raylib
```

- Install XCode tools, if not already installed

```shell
xcode-select --install
```

#### Windows 32-bit (cross-compile static binary on Linux)

- Install Raylib by downloading the release v5.5. from the [official Github release page](https://github.com/raysan5/raylib/releases/tag/5.5)
- Extract the downloaded archive in `raylib/win32`. The structure should look like:

```shell
├── LICENSE
├── Makefile
├── raylib
│   └── win32
│       ├── include
│       └── lib
├── README.md
├── src
...
```

- Install `mingw` toolchain, on Ubuntu, you can use the command `sudo apt install gcc-mingw-w64-i686`

#### Windows 64-bit (cross-compile static binary on Linux)

- Install Raylib by downloading the release v5.5. from the [official Github release page](https://github.com/raysan5/raylib/releases/tag/5.5)
- Extract the downloaded archive in `raylib/win64`. The structure should look like:

```shell
├── LICENSE
├── Makefile
├── raylib
│   └── win64
│       ├── include
│       └── lib
├── README.md
├── src
...
```

- Install `mingw` toolchain, on Ubuntu, you can use the command `sudo apt install gcc-mingw-w64-x86-64`

### Compile

#### Natively

This project uses CMake, to compile for your host computer, use the following commands:

```shell
cmake -B build
cmake --build build
```

#### Cross-compiling for Windows

This project provides CMake toolchain files for building both 32-bit and 64-bit Windows binaries using mingw-w64 toolchain:

**32-bit target:**

```
cmake -B build-win32 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-win32.cmake
cmake --build build-win32
```

**64-bit target:**

```
cmake -B build-win64 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-win64.cmake
cmake --build build-win64
```

### Package / Install

The provided CMake configuration can automatically create an AppImage or MacOS App bundle.

To create an AppImage/AppBundle, you can use the `package` target

```shell
cmake --build build --target package
```

To install to the default location (`~/Applications` on MacOS)

```shell
cmake --build build --target install
```

#### Bundler Prerequisities

##### Linux AppImage

- Install linuxdeploy from [the official Github page](https://github.com/linuxdeploy/linuxdeploy/releases).

##### MacOS App Bundle

- Install xcode command line tools

```shell
xcode-select --install
````

## License

Distributed under the Apache 2.0 License. See LICENSE file for more information.

You are free to use it for personal and commercial use, the boilerplate present in each file must not be removed.

## Contact

For any suggestion or request, you can contact me at contact [at] zeal8bit [dot] com

For feature requests, you can also open an issue or a pull request.