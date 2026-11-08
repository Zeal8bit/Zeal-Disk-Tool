/**
 * SPDX-License-Identifier: Apache-2.0
 */
#include "disk_private.h"
#include "zealfs_v2.h"
#include <errno.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define SECTOR 512u
#define MAX_PART_SIZE (UINT64_C(4) * 1024 * 1024 * 1024)

typedef struct {
    uint64_t sector;
    bool dirty;
    unsigned char data[SECTOR];
} undo_record;

typedef struct {
    zfs_disk* disk;
    zfs_partition part;
    zealfs_context_t fs;
} filesystem;

const char* zfs_status_string(zfs_status s)
{
    static const char* names[] = {"success",
                                  "invalid argument",
                                  "I/O error",
                                  "permission denied",
                                  "not found",
                                  "already exists",
                                  "no space",
                                  "out of memory",
                                  "busy",
                                  "corrupt filesystem",
                                  "partial device write",
                                  "type conflict"};
    return (unsigned) s < sizeof(names) / sizeof(*names) ? names[s] : "unknown error";
}

zfs_status zfs_error_set(zfs_error* e, zfs_status code, const char* format, ...)
{
    if (e) {
        e->code = code;
        va_list args;
        va_start(args, format);
        vsnprintf(e->message, sizeof(e->message), format, args);
        va_end(args);
    }
    return code;
}

static zfs_status sys_error(zfs_error* e, const char* operation)
{
    int n = errno;
    zfs_status s = n == EACCES || n == EPERM ? ZFS_PERMISSION
                   : n == ENOENT             ? ZFS_NOT_FOUND
                   : n == ENOSPC             ? ZFS_NO_SPACE
                   : n == ENOMEM             ? ZFS_NO_MEMORY
                                             : ZFS_IO;
    return zfs_error_set(e, s, "%s: %s", operation, strerror(n));
}

static bool dirty(zfs_disk* d, uint64_t sector)
{
    return (d->dirty[sector / 8] & (1u << (sector % 8))) != 0;
}

static void set_dirty(zfs_disk* d, uint64_t sector, bool value)
{
    bool old = dirty(d, sector);
    if (value) {
        d->dirty[sector / 8] |= (unsigned char) (1u << (sector % 8));
    } else {
        d->dirty[sector / 8] &= (unsigned char) ~(1u << (sector % 8));
    }
    if (old != value) {
        if (value) {
            ++d->dirty_count;
        } else {
            --d->dirty_count;
        }
    }
}

static zfs_status read_sector(zfs_disk* d, uint64_t sector, void* buffer, zfs_error* e)
{
    if (sector >= d->info.size / SECTOR) {
        return zfs_error_set(e, ZFS_INVALID, "Read outside disk");
    }
    FILE* f = dirty(d, sector) ? d->overlay : d->base;
    if (f) {
        if (zfs_seek(f, sector * SECTOR) || fread(buffer, 1, SECTOR, f) != SECTOR) {
            return zfs_error_set(e, ZFS_IO, "Short read from image or staging storage");
        }
    } else if (d->info.image) {
        memset(buffer, 0, SECTOR);
    } else {
        return zfs_native_read(d->device, sector * SECTOR, buffer, SECTOR, e);
    }
    return ZFS_OK;
}

static zfs_status write_sector(zfs_disk* d, uint64_t sector, const void* buffer, zfs_error* e)
{
    if (!d->undo || sector >= d->info.size / SECTOR) {
        return zfs_error_set(e, ZFS_INVALID, "Write outside transaction or disk");
    }
    undo_record record = {.sector = sector, .dirty = dirty(d, sector)};
    zfs_status s = read_sector(d, sector, record.data, e);
    if (s) {
        return s;
    }
    if (zfs_seek(d->undo, d->undo_count * sizeof(record)) ||
        fwrite(&record, 1, sizeof(record), d->undo) != sizeof(record)) {
        return sys_error(e, "Journal write failed");
    }
    ++d->undo_count;
    /* Set dirty before writing so rollback also repairs a partially written sector. */
    set_dirty(d, sector, true);
    if (zfs_seek(d->overlay, sector * SECTOR) || fwrite(buffer, 1, SECTOR, d->overlay) != SECTOR) {
        return sys_error(e, "Staging write failed");
    }
    return ZFS_OK;
}

static zfs_status bytes_io(zfs_disk* d, uint64_t address, void* buffer, size_t size, bool writing, zfs_error* e)
{
    if (address > d->info.size || size > d->info.size - address) {
        return zfs_error_set(e, ZFS_INVALID, "I/O exceeds disk size");
    }
    unsigned char sector[SECTOR];
    unsigned char* p = buffer;
    while (size) {
        size_t offset = (size_t) (address % SECTOR), n = SECTOR - offset;
        if (n > size) {
            n = size;
        }
        zfs_status s = read_sector(d, address / SECTOR, sector, e);
        if (s) {
            return s;
        }
        if (writing) {
            memcpy(sector + offset, p, n);
            s = write_sector(d, address / SECTOR, sector, e);
            if (s) {
                return s;
            }
        } else {
            memcpy(p, sector + offset, n);
        }
        address += n;
        p += n;
        size -= n;
    }
    return ZFS_OK;
}

static zfs_status begin(zfs_disk* d, zfs_error* e)
{
    if (!d) {
        return zfs_error_set(e, ZFS_INVALID, "No disk is open");
    }
    if (d->info.failed) {
        return zfs_error_set(e, ZFS_PARTIAL_WRITE, "Session failed; reopen the target before making changes");
    }
    if (d->undo) {
        return zfs_error_set(e, ZFS_BUSY, "Operation already active");
    }
    memset(d->usage_cached, 0, sizeof(d->usage_cached));
    d->undo = tmpfile();
    d->undo_count = 0;
    return d->undo ? ZFS_OK : sys_error(e, "Cannot create operation journal");
}

static zfs_status finish(zfs_disk* d, zfs_status s, zfs_error* e)
{
    if (s) {
        clearerr(d->overlay);
        clearerr(d->undo);
        while (d->undo_count) {
            undo_record record;
            --d->undo_count;
            if (zfs_seek(d->undo, d->undo_count * sizeof(record)) ||
                fread(&record, 1, sizeof(record), d->undo) != sizeof(record) ||
                (record.dirty && (zfs_seek(d->overlay, record.sector * SECTOR) ||
                                  fwrite(record.data, 1, SECTOR, d->overlay) != SECTOR))) {
                d->info.failed = true;
                s = zfs_error_set(e, ZFS_IO, "Staging rollback failed; close and reopen the session");
                break;
            }
            set_dirty(d, record.sector, record.dirty);
        }
    }
    fclose(d->undo);
    d->undo = NULL;
    d->undo_count = 0;
    d->info.pending = d->info.new_image || d->dirty_count > 0;
    if (!s) {
        ++d->revision;
    }
    return s;
}

static uint32_t le32(const unsigned char* p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static void put32(unsigned char* p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) {
        p[i] = (unsigned char) (v >> (i * 8));
    }
}

static zfs_status parse(zfs_disk* d, zfs_error* e)
{
    unsigned char sector[SECTOR];
    zfs_status s = read_sector(d, 0, sector, e);
    if (s) {
        return s;
    }
    /* A raw ZealFS header takes precedence over incidental 55aa bytes in its
     * root entries/FAT; partition backups are raw images, not MBR sectors. */
    bool raw = sector[0] == 'Z' && sector[1] == 2 && sector[6] <= 8;
    d->info.has_mbr = !raw && sector[510] == 0x55 && sector[511] == 0xaa;
    memset(d->info.partitions, 0, sizeof(d->info.partitions));
    if (!d->info.has_mbr) {
        if (sector[0] == 'Z' && sector[1] == 2) {
            d->info.partitions[0] = (zfs_partition){true, 0x5a, 0, (uint32_t) (d->info.size / SECTOR)};
        }
        return ZFS_OK;
    }
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned char* p = sector + 446 + i * 16;
        zfs_partition* part = &d->info.partitions[i];
        *part = (zfs_partition){(p[0] & 0x80) || p[4] || le32(p + 8) || le32(p + 12), p[4], le32(p + 8), le32(p + 12)};
        if (part->active && (!part->sectors || !part->start_lba ||
                             (uint64_t) part->start_lba + part->sectors > d->info.size / SECTOR)) {
            return zfs_error_set(e, ZFS_CORRUPT, "Partition %u lies outside the disk", i + 1);
        }
        for (unsigned j = 0; j < i; ++j) {
            zfs_partition* q = &d->info.partitions[j];
            if (part->active && q->active && (uint64_t) part->start_lba < (uint64_t) q->start_lba + q->sectors &&
                (uint64_t) q->start_lba < (uint64_t) part->start_lba + part->sectors) {
                return zfs_error_set(e, ZFS_CORRUPT, "Overlapping partitions");
            }
        }
    }
    return ZFS_OK;
}

static zfs_status allocate_disk(const char* path, uint64_t size, bool image, zfs_disk** out, zfs_error* e)
{
    if (!path || !*path || !out || strlen(path) >= ZFS_PATH_MAX - 64 || size < SECTOR || size > ZFS_MAX_DISK_SIZE ||
        size % SECTOR) {
        return zfs_error_set(e, ZFS_INVALID, "Invalid path or disk size (512-byte alignment, maximum 64 GiB)");
    }
    zfs_disk* d = calloc(1, sizeof(*d));
    if (!d) {
        return zfs_error_set(e, ZFS_NO_MEMORY, "Cannot allocate disk session");
    }
    d->device = ZFS_NATIVE_INVALID;
    d->info.size = size;
    d->info.image = image;
    if (!image || !zfs_host_resolve(path, d->info.path, sizeof(d->info.path))) {
        strcpy(d->info.path, path);
    }
    d->bitmap_size = (size_t) ((size / SECTOR + 7) / 8);
    d->dirty = calloc(1, d->bitmap_size);
    d->overlay = tmpfile();
    if (!d->dirty || !d->overlay) {
        zfs_close(d);
        return zfs_error_set(e, ZFS_NO_MEMORY, "Cannot allocate staging storage");
    }
    *out = d;
    return ZFS_OK;
}

static void save_stamp(zfs_disk* d)
{
    fstat(fileno(d->base), &d->stamp);
#ifdef _WIN32
    GetFileInformationByHandle((HANDLE) _get_osfhandle(_fileno(d->base)), &d->win_stamp);
#endif
}

zfs_status zfs_open(const char* path, bool image, zfs_disk** out, zfs_error* e)
{
    if (out) {
        *out = NULL;
    }
    if (!path || !out) {
        return zfs_error_set(e, ZFS_INVALID, "A target path is required");
    }
    FILE* base = NULL;
    zfs_native native = ZFS_NATIVE_INVALID;
    uint64_t size = 0;
    if (image) {
#ifndef _WIN32
        struct stat link_stat;
        if (!lstat(path, &link_stat) && S_ISLNK(link_stat.st_mode)) {
            return zfs_error_set(e, ZFS_INVALID, "Open the actual image path instead of a symbolic link");
        }
#endif
        base = zfs_host_open(path, "rb");
        if (!base) {
            return sys_error(e, "Cannot open image");
        }
        struct stat st;
        if (fstat(fileno(base), &st) || !(st.st_mode & S_IFREG) || st.st_size < 0) {
            fclose(base);
            return zfs_error_set(e, ZFS_INVALID, "Target is not a regular image file");
        }
        size = (uint64_t) st.st_size;
    } else {
        zfs_status s = zfs_native_open(path, false, &native, &size, e);
        if (s) {
            return s;
        }
    }
    zfs_disk* d = NULL;
    zfs_status s = allocate_disk(path, size, image, &d, e);
    if (s) {
        if (base) {
            fclose(base);
        }
        zfs_native_close(native);
        return s;
    }
    d->base = base;
    d->device = native;
    if (base) {
        save_stamp(d);
    }
    s = parse(d, e);
    if (s) {
        zfs_close(d);
        return s;
    }
    d->original = d->info;
    *out = d;
    return ZFS_OK;
}

zfs_status zfs_image_create(const char* path, uint64_t size, bool mbr, zfs_disk** out, zfs_error* e)
{
    if (out) {
        *out = NULL;
    }
    if (!path || !out) {
        return zfs_error_set(e, ZFS_INVALID, "A target path is required");
    }
    FILE* exists = zfs_host_open(path, "rb");
    if (exists) {
        fclose(exists);
        return zfs_error_set(e, ZFS_EXISTS, "Image destination already exists");
    }
    zfs_disk* d = NULL;
    zfs_status s = allocate_disk(path, size, true, &d, e);
    if (s) {
        return s;
    }
    d->info.new_image = true;
    d->info.pending = true;
    d->original = d->info;
    d->original.pending = false;
    if (mbr) {
        s = zfs_mbr_create(d, e);
    }
    if (s) {
        zfs_close(d);
        return s;
    }
    *out = d;
    return ZFS_OK;
}

void zfs_close(zfs_disk* d)
{
    if (!d) {
        return;
    }
    if (d->base && !d->borrowed_base) {
        fclose(d->base);
    }
    if (d->overlay) {
        fclose(d->overlay);
    }
    if (d->undo) {
        fclose(d->undo);
    }
    if (!d->borrowed_base) {
        zfs_native_close(d->device);
    }
    free(d->dirty);
    free(d);
}

void zfs_set_callbacks(zfs_disk* d, zfs_callbacks callbacks)
{
    if (d) {
        d->callbacks = callbacks;
    }
}

const zfs_info* zfs_get_info(const zfs_disk* d)
{
    return d ? &d->info : NULL;
}

static void progress(zfs_disk* d, uint64_t n, uint64_t total)
{
    if (d->callbacks.progress) {
        d->callbacks.progress(d->callbacks.user, n, total);
    }
}

zfs_status zfs_discard(zfs_disk* d, zfs_error* e)
{
    if (!d) {
        return zfs_error_set(e, ZFS_INVALID, "No disk is open");
    }
    if (d->info.failed) {
        return zfs_error_set(e, ZFS_PARTIAL_WRITE, "Reopen failed session; discard cannot undo physical writes");
    }
    FILE* fresh = tmpfile();
    if (!fresh) {
        return sys_error(e, "Cannot reset staging storage");
    }
    fclose(d->overlay);
    d->overlay = fresh;
    memset(d->dirty, 0, d->bitmap_size);
    d->dirty_count = 0;
    d->info = d->original;
    memset(d->usage_cached, 0, sizeof(d->usage_cached));
    /* Discard cancels creation; a later mutation can stage it again. */
    d->info.pending = false;
    d->verified_apply = false;
    d->recovery_backup[0] = 0;
    ++d->revision;
    return ZFS_OK;
}

static bool file_identity(FILE* a, FILE* b)
{
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION x, y;
    return GetFileInformationByHandle((HANDLE) _get_osfhandle(_fileno(a)), &x) &&
           GetFileInformationByHandle((HANDLE) _get_osfhandle(_fileno(b)), &y) &&
           x.dwVolumeSerialNumber == y.dwVolumeSerialNumber && x.nFileIndexHigh == y.nFileIndexHigh &&
           x.nFileIndexLow == y.nFileIndexLow;
#else
    struct stat x, y;
    return !fstat(fileno(a), &x) && !fstat(fileno(b), &y) && x.st_dev == y.st_dev && x.st_ino == y.st_ino;
#endif
}

static bool same_image(zfs_disk* d)
{
    FILE* f = zfs_host_open(d->info.path, "rb");
    struct stat st;
    bool same = f && !fstat(fileno(f), &st) && st.st_dev == d->stamp.st_dev && st.st_ino == d->stamp.st_ino &&
                st.st_size == d->stamp.st_size && st.st_mtime == d->stamp.st_mtime;
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION current;
    if (same) {
        same = GetFileInformationByHandle((HANDLE) _get_osfhandle(_fileno(f)), &current) &&
               current.dwVolumeSerialNumber == d->win_stamp.dwVolumeSerialNumber &&
               current.nFileIndexHigh == d->win_stamp.nFileIndexHigh &&
               current.nFileIndexLow == d->win_stamp.nFileIndexLow &&
               CompareFileTime(&current.ftLastWriteTime, &d->win_stamp.ftLastWriteTime) == 0;
    }
#else
#ifdef __APPLE__
    if (same) {
        same = st.st_mtimespec.tv_nsec == d->stamp.st_mtimespec.tv_nsec;
    }
#else
    if (same) {
        same = st.st_mtim.tv_nsec == d->stamp.st_mtim.tv_nsec;
    }
#endif
#endif
    if (f) {
        fclose(f);
    }
    return same;
}

static zfs_native image_handle(FILE* file);

static zfs_status verify_staged(zfs_disk* d, FILE* file, zfs_native device, zfs_error* e)
{
    unsigned char expected[SECTOR], actual[SECTOR];
    for (uint64_t i = 0; i < d->info.size / SECTOR; ++i) {
        if (dirty(d, i)) {
            zfs_status s = read_sector(d, i, expected, e);
            if (s) {
                return s;
            }
            if (file) {
                if (zfs_seek(file, i * SECTOR) || fread(actual, 1, SECTOR, file) != SECTOR) {
                    return zfs_error_set(e, ZFS_IO, "Cannot read back staged image");
                }
            } else {
                s = zfs_native_read(device, i * SECTOR, actual, SECTOR, e);
                if (s) {
                    return s;
                }
            }
            if (memcmp(expected, actual, SECTOR)) {
                return zfs_error_set(e, ZFS_IO, "Write verification failed at sector %llu", (unsigned long long) i);
            }
        }
    }
    return ZFS_OK;
}

static zfs_status verify_original_device(zfs_disk* d, zfs_error* e)
{
    FILE* backup = zfs_host_open(d->recovery_backup, "rb");
    if (!backup) {
        return zfs_error_set(e, ZFS_IO, "Cannot open retained backup for pre-commit verification");
    }
    unsigned char original[65536], actual[65536];
    zfs_status s = ZFS_OK;
    for (uint64_t at = 0; !s && at < d->recovery_size; at += sizeof(original)) {
        size_t n = d->recovery_size - at < sizeof(original) ? (size_t) (d->recovery_size - at) : sizeof(original);
        if (fread(original, 1, n, backup) != n) {
            s = zfs_error_set(e, ZFS_IO, "Retained backup is truncated or unreadable");
        }
        if (!s) {
            s = zfs_native_read(d->device, d->recovery_offset + at, actual, n, e);
        }
        if (!s && memcmp(original, actual, n)) {
            s = zfs_error_set(e, ZFS_BUSY,
                              "Target or backup changed since preparation; no target sectors were written");
        }
    }
    if (!s && fgetc(backup) != EOF) {
        s = zfs_error_set(e, ZFS_INVALID, "Retained backup size changed");
    }
    fclose(backup);
    return s;
}

zfs_status zfs_apply(zfs_disk* d, zfs_error* e)
{
    if (!d) {
        return zfs_error_set(e, ZFS_INVALID, "No disk is open");
    }
    if (d->info.failed) {
        return zfs_error_set(e, ZFS_PARTIAL_WRITE, "Reopen failed session before applying");
    }
    if (!d->info.pending) {
        return ZFS_OK;
    }
    zfs_status s = ZFS_OK;
    if (d->info.image) {
        if (!d->info.new_image) {
            s = zfs_core_guard(d, true, e);
            if (s) {
                return s;
            }
        }
        char temporary[ZFS_PATH_MAX];
        FILE* dest = zfs_temp_sibling(d->info.path, temporary, sizeof(temporary));
        if (!dest) {
            return sys_error(e, "Cannot create temporary destination image");
        }
        unsigned char buffer[65536];
        for (uint64_t offset = 0; offset < d->info.size && !s; offset += sizeof(buffer)) {
            size_t n = d->info.size - offset < sizeof(buffer) ? (size_t) (d->info.size - offset) : sizeof(buffer);
            s = bytes_io(d, offset, buffer, n, false, e);
            if (!s && fwrite(buffer, 1, n, dest) != n) {
                s = sys_error(e, "Cannot write replacement image");
            }
            progress(d, offset + n, d->info.size);
        }
        if (!s && zfs_host_sync(dest)) {
            s = sys_error(e, "Cannot flush replacement image");
        }
        if (!s && d->verified_apply) {
            s = verify_staged(d, dest, ZFS_NATIVE_INVALID, e);
        }
        if (!s && d->guarded) {
            zfs_native fd = image_handle(dest);
            s = zfs_native_guard(&fd, temporary, true, true, e);
        }
        if (!s && !d->info.new_image && !same_image(d)) {
            s = zfs_error_set(e, ZFS_BUSY, "Image changed while preparing Apply");
        }
        if (!s) {
            if (d->base) {
                fclose(d->base);
                d->base = NULL;
            }
            if (zfs_host_publish(temporary, d->info.path, !d->info.new_image)) {
#ifdef _WIN32
                s = zfs_error_set(e, ZFS_IO, "Cannot publish image (Windows error %lu)",
                                  (unsigned long) GetLastError());
#else
                s = zfs_error_set(e, ZFS_IO, "Cannot publish image (destination exists or replacement failed)");
#endif
                if (!d->info.new_image) {
                    d->base = zfs_host_open(d->info.path, "rb");
                    d->guarded = false;
                    if (!d->base || zfs_core_guard(d, true, NULL)) {
                        d->info.failed = true;
                    }
                }
            } else {
                d->base = dest;
                dest = NULL;
            }
        }
        if (dest) {
            fclose(dest);
        }
        if (s) {
            zfs_host_remove(temporary);
            return s;
        }
        save_stamp(d);
    } else {
        bool held = d->guarded && d->guard_write;
        zfs_native fd = held ? d->device : ZFS_NATIVE_INVALID;
        uint64_t size = d->info.size;
        if (!held) {
            s = zfs_native_open(d->info.path, true, &fd, &size, e);
        }
        if (s) {
            return s;
        }
        if (size != d->info.size || !zfs_native_same(d->device, fd)) {
            if (!held) {
                zfs_native_close(fd);
            }
            return zfs_error_set(e, ZFS_BUSY, "Device identity or size changed");
        }
        if (d->verified_apply && d->recovery_is_merge) {
            s = verify_original_device(d, e);
        }
        if (s) {
            if (!held) {
                zfs_native_close(fd);
            }
            return s;
        }
        unsigned char sector[SECTOR];
        uint64_t completed = 0;
        for (uint64_t i = 0; i < d->info.size / SECTOR && !s; ++i) {
            if (dirty(d, i)) {
                s = read_sector(d, i, sector, e);
                if (!s) {
                    s = zfs_native_write(fd, i * SECTOR, sector, SECTOR, e);
                }
                progress(d, ++completed, d->dirty_count);
            }
        }
        if (!s) {
            s = zfs_native_sync(fd, e);
        }
        if (!s && d->verified_apply) {
            s = verify_staged(d, NULL, fd, e);
        }
        if (!held) {
            zfs_native_close(fd);
        }
        if (s) {
            d->info.failed = true;
            return zfs_error_set(e, ZFS_PARTIAL_WRITE,
                                 "Apply or readback failed; device may be partially updated. Reopen it "
                                 "before restoring from the retained backup");
        }
    }
    d->info.pending = d->info.new_image = false;
    d->original = d->info;
    memset(d->dirty, 0, d->bitmap_size);
    d->dirty_count = 0;
    d->verified_apply = false;
    d->recovery_is_merge = false;
    ++d->revision;
    if (d->callbacks.log) {
        d->callbacks.log(d->callbacks.user, "Changes applied");
    }
    return ZFS_OK;
}

static zfs_status mbr_write(zfs_disk* d, zfs_error* e)
{
    unsigned char mbr[SECTOR];
    zfs_status s = read_sector(d, 0, mbr, e);
    if (s) {
        return s;
    }
    for (int i = 0; i < 4; ++i) {
        unsigned char* p = mbr + 446 + i * 16;
        zfs_partition* part = &d->info.partitions[i];
        memset(p, 0, 16);
        if (part->active) {
            p[1] = p[2] = p[3] = p[5] = p[6] = p[7] = 0xff;
            p[4] = part->type;
            put32(p + 8, part->start_lba);
            put32(p + 12, part->sectors);
        }
    }
    mbr[510] = 0x55;
    mbr[511] = 0xaa;
    return write_sector(d, 0, mbr, e);
}

zfs_status zfs_mbr_create(zfs_disk* d, zfs_error* e)
{
    if (!d || d->info.has_mbr) {
        return zfs_error_set(e, ZFS_INVALID, "Disk already has an MBR or is not open");
    }
    if (d->info.partitions[0].active) {
        return zfs_error_set(e, ZFS_BUSY, "Delete the raw partition before creating an MBR");
    }
    zfs_status s = begin(d, e);
    if (s) {
        return s;
    }
    unsigned char sector[SECTOR] = {0};
    sector[510] = 0x55;
    sector[511] = 0xaa;
    s = write_sector(d, 0, sector, e);
    s = finish(d, s, e);
    if (!s) {
        d->info.has_mbr = true;
    }
    return s;
}

zfs_status zfs_partition_space(zfs_disk* d, uint32_t alignment, uint64_t* start, uint64_t* size, zfs_error* e)
{
    if (!d || !start || !size || alignment < SECTOR || (alignment & (alignment - 1))) {
        return zfs_error_set(e, ZFS_INVALID, "Alignment must be a power of two, at least 512 bytes");
    }
    uint64_t cursor = d->info.has_mbr ? SECTOR : 0, best = 0, best_start = 0;
    zfs_partition parts[4];
    int n = 0;
    for (int i = 0; i < 4; ++i) {
        if (d->info.partitions[i].active) {
            parts[n++] = d->info.partitions[i];
        }
    }
    for (int i = 1; i < n; ++i) {
        zfs_partition p = parts[i];
        int j = i;
        while (j > 0 && parts[j - 1].start_lba > p.start_lba) {
            parts[j] = parts[j - 1];
            --j;
        }
        parts[j] = p;
    }
    for (int i = 0; i <= n; ++i) {
        uint64_t end = i < n ? (uint64_t) parts[i].start_lba * SECTOR : d->info.size;
        uint64_t aligned = (cursor + alignment - 1) & ~((uint64_t) alignment - 1);
        if (end > aligned && end - aligned > best) {
            best = end - aligned;
            best_start = aligned;
        }
        if (i < n) {
            cursor = ((uint64_t) parts[i].start_lba + parts[i].sectors) * SECTOR;
        }
    }
    if (best > MAX_PART_SIZE) {
        best = MAX_PART_SIZE;
    }
    *start = best_start;
    *size = best;
    return ZFS_OK;
}

static zfs_status format(zfs_disk* d, zfs_partition part, zfs_error* e)
{
    uint64_t size = (uint64_t) part.sectors * SECTOR;
    if (size < 8192 || size > MAX_PART_SIZE) {
        return zfs_error_set(e, ZFS_INVALID, "ZealFS v2 partitions must be between 8 KiB and 4 GiB");
    }
    size_t bytes = (size_t) zealfsv2_page_size((long long) size) * 3;
    unsigned char* data = calloc(1, bytes);
    if (!data) {
        return zfs_error_set(e, ZFS_NO_MEMORY, "Cannot allocate format buffer");
    }
    zealfsv2_format(data, size);
    zfs_status s = bytes_io(d, (uint64_t) part.start_lba * SECTOR, data, bytes, true, e);
    free(data);
    return s;
}

zfs_status zfs_partition_create(zfs_disk* d, uint64_t size, uint32_t alignment, int* index, zfs_error* e)
{
    uint64_t start, available;
    zfs_status s = zfs_partition_space(d, alignment, &start, &available, e);
    if (s) {
        return s;
    }
    int slot = -1;
    for (int i = 0; i < (d->info.has_mbr ? 4 : 1); ++i) {
        if (!d->info.partitions[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return zfs_error_set(e, ZFS_NO_SPACE, "No free partition slot");
    }
    if (!size) {
        size = available;
    }
    if (size < 8192 || size > available || size % SECTOR) {
        return zfs_error_set(e, ZFS_INVALID, "Partition size is not available or not sector-aligned");
    }
    zfs_info backup = d->info;
    s = begin(d, e);
    if (s) {
        return s;
    }
    d->info.partitions[slot] = (zfs_partition){true, 0x5a, (uint32_t) (start / SECTOR), (uint32_t) (size / SECTOR)};
    s = format(d, d->info.partitions[slot], e);
    if (!s && d->info.has_mbr) {
        s = mbr_write(d, e);
    }
    s = finish(d, s, e);
    if (s) {
        bool failed = d->info.failed;
        d->info = backup;
        d->info.failed = failed;
    } else if (index) {
        *index = slot;
    }
    return s;
}

static zfs_status partition_check(zfs_disk* d, int index, bool filesystem_required, zfs_error* e)
{
    if (!d || index < 0 || index >= 4 || !d->info.partitions[index].active) {
        return zfs_error_set(e, ZFS_INVALID, "Invalid partition");
    }
    if (filesystem_required && d->info.partitions[index].type != 0x5a) {
        return zfs_error_set(e, ZFS_INVALID, "Partition is not ZealFS v2");
    }
    if (d->info.failed) {
        return zfs_error_set(e, ZFS_PARTIAL_WRITE, "Reopen failed session");
    }
    return ZFS_OK;
}

zfs_status zfs_partition_format(zfs_disk* d, int index, zfs_error* e)
{
    zfs_status s = partition_check(d, index, false, e);
    if (s) {
        return s;
    }
    s = begin(d, e);
    if (s) {
        return s;
    }
    zfs_info backup = d->info;
    d->info.partitions[index].type = 0x5a;
    s = format(d, d->info.partitions[index], e);
    if (!s && d->info.has_mbr) {
        s = mbr_write(d, e);
    }
    s = finish(d, s, e);
    if (s) {
        bool failed = d->info.failed;
        d->info = backup;
        d->info.failed = failed;
    }
    return s;
}

zfs_status zfs_partition_delete(zfs_disk* d, int index, zfs_error* e)
{
    zfs_status s = partition_check(d, index, false, e);
    if (s) {
        return s;
    }
    s = begin(d, e);
    if (s) {
        return s;
    }
    zfs_info backup = d->info;
    memset(&d->info.partitions[index], 0, sizeof(zfs_partition));
    if (d->info.has_mbr) {
        s = mbr_write(d, e);
    } else {
        unsigned char sector[SECTOR] = {0};
        s = write_sector(d, 0, sector, e);
    }
    s = finish(d, s, e);
    if (s) {
        bool failed = d->info.failed;
        d->info = backup;
        d->info.failed = failed;
    }
    return s;
}

static ssize_t fs_read(void* arg, void* buffer, uint32_t address, size_t size)
{
    filesystem* f = arg;
    if ((uint64_t) address + size > (uint64_t) f->part.sectors * SECTOR) {
        return -EIO;
    }
    return bytes_io(f->disk, (uint64_t) f->part.start_lba * SECTOR + address, buffer, size, false, NULL)
               ? -EIO
               : (ssize_t) size;
}

static ssize_t fs_write(void* arg, const void* buffer, uint32_t address, size_t size)
{
    filesystem* f = arg;
    if ((uint64_t) address + size > (uint64_t) f->part.sectors * SECTOR) {
        return -EIO;
    }
    return bytes_io(f->disk, (uint64_t) f->part.start_lba * SECTOR + address, (void*) buffer, size, true, NULL)
               ? -EIO
               : (ssize_t) size;
}

static zfs_status fs_open(zfs_disk* d, int partition, filesystem** out, zfs_error* e)
{
    zfs_status s = partition_check(d, partition, true, e);
    if (s) {
        return s;
    }
    filesystem* f = calloc(1, sizeof(*f));
    if (!f) {
        return zfs_error_set(e, ZFS_NO_MEMORY, "Cannot allocate filesystem context");
    }
    f->disk = d;
    f->part = d->info.partitions[partition];
    f->fs.read = fs_read;
    f->fs.write = fs_write;
    f->fs.arg = f;
    zealfs_fd_t root;
    int r = zealfs_opendir("/", &f->fs, &root);
    if (r < 0) {
        free(f);
        return zfs_error_set(e, ZFS_CORRUPT, "Invalid or unreadable ZealFS v2 header");
    }
    *out = f;
    return ZFS_OK;
}

static zfs_status fs_error(int r, zfs_error* e)
{
    if (r >= 0) {
        return ZFS_OK;
    }
    zfs_status s;
    switch (-r) {
    case ENOENT:
        s = ZFS_NOT_FOUND;
        break;
    case EEXIST:
        s = ZFS_EXISTS;
        break;
    case ENOSPC:
        s = ZFS_NO_SPACE;
        break;
    case EIO:
        s = ZFS_IO;
        break;
    case ENOMEM:
        s = ZFS_NO_MEMORY;
        break;
    default:
        s = ZFS_INVALID;
        break;
    }
    return zfs_error_set(e, s, "%s", strerror(-r));
}

static zfs_status path_check(const char* path, zfs_error* e)
{
    if (!path || path[0] != '/' || strlen(path) >= 512) {
        return zfs_error_set(e, ZFS_INVALID, "Use an absolute ZealFS path shorter than 512 bytes");
    }
    const char* p = path + 1;
    while (*p) {
        const char* end = strchr(p, '/');
        size_t n = end ? (size_t) (end - p) : strlen(p);
        if (!n || n > 16 || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) {
            return zfs_error_set(e, ZFS_INVALID,
                                 "Path components must be 1 to 16 bytes; dot components are unsupported");
        }
        if (!end) {
            break;
        }
        p = end + 1;
        if (!*p) {
            return zfs_error_set(e, ZFS_INVALID, "Remove the trailing slash");
        }
    }
    return ZFS_OK;
}

zfs_status zfs_list(zfs_disk* d, int part, const char* path, zfs_entry** entries, size_t* count, zfs_error* e)
{
    if (!entries || !count) {
        return zfs_error_set(e, ZFS_INVALID, "Listing outputs are required");
    }
    *entries = NULL;
    *count = 0;
    zfs_status s = path_check(path, e);
    if (s) {
        return s;
    }
    filesystem* f = NULL;
    s = fs_open(d, part, &f, e);
    if (s) {
        return s;
    }
    zealfs_fd_t fd;
    int r = zealfs_opendir(path, &f->fs, &fd);
    if (r < 0) {
        free(f);
        return fs_error(r, e);
    }
    /* A directory can chain up to 65535 pages. Readdir reports overflow rather than truncating. */
    size_t capacity = 2048;
    zealfs_entry_t* raw = NULL;
    for (;;) {
        raw = malloc(capacity * sizeof(*raw));
        if (!raw) {
            free(f);
            return zfs_error_set(e, ZFS_NO_MEMORY, "Cannot allocate directory entries");
        }
        r = zealfs_readdir(&f->fs, &fd, raw, (int) capacity);
        if (r != -ENOBUFS) {
            break;
        }
        free(raw);
        raw = NULL;
        if (capacity >= 1048576) {
            free(f);
            return zfs_error_set(e, ZFS_NO_SPACE, "Directory listing exceeds supported entry limit");
        }
        capacity *= 2;
    }
    if (r < 0) {
        free(raw);
        free(f);
        return fs_error(r, e);
    }
    zfs_entry* result = calloc(r ? r : 1, sizeof(*result));
    if (!result) {
        free(raw);
        free(f);
        return zfs_error_set(e, ZFS_NO_MEMORY, "Cannot allocate listing");
    }
    for (int i = 0; i < r; ++i) {
        memcpy(result[i].name, raw[i].name, 16);
        result[i].directory = (raw[i].flags & IS_DIR) != 0;
        result[i].size = raw[i].size;
        result[i].year = from_bcd(raw[i].year[0]) * 100 + from_bcd(raw[i].year[1]);
        result[i].month = from_bcd(raw[i].month);
        result[i].day = from_bcd(raw[i].day);
        result[i].hour = from_bcd(raw[i].hours);
        result[i].minute = from_bcd(raw[i].minutes);
        result[i].second = from_bcd(raw[i].seconds);
    }
    *entries = result;
    *count = (size_t) r;
    free(raw);
    free(f);
    return ZFS_OK;
}

void zfs_entries_free(zfs_entry* entries)
{
    free(entries);
}

zfs_status zfs_usage(zfs_disk* d, int part, uint64_t* free_bytes, uint64_t* total, zfs_error* e)
{
    if (!free_bytes || !total) {
        return zfs_error_set(e, ZFS_INVALID, "Usage outputs are required");
    }
    zfs_status s = partition_check(d, part, true, e);
    if (s) {
        return s;
    }
    if (!d->usage_cached[part]) {
        filesystem* f = NULL;
        s = fs_open(d, part, &f, e);
        if (s) {
            return s;
        }
        d->free_cache[part] = zealfs_free_space(&f->fs);
        d->total_cache[part] =
            f->part.start_lba == 0 ? zealfs_total_space(&f->fs) : (uint64_t) f->part.sectors * SECTOR;
        free(f);
        d->usage_cached[part] = true;
    }
    *free_bytes = d->free_cache[part];
    *total = d->total_cache[part];
    return ZFS_OK;
}

static zfs_status parent_check(filesystem* f, const char* path, zfs_error* e)
{
    if (!strcmp(path, "/")) {
        return zfs_error_set(e, ZFS_INVALID, "Cannot modify the root entry");
    }
    char parent[512];
    strcpy(parent, path);
    char* slash = strrchr(parent, '/');
    if (slash == parent) {
        slash[1] = 0;
    } else {
        *slash = 0;
    }
    zealfs_fd_t fd;
    return fs_error(zealfs_opendir(parent, &f->fs, &fd), e);
}

static zfs_status entry_mutation(zfs_disk* d, int part, const char* path, int operation, zfs_error* e)
{
    zfs_status s = path_check(path, e);
    if (s) {
        return s;
    }
    filesystem* f = NULL;
    s = fs_open(d, part, &f, e);
    if (s) {
        return s;
    }
    s = parent_check(f, path, e);
    if (s) {
        free(f);
        return s;
    }
    s = begin(d, e);
    if (s) {
        free(f);
        return s;
    }
    int r = operation == 0   ? zealfs_mkdir(path, &f->fs, NULL)
            : operation == 1 ? zealfs_unlink(path, &f->fs)
                             : zealfs_rmdir(path, &f->fs);
    s = finish(d, fs_error(r, e), e);
    free(f);
    return s;
}

zfs_status zfs_mkdir(zfs_disk* d, int part, const char* path, zfs_error* e)
{
    return entry_mutation(d, part, path, 0, e);
}

zfs_status zfs_remove(zfs_disk* d, int part, const char* path, bool directory, zfs_error* e)
{
    return entry_mutation(d, part, path, directory ? 2 : 1, e);
}

static zfs_status create_file(zfs_disk* d, int part, const char* path, FILE* source, const unsigned char* data,
                              uint64_t size, zfs_error* e)
{
    zfs_status s = path_check(path, e);
    if (s) {
        return s;
    }
    if (size > UINT32_MAX) {
        return zfs_error_set(e, ZFS_INVALID, "File exceeds ZealFS size limit");
    }
    filesystem* f = NULL;
    s = fs_open(d, part, &f, e);
    if (s) {
        return s;
    }
    s = parent_check(f, path, e);
    if (s) {
        free(f);
        return s;
    }
    s = begin(d, e);
    if (s) {
        free(f);
        return s;
    }
    zealfs_fd_t fd;
    int r = zealfs_create(path, &f->fs, &fd);
    s = fs_error(r, e);
    unsigned char buffer[65536];
    uint64_t offset = 0;
    while (!s && offset < size) {
        size_t n = size - offset < sizeof(buffer) ? (size_t) (size - offset) : sizeof(buffer);
        const void* bytes = data ? data + (size_t) offset : buffer;
        if (source && fread(buffer, 1, n, source) != n) {
            s = zfs_error_set(e, ZFS_IO, "Short read from import source");
            break;
        }
        r = zealfs_write(&f->fs, &fd, (void*) bytes, n, (int64_t) offset);
        if (r != (int) n) {
            s = r < 0 ? fs_error(r, e) : zfs_error_set(e, ZFS_IO, "Short file write");
        }
        offset += n;
        progress(d, offset, size);
    }
    if (!s) {
        s = fs_error(zealfs_flush(&f->fs, &fd), e);
    }
    s = finish(d, s, e);
    free(f);
    return s;
}

zfs_status zfs_file_write(zfs_disk* d, int part, const char* path, const void* buffer, size_t size, zfs_error* e)
{
    if (!buffer && size) {
        return zfs_error_set(e, ZFS_INVALID, "File buffer is required");
    }
    return create_file(d, part, path, NULL, buffer, size, e);
}

zfs_status zfs_import(zfs_disk* d, int part, const char* host, const char* path, zfs_error* e)
{
    if (!host) {
        return zfs_error_set(e, ZFS_INVALID, "Import path is required");
    }
    FILE* file = zfs_host_open(host, "rb");
    if (!file) {
        return sys_error(e, "Cannot open import source");
    }
#ifdef _WIN32
    int seek_result = _fseeki64(file, 0, SEEK_END);
#else
    int seek_result = fseeko(file, 0, SEEK_END);
#endif
    int64_t size = zfs_tell(file);
    if (seek_result || size < 0 || zfs_seek(file, 0)) {
        fclose(file);
        return zfs_error_set(e, ZFS_IO, "Cannot determine source size");
    }
    zfs_status s = create_file(d, part, path, file, NULL, (uint64_t) size, e);
    fclose(file);
    return s;
}

zfs_status zfs_file_read(zfs_disk* d, int part, const char* path, uint64_t offset, void* buffer, size_t size,
                         size_t* count, zfs_error* e)
{
    if (!count || (!buffer && size) || size > INT32_MAX) {
        return zfs_error_set(e, ZFS_INVALID, "Invalid read buffer");
    }
    *count = 0;
    zfs_status s = path_check(path, e);
    if (s) {
        return s;
    }
    filesystem* f = NULL;
    s = fs_open(d, part, &f, e);
    if (s) {
        return s;
    }
    zealfs_fd_t fd;
    int r = zealfs_open(path, &f->fs, &fd);
    if (r >= 0 && (fd.entry.flags & IS_DIR)) {
        free(f);
        return zfs_error_set(e, ZFS_INVALID, "Cannot read a directory as a file");
    }
    if (r >= 0 && offset < fd.entry.size) {
        r = zealfs_read(&f->fs, &fd, buffer, size, (int64_t) offset);
    } else if (r >= 0) {
        r = 0;
    }
    s = fs_error(r, e);
    if (!s) {
        *count = (size_t) r;
    }
    free(f);
    return s;
}

zfs_status zfs_core_export_destination(zfs_disk* d, const char* host, zfs_error* e)
{
    if (!host || !*host || strlen(host) >= ZFS_PATH_MAX - 64) {
        return zfs_error_set(e, ZFS_INVALID, "Invalid export destination");
    }
    if (d && d->info.image) {
        char resolved[ZFS_PATH_MAX];
        const char* destination = zfs_host_resolve(host, resolved, sizeof(resolved)) ? resolved : host;
#ifdef _WIN32
        bool target_alias = _stricmp(destination, d->info.path) == 0;
#else
        bool target_alias = strcmp(destination, d->info.path) == 0;
#endif
        if (target_alias) {
            return zfs_error_set(e, ZFS_INVALID, "Cannot export onto the active image");
        }
        FILE* candidate = zfs_host_open(host, "rb");
        if (candidate) {
            bool same = d->base && file_identity(candidate, d->base);
            fclose(candidate);
            if (same) {
                return zfs_error_set(e, ZFS_INVALID, "Cannot export onto the active image or an alias");
            }
        }
    }
    return ZFS_OK;
}

zfs_status zfs_core_path_directory(zfs_disk* d, int part, const char* path, bool* directory, zfs_error* e)
{
    zfs_status s = path_check(path, e);
    if (s) {
        return s;
    }
    filesystem* f = NULL;
    s = fs_open(d, part, &f, e);
    if (s) {
        return s;
    }
    *directory = !strcmp(path, "/");
    if (!*directory) {
        zealfs_fd_t fd;
        s = fs_error(zealfs_stat(path, &f->fs, &fd), e);
        if (!s) {
            *directory = (fd.entry.flags & IS_DIR) != 0;
        }
    }
    free(f);
    return s;
}

zfs_status zfs_core_export_file(zfs_disk* d, int part, const char* path, const char* host, bool overwrite, zfs_error* e)
{
    zfs_status s = zfs_core_export_destination(d, host, e);
    if (s) {
        return s;
    }
    s = path_check(path, e);
    if (s) {
        return s;
    }
    filesystem* f = NULL;
    s = fs_open(d, part, &f, e);
    if (s) {
        return s;
    }
    zealfs_fd_t fd;
    int r = zealfs_open(path, &f->fs, &fd);
    s = fs_error(r, e);
    if (!s && (fd.entry.flags & IS_DIR)) {
        s = zfs_error_set(e, ZFS_INVALID, "Only files can be exported");
    }
    if (s) {
        free(f);
        return s;
    }
    if (!overwrite) {
        FILE* exists = zfs_host_open(host, "rb");
        if (exists) {
            fclose(exists);
            free(f);
            return zfs_error_set(e, ZFS_EXISTS, "Export destination exists");
        }
    }
    char temporary[ZFS_PATH_MAX];
    FILE* dest = zfs_temp_sibling(host, temporary, sizeof(temporary));
    if (!dest) {
        free(f);
        return sys_error(e, "Cannot create export destination");
    }
    uint64_t offset = 0;
    unsigned char buffer[65536];
    while (!s && offset < fd.entry.size) {
        size_t n = fd.entry.size - offset < sizeof(buffer) ? (size_t) (fd.entry.size - offset) : sizeof(buffer);
        r = zealfs_read(&f->fs, &fd, buffer, n, (int64_t) offset);
        if (r != (int) n) {
            s = r < 0 ? fs_error(r, e) : zfs_error_set(e, ZFS_IO, "Short read during export");
        } else if (fwrite(buffer, 1, n, dest) != n) {
            s = sys_error(e, "Export write failed");
        }
        offset += n;
        progress(d, offset, fd.entry.size);
    }
    if (!s && zfs_host_sync(dest)) {
        s = sys_error(e, "Export flush failed");
    }
    if (fclose(dest) && !s) {
        s = sys_error(e, "Export close failed");
    }
    if (!s && zfs_host_publish(temporary, host, overwrite)) {
        s = zfs_error_set(e, ZFS_IO, "Cannot publish export (destination exists or replacement failed)");
    }
    if (s) {
        zfs_host_remove(temporary);
    }
    free(f);
    return s;
}

static zfs_native image_handle(FILE* file)
{
#ifdef _WIN32
    return (HANDLE) _get_osfhandle(_fileno(file));
#else
    return fileno(file);
#endif
}

bool zfs_core_same_disk(zfs_disk* a, zfs_disk* b)
{
    if (!a || !b) {
        return false;
    }
    if (a == b) {
        return true;
    }
    if (a->info.image != b->info.image) {
        return false;
    }
    return a->info.image ? a->base && b->base && file_identity(a->base, b->base)
                         : zfs_native_same(a->device, b->device);
}

bool zfs_core_unchanged(zfs_disk* d)
{
    return d && !d->info.failed && (!d->info.image || same_image(d));
}

zfs_status zfs_core_guard(zfs_disk* d, bool writing, zfs_error* e)
{
    if (!d || d->info.new_image || d->info.failed) {
        return zfs_error_set(e, ZFS_INVALID, "An existing healthy target is required");
    }
    if (d->guarded) {
        if (!d->info.image && writing && !d->guard_write) {
            return zfs_error_set(e, ZFS_BUSY, "Reopen this read-protected device before using it as a target");
        }
        return ZFS_OK;
    }
    if (d->info.image && !same_image(d)) {
        return zfs_error_set(e, ZFS_BUSY, "Image changed externally; reopen it");
    }
    zfs_native fd = d->info.image ? image_handle(d->base) : d->device;
    zfs_status s = zfs_native_guard(&fd, d->info.path, d->info.image, writing, e);
    if (!d->info.image) {
        d->device = fd;
    }
    if (!s) {
        d->guarded = true;
        d->guard_write = writing;
    }
    return s;
}

zfs_status zfs_core_clone(zfs_disk* d, zfs_disk** view, zfs_error* e)
{
    zfs_status s = allocate_disk(d->info.path, d->info.size, d->info.image, view, e);
    if (s) {
        return s;
    }
    zfs_disk* v = *view;
    v->info = d->info;
    v->original = d->original;
    v->stamp = d->stamp;
    v->base = d->base;
    v->device = d->device;
    v->borrowed_base = true;
    v->callbacks = d->callbacks;
    return ZFS_OK;
}

void zfs_core_attach(zfs_disk* target, zfs_disk* view)
{
    FILE* overlay = target->overlay;
    target->overlay = view->overlay;
    view->overlay = overlay;
    unsigned char* bitmap = target->dirty;
    target->dirty = view->dirty;
    view->dirty = bitmap;
    target->dirty_count = view->dirty_count;
    view->dirty_count = 0;
    target->info = view->info;
    target->verified_apply = true;
    ++target->revision;
    memset(target->usage_cached, 0, sizeof(target->usage_cached));
}

zfs_status zfs_core_read(zfs_disk* d, uint64_t offset, void* buffer, size_t size, zfs_error* e)
{
    return bytes_io(d, offset, buffer, size, false, e);
}

zfs_status zfs_core_stage_raw(zfs_disk* d, uint64_t offset, zfs_disk* source, uint64_t size, zfs_error* e)
{
    zfs_status s = begin(d, e);
    if (s) {
        return s;
    }
    unsigned char incoming[65536], current[65536];
    for (uint64_t at = 0; at < size && !s; at += sizeof(incoming)) {
        size_t n = size - at < sizeof(incoming) ? (size_t) (size - at) : sizeof(incoming);
        s = bytes_io(source, at, incoming, n, false, e);
        if (!s) {
            s = bytes_io(d, offset + at, current, n, false, e);
        }
        if (!s && memcmp(incoming, current, n)) {
            s = bytes_io(d, offset + at, incoming, n, true, e);
        }
        progress(d, at + n, size);
    }
    s = finish(d, s, e);
    if (!s) {
        bool pending = d->info.pending;
        s = parse(d, e);
        d->info.pending = pending;
    }
    return s;
}

zfs_status zfs_core_copy_metadata(zfs_disk* source, int sp, zfs_disk* target, int tp, const char* path, zfs_error* e)
{
    filesystem *from = NULL, *to = NULL;
    zfs_status s = fs_open(source, sp, &from, e);
    if (s) {
        return s;
    }
    s = fs_open(target, tp, &to, e);
    if (s) {
        free(from);
        return s;
    }
    zealfs_fd_t a, b;
    s = fs_error(zealfs_stat(path, &from->fs, &a), e);
    if (!s) {
        s = fs_error(zealfs_stat(path, &to->fs, &b), e);
    }
    if (!s) {
        s = begin(target, e);
        if (!s) {
            memcpy(b.entry.year, a.entry.year, offsetof(zealfs_entry_t, reserved) - offsetof(zealfs_entry_t, year));
            s = finish(target, fs_error((int) fs_write(to, &b.entry, b.entry_addr, sizeof(b.entry)), e), e);
        }
    }
    free(from);
    free(to);
    return s;
}

zfs_status zfs_core_copy_file(zfs_disk* source, int sp, zfs_disk* target, int tp, const char* path, zfs_error* e)
{
    filesystem* from = NULL;
    zfs_status s = fs_open(source, sp, &from, e);
    if (s) {
        return s;
    }
    zealfs_fd_t fd;
    s = fs_error(zealfs_open(path, &from->fs, &fd), e);
    FILE* spool = !s ? tmpfile() : NULL;
    if (!s && !spool) {
        s = sys_error(e, "Cannot create transfer spool");
    }
    unsigned char buffer[65536];
    for (uint64_t at = 0; !s && at < fd.entry.size;) {
        size_t n = fd.entry.size - at < sizeof(buffer) ? (size_t) (fd.entry.size - at) : sizeof(buffer);
        int r = zealfs_read(&from->fs, &fd, buffer, n, (int64_t) at);
        if (r != (int) n) {
            s = r < 0 ? fs_error(r, e) : zfs_error_set(e, ZFS_IO, "Short source read");
        } else if (fwrite(buffer, 1, n, spool) != n) {
            s = sys_error(e, "Transfer spool write failed");
        }
        at += n;
    }
    if (!s && zfs_seek(spool, 0)) {
        s = sys_error(e, "Transfer spool seek failed");
    }
    if (!s) {
        s = create_file(target, tp, path, spool, NULL, fd.entry.size, e);
    }
    if (spool) {
        fclose(spool);
    }
    free(from);
    if (!s) {
        s = zfs_core_copy_metadata(source, sp, target, tp, path, e);
    }
    return s;
}

zfs_status zfs_core_files_equal(zfs_disk* a, int ap, zfs_disk* b, int bp, const char* path, bool* equal, zfs_error* e)
{
    filesystem *x = NULL, *y = NULL;
    *equal = false;
    zfs_status s = fs_open(a, ap, &x, e);
    if (s) {
        return s;
    }
    if (b) {
        s = fs_open(b, bp, &y, e);
        if (s) {
            free(x);
            return s;
        }
    }
    zealfs_fd_t xf, yf;
    s = fs_error(zealfs_open(path, &x->fs, &xf), e);
    if (!s && b) {
        s = fs_error(zealfs_open(path, &y->fs, &yf), e);
    }
    if (!s && b && xf.entry.size != yf.entry.size) {
        free(x);
        free(y);
        return ZFS_OK;
    }
    unsigned char xb[65536], yb[65536];
    bool same = true;
    for (uint64_t at = 0; !s && at < xf.entry.size;) {
        size_t n = xf.entry.size - at < sizeof(xb) ? (size_t) (xf.entry.size - at) : sizeof(xb);
        int r = zealfs_read(&x->fs, &xf, xb, n, (int64_t) at);
        if (r != (int) n) {
            s = r < 0 ? fs_error(r, e) : zfs_error_set(e, ZFS_IO, "Short file read");
        }
        if (!s && b) {
            r = zealfs_read(&y->fs, &yf, yb, n, (int64_t) at);
            if (r != (int) n) {
                s = r < 0 ? fs_error(r, e) : zfs_error_set(e, ZFS_IO, "Short file read");
            } else if (memcmp(xb, yb, n)) {
                same = false;
            }
        }
        at += n;
    }
    *equal = !s && same;
    free(x);
    free(y);
    return s;
}

zfs_status zfs_core_check_file(zfs_disk* d, int part, const char* path, zfs_error* e)
{
    bool equal;
    return zfs_core_files_equal(d, part, NULL, 0, path, &equal, e);
}

const char* zfs_recovery_backup(const zfs_disk* d)
{
    return d && d->recovery_backup[0] ? d->recovery_backup : NULL;
}

int zfs_recovery_partition(const zfs_disk* d)
{
    return d ? d->recovery_partition : -1;
}
