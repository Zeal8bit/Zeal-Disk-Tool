/**
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cli.h"
#include <stdio.h>
#include <string.h>

static bool select_partition(zfs_disk* disk, int requested, bool raw_restore, int* selected, zfs_error* error)
{
    const zfs_info* info = zfs_get_info(disk);
    if (raw_restore && !info->has_mbr) {
        if (requested > 0) {
            error->code = ZFS_INVALID;
            snprintf(error->message, sizeof(error->message), "Raw targets use partition 1");
            return false;
        }
        *selected = 0;
        return true;
    }
    if (requested >= 0) {
        if (requested < 4 && info->partitions[requested].active && info->partitions[requested].type == 0x5a) {
            *selected = requested;
            return true;
        }
        error->code = ZFS_INVALID;
        snprintf(error->message, sizeof(error->message), "Selected partition is not an existing ZealFS partition");
        return false;
    }
    int count = 0;
    for (int i = 0; i < 4; ++i) {
        if (info->partitions[i].active && info->partitions[i].type == 0x5a) {
            *selected = i;
            ++count;
        }
    }
    if (count == 1) {
        return true;
    }
    error->code = ZFS_INVALID;
    snprintf(error->message,
             sizeof(error->message),
             "%s has %d ZealFS partitions; choose --partition or --from-partition explicitly",
             info->path,
             count);
    return false;
}

int cli_transfer(cli_session* session, const cli_options* opt, bool restore)
{
    if (opt->source == NULL) {
        return cli_failure(opt,
                           2,
                           "usage",
                           restore ? "restore requires --from-image BACKUP.img"
                                   : "merge requires --from-image PATH or --from-device PATH");
    }
    if (restore && (!opt->source_image || opt->backup != NULL || opt->source_partition >= 0)) {
        return cli_failure(opt,
                           2,
                           "usage",
                           "restore accepts a raw --from-image backup, without --backup or --from-partition");
    }
    if (zfs_get_info(session->disk)->pending) {
        return cli_failure(opt, 1, "busy", "Apply or discard pending changes before merge/restore");
    }
    zfs_error error = {0};
    zfs_merge_report report = {0};
    zfs_merge_plan* plan = NULL;
    zfs_disk* source = NULL;
    int target_partition = -1;
    int source_partition = -1;
    int result = 0;
    if (!select_partition(session->disk,
                          opt->partition_set ? opt->partition : -1,
                          restore,
                          &target_partition,
                          &error)) {
        return cli_failure(opt, 2, "usage", error.message);
    }
    zfs_status status;
    if (restore) {
        status = zfs_restore_prepare(opt->source, session->disk, target_partition, &plan, &report, &error);
    } else {
        status = zfs_open(opt->source, opt->source_image, &source, &error);
        if (status == ZFS_OK && !select_partition(source, opt->source_partition, false, &source_partition, &error)) {
            zfs_close(source);
            return cli_failure(opt, 2, "usage", error.message);
        }
        if (status == ZFS_OK) {
            status =
                zfs_merge_prepare(source, source_partition, session->disk, target_partition, &plan, &report, &error);
        }
    }
    zfs_close(source);
    /* Retain useful identity fields even if opening a source failed. */
    if (report.source[0] == 0) {
        snprintf(report.source, sizeof(report.source), "%s", opt->source);
    }
    if (report.target[0] == 0) {
        snprintf(report.target, sizeof(report.target), "%s", zfs_get_info(session->disk)->path);
    }
    report.target_partition = target_partition;
    report.restore = restore;
    if (opt->backup != NULL && report.backup[0] == 0) {
        snprintf(report.backup, sizeof(report.backup), "%s", opt->backup);
    }
    if (status == ZFS_OK && !restore && !opt->dry_run && opt->backup != NULL) {
        status = zfs_merge_check_backup(plan, opt->backup, &error);
    }
    if (status != ZFS_OK) {
        result = cli_transfer_report(opt, &report, "failed", 1, &error);
        goto done;
    }
    if (opt->dry_run) {
        result = cli_transfer_report(opt, &report, "preview", 0, NULL);
        goto done;
    }
    if (report.noop) {
        result = cli_transfer_report(opt, &report, "unchanged", 0, NULL);
        goto done;
    }
    fprintf(stderr,
            "Proposed %s for partition %d on %s: %llu added, %llu replaced, %llu unchanged, %llu preserved files.\n",
            restore ? "restore" : "merge",
            target_partition + 1,
            report.target,
            (unsigned long long) report.files_added,
            (unsigned long long) report.files_replaced,
            (unsigned long long) report.files_unchanged,
            (unsigned long long) report.files_preserved);
    if (!restore && opt->backup != NULL) {
        fprintf(stderr, "Backup: %s (%llu bytes).\n", opt->backup, (unsigned long long) report.backup_bytes);
    }
    bool without_backup = !restore && opt->backup == NULL;
    if (without_backup) {
        fprintf(stderr, "No backup will be created.\n");
    }
    const char* prompt = without_backup ? "Proceed without a backup?"
                         : restore      ? "Restore this filesystem?"
                                        : "Create the backup and apply this merge?";
    if (!opt->yes && (without_backup || !session->shell) && cli_confirm(prompt, false) == 0) {
        error.code = ZFS_BUSY;
        snprintf(error.message, sizeof(error.message), "Cancelled; target unchanged");
        result = cli_transfer_report(opt, &report, "cancelled", 3, &error);
        goto done;
    }
    status = restore ? zfs_restore_stage(plan, &error) : zfs_merge_stage(plan, opt->backup, &error);
    if (status == ZFS_OK && !session->shell) {
        status = zfs_apply(session->disk, &error);
        if (status == ZFS_OK) {
            report.applied = true;
        }
    }
    if (status != ZFS_OK) {
        result = cli_transfer_report(opt, &report, status == ZFS_PARTIAL_WRITE ? "partial_write" : "failed", 1, &error);
    } else {
        result = cli_transfer_report(opt, &report, session->shell ? "staged" : "applied", 0, NULL);
    }
    if (status != ZFS_OK) {
        cli_recovery_notice(session->disk);
    }
done:
    zfs_merge_plan_free(plan);
    zfs_merge_report_free(&report);
    return result;
}
