/**
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZDT_CLI_H
#define ZDT_CLI_H
/* Private frontend types. Disk and filesystem behavior belongs to zealfs_core. */
#include "disk_tool.h"
#define CLI_MAX_ARGS 64
/* A host path can use nearly ZFS_PATH_MAX bytes; leave room for command names,
 * quoting, destination paths and multiple import sources. This bounds shell
 * input/history only; one-shot argv follows the host operating system limits. */
#define CLI_LINE_CAPACITY 8192
#define CLI_HISTORY_LIMIT 32

typedef struct {
    char entries[CLI_HISTORY_LIMIT][CLI_LINE_CAPACITY];
    size_t count;
    size_t next;
} cli_history;

typedef enum {
    CLI_INPUT_ERROR = -1,
    CLI_INPUT_EOF,
    CLI_INPUT_OK,
    CLI_INPUT_LONG
} cli_input;

cli_input cli_readline(cli_history* history, char line[CLI_LINE_CAPACITY], bool terminal);
void cli_history_add(cli_history* history, const char* line);

typedef struct {
    const char* target;
    const char* source;
    const char* backup;
    bool source_image;
    bool partition_set;
    int source_partition;
    bool image;
    bool json;
    bool yes;
    bool dry_run;
    bool mbr;
    bool size_set;
    uint64_t size;
    uint32_t alignment;
    int partition;
    int argc;
    /* Arguments borrow storage from main's argv or the current shell input line. */
    char* argv[CLI_MAX_ARGS];
} cli_options;

typedef struct {
    zfs_disk* disk;
    bool shell;
} cli_session;

/* Argument parsing and filesystem-path normalization. */
bool cli_parse(int argc, char** argv, cli_options* options, char* error, size_t capacity);
bool cli_parse_shell(int argc, char** argv, cli_options* options, char* error, size_t capacity);
bool cli_command_matches(const cli_options* options, const char* command, const char* subcommand, int count);
bool cli_filesystem_path(const char* input, char output[ZFS_FS_PATH_MAX]);

/* Context-specific help and complete text/JSON output. */
void cli_usage(void);
void cli_help(void);
void cli_shell_help(void);
int cli_failure(const cli_options* options, int exit_code, const char* code, const char* message);
int cli_api_failure(const cli_options* options, const zfs_error* error);
void cli_success(const cli_options* options, const char* message);
void cli_print_devices(const cli_options* options, const zfs_info* devices, size_t count);
void cli_print_disk(const cli_options* options, zfs_disk* disk);
void cli_print_entries(const cli_options* options, const zfs_entry* entries, size_t count);
void cli_recovery_notice(zfs_disk* disk);
int cli_transfer_report(const cli_options* options, const zfs_merge_report* report, const char* state, int exit_code,
                        const zfs_error* error);
int cli_transfer(cli_session* session, const cli_options* options, bool restore);
void cli_print_summary(const cli_options* options, const zfs_info* info, bool creating);

/* Shared session lifecycle and command execution. Main owns final disk cleanup. */
bool cli_is_terminal(void);
int cli_confirm(const char* message, bool allow_discard);
int cli_leave_disk(cli_session* session, const cli_options* options);
int cli_execute(cli_session* session, const cli_options* options);
int cli_shell(cli_session* session, const cli_options* initial);
#endif
