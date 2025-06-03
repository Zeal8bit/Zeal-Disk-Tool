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
#include <inttypes.h>

static const char* s_image_files[] = {
    // "emulated_sd.img",
    // "disk.img",
    // "test_disk.img"
};

static disk_err_t disk_try_open(const char* path, disk_info_t* info, int is_file)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno != ENOENT) {
            fprintf(stderr, "[LINUX] Skipping device %s: %s\n", path, strerror(errno));
        }
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
    if (*out_count < max_disks) {
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
    assert(disk->has_staged_changes);

    static char error_msg[1024];

    /* Reopen the disk to write it back */
    int fd = open(disk->path, O_WRONLY);
    if (fd < 0) {
        sprintf(error_msg, "Could not open disk %s: %s\n", disk->name, strerror(errno));
        return error_msg;
    }

    /* Write MBR */
    if (disk->has_mbr) {
        ssize_t wr = write(fd, disk->staged_mbr, sizeof(disk->staged_mbr));
        if (wr != DISK_SECTOR_SIZE) {
            sprintf(error_msg, "Could not write disk %s: %s\n", disk->name, strerror(errno));
            goto error;
        }
    }

    /* Write any new partition */
    for (int i = 0; i < MAX_PART_COUNT; i++) {
        const partition_t* part = &disk->staged_partitions[i];
        if (part->data != NULL && part->data_len != 0) {
            /* Data need to be written back to the disk */
            off_t part_offset = part->start_lba * DISK_SECTOR_SIZE;
            const off_t offset = lseek(fd, part_offset, SEEK_SET);
            printf("[DISK] Writing partition %d @ %08" PRIx64 ", %d bytes\n", i, (uint64_t) offset, part->data_len);
            if (offset != part_offset){
                sprintf(error_msg, "Could not offset in the disk %s: %s\n", disk->name, strerror(errno));
                goto error;
            }
            ssize_t wr = write(fd, part->data, part->data_len);
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


int disk_open(disk_info_t* disk, void** ret_fd)
{
    assert(disk);
    assert(disk->valid);

    int fd = open(disk->path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[LINUX] Could not open disk %s: %s\n", disk->name, strerror(errno));
        return 1;
    }

    /* Prevent a warning about casting different size integers */
    *ret_fd = (void*)(intptr_t) fd;
    return 0;
}


ssize_t disk_read(void* disk_fd, void* buffer, off_t disk_offset, uint32_t len)
{
    int fd = (int)(intptr_t) disk_fd;
    if (lseek(fd, disk_offset, SEEK_SET) != disk_offset) {
        fprintf(stderr, "[LINUX] Could not seek to offset %" PRId64 ": %s\n", (uint64_t) disk_offset, strerror(errno));
        return -1;
    }

    ssize_t bytes_read = read(fd, buffer, len);
    if (bytes_read < 0) {
        fprintf(stderr, "[LINUX] Could not read from disk: %s\n", strerror(errno));
    }

    return bytes_read;
}


ssize_t disk_write(void* disk_fd, const void* buffer, off_t disk_offset, uint32_t len)
{
    int fd = (int)(intptr_t) disk_fd;
    if (lseek(fd, disk_offset, SEEK_SET) != disk_offset) {
        fprintf(stderr, "[LINUX] Could not seek to offset %" PRId64 ": %s\n", (uint64_t) disk_offset, strerror(errno));
        return -1;
    }

    ssize_t bytes_written = write(fd, buffer, len);
    if (bytes_written < 0) {
        fprintf(stderr, "[LINUX] Could not write to disk: %s\n", strerror(errno));
    }

    return bytes_written;
}


void disk_close(void* disk_fd)
{
    close((int)(intptr_t) disk_fd);
}


void disk_init_progress_bar(void)
{
}


void disk_update_progress_bar(int percent)
{
    (void) percent;
}


void disk_destroy_progress_bar(void)
{
}
