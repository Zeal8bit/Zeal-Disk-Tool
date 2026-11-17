/**
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <FL/Fl_Widget.H>
#include <functional>
#include <vector>

#include "disk.h"

/* Horizontal map of the selected disk. Partitions are drawn proportionally to
 * their start address and size, and can be selected with the mouse. */
class PartitionBar : public Fl_Widget {
public:
    PartitionBar(int x, int y, int w, int h);

    void refresh(disk_info_t *disk, int selected);
    int selected_slot() const { return selected_; }

    std::function<void(int slot)> on_select;
    std::function<void(int slot)> on_activate;

    void draw();
    int handle(int event);

private:
    struct Item {
        int slot;
        double start_fraction;
        double size_fraction;
        int usage;
        uint8_t type;
        bool zealfs;
    };

    int hit_test(int x) const;
    void notify(int slot);
    void redraw_if_hover(int slot);

    std::vector<Item> items_;
    int selected_ = -1;
    int hover_ = -1;
    bool has_disk_ = false;
};
