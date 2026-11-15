/**
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cli.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool size_value(const char* text, uint64_t* value)
{
    if (text == NULL || isdigit((unsigned char) *text) == 0) {
        return false;
    }
    errno = 0;
    char* end;
    unsigned long long number = strtoull(text, &end, 10);
    if (errno != 0) {
        return false;
    }
    uint64_t scale = 1;
    if (strcmp(end, "KiB") == 0) {
        scale = 1024;
    } else if (strcmp(end, "MiB") == 0) {
        scale = UINT64_C(1024) * 1024;
    } else if (strcmp(end, "GiB") == 0) {
        scale = UINT64_C(1024) * 1024 * 1024;
    } else if (*end != 0 && strcmp(end, "B") != 0) {
        return false;
    }
    if (number > UINT64_MAX / scale) {
        return false;
    }
    *value = (uint64_t) number * scale;
    return true;
}

static bool parse(int argc, char** argv, cli_options* opt, char* error, size_t capacity, bool shell)
{
    bool short_target = false;
    memset(opt, 0, sizeof(*opt));
    opt->image = true;
    opt->source_image = true;
    opt->source_partition = -1;
    opt->alignment = 1024 * 1024;
    for (int i = 0; i < argc; ++i) {
        if (opt->argc >= CLI_MAX_ARGS) {
            snprintf(error, capacity, "Too many arguments");
            return false;
        }
        const char* arg = argv[i];
        if (strcmp(arg, "--json") == 0) {
            opt->json = true;
        } else if (strcmp(arg, "--yes") == 0) {
            opt->yes = true;
        } else if (strcmp(arg, "--dry-run") == 0) {
            opt->dry_run = true;
        } else if (strcmp(arg, "--mbr") == 0) {
            opt->mbr = true;
        } else if (strcmp(arg, "--image") == 0 || strcmp(arg, "-i") == 0 || strcmp(arg, "--device") == 0 ||
                   strcmp(arg, "-d") == 0) {
            if (opt->target != NULL || i + 1 >= argc) {
                snprintf(error, capacity, "Choose exactly one --image (-i) PATH or --device (-d) PATH");
                return false;
            }
            opt->image = strcmp(arg, "--image") == 0 || strcmp(arg, "-i") == 0;
            short_target = strcmp(arg, "-i") == 0 || strcmp(arg, "-d") == 0;
            opt->target = argv[++i];
        } else if (strcmp(arg, "--from-image") == 0 || strcmp(arg, "-f") == 0 || strcmp(arg, "--from-device") == 0) {
            if (opt->source != NULL || i + 1 >= argc) {
                snprintf(error, capacity, "Choose exactly one --from-image PATH or --from-device PATH");
                return false;
            }
            opt->source_image = strcmp(arg, "--from-image") == 0 || strcmp(arg, "-f") == 0;
            opt->source = argv[++i];
        } else if (strcmp(arg, "--backup") == 0 || strcmp(arg, "-b") == 0) {
            if (opt->backup != NULL || i + 1 >= argc) {
                snprintf(error, capacity, "Specify one --backup PATH");
                return false;
            }
            opt->backup = argv[++i];
        } else if (strcmp(arg, "--size") == 0 || strcmp(arg, "--alignment") == 0 ||
                   (strcmp(arg, "--partition") == 0 || strcmp(arg, "--from-partition") == 0)) {
            /* Share numeric parsing for sizes, alignment and partition indices;
             * then validate each option's units/range before narrowing its type. */
            uint64_t value;
            if (i + 1 >= argc || !size_value(argv[++i], &value)) {
                snprintf(error, capacity, "Invalid value for %s", arg);
                return false;
            }
            if (strcmp(arg, "--size") == 0) {
                opt->size = value;
                opt->size_set = true;
            } else if (strcmp(arg, "--alignment") == 0) {
                if (value > UINT32_MAX || value < DISK_SECTOR_SIZE || (value & (value - 1)) != 0) {
                    snprintf(error, capacity, "Invalid alignment");
                    return false;
                }
                opt->alignment = (uint32_t) value;
            } else {
                if (value < 1 || value > ZFS_MAX_PARTITIONS) {
                    snprintf(error, capacity, "Partition must be 1–4");
                    return false;
                }
                if (strcmp(arg, "--from-partition") == 0) {
                    opt->source_partition = (int) value - 1;
                } else {
                    opt->partition = (int) value - 1;
                    opt->partition_set = true;
                }
            }
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            opt->argv[opt->argc++] = "help";
        } else if (strcmp(arg, "--version") == 0) {
            opt->argv[opt->argc++] = "version";
        } else if (arg[0] == '-') {
            snprintf(error, capacity, "Unknown option: %s", arg);
            return false;
        } else {
            if (opt->argc == CLI_MAX_ARGS) {
                snprintf(error, capacity, "Too many arguments");
                return false;
            }
            opt->argv[opt->argc++] = argv[i];
        }
    }
    /* Shell merge already has a target; its short disk flags identify the source.
     * Resolve after parsing so options may appear on either side of the command. */
    if (shell && short_target && cli_command_matches(opt, "merge", NULL, 1)) {
        if (opt->source != NULL) {
            snprintf(error, capacity, "Choose exactly one merge source image or device");
            return false;
        }
        opt->source = opt->target;
        opt->source_image = opt->image;
        opt->target = NULL;
    }
    return true;
}

bool cli_parse(int argc, char** argv, cli_options* opt, char* error, size_t capacity)
{
    return parse(argc, argv, opt, error, capacity, false);
}

bool cli_parse_shell(int argc, char** argv, cli_options* opt, char* error, size_t capacity)
{
    return parse(argc, argv, opt, error, capacity, true);
}

bool cli_command_matches(const cli_options* opt, const char* command, const char* subcommand, int count)
{
    return opt->argc == count && strcmp(opt->argv[0], command) == 0 &&
           (subcommand == NULL || strcmp(opt->argv[1], subcommand) == 0);
}

/* CLI paths are always rooted in the selected filesystem. Host paths are left
 * untouched; the core continues to receive canonical absolute ZealFS paths. */
bool cli_filesystem_path(const char* input, char output[ZFS_FS_PATH_MAX])
{
    if (*input == 0) {
        return false;
    }
    while (*input == '/') {
        ++input;
    }
    size_t length = strlen(input);
    while (length != 0 && input[length - 1] == '/') {
        --length;
    }
    /* Reserve one byte for the root slash and one for the terminating NUL. */
    if (length > ZFS_FS_PATH_MAX - 2) {
        return false;
    }
    output[0] = '/';
    memcpy(output + 1, input, length);
    output[length + 1] = '\0';
    return true;
}
