/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef MENUBAR_H
#define MENUBAR_H

#include <stdint.h>
#include "disk.h"
#include "nuklear.h"

#define MENUBAR_HEIGHT  30

/**
 * @brief Show the menubar
 *
 * @returns 1 if the window must be closed, 0 else
 */
int ui_menubar_show(struct nk_context *ctx, disk_list_state_t* state, int width);

void ui_menubar_create_mbr(struct nk_context *ctx, disk_info_t* disk);

void ui_menubar_new_partition(struct nk_context *ctx, disk_info_t* disk, int *choose_option);

void ui_menubar_delete_partition(struct nk_context *ctx, disk_info_t* disk, int partition);

void ui_menubar_apply_changes(struct nk_context *ctx, disk_info_t* disk);

void ui_menubar_cancel_changes(struct nk_context *ctx, disk_info_t* disk);

#endif // MENUBAR_H
