/* SPDX-License-Identifier: Apache-2.0 */
#include "disk_private.h"
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
#include <strings.h>
#endif

static bool safe_name(const char *name) {
    if (!*name || strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, ".."))
        return false;
#ifdef _WIN32
    if (strpbrk(name, "\\<>:\"|?*") || name[strlen(name) - 1] == '.' || name[strlen(name) - 1] == ' ')
        return false;
    char base[ZFS_NAME_MAX + 1];
    snprintf(base, sizeof(base), "%s", name);
    char *dot = strchr(base, '.');
    if (dot)
        *dot = 0;
    if (!_stricmp(base, "CON") || !_stricmp(base, "PRN") || !_stricmp(base, "AUX") || !_stricmp(base, "NUL"))
        return false;
    if (strlen(base) == 4 && base[3] >= '1' && base[3] <= '9' &&
        (!_strnicmp(base, "COM", 3) || !_strnicmp(base, "LPT", 3)))
        return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
        if (*p < 32)
            return false;
#endif
    return true;
}
static bool same_name(const char *a, const char *b) {
#ifdef _WIN32
    return !_stricmp(a, b);
#elif defined(__APPLE__)
    return !strcasecmp(a, b);
#else
    return !strcmp(a, b);
#endif
}
static int compare_names(const void *a, const void *b) {
#ifdef _WIN32
    return _stricmp(((const zfs_entry *)a)->name, ((const zfs_entry *)b)->name);
#elif defined(__APPLE__)
    return strcasecmp(((const zfs_entry *)a)->name, ((const zfs_entry *)b)->name);
#else
    return strcmp(((const zfs_entry *)a)->name, ((const zfs_entry *)b)->name);
#endif
}
static zfs_status walk(zfs_disk *d, int part, char *path, char *host, bool directory, bool write,
                       bool overwrite, bool *exists, zfs_error *e) {
    zfs_status s = zfs_core_export_destination(d, host, e);
    if (s)
        return s;
    zfs_host_type kind;
    s = zfs_host_kind(host, &kind, e);
    if (s)
        return s;
    if (kind == ZFS_HOST_OTHER || (directory && kind == ZFS_HOST_FILE) ||
        (!directory && kind == ZFS_HOST_DIRECTORY))
        return zfs_error_set(e, ZFS_CONFLICT, "Export destination has an incompatible type or is a link: %s",
                             host);
    if (!directory) {
        if (!write) {
            if (kind == ZFS_HOST_FILE)
                *exists = true;
            return zfs_core_check_file(d, part, path, e);
        }
        return zfs_core_export_file(d, part, path, host, overwrite, e);
    }
    zfs_entry *entries = NULL;
    size_t count = 0;
    s = zfs_list(d, part, path, &entries, &count, e);
    if (s)
        return s;
    if (count > 1)
        qsort(entries, count, sizeof(*entries), compare_names);
    if (write)
        s = zfs_host_mkdirs(host, e);
    for (size_t i = 0; !s && i < count; ++i) {
        if (!safe_name(entries[i].name) || (i && same_name(entries[i - 1].name, entries[i].name))) {
            s = zfs_error_set(e, ZFS_INVALID, "Filesystem names cannot be exported safely on this host: %s",
                              entries[i].name);
            break;
        }
        size_t source_length = strlen(path), host_length = strlen(host);
        bool root = !strcmp(path, "/");
        int n = snprintf(path + source_length, 512 - source_length, "%s%s", root ? "" : "/", entries[i].name);
        int m = snprintf(host + host_length, ZFS_PATH_MAX - host_length, "/%s", entries[i].name);
        if (n < 0 || (size_t)n >= 512 - source_length || m < 0 || (size_t)m >= ZFS_PATH_MAX - host_length)
            s = zfs_error_set(e, ZFS_INVALID, "Export path is too long");
        else
            s = walk(d, part, path, host, entries[i].directory, write, overwrite, exists, e);
        path[source_length] = 0;
        host[host_length] = 0;
    }
    zfs_entries_free(entries);
    return s;
}
static zfs_status export_tree(zfs_disk *d, int part, const char *path, const char *host, bool write,
                              bool overwrite, zfs_error *e) {
    bool directory, exists = false;
    zfs_status s = zfs_core_path_directory(d, part, path, &directory, e);
    if (!s)
        s = zfs_core_export_destination(d, host, e);
    if (s)
        return s;
    /* Reuse one pair of path buffers at every depth to keep traversal stack use bounded. */
    char source[512], destination[ZFS_PATH_MAX];
    strcpy(source, path);
    strcpy(destination, host);
    s = walk(d, part, source, destination, directory, write, overwrite, &exists, e);
    if (!s && exists && !overwrite)
        s = zfs_error_set(e, ZFS_EXISTS, "Export would replace existing host files");
    return s;
}
zfs_status zfs_export_validate(zfs_disk *d, int part, const char *path, const char *host, bool overwrite,
                               zfs_error *e) {
    return export_tree(d, part, path, host, false, overwrite, e);
}
zfs_status zfs_export(zfs_disk *d, int part, const char *path, const char *host, bool overwrite,
                      zfs_error *e) {
    zfs_status s = zfs_export_validate(d, part, path, host, overwrite, e);
    return s ? s : export_tree(d, part, path, host, true, overwrite, e);
}
