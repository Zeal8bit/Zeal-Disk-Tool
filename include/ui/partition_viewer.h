/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PART_VIEW_H
#define PART_VIEW_H

#include <stdint.h>
#include "disk.h"
#include "nuklear.h"

int ui_partition_viewer(struct nk_context *ctx, disk_info_t* disk, int partition_idx, struct nk_rect bounds);

void ui_partition_viewer_clear(struct nk_context *ctx);

int ui_partition_viewer_get_partition_usage_percentage(uint64_t* free_bytes, uint64_t* total_bytes);

#endif // PART_VIEW_H
