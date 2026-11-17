/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gui/SidebarPanel.hpp"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstdio>
#include <string>

#include "gui/ModernStyle.hpp"
#include "gui/Utils.hpp"

constexpr int kHeaderHeight = 44;
constexpr int kDiskHeight = 58;
constexpr int kPartHeight = 48;
constexpr int kDiskGap = 10;
constexpr int kRowInsetX = 10;
constexpr int kRowInsetY = 2;
constexpr int kRowRadius = 8;

/* One rounded row background for disks and partitions, so hover and selection
 * rectangles share the same insets and radius. */
static void draw_row_background(int x, int w, int row_y, int row_h, Fl_Color fill)
{
    modern::fill_round(x + kRowInsetX,
                       row_y + kRowInsetY,
                       w - 2 * kRowInsetX,
                       row_h - 2 * kRowInsetY,
                       kRowRadius,
                       fill);
}

SidebarPanel::SidebarPanel(int x, int y, int w, int h) : Fl_Widget(x, y, w, h)
{
    box(FL_NO_BOX);
    color(modern::color(modern::Palette::kPanel));
}

void SidebarPanel::refresh(disk_list_state_t *state, int selected_disk, int selected_slot)
{
    state_ = state;
    selected_disk_ = selected_disk;
    selected_slot_ = selected_slot;
    rows_.clear();
    int cursor = 0;
    if (state != nullptr) {
        for (int disk_index = 0; disk_index < state->disk_count; ++disk_index) {
            rows_.push_back({RowKind::Disk, disk_index, -1, cursor, kDiskHeight});
            cursor += kDiskHeight;
            const disk_info_t &disk = state->disks[disk_index];
            for (int slot = 0; slot < MAX_PART_COUNT; ++slot) {
                if (!disk.partitions[slot].active || disk.partitions[slot].size_sectors == 0) {
                    continue;
                }
                rows_.push_back({RowKind::Partition, disk_index, slot, cursor, kPartHeight});
                cursor += kPartHeight;
            }
            cursor += kDiskGap;
        }
    }
    clamp_scroll();
    redraw();
}

void SidebarPanel::select(int disk, int slot)
{
    selected_disk_ = disk;
    selected_slot_ = slot;
    redraw();
}

int SidebarPanel::content_height() const
{
    if (rows_.empty()) {
        return 0;
    }
    const Row &last = rows_.back();
    return last.y + last.h;
}

void SidebarPanel::clamp_scroll()
{
    const int visible = std::max(0, h() - kHeaderHeight);
    const int total = content_height();
    scroll_ = std::max(0, std::min(scroll_, std::max(0, total - visible)));
}

void SidebarPanel::scroll_by(int amount)
{
    scroll_ += amount;
    clamp_scroll();
    redraw();
}

void SidebarPanel::draw_scrollbar(int sx, int sy, int sh)
{
    const int visible = std::max(1, h() - kHeaderHeight);
    const int total = content_height();
    if (total <= visible) {
        return;
    }
    const int track_h = sh;
    const int thumb_h = std::max(28, track_h * visible / total);
    const int max_scroll = std::max(1, total - visible);
    const int thumb_y = sy + (track_h - thumb_h) * scroll_ / max_scroll;
    modern::fill_round(sx, sy, 3, track_h, 2, modern::color(modern::Palette::kDividerSoft));
    modern::fill_round(sx, thumb_y, 3, thumb_h, 2, modern::color(modern::Palette::kScrollThumb));
}

void SidebarPanel::draw()
{
    fl_color(modern::color(modern::Palette::kPanel));
    fl_rectf(x(), y(), w(), h());
    fl_color(modern::color(modern::Palette::kDividerSoft));
    fl_line(x() + w() - 1, y(), x() + w() - 1, y() + h());

    /* Header */
    modern::text("Disks", x() + 18, y() + 27, modern::color(modern::Palette::kDark), 13, true);
    if (state_ != nullptr && state_->disk_count > 0) {
        modern::text(std::to_string(state_->disk_count),
                     x() + 64,
                     y() + 27,
                     modern::color(modern::Palette::kGray2),
                     10,
                     false);
    }
    const int add_x = x() + w() - 40;
    if (add_hover_) {
        modern::fill_round(add_x - 1, y() + 7, 30, 30, 8, modern::color(modern::Palette::kHover));
    }
    modern::draw_glyph(modern::Glyph::Create, add_x + 14, y() + 22, 15, modern::color(modern::Palette::kAccent));

    fl_push_clip(x(), y() + kHeaderHeight, w(), h() - kHeaderHeight);

    if (state_ == nullptr || state_->disk_count == 0) {
        modern::draw_drive(x() + w() / 2 - 18, y() + kHeaderHeight + 44, 36, 36, modern::color(modern::Palette::kGray2));
        modern::text_center("No disks detected",
                            x() + w() / 2,
                            y() + kHeaderHeight + 112,
                            modern::color(modern::Palette::kDark),
                            13,
                            true);
        modern::text_center("Use + to open or create",
                            x() + w() / 2,
                            y() + kHeaderHeight + 132,
                            modern::color(modern::Palette::kGray2),
                            11,
                            false);
        modern::text_center("a disk image.",
                            x() + w() / 2,
                            y() + kHeaderHeight + 148,
                            modern::color(modern::Palette::kGray2),
                            11,
                            false);
        fl_pop_clip();
        return;
    }

    for (size_t index = 0; index < rows_.size(); ++index) {
        const Row &row = rows_[index];
        const int row_y = y() + kHeaderHeight + row.y - scroll_;
        if (row_y + row.h <= y() + kHeaderHeight || row_y >= y() + h()) {
            continue;
        }
        const disk_info_t &disk = state_->disks[row.disk];
        const bool hovered = hover_ == (int) index;
        if (row.kind == RowKind::Disk) {
            /* Highlight the disk header itself, so clicking a disk gives the
             * same feedback as clicking one of its partitions. */
            if (row.disk == selected_disk_ && selected_slot_ < 0) {
                /* Only the overview state highlights the disk header; once a
                 * partition is selected, that row carries the selection. */
                draw_row_background(x(), w(), row_y, row.h, modern::color(modern::Palette::kSelection));
            } else if (hovered) {
                draw_row_background(x(), w(), row_y, row.h, modern::color(modern::Palette::kHover));
            }
            const bool failed = !disk.valid;
            const Fl_Color icon_color =
                failed ? modern::color(modern::Palette::kWarning) : modern::color(modern::Palette::kGray);
            modern::draw_drive(x() + 16, row_y + 14, 30, 30, icon_color);
            char title[96];
            std::snprintf(title,
                          sizeof(title),
                          "Disk %d \xE2\x80\x94 %s",
                          row.disk,
                          utils::format_size(disk.size_bytes).c_str());
            modern::text_clipped(title,
                                 x() + 58,
                                 row_y + 12,
                                 w() - 84,
                                 18,
                                 modern::color(failed ? modern::Palette::kWarning : modern::Palette::kDark),
                                 12,
                                 true);
            const std::string subtitle =
                std::string(disk.name) + "  \xC2\xB7  " + (disk.is_image ? "Image" : "Device");
            modern::text_clipped(subtitle,
                                 x() + 58,
                                 row_y + 30,
                                 w() - 84,
                                 16,
                                 modern::color(modern::Palette::kGray),
                                 10,
                                 false);
            if (disk.has_staged_changes) {
                modern::fill_circle(x() + w() - 20, row_y + 22, 4, modern::color(modern::Palette::kWarning));
            }
            continue;
        }

        /* Partition row */
        const partition_t &part = disk.partitions[row.slot];
        const bool selected = row.disk == selected_disk_ && row.slot == selected_slot_;
        if (selected) {
            draw_row_background(x(), w(), row_y, row.h, modern::color(modern::Palette::kSelection));
        } else if (hovered) {
            draw_row_background(x(), w(), row_y, row.h, modern::color(modern::Palette::kHover));
        }
        const Fl_Color chip = modern::slot_color(row.slot);
        modern::draw_drive(x() + 28, row_y + (row.h - 18) / 2, 18, 18, chip);
        char title[48];
        std::snprintf(title, sizeof(title), "Partition %d", row.slot);
        modern::text_clipped(title,
                             x() + 58,
                             row_y + 9,
                             w() - 80,
                             17,
                             modern::color(selected ? modern::Palette::kDark : modern::Palette::kDark2),
                             12,
                             selected);
        char subtitle[96];
        std::snprintf(subtitle,
                      sizeof(subtitle),
                      "%s  \xC2\xB7  %s",
                      disk_get_fs_type(part.type),
                      utils::format_size((uint64_t) part.size_sectors * DISK_SECTOR_SIZE).c_str());
        modern::text_clipped(subtitle,
                             x() + 58,
                             row_y + 26,
                             w() - 80,
                             15,
                             modern::color(modern::Palette::kGray),
                             10,
                             false);
    }

    fl_pop_clip();
    draw_scrollbar(x() + w() - 7, y() + kHeaderHeight, h() - kHeaderHeight);
}

int SidebarPanel::row_at(int py) const
{
    const int content_y = py - y() - kHeaderHeight + scroll_;
    if (content_y < 0) {
        return -1;
    }
    for (size_t index = 0; index < rows_.size(); ++index) {
        const Row &row = rows_[index];
        if (content_y >= row.y && content_y < row.y + row.h) {
            return (int) index;
        }
    }
    return -1;
}

int SidebarPanel::handle(int event)
{
    switch (event) {
    case FL_PUSH: {
        const int mx = Fl::event_x();
        const int my = Fl::event_y();
        if (Fl::event_button() == FL_LEFT_MOUSE) {
            if (mx >= x() + w() - 42 && mx <= x() + w() - 8 && my >= y() + 6 && my <= y() + 38) {
                if (on_add) {
                    on_add(Fl::event_x_root(), Fl::event_y_root());
                }
                return 1;
            }
            const int index = row_at(my);
            if (index >= 0) {
                const Row &row = rows_[index];
                selected_disk_ = row.disk;
                if (row.kind == RowKind::Partition) {
                    selected_slot_ = row.slot;
                }
                redraw();
                if (on_select) {
                    on_select(row.disk, row.kind == RowKind::Partition ? row.slot : -1);
                }
                return 1;
            }
        }
        return 1;
    }
    case FL_MOVE: {
        const int mx = Fl::event_x();
        const int my = Fl::event_y();
        const bool add_hover = mx >= x() + w() - 42 && mx <= x() + w() - 8 && my >= y() + 6 && my <= y() + 38;
        const int hover = row_at(my);
        if (hover != hover_ || add_hover != add_hover_) {
            hover_ = hover;
            add_hover_ = add_hover;
            redraw();
        }
        return 1;
    }
    case FL_LEAVE:
        if (hover_ != -1 || add_hover_) {
            hover_ = -1;
            add_hover_ = false;
            redraw();
        }
        return 1;
    case FL_MOUSEWHEEL:
        scroll_by(Fl::event_dy() * 40);
        return 1;
    default:
        break;
    }
    return Fl_Widget::handle(event);
}
