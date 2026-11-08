# Zeal Disk Tool

Zeal Disk Tool manages ZealFS v2 disk images and physical disks for the Zeal 8-bit Computer. It provides a graphical application, `zeal_disk_tool`, and a terminal tool, `zdt`, backed by the same C library.

![Zeal Disk Tool](md_imgs/zeal_disk_tool_screen1.png)

Both interfaces can create images and partitions, browse files, import/export files, and manage directories. The CLI also supports recursive directory export, merging software updates while preserving target-only files, partition backups and restore, and scripting with JSON output.

The project supports macOS, Linux (including Raspberry Pi) and Windows. See [supported targets, building and installation](docs/BUILD.md) for architectures, dependencies and package contents.

## Documentation

| Guide | Topics |
| --- | --- |
| [CLI](docs/CLI.md) | Commands, options, interactive shell and history, recursive export, merge/restore, automation |
| [GUI](docs/GUI.md) | Open/create images, select cards, manage partitions, browse/import/export files, Apply and Cancel |
| [ZealFS concepts](docs/ZEALFS.md) | MBR versus raw layouts, partition numbering, names, paths, allocation and capacity limits |
| [Working with disks](docs/DISKS.md) | Shared staging behavior, image/device writes, imports/exports versus backups, permissions and discovery troubleshooting |
| [Build and installation](docs/BUILD.md) | Supported platforms, CMake, Raylib, cross-compilation, packages and tests |
| [C API](docs/API.md) | Embedding the shared library, ownership, errors, staging and transfer lifecycle |

## Get started

For the graphical workflow, open the application and follow the [GUI guide](docs/GUI.md). For terminal use, start with the [CLI quick start](docs/CLI.md#quick-start-with-an-image), or inspect an existing image:

```sh
zdt -i tf.img ls /
zdt -i tf.img shell
```

GUI and shell target edits remain staged until Apply. One-shot CLI mutations validate their changes and ask before saving unless `--yes` is supplied. Host exports are immediate. See [shared disk operations](docs/DISKS.md) for how saving an image differs from writing a physical card.

To update a card from a downloaded software image, follow [merge and restore](docs/CLI.md#merge-updates-from-an-image-or-device). For missing devices or permission errors, see [physical device access](docs/DISKS.md#physical-device-access).

## License

Apache-2.0; see [LICENSE](LICENSE). Existing copyright and third-party license notices are retained. Raylib and the bundled Nuklear adapter retain their own licenses.

Contact: contact [at] zeal8bit [dot] com.
