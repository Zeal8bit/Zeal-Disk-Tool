/**
 * SPDX-License-Identifier: Apache-2.0
 */
/* Memory-only devices; the native layer is replaced at link time. */
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define rmdir _rmdir
#else
#include <unistd.h>
#endif
static unsigned char media[131072], source_media[131072], original[131072], source_before[131072];
static bool mounted, target_mounted, deny, corrupt, flush_failure, fail_after_sector;
static unsigned writes;
static zfs_error error;
#define REQUIRE(x)                                                                                                     \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "FAIL %d: %s (%s)\n", __LINE__, #x, error.message);                                        \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
#define OK(x) REQUIRE((x) == ZFS_OK)

zfs_status zfs_native_open(const char* path, bool writing, zfs_native* fd, uint64_t* size, zfs_error* e)
{
    if (writing && deny) {
        return zfs_error_set(e, ZFS_PERMISSION, "Permission denied");
    }
    *fd = (zfs_native) (intptr_t) (!strcmp(path, "source") ? 8 : 7);
    *size = sizeof(media);
    return ZFS_OK;
}

static unsigned char* bytes(zfs_native fd)
{
    return (intptr_t) fd == 8 ? source_media : media;
}

zfs_status zfs_native_read(zfs_native fd, uint64_t offset, void* buffer, size_t size, zfs_error* e)
{
    if (offset > sizeof(media) || size > sizeof(media) - offset) {
        return zfs_error_set(e, ZFS_IO, "Read bounds");
    }
    memcpy(buffer, bytes(fd) + offset, size);
    return ZFS_OK;
}

zfs_status zfs_native_write(zfs_native fd, uint64_t offset, const void* buffer, size_t size, zfs_error* e)
{
    REQUIRE((intptr_t) fd == 7);
    if (fail_after_sector && writes) {
        return zfs_error_set(e, ZFS_IO, "Media removed");
    }
    if (offset > sizeof(media) || size > sizeof(media) - offset) {
        return zfs_error_set(e, ZFS_IO, "Write bounds");
    }
    memcpy(media + offset, buffer, size);
    if (corrupt) {
        media[offset] ^= 1;
    }
    ++writes;
    return ZFS_OK;
}

zfs_status zfs_native_sync(zfs_native fd, zfs_error* e)
{
    (void) fd;
    return flush_failure ? zfs_error_set(e, ZFS_IO, "Flush failed") : ZFS_OK;
}

void zfs_native_close(zfs_native fd)
{
    (void) fd;
}

bool zfs_native_same(zfs_native a, zfs_native b)
{
    return a == b;
}

zfs_status zfs_native_guard(zfs_native* fd, const char* path, bool image, bool writing, zfs_error* e)
{
    (void) fd;
    (void) path;
    if (!image && (mounted || (target_mounted && writing))) {
        return zfs_error_set(e, ZFS_BUSY, "Device is mounted; unmount it");
    }
    if (!image && writing && deny) {
        return zfs_error_set(e, ZFS_PERMISSION, "Permission denied");
    }
    return ZFS_OK;
}

static zfs_disk* open_target(void)
{
    zfs_disk* d = NULL;
    OK(zfs_open("target", false, &d, &error));
    return d;
}

int main(void)
{
    char dir[512];
#ifdef _WIN32
    char root[MAX_PATH];
    REQUIRE(GetTempPathA(sizeof(root), root));
    snprintf(dir, sizeof(dir), "%szdt-merge-device-%lu", root, GetCurrentProcessId());
    REQUIRE(!_mkdir(dir));
#else
    snprintf(dir, sizeof(dir), "/tmp/zdt-merge-device-XXXXXX");
    REQUIRE(mkdtemp(dir));
#endif
    char backup[600];
    snprintf(backup, sizeof(backup), "%s/before.img", dir);
    zfs_disk* d = open_target();
    OK(zfs_mbr_create(d, &error));
    OK(zfs_partition_create(d, 65536, 512, NULL, &error));
    OK(zfs_file_write(d, 0, "/app", "old version", 11, &error));
    OK(zfs_file_write(d, 0, "/personal", "mine", 4, &error));
    OK(zfs_apply(d, &error));
    zfs_close(d);
    memcpy(original, media, sizeof(media));
    memcpy(source_media, media, sizeof(media));
    /* Build a source without writing to the fake source device. */
    d = open_target();
    OK(zfs_remove(d, 0, "/app", false, &error));
    OK(zfs_file_write(d, 0, "/app", "new version", 11, &error));
    OK(zfs_apply(d, &error));
    zfs_close(d);
    memcpy(source_media, media, sizeof(media));
    memcpy(source_before, source_media, sizeof(source_media));
    memcpy(media, original, sizeof(media));
    writes = 0;
    zfs_disk* source = NULL;
    OK(zfs_open("source", false, &source, &error));
    d = open_target();
    zfs_merge_report report = {0};
    zfs_merge_plan* plan = NULL;
    mounted = true;
    REQUIRE(zfs_merge_prepare(source, 0, d, 0, &plan, &report, &error) == ZFS_BUSY);
    REQUIRE(!plan && !writes);
    zfs_merge_report_free(&report);
    mounted = false;
    target_mounted = true;
    REQUIRE(zfs_merge_prepare(source, 0, d, 0, &plan, &report, &error) == ZFS_BUSY);
    REQUIRE(!plan && !writes);
    zfs_merge_report_free(&report);
    target_mounted = false;
    deny = true;
    REQUIRE(zfs_merge_prepare(source, 0, d, 0, &plan, &report, &error) == ZFS_PERMISSION);
    REQUIRE(!plan && !writes);
    zfs_merge_report_free(&report);
    deny = false;
    OK(zfs_merge_prepare(source, 0, d, 0, &plan, &report, &error));
    REQUIRE(!writes && !memcmp(media, original, sizeof(media)));
    REQUIRE(report.files_replaced == 1 && report.files_unchanged == 1);
    /* A prepared plan cannot overwrite later edits to its target session. */
    OK(zfs_mkdir(d, 0, "/later", &error));
    REQUIRE(zfs_merge_stage(plan, backup, &error) == ZFS_BUSY);
    zfs_merge_plan_free(plan);
    zfs_merge_report_free(&report);
    OK(zfs_discard(d, &error));
    OK(zfs_merge_prepare(source, 0, d, 0, &plan, &report, &error));
    OK(zfs_merge_stage(plan, backup, &error));
    REQUIRE(report.backup_created && !writes && !memcmp(media, original, sizeof(media)));
    corrupt = true;
    REQUIRE(zfs_apply(d, &error) == ZFS_PARTIAL_WRITE);
    REQUIRE(zfs_get_info(d)->failed);
    REQUIRE(zfs_recovery_backup(d));
    corrupt = false;
    REQUIRE(zfs_discard(d, &error) == ZFS_PARTIAL_WRITE);
    zfs_merge_plan_free(plan);
    zfs_merge_report_free(&report);
    zfs_close(d);
    zfs_close(source);
    /* A fresh session can restore damaged filesystem metadata from the backup. */
    d = open_target();
    OK(zfs_restore_prepare(backup, d, 0, &plan, &report, &error));
    OK(zfs_restore_stage(plan, &error));
    OK(zfs_apply(d, &error));
    REQUIRE(!memcmp(media, original, sizeof(media)));
    zfs_merge_plan_free(plan);
    zfs_merge_report_free(&report);
    zfs_close(d);
    for (int test = 0; test < 3; ++test) {
        remove(backup);
        writes = 0;
        d = open_target();
        OK(zfs_open("source", false, &source, &error));
        OK(zfs_merge_prepare(source, 0, d, 0, &plan, &report, &error));
        OK(zfs_merge_stage(plan, backup, &error));
        if (test == 0) {
            fail_after_sector = true;
        } else if (test == 1) {
            flush_failure = true;
        } else {
            media[1024] ^= 1;
        }
        zfs_status status = zfs_apply(d, &error);
        if (test < 2) {
            REQUIRE(status == ZFS_PARTIAL_WRITE && zfs_get_info(d)->failed);
        } else {
            REQUIRE(status == ZFS_BUSY && writes == 0 && !zfs_get_info(d)->failed);
        }
        fail_after_sector = flush_failure = false;
        zfs_merge_plan_free(plan);
        zfs_merge_report_free(&report);
        zfs_close(source);
        zfs_close(d);
        d = open_target();
        OK(zfs_restore_prepare(backup, d, 0, &plan, &report, &error));
        OK(zfs_restore_stage(plan, &error));
        OK(zfs_apply(d, &error));
        REQUIRE(!memcmp(media, original, sizeof(media)));
        zfs_merge_plan_free(plan);
        zfs_merge_report_free(&report);
        zfs_close(d);
    }
    /* Explicitly unbacked merges still verify physical writes and stage first. */
    writes = 0;
    d = open_target();
    OK(zfs_open("source", false, &source, &error));
    OK(zfs_merge_prepare(source, 0, d, 0, &plan, &report, &error));
    OK(zfs_merge_stage(plan, NULL, &error));
    REQUIRE(!report.backup_created && !zfs_recovery_backup(d) && !writes);
    OK(zfs_apply(d, &error));
    REQUIRE(writes && !memcmp(media, source_media, sizeof(media)));
    zfs_merge_plan_free(plan);
    zfs_merge_report_free(&report);
    zfs_close(source);
    zfs_close(d);
    REQUIRE(!memcmp(source_media, source_before, sizeof(source_media)));
    remove(backup);
    REQUIRE(!rmdir(dir));
    puts("Merge device protection, stale plans, readback/flush/write failures and restore passed");
    return 0;
}
