/* SPDX-License-Identifier: Apache-2.0 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <wchar.h>
static wchar_t *wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    wchar_t *w = n ? malloc((size_t)n * sizeof(*w)) : NULL;
    if (w)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n);
    return w;
}
bool zfs_host_resolve(const char *path, char *out, size_t capacity) {
    wchar_t *p = wide(path), full[ZFS_PATH_MAX];
    DWORD n = p ? GetFullPathNameW(p, ZFS_PATH_MAX, full, NULL) : 0;
    free(p);
    return n && n < ZFS_PATH_MAX &&
           WideCharToMultiByte(CP_UTF8, 0, full, -1, out, (int)capacity, NULL, NULL) > 0;
}
FILE *zfs_host_open(const char *path, const char *mode) {
    wchar_t *p = wide(path), *m = wide(mode);
    FILE *f = p && m ? _wfopen(p, m) : NULL;
    free(p);
    free(m);
    return f;
}
int zfs_seek(FILE *f, uint64_t offset) {
    return offset > INT64_MAX ? -1 : _fseeki64(f, (int64_t)offset, SEEK_SET);
}
int64_t zfs_tell(FILE *f) { return _ftelli64(f); }
int zfs_host_remove(const char *path) {
    wchar_t *p = wide(path);
    int r = p ? _wremove(p) : -1;
    free(p);
    return r;
}
int zfs_host_sync(FILE *f) { return fflush(f) || _commit(_fileno(f)) ? -1 : 0; }
FILE *zfs_temp_sibling(const char *path, char *out, size_t capacity) {
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        if (snprintf(out, capacity, "%s.zealfs-%lu-%lu-%llu-%u.tmp", path, GetCurrentProcessId(),
                     GetCurrentThreadId(), (unsigned long long)GetTickCount64(), attempt) >= (int)capacity)
            return NULL;
        wchar_t *p = wide(out);
        int fd = p ? _wopen(p, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE) : -1;
        free(p);
        if (fd >= 0) {
            FILE *f = _fdopen(fd, "w+b");
            if (!f) {
                _close(fd);
                zfs_host_remove(out);
            }
            return f;
        }
        if (errno != EEXIST)
            break;
    }
    return NULL;
}
int zfs_host_publish(const char *temporary, const char *path, bool overwrite) {
    wchar_t *a = wide(temporary), *b = wide(path);
    BOOL ok = FALSE;
    if (a && b) {
        DWORD attrs = GetFileAttributesW(b);
        if (overwrite && attrs != INVALID_FILE_ATTRIBUTES)
            ok = ReplaceFileW(b, a, NULL, 0, NULL, NULL);
        else
            ok = MoveFileExW(a, b, MOVEFILE_WRITE_THROUGH);
    }
    free(a);
    free(b);
    return ok ? 0 : -1;
}
#else
#include <unistd.h>
#include <fcntl.h>
bool zfs_host_resolve(const char *path, char *out, size_t capacity) {
    char *resolved = realpath(path, NULL);
    if (resolved) {
        bool ok = strlen(resolved) < capacity;
        if (ok)
            strcpy(out, resolved);
        free(resolved);
        return ok;
    }
    char parent[ZFS_PATH_MAX];
    if (strlen(path) >= sizeof(parent))
        return false;
    strcpy(parent, path);
    char *slash = strrchr(parent, '/');
    const char *base = slash ? strrchr(path, '/') + 1 : path;
    if (slash) {
        if (slash == parent)
            slash[1] = 0;
        else
            *slash = 0;
    } else
        strcpy(parent, ".");
    resolved = realpath(parent, NULL);
    if (!resolved)
        return false;
    int n = snprintf(out, capacity, "%s/%s", resolved, base);
    free(resolved);
    return n >= 0 && (size_t)n < capacity;
}
FILE *zfs_host_open(const char *path, const char *mode) { return fopen(path, mode); }
int zfs_seek(FILE *f, uint64_t offset) {
    return offset > INT64_MAX ? -1 : fseeko(f, (off_t)offset, SEEK_SET);
}
int64_t zfs_tell(FILE *f) { return (int64_t)ftello(f); }
int zfs_host_remove(const char *path) { return unlink(path); }
int zfs_host_sync(FILE *f) { return fflush(f) || fsync(fileno(f)) ? -1 : 0; }
FILE *zfs_temp_sibling(const char *path, char *out, size_t capacity) {
    if (snprintf(out, capacity, "%s.zealfs-XXXXXX", path) >= (int)capacity)
        return NULL;
    int fd = mkstemp(out);
    if (fd < 0)
        return NULL;
    FILE *f = fdopen(fd, "w+b");
    if (!f) {
        close(fd);
        unlink(out);
    }
    return f;
}
int zfs_host_publish(const char *temporary, const char *path, bool overwrite) {
    struct stat st;
    if (overwrite && lstat(path, &st) == 0) {
        if (!S_ISREG(st.st_mode) || chmod(temporary, st.st_mode & 0777))
            return -1;
    }
    int r;
    if (overwrite)
        r = rename(temporary, path);
    else {
        r = link(temporary, path);
        if (!r)
            unlink(temporary);
    }
    if (!r) {
        char parent[ZFS_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s", path);
        char *slash = strrchr(parent, '/');
        if (slash) {
            if (slash == parent)
                slash[1] = 0;
            else
                *slash = 0;
        } else
            strcpy(parent, ".");
        int fd = open(parent, O_RDONLY);
        if (fd >= 0) { /* The replacement has happened even if directory sync fails. */
            (void)fsync(fd);
            close(fd);
        }
    }
    return r;
}
#endif
