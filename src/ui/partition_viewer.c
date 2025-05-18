
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"
#include "ui/statusbar.h"
#include "ui/menubar.h"
#include "ui/partition_viewer.h"
#include "ui/tinyfiledialogs.h"
#include "zealfs_v2.h"

#define MAX_PATH_LENGTH 512
#define MAX_ENTRIES     2048 // 64KB pages / 32
#define ENTRY_NAME_LEN  (NAME_MAX_LEN)
#define ENTRY_SIZE_LEN  14
#define ENTRY_TYPE_LEN  12
#define ENTRY_DATE_LEN  16

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
    void* disk_fd;
    /* Entries for the current view */
    zealfs_entry_t entries_raw[MAX_ENTRIES];
    partition_entry_t entries[MAX_ENTRIES];
    int entries_count;
} partition_viewer_t;


static partition_viewer_t m_part_ctx = {
    .address_bar = { '/', 0 }
};


static inline int chars_width_px(int n)
{
    const int font_size = 8;
    return n * font_size;
}


static ssize_t partition_viewer_read(void* arg, void* buffer, uint32_t addr, size_t len)
{
    partition_viewer_t* fs_ctx = (partition_viewer_t*) arg;
    const off_t disk_offset = (off_t) fs_ctx->partition->start_lba * DISK_SECTOR_SIZE + addr;
    /* Read the sector from the disk */
    return disk_read(fs_ctx->disk_fd, buffer, disk_offset, len);
}


static ssize_t partition_viewer_write(void* arg, const void* buffer, uint32_t addr, size_t len)
{
    partition_viewer_t* fs_ctx = (partition_viewer_t*) arg;

    const off_t disk_offset = (off_t) fs_ctx->partition->start_lba * DISK_SECTOR_SIZE + addr;
    /* Read the sector from the disk */
    return disk_write(fs_ctx->disk_fd, buffer, disk_offset, len);
}


static zealfs_context_t zealfs_ctx = {
    .read     = partition_viewer_read,
    .write    = partition_viewer_write,
    .arg      = &m_part_ctx,
};


static void remove_trailing_slash(char* path)
{
    if (strcmp(path, "/") != 0) {
        size_t len = strlen(path);
        if (len > 0 && path[len - 1] == '/') {
            path[len - 1] = '\0';
        }
    }
}


static int read_directory(char* path)
{
    zealfs_fd_t fd;

    /* Remove trailing `/` if path is not `/` */
    remove_trailing_slash(path);

    /* We have to create a context/arg for zealfs functions */
    int ret = zealfs_opendir(path, &zealfs_ctx, &fd);
    if (ret) {
        printf("[VIEWER] Could not open directory %s: %s\n", path, strerror(-ret));
        return ret;
    }

    /* Browse the root directory */
    const int filled_entries = zealfs_readdir(&zealfs_ctx, &fd, m_part_ctx.entries_raw, MAX_ENTRIES);
    m_part_ctx.entries_count = filled_entries;

    for (int i = 0; i < filled_entries; i++) {
        char tmp[64];

        partition_entry_t* entry = &m_part_ctx.entries[i];
        zealfs_entry_t* entry_raw = &m_part_ctx.entries_raw[i];

        const int is_dir = m_part_ctx.entries_raw[i].flags & 1;
        snprintf(entry->name, ENTRY_NAME_LEN, "%s%c", entry_raw->name, is_dir ? '/' : '\0');
        snprintf(entry->size, ENTRY_SIZE_LEN, "%u", entry_raw->size);
        snprintf(entry->type, ENTRY_TYPE_LEN, "%s", is_dir ? "Directory" : "File");
        const uint8_t year_hi = from_bcd(entry_raw->year[0]);
        const uint8_t year_lo = from_bcd(entry_raw->year[1]);
        const uint8_t month  = from_bcd(entry_raw->month);
        const uint8_t day    = from_bcd(entry_raw->day);
        const uint8_t hour   = from_bcd(entry_raw->hours);
        const uint8_t minute = from_bcd(entry_raw->minutes);
        const uint8_t second = from_bcd(entry_raw->seconds);

        snprintf(tmp, sizeof(tmp), "%02u%02u-%02u-%02u %02u:%02u:%02u",
                 year_hi, year_lo, month, day, hour, minute, second);
        memcpy(entry->date, tmp, ENTRY_DATE_LEN);
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

    refresh_directory();
}


static void partition_viewer_clear(void)
{
    if (m_part_ctx.partition != NULL)
    {
        m_part_ctx.address_bar[0] = '/';
        m_part_ctx.address_bar[1] = 0;
        m_part_ctx.entries_count = 0;
        m_part_ctx.selected_file = 0;
        m_part_ctx.partition = NULL;
        disk_close(m_part_ctx.disk_fd);
        m_part_ctx.disk_fd = NULL;
    }
}


/**
 * @brief Parse a newly opened partition
 */
static void partition_viewer_parse(disk_info_t* disk, partition_t* part)
{
    partition_viewer_clear();
    m_part_ctx.partition = part;

    if (disk == NULL || part == NULL) {
        return;
    } else {
        zealfs_destroy(&zealfs_ctx);
    }

    int ret = disk_open(disk, &m_part_ctx.disk_fd);
    if (ret) {
        printf("[VIEWER] Could not open disk\n");
        return;
    }

    refresh_directory();
}


static void change_directory(const char* directory)
{
    char path[MAX_PATH_LENGTH];
    snprintf(path, MAX_PATH_LENGTH, "%s%s", m_part_ctx.address_bar, directory);
    if (read_directory(path) == 0) {
        /* Success */
        snprintf(m_part_ctx.address_bar, MAX_PATH_LENGTH, "%s/", path);
    }
}


static void create_directory(void)
{
    const char* folder_name = tinyfd_inputBox("New Folder", "Enter folder name (max 16 characters):", "");
    if (folder_name && strlen(folder_name) > 0 && strlen(folder_name) <= ENTRY_NAME_LEN) {
        char path[MAX_PATH_LENGTH];
        snprintf(path, MAX_PATH_LENGTH, "%s%s", m_part_ctx.address_bar, folder_name);
        int ret = zealfs_mkdir(path, &zealfs_ctx, NULL);
        if (ret == 0) {
            ui_statusbar_printf("Folder '%s' created successfully.\n", folder_name);
            refresh_directory();
        } else {
            ui_statusbar_printf("Failed to create folder '%s': %s\n", folder_name, strerror(-ret));
        }
    } else if (folder_name) {
        ui_statusbar_print("Invalid folder name. Must be 1-16 characters long.");
    }
}


static void delete_entry(void)
{
    char path[MAX_PATH_LENGTH];

    if (m_part_ctx.entries_count == 0) {
        return;
    }

    char* name = m_part_ctx.entries[m_part_ctx.selected_file].name;
    snprintf(path, MAX_PATH_LENGTH, "%s%s", m_part_ctx.address_bar, name);
    remove_trailing_slash(path);

    if (m_part_ctx.entries_raw[m_part_ctx.selected_file].flags & 1) {
        int ret = zealfs_rmdir(path, &zealfs_ctx);
        if (ret == 0) {
            ui_statusbar_printf("Directory '%s' deleted.\n", name);
            refresh_directory();
        } else {
            ui_statusbar_printf("Failed to delete directory '%s': %s\n", name, strerror(-ret));
        }
    } else {
        int ret = zealfs_unlink(path, &zealfs_ctx);
        if (ret == 0) {
            ui_statusbar_printf("File '%s' deleted successfully.\n", name);
            refresh_directory();
        } else {
            ui_statusbar_printf("Failed to delete file '%s': %s\n", name, strerror(-ret));
        }
    }
}


static void extract_selected_file(void)
{
    uint8_t buffer[4096];
    size_t bytes_read = 0;
    size_t total_bytes_written = 0;
    char path[MAX_PATH_LENGTH];
    zealfs_fd_t fd;

    if (m_part_ctx.entries_count <= 0) {
        return;
    }
    /* At the moment, only extarct files, not directories */
    if (m_part_ctx.entries_raw[m_part_ctx.selected_file].flags & 1) {
        ui_statusbar_print("Only files can be extracted!");
        return;
    }

    char* filename = m_part_ctx.entries[m_part_ctx.selected_file].name;
    /* Where to save the file */
    char* destination = tinyfd_saveFileDialog("Choose a destination",
                                              filename, 0,
                                              NULL, NULL);
    if (destination == NULL) {
        /* Abort since the dialog was closed */
        return;
    }
    ui_statusbar_printf("Extracting to %s...\n", destination);

    /* Determine the file that is selected */
    snprintf(path, sizeof(path), "%s%s", m_part_ctx.address_bar, filename);
    int ret = zealfs_open(path, &zealfs_ctx, &fd);
    if (ret < 0) {
        ui_statusbar_printf("Could not extract file %s: %s\n", filename, strerror(ret));
        return;
    }

    /* Create the destination file */
    FILE* dest_file = fopen(destination, "wb");
    if (!dest_file) {
        ui_statusbar_printf("Could not open destination file %s\n", destination);
        return;
    }

    /* Write the file chunk by chunk */
    while(1) {
        bytes_read = zealfs_read(&zealfs_ctx, &fd, buffer, sizeof(buffer), total_bytes_written);
        if (bytes_read <= 0) {
            break;
        }
        size_t bytes_written = fwrite(buffer, 1, bytes_read, dest_file);
        if (bytes_written != bytes_read) {
            ui_statusbar_printf("Error writing to destination file %s\n", destination);
            fclose(dest_file);
            return;
        }
        total_bytes_written += bytes_written;
    }

    if (bytes_read < 0) {
        ui_statusbar_printf("Error reading file %s from partition\n", filename);
    } else {
        ui_statusbar_printf("File extracted successfully (%zu bytes)\n", total_bytes_written);
    }

    fclose(dest_file);
}


static void import_file(void)
{
    char path[MAX_PATH_LENGTH];
    uint8_t buffer[4096];
    size_t bytes_read = 0;
    size_t total_bytes_written = 0;
    zealfs_fd_t fd;

    const char* file_path = tinyfd_openFileDialog("Select a file to import", "", 0, NULL, NULL, 0);
    if (!file_path) {
        /* Operation cancelled */
        return;
    }

    FILE* src_file = fopen(file_path, "rb");
    if (!src_file) {
        ui_statusbar_printf("Could not open file %s\n", file_path);
        return;
    }

    /* Check if the file is bigger than the remaining space in the partition */
    fseek(src_file, 0, SEEK_END);
    size_t file_size = ftell(src_file);
    fseek(src_file, 0, SEEK_SET);
    if (file_size > zealfs_free_space(&zealfs_ctx)) {
        ui_statusbar_print("Not enough space in the partition to import the file.");
        fclose(src_file);
        return;
    }

    /* Check if the file name complies with the FS restrictions */
    const char* filename = strrchr(file_path, '/');
    filename = filename ? filename + 1 : file_path;
    if (strlen(filename) > ENTRY_NAME_LEN) {
        filename = tinyfd_inputBox("Rename File", "File name is too long. Enter a new name (max 16 characters):", "");
        if (!filename || strlen(filename) == 0 || strlen(filename) > ENTRY_NAME_LEN) {
            ui_statusbar_print("Invalid file name.");
            fclose(src_file);
            return;
        }
    }

    /* Filename is correct, generate the absolute path and create it! */
    snprintf(path, MAX_PATH_LENGTH, "%s%s", m_part_ctx.address_bar, filename);
    int ret = zealfs_create(path, &zealfs_ctx, &fd);
    if (ret < 0) {
        ui_statusbar_printf("Failed to create file %s: %s\n", filename, strerror(-ret));
        fclose(src_file);
        return;
    }
    while (1) {
        bytes_read = fread(buffer, 1, sizeof(buffer), src_file);
        if (bytes_read <= 0) {
            break;
        }
        size_t bytes_written = zealfs_write(&zealfs_ctx, &fd, buffer, bytes_read, total_bytes_written);
        if (bytes_written != bytes_read) {
            ui_statusbar_printf("Error writing to file %s in partition\n", filename);
            fclose(src_file);
            return;
        }
        total_bytes_written += bytes_written;
    }

    /* Flush the changes on the disk */
    int err = zealfs_flush(&zealfs_ctx, &fd);
    if (err) {
        ui_statusbar_printf("Error flushing file %s\n", filename);
    }

    fclose(src_file);

    ui_statusbar_printf("File %s imported (%zu bytes)\n", filename, total_bytes_written);
    refresh_directory();
}


int ui_partition_viewer_get_partition_usage_percentage(uint64_t* free_bytes, uint64_t* total_bytes)
{
    /* If the current selected partition is not a valid ZealFS partition, return 0% */
    if (m_part_ctx.partition == NULL) {
        return 0;
    }

    uint64_t free_space = zealfs_free_space(&zealfs_ctx);
    if (free_bytes) {
        *free_bytes = free_space;
    }

    uint64_t size_bytes = m_part_ctx.partition->size_sectors * DISK_SECTOR_SIZE;
    /* If the partition starts at 0, it means that the disk has no MBR, instead of taking the
     * whole disk as the partition size, use the number of bytes in the bitmap */
    if (m_part_ctx.partition->start_lba == 0) {
        size_bytes = zealfs_total_space(&zealfs_ctx);
    }
    if (total_bytes) {
        *total_bytes = size_bytes;
    }
    return 100 - ((free_space * 100ULL) /  size_bytes);
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


int ui_partition_viewer(struct nk_context *ctx, disk_info_t* disk, int partition_idx, struct nk_rect bounds)
{
    static char user_address_bar[MAX_PATH_LENGTH];

    /* Check if we just switched partitions */
    partition_t* part = NULL;
    if (partition_idx != -1 && disk_is_valid_zealfs_partition(&disk->partitions[partition_idx])) {
        part = &disk->partitions[partition_idx];
    }

    if (part != m_part_ctx.partition) {
        partition_viewer_parse(disk, part);
        strncpy(user_address_bar, m_part_ctx.address_bar, MAX_PATH_LENGTH);
    }

    if (nk_begin(ctx, "Partition viewer", bounds, NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_BORDER | NK_WINDOW_TITLE))
    {
        if (part == NULL) {
            nk_layout_row_dynamic(ctx, 30, 1);
            nk_label_wrap(ctx, "Please select a ZealFS partition to manage its content.\n"
                               "The disk must not have any pending operation.");
            goto window_end;
        }


        nk_layout_row_dynamic(ctx, 30, 4);
        if (nk_button_label(ctx, "Export")) {
            extract_selected_file();
        }
        if (nk_button_label(ctx, "Import")) {
            import_file();
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
            strncpy(user_address_bar, m_part_ctx.address_bar, MAX_PATH_LENGTH);
        }

        nk_flags flags = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER,
                                user_address_bar, MAX_PATH_LENGTH,
                                nk_filter_default);
        if (nk_button_label(ctx, "Go") || (flags & NK_EDIT_COMMITED)) {
            if (read_directory(user_address_bar) == 0) {
                strncpy(m_part_ctx.address_bar, user_address_bar, MAX_PATH_LENGTH);
            } else {
                ui_statusbar_printf("Invalid path %s\n", user_address_bar);
            }
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
                nk_selectable_text(ctx, m_part_ctx.entries[i].name, ENTRY_NAME_LEN, NK_TEXT_LEFT, &selected);
                nk_selectable_text(ctx, m_part_ctx.entries[i].size, ENTRY_SIZE_LEN, NK_TEXT_RIGHT, &selected);
                nk_selectable_label(ctx, "   ", NK_TEXT_LEFT, &selected);
                nk_selectable_text(ctx, m_part_ctx.entries[i].type, ENTRY_TYPE_LEN, NK_TEXT_LEFT, &selected);
                nk_selectable_text(ctx, m_part_ctx.entries[i].date, ENTRY_DATE_LEN, NK_TEXT_LEFT, &selected);
                if (selected) {
                    m_part_ctx.selected_file = i;
                }

                bool clicked = nk_input_mouse_clicked(&ctx->input, NK_BUTTON_LEFT, bounds);
                if (clicked) {
                    /* Try to detect a double-click on the same item in the view */
                    static double last_click_time = 0.0;
                    static int last_item_clicked = 0;
                    const uint32_t now = GetTime();
                    const double elapsed = now - last_click_time;
                    if (last_item_clicked == m_part_ctx.selected_file && elapsed < 0.4)
                    {
                        if (m_part_ctx.entries_raw[i].flags & 1) {
                            change_directory(m_part_ctx.entries[i].name);
                            strncpy(user_address_bar, m_part_ctx.address_bar, MAX_PATH_LENGTH);
                        } else {
                            extract_selected_file();
                        }
                    }
                    last_click_time = now;
                    last_item_clicked = m_part_ctx.selected_file;
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