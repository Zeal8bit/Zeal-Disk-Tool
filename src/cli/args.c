/* SPDX-License-Identifier: Apache-2.0 */
#include "cli.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool size_value(const char *text, uint64_t *value) {
    if (!text || !isdigit((unsigned char)*text))
        return false;
    errno = 0;
    char *end;
    unsigned long long n = strtoull(text, &end, 10);
    if (errno)
        return false;
    uint64_t scale = 1;
    if (!strcmp(end, "KiB"))
        scale = 1024;
    else if (!strcmp(end, "MiB"))
        scale = UINT64_C(1024) * 1024;
    else if (!strcmp(end, "GiB"))
        scale = UINT64_C(1024) * 1024 * 1024;
    else if (*end && strcmp(end, "B"))
        return false;
    if (n > UINT64_MAX / scale)
        return false;
    *value = (uint64_t)n * scale;
    return true;
}
bool cli_parse(int argc, char **argv, cli_options *o, char *error, size_t capacity) {
    memset(o, 0, sizeof(*o));
    o->image = true;
    o->alignment = 1024 * 1024;
    for (int i = 0; i < argc; ++i) {
        if (o->argc >= CLI_MAX_ARGS) {
            snprintf(error, capacity, "Too many arguments");
            return false;
        }
        const char *arg = argv[i];
        if (!strcmp(arg, "--json"))
            o->json = true;
        else if (!strcmp(arg, "--yes"))
            o->yes = true;
        else if (!strcmp(arg, "--dry-run"))
            o->dry_run = true;
        else if (!strcmp(arg, "--mbr"))
            o->mbr = true;
        else if (!strcmp(arg, "--image") || !strcmp(arg, "-i") || !strcmp(arg, "--device") ||
                 !strcmp(arg, "-d")) {
            if (o->target || i + 1 >= argc) {
                snprintf(error, capacity, "Choose exactly one --image (-i) PATH or --device (-d) PATH");
                return false;
            }
            o->image = !strcmp(arg, "--image") || !strcmp(arg, "-i");
            o->target = argv[++i];
        } else if (!strcmp(arg, "--size") || !strcmp(arg, "--alignment") || !strcmp(arg, "--partition")) {
            uint64_t n;
            if (i + 1 >= argc || !size_value(argv[++i], &n)) {
                snprintf(error, capacity, "Invalid value for %s", arg);
                return false;
            }
            if (!strcmp(arg, "--size")) {
                o->size = n;
                o->size_set = true;
            } else if (!strcmp(arg, "--alignment")) {
                if (n > UINT32_MAX || n < 512 || (n & (n - 1))) {
                    snprintf(error, capacity, "Invalid alignment");
                    return false;
                }
                o->alignment = (uint32_t)n;
            } else {
                if (n < 1 || n > 4) {
                    snprintf(error, capacity, "Partition must be 1–4");
                    return false;
                }
                o->partition = (int)n - 1;
            }
        } else if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            o->argv[o->argc++] = "help";
        } else if (!strcmp(arg, "--version")) {
            o->argv[o->argc++] = "version";
        } else if (arg[0] == '-') {
            snprintf(error, capacity, "Unknown option: %s", arg);
            return false;
        } else {
            if (o->argc == CLI_MAX_ARGS) {
                snprintf(error, capacity, "Too many arguments");
                return false;
            }
            o->argv[o->argc++] = argv[i];
        }
    }
    return true;
}
bool cli_command_matches(const cli_options *o, const char *a, const char *b, int count) {
    return o->argc == count && !strcmp(o->argv[0], a) && (!b || !strcmp(o->argv[1], b));
}
/* CLI paths are always rooted in the selected filesystem. Host paths are left
 * untouched; the core continues to receive canonical absolute ZealFS paths. */
bool cli_filesystem_path(const char *input, char output[512]) {
    if (!*input)
        return false;
    while (*input == '/')
        ++input;
    size_t length = strlen(input);
    while (length && input[length - 1] == '/')
        --length;
    if (length > 510)
        return false;
    output[0] = '/';
    memcpy(output + 1, input, length);
    output[length + 1] = '\0';
    return true;
}
