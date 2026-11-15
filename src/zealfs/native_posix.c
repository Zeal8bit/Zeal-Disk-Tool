/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <ctype.h>
#include <stdlib.h>
#include <dirent.h>
#ifdef __APPLE__
#include <sys/disk.h>
#include <sys/mount.h>
#else
#include <linux/fs.h>
#include <sys/sysmacros.h>
#endif
zfs_status zfs_native_open(const char* path, bool write_access, zfs_native* fd, uint64_t* size, zfs_error* error)
{
    *fd = open(path, write_access ? O_RDWR : O_RDONLY);
    if (*fd < 0) {
        return zfs_error_set(error,
                             errno == EACCES || errno == EPERM ? ZFS_PERMISSION : ZFS_IO,
                             "Cannot open device %s: %s",
                             path,
                             strerror(errno));
    }
    /* Get the size of the disk, make sure it is not bigger than expected */
    struct stat st;
    unsigned block = 0;
    bool ok = fstat(*fd, &st) == 0;
#ifdef __APPLE__
    uint64_t count = 0;
    ok = ok && S_ISCHR(st.st_mode) && ioctl(*fd, DKIOCGETBLOCKSIZE, &block) == 0 &&
         ioctl(*fd, DKIOCGETBLOCKCOUNT, &count) == 0;
    *size = count * block;
#else
    ok = ok && S_ISBLK(st.st_mode) && ioctl(*fd, (int) BLKGETSIZE64, size) == 0 && ioctl(*fd, BLKSSZGET, &block) == 0;
#endif
    if (!ok || block != DISK_SECTOR_SIZE || *size < DISK_SECTOR_SIZE || *size > ZFS_MAX_DISK_SIZE) {
        close(*fd);
        *fd = ZFS_NATIVE_INVALID;
        return zfs_error_set(error,
                             ZFS_INVALID,
                             "Device must have 512-byte sectors and a size from 512 bytes to 64 GiB");
    }
    return ZFS_OK;
}

static zfs_status io(zfs_native fd, uint64_t offset, void* data, size_t size, bool writing, zfs_error* error)
{
    if (offset > INT64_MAX || lseek(fd, (off_t) offset, SEEK_SET) != (off_t) offset) {
        return zfs_error_set(error, ZFS_IO, "Device seek failed: %s", strerror(errno));
    }
    while (size != 0) {
        ssize_t n = writing ? write(fd, data, size) : read(fd, data, size);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return zfs_error_set(error,
                                 ZFS_IO,
                                 "Device %s failed: %s",
                                 writing ? "write" : "read",
                                 n != 0 ? strerror(errno) : "short I/O");
        }
        data = (unsigned char*) data + n;
        size -= (size_t) n;
    }
    return ZFS_OK;
}

zfs_status zfs_native_read(zfs_native fd, uint64_t offset, void* data, size_t size, zfs_error* error)
{
    return io(fd, offset, data, size, false, error);
}

zfs_status zfs_native_write(zfs_native fd, uint64_t offset, const void* data, size_t size, zfs_error* error)
{
    return io(fd, offset, (void*) data, size, true, error);
}

zfs_status zfs_native_sync(zfs_native fd, zfs_error* error)
{
    return fsync(fd) != 0 ? zfs_error_set(error, ZFS_IO, "Device flush failed: %s", strerror(errno)) : ZFS_OK;
}

void zfs_native_close(zfs_native fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

bool zfs_native_same(zfs_native first_device, zfs_native second_device)
{
    struct stat first_info;
    struct stat second_info;
    if (fstat(first_device, &first_info) != 0 || fstat(second_device, &second_info) != 0) {
        return false;
    }
    if ((S_ISBLK(first_info.st_mode) || S_ISCHR(first_info.st_mode)) &&
        (S_ISBLK(second_info.st_mode) || S_ISCHR(second_info.st_mode))) {
        return first_info.st_rdev == second_info.st_rdev &&
               (first_info.st_mode & S_IFMT) == (second_info.st_mode & S_IFMT);
    }
    return first_info.st_dev == second_info.st_dev && first_info.st_ino == second_info.st_ino;
}
#ifndef __APPLE__
static bool has_holders(const char* path)
{
    /* sysfs holders are device-mapper/RAID users that claim a block device,
     * even if the underlying disk does not appear directly in mountinfo. */
    char holders[ZFS_PATH_MAX];
    if (snprintf(holders, sizeof(holders), "%s/holders", path) >= (int) sizeof(holders)) {
        return true;
    }
    DIR* dir = opendir(holders);
    if (dir == NULL) {
        return true;
    }
    bool busy = false;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            busy = true;
            break;
        }
    }
    closedir(dir);
    return busy;
}
#endif
static zfs_status unmounted(zfs_native fd, const char* path, zfs_error* error)
{
#ifdef __APPLE__
    (void) fd;
    char resolved[ZFS_PATH_MAX];
    char block[ZFS_PATH_MAX];
    if (realpath(path, resolved) == NULL) {
        return zfs_error_set(error, ZFS_BUSY, "Cannot identify device mounts");
    }
    if (strncmp(resolved, "/dev/rdisk", 10) != 0 && strncmp(resolved, "/dev/disk", 9) != 0) {
        return zfs_error_set(error, ZFS_INVALID, "Select a whole /dev/rdiskN device");
    }
    const char* number = resolved + (resolved[5] == 'r' ? 10 : 9);
    if (isdigit((unsigned char) *number) == 0) {
        return zfs_error_set(error, ZFS_INVALID, "Select a whole /dev/rdiskN device");
    }
    while (isdigit((unsigned char) *number) != 0) {
        ++number;
    }
    if (*number != 0) {
        return zfs_error_set(error, ZFS_INVALID, "Select a whole /dev/rdiskN device, not a partition node");
    }
    snprintf(block, sizeof(block), "/dev/%s", resolved + (resolved[5] == 'r' ? 6 : 5));
    struct statfs* mounts = NULL;
    int n = getmntinfo(&mounts, MNT_NOWAIT);
    if (n <= 0) {
        return zfs_error_set(error, ZFS_BUSY, "Cannot inspect mounted filesystems");
    }
    size_t len = strlen(block);
    for (int i = 0; i < n; ++i) {
        const char* name = mounts[i].f_mntfromname;
        if (strncmp(name, block, len) == 0 && (name[len] == 0 || name[len] == 's')) {
            return zfs_error_set(error,
                                 ZFS_BUSY,
                                 "Device is mounted; unmount the entire disk first: diskutil unmountDisk %s",
                                 block);
        }
    }
#else
    struct stat st;
    char link[128];
    char device[ZFS_PATH_MAX];
    if (fstat(fd, &st)) {
        return zfs_error_set(error, ZFS_IO, "Cannot identify device");
    }
    /* Resolve major:minor to its sysfs topology path so partition descendants
     * can be matched to the selected whole disk. */
    snprintf(link, sizeof(link), "/sys/dev/block/%u:%u", major(st.st_rdev), minor(st.st_rdev));
    if (realpath(link, device) == NULL) {
        return zfs_error_set(error, ZFS_BUSY, "Cannot inspect device topology");
    }
    /* Whole disks only: a partition node must not bypass checks on siblings. */
    char partition[ZFS_PATH_MAX];
    if (snprintf(partition, sizeof(partition), "%s/partition", device) >= (int) sizeof(partition) ||
        access(partition, F_OK) == 0) {
        return zfs_error_set(error, ZFS_INVALID, "Select the whole disk, not a partition device");
    }
    /* Check mounted filesystems in this process's mount namespace by device
     * number, rather than depending on /dev aliases or mount-point names. */
    FILE* mounts = fopen("/proc/self/mountinfo", "r");
    if (mounts == NULL) {
        return zfs_error_set(error, ZFS_BUSY, "Cannot inspect mounted filesystems");
    }
    char* line = NULL;
    size_t capacity = 0;
    bool busy = false;
    size_t len = strlen(device);
    while (getline(&line, &capacity, mounts) >= 0) {
        unsigned maj;
        unsigned min;
        char mounted[ZFS_PATH_MAX];
        if (sscanf(line, "%*u %*u %u:%u", &maj, &min) != 2) {
            busy = true;
            break;
        }
        snprintf(link, sizeof(link), "/sys/dev/block/%u:%u", maj, min);
        if (realpath(link, mounted) != NULL && strncmp(mounted, device, len) == 0 &&
            (mounted[len] == 0 || mounted[len] == '/')) {
            busy = true;
            break;
        }
    }
    if (ferror(mounts)) {
        busy = true;
    }
    free(line);
    fclose(mounts);
    DIR* blocks = opendir("/sys/class/block");
    if (blocks == NULL) {
        busy = true;
    } else {
        struct dirent* entry;
        while ((entry = readdir(blocks))) {
            char item[ZFS_PATH_MAX];
            char resolved[ZFS_PATH_MAX];
            snprintf(item, sizeof(item), "/sys/class/block/%s", entry->d_name);
            if (realpath(item, resolved) != NULL && strncmp(resolved, device, len) == 0 &&
                (resolved[len] == 0 || resolved[len] == '/') && has_holders(resolved)) {
                busy = true;
                break;
            }
        }
        closedir(blocks);
    }
    if (busy) {
        return zfs_error_set(
            error,
            ZFS_BUSY,
            "Device is mounted or claimed; unmount all its partitions and close device mappings first: %s",
            path);
    }
#endif
    return ZFS_OK;
}

zfs_status zfs_native_guard(zfs_native* fd, const char* path, bool image, bool writing, zfs_error* error)
{
    if (!image) {
        zfs_status status = unmounted(*fd, path, error);
        if (status != ZFS_OK) {
            return status;
        }
#ifdef __linux__
        int exclusive = open(path, (writing ? O_RDWR : O_RDONLY) | O_EXCL);
        if (exclusive < 0) {
            return zfs_error_set(error,
                                 errno == EBUSY ? ZFS_BUSY : ZFS_PERMISSION,
                                 "Cannot exclusively open device; unmount it and check permissions: %s",
                                 path);
        }
        if (!zfs_native_same(*fd, exclusive) || flock(exclusive, LOCK_EX | LOCK_NB)) {
            close(exclusive);
            return zfs_error_set(error, ZFS_BUSY, "Device changed or is locked by another operation");
        }
        close(*fd);
        *fd = exclusive;
        return ZFS_OK;
#else
        /* Open writable once, retaining the same device identity until close. */
        if (writing) {
            int writable = open(path, O_RDWR);
            if (writable < 0) {
                return zfs_error_set(error, ZFS_PERMISSION, "Cannot open device for updating: %s", strerror(errno));
            }
            if (!zfs_native_same(*fd, writable) || flock(writable, LOCK_EX | LOCK_NB) != 0) {
                close(writable);
                return zfs_error_set(error, ZFS_BUSY, "Device changed or is locked");
            }
            close(*fd);
            *fd = writable;
            return ZFS_OK;
        }
#endif
    }
    return flock(*fd, LOCK_EX | LOCK_NB) != 0
               ? zfs_error_set(error, ZFS_BUSY, "Target or source is locked by another operation: %s", path)
               : ZFS_OK;
}
