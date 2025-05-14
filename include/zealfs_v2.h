/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#ifndef BIT
#define BIT(X)  (1ULL << (X))
#endif

#ifndef KB
#define KB  (1024ULL)
#endif

#ifndef MB
#define MB  (1048576ULL)
#endif

#ifndef GB
#define GB  (1073741824ULL)
#endif

/* Bit 0 is 1 if is directory */
#define IS_DIR (1 << 0)
/* Bit 7 is 1 if is entry occupied */
#define IS_OCCUPIED (1 << 7)

/* Maximum length of the names in the file system, including th extension */
#define NAME_MAX_LEN 16


/**
 * @brief The size of the header depends on the bitmap, which is at most
 * 65536/8.
 */
#define ZFS_HEADER_MAX_SIZE (8192 + sizeof(zealfs_entry_t))

/**
 * Helper that converts an 8-bit BCD value into a binary value.
 */
static inline int from_bcd(uint8_t value) {
    return (value >> 4) * 10 + (value & 0xf);
}

/* Type for table entry */
typedef struct {
    /* Bit 0: 1 = directory, 0 = file
     * bit n: reserved
     * Bit 7: 1 = occupied, 0 = free */
    uint8_t flags; /* IS_DIR, IS_FILE, etc... */
    char name[NAME_MAX_LEN];
    uint16_t start_page;
    /* Size of the file in bytes, little-endian! */
    uint32_t size;
    /* Zeal 8-bit OS date format (BCD) */
    uint8_t  year[2];
    uint8_t  month;
    uint8_t  day;
    uint8_t  date;
    uint8_t  hours;
    uint8_t  minutes;
    uint8_t  seconds;
    /* Reserved for future use */
    uint8_t reserved;
} __attribute__((packed)) zealfs_entry_t;
_Static_assert(sizeof(zealfs_entry_t) == 32, "zealfs_entry_t must be smaller than 32 bytes");


/* Type for partition header */
typedef struct {
  uint8_t magic;    /* Must be 'Z' ascii code */
  uint8_t version;  /* Version of the file system */
  /* Number of bytes composing the bitmap */
  uint16_t bitmap_size;
  /* Number of free pages, we always have at most 65535 pages */
  uint16_t free_pages;
  /* Size of the pages:
   * 0 - 256
   * 1 - 512
   * 2 - 1024
   * 3 - 2048
   * 4 - 4096
   * 5 - 8192
   * 6 - 16384
   * 7 - 32768
   * 8 - 65536
   */
  uint8_t page_size;
  uint8_t pages_bitmap[];
} __attribute__((packed)) zealfs_header_t;


typedef struct zealfs_context_t {
    ssize_t (*read) (void* arg, void* buffer, uint32_t addr, size_t len);
    ssize_t (*write)(void* arg, const void* buffer, uint32_t addr, size_t len);
    void* arg;
    /* Cache for the header, filled on `opendir` on the root, MUST be populated */
    uint8_t header[ZFS_HEADER_MAX_SIZE];
    size_t header_size;
    /* Cache for the FAT table, at most 64K entries */
    uint16_t fat[64*KB];
    size_t fat_size;
} zealfs_context_t;


/**
 * @brief Helper to get the recommended page size from a disk size.
 *
 */
static inline int zealfsv2_page_size(long long part_size)
{
    if (part_size <= 64*KB) {
        return 256;
    } else if (part_size <= 256*KB) {
        return 512;
    } else if (part_size <= 1*MB) {
        return 1*KB;
    } else if (part_size <= 4*MB) {
        return 2*KB;
    } else if (part_size <= 16*MB) {
        return 4*KB;
    } else if (part_size <= 64*MB) {
        return 8*KB;
    } else if (part_size <= 256*MB) {
        return 16*KB;
    } else if (part_size <= 1*GB) {
        return 32*KB;
    }

    return 64*KB;
}


/**
 * @brief Format the partition.
 *
 * @param partition Pointer to the partition data.
 * @param size Size of the whole partition.
 *
 * @return 0 on success, error else
 */
static inline int zealfsv2_format(uint8_t* partition, uint64_t size) {
    /* Initialize image header */
    zealfs_header_t* header = (zealfs_header_t*) partition;
    header->magic = 'Z';
    header->version = 2;
    /* According to the size of the disk, we have to calculate the size of the pages */
    const int page_size_bytes = zealfsv2_page_size(size);
    /* The page size in the header is the log2(page_bytes/256) - 1 */
    header->page_size = ((sizeof(int) * 8) - (__builtin_clz(page_size_bytes >> 8))) - 1;
    header->bitmap_size = size / page_size_bytes / 8;
    /* If the page size is 256, there will be only one page for the FAT */
    const int fat_pages_count = 1 + (page_size_bytes == 256 ? 0 : 1);
    /* Do not count the first page and the second page */
    header->free_pages = size / page_size_bytes - 1 - fat_pages_count;
    /* All the pages are free (0), mark the first one as occupied */
    header->pages_bitmap[0] = 3 | ((fat_pages_count > 1) ? 4 : 0);

    printf("[ZEALFS] Bitmap size: %d bytes\n", header->bitmap_size);
    printf("[ZEALFS] Pages size: %d bytes (code %d)\n", page_size_bytes, ((page_size_bytes >> 8) - 1));
#if 0
    printf("[ZEALFS] Maximum root entries: %d\n", get_root_dir_max_entries(header));
    printf("[ZEALFS] Maximum dir entries: %d\n", get_dir_max_entries(header));
    printf("[ZEALFS] Header size/Root entries: %d (0x%x)\n", get_fs_header_size(header), get_fs_header_size(header));
#endif

    return 0;
}

typedef struct {
    zealfs_entry_t entry;
    uint32_t       entry_addr;
} zealfs_fd_t;


/**
 * @brief Opens a directory in the Zealfs filesystem.
 *
 * @param path The absolute path to the directory, starting with '/'.
 * @param ctx The context containing disk read/write functions.
 * @return 0 on success, or a negative error code on failure.
 */
int zealfs_opendir(const char * path, zealfs_context_t* ctx, zealfs_fd_t* fd);


/**
 * @brief Reads entries from an open directory in the zealfs filesystem.
 *
 * @param ctx The context containing disk read/write functions.
 * @param ret_entries Pointer to an array where the directory entries will be stored.
 * @param count The maximum number of entries to read.
 * @return The number of entries read on success, or a negative error code on failure.
 */
int zealfs_readdir(zealfs_context_t* ctx, zealfs_fd_t* fd, zealfs_entry_t* ret_entries, int count);


/**
 * @brief Cleans up and releases resources associated with the zealfs context.
 *
 * @param ctx The context containing disk read/write functions to be destroyed.
 */
void zealfs_destroy(zealfs_context_t* ctx);


/**
 * @brief Opens a file in the zealfs filesystem.
 *
 * @param path The absolute path to the file, starting with '/'.
 * @param ctx The context containing disk read/write functions.
 * @param ret_entry Pointer to a structure where the file entry information will be stored.
 * @return 0 on success, or a negative error code on failure.
 */
int zealfs_open(const char * path, zealfs_context_t* ctx, zealfs_fd_t* fd);


/**
 * @brief Reads data from an open file in the zealfs filesystem.
 *
 * @param ctx The context containing disk read/write functions.
 * @param entry The file entry to read from.
 * @param buf Buffer where the read data will be stored.
 * @param size The number of bytes to read.
 * @param offset The offset in the file from which to start reading.
 * @return The number of bytes read on success, or a negative error code on failure.
 */
int zealfs_read(zealfs_context_t* ctx, zealfs_fd_t* fd, void *buf, size_t size, off_t offset);


/**
 * @brief Creates a new directory in the ZealFS file system.
 *
 * @param path The path where the directory should be created.
 * @param ctx Pointer to the ZealFS context structure.
 * @param ret_entry Pointer to a zealfs_entry_t structure where the created directory's metadata will be stored.
 * @return 0 on success, or a negative error code on failure.
 */
int zealfs_mkdir(const char * path, zealfs_context_t* ctx, zealfs_fd_t* fd);

/**
 * @brief Creates a new file in the ZealFS file system.
 *
 * @param path The path where the file should be created.
 * @param ctx Pointer to the ZealFS context structure.
 * @param ret_entry Pointer to a zealfs_entry_t structure where the created file's metadata will be stored.
 * @return 0 on success, or a negative error code on failure.
 */
int zealfs_create(const char * path, zealfs_context_t* ctx, zealfs_fd_t* fd);

/**
 * @brief Removes a file from the filesystem.
 *
 * This function deletes the specified file from the filesystem.
 * It does not work on directories and will fail if the provided
 * path refers to a directory.
 *
 * @param pathname The path to the file to be removed.
 * @return 0 on success, or -1 on failure with errno set to indicate the error.
 *
 * @note Ensure that the file exists and is not a directory before calling this function.
 * Common errors include:
 * - ENOENT: The file does not exist.
 * - EISDIR: The specified path refers to a directory.
 * - EACCES: Permission denied to remove the file.
 */
int zealfs_unlink(const char* path, zealfs_context_t* ctx);


/**
 * @brief Removes a directory from the filesystem.
 *
 * This function deletes the specified directory from the filesystem.
 * Note that the directory must be empty before it can be removed.
 * Attempting to remove a non-empty directory will result in an error.
 *
 * @param pathname A pointer to a null-terminated string that specifies
 *                 the path of the directory to be removed.
 * @return On success, 0 is returned. On error, -1 is returned, and errno
 *         is set to indicate the error.
 *
 * Possible errors:
 * - EACCES: Permission denied to remove the directory.
 * - ENOENT: The specified directory does not exist.
 * - ENOTDIR: A component of the path is not a directory.
 * - EBUSY: The directory is currently in use.
 * - ENOTEMPTY: The directory is not empty.
 */
int zealfs_rmdir(const char* path, zealfs_context_t* ctx);


/**
 * @brief Retrieves the free space, in bytes, available in the filesystem.
 *
 * @param ctx Pointer to the zealfs_context_t structure representing the filesystem context.
 *
 * @return The amount of free space in bytes.
 */
uint32_t zealfs_free_space(zealfs_context_t* ctx);


/**
 * @brief Writes data to a specified entry in the filesystem.
 *
 * @param ctx A pointer to the zealfs_context_t structure representing the filesystem context.
 * @param entry A pointer to the zealfs_entry_t structure representing the target entry to write to.
 * @param buf A pointer to the buffer containing the data to be written.
 * @param size The size of the data to be written, in bytes.
 * @param offset The offset within the entry where the data should be written.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int zealfs_write(zealfs_context_t* ctx, zealfs_fd_t* fd,
                void *buf, size_t size, off_t offset);


/**
 * @brief Applies changes to the File Allocation Table (FAT) and header on the disk.
 *
 * This function must be called after completing all calls to `zealfs_write`
 * for the specified file descriptor (`fd`). It ensures that all pending
 * updates to the FAT and header are properly written to the disk, maintaining
 * the integrity of the file system.
 *
 * @param fd The file descriptor associated with the file whose FAT and header
 *           changes need to be applied to the disk.
 */
int zealfs_flush(zealfs_context_t* ctx, zealfs_fd_t* fd);
