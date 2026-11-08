/* SPDX-License-Identifier: Apache-2.0 */
#include "cli.h"
#include <stdio.h>
#include <string.h>

static bool select_partition(zfs_disk *disk, int requested, bool raw_restore, int *selected, zfs_error *e) {
    const zfs_info *info = zfs_get_info(disk);
    if (raw_restore && !info->has_mbr) {
        if (requested > 0) {
            e->code = ZFS_INVALID;
            snprintf(e->message, sizeof(e->message), "Raw targets use partition 1");
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
        e->code = ZFS_INVALID;
        snprintf(e->message, sizeof(e->message), "Selected partition is not an existing ZealFS partition");
        return false;
    }
    int count = 0;
    for (int i = 0; i < 4; ++i)
        if (info->partitions[i].active && info->partitions[i].type == 0x5a) {
            *selected = i;
            ++count;
        }
    if (count == 1)
        return true;
    e->code = ZFS_INVALID;
    snprintf(e->message, sizeof(e->message),
             "%s has %d ZealFS partitions; choose --partition or --from-partition explicitly", info->path,
             count);
    return false;
}
int cli_transfer(cli_session *s, const cli_options *o, bool restore) {
    if (!o->source)
        return cli_failure(o, 2, "usage",
                           restore ? "restore requires --from-image BACKUP.img"
                                   : "merge requires --from-image PATH or --from-device PATH");
    if (restore && (!o->source_image || o->backup || o->source_partition >= 0))
        return cli_failure(o, 2, "usage",
                           "restore accepts a raw --from-image backup, without --backup or --from-partition");
    if (zfs_get_info(s->disk)->pending)
        return cli_failure(o, 1, "busy", "Apply or discard pending changes before merge/restore");
    zfs_error error = {0};
    zfs_merge_report report = {0};
    zfs_merge_plan *plan = NULL;
    zfs_disk *source = NULL;
    int tp = -1, sp = -1, result = 0;
    if (!select_partition(s->disk, o->partition_set ? o->partition : -1, restore, &tp, &error))
        return cli_failure(o, 2, "usage", error.message);
    zfs_status status;
    if (restore)
        status = zfs_restore_prepare(o->source, s->disk, tp, &plan, &report, &error);
    else {
        status = zfs_open(o->source, o->source_image, &source, &error);
        if (!status && !select_partition(source, o->source_partition, false, &sp, &error)) {
            zfs_close(source);
            return cli_failure(o, 2, "usage", error.message);
        }
        if (!status)
            status = zfs_merge_prepare(source, sp, s->disk, tp, &plan, &report, &error);
    }
    zfs_close(source);
    /* Retain useful identity fields even if opening a source failed. */
    if (!report.source[0])
        snprintf(report.source, sizeof(report.source), "%s", o->source);
    if (!report.target[0])
        snprintf(report.target, sizeof(report.target), "%s", zfs_get_info(s->disk)->path);
    report.target_partition = tp;
    report.restore = restore;
    if (o->backup && !report.backup[0])
        snprintf(report.backup, sizeof(report.backup), "%s", o->backup);
    if (!status && !restore && !o->dry_run && o->backup)
        status = zfs_merge_check_backup(plan, o->backup, &error);
    if (status) {
        result = cli_transfer_report(o, &report, "failed", 1, &error);
        goto done;
    }
    if (o->dry_run) {
        result = cli_transfer_report(o, &report, "preview", 0, NULL);
        goto done;
    }
    if (report.noop) {
        result = cli_transfer_report(o, &report, "unchanged", 0, NULL);
        goto done;
    }
    fprintf(stderr,
            "Proposed %s for partition %d on %s: %llu added, %llu replaced, %llu unchanged, %llu preserved files.\n",
            restore ? "restore" : "merge", tp + 1, report.target, (unsigned long long)report.files_added,
            (unsigned long long)report.files_replaced, (unsigned long long)report.files_unchanged,
            (unsigned long long)report.files_preserved);
    if (!restore && o->backup)
        fprintf(stderr, "Backup: %s (%llu bytes).\n", o->backup, (unsigned long long)report.backup_bytes);
    bool without_backup = !restore && !o->backup;
    if (without_backup)
        fprintf(stderr, "No backup will be created.\n");
    const char *prompt = without_backup ? "Proceed without a backup?"
                         : restore ? "Restore this filesystem?"
                                   : "Create the backup and apply this merge?";
    if (!o->yes && (without_backup || !s->shell) && !cli_confirm(prompt, false)) {
        error.code = ZFS_BUSY;
        snprintf(error.message, sizeof(error.message), "Cancelled; target unchanged");
        result = cli_transfer_report(o, &report, "cancelled", 3, &error);
        goto done;
    }
    status = restore ? zfs_restore_stage(plan, &error) : zfs_merge_stage(plan, o->backup, &error);
    if (!status && !s->shell) {
        status = zfs_apply(s->disk, &error);
        if (!status)
            report.applied = true;
    }
    if (status)
        result = cli_transfer_report(o, &report, status == ZFS_PARTIAL_WRITE ? "partial_write" : "failed", 1,
                                     &error);
    else
        result = cli_transfer_report(o, &report, s->shell ? "staged" : "applied", 0, NULL);
    if (status)
        cli_recovery_notice(s->disk);
done:
    zfs_merge_plan_free(plan);
    zfs_merge_report_free(&report);
    return result;
}
