/* SPDX-License-Identifier: Apache-2.0 */
#include "cli.h"
#include "app_version.h"
#include <stdio.h>

void cli_usage(void) {
    puts("Usage: zdt [options] COMMAND [arguments]\n"
         "       zdt [--image PATH | --device PATH] shell");
}
void cli_help(void) {
    puts("zdt " VERSION " — ZealFS v2 disk and image manager");
    cli_usage();
    puts("\nOptions:\n"
         "  -i, --image PATH               Disk image target\n"
         "  -d, --device PATH              Physical device target\n"
         "  --partition N                  Partition 1–4 (default 1)\n"
         "  --size SIZE                    Bytes or KiB/MiB/GiB (integer)\n"
         "  --alignment SIZE               Power of two >=512 (default 1MiB)\n"
         "  --mbr                          Initialize a new image with an MBR\n"
         "  --yes                          Apply changes without prompting\n"
         "  --dry-run                      Stage/validate, then discard without committing\n"
         "  --json                         JSON output\n\n"
         "Commands:\n"
         "  devices                        List eligible physical disks (<=64GiB)\n"
         "  info | status                  Disk, partitions, usage and pending state\n"
         "  image create PATH --size SIZE [--mbr]\n"
         "  mbr create                     Stage an MBR (no active raw partition)\n"
         "  partition list | create | format | delete\n"
         "                                 Create uses --size (default largest free extent)\n"
         "  ls [PATH]                      List a directory (default /)\n"
         "  mkdir PATH | rm PATH | rmdir PATH\n"
         "  import HOST_PATH FS_PATH       Stage a file import (no overwrite)\n"
         "  export FS_PATH HOST_PATH       Export a file, including staged contents\n"
         "  shell                          Interactive session; commands above plus:\n"
         "    open --image PATH | --device PATH\n"
         "    apply | discard | help | quit\n\n"
         "Names may be up to 16 bytes. Raw images use\n"
         "partition 1 and start at byte 0. Changes remain staged until Apply.\n"
         "Exit: 0 success, 1 operation error, 2 usage, 3 confirmation.\n"
         "Shell input supports single/double quoted arguments; no expansion.");
}
void cli_shell_help(void) {
    puts("Shell commands:\n"
         "  open -i PATH | -d PATH          Open an image or physical device\n"
         "  devices                        List available physical disks\n"
         "  info | status                  Show the target and pending changes\n"
         "  image create PATH --size SIZE [--mbr]\n"
         "  mbr create                     Stage a new MBR\n"
         "  partition list | create | format | delete\n"
         "  ls [PATH]                      List a directory\n"
         "  mkdir PATH | rm PATH | rmdir PATH\n"
         "  import HOST_PATH FS_PATH        Stage a file import\n"
         "  export FS_PATH HOST_PATH        Export a file\n"
         "  apply                          Save pending changes\n"
         "  discard                        Cancel pending changes\n"
         "  version                        Show the version\n"
         "  help                           Show this reference\n"
         "  quit | exit                    Leave the shell\n\n"
         "Command options:\n"
         "  --partition N                  Partition 1–4 (default 1)\n"
         "  --size SIZE                    Image or partition size\n"
         "  --alignment SIZE               Partition alignment\n\n"
         "Disk changes remain staged until apply.");
}
