# Shared C API

`include/zealfs.h` is the public interface. Link the CMake target `zealfs_core` or the installed static library. It has no GUI or console dependencies. The disk and session are represented by one opaque `zfs_disk` handle; each handle owns its image/device descriptor, temporary sector overlay and callbacks. Multiple handles are independent, with no global current disk or partition.

```c
#include <zealfs.h>

int create_image(void) {
    zfs_disk *disk = NULL;
    zfs_error error = {0};
    int partition;
    zfs_status status = zfs_image_create("disk.img", 16 * 1024 * 1024, true, &disk, &error);
    if (!status) status = zfs_partition_create(disk, 8 * 1024 * 1024, 1024 * 1024, &partition, &error);
    if (!status) status = zfs_mkdir(disk, partition, "/programs", &error);
    if (!status) status = zfs_file_write(disk, partition, "/programs/hello", "hello", 5, &error);
    /* Nothing has appeared at disk.img yet. Caller decides whether to commit. */
    if (!status) status = zfs_apply(disk, &error);
    zfs_close(disk);
    return status == ZFS_OK ? 0 : 1;
}
```

## Ownership and errors

- Open/create return an owned handle through an output pointer, set to NULL on failure. Close accepts NULL and discards unpublished work.
- `zfs_get_info` returns a borrowed snapshot valid until the next operation or close. Never modify it.
- `zfs_list` returns owned normalized entries, including names, sizes, types and decoded timestamps. Release them with `zfs_entries_free`; an empty listing is valid.
- Status is always returned explicitly. Error details are caller-owned and meaningful on failure; pass NULL if details are unnecessary. The core never prints, opens dialogs, or exits the application.
- All API partition indices are zero-based. Frontends translate user-visible partition numbers 1–4. API paths use `/` within the filesystem, independently of the host OS; dot components and trailing slashes are rejected except for root `/`. The CLI accepts optional leading/trailing slashes and normalizes its filesystem arguments before calling the API.
- Each handle is single-threaded. Different handles can be used independently; do not concurrently apply to the same target. Progress/log callbacks run synchronously and must not reenter that handle.

## Staging contract

All disk-changing API operations stage sectors; only `zfs_apply` writes the target. Browsing, reads, usage and export see the current overlay, including newly formatted partitions. Host export is an immediate operation with temporary-output publication, and cannot replace the active image or an alias of it. Host imports, byte-based file creation, MBR changes, partition changes, mkdir and removal are operation-atomic: on failure the previous staged state is restored.

The overlay is a sparse temporary file with a sector bitmap, capped at 16 MiB of bitmap memory for a 64 GiB disk. Operation undo records stream to another temporary file; imported file contents do not accumulate in RAM. Temporary storage is private and removed on close. A storage failure that also prevents rollback marks the session failed; close/reopen it rather than continuing with uncertain staged state.

Discard clears staged sectors and restores the original metadata. Discarding a new image cancels its publication, leaving a blank unpublished handle that may be closed or edited again. The GUI/CLI close that handle when discarding creation. There is no persistence of staged sessions across process exits.

Image Apply checks for external changes, streams the merged image into a unique sibling file, flushes it, then replaces the target. New destinations are published without replacement. Existing POSIX image permissions are preserved. Use the actual image path rather than a symbolic link. Hard-linked image paths are replaced independently; other hard links keep the old inode. A failed pre-publication Apply retains pending data and leaves the original unchanged. There is no cross-process locking protocol; callers must prevent simultaneous writers.

Device Apply opens the selected device for writing only at commit time and flushes it before reporting success. It cannot offer atomic replacement. Any write/flush failure returns `ZFS_PARTIAL_WRITE`, marks the session failed, and requires reopening. Discard does not undo sectors already written to a physical device.

## Boundaries

Disks/images are sector-aligned, from 512 bytes through 64 GiB. ZealFS partitions are 8 KiB–4 GiB; file sizes are limited by the existing v2 on-disk format. Public disk sizes, offsets, usage counters and transfer progress use 64-bit values, with large-file support enabled for ARM32 builds. Platform device backends reject non-512-byte logical sectors.

The existing `zealfs_v2` engine remains private. Its on-disk layout is unchanged. GUI adapter structs in `disk.h` are presentation models, not part of this public API. A future CLI or another frontend should call `zealfs.h` directly rather than depend on those structs.
