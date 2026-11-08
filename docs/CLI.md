# Using the zdt CLI

[README](../README.md) · [GUI guide](GUI.md) · [ZealFS concepts](ZEALFS.md) · [Disk operations](DISKS.md) · [Build/install](BUILD.md)

`zdt` manages ZealFS images and physical disks from a terminal. It runs as a single command or as an interactive shell. On Windows the executable is `zdt.exe`; PowerShell examples may need `./zdt.exe` or `.\zdt.exe` rather than a name on `PATH`.

```text
zdt [options] COMMAND [arguments]
zdt [-i IMAGE | -d DEVICE] shell
```

Run `zdt` for a usage summary, `zdt --help` for full help, or `zdt --version` for the build version. The commands below assume the binary is on `PATH`; a source build can use `./build/default/zdt` instead. See [device access](DISKS.md#physical-device-access) before working with a TF card.

## Quick start with an image

These commands create an MBR image, add a formatted ZealFS partition and import one file. Each mutation asks before applying its changes.

```sh
zdt image create disk.img --size 16MiB --mbr
zdt -i disk.img partition create --size 8MiB
zdt -i disk.img mkdir programs
zdt -i disk.img import ./hello.bin programs/hello.bin
zdt -i disk.img ls programs/
zdt -i disk.img export programs/hello.bin ./hello-copy.bin
```

Omit `--mbr` when creating a [raw filesystem image](ZEALFS.md#disk-partition-and-filesystem). Create its partition before importing files. Image creation refuses to overwrite an existing host file.

## Targets and options

In a one-shot command, `-i`/`--image` selects the target image and `-d`/`--device` selects the target physical disk. Choose one. Options can appear before or after the command.

```sh
zdt -i disk.img info
zdt -d /dev/rdisk4 --partition 1 ls /
zdt -i disk.img partition format --partition 1 --dry-run
```

| Option | Meaning |
| --- | --- |
| `-i, --image PATH` | Target image; also used by shell `open` |
| `-d, --device PATH` | Target whole physical disk; also used by shell `open` |
| `--partition N` | Target partition 1–4; defaults to 1 for ordinary commands |
| `--size SIZE` | Image size or requested new partition size |
| `--alignment SIZE` | New partition alignment: a power of two, at least 512 bytes; default 1 MiB |
| `--mbr` | Create a new image with an MBR |
| `--yes` | Accept one-shot confirmations, including merge without a backup |
| `--dry-run` | Validate a proposed operation without committing it or exporting host files |
| `--json` | Machine-readable one-shot output |
| `-f, --from-image PATH` | Merge source image, or raw backup for restore |
| `--from-device PATH` | Merge source physical disk |
| `--from-partition N` | Source partition 1–4 for merge |
| `-b, --backup PATH` | Optional new raw partition backup for merge |

Merge/restore automatically select the sole ZealFS target partition; merge also auto-selects its sole ZealFS source partition. Use explicit partition options when a side has multiple ZealFS partitions. A raw filesystem uses partition 1. The GUI labels those same slots differently; see the [partition-number mapping](ZEALFS.md#disk-partition-and-filesystem).

### Paths and sizes

Filesystem paths start at the selected filesystem's root. `games`, `games/`, `/games` and `/games/` select the same directory. There is no current directory, `cd` command or drive-letter prefix such as `t:`. `ls` without an argument lists `/`. Names and full paths follow the [ZealFS limits](ZEALFS.md#names-and-paths).

Host paths retain their normal OS meaning and may be relative to the host process's working directory. Quote paths containing spaces:

```sh
zdt -i "my disk.img" import "host files/demo program.bin" games/demo.bin
```

Sizes are integers in bytes, optionally followed by `B`, `KiB`, `MiB` or `GiB`. For example, `8192`, `8KiB` and `16MiB` are accepted; fractional sizes such as `1.5MiB` are not.

## Command reference

| Command | Use |
| --- | --- |
| `devices` | Discover accessible devices within the supported limits; no target required |
| `info` or `status` | Show target metadata, partitions, usage and pending state |
| `image create PATH --size SIZE [--mbr]` | Create a new image; no existing target required |
| `mbr create` | Create an MBR on a target without an active raw partition |
| `partition list` | Show partition information |
| `partition create [--size SIZE] [--alignment SIZE]` | Create and format a ZealFS partition in an available slot |
| `partition format [--partition N]` | Format an existing partition as ZealFS v2 |
| `partition delete [--partition N]` | Remove the selected partition |
| `ls [FS_PATH]` | List filesystem entries |
| `mkdir FS_PATH` | Create a directory; its parent must exist |
| `rm FS_PATH` | Delete a file |
| `rmdir FS_PATH` | Delete an empty directory |
| `import HOST_PATH FS_PATH` | Copy one host file into a new filesystem file |
| `export FS_PATH HOST_PATH` | Export a file or a directory's contents to the host |
| `merge` | Update the target from a source image/device; see below |
| `restore` | Replace a partition from its raw backup; see below |
| `shell` | Start an interactive session |

Partition creation without `--size` uses the largest available extent, capped at 4 GiB. A raw partition starts at byte 0. Existing partitions are not resized or moved. Nonempty directory deletion and recursive host-directory import are not supported.

### Confirmation and dry runs

A one-shot target mutation prepares and validates its changes, then asks before Apply. `--yes` accepts the confirmation. `--dry-run` validates and discards the proposal. Without a terminal, use one of those flags for operations that would otherwise need approval.

```sh
zdt -i disk.img partition delete --partition 1 --dry-run
zdt -i disk.img import ./demo.bin programs/demo.bin --yes
```

Reads and exports do not require Apply. Export asks only when replacing existing host files. A dry-run export validates source contents and destination compatibility without writing host files/directories. More on [staging and immediate host writes](DISKS.md#apply-and-discard).

## Interactive shell

```text
zdt -i disk.img shell
zdt> mkdir tools
zdt> import "host file.bin" tools/program.bin
zdt> ls tools/
zdt> status
zdt> apply
zdt> quit
```

You can also start `zdt shell` without a target, then use `open -i disk.img` or `open -d DEVICE`. The shell is a command prompt for Zeal Disk Tool, not an operating-system shell: it does not run other programs or expand variables, `~`, wildcards or substitutions. It accepts single/double quoted arguments; a backslash escapes the matching quote inside a quoted token, and is otherwise literal.

Shell commands use the open target. The commands above are available, plus:

| Command/key | Use |
| --- | --- |
| `open -i PATH` / `open -d PATH` | Switch to an image or physical device |
| `apply` | Write all pending changes for the open target |
| `discard` | Remove all pending target changes |
| `help` | Show the shell-specific reference |
| `version` | Show the build version |
| `quit` / `exit` | Leave the shell |
| Up / Down | Recall the latest 32 nonempty commands; Down past the newest restores the unfinished input |
| Left / Right, Home / End, Backspace / Delete | Edit the current input, including a recalled command |
| Ctrl-U / Ctrl-C | Clear the input / cancel the current input line |
| Ctrl-D on an empty line | End input |

History stays in memory and is cleared when the shell exits. Command lines are limited to 8190 bytes in the interactive editor. Piped input remains supported; history navigation requires an interactive terminal.

Target mutations remain staged until `apply`. `open` and `quit` ask how to resolve pending changes: apply, discard or cancel. End-of-input discards pending changes and returns exit code 3. `--partition N` applies to the individual shell command; it does not set a new default. `--yes`, `--json` and `--dry-run` are not accepted in shell mode or its commands.

### Source flags in shell merge

Because a shell merge already has its target, its short flags select the source:

```text
zdt> merge -i community.img -b before.img
zdt> apply
```

| Context | `-i PATH` | `-d PATH` |
| --- | --- | --- |
| One-shot invocation or shell `open` | Target image | Target device |
| Shell `merge` | Source image (`--from-image`) | Source device (`--from-device`) |

`merge -f PATH` remains an alias for a source image. `--from-image` and `--from-device` work in either invocation style. Shell `restore` takes `-f`/`--from-image`; it does not reuse merge's `-i` shortcut.

## Export files and directories

For a file, `HOST_PATH` is its destination filename. For a directory, `HOST_PATH` receives that directory's **contents**, recursively, including empty directories. The source directory's own name is not added automatically.

```text
zdt> export / /Users/higginsd/Downloads/tf/exports
zdt> export games/ ./exports/games
```

The first command exports the entire selected filesystem. If the source contains `/games/demo.bin`, the destination contains `exports/games/demo.bin`. The second copies `/games/demo.bin` to `./exports/games/demo.bin` too, because `./exports/games` was explicitly chosen as its destination. Missing host directories are created.

Existing unrelated host files remain. Export validates the source tree and destination conflicts before writing. Existing matching files require overwrite confirmation (`--yes` in one-shot mode). File/directory mismatches, destination links and unsafe host names are refused, even with `--yes`. Names that collide under the host-name checks are also refused. Each file is published individually; an I/O failure can leave completed files and directories from the export. A filesystem export is not a [raw partition backup](DISKS.md#import-export-and-backups).

## Merge updates from an image or device

Use merge to install updated community software while preserving personal files that exist only on the target.

| Source and target entry | Result |
| --- | --- |
| Source-only file or directory | Add it |
| Matching directories | Merge their contents recursively |
| Matching files with different bytes | Replace with source contents and timestamp |
| Byte-identical files | Leave the target entry unchanged |
| Target-only file or directory | Preserve it |
| File on one side, directory on the other | Abort the entire merge and report the conflicts |

Merge preserves target geometry, the MBR and unselected partitions. Source and target capacities may differ, provided the merged contents fit. Source and target must be distinct, and the target must have no pending changes. Space and type-conflict checks happen before backup creation or target writes.

With an accessible, unmounted card selected:

```sh
# Preview the update
zdt -d /dev/rdisk4 merge -f tf.img --dry-run --json

# Save the original partition, then apply the update
zdt -d /dev/rdisk4 merge -f tf.img -b before.img --yes --json

# Use another physical disk as the source
zdt -d /dev/rdisk4 merge --from-device /dev/rdisk5 -b before.img
```

See [physical device access](DISKS.md#physical-device-access) for permissions and how to identify your actual card. In a shell, use `merge -i tf.img -b before.img` or `merge -d SOURCE_DEVICE -b before.img`, followed by `apply`.

### Backups and approval

`-b`/`--backup` is optional. When supplied, it creates a new raw backup of the selected target partition before staging the merge. The backup includes unused partition capacity, is flushed and byte-verified, and never replaces an existing host file. It excludes the MBR and other partitions. Allow space for that backup, staging, and a full replacement image when the target is an image file.

Without a backup path, merge asks **Proceed without a backup?** and defaults to No. In one-shot mode, `--yes` explicitly accepts this choice too. Dry runs and merges with no changes neither prompt nor create a backup.

The **Proposed merge** counts shown before confirmation describe the prepared update; they do not mean target data has been written. In the shell, a requested backup is created immediately when the merge is staged, while the target remains unchanged until `apply`. `discard` cancels the staged merge but retains the backup file.

## Restore a partition backup

Restore replaces the entire selected partition from a valid raw ZealFS backup of **exactly the same capacity**, removing files added after that backup. It leaves the MBR and other partitions intact.

```sh
zdt -d /dev/rdisk4 restore -f before.img --partition 1 --yes
```

Inside the shell:

```text
zdt> restore --from-image before.img --partition 1
zdt> apply
```

Restore does not create a new backup and does not accept `--backup` or `--from-partition`. A full MBR disk image or an exported host directory is not a raw partition backup. A raw target uses partition 1.

After a physical write failure, close and reopen the target before restoring. A damaged ZealFS header can be restored this way if the target partition can still be identified; damaged MBR metadata needs separate recovery. There is no automatic rollback. Keep the backup after a failed update. [Shared disk behavior](DISKS.md) explains in-place writes and session protection.

## Automation, JSON and exit status

Use one-shot commands for scripts, with `--yes` when approving writes or `--dry-run` for a preview. `--json` sends structured results to stdout; prompts, summaries and diagnostics go to stderr.

```sh
zdt -i user.img merge -f community.img -b before.img --yes --json > merge-result.json
```

A successful result has `{"ok":true,"data":...}`. A failure has `{"ok":false,"error":{"code":"...","message":"..."}}`; transfer failures also include `data` with the operation report. Info returns disk metadata; directory listings and discovery return arrays.

Merge/restore reports include:

| Fields | Meaning |
| --- | --- |
| `operation`, `state` | Merge/restore and preview, unchanged, staged, applied, cancelled or failure state |
| `source`, `target`, `source_partition`, `target_partition` | Paths and partition identities |
| `files_added`, `files_replaced`, `files_unchanged`, `files_preserved` | File counts |
| `directories_created`, `directories_merged`, `directories_preserved` | Directory counts |
| `bytes_copied`, `backup_bytes` | Transfer and partition-backup capacity counters |
| `backup_path`, `backup_created` | Requested/recovery path and whether this operation created a backup; path is empty when absent |
| `noop`, `staged`, `applied` | Operation outcome flags |
| `conflicts` | File/directory type-conflict paths |

| Exit code | Meaning |
| --- | --- |
| 0 | Success, including a successful preview or unchanged merge |
| 1 | Filesystem, permission or I/O failure |
| 2 | Invalid command or arguments |
| 3 | Confirmation required/declined, or pending shell work discarded at EOF |

The shell retains a nonzero result from an earlier failed command even if later commands succeed. Use individual one-shot results for automation. Physical-device commands must run on the host with native `zdt` and device permissions; a ZDE host script can invoke the same commands outside its Docker container. Merge consumes a local image/device; it does not download software images.
