/* SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <time.h>
#include <dirent.h>
#include "zealfs_v2.h"

#define MIN(a,b)    (((a) < (b)) ? (a) : (b))
/**
 * Macro to help converting a page number into an address in the cache.
 */
#define ADDR_FROM_PAGE(header, page) (((uint32_t) (page)) << (8 + (header)->page_size ))

#define ALIGN_UP(size,bound) (((size) + (bound) - 1) & ~((bound) - 1))


typedef struct {
    uint16_t       last_dir_page;        /* Last page of the last directory reached */
    uint32_t       free_entry_addr;       /* Address of a free entry in the last directory */
    uint32_t       entry_addr;            /* Address of the found entry */
    zealfs_entry_t entry;                 /* Found entry, when applicable */
} browse_out_t;


/**
 * @brief Get the size of the FileSystem header, multiple of 32 (sizeof(zealfs_entry_t))
 */
static inline int get_fs_header_size(const zealfs_header_t* header)
{
    int size = sizeof(zealfs_header_t) + header->bitmap_size;
    /* Round the size up to the next sizeof(zealfs_entry_t) bytes bound (FileEntry) */
    size = ALIGN_UP(size, sizeof(zealfs_entry_t));
    return size;
}


/**
 * @brief Get the size, in bytes, of the pages on the current disk
 */
static inline int get_page_size(const zealfs_header_t* header)
{
    const int size = header->page_size;
    assert (size <= 8);
    return 256 << size;
}


static inline int check_header(zealfs_context_t* ctx) {
    /* Initialize the header if it is not initialized yet */
    zealfs_header_t* header = (zealfs_header_t*) ctx->header;
    if (header->magic == 0) {
        /* In theory, we should read the header in two times:
         * - First time to get the bitmap size
         * - Second time to read the bitmap
         * Keep it simple and read the potential maximum */
        int err = ctx->read(ctx->arg, ctx->header, 0, sizeof(ctx->header));
        if (err < 0) {
            perror("[ZEALFS] Could not read header");
            return err;
        }
        ctx->header_size = get_fs_header_size(header);
        /* Read the FAT table, that starts at the first page of the disk, its size is one page (when 256 bytes)
         * else, two pages */
        const off_t page_size = get_page_size(header);
        ctx->fat_size = (page_size == 256) ? page_size : 2 * page_size;
        err = ctx->read(ctx->arg, ctx->fat, page_size, ctx->fat_size);
        if (err < 0) {
            perror("[ZEALFS] Could not read header");
            return err;
        }
    }
    return 0;
}


/**
 * Helper to get a pointer to the root directory entries
 */
static inline uint32_t get_root_dir_addr(const zealfs_header_t* header)
{
    return get_fs_header_size(header);
}


/*
 * As the root directory has less available space for the entries, it will have less than
 * regular directories. Define the following macro to simply the calculation.
 */
static inline uint16_t get_root_dir_max_entries(zealfs_header_t* header) {
    /* The size of the header depends on the size of the bitmap */
    return (get_page_size(header) - get_fs_header_size(header)) / sizeof(zealfs_entry_t);
}


/**
 * Get the maximum number of files in other directories than the root one
 */
static inline uint16_t get_dir_max_entries(zealfs_header_t* header) {
    return get_page_size(header) / sizeof(zealfs_entry_t);
}

/**
 * Convert a value between 0 and 99 into a BCD values.
 * For example, if `value` is 13, this function returns 0x13 (in hex!)
 */
static inline uint8_t to_bcd(int value) {
    return (((value / 10) % 10) << 4) | (value % 10);
}

/**
 * @brief Get the next power of two of the given number
 */
static inline uint64_t upper_power_of_two(long long disk_size)
{
    assert(disk_size != 0);
    int highest_one = 0;
    /* Number of ones in the integer*/
    int ones = 0;

    for (int i = 32; i >= 0; i--) {
        if (disk_size & BIT(i)) {
            if (highest_one == 0) highest_one = i;
            else ones++;
        }
    }

    if (ones == 0) {
        return disk_size;
    }

    /* If we got more than a single 1 bit, we have to return the next power
     * of two, so return BIT(highest_one+1) */
    return BIT(highest_one+1);
}


/**
 * @brief Free a page in the header bitmap.
 *
 * @param header File system header containing the bitmap to update.
 * @param page Page number to free, must not be 0.
 *
 */
static inline void free_page(zealfs_header_t* header, uint16_t page) {
    assert(page != 0);
    header->pages_bitmap[page / 8] &= ~(1 << (page % 8));
    header->free_pages++;
}


/**
 * @brief Get the next page of the current from the FAT
 */
static uint16_t get_next_from_fat(zealfs_context_t* ctx, uint16_t current_page)
{
    const zealfs_header_t* header = (zealfs_header_t*) ctx->header;
    assert(header->magic != 0);
    return ctx->fat[current_page];
}


/**
 * @brief Get the next page of the current from the FAT
 */
static void set_next_in_fat(zealfs_context_t* ctx, uint16_t current_page, uint16_t next_page)
{
    const zealfs_header_t* header = (zealfs_header_t*) ctx->header;
    assert(header->magic != 0);
    ctx->fat[current_page] = next_page;
}


/**
 * @brief Allocate one page in the given header's bitmap.
 *
 * @param header File system header to allocate the page from.
 *
 * @return Page number on success, 0 on error.
 */
static uint_fast16_t allocate_page(zealfs_header_t* header) {
    const int size = header->bitmap_size;
    int i = 0;
    int value = 0;
    for (i = 0; i < size; i++) {
        value = header->pages_bitmap[i];
        if (value != 0xff) {
            break;
        }
    }
    /* If we've reached the size, the bitmap is full */
    if (i == size) {
        printf("No more space in the bitmap of size: %d\n", header->bitmap_size);
        return 0;
    }
    /* Else, return the index */
    int index_0 = 0;
    while ((value & 1) != 0) {
        value >>= 1;
        index_0++;
    }

    /* Set the page as allocated in the bitmap */
    header->pages_bitmap[i] |= 1 << index_0;
    header->free_pages--;

    return i * 8 + index_0;
}

uint32_t zealfs_free_space(zealfs_context_t* ctx)
{
    zealfs_header_t* header = (zealfs_header_t*) ctx->header;
    assert (check_header(ctx) == 0);
    return header->free_pages * get_page_size(header);
}


/**
 * @brief Function that goes through the absolute path given as a parameter and verifies
 *        that each sub-directory does exist in the disk image.
 *
 * @param path Absolute path in the disk image
 * @param entries Entries array of the current directory. Must be the header's entries when calling
 *                this function.
 * @param root Boolean set to 1 if the given entries is the root directory's entries, 0 else.
 * @param free_entry When not NULL, will be populate with a free entry address in the last directory
 *                   of the path. Useful to create a non-existing-yet file or directory.
 * @param entry_found Entry to fill when found
 * @param last_page Last page of the last directory reached. Useful to extend the last directory.
 *
 * @return Returns a positive value on success, 0 when the file or directory was not found.
 *         Also returns 0 if one of the sub-directories in the path is not existent or is not a directory.
 */
static int browse_path(zealfs_context_t* ctx, const char * path,
                       uint32_t entries_addr, int root,
                       browse_out_t* out)
{
    zealfs_entry_t entries[2048];
    /* Store the current directory that is followed by '/' */
    char tmp_name[NAME_MAX_LEN + 1] = { 0 };
    zealfs_header_t* header = (zealfs_header_t*) ctx->header;
    int max_entries = root ? get_root_dir_max_entries(header) :
                             get_dir_max_entries(header);
    const int page_size = get_page_size(header);
    int current_page = entries_addr / page_size;

    if (out) {
        out->last_dir_page = current_page;
        out->free_entry_addr = 0;
        out->entry_addr = 0;
        memset(&out->entry, 0, sizeof(zealfs_entry_t));
    }

    /* Check the next path '/' */
    char* slash = strchr(path, '/');
    int len = 0;
    if (slash != NULL) {
        len = slash - path;
    } else {
        len = strlen(path);
    }
    if (len > NAME_MAX_LEN) {
        return -1;
    }
    memcpy(tmp_name, path, len);

    while (1) {
        /* Read all the entries from disk */
        int rd = ctx->read(ctx->arg, (void*) entries, entries_addr, max_entries * sizeof(zealfs_entry_t));
        if (rd < 0) {
            perror("[ZEALFS] Could not read data from partition");
            return rd;
        }

        for (int i = 0; i < max_entries; i++) {
            if ((entries[i].flags & IS_OCCUPIED) == 0) {
                /* If we are browsing the last name in the path and out is not NULL, we can save this
                * address to return. */
                if (slash == NULL && out) {
                    out->free_entry_addr = entries_addr + i * sizeof(zealfs_entry_t);
                }
                continue;
            }
            /* Entry is not empty, check that the name is correct */
            if (strncmp(entries[i].name, tmp_name, NAME_MAX_LEN) == 0) {
                if (slash == NULL) {
                    /* Entry found! Check if we have to return it to the caller */
                    if (out) {
                        out->entry_addr = entries_addr + i * sizeof(zealfs_entry_t);
                        memcpy(&out->entry, &entries[i], sizeof(zealfs_entry_t));
                    }
                    return 1;
                } else {
                    /* Get the page of the current directory */
                    return browse_path(ctx, slash + 1, ADDR_FROM_PAGE(header, entries[i].start_page), 0, out);
                }
            }
        }
        /* Entry was not found and all entries were tested, get the next page for the directory */
        current_page = get_next_from_fat(ctx, current_page);
        if (current_page == 0) {
            /* No next page, reached the end of the directory, return 0 (not found) */
            return 0;
        }
        /* Directory has a next page, check it! */
        if (out) {
            out->last_dir_page = current_page;
        }
        /* No more restrictions on the next pages */
        max_entries = get_dir_max_entries(header);
        entries_addr = ADDR_FROM_PAGE(header, current_page);
    }

    return 0;
}


/**
 * @brief Open the file or directory given as a parameter.
 */
int zealfs_open(const char * path, zealfs_context_t* ctx, zealfs_fd_t* fd)
{
    browse_out_t info;
    zealfs_header_t* header = (zealfs_header_t*) ctx->header;

    if (check_header(ctx)) {
        return -1;
    }

    if (strcmp(path, "/") == 0) {
        return -EISDIR;
    }

    int index = browse_path(ctx, path + 1, get_root_dir_addr(header), 1, &info);
    if (index != 0) {
        /* Check that the entry is a file */
        if ((info.entry.flags & 1) == 0) {
            if (fd) {
                fd->entry = info.entry;
                fd->entry_addr = info.entry_addr;
            }
            return 0;
        }
        return -EISDIR;
    }
    return -ENOENT;
}


/**
 * @brief Remove a file (and only a file!) from the disk image.
 */
int zealfs_unlink(const char* path, zealfs_context_t* ctx)
{
    browse_out_t info;
    zealfs_header_t* header = (zealfs_header_t*) ctx->header;
    if (check_header(ctx)) {
        return -1;
    }

    int index = browse_path(ctx, path + 1, get_root_dir_addr(header), 1, &info);
    if (index == 0) {
        return -ENOENT;
    }
    if (info.entry.flags & IS_DIR) {
        return -EISDIR;
    }

    assert(info.entry_addr != 0);

    uint16_t page = info.entry.start_page;
    while (page != 0) {
        free_page(header, page);
        const uint16_t next = get_next_from_fat(ctx, page);
        set_next_in_fat(ctx, page, 0);
        page = next;
    }
    /* Clear the flags of the file entry */
    memset(&info.entry, 0, sizeof(zealfs_entry_t));

    /* Clear the entry on disk */
    int wr = ctx->write(ctx->arg, &info.entry, info.entry_addr, sizeof(zealfs_entry_t));
    if (wr < 0) {
        perror("[ZEALFS] Error writing the header back to the disk");
        return wr;
    }

    /* Write the new header (bitmap) to the disk too */
    const int page_size = get_page_size(header);
    wr = ctx->write(ctx->arg, header, 0, ctx->header_size);
    if (wr < 0) {
        perror("[ZEALFS] Error writing the header back to the disk");
        return wr;
    }


    /* Update the FAT table and write it back to the disk */
    wr = ctx->write(ctx->arg, ctx->fat, page_size, ctx->fat_size);
    if (wr < 0) {
        perror("[ZEALFS] Error writing the header back to the disk");
        return wr;
    }

    return 0;
}


#if 0
/**
 * @brief Rename an entry, file or directory, in the disk image.
 *        The content will not be altered nor modified, only the entries headers will.
 */
static int zealfs_rename(const char* from, const char* to, unsigned int flags)
{
    zealfs_header_t* header = (zealfs_header_t*) g_image;
    zealfs_entry_t* free_entry = NULL;
    zealfs_entry_t* fentry = (zealfs_entry_t*) browse_path(from + 1, get_root_dir_entries(header), 1, NULL);
    zealfs_entry_t* tentry = (zealfs_entry_t*) browse_path(to + 1,   get_root_dir_entries(header), 1, &free_entry);

    if (fentry == 0 || (tentry == 0 && flags == RENAME_EXCHANGE)) {
        return -ENOENT;
    }

    if (flags == RENAME_NOREPLACE && tentry) {
        return -EEXIST;
    }

    if (flags == RENAME_EXCHANGE) {
        return -EFAULT;
    }

    char* from_mod = strdup(from);
    const char* from_dir = dirname(from_mod);
    char* to_mod = strdup(to);
    const char* to_dir = dirname(to_mod);
    char* to_mod_name = strdup(to);
    const char* newname = basename(to_mod_name);

    /* Check if the new name is valid */
    const int len = strlen(newname);
    if (len > NAME_MAX_LEN)
    {
        free(to_mod);
        free(from_mod);
        free(to_mod_name);
        return -ENAMETOOLONG;
    }

    /* In all cases, if the destination file already exists, remove it! */
    if (tentry) {
        zealfs_unlink(to);
        free_entry = tentry;
    }
    /* And rename the source file in its own directory */
    memset(fentry->name, 0, NAME_MAX_LEN);
    strncpy(fentry->name, newname, NAME_MAX_LEN);

    /* Check if the source and destination are in the same directory */
    int same_dir = strcmp(from_dir, to_dir) == 0;
    free(to_mod);
    free(from_mod);
    free(to_mod_name);

    if (!same_dir) {
        /* Not in the same directory, move the header if we have a free entry */
        if (!free_entry) {
            return -ENOMEM;
        }
        memcpy(free_entry, fentry, sizeof(zealfs_entry_t));
        /* Mark the former one as empty */
        memset(fentry, 0, sizeof(zealfs_entry_t));
    }

    return 0;
}
#endif


/**
 * @brief Remove an empty directory from the disk image.
 */
int zealfs_rmdir(const char* path, zealfs_context_t* ctx)
{
    browse_out_t info;
    zealfs_entry_t entries[2048];
    zealfs_header_t* header = (zealfs_header_t*) ctx->header;

    if (check_header(ctx)) {
        return -1;
    }

    if (strcmp(path, "/") == 0) {
        return -EACCES;
    }

    int index = browse_path(ctx, path + 1, get_root_dir_addr(header), 1, &info);
    if (index == 0) {
        return -ENOENT;
    }
    if ((info.entry.flags & IS_DIR) == 0) {
        return -ENOTDIR;
    }

    uint16_t current_page = info.entry.start_page;
    const int max_entries = get_dir_max_entries(header);

    while (current_page != 0) {
        const uint32_t page_addr = ADDR_FROM_PAGE(header, current_page);
        int rd = ctx->read(ctx->arg, (void*) entries, page_addr, max_entries * sizeof(zealfs_entry_t));
        if (rd < 0) {
            perror("[ZEALFS] Could not read directory entries");
            return rd;
        }

        for (int i = 0; i < max_entries; i++) {
            if (entries[i].flags & IS_OCCUPIED) {
                return -ENOTEMPTY;
            }
        }

        uint16_t next_page = get_next_from_fat(ctx, current_page);
        free_page(header, current_page);
        set_next_in_fat(ctx, current_page, 0);
        current_page = next_page;
    }

    /* Clear the directory entry */
    memset(&info.entry, 0, sizeof(zealfs_entry_t));
    int wr = ctx->write(ctx->arg, &info.entry, info.entry_addr, sizeof(zealfs_entry_t));
    if (wr < 0) {
        perror("[ZEALFS] Error writing the directory entry back to the disk");
        return wr;
    }

    /* Write the updated header (bitmap) to the disk */
    const int page_size = get_page_size(header);
    wr = ctx->write(ctx->arg, header, 0, ctx->header_size);
    if (wr < 0) {
        perror("[ZEALFS] Error writing the header back to the disk");
        return wr;
    }

    /* Update the FAT table and write it back to the disk */
    wr = ctx->write(ctx->arg, ctx->fat, page_size, ctx->fat_size);
    if (wr < 0) {
        perror("[ZEALFS] Error writing the FAT table back to the disk");
        return wr;
    }

    return 0;
}


/**
 * @brief Private function used to create either a directory of a file in the disk image.
 *
 * @param isdir 1 to create a directory, 0 to create a file.
 * @param path Absolute path of the entry to create.
 * @param mode Unused.
 * @param info When not NULL, filled with the newly allocated ZealFS Entry address.
 *
 * @return 0 on success, error else.
 */
static int zealfs_create_both(zealfs_context_t* ctx, int isdir, const char * path,
                              zealfs_fd_t* fd)
{
    browse_out_t info;
    zealfs_entry_t entry;
    uint_fast16_t new_page_dir = 0;
    int err;

    zealfs_header_t* header = (zealfs_header_t*) ctx->header;
    if (check_header(ctx)) {
        return -1;
    }

    /* Make a backup of the header in case we fail to write to disk */
    zealfs_header_t header_backup = *header;

    err = browse_path(ctx, path + 1, get_root_dir_addr(header), 1, &info);
    if (err < 0) {
        return err;
    } else if(err == 1) {
        return -EEXIST;
    }

    /* Entry was not found, check if we have some space left in the last directory browsed */
    if (info.free_entry_addr == 0) {
        /* If we couldn't find any empty entry in the directory, we need to allocate a new page for it */
        new_page_dir = allocate_page(header);
        if (new_page_dir == 0) {
            return -ENOSPC;
        }
        /* We'll need to clean the new page later */
        set_next_in_fat(ctx, new_page_dir, 0);
        set_next_in_fat(ctx, info.last_dir_page, new_page_dir);
        /* The address of the new entry is the same as the new page's */
        info.free_entry_addr = ADDR_FROM_PAGE(header, new_page_dir);
    }

    /* Get the basename of path_mod without using basename */
    const char* filename = strrchr(path, '/');
    filename = (filename != NULL) ? filename + 1 : path;
    const int len = strlen(filename);

    if (len > NAME_MAX_LEN) {
        return -ENAMETOOLONG;
    }

    /* Populate the entry */
    uint_fast16_t newp = allocate_page(header);
    if (newp == 0) {
        *header = header_backup;
        return -ENOSPC;
    }
    set_next_in_fat(ctx, newp, 0);
    /* Fill the new entry structure */
    entry.flags = IS_OCCUPIED | isdir;
    entry.start_page = newp;
    memset(&entry.name, 0, 16);
    memcpy(&entry.name, filename, len);
    entry.size = isdir ? get_page_size(header) : 0;
    /* Set the date in the structure */
    time_t rawtime;
    time(&rawtime);
    struct tm* timest = localtime(&rawtime);
    /* Got the time, populate it in the structure */
    entry.year[0] = to_bcd((1900 + timest->tm_year) / 100);   /* 20 first */
    entry.year[1] = to_bcd(timest->tm_year);         /* 22 then */
    entry.month = to_bcd(timest->tm_mon + 1);
    entry.day = to_bcd(timest->tm_mday);
    entry.date = to_bcd(timest->tm_wday);
    entry.hours = to_bcd(timest->tm_hour);
    entry.minutes = to_bcd(timest->tm_min);
    entry.seconds = to_bcd(timest->tm_sec);

    if (fd) {
        fd->entry = entry;
        fd->entry_addr = info.free_entry_addr;
    }

    /* Clear the new allocated pages */
    const size_t page_size = get_page_size(header);
    const uint8_t buffer[64*KB] = { 0 };
    int wr = ctx->write(ctx->arg, buffer, ADDR_FROM_PAGE(header, newp), page_size);
    if (wr < 0) {
        perror("[ZEALFS] Error writing empty page to the disk");
        goto write_error;
    }
    if (new_page_dir) {
        wr = ctx->write(ctx->arg, buffer, ADDR_FROM_PAGE(header, new_page_dir), page_size);
        if (wr < 0) {
            perror("[ZEALFS] Error writing empty page to the disk");
            goto write_error;
        }
    }

    /* Write the new entry back to the disk */
    wr = ctx->write(ctx->arg, &entry, info.free_entry_addr, sizeof(zealfs_entry_t));
    if (wr < 0) {
        perror("[ZEALFS] Error writing the new entry to the disk");
        goto write_error;
    }

    /* Write the new header (bitmap) to the disk too */
    wr = ctx->write(ctx->arg, header, 0, ctx->header_size);
    if (wr < 0) {
        perror("[ZEALFS] Error writing the header back to the disk");
        goto write_error;
    }

    /* Update the FAT table and write it back to the disk */
    wr = ctx->write(ctx->arg, ctx->fat, page_size, ctx->fat_size);
    if (wr < 0) {
        perror("[ZEALFS] Error writing the header back to the disk");
        goto write_error;
    }

    return 0;
write_error :
    /* Restore former header */
    *header = header_backup;
    return wr;
}


/**
 * @brief Create an empty file in the disk image.
 *
 * @note Underneath, this function calls `zealfs_create_both`
 */
int zealfs_create(const char * path, zealfs_context_t* ctx, zealfs_fd_t* fd)
{
    return zealfs_create_both(ctx, 0, path, fd);
}


/**
 * @brief Create an empty directory in the disk image.
 *
 * @note Underneath, this function calls `zealfs_create_both`
 */
int zealfs_mkdir(const char * path, zealfs_context_t* ctx, zealfs_fd_t* fd)
{
    return zealfs_create_both(ctx, 1, path, fd);
}


/**
 * @brief Read data from an opened file.
 *
 * @param path Path of the file to read. (unused)
 * @param buf Buffer to fill with file's data.
 * @param size Size of the buffer.
 * @param offset Offset in the file to start reading from.
 * @param fi File info containing the ZealFS Entry address of the opened file.
 *
 * @return number of bytes read from the file.
 */
int zealfs_read(zealfs_context_t* ctx, zealfs_fd_t* fd,
                void *buf, size_t size, off_t offset)
{
    zealfs_header_t* header = (zealfs_header_t*) ctx->header;
    if (check_header(ctx) || fd == NULL) {
        return -1;
    }
    if (size == 0) {
        return 0;
    }

    const zealfs_entry_t* entry = &fd->entry;
    const int data_bytes_per_page = get_page_size(header);
    int jump_pages = offset / data_bytes_per_page;
    int offset_in_page = offset % data_bytes_per_page;

    assert(offset <= entry->size);
    const uint32_t remaining_in_file = entry->size - offset;
    if (remaining_in_file < size) {
        size = remaining_in_file;
    }
    const int total = size;

    uint_fast16_t current_page = entry->start_page;
    while (jump_pages) {
        current_page = get_next_from_fat(ctx, current_page);
        jump_pages--;
    }

    uint32_t page_addr = ADDR_FROM_PAGE(header, current_page);

    while (size) {
        int count = MIN(data_bytes_per_page - offset_in_page, size);
        /* Read data from disk */
        ctx->read(ctx->arg, buf, page_addr + offset_in_page, count);
        buf += count;
        if (size != count) {
            current_page = get_next_from_fat(ctx, current_page);
            page_addr = ADDR_FROM_PAGE(header, current_page);
        }
        size -= count;
        offset_in_page = 0;
    }

    return total;
}


static int allocate_next(zealfs_context_t* ctx, zealfs_header_t* header, uint_fast16_t current_page)
{
    /* Only allocate a new page if we still need to write some bytes */
    uint_fast16_t next = allocate_page(header);
    if (next == 0) {
        return -ENOSPC;
    }
    /* Link the newly allocated page to the current page */
    set_next_in_fat(ctx, current_page, next);
    return next;
}


int zealfs_write(zealfs_context_t* ctx, zealfs_fd_t* fd,
                 void *buf, size_t size, off_t offset)
{
    zealfs_header_t* header = (zealfs_header_t*) ctx->header;
    if (check_header(ctx) || fd == NULL) {
        return -1;
    }

    if (size == 0) {
        return 0;
    }

    const int data_bytes_per_page = get_page_size(header);
    int jump_pages = offset / data_bytes_per_page;
    int offset_in_page = offset % data_bytes_per_page;
    const int remaining_in_page = data_bytes_per_page - offset_in_page;

    const int total = size;

    /* Check if we have enough pages. */
    if (zealfs_free_space(ctx) + remaining_in_page < size) {
        return -ENOSPC;
    }

    uint_fast16_t current_page = fd->entry.start_page;

    while (jump_pages) {
        int next_page = get_next_from_fat(ctx, current_page);
        if (next_page == 0) {
            /* There is no more pages to browse but we need to write new data, so allocate a new page.
             * This state must only occur when we are browsing the last page! Else, the disk is corrupted */
            if (jump_pages != 1) {
                printf("[ZEALFS] Could not seek file in the disk, possible corruption!\n");
                return -ESPIPE;
            }
            next_page = allocate_next(ctx, header, current_page);
            /* Make sure the new page is valid! */
            if (next_page <= 0) {
                return next_page;
            }
        }
        current_page = next_page;
        jump_pages--;
    }

    while (size) {
        /* Data page cannot be 0 (header) or FAT (1 for sure) */
        assert(current_page > 1);
        uint32_t page_addr = ADDR_FROM_PAGE(header, current_page);
        uint32_t count = MIN(data_bytes_per_page - offset_in_page, size);

        int wr = ctx->write(ctx->arg, buf, page_addr + offset_in_page, count);
        if (wr < 0) {
            perror("[ZEALFS] Error writing data to the disk");
            return wr;
        }
        fd->entry.size += count;
        buf += count;
        size -= count;

        /* In all cases, check the next page */
        uint16_t next = get_next_from_fat(ctx, current_page);
        if (next) {
            current_page = next;
        } else if (size) {
            int next_page = allocate_next(ctx, header, current_page);
            if (next_page < 0) {
                return next_page;
            }
            current_page = next_page;
        }

        offset_in_page = 0;
    }

    return total;
}


int zealfs_flush(zealfs_context_t* ctx, zealfs_fd_t* fd)
{
    zealfs_header_t* header = (zealfs_header_t*) ctx->header;
    if (check_header(ctx) || fd == NULL) {
        return -1;
    }

    /* Write the updated entry back to the disk */
    int wr = ctx->write(ctx->arg, &fd->entry, fd->entry_addr, sizeof(zealfs_entry_t));
    if (wr < 0) {
        perror("[ZEALFS] Error writing the updated entry to the disk");
        return wr;
    }

    /* Write the updated header (bitmap) to the disk */
    wr = ctx->write(ctx->arg, header, 0, get_fs_header_size(header));
    if (wr < 0) {
        perror("[ZEALFS] Error writing the header back to the disk");
        return wr;
    }

    /* Update the FAT table and write it back to the disk */
    wr = ctx->write(ctx->arg, ctx->fat, ADDR_FROM_PAGE(header, 1), ctx->fat_size);
    if (wr < 0) {
        perror("[ZEALFS] Error writing the FAT table back to the disk");
        return wr;
    }

    return 0;
}


/**
 * @brief Open a directory from the disk image.
 *
 * @param path Absolute path to the directory to open.
 * @param info Info structure that will be filled with the zealfs_entry_t address.
 *
 * @return 0 on success, error code else.
 */
int zealfs_opendir(const char * path, zealfs_context_t* ctx, zealfs_fd_t* fd)
{
    browse_out_t info = { 0 };
    zealfs_header_t* header = (zealfs_header_t*) ctx->header;
    if (check_header(ctx) == -1 || fd == NULL) {
        return -1;
    }

    if (strcmp(path, "/") == 0) {
        fd->entry = info.entry;
        fd->entry_addr = get_root_dir_addr(header);
        return 0;
    }

    int index = browse_path(ctx, path + 1, get_root_dir_addr(header), 1, &info);
    if (index != 0) {
        /* Check that the entry is a directory */
        if (info.entry.flags & 1) {
            fd->entry = info.entry;
            fd->entry_addr = ADDR_FROM_PAGE(header, info.entry.start_page);
            return 0;
        }
        return -ENOTDIR;
    }
    return -ENOENT;
}


/**
 * @brief Read all entries from an opened directory.
 */
int zealfs_readdir(zealfs_context_t* ctx, zealfs_fd_t* fd, zealfs_entry_t* ret_entries, int count)
{
    zealfs_entry_t entries[2048];
    zealfs_header_t* header = (zealfs_header_t*) ctx->header;

    if (check_header(ctx) || fd == NULL) {
        return -1;
    }

    const int is_root = fd->entry_addr == get_root_dir_addr(header);
    /* If the directory we are browsing is the root directory, we have less entries */
    int max_entries = is_root ? get_root_dir_max_entries(header) :
                                get_dir_max_entries(header);
    int filled_count = 0;
    uint16_t current_page = fd->entry_addr / get_page_size(header);

    while (1) {
        /* Read all the entries from disk */
        int rd = ctx->read(ctx->arg, (void*) entries, fd->entry_addr, max_entries * sizeof(zealfs_entry_t));
        if (rd < 0) {
            perror("[ZEALFS] Could not readdir data from partition");
            return 0;
        }

        /* Browse each entry, looking for a non-empty one thanks to the flags */
        for (int i = 0; i < max_entries && filled_count < count; i++) {
            const uint8_t flags = entries[i].flags;
            if (flags & IS_OCCUPIED) {
                ret_entries[filled_count] = entries[i];
                filled_count++;
            }
        }

        /* Check if we have filled the requested count */
        if (filled_count >= count) {
            break;
        }

        max_entries = get_dir_max_entries(header);
        /* Get the next page of the directory */
        current_page = get_next_from_fat(ctx, current_page);
        if (current_page == 0) {
            break;
        }
    }

    return filled_count;
}


void zealfs_destroy(zealfs_context_t* ctx)
{
    /* Remove the header that was previously loaded */
    memset(ctx->header, 0, sizeof(ctx->header));
}
