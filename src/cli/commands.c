/**
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cli.h"
#include "app_version.h"
#include <stdlib.h>
#include <string.h>

int cli_execute(cli_session* session, const cli_options* input)
{
    cli_options normalized = *input;
    const cli_options* opt = &normalized;
    bool merging = cli_command_matches(opt, "merge", NULL, 1);
    bool restoring = cli_command_matches(opt, "restore", NULL, 1);
    bool importing = opt->argc >= 3 && strcmp(opt->argv[0], "import") == 0;
    if (!merging && !restoring && (opt->source != NULL || opt->backup != NULL || opt->source_partition >= 0)) {
        return cli_failure(opt, 2, "usage", "Source and backup options are only valid for merge/restore");
    }
    int path_argument = -1;
    if (cli_command_matches(opt, "ls", NULL, 2) || cli_command_matches(opt, "mkdir", NULL, 2) ||
        cli_command_matches(opt, "rm", NULL, 2) || cli_command_matches(opt, "rmdir", NULL, 2) ||
        cli_command_matches(opt, "export", NULL, 3)) {
        path_argument = 1;
    } else if (importing) {
        path_argument = opt->argc - 1;
    }
    char path[ZFS_FS_PATH_MAX];
    if (path_argument >= 0) {
        if (!cli_filesystem_path(opt->argv[path_argument], path)) {
            return cli_failure(opt,
                               1,
                               "invalid argument",
                               "ZealFS path must be nonempty and shorter than 512 bytes including its root slash");
        }
        if (importing && strcmp(path, "/") != 0 &&
            opt->argv[path_argument][strlen(opt->argv[path_argument]) - 1] == '/') {
            size_t length = strlen(path);
            if (length + 1 >= sizeof(path)) {
                return cli_failure(opt, 1, "invalid argument", "Import destination path is too long");
            }
            path[length] = '/';
            path[length + 1] = 0;
        }
        normalized.argv[path_argument] = path;
    }
    if (opt->argc == 0) {
        return cli_failure(opt, 2, "usage", "A command is required; use --help");
    }
    if (cli_command_matches(opt, "help", NULL, 1)) {
        if (session->shell) {
            cli_shell_help();
        } else {
            cli_help();
        }
        return 0;
    }
    if (cli_command_matches(opt, "version", NULL, 1)) {
        cli_success(opt, VERSION);
        return 0;
    }
    zfs_error error = {0};
    zfs_status status = ZFS_OK;
    if (cli_command_matches(opt, "devices", NULL, 1)) {
        zfs_info* found = calloc(128, sizeof(*found));
        size_t count = 0;
        if (found == NULL) {
            return cli_failure(opt, 1, "out of memory", "Cannot allocate discovery buffer");
        }
        status = zfs_discover(found, 128, &count, &error);
        if (status == ZFS_OK) {
            cli_print_devices(opt, found, count);
        }
        free(found);
        return status != ZFS_OK ? cli_api_failure(opt, &error) : 0;
    }
    if (cli_command_matches(opt, "open", NULL, 1)) {
        if (!session->shell || opt->target == NULL) {
            return cli_failure(opt, 2, "usage", "Shell open requires --image PATH or --device PATH");
        }
        int result = cli_leave_disk(session, opt);
        if (result != 0) {
            return result;
        }
        status = zfs_open(opt->target, opt->image, &session->disk, &error);
        if (status != ZFS_OK) {
            return cli_api_failure(opt, &error);
        }
        cli_success(opt, "Target opened");
        return 0;
    }
    bool creating = cli_command_matches(opt, "image", "create", 3);
    if (creating) {
        if (!opt->size_set || opt->size == 0 || opt->target != NULL) {
            return cli_failure(opt, 2, "usage", "Use image create PATH --size SIZE [--mbr]");
        }
        if (session->disk != NULL) {
            int result = cli_leave_disk(session, opt);
            if (result != 0) {
                return result;
            }
        }
        status = zfs_image_create(opt->argv[2], opt->size, opt->mbr, &session->disk, &error);
    } else if (session->disk == NULL && opt->target != NULL) {
        status = zfs_open(opt->target, opt->image, &session->disk, &error);
    } else if (session->shell && opt->target != NULL) {
        return cli_failure(opt, 2, "usage", "Use open to switch targets in the shell");
    }
    if (status != ZFS_OK) {
        return cli_api_failure(opt, &error);
    }
    if (session->disk == NULL) {
        return cli_failure(opt, 2, "usage", "Select a target with --image PATH or --device PATH");
    }
    if (merging || restoring) {
        return cli_transfer(session, opt, restoring);
    }
    if (cli_command_matches(opt, "apply", NULL, 1) || cli_command_matches(opt, "discard", NULL, 1)) {
        if (!session->shell) {
            return cli_failure(opt, 2, "usage", "Apply/discard are shell commands");
        }
        if (opt->dry_run) {
            return cli_failure(opt, 2, "usage", "--dry-run is for one-shot operations");
        }
        bool applying = strcmp(opt->argv[0], "apply") == 0;
        if (applying) {
            status = zfs_apply(session->disk, &error);
        } else if (zfs_get_info(session->disk)->new_image) {
            zfs_close(session->disk);
            session->disk = NULL;
        } else {
            status = zfs_discard(session->disk, &error);
        }
        if (status != ZFS_OK) {
            if (applying) {
                cli_recovery_notice(session->disk);
            }
            return cli_api_failure(opt, &error);
        }
        cli_success(opt, applying ? "Changes applied" : "Changes discarded");
        return 0;
    }
    if (cli_command_matches(opt, "info", NULL, 1) || cli_command_matches(opt, "status", NULL, 1) ||
        cli_command_matches(opt, "partition", "list", 2)) {
        cli_print_disk(opt, session->disk);
        return 0;
    }
    if (cli_command_matches(opt, "ls", NULL, 1) || cli_command_matches(opt, "ls", NULL, 2)) {
        zfs_entry* entries = NULL;
        size_t count = 0;
        status = zfs_list(session->disk, opt->partition, opt->argc == 2 ? opt->argv[1] : "/", &entries, &count, &error);
        if (status != ZFS_OK) {
            return cli_api_failure(opt, &error);
        }
        cli_print_entries(opt, entries, count);
        zfs_entries_free(entries);
        return 0;
    }
    if (cli_command_matches(opt, "export", NULL, 3)) {
        if (opt->dry_run) {
            status = zfs_export_validate(session->disk, opt->partition, opt->argv[1], opt->argv[2], true, &error);
            if (status != ZFS_OK) {
                return cli_api_failure(opt, &error);
            }
            cli_success(opt, "Export validated; destination unchanged");
            return 0;
        }
        status = zfs_export(session->disk, opt->partition, opt->argv[1], opt->argv[2], false, &error);
        if (status == ZFS_EXISTS) {
            if (!opt->yes && cli_confirm("Overwrite export destination?", false) == 0) {
                return cli_failure(opt, 3, "confirmation", "Export overwrite requires confirmation or --yes");
            }
            status = zfs_export(session->disk, opt->partition, opt->argv[1], opt->argv[2], true, &error);
        }
        if (status != ZFS_OK) {
            return cli_api_failure(opt, &error);
        }
        cli_success(opt, "Export completed");
        return 0;
    }
    if (!creating) {
        if (cli_command_matches(opt, "mbr", "create", 2)) {
            status = zfs_mbr_create(session->disk, &error);
        } else if (cli_command_matches(opt, "partition", "create", 2)) {
            status = zfs_partition_create(session->disk, opt->size_set ? opt->size : 0, opt->alignment, NULL, &error);
        } else if (cli_command_matches(opt, "partition", "format", 2)) {
            status = zfs_partition_format(session->disk, opt->partition, &error);
        } else if (cli_command_matches(opt, "partition", "delete", 2)) {
            status = zfs_partition_delete(session->disk, opt->partition, &error);
        } else if (cli_command_matches(opt, "mkdir", NULL, 2)) {
            status = zfs_mkdir(session->disk, opt->partition, opt->argv[1], &error);
        } else if (cli_command_matches(opt, "rm", NULL, 2) || cli_command_matches(opt, "rmdir", NULL, 2)) {
            status =
                zfs_remove(session->disk, opt->partition, opt->argv[1], strcmp(opt->argv[0], "rmdir") == 0, &error);
        } else if (importing) {
            const char* sources[CLI_MAX_ARGS];
            for (int i = 1; i < opt->argc - 1; ++i) {
                sources[i - 1] = opt->argv[i];
            }
            status = zfs_import_many(session->disk,
                                     opt->partition,
                                     sources,
                                     (size_t) opt->argc - 2,
                                     opt->argv[opt->argc - 1],
                                     opt->yes || opt->dry_run,
                                     &error);
            if (status == ZFS_EXISTS) {
                if (!opt->yes && cli_confirm("Overwrite existing import destination files?", false) == 0) {
                    return cli_failure(opt, 3, "confirmation", "Import overwrite requires confirmation or --yes");
                }
                status = zfs_import_many(session->disk,
                                         opt->partition,
                                         sources,
                                         (size_t) opt->argc - 2,
                                         opt->argv[opt->argc - 1],
                                         true,
                                         &error);
            }
        } else {
            return cli_failure(opt, 2, "usage", "Unknown command or incorrect arguments; use help");
        }
    }
    if (status != ZFS_OK) {
        return cli_api_failure(opt, &error);
    }
    if (session->shell) {
        cli_success(opt, "Change staged; use apply or discard");
        return 0;
    }
    const zfs_info* info = zfs_get_info(session->disk);
    cli_print_summary(opt, info, creating);
    if (opt->dry_run) {
        cli_success(opt, "Dry run completed; target unchanged");
        return 0;
    }
    if (!opt->yes && cli_confirm("Apply changes?", false) == 0) {
        return cli_failure(opt, 3, "confirmation", "Changes discarded; committing requires confirmation or --yes");
    }
    status = zfs_apply(session->disk, &error);
    if (status != ZFS_OK) {
        return cli_api_failure(opt, &error);
    }
    cli_success(opt, "Changes applied");
    return 0;
}
