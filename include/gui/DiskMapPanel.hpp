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

/* Proportional partition map shown above the partition details. */
class DiskMapPanel : public Fl_Widget {
public:
    DiskMapPanel(int x, int y, int w, int h);

    void refresh(disk_info_t *disk, int selected_slot);
    std::function<void(int slot)> on_select;

    void draw() override;
    int handle(int event) override;

private:
    struct Segment {
        int slot;
        double start;
        double length;
        int usage;
        Fl_Color color;
    };

    int hit(int x) const;
    void draw_segment(const Segment &segment, int x, int y, int w, int h, bool selected, bool hovered);

    std::vector<Segment> segments_;
    bool has_disk_ = false;
    uint64_t disk_bytes_ = 0;
    int selected_ = -1;
    int hover_ = -1;
};
