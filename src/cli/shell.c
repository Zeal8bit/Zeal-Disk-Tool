/* SPDX-License-Identifier: Apache-2.0 */
#include "cli.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* In-place tokenizer: quotes group arguments; backslashes stay literal except
 * when escaping a quote inside a quoted token. No shell execution or expansion. */
static int tokenize(char *line, char **args) {
    char *read = line, *write = line;
    int count = 0;
    while (*read) {
        while (isspace((unsigned char)*read))
            ++read;
        if (!*read)
            break;
        if (count == CLI_MAX_ARGS)
            return -1;
        args[count++] = write;
        char quote = 0;
        while (*read) {
            char c = *read++;
            if (quote) {
                if (c == quote)
                    quote = 0;
                else if (c == '\\' && *read == quote)
                    *write++ = *read++;
                else
                    *write++ = c;
            } else if (c == '\'' || c == '"')
                quote = c;
            else if (isspace((unsigned char)c))
                break;
            else
                *write++ = c;
        }
        if (quote)
            return -1;
        *write++ = 0;
    }
    return count;
}
int cli_shell(cli_session *s, const cli_options *initial) {
    if (initial->json || initial->yes || initial->dry_run)
        return cli_failure(initial, 2, "usage", "Shell does not accept --json, --yes, or --dry-run");
    if (initial->target) {
        zfs_error e;
        if (zfs_open(initial->target, initial->image, &s->disk, &e))
            return cli_api_failure(initial, &e);
    }
    s->shell = true;
    bool terminal = cli_is_terminal();
    char line[8192];
    int result = 0;
    if (terminal)
        fputs("Zeal Disk Tool shell. Use help for commands; changes require apply.\n", stderr);
    for (;;) {
        if (terminal) {
            fputs("zdt> ", stderr);
            fflush(stderr);
        }
        if (!fgets(line, sizeof(line), stdin)) {
            if (s->disk && zfs_get_info(s->disk)->pending) {
                result = cli_failure(initial, 3, "confirmation", "End of input: pending changes discarded");
            }
            break;
        }
        if (!strchr(line, '\n') && !feof(stdin)) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {
            }
            result = cli_failure(initial, 2, "usage", "Input line is too long");
            continue;
        }
        char *args[CLI_MAX_ARGS];
        int argc = tokenize(line, args);
        if (argc < 0) {
            result = cli_failure(initial, 2, "usage", "Unclosed quote or too many arguments");
            continue;
        }
        if (!argc)
            continue;
        cli_options o;
        char error[256];
        if (!cli_parse(argc, args, &o, error, sizeof(error))) {
            result = cli_failure(&o, 2, "usage", error);
            continue;
        }
        if (o.json || o.dry_run || o.yes) {
            result = cli_failure(&o, 2, "usage", "Shell commands do not accept --json, --dry-run or --yes");
            continue;
        }
        if (cli_command_matches(&o, "quit", NULL, 1) || cli_command_matches(&o, "exit", NULL, 1)) {
            int r = cli_leave_disk(s, &o);
            if (!r)
                break;
            result = r;
            continue;
        }
        int r = cli_execute(s, &o);
        if (r)
            result = r;
    }
    return result;
}
