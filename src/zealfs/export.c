/**
 * SPDX-License-Identifier: Apache-2.0
 */
#include "disk_private.h"
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
#include <strings.h>
#endif

static bool safe_name(const char* name)
{
    if (*name == 0 || strchr(name, '/') != NULL || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return false;
    }
#ifdef _WIN32
    if (strpbrk(name, "\\<>:\"|?*") != NULL || name[strlen(name) - 1] == '.' || name[strlen(name) - 1] == ' ') {
        return false;
    }
    char base[ZFS_NAME_MAX + 1];
    snprintf(base, sizeof(base), "%s", name);
    char* dot = strchr(base, '.');
    if (dot != NULL) {
        *dot = 0;
    }
    if (_stricmp(base, "CON") == 0 || _stricmp(base, "PRN") == 0 || _stricmp(base, "AUX") == 0 ||
        _stricmp(base, "NUL") == 0) {
        return false;
    }
    if (strlen(base) == 4 && base[3] >= '1' && base[3] <= '9' &&
        (_strnicmp(base, "COM", 3) == 0 || _strnicmp(base, "LPT", 3) == 0)) {
        return false;
    }
    for (const unsigned char* p = (const unsigned char*) name; *p; ++p) {
        if (*p < 32) {
            return false;
        }
    }
#endif
    return true;
}

static bool same_name(const char* first_name, const char* second_name)
{
#ifdef _WIN32
    return _stricmp(first_name, second_name) == 0;
#elif defined(__APPLE__)
    return strcasecmp(first_name, second_name) == 0;
#else
    return strcmp(first_name, second_name) == 0;
#endif
}

static int compare_names(const void* first, const void* second)
{
#ifdef _WIN32
    return _stricmp(((const zfs_entry*) first)->name, ((const zfs_entry*) second)->name);
#elif defined(__APPLE__)
    return strcasecmp(((const zfs_entry*) first)->name, ((const zfs_entry*) second)->name);
#else
    return strcmp(((const zfs_entry*) first)->name, ((const zfs_entry*) second)->name);
#endif
}

static zfs_status walk(zfs_disk* disk, int part, char* path, char* host, bool directory, bool write, bool overwrite,
                       bool* exists, const char* pattern, zfs_error* error)
{
    zfs_status status = zfs_core_export_destination(disk, host, error);
    if (status != ZFS_OK) {
        return status;
    }
    zfs_host_type kind;
    status = zfs_host_kind(host, &kind, error);
    if (status != ZFS_OK) {
        return status;
    }
    if (kind == ZFS_HOST_OTHER || (directory && kind == ZFS_HOST_FILE) || (!directory && kind == ZFS_HOST_DIRECTORY)) {
        return zfs_error_set(error, ZFS_CONFLICT, "Export destination has an incompatible type or is a link: %s", host);
    }
    if (!directory) {
        if (!write) {
            if (kind == ZFS_HOST_FILE) {
                *exists = true;
            }
            return zfs_core_check_file(disk, part, path, error);
        }
        return zfs_core_export_file(disk, part, path, host, overwrite, error);
    }
    zfs_entry* entries = NULL;
    size_t count = 0;
    status = zfs_list(disk, part, path, &entries, &count, error);
    if (status != ZFS_OK) {
        return status;
    }
    if (pattern != NULL) {
        size_t selected = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!entries[i].directory && zfs_wildcard_match(pattern, entries[i].name)) {
                entries[selected++] = entries[i];
            }
        }
        count = selected;
        if (count == 0) {
            zfs_entries_free(entries);
            return zfs_error_set(error, ZFS_NOT_FOUND, "No files match: %s", pattern);
        }
    }
    if (count > 1) {
        qsort(entries, count, sizeof(*entries), compare_names);
    }
    if (write) {
        status = zfs_host_mkdirs(host, error);
    }
    for (size_t i = 0; status == ZFS_OK && i < count; ++i) {
        if (!safe_name(entries[i].name) || (i != 0 && same_name(entries[i - 1].name, entries[i].name))) {
            status = zfs_error_set(error,
                                   ZFS_INVALID,
                                   "Filesystem names cannot be exported safely on this host: %s",
                                   entries[i].name);
            break;
        }
        size_t source_length = strlen(path);
        size_t host_length = strlen(host);
        bool root = strcmp(path, "/") == 0;
        int n =
            snprintf(path + source_length, ZFS_FS_PATH_MAX - source_length, "%s%s", root ? "" : "/", entries[i].name);
        int m = snprintf(host + host_length, ZFS_PATH_MAX - host_length, "/%s", entries[i].name);
        if (n < 0 || (size_t) n >= ZFS_FS_PATH_MAX - source_length || m < 0 ||
            (size_t) m >= ZFS_PATH_MAX - host_length) {
            status = zfs_error_set(error, ZFS_INVALID, "Export path is too long");
        } else {
            status = walk(disk, part, path, host, entries[i].directory, write, overwrite, exists, NULL, error);
        }
        path[source_length] = 0;
        host[host_length] = 0;
    }
    zfs_entries_free(entries);
    return status;
}

static zfs_status export_tree(zfs_disk* disk, int part, const char* path, const char* host, bool write, bool overwrite,
                              zfs_error* error)
{
    if (path == NULL || *path == 0 || strlen(path) >= ZFS_FS_PATH_MAX) {
        return zfs_error_set(error, ZFS_INVALID, "Invalid export source path");
    }
    zfs_status status = zfs_core_export_destination(disk, host, error);
    if (status != ZFS_OK) {
        return status;
    }
    /* Reuse one pair of path buffers at every depth to keep traversal stack use bounded. */
    char source[ZFS_FS_PATH_MAX];
    char destination[ZFS_PATH_MAX];
    char pattern[ZFS_FS_PATH_MAX];
    strcpy(source, path);
    strcpy(destination, host);
    bool matching = strpbrk(path, "*?") != NULL;
    if (matching) {
        char* slash = strrchr(source, '/');
        if (slash == NULL || strcspn(source, "*?") < (size_t) (slash + 1 - source)) {
            return zfs_error_set(error, ZFS_INVALID, "Wildcards are supported in filenames, not directory names");
        }
        strcpy(pattern, slash + 1);
        if (slash == source) {
            slash[1] = 0;
        } else {
            *slash = 0;
        }
    }
    bool directory;
    bool exists = false;
    status = zfs_core_path_directory(disk, part, source, &directory, error);
    if (status != ZFS_OK) {
        return status;
    }
    if (matching && !directory) {
        return zfs_error_set(error, ZFS_INVALID, "Wildcard export source must be a directory");
    }
    size_t length = strlen(destination);
    bool host_directory = destination[length - 1] == '/';
#ifdef _WIN32
    host_directory = host_directory || destination[length - 1] == '\\';
#endif
    while (length > 1 && (destination[length - 1] == '/'
#ifdef _WIN32
                          || destination[length - 1] == '\\'
#endif
                          )) {
#ifdef _WIN32
        if (length == 3 && destination[1] == ':') {
            break;
        }
#endif
        destination[--length] = 0;
    }
    zfs_host_type kind;
    status = zfs_host_kind(destination, &kind, error);
    if (status != ZFS_OK) {
        return status;
    }
    if (!directory && (host_directory || kind == ZFS_HOST_DIRECTORY)) {
        if (kind != ZFS_HOST_DIRECTORY && kind != ZFS_HOST_MISSING) {
            return zfs_error_set(error, ZFS_CONFLICT, "Export destination is not a directory: %s", host);
        }
        const char* name = strrchr(source, '/');
        if (name == NULL || !safe_name(name + 1)) {
            return zfs_error_set(error, ZFS_INVALID, "Unsafe export filename");
        }
        if (write) {
            status = zfs_host_mkdirs(destination, error);
        }
        int n = snprintf(destination + length, sizeof(destination) - length, "/%s", name + 1);
        if (n < 0 || (size_t) n >= sizeof(destination) - length) {
            return zfs_error_set(error, ZFS_INVALID, "Export destination path is too long");
        }
    }
    if (status == ZFS_OK) {
        status = walk(disk,
                      part,
                      source,
                      destination,
                      directory,
                      write,
                      overwrite,
                      &exists,
                      matching ? pattern : NULL,
                      error);
    }
    if (status == ZFS_OK && exists && !overwrite) {
        status = zfs_error_set(error, ZFS_EXISTS, "Export would replace existing host files");
    }
    return status;
}

zfs_status zfs_export_validate(zfs_disk* disk, int part, const char* path, const char* host, bool overwrite,
                               zfs_error* error)
{
    return export_tree(disk, part, path, host, false, overwrite, error);
}

zfs_status zfs_export(zfs_disk* disk, int part, const char* path, const char* host, bool overwrite, zfs_error* error)
{
    zfs_status status = zfs_export_validate(disk, part, path, host, overwrite, error);
    return status != ZFS_OK ? status : export_tree(disk, part, path, host, true, overwrite, error);
}
