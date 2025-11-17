/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "raylib-nuklear.h"

#define STATUSBAR_MSG_LEN   512

void ui_statusbar_show(struct nk_context *ctx, int win_width, int win_height);

int ui_statusbar_height(struct nk_context *ctx);

void ui_statusbar_print(const char* msg);

void ui_statusbar_printf(const char *fmt, ...);

