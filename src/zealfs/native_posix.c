/* SPDX-License-Identifier: Apache-2.0 */
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
zfs_status zfs_native_open(const char *path, bool write_access, zfs_native *fd, uint64_t *size,
                           zfs_error *e) {
    *fd = open(path, write_access ? O_RDWR : O_RDONLY);
    if (*fd < 0)
        return zfs_error_set(e, errno == EACCES || errno == EPERM ? ZFS_PERMISSION : ZFS_IO,
                             "Cannot open device %s: %s", path, strerror(errno));
    struct stat st;
    unsigned block = 0;
    bool ok = fstat(*fd, &st) == 0;
#ifdef __APPLE__
    uint64_t count = 0;
    ok = ok && S_ISCHR(st.st_mode) && ioctl(*fd, DKIOCGETBLOCKSIZE, &block) == 0 &&
         ioctl(*fd, DKIOCGETBLOCKCOUNT, &count) == 0;
    *size = count * block;
#else
    ok = ok && S_ISBLK(st.st_mode) && ioctl(*fd, (int)BLKGETSIZE64, size) == 0 &&
         ioctl(*fd, BLKSSZGET, &block) == 0;
#endif
    if (!ok || block != 512 || *size < 512 || *size > ZFS_MAX_DISK_SIZE) {
        close(*fd);
        *fd = ZFS_NATIVE_INVALID;
        return zfs_error_set(e, ZFS_INVALID,
                             "Device must have 512-byte sectors and a size from 512 bytes to 64 GiB");
    }
    return ZFS_OK;
}
static zfs_status io(zfs_native fd, uint64_t offset, void *data, size_t size, bool writing, zfs_error *e) {
    if (offset > INT64_MAX || lseek(fd, (off_t)offset, SEEK_SET) != (off_t)offset)
        return zfs_error_set(e, ZFS_IO, "Device seek failed: %s", strerror(errno));
    while (size) {
        ssize_t n = writing ? write(fd, data, size) : read(fd, data, size);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return zfs_error_set(e, ZFS_IO, "Device %s failed: %s", writing ? "write" : "read",
                                 n ? strerror(errno) : "short I/O");
        data = (unsigned char *)data + n;
        size -= (size_t)n;
    }
    return ZFS_OK;
}
zfs_status zfs_native_read(zfs_native fd, uint64_t offset, void *data, size_t size, zfs_error *e) {
    return io(fd, offset, data, size, false, e);
}
zfs_status zfs_native_write(zfs_native fd, uint64_t offset, const void *data, size_t size, zfs_error *e) {
    return io(fd, offset, (void *)data, size, true, e);
}
zfs_status zfs_native_sync(zfs_native fd, zfs_error *e) {
    return fsync(fd) ? zfs_error_set(e, ZFS_IO, "Device flush failed: %s", strerror(errno)) : ZFS_OK;
}
void zfs_native_close(zfs_native fd) {
    if (fd >= 0)
        close(fd);
}
