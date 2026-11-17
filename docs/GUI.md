# Using the GUI

[README](../README.md) · [CLI guide](CLI.md) · [ZealFS concepts](ZEALFS.md) · [Disk operations](DISKS.md) · [Build/install](BUILD.md)

The GUI is a single-window FLTK application with a native title bar, a classic
menu bar, a disk sidebar, a partition map, a details/action panel and an
embedded file browser.
Start `zeal_disk_tool` (`zeal_disk_tool.exe` on Windows), or open the macOS
`ZealDiskTool.app` package. A source build can launch one or more images
directly:

```sh
./build/default/zeal_disk_tool disk.img
./build/default/zeal_disk_tool disk.img other.img
```

Startup image arguments must have an `.img` extension. `zeal_disk_tool -h`
prints its startup options; Windows also supports `-g` to allocate a console
window. See [build and installation](BUILD.md) for binaries and dependencies,
and [physical device access](DISKS.md#physical-device-access) if working with a
card rather than an image.

![Zeal Disk Tool](../md_imgs/zeal_disk_tool_screen1.png)

## Window layout

- **Menu bar** — **File**, **Disk**, **Edit** and **Help**, using the native
  window frame for moving, resizing and minimizing.
- **File** — open or create an image, apply or discard pending changes, quit.
- **Disk** — refresh devices, create a partition or MBR, format, delete or
  inspect the selected partition.
- **Edit** — import, export, create folders and delete entries in the file
  browser, plus focus and search shortcuts.
- **Help** — keyboard shortcuts and the About box.
- **Manage / Files tabs** — switch between the two views, with **Manage** on
  the left as the default. Manage shows the partition map, details and actions
  while hiding the browsers; **Files** focuses on the host and disk file
  browsers and hides the partition map and details card. `Ctrl+1` and `Ctrl+2`,
  or the **View** menu, do the same.
- **Heading** — the selected disk's name, plus the partition in the Files view
  and the size/layout/slot usage in the Manage view, with the **Apply changes**
  button on the right. The button is enabled while that disk has pending changes
  and writes them, like **File → Apply Changes** or `Ctrl+S`.
- **Disks sidebar** — every discovered disk and its partitions. Clicking a disk
  header shows that disk's overview in the Manage view (and picks its first
  usable partition in the Files view), while clicking a partition row selects
  it; the disk header is highlighted only in the overview. The **+** button
  opens a menu for new partitions, images and MBR tables. Disks with unsaved
  changes show an amber dot.
- **Disk map** — proportional coloured segments for the selected disk; click a
  segment to select its partition.
- **Partition details** — filesystem, total/used/free space, start address,
  slot, type byte and staged state, with **Format**, **Delete** and
  **Properties** actions. In the disk overview it instead lists every partition
  of the disk (click a row to select it) with the disk's size, layout and slot
  usage.
- **File browser** — two side-by-side panes: the host computer on the left and
  the selected ZealFS partition on the right. Each pane has breadcrumbs, a
  sortable file list, its own search field and multi-select; the host pane adds
  quick places and host file operations. **To disk** copies the host selection
  into the current disk folder, **To host** copies the disk selection into the
  current host folder, and dragging rows between the panes does the same. The
  disk pane is inactive while the disk overview is shown.
- **Status bar** — the latest operation result and the unsaved-changes badge.

## Open an image or select a card

Use the sidebar **+** button, **File → Open Image...**, or `Ctrl+O`. Accessible
physical disks appear in the sidebar automatically; after inserting a card use
**Disk → Refresh Devices** or `F5`. Apply or cancel pending changes before
refreshing or switching targets. If a card is missing,
follow the shared
[device-discovery troubleshooting](DISKS.md#a-card-is-missing-from-the-device-list).

Click a disk header in the sidebar to open that disk's overview in the Manage
view: the map still shows all of its partitions and the details card lists them,
but no partition is selected. In the Files view the same click picks the disk's
first usable partition so the browser has files to show. Click a
partition row in the sidebar, a row in the overview or a map segment to select
it. Opening or refreshing a disk selects its first usable partition. GUI
partition labels are 0–3; see the
[CLI number mapping](ZEALFS.md#disk-partition-and-filesystem) when switching
between interfaces.

## Create an image and partition

1. Open **File → Create Image...** (or `Ctrl+N`).
2. Set **Location** directly or with **Browse...**, and select a **Size**
   preset.
3. Choose **Table: MBR** for a partitioned image, or **None** for a raw
   filesystem image. See
   [disk layouts](ZEALFS.md#disk-partition-and-filesystem) before choosing.
4. Click **Create**. The image is staged and selected; its host file is not
   created yet.
5. Choose **Disk → New Partition...**, or use `Ctrl+P`.
6. Set the **Size** with the field or slider and choose its unit (KiB or MiB).
   Choose the **Alignment** (512 bytes or 1 MiB) and review the **Address**
   where the partition will start.
7. Click **Create**. The new partition is formatted as ZealFS v2 and can be
   selected and browsed before saving.
8. Repeat steps 5–7 for up to four partitions, then press **Apply** and confirm
   to publish the image and its contents.

The new partition dialog starts with the largest free block and shows the
resulting address. Partition slots, capacity and alignment follow the
[shared ZealFS limits](ZEALFS.md#capacity-and-allocation). Creating another
partition does not resize or move an existing one; up to four partitions can be
staged before Apply.

**Disk → Create MBR** stages an MBR partition table on an eligible target
without one. It does not convert an existing raw filesystem into a partitioned
disk.

## Browse and manage files

The **Files** tab shows the file browser full-height, without the partition map
or details card. It is split into a host pane on the left and a disk pane on the
right, so files can be copied between them without leaving the window. Selecting
another partition reloads the disk pane at its root directory; the host pane
keeps its current folder. Switch to **Manage** when you need the partition map,
sizes or Format/Delete actions.

- The host pane starts in your home directory. Its **Places** list jumps to
  **Home**, **Desktop**, **Downloads**, **Documents**, the filesystem root and,
  on Windows, the available drives.
- Double-click a folder in either pane to open it; double-click a file to copy
  it to the other pane.
- Use the breadcrumb chips or the back/up/home buttons to navigate. The disk
  pane keeps its folder tree; click a folder to jump to it and its chevron to
  collapse or expand it.
- Click a column header (**Name**, **Size**, **Type**, **Modified**) to sort;
  click it again to reverse the order. Both panes sort independently.
- Type in a pane's search field to filter that folder; clear the text or press
  `Esc`.
- Click a row to select it. `Ctrl`-click (`Command`-click on macOS) toggles
  entries, `Shift`-click selects a range, and `Ctrl+A` selects every entry in
  the current folder.

### Copy files between the host and the disk

Select files or folders in one pane and press its transfer button: **To disk**
in the host pane or **To host** in the disk pane. You can also drag the selected
rows onto the other pane; the target pane is outlined while a drop is possible.
Folders are copied recursively and keep their structure.

Host-to-disk copies are staged like other disk changes, so review them and press
**Apply changes** when ready. A host filename longer than 16 bytes, or one that
already exists on the disk, is skipped and reported in the status bar. Disk-to-
host copies write the host files immediately and can read staged disk contents.
If a later item fails in a multi-file copy, earlier items remain copied; check
the status bar.

The host pane's **New folder**, **Rename** and **Delete** act on the host
filesystem immediately and cannot be undone. `F2` renames the selected host
entry.

### Import with the file dialog

**Edit → Import Files...** still opens a file chooser and imports host files into
the current disk folder; this is useful when the host pane is not pointing at
the files you want. If a host filename exceeds 16 bytes, a dialog asks for a
shorter name; the host file itself is not renamed. Imports are staged, and
matching destination filenames are refused rather than replaced.

You can also drag files or folders from the desktop file manager and drop them
on the disk pane; the pane is outlined while a drop is possible. Dropped folders
are imported recursively; a folder that already exists is merged. File
drag-and-drop depends on the desktop's native drag support and is not available
from every file manager.

### Export with the file dialog

**Edit → Export Selection...** exports the disk selection through a save or
folder dialog, without copying it into the host pane's current folder. Export
writes the host files immediately and does not need Apply. Selecting a folder
exports its contents recursively.

The distinction between an export and a raw recovery image is explained in
[copies and backups](DISKS.md#import-export-and-backups). For CLI export of the
whole filesystem, see [CLI export](CLI.md#export-files-and-directories).

### Create or delete entries

Use **Edit → New Folder...** (or the **New folder** button), enter a name within
the 16-byte limit, and confirm. The
directory is created inside the directory currently shown and remains staged.

Select a file or empty directory and use **Edit → Delete...** (or the **Delete**
button); confirm the prompt to stage its removal. A nonempty directory cannot be
deleted recursively. The file browser's **Delete** is separate from **Disk →
Delete Partition...** and the partition **Delete** action in the details panel.

## Format or delete a partition

Select the target partition in the sidebar or map.

- **Format** in the details panel (or **Disk → Format Partition...**) stages a
  fresh ZealFS v2 filesystem in that partition, replacing its old filesystem
  and files when applied.
- **Delete** (**Disk → Delete Partition...**) stages removal of the selected
  partition.

**Properties** opens a summary of the disk and selected partition. Inspect the
selection before Apply. **File → Discard Changes** drops all pending changes for
that target, including file and partition edits made in the same session.

## Save, discard and quit

An asterisk (`*`) in the window title, the amber dot in the sidebar and the
**Unsaved changes** badge indicate pending changes.

| Control | Action |
| --- | --- |
| **File → Apply Changes** / `Ctrl+S` | Ask for confirmation, then write all pending changes for the selected disk |
| **File → Discard Changes** | Ask whether to discard all pending changes for the selected disk |
| **File → Quit** / window close / `Ctrl+Q` | For each target with pending work, offer Apply, Discard or Keep editing |

Cancelling an operation's dialog only dismisses that dialog. **Cancel** discards
the target's entire pending set. Discarding a newly created image removes its
unpublished session without creating a host file.

See [Apply and discard](DISKS.md#apply-and-discard) for how image replacement
differs from writing a physical card. Errors and operation results appear in the
status bar, with dialogs for some operations. After a physical write failure,
close and reopen the target before attempting recovery.

## Light and dark appearance

**View → Appearance** offers three choices: **Light**, **Dark** and **Follow
System**. The last one is the default and is re-read every time it is selected;
the explicit choices ignore the desktop. The selection is saved in
`zeal-disk-tool/appearance` under the user configuration directory
(`$XDG_CONFIG_HOME` or `~/.config` on Linux, `~/Library/Application Support` on
macOS, `%APPDATA%` on Windows) and applied at startup, before the window is
created.

FLTK 1.4 has no notion of a system appearance (see [fltk/fltk issue
#870](https://github.com/fltk/fltk/issues/870)), so the application resolves the
preference itself and recolours everything, including the stock FLTK widgets:

| Platform | Source of the "Follow System" value |
| --- | --- |
| Linux | `GTK_THEME` and `KDE_COLOR_SCHEME` when set, otherwise `org.gnome.desktop.interface color-scheme` |
| macOS | the `AppleInterfaceStyle` user default |
| Windows | `AppsUseLightTheme` under `Themes\Personalize` in `HKCU` |

When the preference cannot be read, the light palette is used. Because FLTK
draws its own widgets, a dark desktop does not automatically give a dark
application: the palette has to exist, which is what this menu selects.

## Keyboard shortcuts

| Shortcut | Action |
| --- | --- |
| `Ctrl+O` | Open a disk image |
| `Ctrl+N` | Create a disk image |
| `Ctrl+P` | Create a partition |
| `Ctrl+I` | Import host files into the current folder |
| `Ctrl+E` | Export the selected file or folder |
| `Ctrl+B` | Focus the file browser |
| `Ctrl+F` | Focus the search field |
| `Ctrl+1` | Show the Files view |
| `Ctrl+2` | Show the Manage view |
| `F2` | Rename the selected host entry |
| `Ctrl+A` | Select every entry in the current folder |
| `Shift+Up` / `Shift+Down` | Extend the file selection |
| `Ctrl`-click / `Shift`-click | Toggle an entry / select a range |
| `F5` | Refresh devices |
| `Delete` | Delete the selected entries in the file browser, or the selected partition elsewhere |
| `Ctrl+S` | Apply pending changes |
| `Ctrl+Q` | Quit |

On macOS, FLTK maps the command shortcuts to Command.

## Application icon

The window uses the native title bar. Its 32×32 icon is embedded in the binary
and set as `_NET_WM_ICON`; the window class is `zeal-disk-tool`, matching
`packages/appdir/zeal-disk-tool.desktop`. Desktop shells use the `.desktop`
entry and icon-theme artwork for the dock, Alt-Tab and window lists. Packaged
builds provide both. For a local build, install
`packages/appdir/zeal-disk-tool.desktop` and the icons from `packages/icons/`
with an `Exec` line pointing at the built binary to make the dock icon appear.

## Updating existing software

The GUI can import individual new files. Updating matching files while
preserving personal files, creating raw partition backups during that update,
and restoring those backups are available through
[CLI merge and restore](CLI.md#merge-updates-from-an-image-or-device). Resolve
pending GUI changes and close its target session before accessing the same
target through another process.
