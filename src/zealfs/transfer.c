/**
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "disk_private.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* path;
    bool directory;
    bool replace;
} action;

struct zfs_merge_plan {
    zfs_disk* target;
    zfs_disk* view;
    zfs_merge_report* report;
    action* actions;
    size_t count;
    size_t capacity;
    uint64_t revision;
    zfs_partition partition;
    bool ready;
    bool restore;
};

static char* copy_string(const char* text)
{
    size_t length = strlen(text) + 1;
    char* copy = malloc(length);
    if (copy != NULL) {
        memcpy(copy, text, length);
    }
    return copy;
}

static zfs_status clean(zfs_disk* disk, zfs_error* error)
{
    if (disk == NULL || disk->info.failed || disk->info.new_image) {
        return zfs_error_set(error, ZFS_INVALID, "An existing healthy source/target is required");
    }
    if (disk->info.pending) {
        return zfs_error_set(error, ZFS_BUSY, "Apply or discard pending changes before merge or restore");
    }
    return ZFS_OK;
}

static zfs_status partition(zfs_disk* disk, int index, bool raw_restore, zfs_partition* out, zfs_error* error)
{
    if (index < 0 || index >= ZFS_MAX_PARTITIONS) {
        return zfs_error_set(error, ZFS_INVALID, "Invalid partition selection");
    }
    zfs_partition partition_info = disk->info.partitions[index];
    if (raw_restore && !disk->info.has_mbr && index == 0) {
        partition_info = (zfs_partition){
            true,
            ZEALFS_TYPE,
            0,
            (uint32_t) (disk->info.size / DISK_SECTOR_SIZE),
        };
    }
    if (!partition_info.active || partition_info.type != ZEALFS_TYPE) {
        return zfs_error_set(error, ZFS_INVALID, "Select an existing ZealFS partition");
    }
    *out = partition_info;
    return ZFS_OK;
}

static zfs_status child_path(const char* parent, const char* name, char out[ZFS_FS_PATH_MAX], zfs_error* error)
{
    size_t name_length = strlen(name);
    if (name_length == 0 || strchr(name, '/') != NULL || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return zfs_error_set(error, ZFS_CORRUPT, "Invalid filesystem entry name");
    }
    int length = snprintf(out, ZFS_FS_PATH_MAX, "%s%s%s", parent, strcmp(parent, "/") == 0 ? "" : "/", name);
    return length < 0 || length >= ZFS_FS_PATH_MAX
               ? zfs_error_set(error, ZFS_INVALID, "Filesystem path exceeds 511 bytes")
               : ZFS_OK;
}

static int compare_entries(const void* first, const void* second)
{
    return strcmp(((const zfs_entry*) first)->name, ((const zfs_entry*) second)->name);
}

static int compare_actions(const void* first, const void* second)
{
    return strcmp(((const action*) first)->path, ((const action*) second)->path);
}

static zfs_status list(zfs_disk* disk, int part, const char* path, zfs_entry** entries, size_t* count, zfs_error* error)
{
    zfs_status status = zfs_list(disk, part, path, entries, count, error);
    if (status != ZFS_OK) {
        return status;
    }
    qsort(*entries, *count, sizeof(**entries), compare_entries);
    for (size_t i = 1; i < *count; ++i) {
        if (strcmp((*entries)[i - 1].name, (*entries)[i].name) == 0) {
            zfs_entries_free(*entries);
            *entries = NULL;
            *count = 0;
            return zfs_error_set(error, ZFS_CORRUPT, "Duplicate filesystem entry names");
        }
    }
    return ZFS_OK;
}

static zfs_status add_action(zfs_merge_plan* plan, const char* path, bool directory, bool replace, zfs_error* error)
{
    if (plan->count == plan->capacity) {
        size_t capacity = plan->capacity != 0 ? plan->capacity * 2 : 32;
        if (capacity > SIZE_MAX / sizeof(action)) {
            return zfs_error_set(error, ZFS_NO_MEMORY, "Merge plan is too large");
        }
        action* file_action = realloc(plan->actions, capacity * sizeof(*file_action));
        if (file_action == NULL) {
            return zfs_error_set(error, ZFS_NO_MEMORY, "Cannot allocate merge actions");
        }
        plan->actions = file_action;
        plan->capacity = capacity;
    }
    char* name = copy_string(path);
    if (name == NULL) {
        return zfs_error_set(error, ZFS_NO_MEMORY, "Cannot allocate merge path");
    }
    plan->actions[plan->count++] = (action){name, directory, replace};
    return ZFS_OK;
}

static zfs_status conflict(zfs_merge_report* report, const char* path, zfs_error* error)
{
    if (report->conflict_count == SIZE_MAX / sizeof(char*)) {
        return zfs_error_set(error, ZFS_NO_MEMORY, "Too many conflicts");
    }
    char** paths = realloc(report->conflicts, (report->conflict_count + 1) * sizeof(*paths));
    if (paths == NULL) {
        return zfs_error_set(error, ZFS_NO_MEMORY, "Cannot allocate conflict report");
    }
    report->conflicts = paths;
    char* name = copy_string(path);
    if (name == NULL) {
        return zfs_error_set(error, ZFS_NO_MEMORY, "Cannot allocate conflict path");
    }
    report->conflicts[report->conflict_count++] = name;
    return ZFS_OK;
}

/* Sorted merge traversal visits target-only subtrees too, validating personal
 * files before any backup or mutation. Only the current directory lists remain
 * in memory; file contents always stream through bounded buffers. */
static zfs_status walk(zfs_merge_plan* plan, zfs_disk* source, int source_partition, int target_partition,
                       const char* path, bool have_source, bool have_target, zfs_error* error)
{
    zfs_entry* source_entries = NULL;
    zfs_entry* target_entries = NULL;
    size_t source_count = 0;
    size_t target_count = 0;
    zfs_status status =
        have_source ? list(source, source_partition, path, &source_entries, &source_count, error) : ZFS_OK;
    if (status == ZFS_OK && have_target) {
        status = list(plan->target, target_partition, path, &target_entries, &target_count, error);
    }
    size_t source_index = 0;
    size_t target_index = 0;
    while (status == ZFS_OK && (source_index < source_count || target_index < target_count)) {
        int order = source_index == source_count ? 1
                    : target_index == target_count
                        ? -1
                        : strcmp(source_entries[source_index].name, target_entries[target_index].name);
        zfs_entry* source_entry = order <= 0 ? &source_entries[source_index] : NULL;
        zfs_entry* target_entry = order >= 0 ? &target_entries[target_index] : NULL;
        char child[ZFS_FS_PATH_MAX];
        status = child_path(path, source_entry != NULL ? source_entry->name : target_entry->name, child, error);
        if (status != ZFS_OK) {
            break;
        }
        zfs_merge_report* report = plan->report;
        if (source_entry != NULL && target_entry != NULL && source_entry->directory != target_entry->directory) {
            status = conflict(report, child, error);
        } else if (source_entry != NULL && target_entry != NULL && source_entry->directory) {
            ++report->directories_merged;
            status = walk(plan, source, source_partition, target_partition, child, true, true, error);
        } else if (source_entry != NULL && target_entry != NULL) {
            bool equal = false;
            status =
                zfs_core_files_equal(source, source_partition, plan->target, target_partition, child, &equal, error);
            if (status == ZFS_OK && equal) {
                ++report->files_unchanged;
            } else if (status == ZFS_OK) {
                ++report->files_replaced;
                report->bytes_copied += source_entry->size;
                status = add_action(plan, child, false, true, error);
            }
        } else if (source_entry != NULL) {
            if (source_entry->directory) {
                ++report->directories_created;
                status = add_action(plan, child, true, false, error);
                if (status == ZFS_OK) {
                    status = walk(plan, source, source_partition, target_partition, child, true, false, error);
                }
            } else {
                ++report->files_added;
                report->bytes_copied += source_entry->size;
                status = add_action(plan, child, false, false, error);
            }
        } else {
            if (target_entry->directory) {
                ++report->directories_preserved;
                status = walk(plan, source, source_partition, target_partition, child, false, true, error);
            } else {
                ++report->files_preserved;
                status = zfs_core_check_file(plan->target, target_partition, child, error);
            }
        }
        if (order <= 0) {
            ++source_index;
        }
        if (order >= 0) {
            ++target_index;
        }
    }
    zfs_entries_free(source_entries);
    zfs_entries_free(target_entries);
    return status;
}

static zfs_status validate_tree(zfs_disk* disk, int part, const char* path, zfs_error* error)
{
    zfs_entry* entries = NULL;
    size_t count = 0;
    zfs_status status = list(disk, part, path, &entries, &count, error);
    for (size_t i = 0; status == ZFS_OK && i < count; ++i) {
        char child[ZFS_FS_PATH_MAX];
        status = child_path(path, entries[i].name, child, error);
        if (status == ZFS_OK) {
            status = entries[i].directory ? validate_tree(disk, part, child, error)
                                          : zfs_core_check_file(disk, part, child, error);
        }
    }
    zfs_entries_free(entries);
    return status;
}

static zfs_status create_plan(zfs_disk* source, int source_partition, zfs_disk* target, int target_partition,
                              bool restore, zfs_merge_plan** out, zfs_merge_report* report, zfs_error* error)
{
    *out = NULL;
    memset(report, 0, sizeof(*report));
    report->source_partition = source_partition;
    report->target_partition = target_partition;
    report->restore = restore;
    if (source != NULL) {
        snprintf(report->source, sizeof(report->source), "%s", source->info.path);
    }
    if (target != NULL) {
        snprintf(report->target, sizeof(report->target), "%s", target->info.path);
    }
    zfs_status status = clean(source, error);
    if (status == ZFS_OK) {
        status = clean(target, error);
    }
    if (status != ZFS_OK) {
        return status;
    }
    if (zfs_core_same_disk(source, target)) {
        return zfs_error_set(error, ZFS_INVALID, "Source and target must be distinct disks, including aliases");
    }
    zfs_partition from;
    zfs_partition to;
    status = partition(source, source_partition, false, &from, error);
    if (status == ZFS_OK) {
        status = partition(target, target_partition, restore, &to, error);
    }
    if (status != ZFS_OK) {
        return status;
    }
    status = zfs_core_guard(source, false, error);
    if (status == ZFS_OK) {
        status = zfs_core_guard(target, true, error);
    }
    if (status != ZFS_OK) {
        return status;
    }
    zfs_merge_plan* plan = calloc(1, sizeof(*plan));
    if (plan == NULL) {
        return zfs_error_set(error, ZFS_NO_MEMORY, "Cannot allocate merge plan");
    }
    plan->target = target;
    plan->partition = to;
    report->backup_bytes = (uint64_t) to.sectors * DISK_SECTOR_SIZE;
    plan->revision = target->revision;
    plan->report = report;
    plan->restore = restore;
    status = zfs_core_clone(target, &plan->view, error);
    if (status != ZFS_OK) {
        free(plan);
        return status;
    }
    *out = plan;
    return ZFS_OK;
}

zfs_status zfs_merge_prepare(zfs_disk* source, int source_partition, zfs_disk* target, int target_partition,
                             zfs_merge_plan** out, zfs_merge_report* report, zfs_error* error)
{
    if (out == NULL || report == NULL) {
        return zfs_error_set(error, ZFS_INVALID, "Merge plan and report outputs are required");
    }
    zfs_status status = create_plan(source, source_partition, target, target_partition, false, out, report, error);
    if (status != ZFS_OK) {
        return status;
    }
    zfs_merge_plan* plan = *out;
    status = walk(plan, source, source_partition, target_partition, "/", true, true, error);
    if (status == ZFS_OK && report->conflict_count != 0) {
        status = zfs_error_set(error,
                               ZFS_CONFLICT,
                               "Merge aborted: %zu file/directory type conflict(s), first: %s",
                               report->conflict_count,
                               report->conflicts[0]);
    }
    /* Free every replaced file first, so the final merged tree can fit even
     * when there is insufficient room to hold both old and new versions. */
    for (size_t i = 0; status == ZFS_OK && i < plan->count; ++i) {
        if (plan->actions[i].replace) {
            status = zfs_remove(plan->view, target_partition, plan->actions[i].path, false, error);
        }
    }
    if (status == ZFS_OK && plan->count != 0) {
        qsort(plan->actions, plan->count, sizeof(*plan->actions), compare_actions);
    }
    for (size_t i = 0; status == ZFS_OK && i < plan->count; ++i) {
        action* file_action = &plan->actions[i];
        if (file_action->directory) {
            status = zfs_mkdir(plan->view, target_partition, file_action->path, error);
            if (status == ZFS_OK) {
                status = zfs_core_copy_metadata(source,
                                                source_partition,
                                                plan->view,
                                                target_partition,
                                                file_action->path,
                                                error);
            }
        } else {
            status =
                zfs_core_copy_file(source, source_partition, plan->view, target_partition, file_action->path, error);
        }
    }
    if (status == ZFS_OK && (!zfs_core_unchanged(source) || !zfs_core_unchanged(target))) {
        status = zfs_error_set(error, ZFS_BUSY, "Source or target changed during preparation; reopen and retry");
    }
    if (status != ZFS_OK) {
        zfs_merge_plan_free(plan);
        *out = NULL;
        return status;
    }
    report->noop = plan->count == 0;
    plan->ready = true;
    return ZFS_OK;
}

static zfs_status destination(zfs_disk* disk, const char* path, char resolved[ZFS_PATH_MAX], zfs_error* error)
{
    if (path == NULL || *path == 0 || strlen(path) >= ZFS_PATH_MAX - 64) {
        return zfs_error_set(error, ZFS_INVALID, "A valid --backup PATH is required");
    }
    if (zfs_host_exists(path)) {
        return zfs_error_set(error, ZFS_EXISTS, "Backup destination already exists: %s", path);
    }
    if (!zfs_host_resolve(path, resolved, ZFS_PATH_MAX)) {
        return zfs_error_set(error, ZFS_IO, "Backup parent directory does not exist or path cannot be resolved");
    }
#ifdef _WIN32
    bool same = _stricmp(resolved, disk->info.path) == 0;
#else
    bool same = strcmp(resolved, disk->info.path) == 0;
#endif
    return same ? zfs_error_set(error, ZFS_INVALID, "Backup cannot replace its source") : ZFS_OK;
}

static zfs_status backup_space(const char* path, uint64_t size, zfs_error* error)
{
    uint64_t available;
    if (!zfs_host_free_space(path, &available)) {
        return zfs_error_set(error, ZFS_IO, "Cannot check backup destination free space");
    }
    if (available < size) {
        return zfs_error_set(error,
                             ZFS_NO_SPACE,
                             "Backup requires %llu bytes; only %llu bytes are available",
                             (unsigned long long) size,
                             (unsigned long long) available);
    }
    return ZFS_OK;
}

zfs_status zfs_backup_partition(zfs_disk* disk, int part, const char* path, zfs_error* error)
{
    zfs_status status = clean(disk, error);
    if (status != ZFS_OK) {
        return status;
    }
    zfs_partition partition_info;
    status = partition(disk, part, false, &partition_info, error);
    if (status != ZFS_OK) {
        return status;
    }
    char resolved[ZFS_PATH_MAX];
    status = destination(disk, path, resolved, error);
    if (status != ZFS_OK) {
        return status;
    }
    status = backup_space(resolved, (uint64_t) partition_info.sectors * DISK_SECTOR_SIZE, error);
    if (status != ZFS_OK) {
        return status;
    }
    status = zfs_core_guard(disk, true, error);
    if (status != ZFS_OK) {
        return status;
    }
    char temporary[ZFS_PATH_MAX];
    FILE* file = zfs_temp_sibling(resolved, temporary, sizeof(temporary));
    if (file == NULL) {
        return zfs_error_set(error, ZFS_IO, "Cannot create backup temporary file");
    }
    unsigned char original[ZFS_TRANSFER_BUFFER_SIZE];
    unsigned char saved[ZFS_TRANSFER_BUFFER_SIZE];
    uint64_t size = (uint64_t) partition_info.sectors * DISK_SECTOR_SIZE;
    uint64_t offset = (uint64_t) partition_info.start_lba * DISK_SECTOR_SIZE;
    for (uint64_t at = 0; status == ZFS_OK && at < size; at += sizeof(original)) {
        size_t n = size - at < sizeof(original) ? (size_t) (size - at) : sizeof(original);
        status = zfs_core_read(disk, offset + at, original, n, error);
        if (status == ZFS_OK && fwrite(original, 1, n, file) != n) {
            status = zfs_error_set(error, ZFS_IO, "Backup write failed; target unchanged");
        }
        if (disk->callbacks.progress != NULL) {
            disk->callbacks.progress(disk->callbacks.user, at + n, size);
        }
    }
    if (status == ZFS_OK && zfs_host_sync(file) != 0) {
        status = zfs_error_set(error, ZFS_IO, "Backup flush failed; target unchanged");
    }
    for (uint64_t at = 0; status == ZFS_OK && at < size; at += sizeof(original)) {
        size_t n = size - at < sizeof(original) ? (size_t) (size - at) : sizeof(original);
        status = zfs_core_read(disk, offset + at, original, n, error);
        if (status == ZFS_OK &&
            (zfs_seek(file, at) != 0 || fread(saved, 1, n, file) != n || memcmp(saved, original, n) != 0)) {
            status = zfs_error_set(error, ZFS_IO, "Backup verification failed; target unchanged");
        }
    }
    if (status == ZFS_OK && !zfs_core_unchanged(disk)) {
        status = zfs_error_set(error, ZFS_BUSY, "Target changed while backing up; reopen and retry");
    }
    if (fclose(file) != 0 && status == ZFS_OK) {
        status = zfs_error_set(error, ZFS_IO, "Backup close failed");
    }
    if (status == ZFS_OK && zfs_host_publish(temporary, resolved, false) != 0) {
        status = zfs_error_set(error, ZFS_IO, "Backup publication failed; destination may already exist");
    }
    if (status != ZFS_OK) {
        zfs_host_remove(temporary);
    }
    return status;
}

static zfs_status plan_current(zfs_merge_plan* plan, zfs_error* error)
{
    if (plan == NULL || !plan->ready || plan->view == NULL) {
        return zfs_error_set(error, ZFS_INVALID, "No prepared operation");
    }
    if (plan->target->info.pending || plan->target->revision != plan->revision || !zfs_core_unchanged(plan->target)) {
        return zfs_error_set(error, ZFS_BUSY, "Target changed since preparation; prepare again");
    }
    return ZFS_OK;
}

zfs_status zfs_merge_check_backup(zfs_merge_plan* plan, const char* backup, zfs_error* error)
{
    zfs_status status = plan_current(plan, error);
    if (status != ZFS_OK) {
        return status;
    }
    if (plan->restore) {
        return zfs_error_set(error, ZFS_INVALID, "Restore does not create a backup");
    }
    if (plan->report->noop) {
        return ZFS_OK;
    }
    char resolved[ZFS_PATH_MAX];
    status = destination(plan->target, backup, resolved, error);
    return status != ZFS_OK ? status
                            : backup_space(resolved, (uint64_t) plan->partition.sectors * DISK_SECTOR_SIZE, error);
}

zfs_status zfs_merge_stage(zfs_merge_plan* plan, const char* backup, zfs_error* error)
{
    zfs_status status = plan_current(plan, error);
    if (status != ZFS_OK) {
        return status;
    }
    if (plan->restore) {
        return zfs_error_set(error, ZFS_INVALID, "Use restore staging for this plan");
    }
    if (plan->report->noop) {
        return ZFS_OK;
    }
    char resolved[ZFS_PATH_MAX] = {0};
    if (backup != NULL) {
        status = destination(plan->target, backup, resolved, error);
        if (status != ZFS_OK) {
            return status;
        }
        status = zfs_backup_partition(plan->target, plan->report->target_partition, resolved, error);
        if (status != ZFS_OK) {
            return status;
        }
        snprintf(plan->report->backup, sizeof(plan->report->backup), "%s", resolved);
        plan->report->backup_created = true;
        plan->report->backup_bytes = (uint64_t) plan->partition.sectors * DISK_SECTOR_SIZE;
    }
    zfs_disk* disk = plan->target;
    zfs_core_attach(disk, plan->view);
    plan->ready = false;
    plan->report->staged = true;
    snprintf(disk->recovery_backup, sizeof(disk->recovery_backup), "%s", resolved);
    disk->recovery_partition = plan->report->target_partition;
    disk->recovery_offset = (uint64_t) plan->partition.start_lba * DISK_SECTOR_SIZE;
    disk->recovery_size = (uint64_t) plan->partition.sectors * DISK_SECTOR_SIZE;
    disk->recovery_is_merge = backup != NULL;
    return ZFS_OK;
}

zfs_status zfs_restore_prepare(const char* backup, zfs_disk* target, int target_partition, zfs_merge_plan** out,
                               zfs_merge_report* report, zfs_error* error)
{
    if (out == NULL || report == NULL) {
        return zfs_error_set(error, ZFS_INVALID, "Restore plan and report outputs are required");
    }
    *out = NULL;
    memset(report, 0, sizeof(*report));
    report->restore = true;
    zfs_disk* source = NULL;
    zfs_status status = zfs_open(backup, true, &source, error);
    if (status != ZFS_OK) {
        return status;
    }
    if (source->info.has_mbr) {
        zfs_close(source);
        return zfs_error_set(error, ZFS_INVALID, "Restore requires a raw partition backup, not an MBR disk image");
    }
    status = create_plan(source, 0, target, target_partition, true, out, report, error);
    if (status == ZFS_OK) {
        zfs_merge_plan* plan = *out;
        uint64_t size = (uint64_t) plan->partition.sectors * DISK_SECTOR_SIZE;
        if (source->info.size != size) {
            status = zfs_error_set(error, ZFS_INVALID, "Backup capacity must exactly match the target partition");
        }
        if (status == ZFS_OK) {
            status = validate_tree(source, 0, "/", error);
        }
        if (status == ZFS_OK) {
            status = zfs_core_stage_raw(plan->view,
                                        (uint64_t) plan->partition.start_lba * DISK_SECTOR_SIZE,
                                        source,
                                        size,
                                        error);
        }
        if (status == ZFS_OK && (!zfs_core_unchanged(source) || !zfs_core_unchanged(target))) {
            status = zfs_error_set(error, ZFS_BUSY, "Source or target changed during restore preparation");
        }
        if (status == ZFS_OK) {
            report->backup_bytes = size;
            report->bytes_copied = size;
            report->noop = !plan->view->info.pending;
            snprintf(report->backup, sizeof(report->backup), "%s", source->info.path);
            plan->ready = true;
        }
    }
    zfs_close(source);
    if (status != ZFS_OK) {
        zfs_merge_plan_free(*out);
        *out = NULL;
    }
    return status;
}

zfs_status zfs_restore_stage(zfs_merge_plan* plan, zfs_error* error)
{
    zfs_status status = plan_current(plan, error);
    if (status != ZFS_OK) {
        return status;
    }
    if (!plan->restore) {
        return zfs_error_set(error, ZFS_INVALID, "Use merge staging for this plan");
    }
    if (plan->report->noop) {
        return ZFS_OK;
    }
    zfs_core_attach(plan->target, plan->view);
    plan->ready = false;
    plan->report->staged = true;
    snprintf(plan->target->recovery_backup, sizeof(plan->target->recovery_backup), "%s", plan->report->backup);
    plan->target->recovery_partition = plan->report->target_partition;
    plan->target->recovery_is_merge = false;
    return ZFS_OK;
}

void zfs_merge_plan_free(zfs_merge_plan* plan)
{
    if (plan == NULL) {
        return;
    }
    zfs_close(plan->view);
    for (size_t i = 0; i < plan->count; ++i) {
        free(plan->actions[i].path);
    }
    free(plan->actions);
    free(plan);
}

void zfs_merge_report_free(zfs_merge_report* report)
{
    if (report == NULL) {
        return;
    }
    for (size_t i = 0; i < report->conflict_count; ++i) {
        free(report->conflicts[i]);
    }
    free(report->conflicts);
    memset(report, 0, sizeof(*report));
}
