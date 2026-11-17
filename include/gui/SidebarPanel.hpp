/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <FL/Fl_Widget.H>

#include <functional>
#include <vector>

#include "disk.h"

/* Left-hand disk/partition selector drawn in the modern style. */
class SidebarPanel : public Fl_Widget {
public:
    SidebarPanel(int x, int y, int w, int h);

    void refresh(disk_list_state_t *state, int selected_disk, int selected_slot);
    void select(int disk, int slot);

    std::function<void(int disk, int slot)> on_select;
    /* Opens the add/open menu; coordinates are screen coordinates. */
    std::function<void(int screen_x, int screen_y)> on_add;

    void draw() override;
    int handle(int event) override;

private:
    enum class RowKind {
        Disk,
        Partition,
    };
    struct Row {
        RowKind kind;
        int disk;
        int slot;
        int y;
        int h;
    };

    int content_height() const;
    int row_at(int y) const;
    void clamp_scroll();
    void scroll_by(int amount);
    void draw_scrollbar(int x, int y, int h);

    disk_list_state_t *state_ = nullptr;
    int selected_disk_ = -1;
    int selected_slot_ = -1;
    std::vector<Row> rows_;
    int scroll_ = 0;
    int hover_ = -1;
    bool add_hover_ = false;
};
