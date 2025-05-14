/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "disk.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/disk.h>


static disk_err_t disk_try_open(const char* path, disk_info_t* info, int is_file)
{
    uint64_t block_count = 0;
    uint64_t block_size = 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "[MAC] Skipping device %s: %s\n", path, strerror(errno));
        return ERR_INVALID;
    }

    if (is_file) {
        struct stat st;
        if (fstat(fd, &st) != 0) {
            fprintf(stderr, "Could not get file %s size: %s\n", path, strerror(errno));
            close(fd);
            return ERR_INVALID;
        }
        info->size_bytes = st.st_size;
    } else if (ioctl(fd, DKIOCGETBLOCKCOUNT, &block_count) != 0 ||
               ioctl(fd, DKIOCGETBLOCKSIZE, &block_size) != 0) {
        fprintf(stderr, "Could not get disk %s size: %s\n", path, strerror(errno));
        close(fd);
        return ERR_INVALID;
    } else {
        info->size_bytes = block_count * block_size;
    }

    info->valid = info->size_bytes <= MAX_DISK_SIZE;
    if (!info->valid) {
        fprintf(stderr, "%s exceeds max disk size of %lluGB with %lluGB bytes\n", path, MAX_DISK_SIZE/GB, info->size_bytes/GB);
    }

    strncpy(info->name, path, sizeof(info->name) - 1);
    strncpy(info->path, path, sizeof(info->path) - 1);

    ssize_t r = read(fd, info->mbr, DISK_SECTOR_SIZE);
    if (r == DISK_SECTOR_SIZE) {
        info->has_mbr = (info->mbr[DISK_SECTOR_SIZE - 2] == 0x55 &&
                         info->mbr[DISK_SECTOR_SIZE - 1] == 0xAA);
    } else {
        info->has_mbr = false;
    }

    close(fd);
    return ERR_SUCCESS;
}

disk_err_t disk_list(disk_info_t* out_disks, int max_disks, int* out_count) {
    memset(out_disks, 0, sizeof(disk_info_t) * max_disks);
    *out_count = 0;

    for (int i = 1; i <= max_disks && *out_count < max_disks; ++i) {
        char path[256];
        snprintf(path, sizeof(path), "/dev/rdisk%d", i);
        disk_err_t err = disk_try_open(path, &out_disks[*out_count], 0);
        if (err == ERR_SUCCESS) {
            (*out_count)++;
        } else if (err == ERR_INVALID) {
            continue;
        } else {
            return err;
        }
    }

    return ERR_SUCCESS;
}


const char* disk_write_changes(disk_info_t* disk)
{
    static char error_msg[1024];
    assert(disk);
    assert(disk->has_mbr);
    assert(disk->has_staged_changes);

    /* Reopen the disk to write it back */
    int fd = open(disk->name, O_WRONLY);
    if (fd < 0) {
        sprintf(error_msg, "Could not open disk %s: %s\n", disk->name, strerror(errno));
        return error_msg;
    }

    /* Write MBR */
    ssize_t wr = write(fd, disk->staged_mbr, sizeof(disk->staged_mbr));
    if (wr != DISK_SECTOR_SIZE) {
        sprintf(error_msg, "Could not write disk %s: %s\n", disk->name, strerror(errno));
        goto error;
    }

    /* Write any new partition */
    for (int i = 0; i < MAX_PART_COUNT; i++) {
        const partition_t* part = &disk->staged_partitions[i];
        if (part->data != NULL && part->data_len != 0) {
            /* Data need to be written back to the disk */
            off_t part_offset = part->start_lba * DISK_SECTOR_SIZE;
            const off_t offset = lseek(fd, part_offset, SEEK_SET);
            printf("[DISK] Writing partition %d @ %08llx, %d bytes\n", i, offset, part->data_len);
            if (offset != part_offset){
                sprintf(error_msg, "Could not offset in the disk %s: %s\n", disk->name, strerror(errno));
                goto error;
            }
            wr = write(fd, part->data, part->data_len);
            if (wr != part->data_len) {
                sprintf(error_msg, "Could not write partition to disk %s: %s\n", disk->name, strerror(errno));
                goto error;
            }
        } else {
            printf("[DISK] Partition %d has no changes\n", i);
        }
    }

    /* Apply the changes in RAM too */
    disk_apply_changes(disk);
    close(fd);
    return NULL;
error:
    close(fd);
    return error_msg;
}


int disk_open(const char* path, int flags) {
    int fd = open(path, flags);
    if (fd < 0) {
        fprintf(stderr, "[DISK] Failed to open %s: %s\n", path, strerror(errno));
    } else {
        printf("[DISK] Opened %s with fd %d\n", path, fd);
    }
    return fd;
}

ssize_t disk_read(int fd, void* buffer, size_t size) {
    ssize_t bytes_read = read(fd, buffer, size);
    if (bytes_read < 0) {
        fprintf(stderr, "[DISK] Failed to read from fd %d: %s\n", fd, strerror(errno));
    } else {
        printf("[DISK] Read %zd bytes from fd %d\n", bytes_read, fd);
    }
    return bytes_read;
}

ssize_t disk_write(int fd, const void* buffer, size_t size) {
    ssize_t bytes_written = write(fd, buffer, size);
    if (bytes_written < 0) {
        fprintf(stderr, "[DISK] Failed to write to fd %d: %s\n", fd, strerror(errno));
    } else {
        printf("[DISK] Wrote %zd bytes to fd %d\n", bytes_written, fd);
    }
    return bytes_written;
}

int disk_close(int fd) {
    int result = close(fd);
    if (result < 0) {
        fprintf(stderr, "[DISK] Failed to close fd %d: %s\n", fd, strerror(errno));
    } else {
        printf("[DISK] Closed fd %d\n", fd);
    }
    return result;
}