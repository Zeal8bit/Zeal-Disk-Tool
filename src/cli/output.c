/* SPDX-License-Identifier: Apache-2.0 */
#include "cli.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *json_bool(bool value) { return value ? "true" : "false"; }
static void json_string(const char *value) {
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '"' || *p == '\\') {
            putchar('\\');
            putchar(*p);
        } else if (*p < 32)
            printf("\\u%04x", *p);
        else
            putchar(*p);
    }
    putchar('"');
}
int cli_failure(const cli_options *o, int exit_code, const char *code, const char *message) {
    fprintf(stderr, "zdt: %s\n", message);
    if (o->json) {
        printf("{\"ok\":false,\"error\":{\"code\":");
        json_string(code);
        printf(",\"message\":");
        json_string(message);
        puts("}}");
    }
    return exit_code;
}
int cli_api_failure(const cli_options *o, const zfs_error *e) {
    return cli_failure(o, 1, zfs_status_string(e->code), e->message);
}
void cli_success(const cli_options *o, const char *message) {
    if (o->json) {
        printf("{\"ok\":true,\"data\":{\"message\":");
        json_string(message);
        puts("}}");
    } else
        puts(message);
}
static void print_info(const cli_options *o, const zfs_info *info, zfs_disk *disk) {
    if (o->json) {
        printf("{\"path\":");
        json_string(info->path);
        printf(",\"image\":%s,\"size\":%" PRIu64
               ",\"mbr\":%s,\"pending\":%s,\"new_image\":%s,\"failed\":%s,\"partitions\":[",
               json_bool(info->image), info->size, json_bool(info->has_mbr), json_bool(info->pending),
               json_bool(info->new_image), json_bool(info->failed));
    } else
        printf("%s (%s, %" PRIu64 " bytes)\nMBR: %s  Pending: %s%s\n", info->path,
               info->image ? "image" : "device", info->size, json_bool(info->has_mbr),
               json_bool(info->pending), info->failed ? "  FAILED: reopen required" : "");
    bool comma = false;
    for (int i = 0; i < 4; ++i) {
        const zfs_partition *p = &info->partitions[i];
        if (!p->active)
            continue;
        uint64_t available = 0, total = 0;
        bool usage = disk && zfs_usage(disk, i, &available, &total, NULL) == ZFS_OK;
        if (o->json) {
            if (comma)
                putchar(',');
            printf("{\"partition\":%d,\"type\":%u,\"start_lba\":%u,\"sectors\":%u", i + 1, p->type,
                   p->start_lba, p->sectors);
            if (usage)
                printf(",\"free_bytes\":%" PRIu64 ",\"total_bytes\":%" PRIu64, available, total);
            putchar('}');
        } else {
            printf("  %d  type 0x%02x  LBA %u  %" PRIu64 " bytes", i + 1, p->type, p->start_lba,
                   (uint64_t)p->sectors * 512);
            if (usage)
                printf("  free %" PRIu64 "/%" PRIu64, available, total);
            putchar('\n');
        }
        comma = true;
    }
    if (o->json)
        printf("]}");
}
void cli_print_devices(const cli_options *o, const zfs_info *found, size_t count) {
    if (o->json)
        printf("{\"ok\":true,\"data\":[");
    for (size_t i = 0; i < count; ++i) {
        if (o->json && i)
            putchar(',');
        print_info(o, &found[i], NULL);
    }
    if (o->json)
        puts("]}");
}
void cli_print_disk(const cli_options *o, zfs_disk *disk) {
    if (o->json)
        printf("{\"ok\":true,\"data\":");
    print_info(o, zfs_get_info(disk), disk);
    if (o->json)
        puts("}");
}
void cli_print_entries(const cli_options *o, const zfs_entry *entries, size_t count) {
    if (o->json)
        printf("{\"ok\":true,\"data\":[");
    for (size_t i = 0; i < count; ++i) {
        const zfs_entry *p = &entries[i];
        if (o->json) {
            if (i)
                putchar(',');
            printf("{\"name\":");
            json_string(p->name);
            printf(",\"directory\":%s,\"size\":%u,\"modified\":\"%04u-%02u-%02uT%02u:%02u:%02u\"}",
                   json_bool(p->directory), p->size, p->year, p->month, p->day, p->hour, p->minute,
                   p->second);
        } else
            printf("%-17s %10u  %04u-%02u-%02u %02u:%02u:%02u  %s\n", p->name, p->size, p->year, p->month,
                   p->day, p->hour, p->minute, p->second, p->directory ? "directory" : "file");
    }
    if (o->json)
        puts("]}");
}
void cli_print_summary(const cli_options *o, const zfs_info *info, bool creating) {
    fprintf(stderr, "%s: %s staged on %s (%" PRIu64 " bytes).\n", o->argv[0],
            creating ? "image creation" : "change", info->path, info->size);
}

void cli_recovery_notice(zfs_disk *disk) {
    const char *backup = zfs_recovery_backup(disk);
    if (!backup)
        return;
    fprintf(stderr,
            "Backup retained: %s\nReopen the target, then use restore --from-image BACKUP --partition %d.\n",
            backup, zfs_recovery_partition(disk) + 1);
}
int cli_transfer_report(const cli_options *o, const zfs_merge_report *r, const char *state, int exit_code,
                        const zfs_error *e) {
    if (e)
        fprintf(stderr, "zdt: %s\n", e->message);
    if (o->json) {
        printf("{\"ok\":%s,\"data\":{\"operation\":", json_bool(!exit_code));
        json_string(r->restore ? "restore" : "merge");
        printf(",\"state\":");
        json_string(state);
        printf(",\"source\":");
        json_string(r->source);
        printf(",\"target\":");
        json_string(r->target);
        printf(",\"source_partition\":%d,\"target_partition\":%d,\"backup_path\":", r->source_partition + 1,
               r->target_partition + 1);
        json_string(r->backup);
        printf(",\"files_added\":%" PRIu64 ",\"files_replaced\":%" PRIu64 ",\"files_unchanged\":%" PRIu64
               ",\"files_preserved\":%" PRIu64,
               r->files_added, r->files_replaced, r->files_unchanged, r->files_preserved);
        printf(",\"directories_created\":%" PRIu64 ",\"directories_merged\":%" PRIu64
               ",\"directories_preserved\":%" PRIu64,
               r->directories_created, r->directories_merged, r->directories_preserved);
        printf(",\"bytes_copied\":%" PRIu64 ",\"backup_bytes\":%" PRIu64
               ",\"backup_created\":%s,\"noop\":%s,\"staged\":%s,\"applied\":%s,\"conflicts\":[",
               r->bytes_copied, r->backup_bytes, json_bool(r->backup_created), json_bool(r->noop),
               json_bool(r->staged), json_bool(r->applied));
        for (size_t i = 0; i < r->conflict_count; ++i) {
            if (i)
                putchar(',');
            json_string(r->conflicts[i]);
        }
        printf("]}");
        if (e) {
            printf(",\"error\":{\"code\":");
            json_string(exit_code == 3 ? "confirmation" : zfs_status_string(e->code));
            printf(",\"message\":");
            json_string(e->message);
            putchar('}');
        }
        puts("}");
    } else {
        printf("%s: %s\nFiles: %" PRIu64 " added, %" PRIu64 " replaced, %" PRIu64 " unchanged, %" PRIu64
               " preserved\n",
               r->restore ? "Restore" : "Merge", state, r->files_added, r->files_replaced, r->files_unchanged,
               r->files_preserved);
        for (size_t i = 0; i < r->conflict_count; ++i)
            printf("Type conflict: %s\n", r->conflicts[i]);
        if (r->backup_created)
            printf("Verified backup: %s (%" PRIu64 " bytes)\n", r->backup, r->backup_bytes);
        if (!exit_code && !strcmp(state, "staged")) {
            puts("Use apply to save, or discard to cancel.");
            if (r->backup_created)
                puts("The backup is retained.");
        }
    }
    return exit_code;
}
