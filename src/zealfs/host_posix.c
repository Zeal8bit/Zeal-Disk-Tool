/**
 * SPDX-License-Identifier: Apache-2.0
 */
#include "host_private.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

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

zfs_status zfs_host_kind(const char* path, zfs_host_type* type, zfs_error* error)
{
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
    return ZFS_OK;
}

zfs_status zfs_host_make_directory(const char* path, zfs_error* error)
{
    struct stat st;
    bool directory = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
    int result = directory ? 0 : mkdir(path, 0777);
    int failure = errno;
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
    return c == '/';
}

size_t zfs_host_mkdir_start(const char* current)
{
    (void) current;
    return 0;
}

zfs_status zfs_host_expand_files(zfs_host_expansion* expansion, zfs_error* error)
{
    zfs_status status = ZFS_OK;
    DIR* directory = opendir(expansion->prefix != 0 ? expansion->path : ".");
    if (directory == NULL) {
        return zfs_error_set(error,
                             errno == EACCES ? ZFS_PERMISSION : ZFS_IO,
                             "Cannot list import source directory: %s",
                             expansion->pattern);
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
        if (!zfs_wildcard_match(expansion->name, entry->d_name)) {
            continue;
        }
        int length = snprintf(expansion->path + expansion->prefix,
                              sizeof(expansion->path) - expansion->prefix,
                              "%s",
                              entry->d_name);
        struct stat st;
        if (length < 0 || (size_t) length >= sizeof(expansion->path) - expansion->prefix) {
            status = zfs_error_set(error, ZFS_INVALID, "Import source path is too long");
        } else if (stat(expansion->path, &st) != 0) {
            status = zfs_error_set(error, ZFS_IO, "Cannot inspect import source: %s", expansion->path);
        } else if (S_ISREG(st.st_mode)) {
            status = zfs_host_append_path(expansion->paths, expansion->count, expansion->path, error);
        }
        if (status != ZFS_OK) {
            break;
        }
    }
    closedir(directory);
    return status;
}
