/* SPDX-License-Identifier: Apache-2.0 */
#include "cli.h"
#include "app_version.h"
#include <stdlib.h>
#include <string.h>

int cli_execute(cli_session *s, const cli_options *input) {
    cli_options normalized = *input;
    const cli_options *o = &normalized;
    int path_argument = -1;
    if (cli_command_matches(o, "ls", NULL, 2) || cli_command_matches(o, "mkdir", NULL, 2) ||
        cli_command_matches(o, "rm", NULL, 2) || cli_command_matches(o, "rmdir", NULL, 2) ||
        cli_command_matches(o, "export", NULL, 3))
        path_argument = 1;
    else if (cli_command_matches(o, "import", NULL, 3))
        path_argument = 2;
    char path[512];
    if (path_argument >= 0) {
        if (!cli_filesystem_path(o->argv[path_argument], path))
            return cli_failure(
                o, 1, "invalid argument",
                "ZealFS path must be nonempty and shorter than 512 bytes including its root slash");
        normalized.argv[path_argument] = path;
    }
    if (!o->argc)
        return cli_failure(o, 2, "usage", "A command is required; use --help");
    if (cli_command_matches(o, "help", NULL, 1)) {
        if (s->shell)
            cli_shell_help();
        else
            cli_help();
        return 0;
    }
    if (cli_command_matches(o, "version", NULL, 1)) {
        cli_success(o, VERSION);
        return 0;
    }
    zfs_error e = {0};
    zfs_status status = ZFS_OK;
    if (cli_command_matches(o, "devices", NULL, 1)) {
        zfs_info *found = calloc(128, sizeof(*found));
        size_t count = 0;
        if (!found)
            return cli_failure(o, 1, "out of memory", "Cannot allocate discovery buffer");
        status = zfs_discover(found, 128, &count, &e);
        if (!status)
            cli_print_devices(o, found, count);
        free(found);
        return status ? cli_api_failure(o, &e) : 0;
    }
    if (cli_command_matches(o, "open", NULL, 1)) {
        if (!s->shell || !o->target)
            return cli_failure(o, 2, "usage", "Shell open requires --image PATH or --device PATH");
        int r = cli_leave_disk(s, o);
        if (r)
            return r;
        status = zfs_open(o->target, o->image, &s->disk, &e);
        if (status)
            return cli_api_failure(o, &e);
        cli_success(o, "Target opened");
        return 0;
    }
    bool creating = cli_command_matches(o, "image", "create", 3);
    if (creating) {
        if (!o->size_set || !o->size || o->target)
            return cli_failure(o, 2, "usage", "Use image create PATH --size SIZE [--mbr]");
        if (s->disk) {
            int r = cli_leave_disk(s, o);
            if (r)
                return r;
        }
        status = zfs_image_create(o->argv[2], o->size, o->mbr, &s->disk, &e);
    } else if (!s->disk && o->target)
        status = zfs_open(o->target, o->image, &s->disk, &e);
    else if (s->shell && o->target)
        return cli_failure(o, 2, "usage", "Use open to switch targets in the shell");
    if (status)
        return cli_api_failure(o, &e);
    if (!s->disk)
        return cli_failure(o, 2, "usage", "Select a target with --image PATH or --device PATH");
    if (cli_command_matches(o, "apply", NULL, 1) || cli_command_matches(o, "discard", NULL, 1)) {
        if (!s->shell)
            return cli_failure(o, 2, "usage", "Apply/discard are shell commands");
        if (o->dry_run)
            return cli_failure(o, 2, "usage", "--dry-run is for one-shot operations");
        bool applying = !strcmp(o->argv[0], "apply");
        if (applying)
            status = zfs_apply(s->disk, &e);
        else if (zfs_get_info(s->disk)->new_image) {
            zfs_close(s->disk);
            s->disk = NULL;
        } else
            status = zfs_discard(s->disk, &e);
        if (status)
            return cli_api_failure(o, &e);
        cli_success(o, applying ? "Changes applied" : "Changes discarded");
        return 0;
    }
    if (cli_command_matches(o, "info", NULL, 1) || cli_command_matches(o, "status", NULL, 1) ||
        cli_command_matches(o, "partition", "list", 2)) {
        cli_print_disk(o, s->disk);
        return 0;
    }
    if (cli_command_matches(o, "ls", NULL, 1) || cli_command_matches(o, "ls", NULL, 2)) {
        zfs_entry *entries = NULL;
        size_t count = 0;
        status = zfs_list(s->disk, o->partition, o->argc == 2 ? o->argv[1] : "/", &entries, &count, &e);
        if (status)
            return cli_api_failure(o, &e);
        cli_print_entries(o, entries, count);
        zfs_entries_free(entries);
        return 0;
    }
    if (cli_command_matches(o, "export", NULL, 3)) {
        if (o->dry_run) {
            size_t n;
            unsigned char b;
            status = zfs_file_read(s->disk, o->partition, o->argv[1], 0, &b, 1, &n, &e);
            if (status)
                return cli_api_failure(o, &e);
            cli_success(o, "Export validated; destination unchanged");
            return 0;
        }
        status = zfs_export(s->disk, o->partition, o->argv[1], o->argv[2], false, &e);
        if (status == ZFS_EXISTS) {
            if (!o->yes && !cli_confirm("Overwrite export destination?", false))
                return cli_failure(o, 3, "confirmation", "Export overwrite requires confirmation or --yes");
            status = zfs_export(s->disk, o->partition, o->argv[1], o->argv[2], true, &e);
        }
        if (status)
            return cli_api_failure(o, &e);
        cli_success(o, "File exported");
        return 0;
    }
    if (!creating) {
        if (cli_command_matches(o, "mbr", "create", 2))
            status = zfs_mbr_create(s->disk, &e);
        else if (cli_command_matches(o, "partition", "create", 2))
            status = zfs_partition_create(s->disk, o->size_set ? o->size : 0, o->alignment, NULL, &e);
        else if (cli_command_matches(o, "partition", "format", 2))
            status = zfs_partition_format(s->disk, o->partition, &e);
        else if (cli_command_matches(o, "partition", "delete", 2))
            status = zfs_partition_delete(s->disk, o->partition, &e);
        else if (cli_command_matches(o, "mkdir", NULL, 2))
            status = zfs_mkdir(s->disk, o->partition, o->argv[1], &e);
        else if (cli_command_matches(o, "rm", NULL, 2) || cli_command_matches(o, "rmdir", NULL, 2))
            status = zfs_remove(s->disk, o->partition, o->argv[1], !strcmp(o->argv[0], "rmdir"), &e);
        else if (cli_command_matches(o, "import", NULL, 3))
            status = zfs_import(s->disk, o->partition, o->argv[1], o->argv[2], &e);
        else
            return cli_failure(o, 2, "usage", "Unknown command or incorrect arguments; use help");
    }
    if (status)
        return cli_api_failure(o, &e);
    if (s->shell) {
        cli_success(o, "Change staged; use apply or discard");
        return 0;
    }
    const zfs_info *info = zfs_get_info(s->disk);
    cli_print_summary(o, info, creating);
    if (o->dry_run) {
        cli_success(o, "Dry run completed; target unchanged");
        return 0;
    }
    if (!o->yes && !cli_confirm("Apply changes?", false))
        return cli_failure(o, 3, "confirmation",
                           "Changes discarded; committing requires confirmation or --yes");
    status = zfs_apply(s->disk, &e);
    if (status)
        return cli_api_failure(o, &e);
    cli_success(o, "Changes applied");
    return 0;
}
