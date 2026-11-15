/**
 * SPDX-License-Identifier: Apache-2.0
 */
#include "host_private.h"
#include <stdlib.h>
#include <string.h>

zfs_status zfs_host_mkdirs(const char* path, zfs_error* error)
{
    if (path == NULL || *path == 0 || strlen(path) >= ZFS_PATH_MAX) {
        return zfs_error_set(error, ZFS_INVALID, "Invalid host directory path");
    }
    char current[ZFS_PATH_MAX];
    strcpy(current, path);
    size_t first = zfs_host_mkdir_start(current);
    for (size_t i = first;; ++i) {
        if (current[i] == 0 || zfs_host_separator(current[i])) {
            char saved = current[i];
            if (i != 0 && !zfs_host_separator(current[i - 1]) && i != first) {
                current[i] = 0;
                zfs_status status = zfs_host_make_directory(current, error);
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

zfs_status zfs_host_append_path(char*** paths, size_t* count, const char* path, zfs_error* error)
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
        return zfs_host_append_path(paths, count, pattern, error);
    }
    const char* name = pattern;
    for (const char* component = pattern; *component != 0; ++component) {
        if (zfs_host_separator(*component)) {
            name = component + 1;
        }
    }
    size_t prefix = (size_t) (name - pattern);
    if (strcspn(pattern, "*?") < prefix) {
        return zfs_error_set(error, ZFS_INVALID, "Wildcards are supported in filenames, not directory names");
    }
    zfs_host_expansion expansion;
    expansion.pattern = pattern;
    expansion.name = name;
    expansion.prefix = prefix;
    expansion.paths = paths;
    expansion.count = count;
    memcpy(expansion.path, pattern, prefix);
    expansion.path[prefix] = 0;
    size_t before = *count;
    zfs_status status = zfs_host_expand_files(&expansion, error);
    return status != ZFS_OK   ? status
           : *count == before ? zfs_error_set(error, ZFS_NOT_FOUND, "No files match: %s", pattern)
                              : ZFS_OK;
}
