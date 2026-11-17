/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gui/FileBrowserPanel.hpp"

#include <FL/Fl.H>
#include <FL/Fl_Tooltip.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstdio>
#include <string>

#include "gui/FileBrowserPanelInternal.hpp"
#include "gui/MainWindow.hpp"
#include "gui/ModernStyle.hpp"
#include "gui/Utils.hpp"

using namespace fbp;

void FileBrowserPanel::draw_scrollbar(int sx, int sy, int sw, int sh, int total, int visible, int offset)
{
    if (total <= visible || sh <= 0) {
        return;
    }
    modern::fill_round(sx, sy, sw, sh, sw / 2, modern::color(modern::kDividerSoft));
    const int thumb_h = std::max(24, sh * visible / total);
    const int max_scroll = std::max(1, total - visible);
    const int thumb_y = sy + (sh - thumb_h) * offset / max_scroll;
    modern::fill_round(sx, thumb_y, sw, thumb_h, sw / 2, modern::color(0xb9c3d1));
}

void FileBrowserPanel::draw_navigation()
{
    const int nav_y = y() + 4;
    const bool can_back = history_index_ > 0;
    const bool can_forward = history_index_ + 1 < history_.size();
    const bool can_up = target_valid_ && path_ != "/";
    const bool can_home = target_valid_ && path_ != "/";

    struct NavButton {
        int x;
        modern::Glyph glyph;
        bool enabled;
        HitKind kind;
    };
    const NavButton buttons[] = {
        {nav_back_x_, modern::Glyph::Back, can_back, HitKind::Back},
        {nav_forward_x_, modern::Glyph::Forward, can_forward, HitKind::Forward},
        {nav_up_x_, modern::Glyph::Up, can_up, HitKind::Up},
        {nav_home_x_, modern::Glyph::Home, can_home, HitKind::Home},
    };
    for (const NavButton &button : buttons) {
        const bool hovered = hover_kind_ == (int) button.kind;
        if (hovered && button.enabled) {
            modern::fill_round(button.x - 1, nav_y, 30, 30, 7, modern::color(modern::kHover));
        }
        modern::draw_glyph(button.glyph,
                           button.x + 14,
                           nav_y + 15,
                           16,
                           modern::color(button.enabled ? (unsigned) modern::kIconDark : 0xc5ccd6u));
    }

    crumbs_.clear();
    crumbs_.push_back({"Root", "/", 0, 0});
    std::string prefix;
    std::string remaining = path_;
    size_t cursor = 0;
    while (cursor < remaining.size()) {
        while (cursor < remaining.size() && remaining[cursor] == '/') {
            ++cursor;
        }
        const size_t start = cursor;
        while (cursor < remaining.size() && remaining[cursor] != '/') {
            ++cursor;
        }
        if (start == cursor) {
            break;
        }
        prefix += "/";
        prefix += remaining.substr(start, cursor - start);
        crumbs_.push_back({remaining.substr(start, cursor - start), prefix, 0, 0});
    }

    const int crumb_start = x() + 140;
    const int crumb_end = preview_x_ - 10;
    const int available = std::max(0, crumb_end - crumb_start);
    int total_width = 0;
    for (size_t index = 0; index < crumbs_.size(); ++index) {
        const bool last = index + 1 == crumbs_.size();
        int width = modern::text_width(crumbs_[index].name, 11, last);
        if (last) {
            width += 18;
        }
        if (index > 0) {
            total_width += kCrumbSeparator;
        }
        total_width += width;
    }
    size_t first = 0;
    while (first + 1 < crumbs_.size() && total_width > available) {
        const size_t index = first;
        const bool last = index + 1 == crumbs_.size();
        int width = modern::text_width(crumbs_[index].name, 11, last);
        if (last) {
            width += 18;
        }
        total_width -= width + kCrumbSeparator;
        ++first;
    }
    int bx = crumb_start;
    if (first > 0) {
        modern::draw_glyph(modern::Glyph::More, bx + 7, nav_y + 15, 14, modern::color(modern::kGray2));
        bx += 18;
        total_width += 6;
    }
    for (size_t index = first; index < crumbs_.size(); ++index) {
        Crumb &crumb = crumbs_[index];
        const bool last = index + 1 == crumbs_.size();
        if (index > first) {
            modern::draw_glyph(modern::Glyph::Forward,
                               bx + kCrumbSeparator / 2,
                               nav_y + 15,
                               12,
                               modern::color(modern::kGray2));
            bx += kCrumbSeparator;
        }
        const bool hovered = hover_kind_ == (int) HitKind::Crumb && hover_index_ == (int) index;
        if (last) {
            const int width = modern::text_width(crumb.name, 11, true) + 18;
            crumb.x = bx;
            crumb.w = width;
            modern::fill_round(bx,
                               nav_y + 1,
                               width,
                               28,
                               7,
                               modern::color(hovered ? 0xd7e8fbu : (unsigned) modern::kAccentSoft));
            modern::text(crumb.name, bx + 9, nav_y + 20, modern::color(modern::kAccent), 11, true);
            bx += width;
        } else {
            const int width = modern::text_width(crumb.name, 11, false);
            crumb.x = bx;
            crumb.w = width;
            modern::text(crumb.name,
                         bx,
                         nav_y + 20,
                         modern::color(hovered ? modern::kAccent : modern::kDark),
                         11,
                         false);
            if (hovered) {
                fl_color(modern::color(modern::kAccent));
                fl_line(bx, nav_y + 23, bx + width, nav_y + 23);
            }
            bx += width;
        }
        if (bx > crumb_end) {
            break;
        }
    }
}

void FileBrowserPanel::draw_actions()
{
    const int action_y = y() + nav_h_ + 4;
    const bool selected = has_selection();
    const bool target = target_valid_;
    struct ActionButton {
        int x;
        int w;
        const char *label;
        modern::Glyph glyph;
        bool enabled;
        HitKind kind;
    };
    const ActionButton buttons[] = {
        {import_x_, import_w_, "Import", modern::Glyph::Import, target, HitKind::Import},
        {export_x_, export_w_, "Export", modern::Glyph::Export, target && selected, HitKind::Export},
        {new_folder_x_, new_folder_w_, "New folder", modern::Glyph::NewFolder, target, HitKind::NewFolder},
        {delete_x_, delete_w_, "Delete", modern::Glyph::Delete, target && selected, HitKind::Delete},
    };
    for (const ActionButton &button : buttons) {
        const bool hovered = hover_kind_ == (int) button.kind;
        if (hovered && button.enabled) {
            modern::fill_round(button.x - 2, action_y, button.w + 4, 28, 7, modern::color(modern::kHover));
        }
        const Fl_Color fg = modern::color(button.enabled ? (unsigned) modern::kDark2 : 0xb8c0ccu);
        modern::draw_glyph(button.glyph, button.x + 10, action_y + 14, 16, fg);
        modern::text(button.label, button.x + 24, action_y + 19, fg, 11, false);
    }

    char count[64];
    if (!target) {
        count[0] = 0;
    } else if (filter_.empty()) {
        std::snprintf(count, sizeof(count), "%d item%s", (int) order_.size(), order_.size() == 1 ? "" : "s");
    } else {
        std::snprintf(count,
                      sizeof(count),
                      "%d of %d item%s",
                      (int) order_.size(),
                      (int) count_,
                      count_ == 1 ? "" : "s");
    }
    if (count[0] != 0) {
        modern::text_clipped(count,
                             preview_x_ - 180,
                             action_y + 8,
                             168,
                             18,
                             modern::color(modern::kGray2),
                             10,
                             false,
                             FL_ALIGN_RIGHT | FL_ALIGN_CLIP);
    }

    fl_color(modern::color(modern::kDividerSoft));
    fl_line(x(), y() + nav_h_ + actions_h_ + 1, preview_x_ - 12, y() + nav_h_ + actions_h_ + 1);
}

void FileBrowserPanel::draw_tree()
{
    const int body_top = body_y_;
    const int body_bottom = y() + h() - 4;
    if (body_bottom <= body_top) {
        return;
    }
    fl_push_clip(x(), body_top, tree_w_, body_bottom - body_top);
    if (!target_valid_) {
        modern::text_clipped("Select a ZealFS partition",
                             x() + 12,
                             body_top + 18,
                             tree_w_ - 24,
                             40,
                             modern::color(modern::kGray2),
                             10,
                             false,
                             FL_ALIGN_LEFT | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
        fl_pop_clip();
        return;
    }

    const int first = tree_scroll_ / kTreeRowHeight;
    const int row_y0 = body_top + 6 - (tree_scroll_ % kTreeRowHeight);
    for (int index = first; index < (int) tree_rows_.size(); ++index) {
        const int y = row_y0 + (index - first) * kTreeRowHeight;
        if (y > body_bottom) {
            break;
        }
        const TreeRow &row = tree_rows_[index];
        const bool hovered = hover_kind_ == (int) HitKind::TreeRow && hover_index_ == index;
        if (row.current) {
            modern::fill_round(x() + 5, y, tree_w_ - 12, kTreeRowHeight - 2, 6,
                               modern::color(modern::kSelection));
        } else if (hovered) {
            modern::fill_round(x() + 5, y, tree_w_ - 12, kTreeRowHeight - 2, 6, modern::color(modern::kHover));
        }
        const int indent = 16 + row.depth * 14;
        if (row.has_children) {
            const bool expanded = collapsed_.find(row.path) == collapsed_.end();
            modern::draw_glyph(expanded ? modern::Glyph::Down : modern::Glyph::Forward,
                               x() + indent - 9,
                               y + kTreeRowHeight / 2 - 1,
                               10,
                               modern::color(row.current ? modern::kAccent : modern::kGray2));
        }
        if (row.depth == 0) {
            modern::draw_drive(x() + indent, y + 6, 15, 15,
                               modern::color(row.current ? modern::kAccent : modern::kGray2));
        } else {
            modern::draw_folder(x() + indent, y + 7, 15, 13);
        }
        modern::text_clipped(row.name,
                             x() + indent + 22,
                             y + 6,
                             tree_w_ - indent - 32,
                             16,
                             modern::color(row.current ? modern::kDark : modern::kDark2),
                             11,
                             row.current);
    }
    if (tree_rows_.size() == 1) {
        modern::text("No subfolders",
                     x() + 34,
                     row_y0 + kTreeRowHeight + 10,
                     modern::color(modern::kGray2),
                     10,
                     false);
    }
    fl_pop_clip();

    draw_scrollbar(x() + tree_w_ - 8,
                   body_top + 4,
                   4,
                   body_bottom - body_top - 8,
                   (int) tree_rows_.size() * kTreeRowHeight,
                   body_bottom - body_top - 8,
                   tree_scroll_);
    fl_color(modern::color(modern::kDividerSoft));
    fl_line(x() + tree_w_, body_top, x() + tree_w_, body_bottom);
}

void FileBrowserPanel::draw_table()
{
    const Columns columns = table_columns(table_x_, table_w_);
    const int body_top = body_y_;
    const int body_bottom = y() + h() - 4;
    const int list_top = body_top + kTableHeader;
    const int list_height = std::max(1, body_bottom - list_top - 2);

    modern::text("Name", table_x_ + 10, body_top + 20, modern::color(modern::kGray), 10, false);
    modern::text("Size", columns.size_x + 4, body_top + 20, modern::color(modern::kGray), 10, false);
    modern::text("Type", columns.type_x + 4, body_top + 20, modern::color(modern::kGray), 10, false);
    modern::text("Modified", columns.mod_x + 4, body_top + 20, modern::color(modern::kGray), 10, false);

    const int header_x[] = {table_x_ + 10, columns.size_x + 4, columns.type_x + 4, columns.mod_x + 4};
    for (int column = 0; column < 4; ++column) {
        if (sort_column_ != column) {
            continue;
        }
        const char *label = column == 0 ? "Name" : column == 1 ? "Size" : column == 2 ? "Type" : "Modified";
        const int tx = header_x[column] + modern::text_width(label, 10) + 6;
        modern::draw_glyph(descending_ ? modern::Glyph::Down : modern::Glyph::Up,
                           tx + 4,
                           body_top + 15,
                           10,
                           modern::color(modern::kAccent));
    }
    fl_color(modern::color(modern::kDividerSoft));
    fl_line(table_x_, body_top + kTableHeader - 1, table_x_ + table_w_, body_top + kTableHeader - 1);

    fl_push_clip(table_x_, list_top, table_w_, list_height);
    if (!target_valid_) {
        modern::text_center("Select a ZealFS partition to browse its files",
                            table_x_ + table_w_ / 2,
                            list_top + 34,
                            modern::color(modern::kGray2),
                            11,
                            false);
    } else if (order_.empty()) {
        modern::text_center(filter_.empty() ? "This folder is empty" : "No matching entries",
                            table_x_ + table_w_ / 2,
                            list_top + 34,
                            modern::color(modern::kGray2),
                            11,
                            false);
    } else {
        const int first = table_scroll_ / kRowHeight;
        const int row_y0 = list_top - (table_scroll_ % kRowHeight);
        for (int index = first; index < (int) order_.size(); ++index) {
            const int y = row_y0 + (index - first) * kRowHeight;
            if (y > body_bottom) {
                break;
            }
            const bool selected = is_selected(order_[index]);
            const bool current = selected_row_ == index;
            const bool hovered = hover_kind_ == (int) HitKind::TableRow && hover_index_ == index;
            if (selected) {
                modern::fill_round(table_x_ + 4, y + 1, table_w_ - 16, kRowHeight - 2, 5,
                                   modern::color(modern::kSelection));
            } else if (hovered) {
                modern::fill_round(table_x_ + 4, y + 1, table_w_ - 16, kRowHeight - 2, 5,
                                   modern::color(modern::kHover));
            }
            const zfs_entry &entry = entries_[order_[index]];
            const int icon_x = table_x_ + 12;
            modern::draw_file(entry.directory ? modern::FileKind::Folder : modern::file_kind(entry.name, false),
                              icon_x,
                              y + 5,
                              18,
                              18);
            modern::text_clipped(entry.name,
                                 icon_x + 26,
                                 y + 7,
                                 columns.size_x - icon_x - 30,
                                 16,
                                 modern::color(selected ? modern::kDark : modern::kDark2),
                                 11,
                                 current);
            if (!entry.directory) {
                modern::text(utils::format_size(entry.size, true),
                             columns.size_x + 4,
                             y + 18,
                             modern::color(modern::kGray),
                             10,
                             false);
            } else {
                modern::text("-", columns.size_x + 4, y + 18, modern::color(modern::kGray2), 10, false);
            }
            modern::text(type_name(entry),
                         columns.type_x + 4,
                         y + 18,
                         modern::color(modern::kGray),
                         10,
                         false);
            modern::text(timestamp(entry),
                         columns.mod_x + 4,
                         y + 18,
                         modern::color(modern::kGray),
                         10,
                         false);
            fl_color(modern::color(0xeef1f5));
            fl_line(table_x_ + 8, y + kRowHeight - 1, table_x_ + table_w_ - 12, y + kRowHeight - 1);
        }
    }
    fl_pop_clip();

    if (Fl::focus() == this && target_valid_) {
        modern::stroke_round(table_x_ + 2,
                             body_top + 2,
                             table_w_ - 4,
                             body_bottom - body_top - 4,
                             6,
                             modern::color(modern::kAccentEdge),
                             1);
    }

    draw_scrollbar(table_x_ + table_w_ - 7,
                   list_top + 2,
                   4,
                   list_height - 4,
                   (int) order_.size() * kRowHeight,
                   list_height,
                   table_scroll_);
}

void FileBrowserPanel::draw_preview()
{
    const int px = preview_x_ + 6;
    const int pw = preview_w_ - 12;
    const int py = body_y_;
    const int ph = y() + h() - 4 - py;
    if (pw < 80 || ph < 100) {
        preview_action_w_ = 0;
        return;
    }
    modern::fill_round(px, py, pw, ph, 10, modern::color(modern::kPanel));
    modern::stroke_round(px, py, pw, ph, 10, modern::color(modern::kDivider), 1);

    const int entry_index = selected_entry();
    if (!target_valid_) {
        modern::text("No partition selected",
                     px + 20,
                     py + 38,
                     modern::color(modern::kDark),
                     13,
                     true);
        modern::text("Choose a partition in the sidebar,",
                     px + 20,
                     py + 58,
                     modern::color(modern::kGray2),
                     10,
                     false);
        modern::text("the map or the disk overview.",
                     px + 20,
                     py + 74,
                     modern::color(modern::kGray2),
                     10,
                     false);
        preview_action_w_ = 0;
        return;
    }
    if (entry_index < 0) {
        modern::text("No file selected",
                     px + 20,
                     py + 38,
                     modern::color(modern::kDark),
                     13,
                     true);
        modern::text("Select a file from the list",
                     px + 20,
                     py + 58,
                     modern::color(modern::kGray2),
                     10,
                     false);
        modern::text("to see its details here.",
                     px + 20,
                     py + 74,
                     modern::color(modern::kGray2),
                     10,
                     false);
        preview_action_w_ = 0;
        return;
    }

    if (multi_count_ > 1) {
        modern::text(std::to_string(multi_count_) + " items selected",
                     px + 20,
                     py + 38,
                     modern::color(modern::kDark),
                     13,
                     true);
        int summary_y = py + 64;
        for (const std::string &line : preview_lines_) {
            modern::text(line, px + 20, summary_y, modern::color(modern::kGray), 10, false);
            summary_y += 18;
        }
        preview_action_w_ = 0;
        return;
    }

    const zfs_entry &entry = entries_[entry_index];
    const int icon_x = px + 16;
    const int icon_y = py + 16;
    if (entry.directory) {
        modern::draw_folder(icon_x, icon_y + 2, 42, 34);
    } else {
        modern::draw_file(modern::file_kind(entry.name, false), icon_x, icon_y, 40, 42);
    }
    modern::text_clipped(entry.name,
                         icon_x + 54,
                         py + 18,
                         pw - 78,
                         36,
                         modern::color(modern::kDark),
                         13,
                         true,
                         FL_ALIGN_LEFT | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
    modern::text(std::string("Type: ") + (entry.directory ? "Folder" : type_name(entry)),
                 px + 16,
                 py + 76,
                 modern::color(modern::kGray),
                 10,
                 false);
    modern::text(std::string("Size: ") + (entry.directory ? "-" : utils::format_size(entry.size, true)),
                 px + 16,
                 py + 92,
                 modern::color(modern::kGray),
                 10,
                 false);
    modern::text(std::string("Modified: ") + timestamp(entry),
                 px + 16,
                 py + 108,
                 modern::color(modern::kGray),
                 10,
                 false);
    fl_color(modern::color(modern::kDividerSoft));
    fl_line(px + 14, py + 120, px + pw - 14, py + 120);

    int text_y = py + 138;
    const int text_bottom = py + ph - 58;
    if (preview_kind_ == 2) {
        /* Folders have no inline preview: the header and the action button speak
         * for themselves. */
    } else if (preview_kind_ == 3) {
        modern::text_clipped(preview_message_,
                             px + 16,
                             text_y - 4,
                             pw - 32,
                             text_bottom - text_y + 8,
                             modern::color(modern::kGray),
                             10,
                             false,
                             FL_ALIGN_LEFT | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
    } else if (!preview_message_.empty() && preview_lines_.empty()) {
        modern::text(preview_message_, px + 16, text_y, modern::color(modern::kGray), 10, false);
    } else {
        for (const std::string &line : preview_lines_) {
            if (text_y > text_bottom) {
                modern::text("...", px + 16, text_y, modern::color(modern::kGray2), 10, false);
                break;
            }
            modern::text_clipped(line,
                                 px + 16,
                                 text_y - 9,
                                 pw - 32,
                                 15,
                                 modern::color(modern::kDark2),
                                 10,
                                 false);
            text_y += 15;
        }
    }

    preview_action_x_ = px + 14;
    preview_action_y_ = py + ph - 44;
    preview_action_w_ = pw - 28;
    preview_action_h_ = 30;
    const bool hovered = hover_kind_ == (int) HitKind::PreviewAction;
    modern::fill_round(preview_action_x_,
                       preview_action_y_,
                       preview_action_w_,
                       preview_action_h_,
                       7,
                       modern::color(hovered ? modern::kHover : modern::kPanel));
    modern::stroke_round(preview_action_x_,
                         preview_action_y_,
                         preview_action_w_,
                         preview_action_h_,
                         7,
                         modern::color(modern::kButtonEdge),
                         1);
    modern::draw_glyph(entry.directory ? modern::Glyph::FolderOpen : modern::Glyph::Export,
                       preview_action_x_ + 18,
                       preview_action_y_ + preview_action_h_ / 2,
                       16,
                       modern::color(modern::kDark2));
    modern::text(entry.directory ? "Open folder" : "Export...",
                 preview_action_x_ + 34,
                 preview_action_y_ + 20,
                 modern::color(modern::kDark2),
                 11,
                 false);
}

void FileBrowserPanel::draw()
{
    fl_push_clip(x(), y(), w(), h());
    fl_color(modern::color(modern::kWindow));
    fl_rectf(x(), y(), w(), h());

    draw_navigation();
    modern::draw_glyph(modern::Glyph::Search,
                       preview_x_ + 18,
                       y() + 21,
                       14,
                       modern::color(modern::kGray2));
    draw_actions();

    const int card_w = preview_x_ - 12 - x();
    const int body_h = y() + h() - 4 - body_y_;
    modern::fill_round(x(), body_y_, card_w, body_h, 10, modern::color(modern::kPanel));
    modern::stroke_round(x(), body_y_, card_w, body_h, 10, modern::color(modern::kDivider), 1);

    draw_tree();
    fl_color(modern::color(modern::kDividerSoft));
    fl_line(table_x_ - 10, body_y_, table_x_ - 10, y() + h() - 4);
    draw_table();
    draw_preview();
    if (drop_active_) {
        const int body_bottom = y() + h() - 4;
        modern::stroke_round(x() + 2,
                             body_y_ + 2,
                             w() - 4,
                             body_bottom - body_y_ - 4,
                             9,
                             modern::color(modern::kAccent),
                             2);
        const int label_w = 200;
        const int label_x = x() + (w() - label_w) / 2;
        const int label_y = body_y_ + 8;
        modern::fill_round(label_x, label_y, label_w, 28, 8, modern::color(modern::kAccent));
        modern::text_center("Drop files to import",
                            label_x + label_w / 2,
                            label_y + 19,
                            modern::color(modern::kPanel),
                            11,
                            true);
    }
    fl_pop_clip();

    Fl_Group::draw();

    /* FLTK has no placeholder support: paint a hint inside the empty field. */
    if (search_ != nullptr && filter_.empty() && Fl::focus() != search_) {
        fl_push_clip(search_->x() + 2, search_->y() + 2, search_->w() - 4, search_->h() - 4);
        modern::text("Search this folder",
                     search_->x() + 7,
                     search_->y() + search_->h() / 2 + 4,
                     modern::color(modern::kGray2),
                     11,
                     false);
        fl_pop_clip();
    }
}

