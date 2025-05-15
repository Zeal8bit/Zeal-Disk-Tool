#include <stdio.h>
#include <assert.h>

#include "nuklear.h"
#include "raylib-nuklear.h"
#include "raylib.h"
#include "ui.h"
#include "ui/statusbar.h"
#include "ui/menubar.h"

int ui_combo_disk(struct nk_context *ctx, disk_list_state_t* state, int width)
{
    /* Add one for the last "open image" option */
    const int show_items = state->disk_count + 1;
    if (state->disk_count == 0) {
        ui_statusbar_print("No disk found!\n");
    }

    struct nk_vec2 size = nk_vec2(width, winHeight);

    int i = 0;
    int max_height;
    struct nk_vec2 item_spacing;
    struct nk_vec2 window_padding;

    assert(ctx);
    assert(ctx->current);

    item_spacing = ctx->style.window.spacing;
    window_padding = ctx->style.window.combo_padding;
    max_height = show_items * COMBO_HEIGHT + show_items * (int)item_spacing.y;
    max_height += (int)item_spacing.y * 2 + (int)window_padding.y * 2;
    size.y = NK_MIN(size.y, (float)max_height);

    disk_info_t* disks = state->disks;

    char label[DISK_LABEL_LEN] = { 0 };
    disk_info_t* current_disk = disk_get_current(state);
    if (current_disk) {
        snprintf(label, DISK_LABEL_LEN, "%s", current_disk->label);
        label[0] = current_disk->has_staged_changes ? '*' : ' ';
    }

    if (nk_combo_begin_label(ctx, label, size)) {
        nk_layout_row_dynamic(ctx, (float)COMBO_HEIGHT, 1);
        for (i = 0; i < state->disk_count; ++i) {
            disk_info_t* disk = &disks[i];

            if(disk->valid) {
                if(state->selected_disk == i) {
                    nk_style_push_color(ctx, &ctx->style.contextual_button.text_normal, nk_rgb(0,127,127));
                    char label[DISK_LABEL_LEN];
                    snprintf(label, DISK_LABEL_LEN, "%s", disk->label);
                    label[0] = '>';
                    nk_combo_item_label(ctx, label, NK_TEXT_LEFT);
                    nk_style_pop_color(ctx);
                } else if (nk_combo_item_label(ctx, disk->label, NK_TEXT_LEFT)) {
                    state->selected_disk = i;
                }
            } else {
                nk_style_push_color(ctx, &ctx->style.text.color, nk_rgb(255,87,51));
                nk_label(ctx, disk->label, NK_TEXT_LEFT);
                nk_style_pop_color(ctx);
            }
        }

        if (nk_combo_item_label(ctx, "Open image file...", NK_TEXT_LEFT)) {
            ui_menubar_load_image(ctx, state);
        }

        nk_combo_end(ctx);
    }
    return state->selected_disk;
}
