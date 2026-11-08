/**
 * SPDX-License-Identifier: Apache-2.0
 */
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#ifdef __APPLE__
#include <sys/disk.h>
#else
#include <linux/fs.h>
#endif
zfs_status zfs_native_open(const char* path, bool write_access, zfs_native* fd, uint64_t* size, zfs_error* e)
{
    *fd = open(path, write_access ? O_RDWR : O_RDONLY);
    if (*fd < 0) {
        return zfs_error_set(e, errno == EACCES || errno == EPERM ? ZFS_PERMISSION : ZFS_IO,
                             "Cannot open device %s: %s", path, strerror(errno));
    }
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
    if (!ok || block != 512 || *size < 512 || *size > ZFS_MAX_DISK_SIZE) {
        close(*fd);
        *fd = ZFS_NATIVE_INVALID;
        return zfs_error_set(e, ZFS_INVALID, "Device must have 512-byte sectors and a size from 512 bytes to 64 GiB");
    }
    return ZFS_OK;
}

static zfs_status io(zfs_native fd, uint64_t offset, void* data, size_t size, bool writing, zfs_error* e)
{
    if (offset > INT64_MAX || lseek(fd, (off_t) offset, SEEK_SET) != (off_t) offset) {
        return zfs_error_set(e, ZFS_IO, "Device seek failed: %s", strerror(errno));
    }
    while (size) {
        ssize_t n = writing ? write(fd, data, size) : read(fd, data, size);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return zfs_error_set(e, ZFS_IO, "Device %s failed: %s", writing ? "write" : "read",
                                 n ? strerror(errno) : "short I/O");
        }
        data = (unsigned char*) data + n;
        size -= (size_t) n;
    }
    return ZFS_OK;
}

zfs_status zfs_native_read(zfs_native fd, uint64_t offset, void* data, size_t size, zfs_error* e)
{
    return io(fd, offset, data, size, false, e);
}

zfs_status zfs_native_write(zfs_native fd, uint64_t offset, const void* data, size_t size, zfs_error* e)
{
    return io(fd, offset, (void*) data, size, true, e);
}

zfs_status zfs_native_sync(zfs_native fd, zfs_error* e)
{
    return fsync(fd) ? zfs_error_set(e, ZFS_IO, "Device flush failed: %s", strerror(errno)) : ZFS_OK;
}

void zfs_native_close(zfs_native fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

#include <sys/file.h>
#include <ctype.h>
#include <stdlib.h>
#include <dirent.h>
#ifdef __APPLE__
#include <sys/mount.h>
#else
#include <sys/sysmacros.h>
#endif
bool zfs_native_same(zfs_native a, zfs_native b)
{
    struct stat x, y;
    if (fstat(a, &x) || fstat(b, &y)) {
        return false;
    }
    if ((S_ISBLK(x.st_mode) || S_ISCHR(x.st_mode)) && (S_ISBLK(y.st_mode) || S_ISCHR(y.st_mode))) {
        return x.st_rdev == y.st_rdev && (x.st_mode & S_IFMT) == (y.st_mode & S_IFMT);
    }
    return x.st_dev == y.st_dev && x.st_ino == y.st_ino;
}
#ifndef __APPLE__
static bool has_holders(const char* path)
{
    char holders[ZFS_PATH_MAX];
    if (snprintf(holders, sizeof(holders), "%s/holders", path) >= (int) sizeof(holders)) {
        return true;
    }
    DIR* dir = opendir(holders);
    if (!dir) {
        return true;
    }
    bool busy = false;
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
            busy = true;
            break;
        }
    }
    closedir(dir);
    return busy;
}
#endif
static zfs_status unmounted(zfs_native fd, const char* path, zfs_error* e)
{
#ifdef __APPLE__
    (void) fd;
    char resolved[ZFS_PATH_MAX], block[ZFS_PATH_MAX];
    if (!realpath(path, resolved)) {
        return zfs_error_set(e, ZFS_BUSY, "Cannot identify device mounts");
    }
    if (strncmp(resolved, "/dev/rdisk", 10) && strncmp(resolved, "/dev/disk", 9)) {
        return zfs_error_set(e, ZFS_INVALID, "Select a whole /dev/rdiskN device");
    }
    const char* number = resolved + (resolved[5] == 'r' ? 10 : 9);
    if (!isdigit((unsigned char) *number)) {
        return zfs_error_set(e, ZFS_INVALID, "Select a whole /dev/rdiskN device");
    }
    while (isdigit((unsigned char) *number)) {
        ++number;
    }
    if (*number) {
        return zfs_error_set(e, ZFS_INVALID, "Select a whole /dev/rdiskN device, not a partition node");
    }
    snprintf(block, sizeof(block), "/dev/%s", resolved + (resolved[5] == 'r' ? 6 : 5));
    struct statfs* mounts = NULL;
    int n = getmntinfo(&mounts, MNT_NOWAIT);
    if (n <= 0) {
        return zfs_error_set(e, ZFS_BUSY, "Cannot inspect mounted filesystems");
    }
    size_t len = strlen(block);
    for (int i = 0; i < n; ++i) {
        const char* name = mounts[i].f_mntfromname;
        if (!strncmp(name, block, len) && (!name[len] || name[len] == 's')) {
            return zfs_error_set(e, ZFS_BUSY,
                                 "Device is mounted; unmount the entire disk first: diskutil unmountDisk %s", block);
        }
    }
#else
    struct stat st;
    char link[128], device[ZFS_PATH_MAX];
    if (fstat(fd, &st)) {
        return zfs_error_set(e, ZFS_IO, "Cannot identify device");
    }
    snprintf(link, sizeof(link), "/sys/dev/block/%u:%u", major(st.st_rdev), minor(st.st_rdev));
    if (!realpath(link, device)) {
        return zfs_error_set(e, ZFS_BUSY, "Cannot inspect device topology");
    }
    /* Whole disks only: a partition node must not bypass checks on siblings. */
    char partition[ZFS_PATH_MAX];
    if (snprintf(partition, sizeof(partition), "%s/partition", device) >= (int) sizeof(partition) ||
        access(partition, F_OK) == 0) {
        return zfs_error_set(e, ZFS_INVALID, "Select the whole disk, not a partition device");
    }
    FILE* mounts = fopen("/proc/self/mountinfo", "r");
    if (!mounts) {
        return zfs_error_set(e, ZFS_BUSY, "Cannot inspect mounted filesystems");
    }
    char* line = NULL;
    size_t capacity = 0;
    bool busy = false;
    size_t len = strlen(device);
    while (getline(&line, &capacity, mounts) >= 0) {
        unsigned maj, min;
        char mounted[ZFS_PATH_MAX];
        if (sscanf(line, "%*u %*u %u:%u", &maj, &min) != 2) {
            busy = true;
            break;
        }
        snprintf(link, sizeof(link), "/sys/dev/block/%u:%u", maj, min);
        if (realpath(link, mounted) && !strncmp(mounted, device, len) && (!mounted[len] || mounted[len] == '/')) {
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
    if (!blocks) {
        busy = true;
    } else {
        struct dirent* entry;
        while ((entry = readdir(blocks))) {
            char item[ZFS_PATH_MAX], resolved[ZFS_PATH_MAX];
            snprintf(item, sizeof(item), "/sys/class/block/%s", entry->d_name);
            if (realpath(item, resolved) && !strncmp(resolved, device, len) &&
                (!resolved[len] || resolved[len] == '/') && has_holders(resolved)) {
                busy = true;
                break;
            }
        }
        closedir(blocks);
    }
    if (busy) {
        return zfs_error_set(
            e, ZFS_BUSY, "Device is mounted or claimed; unmount all its partitions and close device mappings first: %s",
            path);
    }
#endif
    return ZFS_OK;
}

zfs_status zfs_native_guard(zfs_native* fd, const char* path, bool image, bool writing, zfs_error* e)
{
    if (!image) {
        zfs_status s = unmounted(*fd, path, e);
        if (s) {
            return s;
        }
#ifdef __linux__
        int exclusive = open(path, (writing ? O_RDWR : O_RDONLY) | O_EXCL);
        if (exclusive < 0) {
            return zfs_error_set(e, errno == EBUSY ? ZFS_BUSY : ZFS_PERMISSION,
                                 "Cannot exclusively open device; unmount it and check permissions: %s", path);
        }
        if (!zfs_native_same(*fd, exclusive) || flock(exclusive, LOCK_EX | LOCK_NB)) {
            close(exclusive);
            return zfs_error_set(e, ZFS_BUSY, "Device changed or is locked by another operation");
        }
        close(*fd);
        *fd = exclusive;
        return ZFS_OK;
#else
        /* Open writable once, retaining the same device identity until close. */
        if (writing) {
            int writable = open(path, O_RDWR);
            if (writable < 0) {
                return zfs_error_set(e, ZFS_PERMISSION, "Cannot open device for updating: %s", strerror(errno));
            }
            if (!zfs_native_same(*fd, writable) || flock(writable, LOCK_EX | LOCK_NB)) {
                close(writable);
                return zfs_error_set(e, ZFS_BUSY, "Device changed or is locked");
            }
            close(*fd);
            *fd = writable;
            return ZFS_OK;
        }
#endif
    }
    return flock(*fd, LOCK_EX | LOCK_NB)
               ? zfs_error_set(e, ZFS_BUSY, "Target or source is locked by another operation: %s", path)
               : ZFS_OK;
}
