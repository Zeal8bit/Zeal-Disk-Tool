/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "disk.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include <linux/fs.h>
#include <sys/ioctl.h>

#define DIM(arr) (sizeof(arr) / sizeof(*(arr)))

static const char* s_image_files[] = {
    // "emulated_sd.img",
    // "backup_sd.img",
    // "test_disk.img"
};

static disk_err_t disk_try_open(const char* path, disk_info_t* info, int is_file)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == EACCES) {
            return ERR_NOT_ADMIN;
        } else if (errno == ENOENT) {
            return ERR_INVALID;
        }
        perror("Could not open the disk");
        return ERR_INVALID;
    }

    strcpy(info->name, path);
    strcpy(info->path, path);

    /* Get the size of the disk, make sure it is not bigger than expected */
    if (is_file) {
        struct stat st;
        if (fstat(fd, &st) != 0) {
            fprintf(stderr, "Could not get file %s size: %s\n", path, strerror(errno));
            close(fd);
            return ERR_INVALID;
        }
        info->size_bytes = st.st_size;
    } else if (ioctl(fd, BLKGETSIZE64, &info->size_bytes) != 0) {
        fprintf(stderr, "Could not get disk %s size: %s\n", path, strerror(errno));
        close(fd);
        return ERR_INVALID;
    }

    info->valid = info->size_bytes <= MAX_DISK_SIZE;
    if (!info->valid) {
        fprintf(stderr, "%s exceeds max disk size of %lluGB with %lluGB bytes\n", path, MAX_DISK_SIZE/GB, info->size_bytes/GB);
    }

    /* Read MBR */
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


disk_err_t disk_list(disk_info_t* out_disks, int max_disks, int* out_count)
{
    memset(out_disks, 0, sizeof(disk_info_t) * max_disks);
    *out_count = 0;
    for (char c = 'a'; c <= 'z' && *out_count < max_disks; ++c) {
        char path[256];
        snprintf(path, sizeof(path), "/dev/sd%c", c);
        disk_err_t err = disk_try_open(path, &out_disks[*out_count], 0);
        if (err == ERR_SUCCESS) {
            (*out_count)++;
        } else if (err == ERR_INVALID) {
            continue;
        } else {
            return err;
        }
    }


    /* Check for images */
    if (0 && *out_count < max_disks) {
        for (size_t i = 0; i < DIM(s_image_files) && *out_count < max_disks; ++i) {
            disk_err_t err = disk_try_open(s_image_files[i], &out_disks[*out_count], 1);
            if (err == ERR_SUCCESS) {
                (*out_count)++;
            }
        }
    }

    return ERR_SUCCESS;
}


const char* disk_write_changes(disk_info_t* disk)
{
    assert(disk);
    assert(disk->valid);
    assert(disk->has_mbr);
    assert(disk->has_staged_changes);

    static char error_msg[1024];

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
            printf("[DISK] Writing partition %d @ %08lx, %d bytes\n", i, offset, part->data_len);
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

    close(fd);
    /* Apply the changes in RAM too */
    disk_apply_changes(disk);
    return NULL;
error:
    close(fd);
    return error_msg;
}