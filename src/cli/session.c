/**
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cli.h"
#include <stdio.h>
#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

bool cli_is_terminal(void)
{
    return isatty(fileno(stdin)) != 0;
}

int cli_confirm(const char* message, bool allow_discard)
{
    if (!cli_is_terminal()) {
        return 0;
    }
    fprintf(stderr, "%s %s ", message, allow_discard ? "[a]pply/[d]iscard/[c]ancel:" : "[y/N]:");
    fflush(stderr);
    char line[64];
    if (!fgets(line, sizeof(line), stdin)) {
        return 0;
    }
    if (allow_discard) {
        return line[0] == 'a' || line[0] == 'A' ? 1 : line[0] == 'd' || line[0] == 'D' ? 2 : 0;
    }
    return line[0] == 'y' || line[0] == 'Y' ? 1 : 0;
}

int cli_leave_disk(cli_session* s, const cli_options* o)
{
    if (!s->disk) {
        return 0;
    }
    const zfs_info* info = zfs_get_info(s->disk);
    if (info->pending) {
        char message[ZFS_PATH_MAX + 80];
        snprintf(message, sizeof(message), "Pending changes on %s.", info->path);
        int answer = cli_confirm(message, true);
        if (!answer) {
            return cli_failure(o, 3, "confirmation", "Apply or discard pending changes before leaving");
        }
        if (answer == 1) {
            zfs_error e;
            if (zfs_apply(s->disk, &e)) {
                cli_recovery_notice(s->disk);
                return cli_api_failure(o, &e);
            }
        }
    }
    zfs_close(s->disk);
    s->disk = NULL;
    return 0;
}
