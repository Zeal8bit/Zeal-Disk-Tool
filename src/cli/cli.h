/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZDT_CLI_H
#define ZDT_CLI_H
/* Private frontend types. Disk and filesystem behavior belongs to zealfs_core. */
#include "zealfs.h"
#define CLI_MAX_ARGS 64

typedef struct {
    const char *target;
    bool image, json, yes, dry_run, mbr, size_set;
    uint64_t size;
    uint32_t alignment;
    int partition;
    int argc;
    /* Arguments borrow storage from main's argv or the current shell input line. */
    char *argv[CLI_MAX_ARGS];
} cli_options;
typedef struct {
    zfs_disk *disk;
    bool shell;
} cli_session;

/* Argument parsing and filesystem-path normalization. */
bool cli_parse(int argc, char **argv, cli_options *options, char *error, size_t capacity);
bool cli_command_matches(const cli_options *options, const char *command, const char *subcommand, int count);
bool cli_filesystem_path(const char *input, char output[512]);

/* Context-specific help and complete text/JSON output. */
void cli_usage(void);
void cli_help(void);
void cli_shell_help(void);
int cli_failure(const cli_options *options, int exit_code, const char *code, const char *message);
int cli_api_failure(const cli_options *options, const zfs_error *error);
void cli_success(const cli_options *options, const char *message);
void cli_print_devices(const cli_options *options, const zfs_info *devices, size_t count);
void cli_print_disk(const cli_options *options, zfs_disk *disk);
void cli_print_entries(const cli_options *options, const zfs_entry *entries, size_t count);
void cli_print_summary(const cli_options *options, const zfs_info *info, bool creating);

/* Shared session lifecycle and command execution. Main owns final disk cleanup. */
bool cli_is_terminal(void);
int cli_confirm(const char *message, bool allow_discard);
int cli_leave_disk(cli_session *session, const cli_options *options);
int cli_execute(cli_session *session, const cli_options *options);
int cli_shell(cli_session *session, const cli_options *initial);
#endif
