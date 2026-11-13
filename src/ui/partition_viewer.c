
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "raylib.h"
#include "ui/statusbar.h"
#include "ui/menubar.h"
#include "ui/partition_viewer.h"
#include "ui/tinyfiledialogs.h"
#include "zealfs.h"

#define MAX_PATH_LENGTH 512
#define MAX_ENTRIES     2048 // 64KB pages / 32
#define ENTRY_NAME_LEN ZFS_NAME_MAX
#define ENTRY_SIZE_LEN  14
#define ENTRY_TYPE_LEN  12
#define ENTRY_DATE_LEN  16

#define CHECK_RW(ret)   do { if((ret) <= 0) { return (ret); } } while(0)

typedef struct {
    char name[ENTRY_NAME_LEN + 2]; // +2 in case it's a directory, to add `/` and \0
    char size[ENTRY_SIZE_LEN];
    char type[ENTRY_TYPE_LEN];
    char date[ENTRY_DATE_LEN];
} partition_entry_t;


typedef struct {
    char address_bar[MAX_PATH_LENGTH];
    partition_t* partition;
    int  selected_file;
    /* Opened disk descriptor */
    disk_info_t* disk;
    unsigned revision;
    /* Entries for the current view */
    zfs_entry* entries_raw;
    partition_entry_t* entries;
    int entries_count;
} partition_viewer_t;


static partition_viewer_t m_part_ctx = {
    /* The address bar MUST always end with a `/` */
    .address_bar = { '/', 0 }
};


static inline int chars_width_px(int n)
{
    const int font_size = 8;
    return n * font_size;
}


static void add_trailing_slash(char* path, size_t max_size)
{
    size_t len = strlen(path);
    if (path[len - 1] != '/') {
        /* We need to add one new character, make sure the max size is nig enough */
        if (len + 1 >= max_size) {
            printf("WARNING: max path length reached!\n");
            return;
        }
        path[len] = '/';
        path[len + 1] = 0;
    }
}


static void remove_trailing_slash(char* path)
{
    if (strcmp(path, "/") != 0) {
        size_t len = strlen(path);
        if (len > 0 && path[len - 1] == '/') {
            path[len - 1] = '\0';
        }
    }
}

static int read_directory(const char* path_ro)
{
    if (!m_part_ctx.disk || !m_part_ctx.partition) {
        return -1;
    }
    char path[MAX_PATH_LENGTH + 1];
    snprintf(path, sizeof(path), "%s", path_ro);
    remove_trailing_slash(path);
    zfs_error error;
    zfs_entry* raw = NULL;
    size_t filled_entries = 0;
    int index = (int) (m_part_ctx.partition - m_part_ctx.disk->partitions);
    if (zfs_list(m_part_ctx.disk->handle, index, path, &raw, &filled_entries, &error)) {
        ui_statusbar_print(error.message);
        return -1;
    }
    partition_entry_t* entries = calloc(filled_entries ? filled_entries : 1, sizeof(*entries));
    if (!entries) {
        zfs_entries_free(raw);
        ui_statusbar_print("Not enough memory to display directory");
        return -1;
    }
    free(m_part_ctx.entries);
    zfs_entries_free(m_part_ctx.entries_raw);
    m_part_ctx.entries = entries;
    m_part_ctx.entries_raw = raw;
    m_part_ctx.entries_count = (int) filled_entries;
    if (m_part_ctx.selected_file >= (int) filled_entries) {
        m_part_ctx.selected_file = 0;
    }
    for (size_t i = 0; i < filled_entries; ++i) {
        snprintf(entries[i].name, sizeof(entries[i].name), "%s%s", raw[i].name, raw[i].directory ? "/" : "");
        snprintf(entries[i].size, sizeof(entries[i].size), "%u", raw[i].size);
        snprintf(entries[i].type, sizeof(entries[i].type), "%s", raw[i].directory ? "Directory" : "File");
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%04u-%02u-%02u %02u:%02u:%02u", raw[i].year, raw[i].month, raw[i].day, raw[i].hour,
                 raw[i].minute, raw[i].second);
        memcpy(entries[i].date, tmp, ENTRY_DATE_LEN);
    }
    return 0;
}

static inline void refresh_directory(void)
{
    read_directory(m_part_ctx.address_bar);
}


static void go_up_directory(void)
{
    remove_trailing_slash(m_part_ctx.address_bar);
    char* last_slash = strrchr(m_part_ctx.address_bar, '/');
    if (last_slash && last_slash != m_part_ctx.address_bar) {
        *last_slash = '\0'; // Remove the last directory
    } else {
        // If we are at the root, reset to "/"
        m_part_ctx.address_bar[0] = '/';
        m_part_ctx.address_bar[1] = '\0';
    }

    add_trailing_slash(m_part_ctx.address_bar, sizeof(m_part_ctx.address_bar));
    refresh_directory();
}


static void partition_viewer_clear(void)
{
    zfs_entries_free(m_part_ctx.entries_raw);
    free(m_part_ctx.entries);
    memset(&m_part_ctx, 0, sizeof(m_part_ctx));
    strcpy(m_part_ctx.address_bar, "/");
}

static void partition_viewer_parse(disk_info_t* disk, partition_t* part)
{
    partition_viewer_clear();
    m_part_ctx.disk = disk;
    m_part_ctx.partition = part;
    m_part_ctx.revision = disk ? disk->revision : 0;
    if (part) {
        refresh_directory();
    }
}

static int current_partition(void)
{
    return (int) (m_part_ctx.partition - m_part_ctx.disk->partitions);
}

static void mutation_result(zfs_status status, const zfs_error* error, const char* message)
{
    disk_sync(m_part_ctx.disk);
    m_part_ctx.revision = m_part_ctx.disk->revision;
    ui_statusbar_print(status ? error->message : message);
    refresh_directory();
}

static void change_directory(const char* directory)
{
    char path[MAX_PATH_LENGTH];
    snprintf(path, MAX_PATH_LENGTH, "%s%s", m_part_ctx.address_bar, directory);
    if (read_directory(path) == 0) {
        /* Success, the path already contains the trailing `/` since all dir entries have one */
        snprintf(m_part_ctx.address_bar, MAX_PATH_LENGTH, "%s", path);
    }
}


static void create_directory(void)
{
    const char* folder_name = tinyfd_inputBox("New Folder", "Enter folder name (max 16 bytes):", "");
    if (!folder_name) {
        return;
    }
    char path[MAX_PATH_LENGTH];
    if (snprintf(path, sizeof(path), "%s%s", m_part_ctx.address_bar, folder_name) >= (int) sizeof(path)) {
        ui_statusbar_print("Path is too long");
        return;
    }
    zfs_error error;
    zfs_status ret = zfs_mkdir(m_part_ctx.disk->handle, current_partition(), path, &error);
    mutation_result(ret, &error, "Folder creation staged");
}

static void delete_entry(void)
{
    if (m_part_ctx.entries_count <= 0) {
        return;
    }
    zfs_entry* entry = &m_part_ctx.entries_raw[m_part_ctx.selected_file];
    char path[MAX_PATH_LENGTH];
    if (snprintf(path, sizeof(path), "%s%s", m_part_ctx.address_bar, entry->name) >= (int) sizeof(path)) {
        ui_statusbar_print("Path is too long");
        return;
    }
    zfs_error error;
    zfs_status ret = zfs_remove(m_part_ctx.disk->handle, current_partition(), path, entry->directory, &error);
    mutation_result(ret, &error, "Deletion staged");
}

static void gui_progress(void* user, uint64_t done, uint64_t total)
{
    (void) user;
    disk_update_progress_bar(total ? (int) (done * 100 / total) : 100);
}
static void extract_selected_file(void)
{
    if (m_part_ctx.entries_count <= 0) {
        return;
    }
    zfs_entry* entry = &m_part_ctx.entries_raw[m_part_ctx.selected_file];
    if (entry->directory) {
        ui_statusbar_print("Only files can be extracted");
        return;
    }
    const char* destination = tinyfd_saveFileDialog("Export file", entry->name, 0, NULL, NULL);
    if (!destination) {
        return;
    }
    char path[MAX_PATH_LENGTH];
    if (snprintf(path, sizeof(path), "%s%s", m_part_ctx.address_bar, entry->name) >= (int) sizeof(path)) {
        ui_statusbar_print("Path is too long");
        return;
    }
    zfs_error error;
    disk_init_progress_bar();
    zfs_set_callbacks(m_part_ctx.disk->handle, (zfs_callbacks){.progress = gui_progress});
    zfs_status ret = zfs_export(m_part_ctx.disk->handle, current_partition(), path, destination, true, &error);
    zfs_set_callbacks(m_part_ctx.disk->handle, (zfs_callbacks){0});
    disk_destroy_progress_bar();
    ui_statusbar_print(ret ? error.message : "File exported");
}

static int import_file(const char* file_path)
{
    const char* filename = disk_get_basename(file_path);
    if (strlen(filename) > ZFS_NAME_MAX) {
        filename = tinyfd_inputBox("Rename File", "Enter a name (max 16 bytes):", "");
    }
    if (!filename || !*filename) {
        return 0;
    }
    char path[MAX_PATH_LENGTH];
    if (snprintf(path, sizeof(path), "%s%s", m_part_ctx.address_bar, filename) >= (int) sizeof(path)) {
        ui_statusbar_print("Path is too long");
        return 0;
    }
    zfs_error error;
    disk_init_progress_bar();
    zfs_set_callbacks(m_part_ctx.disk->handle, (zfs_callbacks){.progress = gui_progress});
    zfs_status ret = zfs_import(m_part_ctx.disk->handle, current_partition(), file_path, path, &error);
    zfs_set_callbacks(m_part_ctx.disk->handle, (zfs_callbacks){0});
    disk_destroy_progress_bar();
    mutation_result(ret, &error, "File import staged");
    return ret == ZFS_OK;
}

static void import_files(void)
{
    const int allow_multiselect = 1;
    const char* files_ro = tinyfd_openFileDialog("Select files to import", "", 0, NULL, NULL, allow_multiselect);
    if (files_ro == NULL) {
        /* Operation cancelled */
        return;
    }

    /* The files will be separated with `|` character */
    char *files = strdup(files_ro);
    char *current = strtok(files, "|");
    int success = 1;

    while (current != NULL) {
        success &= import_file(current);
        if (!success) {
            break;
        }
        current = strtok(NULL, "|");
    }

    if (success) {
        ui_statusbar_printf("File imports staged\n");
    }

    free(files);
    refresh_directory();
}

int ui_partition_viewer_get_partition_usage_percentage(uint64_t* free_bytes, uint64_t* total_bytes)
{
    uint64_t free_space = 0, size_bytes = 0;
    if (m_part_ctx.partition && m_part_ctx.disk) {
        zfs_usage(m_part_ctx.disk->handle, current_partition(), &free_space, &size_bytes, NULL);
    }
    if (free_bytes) {
        *free_bytes = free_space;
    }
    if (total_bytes) {
        *total_bytes = size_bytes;
    }
    return size_bytes ? (int) (100 - free_space * 100 / size_bytes) : 0;
}

static void ui_partition_viewer_show_usage(struct nk_context *ctx)
{
    char usage_info[128];
    char free_size_str[32];
    char total_size_str[32];
    uint64_t free_bytes = 0;
    uint64_t total_bytes = 0;

    int percent_used = ui_partition_viewer_get_partition_usage_percentage(&free_bytes, &total_bytes);
    nk_layout_row_dynamic(ctx, 20, 1);
    disk_get_size_str(free_bytes, free_size_str, sizeof(free_size_str));
    disk_get_size_str(total_bytes, total_size_str, sizeof(total_size_str));
    snprintf(usage_info, sizeof(usage_info), "Usage: %d%% (%s free / %s total)",
            percent_used, free_size_str, total_size_str);
    nk_label(ctx, usage_info, NK_TEXT_CENTERED);
}


static void collapse_slashes(char *path)
{
    char *src = path;
    char *dst = path;
    int prev_was_slash = 0;

    while (*src) {
        if (*src == '/') {
            if (!prev_was_slash) {
                *dst++ = '/';
                prev_was_slash = 1;
            }
        } else {
            *dst++ = *src;
            prev_was_slash = 0;
        }
        src++;
    }
    *dst = '\0';
}


/**
 * @brief Parse and try to access the directory typed by the user
 */
static int read_user_bar_directory(char path[MAX_PATH_LENGTH + 1])
{
    if (path[0] == 0) {
        /* Path is empty! */
        path[0] = '/';
        path[1] = 0;
    } else {
        /* Sanitize the path by removing the multiple slashes */
        collapse_slashes(path);
    }

    if (read_directory(path) == 0) {
        /* Both the strings must have a trailing `/` */
        add_trailing_slash(path, MAX_PATH_LENGTH + 1);
        snprintf(m_part_ctx.address_bar, MAX_PATH_LENGTH, "%s", path);
        return 1;
    }

    return 0;
}


static bool nk_item_double_clicked(struct nk_context *ctx, struct nk_rect bounds, int item_id)
{
    static int last_item_clicked = -1;
    static double last_click_time = 0.0;
    static bool last_pressed = false;

    bool pressed = nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT) &&
                   nk_input_is_mouse_hovering_rect(&ctx->input, bounds);
    bool double_clicked = false;

    /* Edge detection */
    if (pressed && !last_pressed) {
        /* The current item has a mouse-pressed event, and didn't have it a frame ago */
        double now = GetTime();
        double elapsed = now - last_click_time;

        /* If the previous mouse-pressed event was on the same element and within 400ms, it's a double-click! */
        if (last_item_clicked == item_id && elapsed < 0.4) {
            double_clicked = true;
        }

        last_click_time = now;
        last_item_clicked = item_id;
    }

    last_pressed = pressed;
    return double_clicked;
}

int ui_partition_viewer(struct nk_context *ctx, disk_info_t* disk, int partition_idx, struct nk_rect bounds)
{
    static char user_address_bar[MAX_PATH_LENGTH + 1];

    /* Check if we just switched partitions */
    partition_t* part = NULL;
    if (disk && partition_idx >= 0 && partition_idx < 4 &&
        disk_is_valid_zealfs_partition(&disk->partitions[partition_idx])) {
        part = &disk->partitions[partition_idx];
    }

    if (part != m_part_ctx.partition || disk != m_part_ctx.disk || (disk && disk->revision != m_part_ctx.revision)) {
        partition_viewer_parse(disk, part);
        snprintf(user_address_bar, MAX_PATH_LENGTH, "%s", m_part_ctx.address_bar);
    }

    if (nk_begin(ctx, "Partition viewer", bounds, NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_BORDER | NK_WINDOW_TITLE))
    {
        if (part == NULL) {
            nk_layout_row_dynamic(ctx, 30, 1);
            nk_label_wrap(ctx, "Please select a ZealFS partition to manage its content.\n"
                               "All changes remain staged until Apply.");
            goto window_end;
        }


        nk_layout_row_dynamic(ctx, 30, 4);
        if (nk_button_label(ctx, "Export")) {
            extract_selected_file();
        }
        if (nk_button_label(ctx, "Import")) {
            import_files();
        }
        if (nk_button_label(ctx, "New dir")) {
            create_directory();
        }
        if (nk_button_label(ctx, "Delete")) {
            delete_entry();
        }

        nk_layout_row(ctx, NK_DYNAMIC, 30, 3, (float[]){0.1f, 0.7f, 0.19f});
        if (nk_button_label(ctx, "Up")) {
            go_up_directory();
            snprintf(user_address_bar, MAX_PATH_LENGTH, "%s", m_part_ctx.address_bar);
        }

        nk_flags flags = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER,
                                user_address_bar, MAX_PATH_LENGTH,
                                nk_filter_default);
        if ((nk_button_label(ctx, "Go") || (flags & NK_EDIT_COMMITED)) &&
             !read_user_bar_directory(user_address_bar))
        {
            ui_statusbar_printf("Invalid path %s\n", user_address_bar);
        }

        struct nk_rect bounds = nk_window_get_content_region(ctx);
        float remaining_height = bounds.h - (nk_widget_bounds(ctx).y - bounds.y) - 25;
        nk_layout_row_dynamic(ctx, remaining_height, 1);

        if (nk_group_begin(ctx, "EntriesList", NK_WINDOW_BORDER)) {

            /* Assign a minimum width to each of th fields below */
            nk_layout_row_template_begin(ctx, 20);
            nk_layout_row_template_push_variable(ctx, chars_width_px(16));
            nk_layout_row_template_push_variable(ctx, chars_width_px(ENTRY_SIZE_LEN));
            nk_layout_row_template_push_variable(ctx, chars_width_px(2));
            nk_layout_row_template_push_variable(ctx, chars_width_px(ENTRY_TYPE_LEN));
            nk_layout_row_template_push_variable(ctx, chars_width_px(ENTRY_DATE_LEN));
            nk_layout_row_template_end(ctx);
            /* Remove the small gap that exists between each element of a single row, this will help with making the
             * selection of a whole row being of the same color. */
            nk_style_push_vec2(ctx, &ctx->style.window.spacing, nk_vec2(0, 0));
            /* Make the header a bit darker */
            nk_style_push_color(ctx, &ctx->style.window.background, nk_rgba(30, 30, 30, 255)); // Set a darker label background color
            nk_label(ctx, "Name", NK_TEXT_LEFT);
            nk_label(ctx, "Size (bytes)", NK_TEXT_RIGHT);
            nk_label(ctx, " ", NK_TEXT_RIGHT); // Padding
            nk_label(ctx, "Type", NK_TEXT_LEFT);
            nk_label(ctx, "Date", NK_TEXT_LEFT);
            nk_style_pop_color(ctx);

            /* Tried to make all the names background in a darker color */
            // struct nk_command_buffer* canvas = nk_window_get_canvas(ctx);
            // struct nk_rect rect = nk_layout_widget_bounds(ctx);
            // rect.w = chars_width_px(16);
            // rect.h = remaining_height;
            // nk_fill_rect(canvas, rect, 0, nk_rgba(0x28, 0x28, 0x28, 255));
            struct nk_rect group_bounds = nk_window_get_content_region(ctx);

            for (int i = 0; i < m_part_ctx.entries_count; i++) {
                /* In order to check for a double-click, we need to retrieve the bounds of the next widget,
                 * but since we want the whole line to be clickable, we need to modify the width. */
                struct nk_rect bounds = nk_widget_bounds(ctx);
                bounds.w = group_bounds.w;

                nk_bool selected = m_part_ctx.selected_file == i;
                nk_selectable_text(ctx, m_part_ctx.entries[i].name, (int) strlen(m_part_ctx.entries[i].name),
                                   NK_TEXT_LEFT, &selected);
                nk_selectable_text(ctx, m_part_ctx.entries[i].size, ENTRY_SIZE_LEN, NK_TEXT_RIGHT, &selected);
                nk_selectable_label(ctx, "   ", NK_TEXT_LEFT, &selected);
                nk_selectable_text(ctx, m_part_ctx.entries[i].type, ENTRY_TYPE_LEN, NK_TEXT_LEFT, &selected);
                nk_selectable_text(ctx, m_part_ctx.entries[i].date, ENTRY_DATE_LEN, NK_TEXT_LEFT, &selected);
                if (selected) {
                    m_part_ctx.selected_file = i;
                }

                if (nk_item_double_clicked(ctx, bounds, i)) {
                    if (m_part_ctx.entries_raw[i].directory) {
                        change_directory(m_part_ctx.entries[i].name);
                        snprintf(user_address_bar, MAX_PATH_LENGTH, "%s", m_part_ctx.address_bar);
                    } else {
                        extract_selected_file();
                    }
                }
            }
            nk_style_pop_vec2(ctx);
            nk_group_end(ctx);
        }

        ui_partition_viewer_show_usage(ctx);
    }

window_end:
    nk_end(ctx);
    return 0;
}


void ui_partition_viewer_clear(struct nk_context *ctx)
{
    partition_viewer_clear();
}