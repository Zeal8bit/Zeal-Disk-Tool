/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <FL/Fl_Group.H>

#include <functional>
#include <vector>

#include "disk.h"

class Fl_Button;

/* Selected partition card plus the vertical action rail on its right. */
class DetailsPanel : public Fl_Group {
public:
    enum class Action {
        Format,
        Delete,
        Properties,
    };

    DetailsPanel(int x, int y, int w, int h);

    void set_right_width(int width);
    void refresh(disk_info_t *disk, int slot);

    std::function<void(Action action)> on_action;
    /* Called when a partition row of the disk overview is clicked. */
    std::function<void(int slot)> on_select;

    void draw() override;
    int handle(int event) override;
    void resize(int x, int y, int w, int h) override;

private:
    struct OverviewRow {
        int slot;
        int y;
        int h;
    };

    void layout_actions();
    bool overview_active() const;

    disk_info_t *disk_ = nullptr;
    int slot_ = -1;
    std::vector<OverviewRow> overview_rows_;
    int overview_hover_ = -1;
    int right_width_ = 280;
    Fl_Button *buttons_[3] = {nullptr, nullptr, nullptr};
};
