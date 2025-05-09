/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "ui/statusbar.h"

static char s_message[STATUSBAR_MSG_LEN];

int ui_statusbar_height(struct nk_context *ctx)
{
    return ctx->style.font->height + 4 * ctx->style.window.padding.y;
}


void ui_statusbar_print(const char* msg)
{
    strncpy(s_message, msg, STATUSBAR_MSG_LEN - 1);
    s_message[STATUSBAR_MSG_LEN - 1] = 0;
}


void ui_statusbar_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(s_message, STATUSBAR_MSG_LEN, fmt, args);
    va_end(args);

    if (len < 0 || len >= STATUSBAR_MSG_LEN) {
        s_message[STATUSBAR_MSG_LEN - 1] = 0;
    }
}


void ui_statusbar_show(struct nk_context *ctx, int win_width, int win_height)
{
    const int statusbar_height = ui_statusbar_height(ctx);

    if (nk_begin(ctx, "StatusBar", nk_rect(0, win_height - statusbar_height, win_width, statusbar_height),
        NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(ctx, statusbar_height, 1);
        nk_label(ctx, s_message, NK_TEXT_LEFT);
    }
    nk_end(ctx);
}