/* SPDX-License-Identifier: Apache-2.0 */
#include "zealfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#define access _access
#define rmdir _rmdir
#else
#include <unistd.h>
#include <sys/stat.h>
#endif
static zfs_error error;
#define REQUIRE(x)                                                                                           \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error.message);                 \
            exit(1);                                                                                         \
        }                                                                                                    \
    } while (0)
#define OK(x) REQUIRE((x) == ZFS_OK)
static uint64_t hash(const char *path) {
    FILE *f = fopen(path, "rb");
    REQUIRE(f);
    uint64_t h = 1469598103934665603ULL;
    int c;
    while ((c = fgetc(f)) != EOF) {
        h ^= (unsigned)c;
        h *= 1099511628211ULL;
    }
    fclose(f);
    return h;
}
static void empty(zfs_disk *d, int part) {
    zfs_entry *entries;
    size_t n;
    OK(zfs_list(d, part, "/", &entries, &n, &error));
    REQUIRE(n == 0);
    zfs_entries_free(entries);
}
int main(void) {
    char directory[512];
#ifdef _WIN32
    char root[MAX_PATH];
    REQUIRE(GetTempPathA(sizeof(root), root));
    snprintf(directory, sizeof(directory), "%szealfs-test-%lu", root, GetCurrentProcessId());
    REQUIRE(_mkdir(directory) == 0);
#else
    snprintf(directory, sizeof(directory), "/tmp/zealfs-test-XXXXXX");
    REQUIRE(mkdtemp(directory));
#endif
    char image[600], raw[600], exported[600], source[600];
    snprintf(image, sizeof(image), "%s/disk.img", directory);
    snprintf(raw, sizeof(raw), "%s/raw.img", directory);
    snprintf(exported, sizeof(exported), "%s/export.bin", directory);
    snprintf(source, sizeof(source), "%s/source.bin", directory);
    zfs_disk *d = NULL;
    int part = -1;
    OK(zfs_image_create(image, 2 * 1024 * 1024, true, &d, &error));
    REQUIRE(access(image, 0) != 0);
    REQUIRE(zfs_get_info(d)->pending);
    OK(zfs_partition_create(d, 1024 * 1024, 1024 * 1024, &part, &error));
    REQUIRE(part == 0);
    empty(d, 0);
    OK(zfs_mkdir(d, 0, "/folder", &error));
    unsigned char payload[70001];
    for (size_t i = 0; i < sizeof(payload); ++i)
        payload[i] = (unsigned char)(i * 37);
    OK(zfs_file_write(d, 0, "/folder/test.bin", payload, sizeof(payload), &error));
    OK(zfs_file_write(d, 0, "/empty", NULL, 0, &error));
    unsigned char actual[70001];
    size_t n = 0;
    OK(zfs_file_read(d, 0, "/folder/test.bin", 0, actual, sizeof(actual), &n, &error));
    REQUIRE(n == sizeof(payload) && !memcmp(payload, actual, n));
    OK(zfs_file_read(d, 0, "/folder/test.bin", UINT64_MAX, actual, 1, &n, &error));
    REQUIRE(n == 0);
    char unpublished_alias[700];
    snprintf(unpublished_alias, sizeof(unpublished_alias), "%s/./disk.img", directory);
    REQUIRE(zfs_export(d, 0, "/folder/test.bin", unpublished_alias, true, &error) == ZFS_INVALID);
    OK(zfs_export(d, 0, "/folder/test.bin", exported, false, &error));
    REQUIRE(access(image, 0) != 0);
    REQUIRE(zfs_export(d, 0, "/folder/test.bin", exported, false, &error) == ZFS_EXISTS);
    FILE *f = fopen(source, "wb");
    REQUIRE(f);
    REQUIRE(fwrite(payload, 1, sizeof(payload), f) == sizeof(payload));
    fclose(f);
    REQUIRE(hash(source) == hash(exported));
    REQUIRE(zfs_file_write(d, 0, "/folder/test.bin", payload, 1, &error) == ZFS_EXISTS);
    REQUIRE(zfs_mkdir(d, 0, "/missing/child", &error) == ZFS_NOT_FOUND);
    REQUIRE(zfs_remove(d, 0, "/folder", true, &error) != ZFS_OK);
    REQUIRE(zfs_mkdir(d, 0, "/12345678901234567", &error) == ZFS_INVALID);
    REQUIRE(zfs_partition_create(d, 1, 512, NULL, &error) != ZFS_OK);
    OK(zfs_apply(d, &error));
    REQUIRE(access(image, 0) == 0);
    REQUIRE(!zfs_get_info(d)->pending);
    uint64_t original = hash(image);
    OK(zfs_remove(d, 0, "/folder/test.bin", false, &error));
    OK(zfs_remove(d, 0, "/folder", true, &error));
    REQUIRE(hash(image) == original);
    OK(zfs_discard(d, &error));
    REQUIRE(hash(image) == original);
    OK(zfs_file_read(d, 0, "/folder/test.bin", 0, actual, sizeof(actual), &n, &error));
    REQUIRE(n == sizeof(payload));
    OK(zfs_partition_format(d, 0, &error));
    empty(d, 0);
    REQUIRE(hash(image) == original);
    OK(zfs_discard(d, &error));
    OK(zfs_import(d, 0, source, "/import.bin", &error));
    REQUIRE(hash(image) == original);
    OK(zfs_apply(d, &error));
    zfs_close(d);
    d = NULL;
    OK(zfs_open(image, true, &d, &error));
    OK(zfs_file_read(d, 0, "/import.bin", 0, actual, sizeof(actual), &n, &error));
    REQUIRE(n == sizeof(payload) && !memcmp(actual, payload, n));
    original = hash(image);
    OK(zfs_partition_delete(d, 0, &error));
    REQUIRE(!zfs_get_info(d)->partitions[0].active);
    REQUIRE(hash(image) == original);
    OK(zfs_discard(d, &error));
    /* A failed large import must leave neither an entry nor partially allocated pages. */
    uint64_t free_before, total, free_after;
    OK(zfs_usage(d, 0, &free_before, &total, &error));
    unsigned char *large = calloc(1, 1024 * 1024);
    REQUIRE(large);
    REQUIRE(zfs_file_write(d, 0, "/too-big", large, 1024 * 1024, &error) == ZFS_NO_SPACE);
    free(large);
    OK(zfs_usage(d, 0, &free_after, &total, &error));
    REQUIRE(free_before == free_after);
    REQUIRE(!zfs_get_info(d)->pending);
    /* Independent handles do not share staged state. */
    zfs_disk *other = NULL;
    OK(zfs_open(image, true, &other, &error));
    OK(zfs_mkdir(d, 0, "/private", &error));
    zfs_entry *entries;
    size_t count;
    OK(zfs_list(other, 0, "/", &entries, &count, &error));
    bool seen = false;
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(entries[i].name, "private"))
            seen = true;
    REQUIRE(!seen);
    zfs_entries_free(entries);
    zfs_close(other);
    zfs_close(d);
    /* Exports cannot bypass Apply by replacing the currently open target. */
    OK(zfs_open(image, true, &d, &error));
    REQUIRE(zfs_export(d, 0, "/import.bin", image, true, &error) == ZFS_INVALID);
#ifndef _WIN32
    char alias[600];
    snprintf(alias, sizeof(alias), "%s/alias.img", directory);
    REQUIRE(link(image, alias) == 0);
    REQUIRE(zfs_export(d, 0, "/import.bin", alias, true, &error) == ZFS_INVALID);
    remove(alias);
    REQUIRE(chmod(image, 0640) == 0);
#endif
    OK(zfs_mkdir(d, 0, "/permissions", &error));
    OK(zfs_apply(d, &error));
#ifndef _WIN32
    struct stat mode;
    REQUIRE(stat(image, &mode) == 0);
    REQUIRE((mode.st_mode & 0777) == 0640);
#endif
    zfs_close(d);
    /* One session cannot overwrite a newer commit from another session. */
    OK(zfs_open(image, true, &d, &error));
    OK(zfs_mkdir(d, 0, "/conflict", &error));
    f = fopen(image, "r+b");
    REQUIRE(f);
    REQUIRE(fseek(f, 100, SEEK_SET) == 0);
    REQUIRE(fputc(77, f) == 77);
    REQUIRE(fclose(f) == 0);
    original = hash(image);
    REQUIRE(zfs_apply(d, &error) == ZFS_BUSY);
    REQUIRE(hash(image) == original);
    REQUIRE(zfs_get_info(d)->pending);
    zfs_close(d);
    /* Apply failure retains pending data and never publishes a partial image. */
    char missing[600];
    snprintf(missing, sizeof(missing), "%s/missing/new.img", directory);
    OK(zfs_image_create(missing, 8192, true, &d, &error));
    REQUIRE(zfs_apply(d, &error) != ZFS_OK);
    REQUIRE(access(missing, 0) != 0);
    REQUIRE(zfs_get_info(d)->pending);
    OK(zfs_discard(d, &error));
    REQUIRE(!zfs_get_info(d)->pending);
    zfs_close(d);
    /* 4-GiB filesystem counters and disk offsets beyond 4 GiB work on ARM32 too.
       The image is never published; staging is sparse. */
    OK(zfs_image_create(raw, UINT64_C(8) * 1024 * 1024 * 1024, true, &d, &error));
    OK(zfs_partition_create(d, UINT64_C(4) * 1024 * 1024 * 1024, 512, &part, &error));
    OK(zfs_usage(d, part, &free_before, &total, &error));
    REQUIRE(total == UINT64_C(4) * 1024 * 1024 * 1024);
    OK(zfs_partition_create(d, 8192, 512, &part, &error));
    REQUIRE(part == 1);
    REQUIRE((uint64_t)zfs_get_info(d)->partitions[part].start_lba * 512 > UINT32_MAX);
    OK(zfs_file_write(d, part, "/beyond4g", payload, 512, &error));
    OK(zfs_file_read(d, part, "/beyond4g", 0, actual, 512, &n, &error));
    REQUIRE(n == 512 && !memcmp(payload, actual, 512));
    zfs_close(d);
    REQUIRE(access(raw, 0) != 0);
    /* Raw images, smallest supported partition, root extension, and 16-byte names. */
    OK(zfs_image_create(raw, 8192, false, &d, &error));
    OK(zfs_partition_create(d, 0, 512, &part, &error));
    for (int i = 0; i < 10; ++i) {
        char path[32];
        snprintf(path, sizeof(path), "/entry%02d", i);
        OK(zfs_file_write(d, 0, path, NULL, 0, &error));
    }
    OK(zfs_file_write(d, 0, "/1234567890123456", NULL, 0, &error));
    OK(zfs_list(d, 0, "/", &entries, &count, &error));
    REQUIRE(count == 11);
    zfs_entries_free(entries);
    OK(zfs_apply(d, &error));
    zfs_close(d);
    OK(zfs_open(raw, true, &d, &error));
    OK(zfs_list(d, 0, "/", &entries, &count, &error));
    REQUIRE(count == 11);
    zfs_entries_free(entries);
    original = hash(raw);
    OK(zfs_partition_delete(d, 0, &error));
    OK(zfs_mbr_create(d, &error));
    REQUIRE(hash(raw) == original);
    OK(zfs_discard(d, &error));
    REQUIRE(!zfs_get_info(d)->has_mbr);
    zfs_close(d);
    /* An unpublished image is removed by close/discard and cannot overwrite an existing image. */
    REQUIRE(zfs_image_create(image, 8192, false, &d, &error) == ZFS_EXISTS);
    remove(raw);
    OK(zfs_image_create(raw, 8192, true, &d, &error));
    zfs_close(d);
    REQUIRE(access(raw, 0) != 0);
    /* Invalid header never asserts or reads outside a partition. */
    f = fopen(raw, "wb");
    REQUIRE(f);
    unsigned char bad[8192] = {0};
    bad[0] = 'Z';
    bad[1] = 2;
    bad[6] = 255;
    REQUIRE(fwrite(bad, 1, sizeof(bad), f) == sizeof(bad));
    fclose(f);
    OK(zfs_open(raw, true, &d, &error));
    REQUIRE(zfs_list(d, 0, "/", &entries, &count, &error) == ZFS_CORRUPT);
    zfs_close(d);
    remove(image);
    remove(raw);
    remove(source);
    remove(exported);
    REQUIRE(rmdir(directory) == 0);
    puts("Core staging, persistence, transfers, rollback, bounds and raw-image tests passed");
    return 0;
}
