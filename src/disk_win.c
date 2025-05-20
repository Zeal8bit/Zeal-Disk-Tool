/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <windows.h>
#include <commctrl.h>  // for Progress Bar
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include "disk.h"
#define MIN(a,b)    (((a) < (b)) ? (a) : (b))

disk_err_t disk_list(disk_info_t* out_disks, int max_disks, int* out_count) {
    *out_count = 0;
    for (int i = 0; i < max_disks && *out_count < max_disks; ++i) {
        char path[256];
        snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%d", i);

        HANDLE hDisk = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hDisk == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            if (error == ERROR_ACCESS_DENIED) {
                return ERR_NOT_ADMIN;
            }
            continue;
        }

        disk_info_t* info = &out_disks[*out_count];
        snprintf(info->name, sizeof(info->name), "PhysicalDrive%d", i);
        strcpy(info->path, path);

        /* Get the size of the disk, exclude any disk bigger than 32GB to prevent mistakes */
        GET_LENGTH_INFORMATION lenInfo;
        DWORD bytesReturned;
        if (DeviceIoControl(hDisk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &lenInfo, sizeof(lenInfo), &bytesReturned, NULL)) {
            info->size_bytes = lenInfo.Length.QuadPart;
        } else {
            info->size_bytes = 0;
        }


        info->valid = info->size_bytes <= MAX_DISK_SIZE;
        if (!info->valid) {
            fprintf(stderr, "%s exceeds max disk size of %lluGB with %lluGB bytes\n", path, MAX_DISK_SIZE/GB, info->size_bytes/GB);
        }

        /* Read MBR */
        DWORD bytesRead;
        SetFilePointer(hDisk, 0, NULL, FILE_BEGIN);
        if (ReadFile(hDisk, info->mbr, sizeof(info->mbr), &bytesRead, NULL) && bytesRead == DISK_SECTOR_SIZE) {
            info->has_mbr = (info->mbr[DISK_SECTOR_SIZE - 2] == 0x55 &&
                             info->mbr[DISK_SECTOR_SIZE - 1] == 0xAA);
        } else {
            info->has_mbr = false;
        }

        CloseHandle(hDisk);
        (*out_count)++;
    }

    return ERR_SUCCESS;
}


const char* disk_write_changes(disk_info_t* disk)
{
#if DEBUG_DISKS
    return NULL;
#endif
    static char error_msg[1024];
    assert(disk);
    assert(disk->has_staged_changes);


    HANDLE fd = CreateFileA(disk->path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (fd == INVALID_HANDLE_VALUE) {
        snprintf(error_msg, sizeof(error_msg),
            "Could not open disk %s: %lu\n", disk->path, GetLastError());
        return error_msg;
    }

    if (disk->has_mbr) {
        /* Let's be safe and set the pointer */
        SetFilePointer(fd, 0, NULL, FILE_BEGIN);
        DWORD wr = 0;
        BOOL success = WriteFile(fd, disk->staged_mbr, 512, &wr, NULL);
        if (!success || wr != DISK_SECTOR_SIZE) {
            snprintf(error_msg, sizeof(error_msg),
                "Could not write disk %s: %lu\n", disk->name, GetLastError());
            goto error;
        }
    }

    /* Write any new partition */
    for (int i = 0; i < MAX_PART_COUNT; i++) {
        const partition_t* part = &disk->staged_partitions[i];
        if (part->data != NULL && part->data_len != 0) {
            /* Data need to be written back to the disk */
            LARGE_INTEGER offset = {
                .QuadPart = 0
            };
            LARGE_INTEGER part_offset = {
                .QuadPart = part->start_lba * DISK_SECTOR_SIZE
            };
            BOOL success = SetFilePointerEx(fd, part_offset, &offset, FILE_BEGIN);
            printf("[DISK] Writing partition %d @ %08llx, %d bytes\n", i, offset.QuadPart, part->data_len);
            if (offset.QuadPart != part_offset.QuadPart){
                sprintf(error_msg, "Could not offset in the disk %s: %lu\n", disk->name, GetLastError());
                goto error;
            }
            DWORD wr = 0;
            success = WriteFile(fd, part->data, part->data_len, &wr, NULL);
            if (!success || wr != part->data_len) {
                sprintf(error_msg, "Could not write partition to disk %s: %lu\n", disk->name, GetLastError());
                goto error;
            }
        } else {
            printf("[DISK] Partition %d has no changes\n", i);
        }
    }

    /* Apply the changes in RAM too */
    disk_apply_changes(disk);
    CloseHandle(fd);
    return NULL;
error:
    CloseHandle(fd);
    return error_msg;
}


static int set_errno(void)
{
    DWORD error_code = GetLastError();
    switch (error_code) {
        case ERROR_ACCESS_DENIED:
            errno = EACCES;
            break;
        case ERROR_INVALID_PARAMETER:
            errno = EINVAL;
            break;
        case ERROR_NOT_ENOUGH_MEMORY:
            errno = ENOMEM;
            break;
        case ERROR_HANDLE_EOF:
            errno = ENODATA;
            break;
        default:
            errno = EIO;
            break;
    }
    return -errno;
}


int disk_open(disk_info_t* disk, void** ret_fd)
{
    assert(disk);
    assert(ret_fd);

    HANDLE fd = CreateFileA(disk->path,
                            GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, 0, NULL);
    if (fd == INVALID_HANDLE_VALUE) {
        return set_errno();
    }

    *ret_fd = fd;
    return 0;
}


ssize_t disk_read(void* disk_fd, void* buffer, off_t disk_offset, uint32_t len)
{
    HANDLE handle = (HANDLE) disk_fd;
    uint8_t temp_buffer[DISK_SECTOR_SIZE];
    ssize_t total_read = 0;
    DWORD bytes_read;
    BOOL success;

    assert(handle != INVALID_HANDLE_VALUE);
    assert(buffer);

    LARGE_INTEGER li_offset = {
        .QuadPart = disk_offset,
    };
    if (!SetFilePointerEx(handle, li_offset, NULL, FILE_BEGIN)) {
        return set_errno();
    }

    // Read the aligned portion
    const uint32_t aligned_len = len & ~(DISK_SECTOR_SIZE - 1);
    if (aligned_len > 0) {
        success = ReadFile(handle, buffer, aligned_len, &bytes_read, NULL);
        if (!success || bytes_read != aligned_len) {
            return set_errno();
        }
        buffer += bytes_read;
        total_read += bytes_read;
    }

    // Read the remaining portion
    const uint32_t remaining_len = len & (DISK_SECTOR_SIZE - 1);
    if (remaining_len > 0) {
        success = ReadFile(handle, temp_buffer, DISK_SECTOR_SIZE, &bytes_read, NULL);
        if (!success || bytes_read != DISK_SECTOR_SIZE) {
            return set_errno();
        }
        memcpy(buffer, temp_buffer, remaining_len);
        total_read += remaining_len;
    }

    return total_read;
}


ssize_t disk_write(void* disk_fd, const void* buffer, off_t disk_offset, uint32_t len)
{
    DWORD bytes_written;
    HANDLE handle = (HANDLE) disk_fd;
    assert(handle != INVALID_HANDLE_VALUE);
    assert(buffer);
    assert(len > 0);

    LARGE_INTEGER li_offset = {
        .QuadPart = disk_offset
    };
    if (!SetFilePointerEx(handle, li_offset, NULL, FILE_BEGIN)) {
        return set_errno();
    }

    BOOL success = WriteFile(handle, buffer, (DWORD)len, &bytes_written, NULL);
    if (!success || bytes_written != len) {
        return set_errno();
    }

    return bytes_written;
}


void disk_close(void* disk_fd)
{
    HANDLE handle = (HANDLE) disk_fd;
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
    }
}


static HWND hwndProgress = NULL;
static HWND hwndWindow = NULL;

extern int winWidth;
extern int winHeight;
extern int winX;
extern int winY;


void disk_init_progress_bar(void) {
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icex);

    int width = 350;
    int height = 100;

    int centerX = winX + winWidth / 2;
    int centerY = winY + winHeight / 2;

    hwndWindow = CreateWindowEx(
        0, WC_DIALOG, "Copying file...", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        centerX - width / 2, centerY - height / 2, width, height,
        NULL, NULL, GetModuleHandle(NULL), NULL);


    hwndProgress = CreateWindowEx(
        0, PROGRESS_CLASS, NULL,
        WS_CHILD | WS_VISIBLE,
        20, 20, 300, 20,
        hwndWindow, NULL, GetModuleHandle(NULL), NULL);

    SendMessage(hwndProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessage(hwndProgress, PBM_SETPOS, 0, 0);

    ShowWindow(hwndWindow, SW_SHOW);
    UpdateWindow(hwndWindow);
}


void disk_update_progress_bar(int percent) {
    if (hwndProgress) {
        SendMessage(hwndProgress, PBM_SETPOS, percent, 0);

        // Process UI messages so it updates
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}


void disk_destroy_progress_bar(void) {
    if (hwndWindow) {
        DestroyWindow(hwndWindow);
        hwndWindow = NULL;
        hwndProgress = NULL;
    }
}