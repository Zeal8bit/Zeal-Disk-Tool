/**
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cli.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* In-place tokenizer: quotes group arguments; backslashes stay literal except
 * when escaping a quote inside a quoted token. No shell execution or expansion. */
static int tokenize(char* line, char** args)
{
    char* read = line;
    char* write = line;
    int count = 0;
    while (*read != 0) {
        while (isspace((unsigned char) *read) != 0) {
            ++read;
        }
        if (*read == 0) {
            break;
        }
        if (count == CLI_MAX_ARGS) {
            return -1;
        }
        args[count++] = write;
        char quote = 0;
        while (*read != 0) {
            char c = *read++;
            if (quote != 0) {
                if (c == quote) {
                    quote = 0;
                } else if (c == '\\' && *read == quote) {
                    *write++ = *read++;
                } else {
                    *write++ = c;
                }
            } else if (c == '\'' || c == '"') {
                quote = c;
            } else if (isspace((unsigned char) c) != 0) {
                break;
            } else {
                *write++ = c;
            }
        }
        if (quote != 0) {
            return -1;
        }
        *write++ = 0;
    }
    return count;
}

int cli_shell(cli_session* session, const cli_options* initial)
{
    if (initial->json || initial->yes || initial->dry_run) {
        return cli_failure(initial, 2, "usage", "Shell does not accept --json, --yes, or --dry-run");
    }
    if (initial->target != NULL) {
        zfs_error api_error;
        if (zfs_open(initial->target, initial->image, &session->disk, &api_error) != ZFS_OK) {
            return cli_api_failure(initial, &api_error);
        }
    }
    session->shell = true;
    bool terminal = cli_is_terminal();
    cli_history* history = calloc(1, sizeof(*history));
    if (history == NULL) {
        return cli_failure(initial, 1, "out of memory", "Cannot allocate shell history");
    }
    /* Keep terminal editing and confirmation reads on the same input stream. */
    if (terminal) {
        setvbuf(stdin, NULL, _IONBF, 0);
    }
    char line[CLI_LINE_CAPACITY];
    int result = 0;
    if (terminal) {
        fputs("Zeal Disk Tool shell. Use help for commands; changes require apply.\n", stderr);
    }
    for (;;) {
        cli_input input = cli_readline(history, line, terminal);
        if (input == CLI_INPUT_ERROR) {
            result = cli_failure(initial, 1, "input", "Cannot read shell input");
            break;
        }
        if (input == CLI_INPUT_EOF) {
            if (session->disk != NULL && zfs_get_info(session->disk)->pending) {
                result = cli_failure(initial, 3, "confirmation", "End of input: pending changes discarded");
            }
            break;
        }
        if (input == CLI_INPUT_LONG) {
            result = cli_failure(initial, 2, "usage", "Input line is too long");
            continue;
        }
        if (terminal) {
            cli_history_add(history, line);
        }
        char* args[CLI_MAX_ARGS];
        int argc = tokenize(line, args);
        if (argc < 0) {
            result = cli_failure(initial, 2, "usage", "Unclosed quote or too many arguments");
            continue;
        }
        if (argc == 0) {
            continue;
        }
        cli_options opt;
        char error[256];
        if (!cli_parse_shell(argc, args, &opt, error, sizeof(error))) {
            result = cli_failure(&opt, 2, "usage", error);
            continue;
        }
        if (opt.json || opt.dry_run || opt.yes) {
            result = cli_failure(&opt, 2, "usage", "Shell commands do not accept --json, --dry-run or --yes");
            continue;
        }
        if (cli_command_matches(&opt, "quit", NULL, 1) || cli_command_matches(&opt, "exit", NULL, 1)) {
            int command_result = cli_leave_disk(session, &opt);
            if (command_result == 0) {
                break;
            }
            result = command_result;
            continue;
        }
        int command_result = cli_execute(session, &opt);
        if (command_result != 0) {
            result = command_result;
        }
    }
    free(history);
    return result;
}
