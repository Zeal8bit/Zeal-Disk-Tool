/**
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cli.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

static int cli_main(int argc, char** argv)
{
    if (argc == 1) {
        cli_usage();
        puts("\nRun 'zdt --help' for more details.");
        return 0;
    }
    cli_options opt;
    char error[256];
    if (!cli_parse(argc - 1, argv + 1, &opt, error, sizeof(error))) {
        return cli_failure(&opt, 2, "usage", error);
    }
    cli_session session = {0};
    int result;
    if (cli_command_matches(&opt, "shell", NULL, 1)) {
        result = cli_shell(&session, &opt);
    } else {
        result = cli_execute(&session, &opt);
    }
    zfs_close(session.disk);
    return result;
}

#ifdef _WIN32
int main(void)
{
    int argc = 0;
    wchar_t** wide_args = CommandLineToArgvW(GetCommandLineW(), &argc);
    char** args = wide_args != NULL ? calloc((size_t) argc + 1, sizeof(*args)) : NULL;
    bool valid = args != NULL;
    for (int i = 0; valid && i < argc; ++i) {
        int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_args[i], -1, NULL, 0, NULL, NULL);
        args[i] = length != 0 ? malloc((size_t) length) : NULL;
        valid = args[i] != NULL &&
                WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_args[i], -1, args[i], length, NULL, NULL) != 0;
    }
    int result;
    if (valid) {
        result = cli_main(argc, args);
    } else {
        fputs("zdt: cannot decode command line\n", stderr);
        result = 1;
    }
    if (args != NULL) {
        for (int i = 0; i < argc; ++i) {
            free(args[i]);
        }
        free(args);
    }
    if (wide_args != NULL) {
        LocalFree(wide_args);
    }
    return result;
}
#else
int main(int argc, char** argv)
{
    return cli_main(argc, argv);
}
#endif
