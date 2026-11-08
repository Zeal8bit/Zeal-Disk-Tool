/* SPDX-License-Identifier: Apache-2.0 */
#include "disk_private.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *path;
    bool directory, replace;
} action;
struct zfs_merge_plan {
    zfs_disk *target, *view;
    zfs_merge_report *report;
    action *actions;
    size_t count, capacity;
    uint64_t revision;
    zfs_partition partition;
    bool ready, restore;
};
static char *copy_string(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}
static zfs_status clean(zfs_disk *d, zfs_error *e) {
    if (!d || d->info.failed || d->info.new_image)
        return zfs_error_set(e, ZFS_INVALID, "An existing healthy source/target is required");
    if (d->info.pending)
        return zfs_error_set(e, ZFS_BUSY, "Apply or discard pending changes before merge or restore");
    return ZFS_OK;
}
static zfs_status partition(zfs_disk *d, int index, bool raw_restore, zfs_partition *out, zfs_error *e) {
    if (index < 0 || index >= 4)
        return zfs_error_set(e, ZFS_INVALID, "Invalid partition selection");
    zfs_partition p = d->info.partitions[index];
    if (raw_restore && !d->info.has_mbr && index == 0)
        p = (zfs_partition){true, 0x5a, 0, (uint32_t)(d->info.size / 512)};
    if (!p.active || p.type != 0x5a)
        return zfs_error_set(e, ZFS_INVALID, "Select an existing ZealFS partition");
    *out = p;
    return ZFS_OK;
}
static zfs_status child_path(const char *parent, const char *name, char out[512], zfs_error *e) {
    size_t n = strlen(name);
    if (!n || strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, ".."))
        return zfs_error_set(e, ZFS_CORRUPT, "Invalid filesystem entry name");
    int length = snprintf(out, 512, "%s%s%s", parent, !strcmp(parent, "/") ? "" : "/", name);
    return length < 0 || length >= 512 ? zfs_error_set(e, ZFS_INVALID, "Filesystem path exceeds 511 bytes")
                                       : ZFS_OK;
}
static int compare_entries(const void *a, const void *b) {
    return strcmp(((const zfs_entry *)a)->name, ((const zfs_entry *)b)->name);
}
static int compare_actions(const void *a, const void *b) {
    return strcmp(((const action *)a)->path, ((const action *)b)->path);
}
static zfs_status list(zfs_disk *disk, int part, const char *path, zfs_entry **entries, size_t *count,
                       zfs_error *e) {
    zfs_status s = zfs_list(disk, part, path, entries, count, e);
    if (s)
        return s;
    qsort(*entries, *count, sizeof(**entries), compare_entries);
    for (size_t i = 1; i < *count; ++i)
        if (!strcmp((*entries)[i - 1].name, (*entries)[i].name)) {
            zfs_entries_free(*entries);
            *entries = NULL;
            *count = 0;
            return zfs_error_set(e, ZFS_CORRUPT, "Duplicate filesystem entry names");
        }
    return ZFS_OK;
}
static zfs_status add_action(zfs_merge_plan *p, const char *path, bool directory, bool replace,
                             zfs_error *e) {
    if (p->count == p->capacity) {
        size_t capacity = p->capacity ? p->capacity * 2 : 32;
        if (capacity > SIZE_MAX / sizeof(action))
            return zfs_error_set(e, ZFS_NO_MEMORY, "Merge plan is too large");
        action *a = realloc(p->actions, capacity * sizeof(*a));
        if (!a)
            return zfs_error_set(e, ZFS_NO_MEMORY, "Cannot allocate merge actions");
        p->actions = a;
        p->capacity = capacity;
    }
    char *name = copy_string(path);
    if (!name)
        return zfs_error_set(e, ZFS_NO_MEMORY, "Cannot allocate merge path");
    p->actions[p->count++] = (action){name, directory, replace};
    return ZFS_OK;
}
static zfs_status conflict(zfs_merge_report *r, const char *path, zfs_error *e) {
    if (r->conflict_count == SIZE_MAX / sizeof(char *))
        return zfs_error_set(e, ZFS_NO_MEMORY, "Too many conflicts");
    char **paths = realloc(r->conflicts, (r->conflict_count + 1) * sizeof(*paths));
    if (!paths)
        return zfs_error_set(e, ZFS_NO_MEMORY, "Cannot allocate conflict report");
    r->conflicts = paths;
    char *name = copy_string(path);
    if (!name)
        return zfs_error_set(e, ZFS_NO_MEMORY, "Cannot allocate conflict path");
    r->conflicts[r->conflict_count++] = name;
    return ZFS_OK;
}
/* Sorted merge traversal visits target-only subtrees too, validating personal
 * files before any backup or mutation. Only the current directory lists remain
 * in memory; file contents always stream through bounded buffers. */
static zfs_status walk(zfs_merge_plan *p, zfs_disk *source, int sp, int tp, const char *path,
                       bool have_source, bool have_target, zfs_error *e) {
    zfs_entry *from = NULL, *to = NULL;
    size_t sn = 0, tn = 0;
    zfs_status s = have_source ? list(source, sp, path, &from, &sn, e) : ZFS_OK;
    if (!s && have_target)
        s = list(p->target, tp, path, &to, &tn, e);
    size_t i = 0, j = 0;
    while (!s && (i < sn || j < tn)) {
        int order = i == sn ? 1 : j == tn ? -1 : strcmp(from[i].name, to[j].name);
        zfs_entry *a = order <= 0 ? &from[i] : NULL, *b = order >= 0 ? &to[j] : NULL;
        char child[512];
        s = child_path(path, a ? a->name : b->name, child, e);
        if (s)
            break;
        zfs_merge_report *r = p->report;
        if (a && b && a->directory != b->directory)
            s = conflict(r, child, e);
        else if (a && b && a->directory) {
            ++r->directories_merged;
            s = walk(p, source, sp, tp, child, true, true, e);
        } else if (a && b) {
            bool equal = false;
            s = zfs_core_files_equal(source, sp, p->target, tp, child, &equal, e);
            if (!s && equal)
                ++r->files_unchanged;
            else if (!s) {
                ++r->files_replaced;
                r->bytes_copied += a->size;
                s = add_action(p, child, false, true, e);
            }
        } else if (a) {
            if (a->directory) {
                ++r->directories_created;
                s = add_action(p, child, true, false, e);
                if (!s)
                    s = walk(p, source, sp, tp, child, true, false, e);
            } else {
                ++r->files_added;
                r->bytes_copied += a->size;
                s = add_action(p, child, false, false, e);
            }
        } else {
            if (b->directory) {
                ++r->directories_preserved;
                s = walk(p, source, sp, tp, child, false, true, e);
            } else {
                ++r->files_preserved;
                s = zfs_core_check_file(p->target, tp, child, e);
            }
        }
        if (order <= 0)
            ++i;
        if (order >= 0)
            ++j;
    }
    zfs_entries_free(from);
    zfs_entries_free(to);
    return s;
}
static zfs_status validate_tree(zfs_disk *disk, int part, const char *path, zfs_error *e) {
    zfs_entry *entries = NULL;
    size_t count = 0;
    zfs_status s = list(disk, part, path, &entries, &count, e);
    for (size_t i = 0; !s && i < count; ++i) {
        char child[512];
        s = child_path(path, entries[i].name, child, e);
        if (!s)
            s = entries[i].directory ? validate_tree(disk, part, child, e)
                                     : zfs_core_check_file(disk, part, child, e);
    }
    zfs_entries_free(entries);
    return s;
}
static zfs_status create_plan(zfs_disk *source, int sp, zfs_disk *target, int tp, bool restore,
                              zfs_merge_plan **out, zfs_merge_report *r, zfs_error *e) {
    *out = NULL;
    memset(r, 0, sizeof(*r));
    r->source_partition = sp;
    r->target_partition = tp;
    r->restore = restore;
    if (source)
        snprintf(r->source, sizeof(r->source), "%s", source->info.path);
    if (target)
        snprintf(r->target, sizeof(r->target), "%s", target->info.path);
    zfs_status s = clean(source, e);
    if (!s)
        s = clean(target, e);
    if (s)
        return s;
    if (zfs_core_same_disk(source, target))
        return zfs_error_set(e, ZFS_INVALID, "Source and target must be distinct disks, including aliases");
    zfs_partition from, to;
    s = partition(source, sp, false, &from, e);
    if (!s)
        s = partition(target, tp, restore, &to, e);
    if (s)
        return s;
    s = zfs_core_guard(source, false, e);
    if (!s)
        s = zfs_core_guard(target, true, e);
    if (s)
        return s;
    zfs_merge_plan *p = calloc(1, sizeof(*p));
    if (!p)
        return zfs_error_set(e, ZFS_NO_MEMORY, "Cannot allocate merge plan");
    p->target = target;
    p->partition = to;
    r->backup_bytes = (uint64_t)to.sectors * 512;
    p->revision = target->revision;
    p->report = r;
    p->restore = restore;
    s = zfs_core_clone(target, &p->view, e);
    if (s) {
        free(p);
        return s;
    }
    *out = p;
    return ZFS_OK;
}
zfs_status zfs_merge_prepare(zfs_disk *source, int sp, zfs_disk *target, int tp, zfs_merge_plan **out,
                             zfs_merge_report *r, zfs_error *e) {
    if (!out || !r)
        return zfs_error_set(e, ZFS_INVALID, "Merge plan and report outputs are required");
    zfs_status s = create_plan(source, sp, target, tp, false, out, r, e);
    if (s)
        return s;
    zfs_merge_plan *p = *out;
    s = walk(p, source, sp, tp, "/", true, true, e);
    if (!s && r->conflict_count)
        s = zfs_error_set(e, ZFS_CONFLICT, "Merge aborted: %zu file/directory type conflict(s), first: %s",
                          r->conflict_count, r->conflicts[0]);
    /* Free every replaced file first, so the final merged tree can fit even
     * when there is insufficient room to hold both old and new versions. */
    for (size_t i = 0; !s && i < p->count; ++i)
        if (p->actions[i].replace)
            s = zfs_remove(p->view, tp, p->actions[i].path, false, e);
    if (!s && p->count)
        qsort(p->actions, p->count, sizeof(*p->actions), compare_actions);
    for (size_t i = 0; !s && i < p->count; ++i) {
        action *a = &p->actions[i];
        if (a->directory) {
            s = zfs_mkdir(p->view, tp, a->path, e);
            if (!s)
                s = zfs_core_copy_metadata(source, sp, p->view, tp, a->path, e);
        } else
            s = zfs_core_copy_file(source, sp, p->view, tp, a->path, e);
    }
    if (!s && (!zfs_core_unchanged(source) || !zfs_core_unchanged(target)))
        s = zfs_error_set(e, ZFS_BUSY, "Source or target changed during preparation; reopen and retry");
    if (s) {
        zfs_merge_plan_free(p);
        *out = NULL;
        return s;
    }
    r->noop = p->count == 0;
    p->ready = true;
    return ZFS_OK;
}
static zfs_status destination(zfs_disk *d, const char *path, char resolved[ZFS_PATH_MAX], zfs_error *e) {
    if (!path || !*path || strlen(path) >= ZFS_PATH_MAX - 64)
        return zfs_error_set(e, ZFS_INVALID, "A valid --backup PATH is required");
    if (zfs_host_exists(path))
        return zfs_error_set(e, ZFS_EXISTS, "Backup destination already exists: %s", path);
    if (!zfs_host_resolve(path, resolved, ZFS_PATH_MAX))
        return zfs_error_set(e, ZFS_IO, "Backup parent directory does not exist or path cannot be resolved");
#ifdef _WIN32
    bool same = _stricmp(resolved, d->info.path) == 0;
#else
    bool same = strcmp(resolved, d->info.path) == 0;
#endif
    return same ? zfs_error_set(e, ZFS_INVALID, "Backup cannot replace its source") : ZFS_OK;
}
static zfs_status backup_space(const char *path, uint64_t size, zfs_error *e) {
    uint64_t available;
    if (!zfs_host_free_space(path, &available))
        return zfs_error_set(e, ZFS_IO, "Cannot check backup destination free space");
    if (available < size)
        return zfs_error_set(e, ZFS_NO_SPACE, "Backup requires %llu bytes; only %llu bytes are available",
                             (unsigned long long)size, (unsigned long long)available);
    return ZFS_OK;
}
zfs_status zfs_backup_partition(zfs_disk *d, int part, const char *path, zfs_error *e) {
    zfs_status s = clean(d, e);
    if (s)
        return s;
    zfs_partition p;
    s = partition(d, part, false, &p, e);
    if (s)
        return s;
    char resolved[ZFS_PATH_MAX];
    s = destination(d, path, resolved, e);
    if (s)
        return s;
    s = backup_space(resolved, (uint64_t)p.sectors * 512, e);
    if (s)
        return s;
    s = zfs_core_guard(d, true, e);
    if (s)
        return s;
    char temporary[ZFS_PATH_MAX];
    FILE *file = zfs_temp_sibling(resolved, temporary, sizeof(temporary));
    if (!file)
        return zfs_error_set(e, ZFS_IO, "Cannot create backup temporary file");
    unsigned char original[65536], saved[65536];
    uint64_t size = (uint64_t)p.sectors * 512, offset = (uint64_t)p.start_lba * 512;
    for (uint64_t at = 0; !s && at < size; at += sizeof(original)) {
        size_t n = size - at < sizeof(original) ? (size_t)(size - at) : sizeof(original);
        s = zfs_core_read(d, offset + at, original, n, e);
        if (!s && fwrite(original, 1, n, file) != n)
            s = zfs_error_set(e, ZFS_IO, "Backup write failed; target unchanged");
        if (d->callbacks.progress)
            d->callbacks.progress(d->callbacks.user, at + n, size);
    }
    if (!s && zfs_host_sync(file))
        s = zfs_error_set(e, ZFS_IO, "Backup flush failed; target unchanged");
    for (uint64_t at = 0; !s && at < size; at += sizeof(original)) {
        size_t n = size - at < sizeof(original) ? (size_t)(size - at) : sizeof(original);
        s = zfs_core_read(d, offset + at, original, n, e);
        if (!s && (zfs_seek(file, at) || fread(saved, 1, n, file) != n || memcmp(saved, original, n)))
            s = zfs_error_set(e, ZFS_IO, "Backup verification failed; target unchanged");
    }
    if (!s && !zfs_core_unchanged(d))
        s = zfs_error_set(e, ZFS_BUSY, "Target changed while backing up; reopen and retry");
    if (fclose(file) && !s)
        s = zfs_error_set(e, ZFS_IO, "Backup close failed");
    if (!s && zfs_host_publish(temporary, resolved, false))
        s = zfs_error_set(e, ZFS_IO, "Backup publication failed; destination may already exist");
    if (s)
        zfs_host_remove(temporary);
    return s;
}
static zfs_status plan_current(zfs_merge_plan *p, zfs_error *e) {
    if (!p || !p->ready || !p->view)
        return zfs_error_set(e, ZFS_INVALID, "No prepared operation");
    if (p->target->info.pending || p->target->revision != p->revision || !zfs_core_unchanged(p->target))
        return zfs_error_set(e, ZFS_BUSY, "Target changed since preparation; prepare again");
    return ZFS_OK;
}
zfs_status zfs_merge_check_backup(zfs_merge_plan *p, const char *backup, zfs_error *e) {
    zfs_status s = plan_current(p, e);
    if (s)
        return s;
    if (p->restore)
        return zfs_error_set(e, ZFS_INVALID, "Restore does not create a backup");
    if (p->report->noop)
        return ZFS_OK;
    char resolved[ZFS_PATH_MAX];
    s = destination(p->target, backup, resolved, e);
    return s ? s : backup_space(resolved, (uint64_t)p->partition.sectors * 512, e);
}
zfs_status zfs_merge_stage(zfs_merge_plan *p, const char *backup, zfs_error *e) {
    zfs_status s = plan_current(p, e);
    if (s)
        return s;
    if (p->restore)
        return zfs_error_set(e, ZFS_INVALID, "Use restore staging for this plan");
    if (p->report->noop)
        return ZFS_OK;
    char resolved[ZFS_PATH_MAX] = {0};
    if (backup) {
        s = destination(p->target, backup, resolved, e);
        if (s)
            return s;
        s = zfs_backup_partition(p->target, p->report->target_partition, resolved, e);
        if (s)
            return s;
        snprintf(p->report->backup, sizeof(p->report->backup), "%s", resolved);
        p->report->backup_created = true;
        p->report->backup_bytes = (uint64_t)p->partition.sectors * 512;
    }
    zfs_disk *d = p->target;
    zfs_core_attach(d, p->view);
    p->ready = false;
    p->report->staged = true;
    snprintf(d->recovery_backup, sizeof(d->recovery_backup), "%s", resolved);
    d->recovery_partition = p->report->target_partition;
    d->recovery_offset = (uint64_t)p->partition.start_lba * 512;
    d->recovery_size = (uint64_t)p->partition.sectors * 512;
    d->recovery_is_merge = backup != NULL;
    return ZFS_OK;
}
zfs_status zfs_restore_prepare(const char *backup, zfs_disk *target, int tp, zfs_merge_plan **out,
                               zfs_merge_report *r, zfs_error *e) {
    if (!out || !r)
        return zfs_error_set(e, ZFS_INVALID, "Restore plan and report outputs are required");
    *out = NULL;
    memset(r, 0, sizeof(*r));
    r->restore = true;
    zfs_disk *source = NULL;
    zfs_status s = zfs_open(backup, true, &source, e);
    if (s)
        return s;
    if (source->info.has_mbr) {
        zfs_close(source);
        return zfs_error_set(e, ZFS_INVALID,
                             "Restore requires a raw partition backup, not an MBR disk image");
    }
    s = create_plan(source, 0, target, tp, true, out, r, e);
    if (!s) {
        zfs_merge_plan *p = *out;
        uint64_t size = (uint64_t)p->partition.sectors * 512;
        if (source->info.size != size)
            s = zfs_error_set(e, ZFS_INVALID, "Backup capacity must exactly match the target partition");
        if (!s)
            s = validate_tree(source, 0, "/", e);
        if (!s)
            s = zfs_core_stage_raw(p->view, (uint64_t)p->partition.start_lba * 512, source, size, e);
        if (!s && (!zfs_core_unchanged(source) || !zfs_core_unchanged(target)))
            s = zfs_error_set(e, ZFS_BUSY, "Source or target changed during restore preparation");
        if (!s) {
            r->backup_bytes = size;
            r->bytes_copied = size;
            r->noop = !p->view->info.pending;
            snprintf(r->backup, sizeof(r->backup), "%s", source->info.path);
            p->ready = true;
        }
    }
    zfs_close(source);
    if (s) {
        zfs_merge_plan_free(*out);
        *out = NULL;
    }
    return s;
}
zfs_status zfs_restore_stage(zfs_merge_plan *p, zfs_error *e) {
    zfs_status s = plan_current(p, e);
    if (s)
        return s;
    if (!p->restore)
        return zfs_error_set(e, ZFS_INVALID, "Use merge staging for this plan");
    if (p->report->noop)
        return ZFS_OK;
    zfs_core_attach(p->target, p->view);
    p->ready = false;
    p->report->staged = true;
    snprintf(p->target->recovery_backup, sizeof(p->target->recovery_backup), "%s", p->report->backup);
    p->target->recovery_partition = p->report->target_partition;
    p->target->recovery_is_merge = false;
    return ZFS_OK;
}
void zfs_merge_plan_free(zfs_merge_plan *p) {
    if (!p)
        return;
    zfs_close(p->view);
    for (size_t i = 0; i < p->count; ++i)
        free(p->actions[i].path);
    free(p->actions);
    free(p);
}
void zfs_merge_report_free(zfs_merge_report *r) {
    if (!r)
        return;
    for (size_t i = 0; i < r->conflict_count; ++i)
        free(r->conflicts[i]);
    free(r->conflicts);
    memset(r, 0, sizeof(*r));
}
