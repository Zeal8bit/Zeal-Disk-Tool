/**
 * SPDX-License-Identifier: Apache-2.0
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <wchar.h>

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
#else
#include <sys/statvfs.h>
#include <unistd.h>
#include <fcntl.h>

bool zfs_host_resolve(const char* path, char* out, size_t capacity)
{
    char* resolved = realpath(path, NULL);
    if (resolved != NULL) {
        bool ok = strlen(resolved) < capacity;
        if (ok) {
            strcpy(out, resolved);
        }
        free(resolved);
        return ok;
    }
    char parent[ZFS_PATH_MAX];
    if (strlen(path) >= sizeof(parent)) {
        return false;
    }
    strcpy(parent, path);
    char* slash = strrchr(parent, '/');
    const char* base = slash != NULL ? strrchr(path, '/') + 1 : path;
    if (slash != NULL) {
        if (slash == parent) {
            slash[1] = 0;
        } else {
            *slash = 0;
        }
    } else {
        strcpy(parent, ".");
    }
    resolved = realpath(parent, NULL);
    if (resolved == NULL) {
        return false;
    }
    int length = snprintf(out, capacity, "%s/%s", resolved, base);
    free(resolved);
    return length >= 0 && (size_t) length < capacity;
}

bool zfs_host_exists(const char* path)
{
    struct stat st;
    return lstat(path, &st) == 0;
}

bool zfs_host_free_space(const char* path, uint64_t* bytes)
{
    char parent[ZFS_PATH_MAX];
    if (strlen(path) >= sizeof(parent)) {
        return false;
    }
    strcpy(parent, path);
    char* slash = strrchr(parent, '/');
    if (slash == NULL) {
        return false;
    }
    slash[1] = 0;
    struct statvfs info;
    if (statvfs(parent, &info) != 0) {
        return false;
    }
    if (info.f_frsize != 0 && (uint64_t) info.f_bavail > UINT64_MAX / info.f_frsize) {
        *bytes = UINT64_MAX;
    } else {
        *bytes = (uint64_t) info.f_bavail * info.f_frsize;
    }
    return true;
}

FILE* zfs_host_open(const char* path, const char* mode)
{
    return fopen(path, mode);
}

int zfs_seek(FILE* file, uint64_t offset)
{
    return offset > INT64_MAX ? -1 : fseeko(file, (off_t) offset, SEEK_SET);
}

int64_t zfs_tell(FILE* file)
{
    return (int64_t) ftello(file);
}

int zfs_host_remove(const char* path)
{
    return unlink(path);
}

int zfs_host_sync(FILE* file)
{
    return fflush(file) != 0 || fsync(fileno(file)) != 0 ? -1 : 0;
}

FILE* zfs_temp_sibling(const char* path, char* out, size_t capacity)
{
    if (snprintf(out, capacity, "%s.zealfs-XXXXXX", path) >= (int) capacity) {
        return NULL;
    }
    int fd = mkstemp(out);
    if (fd < 0) {
        return NULL;
    }
    FILE* file = fdopen(fd, "w+b");
    if (file == NULL) {
        close(fd);
        unlink(out);
    }
    return file;
}

int zfs_host_publish(const char* temporary, const char* path, bool overwrite)
{
    struct stat st;
    if (overwrite && lstat(path, &st) == 0) {
        if (!S_ISREG(st.st_mode) || chmod(temporary, st.st_mode & 0777) != 0) {
            return -1;
        }
    }
    int result;
    if (overwrite) {
        result = rename(temporary, path);
    } else {
        result = link(temporary, path);
        if (result == 0) {
            unlink(temporary);
        }
    }
    if (result == 0) {
        char parent[ZFS_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s", path);
        char* slash = strrchr(parent, '/');
        if (slash != NULL) {
            if (slash == parent) {
                slash[1] = 0;
            } else {
                *slash = 0;
            }
        } else {
            strcpy(parent, ".");
        }
        int fd = open(parent, O_RDONLY);
        if (fd >= 0) { /* The replacement has happened even if directory sync fails. */
            (void) fsync(fd);
            close(fd);
        }
    }
    return result;
}
#endif

#ifdef _WIN32
#include <direct.h>
#endif
zfs_status zfs_host_kind(const char* path, zfs_host_type* type, zfs_error* error)
{
#ifdef _WIN32
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
#else
    struct stat st;
    if (lstat(path, &st) != 0) {
        if (errno != ENOENT) {
            return zfs_error_set(error,
                                 errno == EACCES ? ZFS_PERMISSION : ZFS_IO,
                                 "Cannot inspect export destination %s: %s",
                                 path,
                                 strerror(errno));
        }
        *type = ZFS_HOST_MISSING;
    } else {
        *type = S_ISDIR(st.st_mode) ? ZFS_HOST_DIRECTORY : S_ISREG(st.st_mode) ? ZFS_HOST_FILE : ZFS_HOST_OTHER;
    }
#endif
    return ZFS_OK;
}

static zfs_status make_directory(const char* path, zfs_error* error)
{
#ifdef _WIN32
    wchar_t* wide_path = wide(path);
    if (wide_path == NULL) {
        return zfs_error_set(error, ZFS_INVALID, "Invalid UTF-8 host path");
    }
    DWORD attrs = GetFileAttributesW(wide_path);
    bool directory = attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    int result = directory ? 0 : _wmkdir(wide_path);
    int failure = errno;
    free(wide_path);
#else
    struct stat st;
    bool directory = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
    int result = directory ? 0 : mkdir(path, 0777);
    int failure = errno;
#endif
    if (result == 0) {
        return ZFS_OK;
    }
    return zfs_error_set(error,
                         failure == EACCES || failure == EPERM ? ZFS_PERMISSION : ZFS_IO,
                         "Cannot create host directory %s: %s",
                         path,
                         strerror(failure));
}

static bool separator(char c)
{
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

zfs_status zfs_host_mkdirs(const char* path, zfs_error* error)
{
    if (path == NULL || *path == 0 || strlen(path) >= ZFS_PATH_MAX) {
        return zfs_error_set(error, ZFS_INVALID, "Invalid host directory path");
    }
    char current[ZFS_PATH_MAX];
    strcpy(current, path);
    size_t first = 0;
#ifdef _WIN32
    if (current[0] && current[1] == ':') {
        first = 2;
    } else if (separator(current[0]) && separator(current[1])) {
        /* UNC server/share roots already exist; never try to create a server. */
        first = 2;
        while (current[first] && !separator(current[first])) {
            ++first;
        }
        if (current[first]) {
            ++first;
        }
        while (current[first] && !separator(current[first])) {
            ++first;
        }
    }
#endif
    for (size_t i = first;; ++i) {
        if (current[i] == 0 || separator(current[i])) {
            char saved = current[i];
            if (i != 0 && !separator(current[i - 1]) && i != first) {
                current[i] = 0;
                zfs_status status = make_directory(current, error);
                current[i] = saved;
                if (status != ZFS_OK) {
                    return status;
                }
            }
            if (saved == 0) {
                break;
            }
        }
    }
    return ZFS_OK;
}

#ifndef _WIN32
#include <dirent.h>
#endif

static const char* next_character(const char* text)
{
    ++text;
    while (((unsigned char) *text & 0xc0) == 0x80) {
        ++text;
    }
    return text;
}

/* Match only filename components, consistently on every host. */
bool zfs_wildcard_match(const char* pattern, const char* name)
{
    const char* star = NULL;
    const char* retry = NULL;
    if (*name == '.' && *pattern != '.') {
        return false;
    }
    while (*name != 0) {
        if (*pattern == '*') {
            star = ++pattern;
            retry = name;
        } else if (*pattern == '?') {
            ++pattern;
            name = next_character(name);
        } else if (*pattern == *name) {
            ++pattern;
            ++name;
        } else if (star != NULL && *retry != 0) {
            pattern = star;
            retry = next_character(retry);
            name = retry;
        } else {
            return false;
        }
    }
    while (*pattern == '*') {
        ++pattern;
    }
    return *pattern == 0;
}

static zfs_status append_path(char*** paths, size_t* count, const char* path, zfs_error* error)
{
    if (*count >= SIZE_MAX / sizeof(**paths) - 1) {
        return zfs_error_set(error, ZFS_NO_MEMORY, "Too many import sources");
    }
    char* copy = malloc(strlen(path) + 1);
    if (copy == NULL) {
        return zfs_error_set(error, ZFS_NO_MEMORY, "Cannot allocate import path");
    }
    strcpy(copy, path);
    char** grown = realloc(*paths, (*count + 1) * sizeof(*grown));
    if (grown == NULL) {
        free(copy);
        return zfs_error_set(error, ZFS_NO_MEMORY, "Cannot allocate import sources");
    }
    *paths = grown;
    (*paths)[(*count)++] = copy;
    return ZFS_OK;
}

void zfs_host_paths_free(char** paths, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        free(paths[i]);
    }
    free(paths);
}

zfs_status zfs_host_expand(const char* pattern, char*** paths, size_t* count, zfs_error* error)
{
    if (pattern == NULL || *pattern == 0 || strlen(pattern) >= ZFS_PATH_MAX) {
        return zfs_error_set(error, ZFS_INVALID, "Invalid import source path");
    }
    if (strpbrk(pattern, "*?") == NULL) {
        return append_path(paths, count, pattern, error);
    }
    const char* name = pattern;
    for (const char* component = pattern; *component != 0; ++component) {
        if (separator(*component)) {
            name = component + 1;
        }
    }
    size_t prefix = (size_t) (name - pattern);
    if (strcspn(pattern, "*?") < prefix) {
        return zfs_error_set(error, ZFS_INVALID, "Wildcards are supported in filenames, not directory names");
    }
    char path[ZFS_PATH_MAX];
    memcpy(path, pattern, prefix);
    path[prefix] = 0;
    size_t before = *count;
    zfs_status status = ZFS_OK;
#ifdef _WIN32
    snprintf(path + prefix, sizeof(path) - prefix, "*");
    wchar_t* search = wide(path);
    WIN32_FIND_DATAW entry;
    HANDLE directory = search != NULL ? FindFirstFileW(search, &entry) : INVALID_HANDLE_VALUE;
    free(search);
    if (directory == INVALID_HANDLE_VALUE) {
        return zfs_error_set(error, ZFS_IO, "Cannot list import source directory: %s", pattern);
    }
    do {
        if ((entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)) != 0) {
            continue;
        }
        if (WideCharToMultiByte(CP_UTF8,
                                WC_ERR_INVALID_CHARS,
                                entry.cFileName,
                                -1,
                                path + prefix,
                                (int) (sizeof(path) - prefix),
                                NULL,
                                NULL) == 0) {
            status = zfs_error_set(error, ZFS_INVALID, "Import source path is too long or cannot be decoded");
            break;
        }
        if (zfs_wildcard_match(name, path + prefix)) {
            status = append_path(paths, count, path, error);
        }
    } while (status == ZFS_OK && FindNextFileW(directory, &entry) != 0);
    DWORD failure = GetLastError();
    FindClose(directory);
    if (status == ZFS_OK && failure != ERROR_NO_MORE_FILES) {
        status = zfs_error_set(error, ZFS_IO, "Cannot finish listing import sources");
    }
#else
    DIR* directory = opendir(prefix != 0 ? path : ".");
    if (directory == NULL) {
        return zfs_error_set(error,
                             errno == EACCES ? ZFS_PERMISSION : ZFS_IO,
                             "Cannot list import source directory: %s",
                             pattern);
    }
    for (;;) {
        errno = 0;
        struct dirent* entry = readdir(directory);
        if (entry == NULL) {
            if (errno != 0) {
                status = zfs_error_set(error, ZFS_IO, "Cannot finish listing import sources");
            }
            break;
        }
        if (!zfs_wildcard_match(name, entry->d_name)) {
            continue;
        }
        int length = snprintf(path + prefix, sizeof(path) - prefix, "%s", entry->d_name);
        struct stat st;
        if (length < 0 || (size_t) length >= sizeof(path) - prefix) {
            status = zfs_error_set(error, ZFS_INVALID, "Import source path is too long");
        } else if (stat(path, &st) != 0) {
            status = zfs_error_set(error, ZFS_IO, "Cannot inspect import source: %s", path);
        } else if (S_ISREG(st.st_mode)) {
            status = append_path(paths, count, path, error);
        }
        if (status != ZFS_OK) {
            break;
        }
    }
    closedir(directory);
#endif
    return status != ZFS_OK   ? status
           : *count == before ? zfs_error_set(error, ZFS_NOT_FOUND, "No files match: %s", pattern)
                              : ZFS_OK;
}
