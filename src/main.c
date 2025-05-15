/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include "app_version.h"
#include "app_icon.h"
#include "raylib.h"
#include "nuklear.h"
#include "raylib-nuklear.h"
#include "disk.h"

#include "ui.h"
#include "ui/popup.h"
#include "ui/menubar.h"
#include "ui/statusbar.h"
#include "ui/partition_viewer.h"
#include "ui/tinyfiledialogs.h"

static struct nk_context *ctx;

int winWidth, winHeight;


static struct nk_color get_partition_color(int i)
{
    switch (i) {
        case 0:  return nk_rgb(0x4f, 0xad, 0x4f); break;
        case 1:  return nk_rgb(0x39, 0x5b, 0x7e); break;
        case 2:  return nk_rgb(0x9f, 0x62, 0xb6); break;
        case 3:  return nk_rgb(0xc9, 0x4b, 0x24); break;
        default: return nk_rgb(200, 200, 200); break;
    }
}


static void draw_dashed_rect(struct nk_context *ctx, struct nk_rect rect,
                             struct nk_color color, float thickness,
                             float dash_length, float space_length) {
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

    float x0 = rect.x;
    float y0 = rect.y;
    float x1 = rect.x + rect.w;
    float y1 = rect.y + rect.h;
    for (float x = x0; x < x1; x += dash_length + space_length) {
        float end = NK_MIN(x + dash_length, x1);
        nk_stroke_line(canvas, x, y0, end, y0, thickness, color); // top
        nk_stroke_line(canvas, x, y1, end, y1, thickness, color); // bottom
    }
    for (float y = y0; y < y1; y += dash_length + space_length) {
        float end = NK_MIN(y + dash_length, y1);
        nk_stroke_line(canvas, x0, y, x0, end, thickness, color); // left
        nk_stroke_line(canvas, x1, y, x1, end, thickness, color); // right
    }
}


static void ui_draw_disk(struct nk_context *ctx, const disk_info_t *disk, int* selected_part) {
    nk_layout_row_dynamic(ctx, 100, 1);
    struct nk_rect bounds = nk_widget_bounds(ctx);
    /* Prevent the window fom overflowing */
    bounds.w *= 0.99;

    float full_width = bounds.w;
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

    nk_fill_rect(canvas, bounds, 0, nk_rgb(220, 220, 220));

    if (disk == NULL || !disk->valid) {
        return;
    }

    const uint64_t total_sectors = disk->size_bytes / DISK_SECTOR_SIZE;
    for (int i = 0; i < MAX_PART_COUNT; ++i) {
        const partition_t *p = &disk->staged_partitions[i];
        if (!p->active || p->size_sectors == 0) {
            continue;
        }

        float start_frac = (float)p->start_lba / (float)total_sectors;
        float size_frac = (float)p->size_sectors / (float)total_sectors;

        struct nk_rect part_rect = nk_rect(
            bounds.x + full_width * start_frac,
            bounds.y,
            MAX(full_width * size_frac, 10),
            bounds.h
        );

        struct nk_color part_color = get_partition_color(i);

        /* Always fill the background of partitions in white */
        nk_fill_rect(canvas, part_rect, 0, NK_WHITE);
        /* Check how empty/full it is, this is only valid for ZealFS partitions */
        if (*selected_part == i) {
            int percentage = ui_partition_viewer_get_partition_usage_percentage(NULL, NULL);
            if (percentage > 0) {
                struct nk_rect filled_rect = part_rect;
                filled_rect.w = part_rect.w * ((float) percentage / 100.f);
                nk_fill_rect(canvas, filled_rect, 0, NK_SELECTED);
            }
        }
        /* Draw the border of the partition */
        const float outer_border = 5.0f;
        nk_stroke_rect(canvas, part_rect, 0, outer_border, part_color);
        /* For the selected partition, add dashed border on top of it */
        if (*selected_part == i) {
            const float stroke_thick = 3.0f;
            const float stroke_size = 7.8f;
            const float stroke_space = 4.8f;
            struct nk_rect dotted_part_rect = part_rect;
            dotted_part_rect.x += 3.0f;
            dotted_part_rect.y += 3.0f;
            dotted_part_rect.w -= 5.0f;
            dotted_part_rect.h -= 5.0f;
            draw_dashed_rect(ctx, dotted_part_rect, NK_SELECTED, stroke_thick, stroke_size, stroke_space);
        }

        char label[128];
        snprintf(label, sizeof(label), "Part. %d", i);

        /* Measure text size */
        float text_width = ctx->style.font->width(ctx->style.font->userdata, ctx->style.font->height, label, strlen(label));
        float text_height = ctx->style.font->height;

        /* Draw text centered if there is enough space (not counting the borders) */
        if (text_width < part_rect.w - 10) {
            /* Compute centered position */
            const float label_x = part_rect.x + (part_rect.w - text_width) / 2.0f;
            const float label_y = part_rect.y + (part_rect.h - text_height) / 2.0f;
            nk_draw_text(canvas, nk_rect(label_x, label_y, text_width, text_height),
                label, strlen(label), ctx->style.font, NK_BLACK, NK_BLACK);
        }
    }

    // Draw table header
    const float ratios[] = {
        0.04f,  // Color
        0.05f,  // Padding
        0.15f,  // Number
        0.20f,  // File System
        0.15f,  // Start address
        0.15f,  // Size
        0.25f,  // Padding
    };

    // Draw table header
    nk_layout_row(ctx, NK_DYNAMIC, 25, 7, ratios);
    nk_label(ctx, "Color",              NK_TEXT_CENTERED);
    nk_label(ctx, " ",                  NK_TEXT_LEFT);
    nk_label(ctx, "Partition",          NK_TEXT_LEFT);
    nk_label(ctx, "File System (Type)", NK_TEXT_LEFT);
    nk_label(ctx, "Start address",      NK_TEXT_LEFT);
    nk_label(ctx, "Size",               NK_TEXT_CENTERED);
    nk_label(ctx, " ",                  NK_TEXT_LEFT);

    /* Make all the elements' background transparent in the list */
    nk_style_push_color(ctx, &ctx->style.selectable.normal.data.color, NK_TRANSPARENT);
    nk_style_push_color(ctx, &ctx->style.selectable.hover.data.color,  NK_TRANSPARENT);
    nk_style_push_color(ctx, &ctx->style.selectable.pressed.data.color,  NK_TRANSPARENT);
    nk_style_push_color(ctx, &ctx->style.selectable.normal_active.data.color, NK_TRANSPARENT);
    nk_style_push_color(ctx, &ctx->style.selectable.hover_active.data.color,  NK_TRANSPARENT);
    nk_style_push_color(ctx, &ctx->style.selectable.pressed_active.data.color,  NK_TRANSPARENT);

    for (int i = 0; i < MAX_PART_COUNT; i++) {
        char buffer[256];
        const partition_t* part = &disk->staged_partitions[i];
        if (!part->active || part->size_sectors == 0) {
            continue;
        }

        /* Fill the whole line first to create a "selected" effect */
        if (*selected_part == i) {
            bounds = nk_widget_bounds(ctx);
            bounds.w = winWidth;
            bounds.x = 0;
            nk_fill_rect(canvas, bounds, 2.0f, NK_LIST_SELECTED);
        }

        /* Partition color */
        /* Rectangles don't count as an element in the flow so add a dummy element right after */
        bounds = nk_widget_bounds(ctx);
        /* Arrange a bit the colored square*/
        bounds.h -= 10;
        bounds.w -= 10;
        bounds.y += 5;
        bounds.x += 5;
        nk_fill_rect(canvas, bounds, 2.f, get_partition_color(i));
        nk_bool select = false;
        nk_selectable_label(ctx, " ", NK_TEXT_LEFT, &select);
        nk_selectable_label(ctx, " ", NK_TEXT_LEFT, &select);

        /* Partition number */
        sprintf(buffer, "%d", i);
        nk_selectable_label(ctx, buffer, NK_TEXT_LEFT, &select);

        /* Partition file system */
        nk_selectable_label(ctx, disk_get_fs_type(part->type), NK_TEXT_LEFT, &select);

        /* Partition start address */
        sprintf(buffer, "0x%08lx", part->start_lba * DISK_SECTOR_SIZE);
        nk_selectable_label(ctx, buffer, NK_TEXT_LEFT, &select);

        /* Partition size */
        disk_get_size_str(part->size_sectors * DISK_SECTOR_SIZE, buffer, sizeof(buffer));
        nk_selectable_label(ctx, buffer, NK_TEXT_RIGHT, &select);
        nk_selectable_label(ctx, " ", NK_TEXT_LEFT, &select);

        if (select) {
            *selected_part = i;
        }
    }

    nk_style_pop_color(ctx);
    nk_style_pop_color(ctx);
    nk_style_pop_color(ctx);
    nk_style_pop_color(ctx);
    nk_style_pop_color(ctx);
    nk_style_pop_color(ctx);
}


static void ui_mbr_handle(struct nk_context *ctx, disk_info_t* disk)
{
    void* arg;
    struct nk_rect position;
    if (popup_is_opened(POPUP_MBR, &position, &arg)) {
        popup_info_t* info = (popup_info_t*) arg;

        if(nk_begin(ctx, info->title, position, NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_MOVABLE)) {
            nk_window_set_bounds(ctx, info->title, position);
            nk_layout_row_dynamic(ctx, 40, 1);
            nk_label_wrap(ctx, info->msg);
            if (nk_button_label(ctx, "Okay")) {
                popup_close(POPUP_MBR);
            }
        }
        nk_end(ctx);
    }

    (void) disk;
}


static void ui_apply_handle(struct nk_context *ctx, disk_info_t* disk)
{
    struct nk_rect position;
    if (popup_is_opened(POPUP_APPLY, &position, NULL)) {

        if(nk_begin(ctx, "Apply changes", position, NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_MOVABLE)) {
            nk_layout_row_dynamic(ctx, 30, 1);
            nk_label_wrap(ctx, "Apply changes to disk? This action is permanent and cannot be undone.");
            nk_layout_row_dynamic(ctx, 30, 2);
            if (nk_button_label(ctx, "Yes")) {
                static popup_info_t result_info = {
                    .title = "Apply changes",
                    .msg = "Success!"
                };
                const char* error_str = disk_write_changes(disk);
                if (error_str) {
                    result_info.msg = error_str;
                    printf("%s\n", error_str);
                }
                popup_close(POPUP_APPLY);
                popup_open(POPUP_MBR, 300, 140, &result_info);
            } else if (nk_button_label(ctx, "No")) {
                popup_close(POPUP_APPLY);
            }
        }
        nk_end(ctx);
    }

    (void) disk;
}


static void ui_cancel_handle(struct nk_context *ctx, disk_info_t* disk)
{
    struct nk_rect position;
    if (popup_is_opened(POPUP_CANCEL, &position, NULL)) {

        if(nk_begin(ctx, "Cancel changes", position, NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_MOVABLE)) {
            nk_layout_row_dynamic(ctx, 30, 1);
            nk_label_wrap(ctx, "Discard all changes? All unsaved changes will be lost.");
            nk_layout_row_dynamic(ctx, 30, 2);
            if (nk_button_label(ctx, "Yes")) {
                disk_revert_changes(disk);
                popup_close(POPUP_CANCEL);
            } else if (nk_button_label(ctx, "No")) {
                popup_close(POPUP_CANCEL);
            }
        }
        nk_end(ctx);
    }

    (void) disk;
}



/**
 * @brief Render the new partition popup
 */
static void ui_new_partition(struct nk_context *ctx, disk_info_t* disk)
{
    const char* all_alignments[] = { "512 bytes", "1 MiB" };
    static int selected_alignment = 1;
    static int selected_size = 0;
    const int alignment = (selected_alignment == 0) ? 512 : 1048576;

    struct nk_rect position;
    void *arg;
    if (!popup_is_opened(POPUP_NEWPART, &position, &arg)) {
        return;
    }
    if(nk_begin(ctx, "Create a new partition", position, NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_MOVABLE)) {
        /* If there are no empty partition, show an error */
        if (disk->free_part_idx == -1) {
            nk_layout_row_dynamic(ctx, 30, 1);
            nk_label(ctx, "No free partition found on this disk", NK_TEXT_CENTERED);
            if (nk_button_label(ctx, "Cancel")) {
                popup_close(POPUP_NEWPART);
            }
            nk_end(ctx);
            return;
        }

        /* There is a free partition on the disk */
        uint64_t largest_free_addr = 0;

        const float ratio[] = { 0.3f, 0.6f };
        nk_layout_row(ctx, NK_DYNAMIC, COMBO_HEIGHT, 2, ratio);

        /* Combo box for the partition type, only ZealFS v2 (for now?) */
        nk_label(ctx, "Type:", NK_TEXT_CENTERED);
        const char* types[] = { "ZealFSv2" };
        const float width = nk_widget_width(ctx);
        nk_combo(ctx, types, 1, 0, COMBO_HEIGHT, nk_vec2(width, 150));

        /* For the partition size, do not propose anything bigger than the disk size of course */
        nk_label(ctx, "Size:", NK_TEXT_CENTERED);
        const int valid_entries = disk_valid_partition_size(disk, alignment, &largest_free_addr);
        if (valid_entries > 0) {
            /* Make sure the selection isn't bigger than the last valid size */
            selected_size = NK_MIN(selected_size, valid_entries - 1);
            selected_size = nk_combo(ctx, disk_get_partition_size_list(NULL), valid_entries,
                                 selected_size, COMBO_HEIGHT, nk_vec2(width, 150));
        } else {
            nk_label(ctx, "No size available", NK_TEXT_LEFT);
        }

        /* Combo box for the alignment */
        nk_label(ctx, "Alignment:", NK_TEXT_CENTERED);
        selected_alignment = nk_combo(ctx, all_alignments, 2, selected_alignment, COMBO_HEIGHT, nk_vec2(width, 150));

        /* Show the address where it will be created */
        char address[16];
        nk_label(ctx, "Address:", NK_TEXT_CENTERED);
        snprintf(address, sizeof(address), "0x%08" PRIx64, largest_free_addr);
        nk_label(ctx, address, NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, 30, 2);

        /* One line padding */
        nk_label(ctx, "", NK_TEXT_CENTERED);
        nk_label(ctx, "", NK_TEXT_CENTERED);

        if (valid_entries != -1 && nk_button_label(ctx, "Create")) {
            /* The user clicked on `Create`, allocate a new ZealFS partition */
            assert(largest_free_addr % DISK_SECTOR_SIZE == 0);
            disk_allocate_partition(disk, largest_free_addr / DISK_SECTOR_SIZE, selected_size);
            popup_close(POPUP_NEWPART);
        }
        if (nk_button_label(ctx, "Cancel")) {
            popup_close(POPUP_NEWPART);
        }
    }
    nk_end(ctx);
}


/**
 * @brief Render the new disk image popup
 */
static void ui_new_image(struct nk_context *ctx, disk_list_state_t* state)
{
    disk_info_t* current_disk = disk_get_current(state);
    struct nk_rect position;
    void *arg;
    if (!popup_is_opened(POPUP_NEWIMG, &position, &arg)) {
        return;
    }
    if (nk_begin(ctx, "Create a new disk image", position, NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_MOVABLE))
    {
        static char image_path[4096] = "disk.img";
        static int image_len = 8;
        static int image_size_index = 0;

        const float ratio[] = { 0.3f, 0.5f, 0.2f };
        nk_layout_row(ctx, NK_DYNAMIC, COMBO_HEIGHT, 3, ratio);

        /* Input field for the image name */
        nk_label(ctx, "Location:", NK_TEXT_CENTERED);
        /* Button to browse for a file using tinyfiledialogs */
        nk_edit_string(ctx, NK_EDIT_FIELD, image_path, &image_len, sizeof(image_path), nk_filter_default);
        if (nk_button_label(ctx, "Browse...")) {
            const char* filter_patterns[] = { "*.img" };
            const char* selected_file = tinyfd_saveFileDialog("Select Disk Image", image_path, 1, filter_patterns, NULL);
            if (selected_file) {
                strncpy(image_path, selected_file, sizeof(image_path) - 1);
                image_path[sizeof(image_path) - 1] = '\0';
                image_len = strlen(image_path);
            }
        }

        /* Combo box for the image size */
        nk_label(ctx, "Size:", NK_TEXT_CENTERED);
        const float width = nk_widget_width(ctx);
        int sizes_count = 0;
        const char* const * sizes = disk_get_partition_size_list(&sizes_count);
        int former_size_index = image_size_index;
        image_size_index = nk_combo(ctx, sizes, sizes_count,
                                    image_size_index, COMBO_HEIGHT, nk_vec2(width, 150));
        nk_label(ctx, "", NK_TEXT_CENTERED);

        /* Combo box for the partition table */
        nk_label(ctx, "Table:", NK_TEXT_CENTERED);
        const char* partition_table_options[] = { "None", "MBR" };
        static int selected_partition_table = 0;
        /* If the size just changed and the new size is smalle than a few MB, make None the default option */
        if (former_size_index != image_size_index) {
            /* Default to "None" if the disk size selected is a few MB or lower */
            selected_partition_table = (image_size_index <= 5) ? 0 : 1;
        }
        selected_partition_table = nk_combo(ctx, partition_table_options, 2, selected_partition_table, COMBO_HEIGHT, nk_vec2(width, 150));
        nk_label(ctx, "", NK_TEXT_CENTERED);


        nk_layout_row_dynamic(ctx, 30, 2);

        /* One line padding */
        nk_label(ctx, "", NK_TEXT_CENTERED);
        nk_label(ctx, "", NK_TEXT_CENTERED);

        if (nk_button_label(ctx, "Create")) {
            /* The user clicked on `Create`, create a new disk image */
            uint64_t selected_size = disk_get_size_of_idx(image_size_index);
            popup_close(POPUP_NEWIMG);
            int new_index = disk_create_image(state, image_path, selected_size, selected_partition_table == 1);
            if (new_index == -1) {
                static popup_info_t error_info = {
                    .title = "Error",
                    .msg = "Failed to create the disk image. Please try again."
                };
                popup_open(POPUP_MBR, 300, 140, &error_info);
            } else if (!current_disk->has_staged_changes) {
                /* Switch to thew newly created disk if the current one has no pending changes */
                state->selected_disk = new_index;
                state->selected_partition = -1;
            }
        }
        if (nk_button_label(ctx, "Cancel")) {
            popup_close(POPUP_NEWIMG);
        }
    }
    nk_end(ctx);
}


static void setup_window() {
    InitWindow(0, 0, "Zeal Disk Tool " VERSION);

    // get current monitor details
    int mw, mh;
    int monitor = GetCurrentMonitor();
    mw = GetMonitorWidth(monitor);
    mh = GetMonitorHeight(monitor);

    // clamp the window size to either WIN_SCALE or MIN
    winWidth = NK_MAX(MIN_WIN_WIDTH, mw * WIN_SCALE);
    winHeight = NK_MAX(MIN_WIN_HEIGHT, winWidth * WIN_ASPECT);
    SetWindowSize(winWidth, winHeight);

    // center the window on the current monitor
    Vector2 mon_pos = GetMonitorPosition(monitor);
    int pos_x = mon_pos.x, pos_y = mon_pos.y;
    pos_x += (mw - winWidth) / 2;
    pos_y += (mh - winHeight) / 2;
    SetWindowPosition(pos_x, pos_y);

#ifndef __APPLE__
    /* Set an icon for the application */
    Image icon = LoadImageFromMemory(".png", s_app_icon_png, sizeof(s_app_icon_png));
    SetWindowIcon(icon);
#endif
}


int main(void) {
    SetTraceLogLevel(LOG_WARNING);
    setup_window();

    SetTargetFPS(60);
    popup_init(winWidth, winHeight);

    disk_err_t err = disks_refresh();

    /* Mac/Linux targets only */
    if (err == ERR_NOT_ROOT) {
        printf("You must run this program as root\n");
        return 1;
    } else if (err == ERR_NOT_ADMIN) {
        return message_box(ctx, "You must run this program as Administrator!\n");
    }

    const int fontSize = 13;
    Font font = LoadFontFromNuklear(fontSize);
    ctx = InitNuklearEx(font, fontSize);

    ui_statusbar_print("Ready!");

    while (!WindowShouldClose()) {
        UpdateNuklear(ctx);

        /* If any popup is opened, the main window must not be focusable */
        int flags = NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_MINIMIZABLE |
                    NK_WINDOW_BORDER | NK_WINDOW_TITLE;
        flags |= popup_any_opened() ? NK_WINDOW_NO_INPUT : 0;
        disk_list_state_t* state = disk_get_state();
        disk_info_t* current_disk = disk_get_current(state);

        nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                                nk_style_item_color(nk_rgb(0x39, 0x39, 0x39)));

        struct nk_rect disk_view_rect = {
            .x = 0,
            .y = MENUBAR_HEIGHT,
            .w = winWidth * 0.70f,
            .h = winHeight - MENUBAR_HEIGHT - ui_statusbar_height(ctx)
        };
        if (nk_begin(ctx, "Disk partitioning", disk_view_rect, flags)) {

            /* Create the top row with the buttons and the disk selection */
            const float ratio[] = { 0.10f, 0.15f, 0.15f, 0.07f, 0.07f, 0.15f, 0.3f };
            nk_layout_row(ctx, NK_DYNAMIC, COMBO_HEIGHT, 7, ratio);

            /* Create the button with label "MBR" */
            if (nk_widget_is_hovered(ctx)) {
                nk_tooltip(ctx, "Create an MBR on the disk");
            }
            /* Only enable the buttons if we have at least one disk */
            if (nk_button_label(ctx, "Create MBR")) {
                ui_menubar_create_mbr(ctx, current_disk);
            }

            /* Create the button to add a new partition */
            if (nk_widget_is_hovered(ctx)) {
                nk_tooltip(ctx, "Create a new partition on the disk");
            }
            if (nk_button_label(ctx, "New partition")) {
                ui_menubar_new_partition(ctx, current_disk, &state->selected_new_part_opt);
            }

            /* Create the button to delete a partition */
            if (nk_widget_is_hovered(ctx)) {
                nk_tooltip(ctx, "Delete the selected partition on the disk");
            }
            if ((nk_button_label(ctx, "Delete partition") || IsKeyPressed(KEY_DELETE))) {
                ui_menubar_delete_partition(ctx, current_disk, state->selected_partition);
            }

            /* Create the button to commit the changes */
            if (nk_widget_is_hovered(ctx)) {
                nk_tooltip(ctx, "Apply all the changes to the selected disk");
            }
            if (nk_button_label(ctx, "Apply")) {
                ui_menubar_apply_changes(ctx, current_disk);
            }

            if (nk_widget_is_hovered(ctx)) {
                nk_tooltip(ctx, "Cancel all the changes to the selected disk");
            }
            if (nk_button_label(ctx, "Cancel")) {
                ui_menubar_cancel_changes(ctx, current_disk);
            }

            nk_label(ctx, "Select a disk:", NK_TEXT_RIGHT);
            float combo_width = nk_widget_width(ctx);

            int new_selection = ui_combo_disk(ctx, state, combo_width);
            if (new_selection != state->selected_disk) {
                if (disk_can_be_switched(current_disk)) {
                    state->selected_disk = new_selection;
                    state->selected_partition = -1;
                } else {
                    static popup_info_t info = {
                        .title = "Cannot switch disk",
                        .msg = "The selected disk has unsaved changes. Please apply or discard them before switching disks."};
                    popup_open(POPUP_MBR, 300, 140, &info);
                }
            }

            ui_draw_disk(ctx, current_disk, &state->selected_partition);
        }
        nk_end(ctx);
        nk_style_pop_style_item(ctx);

        /* Manage other windows here */
        ui_mbr_handle(ctx, current_disk);
        ui_apply_handle(ctx, current_disk);
        ui_cancel_handle(ctx, current_disk);
        ui_new_partition(ctx, current_disk);
        ui_new_image(ctx, state);
        /* Only allow the partition viewer if a partition is selected and we have no staged changes */
        struct nk_rect viewer_bounds = {
            .x = disk_view_rect.w,
            .y = disk_view_rect.y,
            .w = winWidth - disk_view_rect.w,
            .h = disk_view_rect.h,
        };
        if (current_disk != NULL && !current_disk->has_staged_changes) {
            ui_partition_viewer(ctx, current_disk, state->selected_partition, viewer_bounds);
        } else {
            ui_partition_viewer(ctx, current_disk, -1, viewer_bounds);
        }

        /* Make the menubar always on top, returns non-zero if we must close the window */
        if (ui_menubar_show(ctx, state, winWidth)) {
            break;
        }

        /* Show the status bar */
        ui_statusbar_show(ctx, winWidth, winHeight);

        BeginDrawing();
            ClearBackground(WHITE);
            DrawNuklear(ctx);
        EndDrawing();
    }

    UnloadNuklear(ctx);
    CloseWindow();
    return 0;
}
