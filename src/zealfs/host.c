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

static wchar_t* wide(const char* s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    wchar_t* w = n ? malloc((size_t) n * sizeof(*w)) : NULL;
    if (w) {
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n);
    }
    return w;
}

bool zfs_host_resolve(const char* path, char* out, size_t capacity)
{
    wchar_t* p = wide(path);
    wchar_t full[ZFS_PATH_MAX];
    DWORD n = p ? GetFullPathNameW(p, ZFS_PATH_MAX, full, NULL) : 0;
    free(p);
    return n && n < ZFS_PATH_MAX && WideCharToMultiByte(CP_UTF8, 0, full, -1, out, (int) capacity, NULL, NULL) > 0;
}

bool zfs_host_exists(const char* path)
{
    wchar_t* p = wide(path);
    bool exists = p && GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
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
    if (forward && (!slash || forward > slash)) {
        slash = forward;
    }
    if (!slash) {
        return false;
    }
    slash[1] = 0;
    wchar_t* p = wide(parent);
    ULARGE_INTEGER space;
    bool ok = p && GetDiskFreeSpaceExW(p, &space, NULL, NULL);
    free(p);
    if (ok) {
        *bytes = space.QuadPart;
    }
    return ok;
}

FILE* zfs_host_open(const char* path, const char* mode)
{
    wchar_t* p = wide(path);
    wchar_t* m = wide(mode);
    FILE* f = p && m ? _wfopen(p, m) : NULL;
    free(p);
    free(m);
    return f;
}

int zfs_seek(FILE* f, uint64_t offset)
{
    return offset > INT64_MAX ? -1 : _fseeki64(f, (int64_t) offset, SEEK_SET);
}

int64_t zfs_tell(FILE* f)
{
    return _ftelli64(f);
}

int zfs_host_remove(const char* path)
{
    wchar_t* p = wide(path);
    int r = p ? _wremove(p) : -1;
    free(p);
    return r;
}

int zfs_host_sync(FILE* f)
{
    return fflush(f) || _commit(_fileno(f)) ? -1 : 0;
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
        wchar_t* p = wide(out);
        HANDLE handle = p ? CreateFileW(p,
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
        free(p);
        if (fd >= 0) {
            FILE* f = _fdopen(fd, "w+b");
            if (!f) {
                _close(fd);
                zfs_host_remove(out);
            }
            return f;
        }
        if (errno != EEXIST) {
            break;
        }
    }
    return NULL;
}

int zfs_host_publish(const char* temporary, const char* path, bool overwrite)
{
    wchar_t* a = wide(temporary);
    wchar_t* b = wide(path);
    /* ReplaceFile opens the replacement without sharing, which conflicts with
     * the retained/locked image handle. Same-volume rename permits that handle
     * to stay open with FILE_SHARE_DELETE through publication. */
    DWORD flags = MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0);
    BOOL ok = a && b && MoveFileExW(a, b, flags);
    DWORD failure = GetLastError();
    free(a);
    free(b);
    if (!ok) {
        SetLastError(failure);
    }
    return ok ? 0 : -1;
}
#else
#include <sys/statvfs.h>
#include <unistd.h>
#include <fcntl.h>

bool zfs_host_resolve(const char* path, char* out, size_t capacity)
{
    char* resolved = realpath(path, NULL);
    if (resolved) {
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
    const char* base = slash ? strrchr(path, '/') + 1 : path;
    if (slash) {
        if (slash == parent) {
            slash[1] = 0;
        } else {
            *slash = 0;
        }
    } else {
        strcpy(parent, ".");
    }
    resolved = realpath(parent, NULL);
    if (!resolved) {
        return false;
    }
    int n = snprintf(out, capacity, "%s/%s", resolved, base);
    free(resolved);
    return n >= 0 && (size_t) n < capacity;
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
    if (!slash) {
        return false;
    }
    slash[1] = 0;
    struct statvfs info;
    if (statvfs(parent, &info)) {
        return false;
    }
    if (info.f_frsize && (uint64_t) info.f_bavail > UINT64_MAX / info.f_frsize) {
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

int zfs_seek(FILE* f, uint64_t offset)
{
    return offset > INT64_MAX ? -1 : fseeko(f, (off_t) offset, SEEK_SET);
}

int64_t zfs_tell(FILE* f)
{
    return (int64_t) ftello(f);
}

int zfs_host_remove(const char* path)
{
    return unlink(path);
}

int zfs_host_sync(FILE* f)
{
    return fflush(f) || fsync(fileno(f)) ? -1 : 0;
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
    FILE* f = fdopen(fd, "w+b");
    if (!f) {
        close(fd);
        unlink(out);
    }
    return f;
}

int zfs_host_publish(const char* temporary, const char* path, bool overwrite)
{
    struct stat st;
    if (overwrite && lstat(path, &st) == 0) {
        if (!S_ISREG(st.st_mode) || chmod(temporary, st.st_mode & 0777)) {
            return -1;
        }
    }
    int r;
    if (overwrite) {
        r = rename(temporary, path);
    } else {
        r = link(temporary, path);
        if (!r) {
            unlink(temporary);
        }
    }
    if (!r) {
        char parent[ZFS_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s", path);
        char* slash = strrchr(parent, '/');
        if (slash) {
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
    return r;
}
#endif

#ifdef _WIN32
#include <direct.h>
#endif
zfs_status zfs_host_kind(const char* path, zfs_host_type* type, zfs_error* e)
{
#ifdef _WIN32
    wchar_t* p = wide(path);
    if (!p) {
        return zfs_error_set(e, ZFS_INVALID, "Invalid UTF-8 host path");
    }
    DWORD attrs = GetFileAttributesW(p);
    DWORD failure = GetLastError();
    free(p);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (failure != ERROR_FILE_NOT_FOUND && failure != ERROR_PATH_NOT_FOUND) {
            return zfs_error_set(e, ZFS_IO, "Cannot inspect export destination: %s", path);
        }
        *type = ZFS_HOST_MISSING;
    } else if (attrs & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) {
        *type = ZFS_HOST_OTHER;
    } else {
        *type = attrs & FILE_ATTRIBUTE_DIRECTORY ? ZFS_HOST_DIRECTORY : ZFS_HOST_FILE;
    }
#else
    struct stat st;
    if (lstat(path, &st)) {
        if (errno != ENOENT) {
            return zfs_error_set(e,
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

static zfs_status make_directory(const char* path, zfs_error* e)
{
#ifdef _WIN32
    wchar_t* p = wide(path);
    if (!p) {
        return zfs_error_set(e, ZFS_INVALID, "Invalid UTF-8 host path");
    }
    DWORD attrs = GetFileAttributesW(p);
    bool directory = attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
    int r = directory ? 0 : _wmkdir(p);
    int failure = errno;
    free(p);
#else
    struct stat st;
    bool directory = !stat(path, &st) && S_ISDIR(st.st_mode);
    int r = directory ? 0 : mkdir(path, 0777);
    int failure = errno;
#endif
    if (!r) {
        return ZFS_OK;
    }
    return zfs_error_set(e,
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

zfs_status zfs_host_mkdirs(const char* path, zfs_error* e)
{
    if (!path || !*path || strlen(path) >= ZFS_PATH_MAX) {
        return zfs_error_set(e, ZFS_INVALID, "Invalid host directory path");
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
        if (!current[i] || separator(current[i])) {
            char saved = current[i];
            if (i && !separator(current[i - 1]) && i != first) {
                current[i] = 0;
                zfs_status s = make_directory(current, e);
                current[i] = saved;
                if (s) {
                    return s;
                }
            }
            if (!saved) {
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
    while (*name) {
        if (*pattern == '*') {
            star = ++pattern;
            retry = name;
        } else if (*pattern == '?') {
            ++pattern;
            name = next_character(name);
        } else if (*pattern == *name) {
            ++pattern;
            ++name;
        } else if (star && *retry) {
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
    return !*pattern;
}

static zfs_status append_path(char*** paths, size_t* count, const char* path, zfs_error* e)
{
    if (*count >= SIZE_MAX / sizeof(**paths) - 1) {
        return zfs_error_set(e, ZFS_NO_MEMORY, "Too many import sources");
    }
    char* copy = malloc(strlen(path) + 1);
    if (!copy) {
        return zfs_error_set(e, ZFS_NO_MEMORY, "Cannot allocate import path");
    }
    strcpy(copy, path);
    char** grown = realloc(*paths, (*count + 1) * sizeof(*grown));
    if (!grown) {
        free(copy);
        return zfs_error_set(e, ZFS_NO_MEMORY, "Cannot allocate import sources");
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

zfs_status zfs_host_expand(const char* pattern, char*** paths, size_t* count, zfs_error* e)
{
    if (!pattern || !*pattern || strlen(pattern) >= ZFS_PATH_MAX) {
        return zfs_error_set(e, ZFS_INVALID, "Invalid import source path");
    }
    if (!strpbrk(pattern, "*?")) {
        return append_path(paths, count, pattern, e);
    }
    const char* name = pattern;
    for (const char* p = pattern; *p; ++p) {
        if (separator(*p)) {
            name = p + 1;
        }
    }
    size_t prefix = (size_t) (name - pattern);
    if (strcspn(pattern, "*?") < prefix) {
        return zfs_error_set(e, ZFS_INVALID, "Wildcards are supported in filenames, not directory names");
    }
    char path[ZFS_PATH_MAX];
    memcpy(path, pattern, prefix);
    path[prefix] = 0;
    size_t before = *count;
    zfs_status s = ZFS_OK;
#ifdef _WIN32
    snprintf(path + prefix, sizeof(path) - prefix, "*");
    wchar_t* search = wide(path);
    WIN32_FIND_DATAW entry;
    HANDLE directory = search ? FindFirstFileW(search, &entry) : INVALID_HANDLE_VALUE;
    free(search);
    if (directory == INVALID_HANDLE_VALUE) {
        return zfs_error_set(e, ZFS_IO, "Cannot list import source directory: %s", pattern);
    }
    do {
        if (entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)) {
            continue;
        }
        if (!WideCharToMultiByte(CP_UTF8,
                                 WC_ERR_INVALID_CHARS,
                                 entry.cFileName,
                                 -1,
                                 path + prefix,
                                 (int) (sizeof(path) - prefix),
                                 NULL,
                                 NULL)) {
            s = zfs_error_set(e, ZFS_INVALID, "Import source path is too long or cannot be decoded");
            break;
        }
        if (zfs_wildcard_match(name, path + prefix)) {
            s = append_path(paths, count, path, e);
        }
    } while (!s && FindNextFileW(directory, &entry));
    DWORD failure = GetLastError();
    FindClose(directory);
    if (!s && failure != ERROR_NO_MORE_FILES) {
        s = zfs_error_set(e, ZFS_IO, "Cannot finish listing import sources");
    }
#else
    DIR* directory = opendir(prefix ? path : ".");
    if (!directory) {
        return zfs_error_set(e,
                             errno == EACCES ? ZFS_PERMISSION : ZFS_IO,
                             "Cannot list import source directory: %s",
                             pattern);
    }
    for (;;) {
        errno = 0;
        struct dirent* entry = readdir(directory);
        if (!entry) {
            if (errno) {
                s = zfs_error_set(e, ZFS_IO, "Cannot finish listing import sources");
            }
            break;
        }
        if (!zfs_wildcard_match(name, entry->d_name)) {
            continue;
        }
        int n = snprintf(path + prefix, sizeof(path) - prefix, "%s", entry->d_name);
        struct stat st;
        if (n < 0 || (size_t) n >= sizeof(path) - prefix) {
            s = zfs_error_set(e, ZFS_INVALID, "Import source path is too long");
        } else if (stat(path, &st)) {
            s = zfs_error_set(e, ZFS_IO, "Cannot inspect import source: %s", path);
        } else if (S_ISREG(st.st_mode)) {
            s = append_path(paths, count, path, e);
        }
        if (s) {
            break;
        }
    }
    closedir(directory);
#endif
    return s ? s : *count == before ? zfs_error_set(e, ZFS_NOT_FOUND, "No files match: %s", pattern) : ZFS_OK;
}
