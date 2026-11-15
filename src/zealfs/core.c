/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
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

/* Byte offsets in the 512-byte PC MBR sector. Each of its four partition
 * entries contains a boot flag, CHS addresses, type, start LBA and sector count. */
#define MBR_PARTITION_TABLE_OFFSET 446
#define MBR_PARTITION_ENTRY_SIZE 16
#define MBR_BOOT_FLAG_OFFSET 0
#define MBR_BOOTABLE_FLAG 0x80
#define MBR_START_CHS_OFFSET 1
#define MBR_TYPE_OFFSET 4
#define MBR_END_CHS_OFFSET 5
#define MBR_CHS_SIZE 3
#define MBR_CHS_UNUSED 0xff
#define MBR_START_LBA_OFFSET 8
#define MBR_SECTOR_COUNT_OFFSET 12
#define MBR_SIGNATURE_OFFSET 510
#define MBR_SIGNATURE_LOW 0x55
#define MBR_SIGNATURE_HIGH 0xaa

/* Names for bytes_io's writing flag at call sites. */
static const bool DISK_READ = false;
static const bool DISK_WRITE = true;

typedef struct {
    uint64_t sector;
    bool dirty;
    uint8_t data[DISK_SECTOR_SIZE];
} undo_record;

typedef struct {
    zfs_disk* disk;
    zfs_partition part;
    zealfs_context_t fs;
} filesystem;

/* Private consumers share the chunk reader; only comparisons need a second buffer. */
typedef zfs_status (*file_chunk_fn)(void* user, const uint8_t* data, size_t size, uint64_t offset, zfs_error* error);

typedef struct {
    file_chunk_fn consume;
    void* user;
    const char* read_error;
    bool report_progress;
} file_chunk_consumer;

typedef struct {
    FILE* file;
    const char* write_error;
} file_output;

typedef struct {
    filesystem* target_fs;
    zealfs_fd_t* target_file;
    bool same;
} file_comparison;

static uint32_t le32(const uint8_t* bytes)
{
    return (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8) | ((uint32_t) bytes[2] << 16) | ((uint32_t) bytes[3] << 24);
}

static void put32(uint8_t* bytes, uint32_t value)
{
    for (unsigned i = 0; i < sizeof(value); ++i) {
        bytes[i] = (uint8_t) (value >> (i * 8));
    }
}

const char* zfs_status_string(zfs_status status)
{
    static const char* names[] = {
        "success",
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
        "type conflict",
    };
    return (unsigned) status < sizeof(names) / sizeof(*names) ? names[status] : "unknown error";
}

zfs_status zfs_error_set(zfs_error* error, zfs_status code, const char* format, ...)
{
    if (error != NULL) {
        error->code = code;
        va_list args;
        va_start(args, format);
        vsnprintf(error->message, sizeof(error->message), format, args);
        va_end(args);
    }
    return code;
}

static zfs_status sys_error(zfs_error* error, const char* operation)
{
    int error_number = errno;
    zfs_status status = error_number == EACCES || error_number == EPERM ? ZFS_PERMISSION
                        : error_number == ENOENT                        ? ZFS_NOT_FOUND
                        : error_number == ENOSPC                        ? ZFS_NO_SPACE
                        : error_number == ENOMEM                        ? ZFS_NO_MEMORY
                                                                        : ZFS_IO;
    return zfs_error_set(error, status, "%s: %s", operation, strerror(error_number));
}

/* One bit per sector selects the staged overlay instead of the original media. */
static bool dirty(zfs_disk* disk, uint64_t sector)
{
    return (disk->dirty[sector / 8] & (1u << (sector % 8))) != 0;
}

static void set_dirty(zfs_disk* disk, uint64_t sector, bool value)
{
    bool old = dirty(disk, sector);
    if (value) {
        disk->dirty[sector / 8] |= (uint8_t) (1u << (sector % 8));
    } else {
        disk->dirty[sector / 8] &= (uint8_t) ~(1u << (sector % 8));
    }
    if (old != value) {
        if (value) {
            ++disk->dirty_count;
        } else {
            --disk->dirty_count;
        }
    }
}

static zfs_status read_sector(zfs_disk* disk, uint64_t sector, void* buffer, zfs_error* error)
{
    if (sector >= disk->info.size / DISK_SECTOR_SIZE) {
        return zfs_error_set(error, ZFS_INVALID, "Read outside disk");
    }
    FILE* file = dirty(disk, sector) ? disk->overlay : disk->base;
    if (file != NULL) {
        if (zfs_seek(file, sector * DISK_SECTOR_SIZE) != 0 ||
            fread(buffer, 1, DISK_SECTOR_SIZE, file) != DISK_SECTOR_SIZE) {
            return zfs_error_set(error, ZFS_IO, "Short read from image or staging storage");
        }
    } else if (disk->info.image) {
        memset(buffer, 0, DISK_SECTOR_SIZE);
    } else {
        return zfs_native_read(disk->device, sector * DISK_SECTOR_SIZE, buffer, DISK_SECTOR_SIZE, error);
    }
    return ZFS_OK;
}

static zfs_status write_sector(zfs_disk* disk, uint64_t sector, const void* buffer, zfs_error* error)
{
    if (disk->undo == NULL || sector >= disk->info.size / DISK_SECTOR_SIZE) {
        return zfs_error_set(error, ZFS_INVALID, "Write outside transaction or disk");
    }
    undo_record record = {
        .sector = sector,
        .dirty = dirty(disk, sector),
    };
    zfs_status status = read_sector(disk, sector, record.data, error);
    if (status != ZFS_OK) {
        return status;
    }
    if (zfs_seek(disk->undo, disk->undo_count * sizeof(record)) != 0 ||
        fwrite(&record, 1, sizeof(record), disk->undo) != sizeof(record)) {
        return sys_error(error, "Journal write failed");
    }
    ++disk->undo_count;
    /* Set dirty before writing so rollback also repairs a partially written sector. */
    set_dirty(disk, sector, true);
    if (zfs_seek(disk->overlay, sector * DISK_SECTOR_SIZE) != 0 ||
        fwrite(buffer, 1, DISK_SECTOR_SIZE, disk->overlay) != DISK_SECTOR_SIZE) {
        return sys_error(error, "Staging write failed");
    }
    return ZFS_OK;
}

static zfs_status bytes_io(zfs_disk* disk, uint64_t address, void* buffer, size_t size, bool writing, zfs_error* error)
{
    if (address > disk->info.size || size > disk->info.size - address) {
        return zfs_error_set(error, ZFS_INVALID, "I/O exceeds disk size");
    }
    uint8_t sector[DISK_SECTOR_SIZE];
    uint8_t* data = buffer;
    while (size != 0) {
        /* Check whether the address is sector-aligned. */
        size_t offset = (size_t) (address % DISK_SECTOR_SIZE);
        size_t chunk_size = DISK_SECTOR_SIZE - offset;
        if (chunk_size > size) {
            chunk_size = size;
        }
        /* Read the sector from the disk */
        zfs_status status = read_sector(disk, address / DISK_SECTOR_SIZE, sector, error);
        if (status != ZFS_OK) {
            return status;
        }
        if (writing) {
            // Update the buffer with the new data
            memcpy(sector + offset, data, chunk_size);
            /* Write it back */
            status = write_sector(disk, address / DISK_SECTOR_SIZE, sector, error);
            if (status != ZFS_OK) {
                return status;
            }
        } else {
            /* Copy the relevant part of the sector to the buffer */
            memcpy(data, sector + offset, chunk_size);
        }
        /* Update the buffer, address, and size */
        address += chunk_size;
        data += chunk_size;
        size -= chunk_size;
    }
    return ZFS_OK;
}

/* Start one API operation's undo journal. Sector writes still go only to the
 * overlay; a successful operation keeps them staged until zfs_apply. */
static zfs_status begin(zfs_disk* disk, zfs_error* error)
{
    if (disk == NULL) {
        return zfs_error_set(error, ZFS_INVALID, "No disk is open");
    }
    if (disk->info.failed) {
        return zfs_error_set(error, ZFS_PARTIAL_WRITE, "Session failed; reopen the target before making changes");
    }
    if (disk->undo != NULL) {
        return zfs_error_set(error, ZFS_BUSY, "Operation already active");
    }
    memset(disk->usage_cached, 0, sizeof(disk->usage_cached));
    disk->undo = tmpfile();
    disk->undo_count = 0;
    return disk->undo != NULL ? ZFS_OK : sys_error(error, "Cannot create operation journal");
}

/* On failure, replay before-images backwards so repeated writes to a sector
 * unwind to its state before this operation, preserving earlier staged edits.
 * A sector previously read from the base becomes clean again; its stale overlay
 * bytes are ignored. Failed rollback makes the session unusable. */
static zfs_status finish(zfs_disk* disk, zfs_status status, zfs_error* error)
{
    if (status != ZFS_OK) {
        clearerr(disk->overlay);
        clearerr(disk->undo);
        while (disk->undo_count != 0) {
            undo_record record;
            --disk->undo_count;
            if (zfs_seek(disk->undo, disk->undo_count * sizeof(record)) != 0 ||
                fread(&record, 1, sizeof(record), disk->undo) != sizeof(record) ||
                (record.dirty && (zfs_seek(disk->overlay, record.sector * DISK_SECTOR_SIZE) != 0 ||
                                  fwrite(record.data, 1, DISK_SECTOR_SIZE, disk->overlay) != DISK_SECTOR_SIZE))) {
                disk->info.failed = true;
                status = zfs_error_set(error, ZFS_IO, "Staging rollback failed; close and reopen the session");
                break;
            }
            set_dirty(disk, record.sector, record.dirty);
        }
    }
    fclose(disk->undo);
    disk->undo = NULL;
    disk->undo_count = 0;
    disk->info.pending = disk->info.new_image || disk->dirty_count > 0;
    if (status == ZFS_OK) {
        ++disk->revision;
    }
    return status;
}

static zfs_status parse(zfs_disk* disk, zfs_error* error)
{
    uint8_t sector[DISK_SECTOR_SIZE];
    /* Read MBR */
    zfs_status status = read_sector(disk, 0, sector, error);
    if (status != ZFS_OK) {
        return status;
    }
    /* A raw ZealFS header takes precedence over incidental 55aa bytes in its
     * root entries/FAT; partition backups are raw images, not MBR sectors. */
    const zealfs_header_t* header = (const zealfs_header_t*) sector;
    bool raw = header->magic == ZEALFS_V2_MAGIC && header->version == ZEALFS_V2_VERSION &&
               header->page_size <= ZEALFS_V2_MAX_PAGE_SHIFT;
    disk->info.has_mbr = !raw && sector[MBR_SIGNATURE_OFFSET] == MBR_SIGNATURE_LOW &&
                         sector[MBR_SIGNATURE_OFFSET + 1] == MBR_SIGNATURE_HIGH;
    memset(disk->info.partitions, 0, sizeof(disk->info.partitions));
    if (!disk->info.has_mbr) {
        if (header->magic == ZEALFS_V2_MAGIC && header->version == ZEALFS_V2_VERSION) {
            disk->info.partitions[0] = (zfs_partition){
                true,
                ZEALFS_TYPE,
                0,
                (uint32_t) (disk->info.size / DISK_SECTOR_SIZE),
            };
        }
        return ZFS_OK;
    }
    /* Inspect every MBR slot, including non-ZealFS partitions, so allocating a
     * ZealFS partition cannot overwrite another filesystem's occupied range. */
    for (unsigned i = 0; i < ZFS_MAX_PARTITIONS; ++i) {
        const uint8_t* partition_bytes = sector + MBR_PARTITION_TABLE_OFFSET + i * MBR_PARTITION_ENTRY_SIZE;
        zfs_partition* part = &disk->info.partitions[i];
        /* Treat any bootable flag, nonzero type, start LBA or length as occupied.
         * This deliberately retains suspicious entries for validation below;
         * considering only the bootable flag would erase ordinary data partitions. */
        *part = (zfs_partition){
            (partition_bytes[MBR_BOOT_FLAG_OFFSET] & MBR_BOOTABLE_FLAG) != 0 || partition_bytes[MBR_TYPE_OFFSET] != 0 ||
                le32(partition_bytes + MBR_START_LBA_OFFSET) != 0 ||
                le32(partition_bytes + MBR_SECTOR_COUNT_OFFSET) != 0,
            partition_bytes[MBR_TYPE_OFFSET],
            le32(partition_bytes + MBR_START_LBA_OFFSET),
            le32(partition_bytes + MBR_SECTOR_COUNT_OFFSET),
        };
        /* Be very conservative to make sure nothing is erased! */
        if (part->active && (part->sectors == 0 || part->start_lba == 0 ||
                             (uint64_t) part->start_lba + part->sectors > disk->info.size / DISK_SECTOR_SIZE)) {
            return zfs_error_set(error, ZFS_CORRUPT, "Partition %u lies outside the disk", i + 1);
        }
        for (unsigned j = 0; j < i; ++j) {
            zfs_partition* previous_partition = &disk->info.partitions[j];
            if (part->active && previous_partition->active &&
                (uint64_t) part->start_lba < (uint64_t) previous_partition->start_lba + previous_partition->sectors &&
                (uint64_t) previous_partition->start_lba < (uint64_t) part->start_lba + part->sectors) {
                return zfs_error_set(error, ZFS_CORRUPT, "Overlapping partitions");
            }
        }
    }
    return ZFS_OK;
}

static zfs_status allocate_disk(const char* path, uint64_t size, bool image, zfs_disk** out, zfs_error* error)
{
    if (path == NULL || *path == 0 || out == NULL || strlen(path) >= ZFS_PATH_MAX - 64 || size < DISK_SECTOR_SIZE ||
        size > ZFS_MAX_DISK_SIZE || size % DISK_SECTOR_SIZE != 0) {
        return zfs_error_set(error, ZFS_INVALID, "Invalid path or disk size (512-byte alignment, maximum 64 GiB)");
    }
    zfs_disk* disk = calloc(1, sizeof(*disk));
    if (disk == NULL) {
        return zfs_error_set(error, ZFS_NO_MEMORY, "Cannot allocate disk session");
    }
    disk->device = ZFS_NATIVE_INVALID;
    disk->info.size = size;
    disk->info.image = image;
    if (!image || !zfs_host_resolve(path, disk->info.path, sizeof(disk->info.path))) {
        strcpy(disk->info.path, path);
    }
    disk->bitmap_size = (size_t) ((size / DISK_SECTOR_SIZE + 7) / 8);
    disk->dirty = calloc(1, disk->bitmap_size);
    /* Sparse sector overlay at original disk offsets. Reads prefer its dirty
     * sectors; all other sectors come from the original image/device (or zeros
     * for a new image). Only Apply writes/publishes the merged view. */
    disk->overlay = tmpfile();
    if (disk->dirty == NULL || disk->overlay == NULL) {
        zfs_close(disk);
        return zfs_error_set(error, ZFS_NO_MEMORY, "Cannot allocate staging storage");
    }
    *out = disk;
    return ZFS_OK;
}

static void save_stamp(zfs_disk* disk)
{
    fstat(fileno(disk->base), &disk->stamp);
#ifdef _WIN32
    GetFileInformationByHandle((HANDLE) _get_osfhandle(_fileno(disk->base)), &disk->win_stamp);
#endif
}

zfs_status zfs_open(const char* path, bool image, zfs_disk** out, zfs_error* error)
{
    if (out != NULL) {
        *out = NULL;
    }
    if (path == NULL || out == NULL) {
        return zfs_error_set(error, ZFS_INVALID, "A target path is required");
    }
    FILE* base = NULL;
    zfs_native native = ZFS_NATIVE_INVALID;
    uint64_t size = 0;
    if (image) {
#ifndef _WIN32
        struct stat link_stat;
        if (lstat(path, &link_stat) == 0 && S_ISLNK(link_stat.st_mode)) {
            return zfs_error_set(error, ZFS_INVALID, "Open the actual image path instead of a symbolic link");
        }
#endif
        base = zfs_host_open(path, "rb");
        if (base == NULL) {
            return sys_error(error, "Cannot open image");
        }
        struct stat st;
        if (fstat(fileno(base), &st) != 0 || (st.st_mode & S_IFREG) == 0 || st.st_size < 0) {
            fclose(base);
            return zfs_error_set(error, ZFS_INVALID, "Target is not a regular image file");
        }
        size = (uint64_t) st.st_size;
    } else {
        zfs_status status = zfs_native_open(path, false, &native, &size, error);
        if (status != ZFS_OK) {
            return status;
        }
    }
    zfs_disk* disk = NULL;
    zfs_status status = allocate_disk(path, size, image, &disk, error);
    if (status != ZFS_OK) {
        if (base != NULL) {
            fclose(base);
        }
        zfs_native_close(native);
        return status;
    }
    disk->base = base;
    disk->device = native;
    if (base != NULL) {
        save_stamp(disk);
    }
    status = parse(disk, error);
    if (status != ZFS_OK) {
        zfs_close(disk);
        return status;
    }
    disk->original = disk->info;
    *out = disk;
    return ZFS_OK;
}

zfs_status zfs_image_create(const char* path, uint64_t size, bool mbr, zfs_disk** out, zfs_error* error)
{
    if (out != NULL) {
        *out = NULL;
    }
    if (path == NULL || out == NULL) {
        return zfs_error_set(error, ZFS_INVALID, "A target path is required");
    }
    FILE* exists = zfs_host_open(path, "rb");
    if (exists != NULL) {
        fclose(exists);
        return zfs_error_set(error, ZFS_EXISTS, "Image destination already exists");
    }
    zfs_disk* disk = NULL;
    zfs_status status = allocate_disk(path, size, true, &disk, error);
    if (status != ZFS_OK) {
        return status;
    }
    disk->info.new_image = true;
    disk->info.pending = true;
    disk->original = disk->info;
    disk->original.pending = false;
    if (mbr) {
        status = zfs_mbr_create(disk, error);
    }
    if (status != ZFS_OK) {
        zfs_close(disk);
        return status;
    }
    *out = disk;
    return ZFS_OK;
}

void zfs_close(zfs_disk* disk)
{
    if (disk == NULL) {
        return;
    }
    if (disk->base != NULL && !disk->borrowed_base) {
        fclose(disk->base);
    }
    if (disk->overlay != NULL) {
        fclose(disk->overlay);
    }
    if (disk->undo != NULL) {
        fclose(disk->undo);
    }
    if (!disk->borrowed_base) {
        zfs_native_close(disk->device);
    }
    free(disk->dirty);
    free(disk);
}

void zfs_set_callbacks(zfs_disk* disk, zfs_callbacks callbacks)
{
    if (disk != NULL) {
        disk->callbacks = callbacks;
    }
}

const zfs_info* zfs_get_info(const zfs_disk* disk)
{
    return disk != NULL ? &disk->info : NULL;
}

static void progress(zfs_disk* disk, uint64_t n, uint64_t total)
{
    if (disk->callbacks.progress != NULL) {
        disk->callbacks.progress(disk->callbacks.user, n, total);
    }
}

zfs_status zfs_discard(zfs_disk* disk, zfs_error* error)
{
    if (disk == NULL) {
        return zfs_error_set(error, ZFS_INVALID, "No disk is open");
    }
    if (disk->info.failed) {
        return zfs_error_set(error, ZFS_PARTIAL_WRITE, "Reopen failed session; discard cannot undo physical writes");
    }
    FILE* fresh = tmpfile();
    if (fresh == NULL) {
        return sys_error(error, "Cannot reset staging storage");
    }
    fclose(disk->overlay);
    disk->overlay = fresh;
    memset(disk->dirty, 0, disk->bitmap_size);
    disk->dirty_count = 0;
    disk->info = disk->original;
    memset(disk->usage_cached, 0, sizeof(disk->usage_cached));
    /* Discard cancels creation; a later mutation can stage it again. */
    disk->info.pending = false;
    disk->verified_apply = false;
    disk->recovery_backup[0] = 0;
    ++disk->revision;
    return ZFS_OK;
}

static bool file_identity(FILE* first_file, FILE* second_file)
{
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION first_info;
    BY_HANDLE_FILE_INFORMATION second_info;
    return GetFileInformationByHandle((HANDLE) _get_osfhandle(_fileno(first_file)), &first_info) != 0 &&
           GetFileInformationByHandle((HANDLE) _get_osfhandle(_fileno(second_file)), &second_info) != 0 &&
           first_info.dwVolumeSerialNumber == second_info.dwVolumeSerialNumber &&
           first_info.nFileIndexHigh == second_info.nFileIndexHigh &&
           first_info.nFileIndexLow == second_info.nFileIndexLow;
#else
    struct stat first_info;
    struct stat second_info;
    return fstat(fileno(first_file), &first_info) == 0 && fstat(fileno(second_file), &second_info) == 0 &&
           first_info.st_dev == second_info.st_dev && first_info.st_ino == second_info.st_ino;
#endif
}

static bool same_image(zfs_disk* disk)
{
    FILE* file = zfs_host_open(disk->info.path, "rb");
    struct stat st;
    bool same = file != NULL && fstat(fileno(file), &st) == 0 && st.st_dev == disk->stamp.st_dev &&
                st.st_ino == disk->stamp.st_ino && st.st_size == disk->stamp.st_size &&
                st.st_mtime == disk->stamp.st_mtime;
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION current;
    if (same) {
        same = GetFileInformationByHandle((HANDLE) _get_osfhandle(_fileno(file)), &current) != 0 &&
               current.dwVolumeSerialNumber == disk->win_stamp.dwVolumeSerialNumber &&
               current.nFileIndexHigh == disk->win_stamp.nFileIndexHigh &&
               current.nFileIndexLow == disk->win_stamp.nFileIndexLow &&
               CompareFileTime(&current.ftLastWriteTime, &disk->win_stamp.ftLastWriteTime) == 0;
    }
#else
#ifdef __APPLE__
    if (same) {
        same = st.st_mtimespec.tv_nsec == disk->stamp.st_mtimespec.tv_nsec;
    }
#else
    if (same) {
        same = st.st_mtim.tv_nsec == disk->stamp.st_mtim.tv_nsec;
    }
#endif
#endif
    if (file != NULL) {
        fclose(file);
    }
    return same;
}

static zfs_native image_handle(FILE* file);

static zfs_status verify_staged(zfs_disk* disk, FILE* file, zfs_native device, zfs_error* error)
{
    uint8_t expected[DISK_SECTOR_SIZE];
    uint8_t actual[DISK_SECTOR_SIZE];
    for (uint64_t i = 0; i < disk->info.size / DISK_SECTOR_SIZE; ++i) {
        if (dirty(disk, i)) {
            zfs_status status = read_sector(disk, i, expected, error);
            if (status != ZFS_OK) {
                return status;
            }
            if (file != NULL) {
                if (zfs_seek(file, i * DISK_SECTOR_SIZE) != 0 ||
                    fread(actual, 1, DISK_SECTOR_SIZE, file) != DISK_SECTOR_SIZE) {
                    return zfs_error_set(error, ZFS_IO, "Cannot read back staged image");
                }
            } else {
                status = zfs_native_read(device, i * DISK_SECTOR_SIZE, actual, DISK_SECTOR_SIZE, error);
                if (status != ZFS_OK) {
                    return status;
                }
            }
            if (memcmp(expected, actual, DISK_SECTOR_SIZE) != 0) {
                return zfs_error_set(error, ZFS_IO, "Write verification failed at sector %llu", (unsigned long long) i);
            }
        }
    }
    return ZFS_OK;
}

static zfs_status verify_original_device(zfs_disk* disk, zfs_error* error)
{
    FILE* backup = zfs_host_open(disk->recovery_backup, "rb");
    if (backup == NULL) {
        return zfs_error_set(error, ZFS_IO, "Cannot open retained backup for pre-commit verification");
    }
    uint8_t original[ZFS_TRANSFER_BUFFER_SIZE];
    uint8_t actual[ZFS_TRANSFER_BUFFER_SIZE];
    zfs_status status = ZFS_OK;
    for (uint64_t at = 0; status == ZFS_OK && at < disk->recovery_size; at += sizeof(original)) {
        size_t n = (size_t) MIN(disk->recovery_size - at, sizeof(original));
        if (fread(original, 1, n, backup) != n) {
            status = zfs_error_set(error, ZFS_IO, "Retained backup is truncated or unreadable");
        }
        if (status == ZFS_OK) {
            status = zfs_native_read(disk->device, disk->recovery_offset + at, actual, n, error);
        }
        if (status == ZFS_OK && memcmp(original, actual, n) != 0) {
            status = zfs_error_set(error,
                                   ZFS_BUSY,
                                   "Target or backup changed since preparation; no target sectors were written");
        }
    }
    if (status == ZFS_OK && fgetc(backup) != EOF) {
        status = zfs_error_set(error, ZFS_INVALID, "Retained backup size changed");
    }
    fclose(backup);
    return status;
}

zfs_status zfs_apply(zfs_disk* disk, zfs_error* error)
{
    if (disk == NULL) {
        return zfs_error_set(error, ZFS_INVALID, "No disk is open");
    }
    if (disk->info.failed) {
        return zfs_error_set(error, ZFS_PARTIAL_WRITE, "Reopen failed session before applying");
    }
    if (!disk->info.pending) {
        return ZFS_OK;
    }
    zfs_status status = ZFS_OK;
    if (disk->info.image) {
        if (!disk->info.new_image) {
            status = zfs_core_guard(disk, true, error);
            if (status != ZFS_OK) {
                return status;
            }
        }
        char temporary[ZFS_PATH_MAX];
        FILE* dest = zfs_temp_sibling(disk->info.path, temporary, sizeof(temporary));
        if (dest == NULL) {
            return sys_error(error, "Cannot create temporary destination image");
        }
        uint8_t buffer[ZFS_TRANSFER_BUFFER_SIZE];
        for (uint64_t offset = 0; offset < disk->info.size && status == ZFS_OK; offset += sizeof(buffer)) {
            size_t n = (size_t) MIN(disk->info.size - offset, sizeof(buffer));
            status = bytes_io(disk, offset, buffer, n, DISK_READ, error);
            if (status == ZFS_OK && fwrite(buffer, 1, n, dest) != n) {
                status = sys_error(error, "Cannot write replacement image");
            }
            progress(disk, offset + n, disk->info.size);
        }
        if (status == ZFS_OK && zfs_host_sync(dest) != 0) {
            status = sys_error(error, "Cannot flush replacement image");
        }
        if (status == ZFS_OK && disk->verified_apply) {
            status = verify_staged(disk, dest, ZFS_NATIVE_INVALID, error);
        }
        if (status == ZFS_OK && disk->guarded) {
            zfs_native fd = image_handle(dest);
            status = zfs_native_guard(&fd, temporary, true, true, error);
        }
        if (status == ZFS_OK && !disk->info.new_image && !same_image(disk)) {
            status = zfs_error_set(error, ZFS_BUSY, "Image changed while preparing Apply");
        }
        if (status == ZFS_OK) {
            if (disk->base != NULL) {
                fclose(disk->base);
                disk->base = NULL;
            }
            if (zfs_host_publish(temporary, disk->info.path, !disk->info.new_image) != 0) {
#ifdef _WIN32
                status = zfs_error_set(error,
                                       ZFS_IO,
                                       "Cannot publish image (Windows error %lu)",
                                       (unsigned long) GetLastError());
#else
                status =
                    zfs_error_set(error, ZFS_IO, "Cannot publish image (destination exists or replacement failed)");
#endif
                if (!disk->info.new_image) {
                    disk->base = zfs_host_open(disk->info.path, "rb");
                    disk->guarded = false;
                    if (disk->base == NULL || zfs_core_guard(disk, true, NULL) != ZFS_OK) {
                        disk->info.failed = true;
                    }
                }
            } else {
                disk->base = dest;
                dest = NULL;
            }
        }
        if (dest != NULL) {
            fclose(dest);
        }
        if (status != ZFS_OK) {
            zfs_host_remove(temporary);
            return status;
        }
        save_stamp(disk);
    } else {
        bool held = disk->guarded && disk->guard_write;
        zfs_native fd = held ? disk->device : ZFS_NATIVE_INVALID;
        uint64_t size = disk->info.size;
        if (!held) {
            /* Reopen the disk to write it back */
            status = zfs_native_open(disk->info.path, true, &fd, &size, error);
        }
        if (status != ZFS_OK) {
            return status;
        }
        if (size != disk->info.size || !zfs_native_same(disk->device, fd)) {
            if (!held) {
                zfs_native_close(fd);
            }
            return zfs_error_set(error, ZFS_BUSY, "Device identity or size changed");
        }
        if (disk->verified_apply && disk->recovery_is_merge) {
            status = verify_original_device(disk, error);
        }
        if (status != ZFS_OK) {
            if (!held) {
                zfs_native_close(fd);
            }
            return status;
        }
        uint8_t sector[DISK_SECTOR_SIZE];
        uint64_t completed = 0;
        for (uint64_t i = 0; i < disk->info.size / DISK_SECTOR_SIZE && status == ZFS_OK; ++i) {
            if (dirty(disk, i)) {
                status = read_sector(disk, i, sector, error);
                if (status == ZFS_OK) {
                    status = zfs_native_write(fd, i * DISK_SECTOR_SIZE, sector, DISK_SECTOR_SIZE, error);
                }
                progress(disk, ++completed, disk->dirty_count);
            }
        }
        if (status == ZFS_OK) {
            status = zfs_native_sync(fd, error);
        }
        if (status == ZFS_OK && disk->verified_apply) {
            status = verify_staged(disk, NULL, fd, error);
        }
        if (!held) {
            zfs_native_close(fd);
        }
        if (status != ZFS_OK) {
            disk->info.failed = true;
            return zfs_error_set(error,
                                 ZFS_PARTIAL_WRITE,
                                 "Apply or readback failed; device may be partially updated. Reopen it "
                                 "before restoring from the retained backup");
        }
    }
    /* Apply the changes in RAM too */
    disk->info.new_image = false;
    disk->info.pending = false;
    disk->original = disk->info;
    memset(disk->dirty, 0, disk->bitmap_size);
    disk->dirty_count = 0;
    disk->verified_apply = false;
    disk->recovery_is_merge = false;
    ++disk->revision;
    if (disk->callbacks.log != NULL) {
        disk->callbacks.log(disk->callbacks.user, "Changes applied");
    }
    return ZFS_OK;
}

static zfs_status mbr_write(zfs_disk* disk, zfs_error* error)
{
    uint8_t mbr[DISK_SECTOR_SIZE];
    zfs_status status = read_sector(disk, 0, mbr, error);
    if (status != ZFS_OK) {
        return status;
    }
    for (int i = 0; i < ZFS_MAX_PARTITIONS; ++i) {
        uint8_t* partition_bytes = mbr + MBR_PARTITION_TABLE_OFFSET + i * MBR_PARTITION_ENTRY_SIZE;
        zfs_partition* part = &disk->info.partitions[i];
        memset(partition_bytes, 0, MBR_PARTITION_ENTRY_SIZE);
        if (part->active) {
            /* CHS fields not used */
            memset(partition_bytes + MBR_START_CHS_OFFSET, MBR_CHS_UNUSED, MBR_CHS_SIZE);
            memset(partition_bytes + MBR_END_CHS_OFFSET, MBR_CHS_UNUSED, MBR_CHS_SIZE);
            /* Partition type */
            partition_bytes[MBR_TYPE_OFFSET] = part->type;
            /* Start LBA */
            put32(partition_bytes + MBR_START_LBA_OFFSET, part->start_lba);
            /* Size in sectors */
            put32(partition_bytes + MBR_SECTOR_COUNT_OFFSET, part->sectors);
        }
    }
    mbr[MBR_SIGNATURE_OFFSET] = MBR_SIGNATURE_LOW;
    mbr[MBR_SIGNATURE_OFFSET + 1] = MBR_SIGNATURE_HIGH;
    return write_sector(disk, 0, mbr, error);
}

zfs_status zfs_mbr_create(zfs_disk* disk, zfs_error* error)
{
    if (disk == NULL || disk->info.has_mbr) {
        return zfs_error_set(error, ZFS_INVALID, "Disk already has an MBR or is not open");
    }
    if (disk->info.partitions[0].active) {
        return zfs_error_set(error, ZFS_BUSY, "Delete the raw partition before creating an MBR");
    }
    zfs_status status = begin(disk, error);
    if (status != ZFS_OK) {
        return status;
    }
    /* Reset the MBR and set the signature only */
    uint8_t sector[DISK_SECTOR_SIZE] = {0};
    sector[MBR_SIGNATURE_OFFSET] = MBR_SIGNATURE_LOW;
    sector[MBR_SIGNATURE_OFFSET + 1] = MBR_SIGNATURE_HIGH;
    status = write_sector(disk, 0, sector, error);
    status = finish(disk, status, error);
    /* Create a mirror for the RAM changes */
    if (status == ZFS_OK) {
        disk->info.has_mbr = true;
    }
    return status;
}

zfs_status zfs_partition_space(zfs_disk* disk, uint32_t alignment, uint64_t* start, uint64_t* size, zfs_error* error)
{
    if (disk == NULL || start == NULL || size == NULL || alignment < DISK_SECTOR_SIZE ||
        (alignment & (alignment - 1)) != 0) {
        return zfs_error_set(error, ZFS_INVALID, "Alignment must be a power of two, at least 512 bytes");
    }
    /* Make sure the first sector is taken (MBR), so start checking at sector 1 when an MBR is present */
    uint64_t cursor = disk->info.has_mbr ? DISK_SECTOR_SIZE : 0;
    uint64_t best = 0;
    uint64_t best_start = 0;
    /* Build a sorted list of active partitions */
    zfs_partition parts[ZFS_MAX_PARTITIONS];
    int n = 0;
    for (int i = 0; i < ZFS_MAX_PARTITIONS; ++i) {
        if (disk->info.partitions[i].active) {
            parts[n++] = disk->info.partitions[i];
        }
    }
    for (int i = 1; i < n; ++i) {
        zfs_partition partition_info = parts[i];
        int j = i;
        while (j > 0 && parts[j - 1].start_lba > partition_info.start_lba) {
            parts[j] = parts[j - 1];
            --j;
        }
        parts[j] = partition_info;
    }
    /* Check all the gaps between the sections and keep the maximum in `best`.
     * Check for free space after the last partition until the end of the disk too. */
    for (int i = 0; i <= n; ++i) {
        uint64_t end = i < n ? (uint64_t) parts[i].start_lba * DISK_SECTOR_SIZE : disk->info.size;
        /* Try to align the address on the given alignment */
        uint64_t aligned = (cursor + alignment - 1) & ~((uint64_t) alignment - 1);
        if (end > aligned && end - aligned > best) {
            best = end - aligned;
            best_start = aligned;
        }
        if (i < n) {
            cursor = ((uint64_t) parts[i].start_lba + parts[i].sectors) * DISK_SECTOR_SIZE;
        }
    }
    if (best > ZEALFS_V2_MAX_PARTITION_SIZE) {
        best = ZEALFS_V2_MAX_PARTITION_SIZE;
    }
    *start = best_start;
    *size = best;
    return ZFS_OK;
}

static zfs_status format(zfs_disk* disk, zfs_partition part, zfs_error* error)
{
    uint64_t size = (uint64_t) part.sectors * DISK_SECTOR_SIZE;
    if (size < ZEALFS_V2_MIN_PARTITION_SIZE || size > ZEALFS_V2_MAX_PARTITION_SIZE) {
        return zfs_error_set(error, ZFS_INVALID, "ZealFS v2 partitions must be between 8 KiB and 4 GiB");
    }
    /* Format the partition with data. We need to allocate 3 pages at all time:
     * - One for the header
     * - Two for the FAT
     */
    size_t bytes = (size_t) zealfsv2_page_size((long long) size) * 3;
    uint8_t* data = calloc(1, bytes);
    if (data == NULL) {
        return zfs_error_set(error, ZFS_NO_MEMORY, "Cannot allocate format buffer");
    }
    zealfsv2_format(data, size);
    zfs_status status = bytes_io(disk, (uint64_t) part.start_lba * DISK_SECTOR_SIZE, data, bytes, DISK_WRITE, error);
    free(data);
    return status;
}

zfs_status zfs_partition_create(zfs_disk* disk, uint64_t size, uint32_t alignment, int* index, zfs_error* error)
{
    uint64_t start;
    uint64_t available;
    zfs_status status = zfs_partition_space(disk, alignment, &start, &available, error);
    if (status != ZFS_OK) {
        return status;
    }
    int slot = -1;
    /* No MBR, only allow the first partition to be used */
    for (int i = 0; i < (disk->info.has_mbr ? ZFS_MAX_PARTITIONS : 1); ++i) {
        /* Find free partition */
        if (!disk->info.partitions[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return zfs_error_set(error, ZFS_NO_SPACE, "No free partition slot");
    }
    if (size == 0) {
        size = available;
    }
    if (size < ZEALFS_V2_MIN_PARTITION_SIZE || size > available || size % DISK_SECTOR_SIZE != 0) {
        return zfs_error_set(error, ZFS_INVALID, "Partition size is not available or not sector-aligned");
    }
    zfs_info backup = disk->info;
    status = begin(disk, error);
    if (status != ZFS_OK) {
        return status;
    }
    disk->info.partitions[slot] = (zfs_partition){
        true,
        ZEALFS_TYPE,
        (uint32_t) (start / DISK_SECTOR_SIZE),
        (uint32_t) (size / DISK_SECTOR_SIZE),
    };
    status = format(disk, disk->info.partitions[slot], error);
    if (status == ZFS_OK && disk->info.has_mbr) {
        /* Encode the partition in the staged MBR */
        status = mbr_write(disk, error);
    }
    status = finish(disk, status, error);
    if (status != ZFS_OK) {
        bool failed = disk->info.failed;
        disk->info = backup;
        disk->info.failed = failed;
    } else if (index != NULL) {
        *index = slot;
    }
    return status;
}

static zfs_status partition_check(zfs_disk* disk, int index, bool filesystem_required, zfs_error* error)
{
    if (disk == NULL || index < 0 || index >= ZFS_MAX_PARTITIONS || !disk->info.partitions[index].active) {
        return zfs_error_set(error, ZFS_INVALID, "Invalid partition");
    }
    if (filesystem_required && disk->info.partitions[index].type != ZEALFS_TYPE) {
        return zfs_error_set(error, ZFS_INVALID, "Partition is not ZealFS v2");
    }
    if (disk->info.failed) {
        return zfs_error_set(error, ZFS_PARTIAL_WRITE, "Reopen failed session");
    }
    return ZFS_OK;
}

zfs_status zfs_partition_format(zfs_disk* disk, int index, zfs_error* error)
{
    zfs_status status = partition_check(disk, index, false, error);
    if (status != ZFS_OK) {
        return status;
    }
    status = begin(disk, error);
    if (status != ZFS_OK) {
        return status;
    }
    zfs_info backup = disk->info;
    disk->info.partitions[index].type = ZEALFS_TYPE;
    status = format(disk, disk->info.partitions[index], error);
    if (status == ZFS_OK && disk->info.has_mbr) {
        status = mbr_write(disk, error);
    }
    status = finish(disk, status, error);
    if (status != ZFS_OK) {
        bool failed = disk->info.failed;
        disk->info = backup;
        disk->info.failed = failed;
    }
    return status;
}

zfs_status zfs_partition_delete(zfs_disk* disk, int index, zfs_error* error)
{
    zfs_status status = partition_check(disk, index, false, error);
    if (status != ZFS_OK) {
        return status;
    }
    status = begin(disk, error);
    if (status != ZFS_OK) {
        return status;
    }
    zfs_info backup = disk->info;
    memset(&disk->info.partitions[index], 0, sizeof(zfs_partition));
    if (disk->info.has_mbr) {
        status = mbr_write(disk, error);
    } else {
        uint8_t sector[DISK_SECTOR_SIZE] = {0};
        status = write_sector(disk, 0, sector, error);
    }
    status = finish(disk, status, error);
    if (status != ZFS_OK) {
        bool failed = disk->info.failed;
        disk->info = backup;
        disk->info.failed = failed;
    }
    return status;
}

static ssize_t fs_read(void* arg, void* buffer, uint32_t address, size_t size)
{
    filesystem* fs = arg;
    if ((uint64_t) address + size > (uint64_t) fs->part.sectors * DISK_SECTOR_SIZE) {
        return -EIO;
    }
    return bytes_io(fs->disk,
                    (uint64_t) fs->part.start_lba * DISK_SECTOR_SIZE + address,
                    buffer,
                    size,
                    DISK_READ,
                    NULL) != ZFS_OK
               ? -EIO
               : (ssize_t) size;
}

static ssize_t fs_write(void* arg, const void* buffer, uint32_t address, size_t size)
{
    filesystem* fs = arg;
    if ((uint64_t) address + size > (uint64_t) fs->part.sectors * DISK_SECTOR_SIZE) {
        return -EIO;
    }
    return bytes_io(fs->disk,
                    (uint64_t) fs->part.start_lba * DISK_SECTOR_SIZE + address,
                    (void*) buffer,
                    size,
                    DISK_WRITE,
                    NULL) != ZFS_OK
               ? -EIO
               : (ssize_t) size;
}

static zfs_status fs_open(zfs_disk* disk, int partition, filesystem** out, zfs_error* error)
{
    zfs_status status = partition_check(disk, partition, true, error);
    if (status != ZFS_OK) {
        return status;
    }
    filesystem* fs = calloc(1, sizeof(*fs));
    if (fs == NULL) {
        return zfs_error_set(error, ZFS_NO_MEMORY, "Cannot allocate filesystem context");
    }
    /* We have to create a context/arg for zealfs functions */
    fs->disk = disk;
    fs->part = disk->info.partitions[partition];
    fs->fs.read = fs_read;
    fs->fs.write = fs_write;
    fs->fs.arg = fs;
    /* Browse the root directory */
    zealfs_fd_t root;
    int entry_count = zealfs_opendir("/", &fs->fs, &root);
    if (entry_count < 0) {
        free(fs);
        return zfs_error_set(error, ZFS_CORRUPT, "Invalid or unreadable ZealFS v2 header");
    }
    *out = fs;
    return ZFS_OK;
}

static zfs_status fs_error(int entry_count, zfs_error* error)
{
    if (entry_count >= 0) {
        return ZFS_OK;
    }
    zfs_status status;
    switch (-entry_count) {
    case ENOENT:
        status = ZFS_NOT_FOUND;
        break;
    case EEXIST:
        status = ZFS_EXISTS;
        break;
    case ENOSPC:
        status = ZFS_NO_SPACE;
        break;
    case EIO:
        status = ZFS_IO;
        break;
    case ENOMEM:
        status = ZFS_NO_MEMORY;
        break;
    default:
        status = ZFS_INVALID;
        break;
    }
    return zfs_error_set(error, status, "%s", strerror(-entry_count));
}

static zfs_status path_check(const char* path, zfs_error* error)
{
    if (path == NULL || path[0] != '/' || strlen(path) >= ZFS_FS_PATH_MAX) {
        return zfs_error_set(error, ZFS_INVALID, "Use an absolute ZealFS path shorter than 512 bytes");
    }
    const char* p = path + 1;
    while (*p != 0) {
        const char* end = strchr(p, '/');
        size_t n = end != NULL ? (size_t) (end - p) : strlen(p);
        if (n == 0 || n > ZFS_NAME_MAX || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) {
            return zfs_error_set(error,
                                 ZFS_INVALID,
                                 "Path components must be 1 to 16 bytes; dot components are unsupported");
        }
        if (end == NULL) {
            break;
        }
        p = end + 1;
        if (*p == 0) {
            return zfs_error_set(error, ZFS_INVALID, "Remove the trailing slash");
        }
    }
    return ZFS_OK;
}

zfs_status zfs_list(zfs_disk* disk, int part, const char* path, zfs_entry** entries, size_t* count, zfs_error* error)
{
    if (entries == NULL || count == NULL) {
        return zfs_error_set(error, ZFS_INVALID, "Listing outputs are required");
    }
    *entries = NULL;
    *count = 0;
    zfs_status status = path_check(path, error);
    if (status != ZFS_OK) {
        return status;
    }
    filesystem* fs = NULL;
    status = fs_open(disk, part, &fs, error);
    if (status != ZFS_OK) {
        return status;
    }
    zealfs_fd_t fd;
    int entry_count = zealfs_opendir(path, &fs->fs, &fd);
    if (entry_count < 0) {
        free(fs);
        return fs_error(entry_count, error);
    }
    /* A directory can chain up to 65535 pages. Readdir reports overflow rather than truncating. */
    size_t capacity = 2048;
    zealfs_entry_t* raw = NULL;
    for (;;) {
        raw = malloc(capacity * sizeof(*raw));
        if (raw == NULL) {
            free(fs);
            return zfs_error_set(error, ZFS_NO_MEMORY, "Cannot allocate directory entries");
        }
        entry_count = zealfs_readdir(&fs->fs, &fd, raw, (int) capacity);
        if (entry_count != -ENOBUFS) {
            break;
        }
        free(raw);
        raw = NULL;
        if (capacity >= 1048576) {
            free(fs);
            return zfs_error_set(error, ZFS_NO_SPACE, "Directory listing exceeds supported entry limit");
        }
        capacity *= 2;
    }
    if (entry_count < 0) {
        free(raw);
        free(fs);
        return fs_error(entry_count, error);
    }
    zfs_entry* result = calloc(entry_count != 0 ? entry_count : 1, sizeof(*result));
    if (result == NULL) {
        free(raw);
        free(fs);
        return zfs_error_set(error, ZFS_NO_MEMORY, "Cannot allocate listing");
    }
    for (int i = 0; i < entry_count; ++i) {
        memcpy(result[i].name, raw[i].name, ZFS_NAME_MAX);
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
    *count = (size_t) entry_count;
    free(raw);
    free(fs);
    return ZFS_OK;
}

void zfs_entries_free(zfs_entry* entries)
{
    free(entries);
}

zfs_status zfs_usage(zfs_disk* disk, int part, uint64_t* free_bytes, uint64_t* total, zfs_error* error)
{
    if (free_bytes == NULL || total == NULL) {
        return zfs_error_set(error, ZFS_INVALID, "Usage outputs are required");
    }
    zfs_status status = partition_check(disk, part, true, error);
    if (status != ZFS_OK) {
        return status;
    }
    if (!disk->usage_cached[part]) {
        filesystem* fs = NULL;
        status = fs_open(disk, part, &fs, error);
        if (status != ZFS_OK) {
            return status;
        }
        disk->free_cache[part] = zealfs_free_space(&fs->fs);
        /* If the partition starts at 0, it means that the disk has no MBR, instead of taking the
         * whole disk as the partition size, use the number of bytes in the bitmap */
        disk->total_cache[part] =
            fs->part.start_lba == 0 ? zealfs_total_space(&fs->fs) : (uint64_t) fs->part.sectors * DISK_SECTOR_SIZE;
        free(fs);
        disk->usage_cached[part] = true;
    }
    *free_bytes = disk->free_cache[part];
    *total = disk->total_cache[part];
    return ZFS_OK;
}

static zfs_status parent_check(filesystem* fs, const char* path, zfs_error* error)
{
    if (strcmp(path, "/") == 0) {
        return zfs_error_set(error, ZFS_INVALID, "Cannot modify the root entry");
    }
    char parent[ZFS_FS_PATH_MAX];
    strcpy(parent, path);
    char* slash = strrchr(parent, '/');
    if (slash == parent) {
        slash[1] = 0;
    } else {
        *slash = 0;
    }
    zealfs_fd_t fd;
    return fs_error(zealfs_opendir(parent, &fs->fs, &fd), error);
}

static zfs_status entry_mutation(zfs_disk* disk, int part, const char* path, int operation, zfs_error* error)
{
    zfs_status status = path_check(path, error);
    if (status != ZFS_OK) {
        return status;
    }
    filesystem* fs = NULL;
    status = fs_open(disk, part, &fs, error);
    if (status != ZFS_OK) {
        return status;
    }
    status = parent_check(fs, path, error);
    if (status != ZFS_OK) {
        free(fs);
        return status;
    }
    status = begin(disk, error);
    if (status != ZFS_OK) {
        free(fs);
        return status;
    }
    int result = operation == 0   ? zealfs_mkdir(path, &fs->fs, NULL)
                 : operation == 1 ? zealfs_unlink(path, &fs->fs)
                                  : zealfs_rmdir(path, &fs->fs);
    status = finish(disk, fs_error(result, error), error);
    free(fs);
    return status;
}

zfs_status zfs_mkdir(zfs_disk* disk, int part, const char* path, zfs_error* error)
{
    return entry_mutation(disk, part, path, 0, error);
}

zfs_status zfs_remove(zfs_disk* disk, int part, const char* path, bool directory, zfs_error* error)
{
    return entry_mutation(disk, part, path, directory ? 2 : 1, error);
}

static zfs_status write_file(filesystem* fs, const char* path, FILE* source, const uint8_t* data, uint64_t size,
                             zfs_error* error)
{
    zfs_disk* disk = fs->disk;
    zfs_status status = path_check(path, error);
    if (status != ZFS_OK) {
        return status;
    }
    if (size > UINT32_MAX) {
        return zfs_error_set(error, ZFS_INVALID, "File exceeds ZealFS size limit");
    }
    status = parent_check(fs, path, error);
    if (status != ZFS_OK) {
        return status;
    }
    zealfs_fd_t fd;
    int bytes_written = zealfs_create(path, &fs->fs, &fd);
    status = fs_error(bytes_written, error);
    uint8_t buffer[ZFS_TRANSFER_BUFFER_SIZE];
    uint64_t offset = 0;
    while (status == ZFS_OK && offset < size) {
        size_t chunk_size = (size_t) MIN(size - offset, sizeof(buffer));
        const void* bytes = data != NULL ? data + (size_t) offset : buffer;
        if (source != NULL && fread(buffer, 1, chunk_size, source) != chunk_size) {
            status = zfs_error_set(error, ZFS_IO, "Short read from import source");
            break;
        }
        bytes_written = zealfs_write(&fs->fs, &fd, (void*) bytes, chunk_size, (int64_t) offset);
        if (bytes_written != (int) chunk_size) {
            status =
                bytes_written < 0 ? fs_error(bytes_written, error) : zfs_error_set(error, ZFS_IO, "Short file write");
        }
        offset += chunk_size;
        progress(disk, offset, size);
    }
    if (status == ZFS_OK) {
        /* Flush the changes on the disk */
        status = fs_error(zealfs_flush(&fs->fs, &fd), error);
    }
    return status;
}

static zfs_status create_file(zfs_disk* disk, int part, const char* path, FILE* source, const uint8_t* data,
                              uint64_t size, zfs_error* error)
{
    filesystem* fs = NULL;
    zfs_status status = fs_open(disk, part, &fs, error);
    if (status == ZFS_OK) {
        status = begin(disk, error);
        if (status == ZFS_OK) {
            status = finish(disk, write_file(fs, path, source, data, size, error), error);
        }
    }
    free(fs);
    return status;
}

zfs_status zfs_file_write(zfs_disk* disk, int part, const char* path, const void* buffer, size_t size, zfs_error* error)
{
    if (buffer == NULL && size != 0) {
        return zfs_error_set(error, ZFS_INVALID, "File buffer is required");
    }
    return create_file(disk, part, path, NULL, buffer, size, error);
}

static int compare_import_paths(const void* first, const void* second)
{
    return strcmp(*(const char* const*) first, *(const char* const*) second);
}

static zfs_status import_file(filesystem* fs, const char* host, const char* path, bool overwrite, zfs_error* error)
{
    zfs_status status = path_check(path, error);
    if (status != ZFS_OK) {
        return status;
    }
    FILE* file = zfs_host_open(host, "rb");
    if (file == NULL) {
        return sys_error(error, "Cannot open import source");
    }
    struct stat st;
    if (fstat(fileno(file), &st) != 0) {
        status = sys_error(error, "Cannot inspect import source");
    } else if ((st.st_mode & S_IFMT) != S_IFREG || st.st_size < 0) {
        status = zfs_error_set(error, ZFS_INVALID, "Import sources must be regular files: %s", host);
    } else {
        zealfs_fd_t existing;
        int result = zealfs_stat(path, &fs->fs, &existing);
        if (result >= 0 && (existing.entry.flags & IS_DIR) != 0) {
            status = zfs_error_set(error, ZFS_CONFLICT, "Import would replace a directory: %s", path);
        } else if (result >= 0 && !overwrite) {
            status = zfs_error_set(error, ZFS_EXISTS, "Import destination exists: %s", path);
        } else if (result >= 0) {
            status = fs_error(zealfs_unlink(path, &fs->fs), error);
        } else if (result != -ENOENT) {
            status = fs_error(result, error);
        }
        if (status == ZFS_OK) {
            status = write_file(fs, path, file, NULL, (uint64_t) st.st_size, error);
        }
    }
    fclose(file);
    return status;
}

static zfs_status import_many(zfs_disk* disk, const zfs_import_request* request, zfs_error* error)
{
    if (request->host_paths == NULL || request->count == 0 || request->path == NULL || *request->path == 0 ||
        strlen(request->path) >= ZFS_FS_PATH_MAX) {
        return zfs_error_set(error, ZFS_INVALID, "Import sources and a destination are required");
    }
    char destination[ZFS_FS_PATH_MAX];
    strcpy(destination, request->path);
    size_t length = strlen(destination);
    bool require_directory = request->count > 1 || destination[length - 1] == '/';
    while (length > 1 && destination[length - 1] == '/') {
        destination[--length] = 0;
    }
    zfs_status status = path_check(destination, error);
    char** sources = NULL;
    size_t source_count = 0;
    for (size_t i = 0; status == ZFS_OK && i < request->count; ++i) {
        if (request->host_paths[i] != NULL && strpbrk(request->host_paths[i], "*?") != NULL) {
            require_directory = true;
        }
        status = zfs_host_expand(request->host_paths[i], &sources, &source_count, error);
    }
    bool directory = false;
    if (status == ZFS_OK) {
        status = zfs_core_path_directory(disk, request->partition, destination, &directory, error);
        if (status == ZFS_NOT_FOUND && !require_directory) {
            status = ZFS_OK;
        } else if (status == ZFS_OK && require_directory && !directory) {
            status = zfs_error_set(error, ZFS_INVALID, "Multiple sources and patterns require a destination directory");
        }
    }
    filesystem* fs = NULL;
    if (status == ZFS_OK) {
        status = fs_open(disk, request->partition, &fs, error);
    }
    if (status == ZFS_OK) {
        qsort(sources, source_count, sizeof(*sources), compare_import_paths);
        status = begin(disk, error);
        if (status == ZFS_OK) {
            for (size_t i = 0; status == ZFS_OK && i < source_count; ++i) {
                char target[ZFS_FS_PATH_MAX];
                const char* name = sources[i];
                for (const char* p = name; *p != 0; ++p) {
                    if (*p == '/'
#ifdef _WIN32
                        || *p == '\\'
#endif
                    ) {
                        name = p + 1;
                    }
                }
                int n = directory
                            ? snprintf(target, sizeof(target), "%s%s%s", destination, length == 1 ? "" : "/", name)
                            : snprintf(target, sizeof(target), "%s", destination);
                status = n < 0 || (size_t) n >= sizeof(target)
                             ? zfs_error_set(error, ZFS_INVALID, "Import destination path is too long")
                             : import_file(fs, sources[i], target, request->overwrite, error);
            }
            status = finish(disk, status, error);
        }
    }
    free(fs);
    zfs_host_paths_free(sources, source_count);
    return status;
}

zfs_status zfs_import_files(zfs_disk* disk, const zfs_import_request* request, zfs_error* error)
{
    if (request == NULL) {
        return zfs_error_set(error, ZFS_INVALID, "Import request is required");
    }
    zfs_callbacks previous = {0};
    if (disk != NULL) {
        previous = disk->callbacks;
        if (request->callbacks != NULL) {
            disk->callbacks = *request->callbacks;
        }
    }
    zfs_status status = import_many(disk, request, error);
    if (disk != NULL) {
        disk->callbacks = previous;
    }
    return status;
}

zfs_status zfs_import_many(zfs_disk* disk, int part, const char* const* hosts, size_t count, const char* path,
                           bool overwrite, zfs_error* error)
{
    const zfs_import_request request = {
        .partition = part,
        .host_paths = hosts,
        .count = count,
        .path = path,
        .overwrite = overwrite,
    };
    return zfs_import_files(disk, &request, error);
}

zfs_status zfs_import(zfs_disk* disk, int part, const char* host, const char* path, zfs_error* error)
{
    return zfs_import_many(disk, part, &host, 1, path, false, error);
}

zfs_status zfs_file_read(zfs_disk* disk, int part, const char* path, uint64_t offset, void* buffer, size_t size,
                         size_t* count, zfs_error* error)
{
    if (count == NULL || (buffer == NULL && size != 0) || size > INT32_MAX) {
        return zfs_error_set(error, ZFS_INVALID, "Invalid read buffer");
    }
    *count = 0;
    zfs_status status = path_check(path, error);
    if (status != ZFS_OK) {
        return status;
    }
    filesystem* fs = NULL;
    status = fs_open(disk, part, &fs, error);
    if (status != ZFS_OK) {
        return status;
    }
    zealfs_fd_t fd;
    int bytes_read = zealfs_open(path, &fs->fs, &fd);
    if (bytes_read >= 0 && (fd.entry.flags & IS_DIR) != 0) {
        free(fs);
        return zfs_error_set(error, ZFS_INVALID, "Cannot read a directory as a file");
    }
    if (bytes_read >= 0 && offset < fd.entry.size) {
        bytes_read = zealfs_read(&fs->fs, &fd, buffer, size, (int64_t) offset);
    } else if (bytes_read >= 0) {
        bytes_read = 0;
    }
    status = fs_error(bytes_read, error);
    if (status == ZFS_OK) {
        *count = (size_t) bytes_read;
    }
    free(fs);
    return status;
}

zfs_status zfs_core_export_destination(zfs_disk* disk, const char* host, zfs_error* error)
{
    if (host == NULL || *host == 0 || strlen(host) >= ZFS_PATH_MAX - 64) {
        return zfs_error_set(error, ZFS_INVALID, "Invalid export destination");
    }
    if (disk != NULL && disk->info.image) {
        char resolved[ZFS_PATH_MAX];
        const char* destination = zfs_host_resolve(host, resolved, sizeof(resolved)) ? resolved : host;
#ifdef _WIN32
        bool target_alias = _stricmp(destination, disk->info.path) == 0;
#else
        bool target_alias = strcmp(destination, disk->info.path) == 0;
#endif
        if (target_alias) {
            return zfs_error_set(error, ZFS_INVALID, "Cannot export onto the active image");
        }
        FILE* candidate = zfs_host_open(host, "rb");
        if (candidate != NULL) {
            bool same = disk->base != NULL && file_identity(candidate, disk->base);
            fclose(candidate);
            if (same) {
                return zfs_error_set(error, ZFS_INVALID, "Cannot export onto the active image or an alias");
            }
        }
    }
    return ZFS_OK;
}

zfs_status zfs_core_path_directory(zfs_disk* disk, int part, const char* path, bool* directory, zfs_error* error)
{
    zfs_status status = path_check(path, error);
    if (status != ZFS_OK) {
        return status;
    }
    filesystem* fs = NULL;
    status = fs_open(disk, part, &fs, error);
    if (status != ZFS_OK) {
        return status;
    }
    *directory = strcmp(path, "/") == 0;
    if (!*directory) {
        zealfs_fd_t fd;
        status = fs_error(zealfs_stat(path, &fs->fs, &fd), error);
        if (status == ZFS_OK) {
            *directory = (fd.entry.flags & IS_DIR) != 0;
        }
    }
    free(fs);
    return status;
}

/* Read the complete file, including after a comparison mismatch, so a later
 * read error cannot be mistaken for a valid file with different contents. */
static zfs_status read_file_chunks(filesystem* fs, zealfs_fd_t* file, const file_chunk_consumer* consumer,
                                   zfs_error* error)
{
    uint8_t source_buffer[ZFS_TRANSFER_BUFFER_SIZE];
    uint64_t offset = 0;
    zfs_status status = ZFS_OK;

    while (status == ZFS_OK && offset < file->entry.size) {
        size_t chunk_size = (size_t) MIN(file->entry.size - offset, sizeof(source_buffer));
        int bytes_read = zealfs_read(&fs->fs, file, source_buffer, chunk_size, (int64_t) offset);
        if (bytes_read != (int) chunk_size) {
            status =
                bytes_read < 0 ? fs_error(bytes_read, error) : zfs_error_set(error, ZFS_IO, "%s", consumer->read_error);
        } else if (consumer->consume != NULL) {
            status = consumer->consume(consumer->user, source_buffer, chunk_size, offset, error);
        }
        offset += chunk_size;
        if (consumer->report_progress) {
            progress(fs->disk, offset, file->entry.size);
        }
    }
    return status;
}

static zfs_status write_file_chunk(void* user, const uint8_t* data, size_t size, uint64_t offset, zfs_error* error)
{
    file_output* output = user;
    (void) offset;
    return fwrite(data, 1, size, output->file) == size ? ZFS_OK : sys_error(error, output->write_error);
}

static zfs_status compare_file_chunk(void* user, const uint8_t* data, size_t size, uint64_t offset, zfs_error* error)
{
    file_comparison* comparison = user;
    uint8_t target_buffer[ZFS_TRANSFER_BUFFER_SIZE];
    int bytes_read =
        zealfs_read(&comparison->target_fs->fs, comparison->target_file, target_buffer, size, (int64_t) offset);

    if (bytes_read != (int) size) {
        return bytes_read < 0 ? fs_error(bytes_read, error) : zfs_error_set(error, ZFS_IO, "Short file read");
    }
    if (memcmp(data, target_buffer, size) != 0) {
        comparison->same = false;
    }
    return ZFS_OK;
}

zfs_status zfs_core_export_file(zfs_disk* disk, int part, const char* path, const char* host, bool overwrite,
                                zfs_error* error)
{
    zfs_status status = zfs_core_export_destination(disk, host, error);
    if (status != ZFS_OK) {
        return status;
    }
    status = path_check(path, error);
    if (status != ZFS_OK) {
        return status;
    }
    filesystem* fs = NULL;
    status = fs_open(disk, part, &fs, error);
    if (status != ZFS_OK) {
        return status;
    }
    zealfs_fd_t fd;
    int bytes_read = zealfs_open(path, &fs->fs, &fd);
    status = fs_error(bytes_read, error);
    if (status == ZFS_OK && (fd.entry.flags & IS_DIR) != 0) {
        status = zfs_error_set(error, ZFS_INVALID, "Only files can be exported");
    }
    if (status != ZFS_OK) {
        free(fs);
        return status;
    }
    if (!overwrite) {
        FILE* exists = zfs_host_open(host, "rb");
        if (exists != NULL) {
            fclose(exists);
            free(fs);
            return zfs_error_set(error, ZFS_EXISTS, "Export destination exists");
        }
    }
    char temporary[ZFS_PATH_MAX];
    /* Create the destination file */
    FILE* dest = zfs_temp_sibling(host, temporary, sizeof(temporary));
    if (dest == NULL) {
        free(fs);
        return sys_error(error, "Cannot create export destination");
    }
    file_output output = {
        .file = dest,
        .write_error = "Export write failed",
    };
    const file_chunk_consumer consumer = {
        .consume = write_file_chunk,
        .user = &output,
        .read_error = "Short read during export",
        .report_progress = true,
    };
    /* Write the file chunk by chunk */
    status = read_file_chunks(fs, &fd, &consumer, error);
    if (status == ZFS_OK && zfs_host_sync(dest) != 0) {
        status = sys_error(error, "Export flush failed");
    }
    if (fclose(dest) != 0 && status == ZFS_OK) {
        status = sys_error(error, "Export close failed");
    }
    if (status == ZFS_OK && zfs_host_publish(temporary, host, overwrite) != 0) {
        status = zfs_error_set(error, ZFS_IO, "Cannot publish export (destination exists or replacement failed)");
    }
    if (status != ZFS_OK) {
        zfs_host_remove(temporary);
    }
    free(fs);
    return status;
}

static zfs_native image_handle(FILE* file)
{
#ifdef _WIN32
    return (HANDLE) _get_osfhandle(_fileno(file));
#else
    return fileno(file);
#endif
}

bool zfs_core_same_disk(zfs_disk* first_disk, zfs_disk* second_disk)
{
    if (first_disk == NULL || second_disk == NULL) {
        return false;
    }
    if (first_disk == second_disk) {
        return true;
    }
    if (first_disk->info.image != second_disk->info.image) {
        return false;
    }
    return first_disk->info.image ? first_disk->base != NULL && second_disk->base != NULL &&
                                        file_identity(first_disk->base, second_disk->base)
                                  : zfs_native_same(first_disk->device, second_disk->device);
}

bool zfs_core_unchanged(zfs_disk* disk)
{
    return disk != NULL && !disk->info.failed && (!disk->info.image || same_image(disk));
}

zfs_status zfs_core_guard(zfs_disk* disk, bool writing, zfs_error* error)
{
    if (disk == NULL || disk->info.new_image || disk->info.failed) {
        return zfs_error_set(error, ZFS_INVALID, "An existing healthy target is required");
    }
    if (disk->guarded) {
        if (!disk->info.image && writing && !disk->guard_write) {
            return zfs_error_set(error, ZFS_BUSY, "Reopen this read-protected device before using it as a target");
        }
        return ZFS_OK;
    }
    if (disk->info.image && !same_image(disk)) {
        return zfs_error_set(error, ZFS_BUSY, "Image changed externally; reopen it");
    }
    zfs_native fd = disk->info.image ? image_handle(disk->base) : disk->device;
    zfs_status status = zfs_native_guard(&fd, disk->info.path, disk->info.image, writing, error);
    if (!disk->info.image) {
        disk->device = fd;
    }
    if (status == ZFS_OK) {
        disk->guarded = true;
        disk->guard_write = writing;
    }
    return status;
}

zfs_status zfs_core_clone(zfs_disk* disk, zfs_disk** view, zfs_error* error)
{
    zfs_status status = allocate_disk(disk->info.path, disk->info.size, disk->info.image, view, error);
    if (status != ZFS_OK) {
        return status;
    }
    zfs_disk* clone = *view;
    clone->info = disk->info;
    clone->original = disk->original;
    clone->stamp = disk->stamp;
    clone->base = disk->base;
    clone->device = disk->device;
    clone->borrowed_base = true;
    clone->callbacks = disk->callbacks;
    return ZFS_OK;
}

void zfs_core_attach(zfs_disk* target, zfs_disk* view)
{
    FILE* overlay = target->overlay;
    target->overlay = view->overlay;
    view->overlay = overlay;
    uint8_t* bitmap = target->dirty;
    target->dirty = view->dirty;
    view->dirty = bitmap;
    target->dirty_count = view->dirty_count;
    view->dirty_count = 0;
    target->info = view->info;
    target->verified_apply = true;
    ++target->revision;
    memset(target->usage_cached, 0, sizeof(target->usage_cached));
}

zfs_status zfs_core_read(zfs_disk* disk, uint64_t offset, void* buffer, size_t size, zfs_error* error)
{
    return bytes_io(disk, offset, buffer, size, DISK_READ, error);
}

zfs_status zfs_core_stage_raw(zfs_disk* disk, uint64_t offset, zfs_disk* source, uint64_t size, zfs_error* error)
{
    zfs_status status = begin(disk, error);
    if (status != ZFS_OK) {
        return status;
    }
    uint8_t incoming[ZFS_TRANSFER_BUFFER_SIZE];
    uint8_t current[ZFS_TRANSFER_BUFFER_SIZE];
    for (uint64_t at = 0; at < size && status == ZFS_OK; at += sizeof(incoming)) {
        size_t n = (size_t) MIN(size - at, sizeof(incoming));
        status = bytes_io(source, at, incoming, n, DISK_READ, error);
        if (status == ZFS_OK) {
            status = bytes_io(disk, offset + at, current, n, DISK_READ, error);
        }
        if (status == ZFS_OK && memcmp(incoming, current, n) != 0) {
            status = bytes_io(disk, offset + at, incoming, n, DISK_WRITE, error);
        }
        progress(disk, at + n, size);
    }
    status = finish(disk, status, error);
    if (status == ZFS_OK) {
        bool pending = disk->info.pending;
        status = parse(disk, error);
        disk->info.pending = pending;
    }
    return status;
}

zfs_status zfs_core_copy_metadata(zfs_disk* source, int source_partition, zfs_disk* target, int target_partition,
                                  const char* path, zfs_error* error)
{
    filesystem* source_fs = NULL;
    filesystem* target_fs = NULL;
    zfs_status status = fs_open(source, source_partition, &source_fs, error);
    if (status != ZFS_OK) {
        return status;
    }
    status = fs_open(target, target_partition, &target_fs, error);
    if (status != ZFS_OK) {
        free(source_fs);
        return status;
    }
    zealfs_fd_t source_entry;
    zealfs_fd_t target_entry;
    status = fs_error(zealfs_stat(path, &source_fs->fs, &source_entry), error);
    if (status == ZFS_OK) {
        status = fs_error(zealfs_stat(path, &target_fs->fs, &target_entry), error);
    }
    if (status == ZFS_OK) {
        status = begin(target, error);
        if (status == ZFS_OK) {
            memcpy(target_entry.entry.year,
                   source_entry.entry.year,
                   offsetof(zealfs_entry_t, reserved) - offsetof(zealfs_entry_t, year));
            status = finish(
                target,
                fs_error(
                    (int) fs_write(target_fs, &target_entry.entry, target_entry.entry_addr, sizeof(target_entry.entry)),
                    error),
                error);
        }
    }
    free(source_fs);
    free(target_fs);
    return status;
}

zfs_status zfs_core_copy_file(zfs_disk* source, int source_partition, zfs_disk* target, int target_partition,
                              const char* path, zfs_error* error)
{
    filesystem* source_fs = NULL;
    zfs_status status = fs_open(source, source_partition, &source_fs, error);
    if (status != ZFS_OK) {
        return status;
    }
    zealfs_fd_t source_file;
    status = fs_error(zealfs_open(path, &source_fs->fs, &source_file), error);
    FILE* spool = status == ZFS_OK ? tmpfile() : NULL;
    if (status == ZFS_OK && spool == NULL) {
        status = sys_error(error, "Cannot create transfer spool");
    }
    file_output output = {
        .file = spool,
        .write_error = "Transfer spool write failed",
    };
    const file_chunk_consumer consumer = {
        .consume = write_file_chunk,
        .user = &output,
        .read_error = "Short source read",
    };
    if (status == ZFS_OK) {
        status = read_file_chunks(source_fs, &source_file, &consumer, error);
    }
    if (status == ZFS_OK && zfs_seek(spool, 0) != 0) {
        status = sys_error(error, "Transfer spool seek failed");
    }
    if (status == ZFS_OK) {
        status = create_file(target, target_partition, path, spool, NULL, source_file.entry.size, error);
    }
    if (spool != NULL) {
        fclose(spool);
    }
    free(source_fs);
    if (status == ZFS_OK) {
        status = zfs_core_copy_metadata(source, source_partition, target, target_partition, path, error);
    }
    return status;
}

zfs_status zfs_core_files_equal(zfs_partition_ref source, zfs_partition_ref target, const char* path, bool* equal,
                                zfs_error* error)
{
    filesystem* source_fs = NULL;
    filesystem* target_fs = NULL;
    *equal = false;
    zfs_status status = fs_open(source.disk, source.partition, &source_fs, error);
    if (status != ZFS_OK) {
        return status;
    }
    if (target.disk != NULL) {
        status = fs_open(target.disk, target.partition, &target_fs, error);
        if (status != ZFS_OK) {
            free(source_fs);
            return status;
        }
    }
    zealfs_fd_t source_file;
    zealfs_fd_t target_file;
    status = fs_error(zealfs_open(path, &source_fs->fs, &source_file), error);
    if (status == ZFS_OK && target.disk != NULL) {
        status = fs_error(zealfs_open(path, &target_fs->fs, &target_file), error);
    }
    if (status == ZFS_OK && target.disk != NULL && source_file.entry.size != target_file.entry.size) {
        free(source_fs);
        free(target_fs);
        return ZFS_OK;
    }
    file_comparison comparison = {
        .target_fs = target_fs,
        .target_file = &target_file,
        .same = true,
    };
    const file_chunk_consumer consumer = {
        .consume = target.disk != NULL ? compare_file_chunk : NULL,
        .user = &comparison,
        .read_error = "Short file read",
    };
    if (status == ZFS_OK) {
        status = read_file_chunks(source_fs, &source_file, &consumer, error);
    }
    *equal = status == ZFS_OK && comparison.same;
    free(source_fs);
    free(target_fs);
    return status;
}

zfs_status zfs_core_check_file(zfs_disk* disk, int part, const char* path, zfs_error* error)
{
    bool equal;
    const zfs_partition_ref source = {
        .disk = disk,
        .partition = part,
    };
    return zfs_core_files_equal(source, (zfs_partition_ref){0}, path, &equal, error);
}

const char* zfs_recovery_backup(const zfs_disk* disk)
{
    return disk != NULL && disk->recovery_backup[0] != 0 ? disk->recovery_backup : NULL;
}

int zfs_recovery_partition(const zfs_disk* disk)
{
    return disk != NULL ? disk->recovery_partition : -1;
}
