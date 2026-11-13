# Working with images and physical disks

[README](../README.md) · [CLI guide](CLI.md) · [GUI guide](GUI.md) · [ZealFS concepts](ZEALFS.md)

These concepts apply to both frontends. A **target** is the image or device you are editing. In a merge, the **source** supplies updated files. A **host path** is a normal file or directory on the computer running Zeal Disk Tool.

## Apply and discard

Changes to a target are staged in temporary storage. Creating images or partitions, formatting, importing files, making directories and deleting entries do not write the target immediately. Browsing, usage displays and exports see those staged changes.

- **Apply** writes all pending changes for the selected target.
- **Discard** removes all pending changes and returns to the committed state. The GUI calls this **Cancel**.
- A newly created image does not appear at its destination until Apply. Discarding its creation leaves no image there.
- Staged changes are not saved as a session across process exits.

The [CLI guide](CLI.md) explains automatic Apply for one-shot commands and explicit `apply` in the shell. The [GUI guide](GUI.md#save-discard-and-quit) explains its buttons and exit prompts.

Image Apply builds and flushes a complete replacement alongside the existing image, then replaces it. Allow enough host disk space for the replacement and temporary staging. External modifications can prevent Apply; reopen the image rather than continuing with a stale session. Use the actual image path rather than a symbolic link. If an image has hard links, replacing one path leaves the other links pointing at the old image.

Physical disks are written in place. A write/flush failure can leave a partially updated card and marks the session failed. Reopen it before further work; discarding staged changes cannot undo sectors already written. Merge/restore additionally read back writes and retain any backup for explicit recovery. See [CLI restore](CLI.md#restore-a-partition-backup).

## Import, export and backups

**Import** copies a host file into the selected filesystem and stages it. Existing destination files are not overwritten. Use a destination name within the [16-byte name limit](ZEALFS.md#names-and-paths). Importing a host directory recursively is not supported.

**Export** copies filesystem contents to the host immediately, including staged contents. Discarding later does not remove those exported files. The GUI exports selected files; the CLI also [exports directories recursively](CLI.md#export-files-and-directories). Export cannot replace the active image or an alias of it.

A filesystem export is different from a raw backup:

| Copy | Preserves | Does not preserve |
| --- | --- | --- |
| Exported files/directories | File contents and directory structure | Raw allocation, unused space, partition geometry or MBR |
| Merge's partition backup | Every original byte of the selected partition, including unused capacity | MBR, other partitions or the entire physical card |

A partition backup is a raw `.img` file that can be opened for inspection or [restored with the CLI](CLI.md#restore-a-partition-backup). Its capacity must exactly match the selected target partition for restore. A host directory of exported files is not a restore image.

## Physical device access

Image files normally need only normal file permissions. Physical device access needs OS permissions, typically root on macOS/Linux or an elevated terminal on Windows. `zdt` does not request elevation itself. To start an installed CLI with device access on macOS/Linux:

```sh
sudo zdt devices
sudo zdt shell
```

For a local build, substitute its path, such as `sudo ./build/default/zdt shell`. Start the GUI executable with suitable permissions if using its physical-disk controls. On Linux the GUI also needs access to the desktop display; device permissions may be preferable to running a desktop application as root.

| Platform | Whole-disk path example | Host inventory |
| --- | --- | --- |
| macOS | `/dev/rdisk4` | `diskutil list` |
| Linux | `/dev/sdb`, `/dev/mmcblk0`, `/dev/nvme0n1`, `/dev/vda` | `lsblk -o NAME,SIZE,TYPE,MOUNTPOINTS` |
| Windows | `\\.\PhysicalDrive2` | Disk Management |

Examples identify path syntax, not the correct disk for your machine. Check the actual card and capacity. A macOS built-in card reader may be listed as **internal**, so `diskutil list external physical` can miss it.

Close other programs using the card and unmount host-mounted filesystems before updating it. **Merge and restore refuse mounted physical sources/targets and never unmount them automatically.** On macOS, unmounting the whole card with `diskutil unmountDisk /dev/diskN` leaves the raw device available; ejecting it removes the device. On Linux, unmount each mounted partition shown by `lsblk`. On Windows, release mounted volumes and applications holding the disk before retrying.

Protection acquired for merge/restore remains held until the session closes, including after Apply or discard. Close a shell/device session before trying to use that card from another process.

## A card is missing from the device list

Discovery lists devices that the current process can open and parse within supported limits. Inaccessible devices are silently skipped. If the GUI selector or `zdt devices` omits a card:

1. Check the host inventory above to confirm the OS sees it.
2. Check permissions; retry discovery from an appropriately elevated process if needed.
3. Check the [capacity and sector-size limits](ZEALFS.md#capacity-and-allocation).
4. For a known path, run `zdt -d DEVICE info` to see its specific error instead of relying on discovery.

Refresh the GUI with **File → Refresh devices** after resolving pending changes. In the CLI, each `devices` command performs fresh discovery.
