/* SPDX-License-Identifier: Apache-2.0 */
/* Link-time native-backend replacement: no real device is ever opened. */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

static unsigned char media[1024 * 1024];
static unsigned writes;
static bool deny_write, fail_write, fail_flush;
static zfs_error error;
#define REQUIRE(value)                                                                                       \
    do {                                                                                                     \
        if (!(value)) {                                                                                      \
            fprintf(stderr, "Device test failed at line %d: %s (%s)\n", __LINE__, #value, error.message);    \
            exit(1);                                                                                         \
        }                                                                                                    \
    } while (0)
#define OK(value) REQUIRE((value) == ZFS_OK)
zfs_status zfs_native_open(const char *path, bool writing, zfs_native *fd, uint64_t *size, zfs_error *e) {
    (void)path;
    if (writing && deny_write)
        return zfs_error_set(e, ZFS_PERMISSION, "Simulated permission error");
    *fd = (zfs_native)(intptr_t)7;
    *size = sizeof(media);
    return ZFS_OK;
}
zfs_status zfs_native_read(zfs_native fd, uint64_t offset, void *data, size_t size, zfs_error *e) {
    (void)fd;
    if (offset > sizeof(media) || size > sizeof(media) - offset)
        return zfs_error_set(e, ZFS_IO, "Read out of bounds");
    memcpy(data, media + offset, size);
    return ZFS_OK;
}
zfs_status zfs_native_write(zfs_native fd, uint64_t offset, const void *data, size_t size, zfs_error *e) {
    (void)fd;
    if (fail_write && writes)
        return zfs_error_set(e, ZFS_IO, "Simulated removed media after first sector");
    if (offset > sizeof(media) || size > sizeof(media) - offset)
        return zfs_error_set(e, ZFS_IO, "Write out of bounds");
    memcpy(media + offset, data, size);
    ++writes;
    return ZFS_OK;
}
zfs_status zfs_native_sync(zfs_native fd, zfs_error *e) {
    (void)fd;
    return fail_flush ? zfs_error_set(e, ZFS_IO, "Simulated flush failure") : ZFS_OK;
}
void zfs_native_close(zfs_native fd) { (void)fd; }
static zfs_disk *staged(void) {
    memset(media, 0, sizeof(media));
    writes = 0;
    zfs_disk *d = NULL;
    OK(zfs_open("simulated-device", false, &d, &error));
    OK(zfs_mbr_create(d, &error));
    OK(zfs_partition_create(d, 8192, 512, NULL, &error));
    OK(zfs_mkdir(d, 0, "/staged", &error));
    REQUIRE(writes == 0 && media[510] == 0);
    return d;
}
int main(void) {
    zfs_disk *d = staged();
    OK(zfs_discard(d, &error));
    REQUIRE(writes == 0 && media[510] == 0);
    zfs_close(d);
    d = staged();
    deny_write = true;
    REQUIRE(zfs_apply(d, &error) == ZFS_PERMISSION);
    REQUIRE(zfs_get_info(d)->pending && !zfs_get_info(d)->failed && writes == 0);
    deny_write = false;
    OK(zfs_apply(d, &error));
    REQUIRE(writes > 1 && !zfs_get_info(d)->pending);
    zfs_close(d);
    d = staged();
    fail_write = true;
    REQUIRE(zfs_apply(d, &error) == ZFS_PARTIAL_WRITE);
    REQUIRE(writes == 1 && media[510] == 0x55 && zfs_get_info(d)->failed);
    REQUIRE(zfs_mkdir(d, 0, "/blocked", &error) == ZFS_PARTIAL_WRITE);
    REQUIRE(zfs_discard(d, &error) == ZFS_PARTIAL_WRITE);
    REQUIRE(zfs_apply(d, &error) == ZFS_PARTIAL_WRITE);
    zfs_close(d);
    fail_write = false;
    d = staged();
    fail_flush = true;
    REQUIRE(zfs_apply(d, &error) == ZFS_PARTIAL_WRITE);
    REQUIRE(zfs_get_info(d)->failed);
    zfs_close(d);
    puts("Simulated device staging, permission, partial-write and flush-failure tests passed");
    return 0;
}
