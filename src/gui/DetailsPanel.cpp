/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gui/DetailsPanel.hpp"

#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstdio>
#include <string>

#include "gui/ModernStyle.hpp"
#include "gui/Utils.hpp"

constexpr int kGap = 14;

static const char *action_label(DetailsPanel::Action action)
{
    switch (action) {
    case DetailsPanel::Action::Format:
        return "Format";
    case DetailsPanel::Action::Delete:
        return "Delete";
    case DetailsPanel::Action::Properties:
        return "Properties";
    }
    return "";
}

static modern::Glyph action_glyph(DetailsPanel::Action action)
{
    switch (action) {
    case DetailsPanel::Action::Format:
        return modern::Glyph::Format;
    case DetailsPanel::Action::Delete:
        return modern::Glyph::Delete;
    case DetailsPanel::Action::Properties:
        return modern::Glyph::Settings;
    }
    return modern::Glyph::More;
}

class DetailsActionButton : public Fl_Button {
public:
    DetailsActionButton(int x, int y, int w, int h, DetailsPanel::Action action, DetailsPanel *owner)
        : Fl_Button(x, y, w, h, action_label(action)), action_(action), owner_(owner)
    {
        box(FL_NO_BOX);
        callback(click_cb, this);
    }

    void draw() override
    {
        const bool enabled = active() != 0;
        const bool pressed = enabled && value() != 0;
        const bool hovered = enabled && Fl::belowmouse() == this;

        Fl_Color fill = modern::color(modern::kPanel);
        if (!enabled) {
            fill = modern::color(0xf3f5f8);
        } else if (pressed) {
            fill = modern::color(modern::kPressed);
        } else if (hovered) {
            fill = modern::color(modern::kHover);
        }
        const Fl_Color edge = modern::color(enabled ? (unsigned) modern::kButtonEdge
                                                    : (unsigned) modern::kDividerSoft);
        const Fl_Color foreground = modern::color(enabled ? (unsigned) modern::kDark2
                                                          : 0xb3bcc8u);

        modern::fill_round(x(), y(), w(), h(), 8, fill);
        modern::stroke_round(x(), y(), w(), h(), 8, edge, 1);
        if (Fl::focus() == this) {
            modern::stroke_round(x() + 1, y() + 1, w() - 2, h() - 2, 7, modern::color(modern::kAccent), 1);
        }
        modern::draw_glyph(action_glyph(action_), x() + 22, y() + h() / 2, 18, foreground);
        modern::text(label(), x() + 42, y() + h() / 2 + 5, foreground, 12, false);
    }

    int handle(int event) override
    {
        const int result = Fl_Button::handle(event);
        if (event == FL_ENTER || event == FL_LEAVE) {
            redraw();
        }
        return result;
    }

private:
    static void click_cb(Fl_Widget *widget, void *data)
    {
        (void) widget;
        auto *button = static_cast<DetailsActionButton *>(data);
        if (button->owner_ != nullptr && button->owner_->on_action) {
            button->owner_->on_action(button->action_);
        }
    }

    DetailsPanel::Action action_;
    DetailsPanel *owner_;
};

DetailsPanel::DetailsPanel(int x, int y, int w, int h) : Fl_Group(x, y, w, h)
{
    box(FL_NO_BOX);
    begin();
    for (int index = 0; index < 3; ++index) {
        buttons_[index] = new DetailsActionButton(0, 0, 10, 10, (Action) index, this);
    }
    end();
    layout_actions();
}

void DetailsPanel::resize(int x, int y, int w, int h)
{
    Fl_Group::resize(x, y, w, h);
    layout_actions();
}

void DetailsPanel::set_right_width(int width)
{
    right_width_ = width;
    layout_actions();
}

void DetailsPanel::layout_actions()
{
    const int rail_x = w() - right_width_;
    constexpr int kButtonHeight = 40;
    constexpr int kButtonGap = 7;
    for (int index = 0; index < 3; ++index) {
        buttons_[index]->resize(x() + rail_x,
                                y() + index * (kButtonHeight + kButtonGap),
                                right_width_,
                                kButtonHeight);
    }
}

bool DetailsPanel::overview_active() const
{
    return disk_ != nullptr && (slot_ < 0 || slot_ >= MAX_PART_COUNT || !disk_->partitions[slot_].active);
}

int DetailsPanel::handle(int event)
{
    if (overview_active()) {
        switch (event) {
        case FL_PUSH:
            if (Fl::event_button() == FL_LEFT_MOUSE && Fl::event_x() < x() + w() - right_width_) {
                for (const OverviewRow &row : overview_rows_) {
                    if (Fl::event_y() >= row.y && Fl::event_y() < row.y + row.h) {
                        if (on_select) {
                            on_select(row.slot);
                        }
                        return 1;
                    }
                }
            }
            break;
        case FL_MOVE: {
            int hover = -1;
            for (const OverviewRow &row : overview_rows_) {
                if (Fl::event_y() >= row.y && Fl::event_y() < row.y + row.h) {
                    hover = row.slot;
                }
            }
            if (hover != overview_hover_) {
                overview_hover_ = hover;
                redraw();
            }
            break;
        }
        case FL_LEAVE:
            if (overview_hover_ != -1) {
                overview_hover_ = -1;
                redraw();
            }
            break;
        default:
            break;
        }
    }
    return Fl_Group::handle(event);
}

void DetailsPanel::refresh(disk_info_t *disk, int slot)
{
    disk_ = disk;
    slot_ = slot;
    overview_hover_ = -1;
    overview_rows_.clear();
    const bool valid = disk_ != nullptr && slot_ >= 0 && slot_ < MAX_PART_COUNT &&
                       disk_->partitions[slot_].active;
    const bool enabled[3] = {valid, valid, disk_ != nullptr};
    for (int index = 0; index < 3; ++index) {
        if (enabled[index]) {
            buttons_[index]->activate();
        } else {
            buttons_[index]->deactivate();
        }
    }
    redraw();
}

void DetailsPanel::draw()
{
    const int card_w = w() - right_width_ - kGap;
    const int card_h = h();

    /* Partition card */
    modern::fill_round(x(), y(), card_w, card_h, 10, modern::color(modern::kPanel));
    modern::stroke_round(x(), y(), card_w, card_h, 10, modern::color(modern::kDivider), 1);

    const bool valid = disk_ != nullptr && slot_ >= 0 && slot_ < MAX_PART_COUNT &&
                       disk_->partitions[slot_].active;
    int info_top = y() + 96;
    if (!valid) {
        overview_rows_.clear();
        if (disk_ == nullptr) {
            modern::text("No disk selected",
                         x() + 24,
                         y() + 42,
                         modern::color(modern::kDark),
                         15,
                         true);
            modern::text("Open an image from the File menu.",
                         x() + 24,
                         y() + 64,
                         modern::color(modern::kGray),
                         11,
                         false);
            info_top = y() + 92;
        } else {
            /* Disk overview: shown when the disk header is selected instead of
             * one of its partitions. */
            modern::draw_drive(x() + 22, y() + 16, 40, 40, modern::color(modern::kDark2));
            modern::text("Disk overview", x() + 80, y() + 37, modern::color(modern::kDark), 15, true);
            int used_slots = 0;
            for (int index = 0; index < MAX_PART_COUNT; ++index) {
                if (disk_->partitions[index].active) {
                    ++used_slots;
                }
            }
            char subtitle[160];
            std::snprintf(subtitle,
                          sizeof(subtitle),
                          "%s  \xC2\xB7  %s  \xC2\xB7  %d of %d slots used",
                          utils::format_size(disk_->size_bytes).c_str(),
                          disk_->has_mbr ? "MBR" : "Raw",
                          used_slots,
                          MAX_PART_COUNT);
            modern::text_clipped(subtitle,
                                 x() + 80,
                                 y() + 57,
                                 card_w - 104,
                                 16,
                                 modern::color(modern::kGray),
                                 11,
                                 false);
            fl_color(modern::color(modern::kDividerSoft));
            fl_line(x() + 20, y() + 78, x() + card_w - 20, y() + 78);

            int row_y = y() + 92;
            if (used_slots == 0) {
                modern::text("No partitions yet. Use + to create one.",
                             x() + 24,
                             row_y + 15,
                             modern::color(modern::kGray),
                             11,
                             false);
            }
            for (int index = 0; index < MAX_PART_COUNT; ++index) {
                const partition_t &part = disk_->partitions[index];
                if (!part.active) {
                    continue;
                }
                if (overview_hover_ == index) {
                    modern::fill_round(x() + 12, row_y - 2, card_w - 24, 22, 6, modern::color(modern::kHover));
                }
                modern::fill_round(x() + 24, row_y + 4, 10, 10, 3, modern::slot_color(index));
                char title[32];
                std::snprintf(title, sizeof(title), "Partition %d", index);
                modern::text(title, x() + 44, row_y + 15, modern::color(modern::kDark2), 11, true);
                modern::text(disk_get_fs_type(part.type),
                             x() + 160,
                             row_y + 15,
                             modern::color(modern::kGray),
                             11,
                             false);
                modern::text_clipped(utils::format_size((uint64_t) part.size_sectors * DISK_SECTOR_SIZE),
                                     x() + 250,
                                     row_y + 3,
                                     card_w - 274,
                                     16,
                                     modern::color(modern::kGray),
                                     11,
                                     false,
                                     FL_ALIGN_RIGHT | FL_ALIGN_CLIP);
                overview_rows_.push_back({index, row_y - 2, 22});
                row_y += 22;
            }
        }
    } else {
        const partition_t &part = disk_->partitions[slot_];
        const Fl_Color chip = modern::slot_color(slot_);
        modern::fill_round(x() + 20, y() + 18, 44, 44, 9, chip);
        modern::draw_glyph(modern::Glyph::Drive, x() + 42, y() + 40, 24, FL_WHITE);

        char title[48];
        std::snprintf(title, sizeof(title), "Partition %d", slot_);
        modern::text(title, x() + 80, y() + 37, modern::color(modern::kDark), 15, true);
        const std::string subtitle =
            std::string(disk_get_fs_type(part.type)) + "  \xC2\xB7  " +
            utils::format_size((uint64_t) part.size_sectors * DISK_SECTOR_SIZE);
        modern::text_clipped(subtitle,
                             x() + 80,
                             y() + 57,
                             card_w - 104,
                             16,
                             modern::color(modern::kGray),
                             11,
                             false);
        fl_color(modern::color(modern::kDividerSoft));
        fl_line(x() + 20, y() + 78, x() + card_w - 20, y() + 78);
    }

    if (disk_ != nullptr && valid) {
        const partition_t &part = disk_->partitions[slot_];
        const uint64_t size_bytes = (uint64_t) part.size_sectors * DISK_SECTOR_SIZE;
        uint64_t used = 0;
        uint64_t total = 0;
        int percent = -1;
        if (part.type == ZEALFS_TYPE && disk_fs_usage(disk_, slot_, &used, &total) == 0) {
            percent = utils::percent_of(used, total);
        }

        char used_value[64] = "-";
        char available_value[64] = "-";
        if (percent >= 0) {
            std::snprintf(used_value, sizeof(used_value), "%s  (%d%%)", utils::format_size(used).c_str(), percent);
            std::snprintf(available_value,
                          sizeof(available_value),
                          "%s",
                          utils::format_size(total >= used ? total - used : 0).c_str());
        }

        struct Pair {
            const char *label;
            std::string value;
            Fl_Color color;
        };
        char start_value[32];
        std::snprintf(start_value,
                      sizeof(start_value),
                      "0x%08llX",
                      (unsigned long long) part.start_lba * (unsigned long long) DISK_SECTOR_SIZE);
        char type_value[16];
        std::snprintf(type_value, sizeof(type_value), "0x%02X", (unsigned) part.type);
        const char *state = disk_->has_staged_changes ? "Staged changes" : "Written to disk";
        const Fl_Color state_color =
            disk_->has_staged_changes ? modern::color(modern::kWarning) : modern::color(modern::kSuccess);

        Pair left[] = {
            {"File system:", disk_get_fs_type(part.type), modern::color(modern::kDark2)},
            {"Total size:", utils::format_size(size_bytes), modern::color(modern::kDark2)},
            {"Used:", used_value, modern::color(modern::kDark2)},
            {"Available:", available_value, modern::color(modern::kDark2)},
        };
        Pair right[] = {
            {"Start address:", start_value, modern::color(modern::kDark2)},
            {"Partition slot:", std::to_string(slot_), modern::color(modern::kDark2)},
            {"Type byte:", type_value, modern::color(modern::kDark2)},
            {"Status:", state, state_color},
        };

        const int row_h = 24;
        const int col1_x = x() + 24;
        const int val1_x = x() + 118;
        const int col2_x = x() + card_w / 2 + 8;
        const int val2_x = col2_x + 108;
        for (int row = 0; row < 4; ++row) {
            const int by = info_top + 8 + row * row_h;
            modern::text(left[row].label, col1_x, by, modern::color(modern::kGray), 11, false);
            modern::text_clipped(left[row].value,
                                 val1_x,
                                 by - 12,
                                 col2_x - val1_x - 12,
                                 17,
                                 left[row].color,
                                 11,
                                 false);
            modern::text(right[row].label, col2_x, by, modern::color(modern::kGray), 11, false);
            modern::text_clipped(right[row].value,
                                 val2_x,
                                 by - 12,
                                 x() + card_w - 20 - val2_x,
                                 17,
                                 right[row].color,
                                 11,
                                 false);
        }
    }

    Fl_Group::draw();
}
