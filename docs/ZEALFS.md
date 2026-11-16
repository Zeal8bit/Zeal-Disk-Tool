# ZealFS concepts and limits

[README](../README.md) · [CLI guide](CLI.md) · [GUI guide](GUI.md) · [Disk operations](DISKS.md)

Zeal Disk Tool reads and writes **ZealFS v2**, the filesystem used to store programs and personal files for the Zeal 8-bit Computer. Both frontends use the same filesystem implementation. Other partition types may be displayed in a disk's partition table, but only ZealFS v2 file contents can be browsed and edited.

## Disk, partition and filesystem

A disk image is a host file containing disk bytes. A physical disk is a device such as a TF card. Either can use one of these layouts:

| Layout | Contents | Choosing it |
| --- | --- | --- |
| MBR | A partition table followed by up to four primary partitions | Use when the Zeal setup expects a partitioned disk or you need multiple partitions |
| Raw ZealFS | One filesystem starting at byte 0, without a partition table | Use when the Zeal setup expects a raw filesystem image |

An `.img` extension alone does not identify the layout. Open the image and inspect its MBR and partition information. Creating an MBR is not a conversion of an existing raw filesystem; the tool refuses to put an MBR over an active raw partition.

Partition labels differ between interfaces:

| Partition slot | GUI label | CLI `--partition` |
| --- | --- | --- |
| First (including a raw filesystem) | `0` | `1` |
| Second | `1` | `2` |
| Third | `2` | `3` |
| Fourth | `3` | `4` |

Creating a partition also formats it as ZealFS v2. Formatting an existing partition replaces its filesystem with ZealFS v2 when applied, including changing its partition type if necessary. Deleting an MBR partition removes its table entry; deleting a raw partition clears its header. Neither is a secure erase of every old data byte.

## Names and paths

File and directory names are limited to **16 bytes**, including the extension. A name containing non-ASCII characters may use more than one byte per character. Directory components are separated by `/`, and the filesystem root is `/`.

ZealFS paths and host paths are separate. `/games/demo.bin` inside a selected filesystem is unrelated to `/games/demo.bin` on the host. The CLI accepts filesystem paths without a leading slash and normalizes optional trailing slashes; see [CLI paths](CLI.md#paths-and-sizes). The GUI has an address bar for browsing directories; see [GUI navigation](GUI.md#browse-and-manage-files).

The shared API accepts filesystem paths shorter than 512 bytes, including the root slash and excluding the terminating null byte. `.` and `..` path components are unsupported. The GUI's **Up** button navigates to the parent without needing `..`.

## Capacity and allocation

| Item | Supported limit |
| --- | --- |
| Disk/image size | 512 bytes through 64 GiB, aligned to 512 bytes |
| Physical device logical sector size | 512 bytes |
| ZealFS partition size | 8 KiB through 4 GiB |
| MBR partition slots | Four primary partitions; extended partitions are not managed |
| Raw filesystem slots | One, starting at byte 0 |
| Filename/directory name | 16 bytes per component |

ZealFS v2 allocates storage in pages. A bitmap records page allocation and a file allocation table links pages belonging to files and directories. Formatting selects a page size for the partition's capacity; users do not need to choose it separately. Metadata and allocation rounding consume space, so the filesystem's usable/free space differs from the disk or partition's advertised capacity. An empty file still needs a directory entry, and directories also consume storage.

MBR partitions can leave unallocated space before, between or after them. Partition creation uses an available extent and the selected alignment; it does not resize or move existing partitions. Updating from a smaller image does not require shrinking a larger target: [CLI merge](CLI.md#merge-updates-from-an-image-or-device) copies filesystem contents while retaining target geometry.

For implementation details, see the [C API](API.md) and the [v2 format definitions](../include/zealfs_v2.h).
