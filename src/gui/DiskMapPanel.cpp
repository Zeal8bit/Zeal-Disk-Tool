/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gui/DiskMapPanel.hpp"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstdio>
#include <string>

#include "gui/ModernStyle.hpp"
#include "gui/Utils.hpp"

DiskMapPanel::DiskMapPanel(int x, int y, int w, int h) : Fl_Widget(x, y, w, h)
{
    box(FL_NO_BOX);
}

void DiskMapPanel::refresh(disk_info_t *disk, int selected_slot)
{
    segments_.clear();
    selected_ = selected_slot;
    has_disk_ = disk != nullptr && disk->valid;
    disk_bytes_ = disk != nullptr ? disk->size_bytes : 0;
    if (has_disk_ && disk_bytes_ > 0) {
        const double total = (double) disk_bytes_;
        for (int slot = 0; slot < MAX_PART_COUNT; ++slot) {
            const partition_t &part = disk->staged_partitions[slot];
            if (!part.active || part.size_sectors == 0) {
                continue;
            }
            Segment segment;
            segment.slot = slot;
            segment.start = (double) part.start_lba * DISK_SECTOR_SIZE / total;
            segment.length = (double) part.size_sectors * DISK_SECTOR_SIZE / total;
            segment.usage = -1;
            segment.color = modern::slot_color(slot);
            if (part.type == ZEALFS_TYPE) {
                uint64_t used = 0;
                uint64_t part_total = 0;
                if (disk_fs_usage(disk, slot, &used, &part_total) == 0) {
                    segment.usage = utils::percent_of(used, part_total);
                }
            }
            segments_.push_back(segment);
        }
    }
    hover_ = -1;
    redraw();
}

int DiskMapPanel::hit(int px) const
{
    if (segments_.empty()) {
        return -1;
    }
    const int left = x() + 8;
    const int width = w() - 16;
    for (const Segment &segment : segments_) {
        const int sx = left + (int) (segment.start * width);
        const int sw = std::max(4, (int) (segment.length * width));
        if (px >= sx && px < sx + sw) {
            return segment.slot;
        }
    }
    return -1;
}

void DiskMapPanel::draw_segment(const Segment &segment,
                                int x,
                                int y,
                                int w,
                                int h,
                                bool selected,
                                bool hovered)
{
    if (w <= 0) {
        return;
    }
    const Fl_Color base = hovered ? modern::lighten(segment.color, 0.12) : segment.color;
    modern::fill_round(x, y, w, h, 7, base);
    if (segment.usage > 0) {
        const int used = std::max(2, h * segment.usage / 100);
        modern::fill_round(x, y + h - used, w, used, 7, modern::darken(segment.color, 0.18));
    }
    if (selected) {
        modern::stroke_round(x + 1, y + 1, w - 2, h - 2, 6, FL_WHITE, 2);
    }
    if (w >= 56) {
        char title[32];
        std::snprintf(title, sizeof(title), "Partition %d", segment.slot);
        modern::text_clipped(title,
                             x + 4,
                             y + h / 2 - 12,
                             w - 8,
                             18,
                             FL_WHITE,
                             11,
                             true,
                             FL_ALIGN_CENTER | FL_ALIGN_CLIP);
        const std::string size = utils::format_size((uint64_t) (segment.length * (double) disk_bytes_));
        modern::text_clipped(size,
                             x + 4,
                             y + h / 2 + 2,
                             w - 8,
                             16,
                             modern::lighten(segment.color, 0.78),
                             10,
                             false,
                             FL_ALIGN_CENTER | FL_ALIGN_CLIP);
    }
}

void DiskMapPanel::draw()
{
    modern::fill_round(x(), y(), w(), h(), 8, modern::color(0xedf1f6));
    if (!has_disk_) {
        modern::text_center(disk_bytes_ == 0 ? "No disk selected" : "This disk has no readable size",
                            x() + w() / 2,
                            y() + h() / 2 + 4,
                            modern::color(modern::kGray2),
                            12,
                            false);
        return;
    }
    if (segments_.empty()) {
        modern::text_center("No partitions on this disk yet",
                            x() + w() / 2,
                            y() + h() / 2 + 4,
                            modern::color(modern::kGray2),
                            12,
                            false);
        return;
    }

    const int left = x() + 8;
    const int inner_w = w() - 16;
    const int top = y() + 8;
    const int inner_h = h() - 16;
    for (const Segment &segment : segments_) {
        const int sx = left + (int) (segment.start * inner_w) + 1;
        const int sw = std::max(4, (int) (segment.length * inner_w) - 2);
        draw_segment(segment, sx, top, sw, inner_h, segment.slot == selected_, segment.slot == hover_);
    }

    if (segments_.front().start > 0.02) {
        modern::text("Free", left + 6, top + inner_h / 2 + 4, modern::color(modern::kGray2), 10, false);
    }
}

int DiskMapPanel::handle(int event)
{
    switch (event) {
    case FL_PUSH:
        if (Fl::event_button() == FL_LEFT_MOUSE) {
            const int slot = hit(Fl::event_x());
            if (slot >= 0) {
                selected_ = slot;
                redraw();
                if (on_select) {
                    on_select(slot);
                }
            }
            return 1;
        }
        break;
    case FL_MOVE: {
        const int slot = hit(Fl::event_x());
        if (slot != hover_) {
            hover_ = slot;
            redraw();
        }
        return 1;
    }
    case FL_LEAVE:
        if (hover_ != -1) {
            hover_ = -1;
            redraw();
        }
        break;
    default:
        break;
    }
    return Fl_Widget::handle(event);
}
