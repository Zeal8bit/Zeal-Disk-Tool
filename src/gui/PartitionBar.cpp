/**
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "PartitionBar.hpp"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstdio>

#include "Gui.hpp"

namespace {

Fl_Color lighten(Fl_Color color, double amount)
{
    uchar red = 0;
    uchar green = 0;
    uchar blue = 0;
    Fl::get_color(color, red, green, blue);
    const double keep = 1.0 - amount;
    return fl_rgb_color((uchar) (red * keep + 255 * amount),
                        (uchar) (green * keep + 255 * amount),
                        (uchar) (blue * keep + 255 * amount));
}

Fl_Color darken(Fl_Color color, double amount)
{
    uchar red = 0;
    uchar green = 0;
    uchar blue = 0;
    Fl::get_color(color, red, green, blue);
    const double keep = 1.0 - amount;
    return fl_rgb_color((uchar) (red * keep), (uchar) (green * keep), (uchar) (blue * keep));
}

int box_left(int x)
{
    return x + 12;
}

int box_width(int w)
{
    return w - 24;
}

}  // namespace

PartitionBar::PartitionBar(int x, int y, int w, int h) : Fl_Widget(x, y, w, h)
{
    box(FL_NO_BOX);
}

void PartitionBar::refresh(disk_info_t *disk, int selected)
{
    items_.clear();
    selected_ = selected;
    has_disk_ = disk != nullptr;
    if (disk != nullptr && disk->valid && disk->size_bytes > 0) {
        const double total_sectors = (double) (disk->size_bytes / DISK_SECTOR_SIZE);
        for (int slot = 0; slot < MAX_PART_COUNT; ++slot) {
            const partition_t &part = disk->staged_partitions[slot];
            if (!part.active || part.size_sectors == 0 || total_sectors <= 0) {
                continue;
            }
            Item item;
            item.slot = slot;
            item.start_fraction = (double) part.start_lba / total_sectors;
            item.size_fraction = (double) part.size_sectors / total_sectors;
            item.usage = -1;
            item.type = part.type;
            item.zealfs = part.type == ZEALFS_TYPE;
            if (item.zealfs) {
                uint64_t used = 0;
                uint64_t total = 0;
                if (disk_fs_usage(disk, slot, &used, &total) == 0) {
                    item.usage = gui::percent_of(used, total);
                }
            }
            items_.push_back(item);
        }
    }
    hover_ = -1;
    redraw();
}

int PartitionBar::hit_test(int px) const
{
    const int left = box_left(x());
    const int width = box_width(w());
    const int inner_left = left + 2;
    const int inner_width = width - 4;
    if (px < inner_left || px >= inner_left + inner_width) {
        return -1;
    }
    for (const Item &item : items_) {
        const int start = inner_left + (int) (item.start_fraction * inner_width);
        const int span = std::max(4, (int) (item.size_fraction * inner_width));
        if (px >= start && px < start + span) {
            return item.slot;
        }
    }
    return -1;
}

void PartitionBar::notify(int slot)
{
    if (slot < 0 || slot == selected_) {
        return;
    }
    selected_ = slot;
    redraw();
    if (on_select) {
        on_select(slot);
    }
}

void PartitionBar::redraw_if_hover(int slot)
{
    if (slot != hover_) {
        hover_ = slot;
        redraw();
    }
}

void PartitionBar::draw()
{
    const int left = box_left(x());
    const int width = box_width(w());
    const int top = y();

    fl_push_clip(x(), y(), w(), h());
    fl_color(gui::background);
    fl_rectf(x(), y(), w(), h());

    fl_rounded_rectf(left, top, width, h(), 8);
    fl_color(gui::panel_alt);
    fl_rounded_rectf(left + 1, top + 1, width - 2, h() - 2, 8);
    fl_color(gui::border);
    fl_rounded_rect(left, top, width, h(), 8);

    if (items_.empty()) {
        fl_font(FL_HELVETICA, 12);
        fl_color(gui::text_muted);
        fl_draw(has_disk_ ? "No partitions on this disk yet - use \"New partition\" to create one."
                          : "No disk selected - open or create a disk image.",
                left,
                top,
                width,
                h(),
                FL_ALIGN_CENTER | FL_ALIGN_CLIP);
        fl_pop_clip();
        return;
    }

    const int inner_left = left + 2;
    const int inner_width = width - 4;
    const int inner_top = top + 10;
    const int inner_height = h() - 20;

    for (const Item &item : items_) {
        const int start = inner_left + (int) (item.start_fraction * inner_width);
        const int span = std::max(4, (int) (item.size_fraction * inner_width));
        const Fl_Color base = gui::partition_color(item.slot);
        const Fl_Color fill = lighten(base, hover_ == item.slot ? 0.62 : 0.78);
        const Fl_Color edge = hover_ == item.slot ? darken(base, 0.12) : base;

        fl_color(fill);
        fl_rectf(start, inner_top, span, inner_height);
        if (item.usage > 0) {
            const int used_height = std::max(2, inner_height * item.usage / 100);
            fl_color(lighten(base, 0.42));
            fl_rectf(start, inner_top + inner_height - used_height, span, used_height);
        }
        fl_color(edge);
        fl_rect(start, inner_top, span, inner_height);
        if (item.slot == selected_) {
            fl_line_style(FL_SOLID, 3);
            fl_color(gui::accent);
            fl_rect(start + 1, inner_top + 1, span - 2, inner_height - 2);
            fl_line_style(0);
        }

        if (span >= 46) {
            char title[32];
            char detail[48];
            std::snprintf(title, sizeof(title), "P%d", item.slot);
            if (item.usage >= 0) {
                std::snprintf(detail, sizeof(detail), "%d%% used", item.usage);
            } else {
                std::snprintf(detail, sizeof(detail), "%s", item.zealfs ? "ZealFS" : disk_get_fs_type(item.type));
            }
            fl_font(FL_HELVETICA_BOLD, 12);
            fl_color(gui::text);
            fl_draw(title,
                    start + 4,
                    inner_top + (inner_height / 2) - 14,
                    span - 8,
                    16,
                    FL_ALIGN_CENTER | FL_ALIGN_CLIP);
            fl_font(FL_HELVETICA, 10);
            fl_color(gui::text_muted);
            fl_draw(detail,
                    start + 4,
                    inner_top + (inner_height / 2) + 2,
                    span - 8,
                    14,
                    FL_ALIGN_CENTER | FL_ALIGN_CLIP);
        }
    }

    fl_font(FL_HELVETICA, 10);
    fl_color(gui::text_muted);
    fl_draw("0", inner_left, top + h() - 2);
    fl_draw("end", inner_left, top + h() - 2, inner_width, 8, FL_ALIGN_RIGHT | FL_ALIGN_CLIP);
    fl_pop_clip();
}

int PartitionBar::handle(int event)
{
    switch (event) {
    case FL_PUSH:
        if (Fl::event_button() == FL_LEFT_MOUSE) {
            const int slot = hit_test(Fl::event_x());
            if (slot >= 0) {
                notify(slot);
                if (Fl::event_clicks() > 0 && on_activate) {
                    on_activate(slot);
                }
            }
            return 1;
        }
        break;
    case FL_MOVE:
        redraw_if_hover(hit_test(Fl::event_x()));
        return 1;
    case FL_LEAVE:
        redraw_if_hover(-1);
        break;
    default:
        break;
    }
    return Fl_Widget::handle(event);
}
