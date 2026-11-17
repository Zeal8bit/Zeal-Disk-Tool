/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gui/HostBrowserPanel.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <FL/Fl.H>
#include <FL/Fl_Tooltip.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>

#include "gui/FileBrowserPanel.hpp"
#include "gui/MainWindow.hpp"
#include "gui/ModernStyle.hpp"
#include "gui/Utils.hpp"

namespace fs = std::filesystem;

constexpr int kRowHeight = 28;
constexpr int kTableHeader = 30;

/* Window-relative point test; Fl_Widget only offers whole-widget containment. */
static bool point_inside(const Fl_Widget *widget, int x, int y)
{
    return widget != nullptr && x >= widget->x() && x < widget->x() + widget->w() && y >= widget->y() &&
           y < widget->y() + widget->h();
}

static std::string lower_copy(const std::string &value)
{
    std::string result = value;
    for (char &c : result) {
        c = (char) std::tolower((unsigned char) c);
    }
    return result;
}

static std::time_t to_time_t(fs::file_time_type value)
{
    const auto system = std::chrono::system_clock::now() + (value - fs::file_time_type::clock::now());
    return std::chrono::system_clock::to_time_t(system);
}

static fs::path home_directory()
{
#ifdef _WIN32
    const char *home = std::getenv("USERPROFILE");
#else
    const char *home = std::getenv("HOME");
#endif
    std::error_code ec;
    if (home != nullptr && *home != '\0') {
#ifdef _WIN32
        const fs::path path = fs::path(home);
#else
        const fs::path path = fs::u8path(home);
#endif
        if (fs::is_directory(path, ec)) {
            return path;
        }
    }
    const fs::path current = fs::current_path(ec);
    return ec ? fs::path("/") : current;
}

HostBrowserPanel::HostBrowserPanel(int x, int y, int w, int h, MainWindow *owner)
    : Fl_Group(x, y, w, h), owner_(owner)
{
    box(FL_NO_BOX);
    path_ = home_directory();
    begin();
    search_ = new Fl_Input(0, 0, 10, 10);
    search_->box(FL_DOWN_BOX);
    search_->textsize(12);
    search_->textfont(FL_HELVETICA);
    search_->textcolor(modern::color(modern::Palette::kDark));
    search_->cursor_color(modern::color(modern::Palette::kAccent));
    search_->when(FL_WHEN_CHANGED);
    search_->callback(search_cb, this);
    search_->tooltip("Filter the host folder");
    end();
    history_.push_back(path_);
    rebuild_places();
    layout_geometry();
    layout_search();
    reload();
}

HostBrowserPanel::~HostBrowserPanel() = default;

void HostBrowserPanel::resize(int x, int y, int w, int h)
{
    Fl_Group::resize(x, y, w, h);
    layout_geometry();
    layout_search();
    redraw();
}

void HostBrowserPanel::layout_geometry()
{
    nav_h_ = 38;
    actions_h_ = 34;
    body_y_ = y() + nav_h_ + actions_h_ + 2;
    places_w_ = w() >= 430 ? std::min(150, std::max(120, w() / 4)) : 0;
    table_x_ = x() + places_w_ + (places_w_ > 0 ? 10 : 0);
    table_w_ = std::max(120, x() + w() - table_x_);

    nav_back_x_ = x() + 4;
    nav_up_x_ = x() + 36;
    nav_home_x_ = x() + 68;

    new_folder_w_ = 22 + modern::text_width("New folder", 11) + 20;
    rename_w_ = 22 + modern::text_width("Rename", 11) + 20;
    delete_w_ = 22 + modern::text_width("Delete", 11) + 20;
    copy_w_ = 22 + modern::text_width("To disk", 11) + 20;
    new_folder_x_ = x() + 4;
    rename_x_ = new_folder_x_ + new_folder_w_ + 4;
    delete_x_ = rename_x_ + rename_w_ + 4;
    copy_x_ = delete_x_ + delete_w_ + 4;
}

void HostBrowserPanel::layout_search()
{
    if (search_ == nullptr) {
        return;
    }
    const int field_w = std::min(220, std::max(140, w() / 3));
    search_->resize(x() + w() - field_w - 8, y() + 7, field_w, 26);
}

void HostBrowserPanel::search_cb(Fl_Widget *, void *data)
{
    auto *panel = static_cast<HostBrowserPanel *>(data);
    panel->filter_ = panel->search_->value();
    if (panel->filter_.find_first_not_of(" \t") == std::string::npos) {
        panel->filter_.clear();
    }
    panel->rebuild_order();
    panel->selected_row_ = -1;
    panel->selection_.clear();
    panel->table_scroll_ = 0;
    panel->redraw();
}

void HostBrowserPanel::focus_search()
{
    if (search_ != nullptr) {
        Fl::focus(search_);
        search_->take_focus();
        redraw();
    }
}

void HostBrowserPanel::clear_search()
{
    if (search_ == nullptr || filter_.empty()) {
        return;
    }
    filter_.clear();
    search_->value("");
    rebuild_order();
    redraw();
}

void HostBrowserPanel::rebuild_places()
{
    places_.clear();
    const fs::path home = home_directory();
    places_.push_back({"Home", home});
    places_.push_back({"Desktop", home / "Desktop"});
    places_.push_back({"Downloads", home / "Downloads"});
    places_.push_back({"Documents", home / "Documents"});
    std::error_code ec;
    const fs::path root = path_.root_path();
    places_.push_back({"Root", root.empty() ? fs::path("/") : root});
#ifdef _WIN32
    const unsigned drives = GetLogicalDrives();
    for (int index = 0; index < 26; ++index) {
        if ((drives & (1u << index)) != 0) {
            const fs::path drive = fs::u8path(std::string(1, (char) ('A' + index)) + ":\\");
            if (fs::exists(drive, ec)) {
                places_.push_back({std::string(1, (char) ('A' + index)) + ":", drive});
            }
        }
    }
#endif
    std::vector<Place> existing;
    for (const Place &place : places_) {
        if (fs::is_directory(place.path, ec)) {
            existing.push_back(place);
        }
    }
    places_ = std::move(existing);
}

void HostBrowserPanel::reload()
{
    entries_.clear();
    selection_.clear();
    selected_row_ = -1;
    anchor_row_ = -1;
    path_string_ = path_.u8string();
    std::error_code ec;
    if (!fs::is_directory(path_, ec)) {
        if (owner_ != nullptr) {
            owner_->set_status("The host folder is not accessible");
        }
        rebuild_order();
        rebuild_crumbs();
        redraw();
        return;
    }
    for (fs::directory_iterator it(path_, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entry_ec;
        Entry entry;
        entry.name = it->path().filename().u8string();
        entry.directory = it->is_directory(entry_ec);
        if (!entry.directory) {
            entry.size = (uint64_t) it->file_size(entry_ec);
            if (entry_ec) {
                entry.size = 0;
            }
        }
        const auto modified = it->last_write_time(entry_ec);
        entry.modified = entry_ec ? 0 : to_time_t(modified);
        entries_.push_back(entry);
    }
    if (ec && owner_ != nullptr) {
        owner_->set_status("Could not read the whole host folder");
    }
    rebuild_order();
    rebuild_crumbs();
    redraw();
}

void HostBrowserPanel::rebuild_crumbs()
{
    crumbs_.clear();
    std::vector<fs::path> stack;
    for (fs::path current = path_;; current = current.parent_path()) {
        stack.push_back(current);
        if (current.parent_path() == current) {
            break;
        }
    }
    std::reverse(stack.begin(), stack.end());
    for (const fs::path &path : stack) {
        Crumb crumb;
        crumb.path = path;
        crumb.label = path.filename().u8string();
        if (crumb.label.empty()) {
            crumb.label = path.root_name().u8string() + path.root_directory().u8string();
        }
        if (crumb.label.empty()) {
            crumb.label = path.u8string();
        }
        crumbs_.push_back(crumb);
    }
}

void HostBrowserPanel::rebuild_order()
{
    order_.clear();
    const std::string needle = lower_copy(filter_);
    for (size_t index = 0; index < entries_.size(); ++index) {
        if (needle.empty() || lower_copy(entries_[index].name).find(needle) != std::string::npos) {
            order_.push_back((int) index);
        }
    }
    const int column = sort_column_;
    const bool descending = descending_;
    std::sort(order_.begin(), order_.end(), [&](int first, int second) {
        const Entry &a = entries_[first];
        const Entry &b = entries_[second];
        if (a.directory != b.directory) {
            return a.directory;
        }
        bool less = false;
        if (column == 1) {
            less = a.size < b.size;
        } else if (column == 2) {
            less = a.modified < b.modified;
        } else {
            less = lower_copy(a.name) < lower_copy(b.name);
        }
        return descending ? !less : less;
    });
}

void HostBrowserPanel::navigate_to(const fs::path &path, bool record_history)
{
    std::error_code ec;
    if (!fs::is_directory(path, ec)) {
        owner_->set_status("That host folder cannot be opened");
        return;
    }
    path_ = path;
    filter_.clear();
    if (search_ != nullptr) {
        search_->value("");
    }
    if (record_history) {
        history_.resize(history_index_ + 1);
        history_.push_back(path_);
        history_index_ = history_.size() - 1;
    }
    table_scroll_ = 0;
    reload();
    rebuild_places();
}

void HostBrowserPanel::go_back()
{
    if (history_.empty() || history_index_ == 0) {
        return;
    }
    history_index_--;
    path_ = history_[history_index_];
    table_scroll_ = 0;
    reload();
}

void HostBrowserPanel::go_up()
{
    const fs::path parent = path_.parent_path();
    if (parent.empty() || parent == path_) {
        return;
    }
    navigate_to(parent, true);
}

void HostBrowserPanel::open_row(int row)
{
    if (row < 0 || row >= (int) order_.size()) {
        return;
    }
    if (entries_[order_[row]].directory) {
        navigate_to(path_ / fs::u8path(entries_[order_[row]].name), true);
    } else {
        /* Double-click copies the file to the disk pane's current folder. */
        copy_selection();
    }
}

int HostBrowserPanel::selected_index() const
{
    return selected_row_ >= 0 && selected_row_ < (int) order_.size() ? order_[selected_row_] : -1;
}

bool HostBrowserPanel::is_selected(int entry_index) const
{
    return std::find(selection_.begin(), selection_.end(), entry_index) != selection_.end();
}

void HostBrowserPanel::select_row(int row, bool ensure_visible)
{
    selection_.clear();
    selected_row_ = -1;
    anchor_row_ = -1;
    if (row >= 0 && row < (int) order_.size()) {
        selected_row_ = row;
        anchor_row_ = row;
        selection_.push_back(order_[row]);
        if (ensure_visible) {
            ensure_row_visible(row);
        }
    }
    redraw();
}

void HostBrowserPanel::select_row_with(int row, bool toggle, bool range)
{
    if (row < 0 || row >= (int) order_.size()) {
        return;
    }
    const int entry_index = order_[row];
    if (range && anchor_row_ >= 0 && anchor_row_ < (int) order_.size()) {
        selection_.clear();
        for (int index = std::min(anchor_row_, row); index <= std::max(anchor_row_, row); ++index) {
            selection_.push_back(order_[index]);
        }
    } else if (toggle) {
        const auto found = std::find(selection_.begin(), selection_.end(), entry_index);
        if (found == selection_.end()) {
            selection_.push_back(entry_index);
        } else {
            selection_.erase(found);
        }
        anchor_row_ = row;
    } else {
        selection_.assign(1, entry_index);
        anchor_row_ = row;
    }
    selected_row_ = row;
    if (!is_selected(entry_index)) {
        selected_row_ = -1;
        if (!selection_.empty()) {
            for (size_t index = 0; index < order_.size(); ++index) {
                if (order_[index] == selection_.front()) {
                    selected_row_ = (int) index;
                    break;
                }
            }
        }
    }
    if (selected_row_ >= 0) {
        ensure_row_visible(selected_row_);
    }
    redraw();
}

void HostBrowserPanel::select_all()
{
    selection_.clear();
    for (int entry : order_) {
        selection_.push_back(entry);
    }
    anchor_row_ = order_.empty() ? -1 : 0;
    if (!selection_.empty() && selected_row_ < 0) {
        selected_row_ = 0;
    }
    redraw();
}

void HostBrowserPanel::ensure_row_visible(int row)
{
    const int view_height = std::max(1, y() + h() - 4 - (body_y_ + kTableHeader) - 6);
    const int top = row * kRowHeight;
    const int bottom = top + kRowHeight;
    if (top < table_scroll_) {
        table_scroll_ = top;
    } else if (bottom > table_scroll_ + view_height) {
        table_scroll_ = bottom - view_height;
    }
    table_scroll_ = std::max(0, table_scroll_);
}

void HostBrowserPanel::scroll_table(int amount)
{
    const int view_height = std::max(1, y() + h() - 4 - (body_y_ + kTableHeader) - 6);
    const int total = (int) order_.size() * kRowHeight;
    table_scroll_ = std::max(0, std::min(table_scroll_ + amount, std::max(0, total - view_height)));
    redraw();
}

std::vector<std::string> HostBrowserPanel::selected_paths() const
{
    std::vector<std::string> paths;
    for (int entry_index : selection_) {
        if (entry_index >= 0 && entry_index < (int) entries_.size()) {
            paths.push_back((path_ / fs::u8path(entries_[entry_index].name)).u8string());
        }
    }
    return paths;
}

void HostBrowserPanel::refresh()
{
    reload();
}

void HostBrowserPanel::set_transfer_hint(bool active)
{
    if (transfer_hint_ != active) {
        transfer_hint_ = active;
        redraw();
    }
}

void HostBrowserPanel::new_folder()
{
    const char *name = fl_input("New folder name:", "");
    if (name == nullptr || *name == '\0') {
        return;
    }
    std::error_code ec;
    fs::create_directory(path_ / fs::u8path(name), ec);
    if (ec) {
        owner_->set_status("Could not create the host folder");
        return;
    }
    reload();
    owner_->set_status("Host folder created");
}

void HostBrowserPanel::rename_entry()
{
    const int index = selected_index();
    if (index < 0) {
        owner_->set_status("Select one host entry to rename");
        return;
    }
    const std::string previous = entries_[index].name;
    const char *name = fl_input("Rename the host entry:", previous.c_str());
    if (name == nullptr || *name == '\0' || previous == name) {
        return;
    }
    std::error_code ec;
    fs::rename(path_ / fs::u8path(previous), path_ / fs::u8path(name), ec);
    if (ec) {
        owner_->set_status("Could not rename the host entry");
        return;
    }
    reload();
    owner_->set_status("Host entry renamed");
}

void HostBrowserPanel::delete_selection()
{
    if (selection_.empty()) {
        owner_->set_status("Select a host entry to delete");
        return;
    }
    char prompt[160];
    std::snprintf(prompt,
                  sizeof(prompt),
                  "Delete %d host entr%s permanently?\nThis cannot be undone.",
                  (int) selection_.size(),
                  selection_.size() == 1 ? "y" : "ies");
    if (fl_choice("%s", "Cancel", "Delete", nullptr, prompt) != 1) {
        return;
    }
    int failed = 0;
    for (int entry_index : selection_) {
        std::error_code ec;
        const fs::path target = path_ / fs::u8path(entries_[entry_index].name);
        if (entries_[entry_index].directory) {
            fs::remove_all(target, ec);
        } else {
            fs::remove(target, ec);
        }
        if (ec) {
            ++failed;
        }
    }
    reload();
    owner_->set_status(failed == 0 ? "Host entries deleted" : "Some host entries could not be deleted");
}

void HostBrowserPanel::delete_entry()
{
    delete_selection();
}

void HostBrowserPanel::copy_selection()
{
    const std::vector<std::string> paths = selected_paths();
    if (paths.empty()) {
        owner_->set_status("Select host files or folders to copy");
        return;
    }
    owner_->copy_host_to_disk(paths);
}

HostBrowserPanel::Hit HostBrowserPanel::hit_test(int mx, int my) const
{
    Hit hit;
    const int nav_y = y() + 4;
    if (my >= nav_y && my <= nav_y + 30) {
        if (mx >= nav_back_x_ && mx < nav_back_x_ + 30) {
            return {HitKind::Back, -1};
        }
        if (mx >= nav_up_x_ && mx < nav_up_x_ + 30) {
            return {HitKind::Up, -1};
        }
        if (mx >= nav_home_x_ && mx < nav_home_x_ + 30) {
            return {HitKind::Home, -1};
        }
        for (size_t index = 0; index < crumbs_.size(); ++index) {
            if (mx >= crumbs_[index].x && mx < crumbs_[index].x + crumbs_[index].w) {
                return {HitKind::Crumb, (int) index};
            }
        }
    }
    const int action_y = y() + nav_h_ + 4;
    if (my >= action_y && my < action_y + 28) {
        if (mx >= new_folder_x_ && mx < new_folder_x_ + new_folder_w_) {
            return {HitKind::NewFolder, -1};
        }
        if (mx >= rename_x_ && mx < rename_x_ + rename_w_) {
            return {HitKind::Rename, -1};
        }
        if (mx >= delete_x_ && mx < delete_x_ + delete_w_) {
            return {HitKind::Delete, -1};
        }
        if (mx >= copy_x_ && mx < copy_x_ + copy_w_) {
            return {HitKind::Copy, -1};
        }
    }
    if (my < body_y_ || my > y() + h() - 4) {
        return hit;
    }
    if (places_w_ > 0 && mx >= x() && mx < x() + places_w_ && my >= body_y_ + 28) {
        const int row = (my - body_y_ - 28) / 26;
        if (row >= 0 && row < (int) places_.size()) {
            return {HitKind::Place, row};
        }
    }
    if (mx >= table_x_ && mx < table_x_ + table_w_) {
        if (my < body_y_ + kTableHeader) {
            const int mod_x = table_x_ + table_w_ - std::min(150, std::max(90, table_w_ / 3)) - 8;
            const int size_x = mod_x - 76;
            return {HitKind::Header, mx < size_x ? 0 : (mx < mod_x ? 1 : 2)};
        }
        if (mx >= table_x_ + table_w_ - 8) {
            return {HitKind::Scrollbar, -1};
        }
        const int row = (my - body_y_ - kTableHeader + table_scroll_) / kRowHeight;
        if (row >= 0 && row < (int) order_.size()) {
            return {HitKind::Row, row};
        }
    }
    return hit;
}

int HostBrowserPanel::handle(int event)
{
    switch (event) {
    case FL_PUSH: {
        /* Clicks on the search child must reach it; this pane handles most
         * mouse events itself, so Fl_Group never sets it as belowmouse. */
        if (search_ != nullptr && point_inside(search_, Fl::event_x(), Fl::event_y())) {
            return search_->handle(event);
        }
        Fl::focus(this);
        if (Fl::event_button() != FL_LEFT_MOUSE) {
            return 1;
        }
        const Hit hit = hit_test(Fl::event_x(), Fl::event_y());
        switch (hit.kind) {
        case HitKind::Back:
            go_back();
            return 1;
        case HitKind::Up:
            go_up();
            return 1;
        case HitKind::Home:
            navigate_to(home_directory(), true);
            return 1;
        case HitKind::Crumb:
            if (hit.index >= 0 && hit.index < (int) crumbs_.size()) {
                navigate_to(crumbs_[hit.index].path, true);
            }
            return 1;
        case HitKind::Place:
            if (hit.index >= 0 && hit.index < (int) places_.size()) {
                navigate_to(places_[hit.index].path, true);
            }
            return 1;
        case HitKind::NewFolder:
            new_folder();
            return 1;
        case HitKind::Rename:
            rename_entry();
            return 1;
        case HitKind::Delete:
            delete_selection();
            return 1;
        case HitKind::Copy:
            copy_selection();
            return 1;
        case HitKind::Header:
            if (hit.index == sort_column_) {
                descending_ = !descending_;
            } else {
                sort_column_ = hit.index;
                descending_ = false;
            }
            rebuild_order();
            table_scroll_ = 0;
            redraw();
            return 1;
        case HitKind::Row:
            if (hit.index >= 0 && hit.index < (int) order_.size()) {
                const int modifiers = Fl::event_state();
                const bool toggle = (modifiers & FL_COMMAND) != 0;
                const bool range = (modifiers & FL_SHIFT) != 0;
                if (toggle || range) {
                    select_row_with(hit.index, toggle, range);
                } else {
                    select_row(hit.index, false);
                }
                drag_candidate_ = true;
                dragging_ = false;
                drag_x_ = Fl::event_x_root();
                drag_y_ = Fl::event_y_root();
                Fl::pushed(this);
                if (Fl::event_clicks() > 0) {
                    open_row(hit.index);
                }
            }
            return 1;
        case HitKind::Scrollbar: {
            scroll_drag_ = true;
            Fl::pushed(this);
            const int list_top = body_y_ + kTableHeader;
            const int list_height = std::max(1, y() + h() - 4 - list_top - 2);
            const int total = (int) order_.size() * kRowHeight;
            const int max_scroll = std::max(0, total - list_height);
            if (max_scroll > 0) {
                const int thumb_h = std::max(24, list_height * list_height / std::max(1, total));
                const int track = std::max(1, list_height - thumb_h);
                const int pos = std::max(0, std::min(Fl::event_y() - list_top - thumb_h / 2, track));
                table_scroll_ = pos * max_scroll / track;
                redraw();
            }
            return 1;
        }
        case HitKind::None:
            return 1;
        }
        return 1;
    }
    case FL_DRAG: {
        if (scroll_drag_) {
            const int list_top = body_y_ + kTableHeader;
            const int list_height = std::max(1, y() + h() - 4 - list_top - 2);
            const int total = (int) order_.size() * kRowHeight;
            const int max_scroll = std::max(0, total - list_height);
            if (max_scroll > 0) {
                const int thumb_h = std::max(24, list_height * list_height / std::max(1, total));
                const int track = std::max(1, list_height - thumb_h);
                const int pos = std::max(0, std::min(Fl::event_y() - list_top - thumb_h / 2, track));
                table_scroll_ = pos * max_scroll / track;
                redraw();
            }
            return 1;
        }
        if (drag_candidate_) {
            if (!dragging_ && (std::abs(Fl::event_x_root() - drag_x_) > 4 ||
                               std::abs(Fl::event_y_root() - drag_y_) > 4)) {
                dragging_ = true;
            }
            if (dragging_ && owner_ != nullptr && owner_->disk_browser() != nullptr) {
                const bool over_disk = point_inside(owner_->disk_browser(), Fl::event_x(), Fl::event_y());
                owner_->disk_browser()->set_transfer_hint(over_disk);
                return 1;
            }
        }
        return Fl_Group::handle(event);
    }
    case FL_RELEASE: {
        if (scroll_drag_) {
            scroll_drag_ = false;
            Fl::pushed(nullptr);
            return 1;
        }
        if (dragging_) {
            const bool over_disk = owner_ != nullptr && owner_->disk_browser() != nullptr &&
                                   point_inside(owner_->disk_browser(), Fl::event_x(), Fl::event_y());
            if (owner_ != nullptr && owner_->disk_browser() != nullptr) {
                owner_->disk_browser()->set_transfer_hint(false);
            }
            if (over_disk) {
                copy_selection();
            }
            dragging_ = false;
            drag_candidate_ = false;
            Fl::pushed(nullptr);
            return 1;
        }
        drag_candidate_ = false;
        return 1;
    }
    case FL_MOVE: {
        const Hit hit = hit_test(Fl::event_x(), Fl::event_y());
        if ((int) hit.kind != hover_kind_ || hit.index != hover_index_) {
            hover_kind_ = (int) hit.kind;
            hover_index_ = hit.index;
            redraw();
        }
        return 1;
    }
    case FL_LEAVE:
        if (hover_kind_ != -1) {
            hover_kind_ = -1;
            hover_index_ = -1;
            redraw();
        }
        Fl_Tooltip::exit(this);
        return 1;
    case FL_MOUSEWHEEL:
        scroll_table(Fl::event_dy() * 42);
        return 1;
    case FL_KEYDOWN: {
        const int key = Fl::event_key();
        const int state = Fl::event_state();
        if ((state & FL_COMMAND) != 0 && (key == 'a' || key == 'A')) {
            select_all();
            return 1;
        }
        if (key == FL_Escape) {
            clear_search();
            return 1;
        }
        if (key == FL_Up || key == FL_Down || key == FL_Page_Up || key == FL_Page_Down || key == FL_Home ||
            key == FL_End) {
            if (order_.empty()) {
                return 1;
            }
            int row = selected_row_;
            if (key == FL_Up) {
                row = row <= 0 ? 0 : row - 1;
            } else if (key == FL_Down) {
                row = row < 0 ? 0 : std::min((int) order_.size() - 1, row + 1);
            } else if (key == FL_Page_Up) {
                row = std::max(0, row - 8);
            } else if (key == FL_Page_Down) {
                row = std::min((int) order_.size() - 1, row + 8);
            } else if (key == FL_Home) {
                row = 0;
            } else {
                row = (int) order_.size() - 1;
            }
            if (state & FL_SHIFT) {
                select_row_with(row, false, true);
            } else {
                select_row(row, true);
            }
            return 1;
        }
        if (key == FL_Enter) {
            open_row(selected_row_);
            return 1;
        }
        if (key == FL_Delete) {
            delete_selection();
            return 1;
        }
        if (key == FL_F + 2) {
            rename_entry();
            return 1;
        }
        if (key == FL_BackSpace) {
            go_up();
            return 1;
        }
        return 0;
    }
    default:
        break;
    }
    return Fl_Group::handle(event);
}

std::string HostBrowserPanel::timestamp(const Entry &entry) const
{
    if (entry.modified == 0) {
        return "-";
    }
    std::tm value{};
#ifdef _WIN32
    localtime_s(&value, &entry.modified);
#else
    localtime_r(&entry.modified, &value);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &value);
    return buffer;
}

void HostBrowserPanel::draw_navigation()
{
    const int nav_y = y() + 4;
    struct NavButton {
        int x;
        modern::Glyph glyph;
        bool hovered;
    };
    const NavButton buttons[] = {
        {nav_back_x_, modern::Glyph::Back, hover_kind_ == (int) HitKind::Back},
        {nav_up_x_, modern::Glyph::Up, hover_kind_ == (int) HitKind::Up},
        {nav_home_x_, modern::Glyph::Home, hover_kind_ == (int) HitKind::Home},
    };
    for (const NavButton &button : buttons) {
        if (button.hovered) {
            modern::fill_round(button.x, nav_y + 2, 28, 26, 6, modern::color(modern::Palette::kHover));
        }
        modern::draw_glyph(button.glyph, button.x + 14, nav_y + 15, 15, modern::color(modern::Palette::kGray));
    }

    int crumb_x = nav_home_x_ + 36;
    const int crumb_end = search_ != nullptr ? search_->x() - 24 : x() + w() - 12;
    for (size_t index = 0; index < crumbs_.size(); ++index) {
        Crumb &crumb = crumbs_[index];
        crumb.w = modern::text_width(crumb.label, 11, false) + 18;
        if (crumb_x + crumb.w > crumb_end) {
            crumb.w = 0;
            continue;
        }
        crumb.x = crumb_x;
        const bool hovered = hover_kind_ == (int) HitKind::Crumb && hover_index_ == (int) index;
        const bool last = index + 1 == crumbs_.size();
        if (hovered) {
            modern::fill_round(crumb_x, nav_y + 3, crumb.w, 24, 6, modern::color(modern::Palette::kHover));
        } else if (last) {
            modern::fill_round(crumb_x, nav_y + 3, crumb.w, 24, 6, modern::color(modern::Palette::kAccentSoft));
        }
        modern::text(crumb.label,
                     crumb_x + 9,
                     nav_y + 19,
                     modern::color(last ? modern::Palette::kDark : modern::Palette::kGray),
                     11,
                     last);
        crumb_x += crumb.w + 4;
    }

}

void HostBrowserPanel::draw_places()
{
    if (places_w_ <= 0) {
        return;
    }
    modern::text("Places", x() + 12, body_y_ + 20, modern::color(modern::Palette::kGray), 10, true);
    int row_y = body_y_ + 28;
    for (size_t index = 0; index < places_.size(); ++index) {
        if (row_y + 26 > y() + h() - 4) {
            break;
        }
        const Place &place = places_[index];
        const bool hovered = hover_kind_ == (int) HitKind::Place && hover_index_ == (int) index;
        const bool current = place.path == path_;
        if (current) {
            modern::fill_round(x() + 4, row_y, places_w_ - 8, 24, 6, modern::color(modern::Palette::kSelection));
        } else if (hovered) {
            modern::fill_round(x() + 4, row_y, places_w_ - 8, 24, 6, modern::color(modern::Palette::kHover));
        }
        modern::draw_folder(x() + 12, row_y + 4, 16, 16);
        modern::text_clipped(place.label,
                             x() + 34,
                             row_y + 5,
                             places_w_ - 44,
                             16,
                             modern::color(current ? modern::Palette::kDark : modern::Palette::kDark2),
                             11,
                             current);
        row_y += 26;
    }
    fl_color(modern::color(modern::Palette::kDividerSoft));
    fl_line(x() + places_w_ - 1, body_y_ + 8, x() + places_w_ - 1, y() + h() - 12);
}

void HostBrowserPanel::draw_actions()
{
    const int action_y = y() + nav_h_ + 4;
    struct Action {
        int x;
        int w;
        const char *label;
        modern::Glyph glyph;
        bool enabled;
        HitKind kind;
    };
    const bool selected = !selection_.empty();
    const Action buttons[] = {
        {new_folder_x_, new_folder_w_, "New folder", modern::Glyph::NewFolder, true, HitKind::NewFolder},
        {rename_x_, rename_w_, "Rename", modern::Glyph::More, selected, HitKind::Rename},
        {delete_x_, delete_w_, "Delete", modern::Glyph::Delete, selected, HitKind::Delete},
        {copy_x_, copy_w_, "To disk", modern::Glyph::Export, selected, HitKind::Copy},
    };
    for (const Action &button : buttons) {
        const bool hovered = hover_kind_ == (int) button.kind;
        if (hovered && button.enabled) {
            modern::fill_round(button.x - 2, action_y, button.w + 4, 28, 7, modern::color(modern::Palette::kHover));
        }
        const Fl_Color fg = modern::color(button.enabled ? modern::Palette::kDark2 : modern::Palette::kTextDisabled);
        modern::draw_glyph(button.glyph, button.x + 10, action_y + 14, 16, fg);
        modern::text(button.label, button.x + 24, action_y + 19, fg, 11, false);
    }
}

void HostBrowserPanel::draw_table()
{
    const int mod_w = std::min(150, std::max(90, table_w_ / 3));
    const int mod_x = table_x_ + table_w_ - mod_w - 8;
    const int size_x = mod_x - 76;
    const int body_bottom = y() + h() - 4;
    const int list_top = body_y_ + kTableHeader;
    const int list_height = std::max(1, body_bottom - list_top - 2);

    modern::text_clipped("Name",
                         table_x_ + 10,
                         body_y_ + 10,
                         std::max(40, size_x - table_x_ - 14),
                         18,
                         modern::color(modern::Palette::kGray),
                         10,
                         true);
    modern::text("Size", size_x + 4, body_y_ + 18, modern::color(modern::Palette::kGray), 10, true);
    modern::text("Modified", mod_x + 4, body_y_ + 18, modern::color(modern::Palette::kGray), 10, true);
    fl_color(modern::color(modern::Palette::kDividerSoft));
    fl_line(table_x_, body_y_ + kTableHeader - 1, table_x_ + table_w_, body_y_ + kTableHeader - 1);

    fl_push_clip(table_x_, list_top, table_w_, list_height);
    if (order_.empty()) {
        modern::text_center(filter_.empty() ? "This folder is empty" : "No matching entries",
                            table_x_ + table_w_ / 2,
                            list_top + 34,
                            modern::color(modern::Palette::kGray2),
                            11,
                            false);
    } else {
        const int first = table_scroll_ / kRowHeight;
        const int row_y0 = list_top - (table_scroll_ % kRowHeight);
        for (int index = first; index < (int) order_.size(); ++index) {
            const int row_y = row_y0 + (index - first) * kRowHeight;
            if (row_y > body_bottom) {
                break;
            }
            const Entry &entry = entries_[order_[index]];
            const bool selected = is_selected(order_[index]);
            const bool hovered = hover_kind_ == (int) HitKind::Row && hover_index_ == index;
            if (selected) {
                modern::fill_round(table_x_ + 4, row_y + 1, table_w_ - 16, kRowHeight - 2, 5,
                                   modern::color(modern::Palette::kSelection));
            } else if (hovered) {
                modern::fill_round(table_x_ + 4, row_y + 1, table_w_ - 16, kRowHeight - 2, 5,
                                   modern::color(modern::Palette::kHover));
            }
            const int icon_x = table_x_ + 12;
            if (entry.directory) {
                modern::draw_folder(icon_x, row_y + 5, 18, 18);
            } else {
                modern::draw_file(modern::file_kind(entry.name.c_str(), false), icon_x, row_y + 5, 18, 18);
            }
            modern::text_clipped(entry.name,
                                 icon_x + 26,
                                 row_y + 7,
                                 std::max(20, size_x - icon_x - 30),
                                 16,
                                 modern::color(selected ? modern::Palette::kDark : modern::Palette::kDark2),
                                 11,
                                 selected);
            if (entry.directory) {
                modern::text("-", size_x + 4, row_y + 18, modern::color(modern::Palette::kGray2), 10, false);
            } else {
                modern::text(utils::format_size(entry.size, true),
                             size_x + 4,
                             row_y + 18,
                             modern::color(modern::Palette::kGray),
                             10,
                             false);
            }
            modern::text(timestamp(entry), mod_x + 4, row_y + 18, modern::color(modern::Palette::kGray), 10, false);
            fl_color(modern::color(modern::Palette::kHoverSoft));
            fl_line(table_x_ + 8, row_y + kRowHeight - 1, table_x_ + table_w_ - 12, row_y + kRowHeight - 1);
        }
    }
    fl_pop_clip();

    draw_scrollbar(table_x_ + table_w_ - 7, list_top, list_height, (int) order_.size() * kRowHeight,
                   list_height, table_scroll_);
}

void HostBrowserPanel::draw_scrollbar(int sx, int sy, int sh, int total, int visible, int offset)
{
    if (total <= visible) {
        return;
    }
    const int thumb_h = std::max(28, sh * visible / total);
    const int max_scroll = std::max(1, total - visible);
    const int thumb_y = sy + (sh - thumb_h) * offset / max_scroll;
    modern::fill_round(sx, sy, 3, sh, 2, modern::color(modern::Palette::kDividerSoft));
    modern::fill_round(sx, thumb_y, 3, thumb_h, 2, modern::color(modern::Palette::kScrollThumb));
}

void HostBrowserPanel::draw()
{
    fl_push_clip(x(), y(), w(), h());
    fl_color(modern::color(modern::Palette::kWindow));
    fl_rectf(x(), y(), w(), h());

    draw_navigation();
    draw_actions();

    const int body_h = y() + h() - 4 - body_y_;
    modern::fill_round(x(), body_y_, w(), body_h, 10, modern::color(modern::Palette::kPanel));
    modern::stroke_round(x(), body_y_, w(), body_h, 10, modern::color(modern::Palette::kDivider), 1);
    draw_places();
    draw_table();

    if (transfer_hint_) {
        modern::stroke_round(x() + 2, body_y_ + 2, w() - 4, body_h - 4, 9,
                             modern::color(modern::Palette::kAccent), 2);
    }
    fl_pop_clip();

    Fl_Group::draw();

    if (search_ != nullptr) {
        modern::draw_glyph(modern::Glyph::Search,
                           search_->x() - 14,
                           search_->y() + search_->h() / 2,
                           14,
                           modern::color(modern::Palette::kGray2));
    }

    /* The search field is a child widget, so its placeholder must be painted
     * after the group draws its children. */
    if (search_ != nullptr && filter_.empty() && Fl::focus() != search_) {
        fl_push_clip(search_->x() + 2, search_->y() + 2, search_->w() - 4, search_->h() - 4);
        modern::text("Search this folder",
                     search_->x() + 7,
                     search_->y() + search_->h() / 2 + 4,
                     modern::color(modern::Palette::kGray2),
                     11,
                     false);
        fl_pop_clip();
    }
}
