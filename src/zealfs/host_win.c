/**
 * SPDX-License-Identifier: Apache-2.0
 */
#include "host_private.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <io.h>
#include <fcntl.h>
#include <wchar.h>
#include <direct.h>

static wchar_t* wide(const char* text)
{
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    wchar_t* w = length != 0 ? malloc((size_t) length * sizeof(*w)) : NULL;
    if (w != NULL) {
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, w, length);
    }
    return w;
}

bool zfs_host_resolve(const char* path, char* out, size_t capacity)
{
    wchar_t* wide_path = wide(path);
    wchar_t full[ZFS_PATH_MAX];
    DWORD length = wide_path != NULL ? GetFullPathNameW(wide_path, ZFS_PATH_MAX, full, NULL) : 0;
    free(wide_path);
    return length != 0 && length < ZFS_PATH_MAX &&
           WideCharToMultiByte(CP_UTF8, 0, full, -1, out, (int) capacity, NULL, NULL) > 0;
}

bool zfs_host_exists(const char* path)
{
    wchar_t* p = wide(path);
    bool exists = p != NULL && GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
    free(p);
    return exists;
}

bool zfs_host_free_space(const char* path, uint64_t* bytes)
{
    char parent[ZFS_PATH_MAX];
    if (strlen(path) >= sizeof(parent) - 2) {
        return false;
    }
    strcpy(parent, path);
    char* slash = strrchr(parent, '\\');
    char* forward = strrchr(parent, '/');
    if (forward != NULL && (slash == NULL || forward > slash)) {
        slash = forward;
    }
    if (slash == NULL) {
        return false;
    }
    slash[1] = 0;
    wchar_t* wide_path = wide(parent);
    ULARGE_INTEGER space;
    bool ok = wide_path != NULL && GetDiskFreeSpaceExW(wide_path, &space, NULL, NULL) != 0;
    free(wide_path);
    if (ok) {
        *bytes = space.QuadPart;
    }
    return ok;
}

FILE* zfs_host_open(const char* path, const char* mode)
{
    wchar_t* wide_path = wide(path);
    wchar_t* wide_mode = wide(mode);
    FILE* file = wide_path != NULL && wide_mode != NULL ? _wfopen(wide_path, wide_mode) : NULL;
    free(wide_path);
    free(wide_mode);
    return file;
}

int zfs_seek(FILE* file, uint64_t offset)
{
    return offset > INT64_MAX ? -1 : _fseeki64(file, (int64_t) offset, SEEK_SET);
}

int64_t zfs_tell(FILE* file)
{
    return _ftelli64(file);
}

int zfs_host_remove(const char* path)
{
    wchar_t* wide_path = wide(path);
    int result = wide_path != NULL ? _wremove(wide_path) : -1;
    free(wide_path);
    return result;
}

int zfs_host_sync(FILE* file)
{
    return fflush(file) != 0 || _commit(_fileno(file)) != 0 ? -1 : 0;
}

FILE* zfs_temp_sibling(const char* path, char* out, size_t capacity)
{
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        if (snprintf(out,
                     capacity,
                     "%s.zealfs-%lu-%lu-%llu-%u.tmp",
                     path,
                     GetCurrentProcessId(),
                     GetCurrentThreadId(),
                     (unsigned long long) GetTickCount64(),
                     attempt) >= (int) capacity) {
            return NULL;
        }
        wchar_t* wide_path = wide(out);
        HANDLE handle = wide_path != NULL ? CreateFileW(wide_path,
                                                        GENERIC_READ | GENERIC_WRITE,
                                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                                        NULL,
                                                        CREATE_NEW,
                                                        FILE_ATTRIBUTE_NORMAL,
                                                        NULL)
                                          : INVALID_HANDLE_VALUE;
        DWORD error = GetLastError();
        int fd = handle != INVALID_HANDLE_VALUE ? _open_osfhandle((intptr_t) handle, _O_RDWR | _O_BINARY) : -1;
        if (fd < 0 && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
            errno = EEXIST;
        } else if (fd < 0) {
            errno = EACCES;
        }
        free(wide_path);
        if (fd >= 0) {
            FILE* file = _fdopen(fd, "w+b");
            if (file == NULL) {
                _close(fd);
                zfs_host_remove(out);
            }
            return file;
        }
        if (errno != EEXIST) {
            break;
        }
    }
    return NULL;
}

int zfs_host_publish(const char* temporary, const char* path, bool overwrite)
{
    wchar_t* temporary_path = wide(temporary);
    wchar_t* destination_path = wide(path);
    /* ReplaceFile opens the replacement without sharing, which conflicts with
     * the retained/locked image handle. Same-volume rename permits that handle
     * to stay open with FILE_SHARE_DELETE through publication. */
    DWORD flags = MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0);
    BOOL ok =
        temporary_path != NULL && destination_path != NULL && MoveFileExW(temporary_path, destination_path, flags) != 0;
    DWORD failure = GetLastError();
    free(temporary_path);
    free(destination_path);
    if (ok == 0) {
        SetLastError(failure);
    }
    return ok != 0 ? 0 : -1;
}

zfs_status zfs_host_kind(const char* path, zfs_host_type* type, zfs_error* error)
{
    wchar_t* wide_path = wide(path);
    if (wide_path == NULL) {
        return zfs_error_set(error, ZFS_INVALID, "Invalid UTF-8 host path");
    }
    DWORD attrs = GetFileAttributesW(wide_path);
    DWORD failure = GetLastError();
    free(wide_path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (failure != ERROR_FILE_NOT_FOUND && failure != ERROR_PATH_NOT_FOUND) {
            return zfs_error_set(error, ZFS_IO, "Cannot inspect export destination: %s", path);
        }
        *type = ZFS_HOST_MISSING;
    } else if ((attrs & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) != 0) {
        *type = ZFS_HOST_OTHER;
    } else {
        *type = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0 ? ZFS_HOST_DIRECTORY : ZFS_HOST_FILE;
    }
    return ZFS_OK;
}

zfs_status zfs_host_make_directory(const char* path, zfs_error* error)
{
    wchar_t* wide_path = wide(path);
    if (wide_path == NULL) {
        return zfs_error_set(error, ZFS_INVALID, "Invalid UTF-8 host path");
    }
    DWORD attrs = GetFileAttributesW(wide_path);
    bool directory = attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    int result = directory ? 0 : _wmkdir(wide_path);
    int failure = errno;
    free(wide_path);
    if (result == 0) {
        return ZFS_OK;
    }
    return zfs_error_set(error,
                         failure == EACCES || failure == EPERM ? ZFS_PERMISSION : ZFS_IO,
                         "Cannot create host directory %s: %s",
                         path,
                         strerror(failure));
}

bool zfs_host_separator(char c)
{
    return c == '/' || c == '\\';
}

size_t zfs_host_mkdir_start(const char* current)
{
    size_t first = 0;
    if (current[0] != 0 && current[1] == ':') {
        first = 2;
    } else if (zfs_host_separator(current[0]) && zfs_host_separator(current[1])) {
        /* UNC server/share roots already exist; never try to create a server. */
        first = 2;
        while (current[first] != 0 && !zfs_host_separator(current[first])) {
            ++first;
        }
        if (current[first] != 0) {
            ++first;
        }
        while (current[first] != 0 && !zfs_host_separator(current[first])) {
            ++first;
        }
    }
    return first;
}

zfs_status zfs_host_expand_files(zfs_host_expansion* expansion, zfs_error* error)
{
    zfs_status status = ZFS_OK;
    snprintf(expansion->path + expansion->prefix, sizeof(expansion->path) - expansion->prefix, "*");
    wchar_t* search = wide(expansion->path);
    WIN32_FIND_DATAW entry;
    HANDLE directory = search != NULL ? FindFirstFileW(search, &entry) : INVALID_HANDLE_VALUE;
    free(search);
    if (directory == INVALID_HANDLE_VALUE) {
        return zfs_error_set(error, ZFS_IO, "Cannot list import source directory: %s", expansion->pattern);
    }
    do {
        if ((entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)) != 0) {
            continue;
        }
        if (WideCharToMultiByte(CP_UTF8,
                                WC_ERR_INVALID_CHARS,
                                entry.cFileName,
                                -1,
                                expansion->path + expansion->prefix,
                                (int) (sizeof(expansion->path) - expansion->prefix),
                                NULL,
                                NULL) == 0) {
            status = zfs_error_set(error, ZFS_INVALID, "Import source path is too long or cannot be decoded");
            break;
        }
        if (zfs_wildcard_match(expansion->name, expansion->path + expansion->prefix)) {
            status = zfs_host_append_path(expansion->paths, expansion->count, expansion->path, error);
        }
    } while (status == ZFS_OK && FindNextFileW(directory, &entry) != 0);
    DWORD failure = GetLastError();
    FindClose(directory);
    if (status == ZFS_OK && failure != ERROR_NO_MORE_FILES) {
        status = zfs_error_set(error, ZFS_IO, "Cannot finish listing import sources");
    }
    return status;
}
