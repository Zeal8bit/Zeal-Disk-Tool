/**
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* CLI presentation for disk metadata, listings, transfer reports and errors.
 * Human-readable text serves interactive use; JSON gives CI and ZDE host scripts
 * structured results without parsing display text. Keep formatting and escaping
 * here so the shared disk API remains independent of console I/O. */
#include "cli.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char* json_bool(bool value)
{
    return value ? "true" : "false";
}

static void json_string(const char* value)
{
    putchar('"');
    for (const unsigned char* p = (const unsigned char*) value; *p != 0; ++p) {
        if (*p == '"' || *p == '\\') {
            putchar('\\');
            putchar(*p);
        } else if (*p < 32) {
            printf("\\u%04x", *p);
        } else {
            putchar(*p);
        }
    }
    putchar('"');
}

int cli_failure(const cli_options* opt, int exit_code, const char* code, const char* message)
{
    fprintf(stderr, "zdt: %s\n", message);
    if (opt->json) {
        printf("{\"ok\":false,\"error\":{\"code\":");
        json_string(code);
        printf(",\"message\":");
        json_string(message);
        puts("}}");
    }
    return exit_code;
}

int cli_api_failure(const cli_options* opt, const zfs_error* error)
{
    return cli_failure(opt, 1, zfs_status_string(error->code), error->message);
}

void cli_success(const cli_options* opt, const char* message)
{
    if (opt->json) {
        printf("{\"ok\":true,\"data\":{\"message\":");
        json_string(message);
        puts("}}");
    } else {
        puts(message);
    }
}

static void print_info(const cli_options* opt, const zfs_info* info, zfs_disk* disk)
{
    if (opt->json) {
        printf("{\"path\":");
        json_string(info->path);
        printf(",\"image\":%s,\"size\":%" PRIu64
               ",\"mbr\":%s,\"pending\":%s,\"new_image\":%s,\"failed\":%s,\"partitions\":[",
               json_bool(info->image),
               info->size,
               json_bool(info->has_mbr),
               json_bool(info->pending),
               json_bool(info->new_image),
               json_bool(info->failed));
    } else {
        printf("%s (%s, %" PRIu64 " bytes)\nMBR: %s  Pending: %s%s\n",
               info->path,
               info->image ? "image" : "device",
               info->size,
               json_bool(info->has_mbr),
               json_bool(info->pending),
               info->failed ? "  FAILED: reopen required" : "");
    }
    bool comma = false;
    for (int i = 0; i < ZFS_MAX_PARTITIONS; ++i) {
        const zfs_partition* partition_info = &info->partitions[i];
        if (!partition_info->active) {
            continue;
        }
        uint64_t available = 0;
        uint64_t total = 0;
        bool usage = disk != NULL && zfs_usage(disk, i, &available, &total, NULL) == ZFS_OK;
        if (opt->json) {
            if (comma) {
                putchar(',');
            }
            printf("{\"partition\":%d,\"type\":%u,\"start_lba\":%u,\"sectors\":%u",
                   i + 1,
                   partition_info->type,
                   partition_info->start_lba,
                   partition_info->sectors);
            if (usage) {
                printf(",\"free_bytes\":%" PRIu64 ",\"total_bytes\":%" PRIu64, available, total);
            }
            putchar('}');
        } else {
            printf("  %d  type 0x%02x  LBA %u  %" PRIu64 " bytes",
                   i + 1,
                   partition_info->type,
                   partition_info->start_lba,
                   (uint64_t) partition_info->sectors * DISK_SECTOR_SIZE);
            if (usage) {
                printf("  free %" PRIu64 "/%" PRIu64, available, total);
            }
            putchar('\n');
        }
        comma = true;
    }
    if (opt->json) {
        printf("]}");
    }
}

void cli_print_devices(const cli_options* opt, const zfs_info* found, size_t count)
{
    if (opt->json) {
        printf("{\"ok\":true,\"data\":[");
    }
    for (size_t i = 0; i < count; ++i) {
        if (opt->json && i != 0) {
            putchar(',');
        }
        print_info(opt, &found[i], NULL);
    }
    if (opt->json) {
        puts("]}");
    }
}

void cli_print_disk(const cli_options* opt, zfs_disk* disk)
{
    if (opt->json) {
        printf("{\"ok\":true,\"data\":");
    }
    print_info(opt, zfs_get_info(disk), disk);
    if (opt->json) {
        puts("}");
    }
}

void cli_print_entries(const cli_options* opt, const zfs_entry* entries, size_t count)
{
    if (opt->json) {
        printf("{\"ok\":true,\"data\":[");
    }
    for (size_t i = 0; i < count; ++i) {
        const zfs_entry* entry = &entries[i];
        if (opt->json) {
            if (i != 0) {
                putchar(',');
            }
            printf("{\"name\":");
            json_string(entry->name);
            printf(",\"directory\":%s,\"size\":%u,\"modified\":\"%04u-%02u-%02uT%02u:%02u:%02u\"}",
                   json_bool(entry->directory),
                   entry->size,
                   entry->year,
                   entry->month,
                   entry->day,
                   entry->hour,
                   entry->minute,
                   entry->second);
        } else {
            printf("%-17s %10u  %04u-%02u-%02u %02u:%02u:%02u  %s\n",
                   entry->name,
                   entry->size,
                   entry->year,
                   entry->month,
                   entry->day,
                   entry->hour,
                   entry->minute,
                   entry->second,
                   entry->directory ? "directory" : "file");
        }
    }
    if (opt->json) {
        puts("]}");
    }
}

void cli_print_summary(const cli_options* opt, const zfs_info* info, bool creating)
{
    fprintf(stderr,
            "%s: %s staged on %s (%" PRIu64 " bytes).\n",
            opt->argv[0],
            creating ? "image creation" : "change",
            info->path,
            info->size);
}

void cli_recovery_notice(zfs_disk* disk)
{
    const char* backup = zfs_recovery_backup(disk);
    if (backup == NULL) {
        return;
    }
    fprintf(stderr,
            "Backup retained: %s\nReopen the target, then use restore --from-image BACKUP --partition %d.\n",
            backup,
            zfs_recovery_partition(disk) + 1);
}

int cli_transfer_report(const cli_options* opt, const zfs_merge_report* report, const char* state, int exit_code,
                        const zfs_error* error)
{
    if (error != NULL) {
        fprintf(stderr, "zdt: %s\n", error->message);
    }
    if (opt->json) {
        printf("{\"ok\":%s,\"data\":{\"operation\":", json_bool(exit_code == 0));
        json_string(report->restore ? "restore" : "merge");
        printf(",\"state\":");
        json_string(state);
        printf(",\"source\":");
        json_string(report->source);
        printf(",\"target\":");
        json_string(report->target);
        printf(",\"source_partition\":%d,\"target_partition\":%d,\"backup_path\":",
               report->source_partition + 1,
               report->target_partition + 1);
        json_string(report->backup);
        printf(",\"files_added\":%" PRIu64 ",\"files_replaced\":%" PRIu64 ",\"files_unchanged\":%" PRIu64
               ",\"files_preserved\":%" PRIu64,
               report->files_added,
               report->files_replaced,
               report->files_unchanged,
               report->files_preserved);
        printf(",\"directories_created\":%" PRIu64 ",\"directories_merged\":%" PRIu64
               ",\"directories_preserved\":%" PRIu64,
               report->directories_created,
               report->directories_merged,
               report->directories_preserved);
        printf(",\"bytes_copied\":%" PRIu64 ",\"backup_bytes\":%" PRIu64
               ",\"backup_created\":%s,\"noop\":%s,\"staged\":%s,\"applied\":%s,\"conflicts\":[",
               report->bytes_copied,
               report->backup_bytes,
               json_bool(report->backup_created),
               json_bool(report->noop),
               json_bool(report->staged),
               json_bool(report->applied));
        for (size_t i = 0; i < report->conflict_count; ++i) {
            if (i != 0) {
                putchar(',');
            }
            json_string(report->conflicts[i]);
        }
        printf("]}");
        if (error != NULL) {
            printf(",\"error\":{\"code\":");
            json_string(exit_code == 3 ? "confirmation" : zfs_status_string(error->code));
            printf(",\"message\":");
            json_string(error->message);
            putchar('}');
        }
        puts("}");
    } else {
        printf("%s: %s\nFiles: %" PRIu64 " added, %" PRIu64 " replaced, %" PRIu64 " unchanged, %" PRIu64 " preserved\n",
               report->restore ? "Restore" : "Merge",
               state,
               report->files_added,
               report->files_replaced,
               report->files_unchanged,
               report->files_preserved);
        for (size_t i = 0; i < report->conflict_count; ++i) {
            printf("Type conflict: %s\n", report->conflicts[i]);
        }
        if (report->backup_created) {
            printf("Verified backup: %s (%" PRIu64 " bytes)\n", report->backup, report->backup_bytes);
        }
        if (exit_code == 0 && strcmp(state, "staged") == 0) {
            puts("Use apply to save, or discard to cancel.");
            if (report->backup_created) {
                puts("The backup is retained.");
            }
        }
    }
    return exit_code;
}
