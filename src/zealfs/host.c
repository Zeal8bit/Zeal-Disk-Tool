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
    wchar_t *p = wide(path), full[ZFS_PATH_MAX];
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
    char *slash = strrchr(parent, '\\'), *forward = strrchr(parent, '/');
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
    wchar_t *p = wide(path), *m = wide(mode);
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
        if (snprintf(out, capacity, "%s.zealfs-%lu-%lu-%llu-%u.tmp", path, GetCurrentProcessId(), GetCurrentThreadId(),
                     (unsigned long long) GetTickCount64(), attempt) >= (int) capacity) {
            return NULL;
        }
        wchar_t* p = wide(out);
        HANDLE handle =
            p ? CreateFileW(p, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL)
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
    wchar_t *a = wide(temporary), *b = wide(path);
    BOOL ok = FALSE;
    if (a && b) {
        DWORD attrs = GetFileAttributesW(b);
        if (overwrite && attrs != INVALID_FILE_ATTRIBUTES) {
            ok = ReplaceFileW(b, a, NULL, 0, NULL, NULL);
        } else {
            ok = MoveFileExW(a, b, MOVEFILE_WRITE_THROUGH);
        }
    }
    free(a);
    free(b);
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
    DWORD attrs = GetFileAttributesW(p), failure = GetLastError();
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
            return zfs_error_set(e, errno == EACCES ? ZFS_PERMISSION : ZFS_IO,
                                 "Cannot inspect export destination %s: %s", path, strerror(errno));
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
    int r = directory ? 0 : _wmkdir(p), failure = errno;
    free(p);
#else
    struct stat st;
    bool directory = !stat(path, &st) && S_ISDIR(st.st_mode);
    int r = directory ? 0 : mkdir(path, 0777), failure = errno;
#endif
    if (!r) {
        return ZFS_OK;
    }
    return zfs_error_set(e, failure == EACCES || failure == EPERM ? ZFS_PERMISSION : ZFS_IO,
                         "Cannot create host directory %s: %s", path, strerror(failure));
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
