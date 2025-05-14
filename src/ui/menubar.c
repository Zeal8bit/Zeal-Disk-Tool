
/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include "ui/popup.h"
#include "ui/menubar.h"

static popup_info_t info;


void ui_menubar_create_mbr(struct nk_context *ctx, disk_info_t* disk)
{
    if (disk != NULL) {
        info.title = "Create MBR table";
        if (disk->has_mbr) {
            info.msg = "Selected disk already has an MBR";
        } else {
            info.msg = "Feature not supported yet";
            // mbr_last_msg = "MBR created successfully!";
        }
        popup_open(POPUP_MBR, 300, 140, &info);
    }
}


void ui_menubar_new_partition(struct nk_context *ctx, disk_info_t* disk, int *choose_option)
{
    if (disk != NULL) {
        popup_open(POPUP_NEWPART, 300, 300, choose_option);
    }
}


void ui_menubar_delete_partition(struct nk_context *ctx, disk_info_t* disk, int partition)
{
    if (disk != NULL) {
        disk_delete_partition(disk, partition);
    } else {

    }
}


void ui_menubar_apply_changes(struct nk_context *ctx, disk_info_t* disk)
{
    if (disk != NULL && disk->has_staged_changes) {
        popup_open(POPUP_APPLY, 300, 130, NULL);
    } else {

    }
}


void ui_menubar_cancel_changes(struct nk_context *ctx, disk_info_t* disk)
{
    if (disk != NULL && disk->has_staged_changes) {
        popup_open(POPUP_CANCEL, 300, 130, NULL);
    } else {

    }
}


void ui_menubar_load_image(struct nk_context *ctx, disk_list_state_t* state)
{
    disk_info_t* current_disk = disk_get_current(state);
    /* Make the newly opened image the current disk only if it is valid AND teh current disk has no changes */
    int new_disk_idx = disk_open_image_file(state);
    if (new_disk_idx >= 0 && !current_disk->has_staged_changes && state->disks[new_disk_idx].valid) {
        state->selected_disk = new_disk_idx;
    }
}


void ui_menubar_new_image(struct nk_context *ctx, disk_list_state_t* state)
{
    popup_open(POPUP_NEWIMG, 300, 300, state);
}


int ui_menubar_show(struct nk_context *ctx, disk_list_state_t* state, int width)
{
    int must_exit = 0;

    if (nk_begin(ctx, "Menu", nk_rect(0, 0, width, MENUBAR_HEIGHT), NK_WINDOW_NO_SCROLLBAR)) {
        nk_menubar_begin(ctx);

        disk_info_t* disk = disk_get_current(state);

        const float ratios[] = { 0.04f, 0.07f, 0.04f };
        nk_layout_row(ctx, NK_DYNAMIC, 25, 3, ratios);

        if (nk_menu_begin_label(ctx, "File", NK_TEXT_LEFT, nk_vec2(130, 200))) {
            nk_layout_row_dynamic(ctx, 25, 1);
            if (nk_menu_item_label(ctx, "Open image...", NK_TEXT_LEFT)) {
                ui_menubar_load_image(ctx, state);
            } else if (nk_menu_item_label(ctx, "Create image...", NK_TEXT_LEFT)) {
                ui_menubar_new_image(ctx, state);
            }
            if (nk_menu_item_label(ctx, "Refresh devices", NK_TEXT_LEFT)) {
                disks_refresh();
            } else if (nk_menu_item_label(ctx, "Apply changes", NK_TEXT_LEFT)) {
                popup_open(POPUP_APPLY, 300, 130, NULL);
            } else if (nk_menu_item_label(ctx, "Cancel changes", NK_TEXT_LEFT)) {
                popup_open(POPUP_CANCEL, 300, 130, NULL);
            } else if (nk_menu_item_label(ctx, "Quit", NK_TEXT_LEFT)) {
                must_exit = 1;
            }
            nk_menu_end(ctx);
        }

        if (nk_menu_begin_label(ctx, "Partition", NK_TEXT_LEFT, nk_vec2(100, 200))) {
            nk_layout_row_dynamic(ctx, 25, 1);
            if (nk_menu_item_label(ctx, "Create MBR", NK_TEXT_LEFT)) {
                ui_menubar_create_mbr(ctx, disk);
            } else if (nk_menu_item_label(ctx, "New", NK_TEXT_LEFT)) {
                ui_menubar_new_partition(ctx, disk, &state->selected_new_part_opt);
            } else if (nk_menu_item_label(ctx, "Delete", NK_TEXT_LEFT)) {
                ui_menubar_delete_partition(ctx, disk, state->selected_partition);
            } else if (nk_menu_item_label(ctx, "Format", NK_TEXT_LEFT)) {
                const char* error = disk_format_partition(disk, state->selected_partition);
                info.title = "Format partition";
                info.msg = error ? error : "Success!";
                popup_open(POPUP_MBR, 300, 140, &info);
            }
            nk_menu_end(ctx);
        }

        if (nk_menu_begin_label(ctx, "Help", NK_TEXT_LEFT, nk_vec2(100, 200))) {
            nk_layout_row_dynamic(ctx, 25, 1);
            if (nk_menu_item_label(ctx, "About", NK_TEXT_LEFT)) {
                info = (popup_info_t) {
                    .title = "About",
                    .msg = "Zeal Disk Tool\nCreate ZealFS v2 partitions for disks!",
                };
                popup_open(POPUP_MBR, 300, 140, &info);
            }
            nk_menu_end(ctx);
        }

        nk_menubar_end(ctx);
    }
    nk_end(ctx);

    return must_exit;
}
