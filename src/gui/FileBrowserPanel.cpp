/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gui/FileBrowserPanel.hpp"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Tooltip.H>
#include <FL/Fl_Window.H>
#include <FL/filename.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "gui/FileBrowserPanelInternal.hpp"
#include "gui/MainWindow.hpp"
#include "gui/ModernStyle.hpp"
#include "gui/Utils.hpp"

using namespace fbp;

/* Modal progress window used while the synchronous core copies data. */
class ProgressDialog {
public:
    ProgressDialog(const char *title, const std::string &detail) : detail_(detail)
    {
        window_ = new Fl_Window(400, 118, title);
        window_->set_modal();
        window_->color(modern::color(modern::kWindow));
        label_ = new Fl_Box(FL_NO_BOX, 16, 12, 368, 40, detail.c_str());
        label_->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
        label_->labelsize(12);
        label_->labelcolor(modern::color(modern::kDark));
        progress_ = new Fl_Progress(16, 58, 368, 22);
        progress_->minimum(0);
        progress_->maximum(100);
        progress_->value(0);
        progress_->box(FL_DOWN_BOX);
        progress_->color(modern::color(modern::kPanelAlt));
        progress_->selection_color(modern::color(modern::kAccent));
        progress_->labelsize(0);
        window_->end();
        window_->position((Fl::w() - window_->w()) / 2, (Fl::h() - window_->h()) / 2);
        window_->show();
        Fl::check();
    }

    ~ProgressDialog() { delete window_; }

    void set_detail(const std::string &detail) { detail_ = detail; }

    void update(uint64_t done, uint64_t total)
    {
        const int percent = total != 0 ? (int) (done * 100 / total) : 100;
        progress_->value(percent);
        char buffer[256];
        std::snprintf(buffer,
                      sizeof(buffer),
                      "%s\n%s of %s",
                      detail_.c_str(),
                      utils::format_size(done).c_str(),
                      utils::format_size(total).c_str());
        label_->copy_label(buffer);
        Fl::check();
    }

private:
    Fl_Window *window_ = nullptr;
    Fl_Box *label_ = nullptr;
    Fl_Progress *progress_ = nullptr;
    std::string detail_;
};

static void progress_cb(void *user, uint64_t done, uint64_t total)
{
    static_cast<ProgressDialog *>(user)->update(done, total);
}

class SearchInput : public Fl_Input {
public:
    explicit SearchInput(FileBrowserPanel *panel) : Fl_Input(0, 0, 10, 10), panel_(panel) {}

    int handle(int event) override
    {
        if (event == FL_KEYBOARD && Fl::event_key() == FL_Escape) {
            panel_->clear_search();
            return 1;
        }
        return Fl_Input::handle(event);
    }

private:
    FileBrowserPanel *panel_;
};

/* FLTK only delivers drag-and-drop events to non-group widgets, and
 * FileBrowserPanel is a group because of its search field. This transparent
 * child covers the browsing area, forwards ordinary mouse events to the panel
 * and turns dropped files into imports. */
class FileDropTarget : public Fl_Widget {
public:
    explicit FileDropTarget(FileBrowserPanel *panel) : Fl_Widget(0, 0, 10, 10), panel_(panel) { box(FL_NO_BOX); }

    void draw() override {}

    int handle(int event) override
    {
        switch (event) {
        /* FLTK sends FL_DND_DRAG instead of FL_DND_ENTER when the pointer is
         * already over the widget, so both open the drop highlight. */
        case FL_DND_ENTER:
        case FL_DND_DRAG:
            if (!panel_->can_import()) {
                return 0;
            }
            panel_->clear_tooltip();
            panel_->set_drop_active(true);
            return 1;
        case FL_DND_LEAVE:
        case FL_LEAVE:
            /* FLTK clears belowmouse() on a platform DND leave, which arrives
             * here as FL_LEAVE; both must drop the highlight. */
            panel_->set_drop_active(false);
            return event == FL_DND_LEAVE ? 1 : panel_->handle(event);
        case FL_DND_RELEASE:
            /* Returning 1 asks FLTK to send FL_PASTE with the dropped data. */
            return 1;
        case FL_PASTE: {
            panel_->clear_tooltip();
            panel_->set_drop_active(false);
            std::vector<std::string> paths;
            const char *text = Fl::event_text();
            if (text == nullptr || text[0] == '\0') {
                return 1;
            }
            std::string payload(text);
            std::vector<std::string> tokens;
            const bool line_separated = payload.find('\n') != std::string::npos;
            size_t start = 0;
            while (start <= payload.size()) {
                size_t end = line_separated ? payload.find('\n', start) : payload.find(' ', start);
                std::string token = payload.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!token.empty() && token.back() == '\r') {
                    token.pop_back();
                }
                tokens.push_back(token);
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            for (std::string &token : tokens) {
                if (token.empty()) {
                    continue;
                }
                if (token.rfind("file://", 0) == 0) {
                    token.erase(0, 7);
                } else if (token.find("://") != std::string::npos) {
                    continue; /* web or other URI, not a local file */
                }
                fl_decode_uri(token.data());
                if (!token.empty()) {
                    paths.push_back(token);
                }
            }
            /* The import pumps its progress dialog, which processes X events
             * and would clobber FLTK's Xdnd bookkeeping before it can send
             * XdndFinished; defer it to the next event-loop turn. */
            panel_->queue_drop_import(paths);
            return 1;
        }
        case FL_PUSH:
        case FL_RELEASE:
        case FL_DRAG:
        case FL_MOVE:
        case FL_ENTER:
        case FL_MOUSEWHEEL:
            return panel_->handle(event);
        default:
            return Fl_Widget::handle(event);
        }
    }

private:
    FileBrowserPanel *panel_;
};

/* Join a host directory and a leaf name, tolerating either separator. */
static std::string host_join(const std::string &directory, const std::string &name)
{
    if (directory.empty()) {
        return name;
    }
    const char last = directory.back();
    if (last == '/' || last == '\\') {
        return directory + name;
    }
    return directory + "/" + name;
}

FileBrowserPanel::FileBrowserPanel(int x, int y, int w, int h, MainWindow *owner)
    : Fl_Group(x, y, w, h), owner_(owner)
{
    box(FL_NO_BOX);
    begin();
    search_ = new SearchInput(this);
    search_->box(FL_DOWN_BOX);
    search_->textsize(12);
    search_->textfont(FL_HELVETICA);
    search_->textcolor(modern::color(modern::kDark));
    search_->cursor_color(modern::color(modern::kAccent));
    search_->when(FL_WHEN_CHANGED);
    search_->callback(search_cb, this);
    search_->tooltip("Filter the current folder");
    drop_target_ = new FileDropTarget(this);
    end();
    history_.push_back("/");
    layout_geometry();
    layout_search();
}

FileBrowserPanel::~FileBrowserPanel()
{
    Fl::remove_timeout(drop_import_cb, this);
    zfs_entries_free(entries_);
    entries_ = nullptr;
}

void FileBrowserPanel::resize(int x, int y, int w, int h)
{
    Fl_Group::resize(x, y, w, h);
    layout_geometry();
    layout_search();
    redraw();
}

void FileBrowserPanel::set_right_width(int width)
{
    right_width_ = std::max(200, width);
    layout_geometry();
    layout_search();
    redraw();
}

void FileBrowserPanel::layout_geometry()
{
    nav_h_ = 38;
    actions_h_ = 34;
    body_y_ = y() + nav_h_ + actions_h_ + 2;
    if (drop_target_ != nullptr) {
        drop_target_->resize(x(), body_y_, w(), std::max(1, y() + h() - body_y_));
    }

    const int rail_rel = std::max(220, w() - right_width_);
    preview_x_ = x() + rail_rel;
    preview_w_ = right_width_;

    const int card_w_rel = rail_rel - 12;
    tree_w_ = std::min(230, std::max(170, rail_rel / 5));
    table_x_ = x() + tree_w_ + 10;
    table_w_ = std::max(220, x() + card_w_rel - 10 - table_x_);

    nav_back_x_ = x() + 4;
    nav_forward_x_ = x() + 36;
    nav_up_x_ = x() + 68;
    nav_home_x_ = x() + 100;

    const int action_y = y() + nav_h_ + 4;
    const int action_h = 28;
    import_w_ = 22 + modern::text_width("Import", 11) + 20;
    export_w_ = 22 + modern::text_width("Export", 11) + 20;
    new_folder_w_ = 22 + modern::text_width("New folder", 11) + 20;
    delete_w_ = 22 + modern::text_width("Delete", 11) + 20;
    import_x_ = x() + 4;
    export_x_ = import_x_ + import_w_ + 4;
    new_folder_x_ = export_x_ + export_w_ + 4;
    delete_x_ = new_folder_x_ + new_folder_w_ + 4;
    (void) action_y;
    (void) action_h;
}

void FileBrowserPanel::layout_search()
{
    if (search_ == nullptr) {
        return;
    }
    const int field_w = std::max(140, preview_w_ - 40);
    search_->resize(preview_x_ + 30, y() + 8, std::max(60, field_w), 26);
}

bool FileBrowserPanel::valid_target() const
{
    return disk_ != nullptr && disk_->valid && slot_ >= 0 && slot_ < MAX_PART_COUNT &&
           disk_->partitions[slot_].active && disk_->partitions[slot_].type == ZEALFS_TYPE;
}

void FileBrowserPanel::retarget(disk_info_t *disk, int slot)
{
    const bool changed = disk != disk_ || slot != slot_;
    const unsigned revision = disk != nullptr ? disk->revision : 0;
    if (changed) {
        disk_ = disk;
        slot_ = slot;
        revision_ = revision;
        target_valid_ = valid_target();
        path_ = "/";
        filter_.clear();
        if (search_ != nullptr) {
            search_->value("");
        }
        selected_name_.clear();
        selected_row_ = -1;
        history_.clear();
        history_.push_back("/");
        history_index_ = 0;
        table_scroll_ = 0;
        tree_scroll_ = 0;
        table_scroll_drag_ = false;
        collapsed_.clear();
        reload();
        return;
    }
    if (revision != revision_) {
        revision_ = revision;
        reload();
        return;
    }
    target_valid_ = valid_target();
}

bool FileBrowserPanel::reload()
{
    const std::string previous = selected_name_;
    zfs_entries_free(entries_);
    entries_ = nullptr;
    count_ = 0;
    target_valid_ = valid_target();

    bool listed = false;
    if (target_valid_) {
        if (disk_fs_list(disk_, slot_, path_.c_str(), &entries_, &count_) == 0) {
            listed = true;
        } else if (path_ != "/") {
            path_ = "/";
            if (disk_fs_list(disk_, slot_, path_.c_str(), &entries_, &count_) == 0) {
                listed = true;
            }
        }
    }
    if (!listed) {
        zfs_entries_free(entries_);
        entries_ = nullptr;
        count_ = 0;
    }

    rebuild_order();
    rebuild_tree();
    selected_row_ = -1;
    selected_name_.clear();
    int row = -1;
    if (!order_.empty()) {
        for (int index = 0; index < (int) order_.size(); ++index) {
            if (previous == entries_[order_[index]].name) {
                row = index;
                break;
            }
        }
        if (row < 0) {
            row = 0;
        }
    }
    select_row(row, false);
    table_scroll_ = 0;
    tree_scroll_ = 0;
    update_preview();
    redraw();
    return listed;
}

void FileBrowserPanel::rebuild_order()
{
    const std::string needle = lower_copy(filter_.c_str());
    order_.clear();
    if (entries_ == nullptr) {
        return;
    }
    for (size_t index = 0; index < count_; ++index) {
        if (name_matches_filter(entries_[index].name, needle)) {
            order_.push_back((int) index);
        }
    }

    const int column = sort_column_;
    const bool descending = descending_;
    std::sort(order_.begin(), order_.end(), [&](int first, int second) {
        const zfs_entry &a = entries_[first];
        const zfs_entry &b = entries_[second];
        if (a.directory != b.directory) {
            return a.directory; /* folders stay first in both directions */
        }
        int result = 0;
        switch (column) {
        case 1:
            result = a.size < b.size ? -1 : a.size > b.size ? 1 : 0;
            break;
        case 2:
            result = std::strcmp(type_name(a), type_name(b));
            break;
        case 3: {
            const uint64_t left = timestamp_value(a);
            const uint64_t right = timestamp_value(b);
            result = left < right ? -1 : left > right ? 1 : 0;
            break;
        }
        case 0:
        default: {
            const std::string left = lower_copy(a.name);
            const std::string right = lower_copy(b.name);
            result = left < right ? -1 : left > right ? 1 : 0;
            break;
        }
        }
        if (result == 0) {
            const std::string left = lower_copy(a.name);
            const std::string right = lower_copy(b.name);
            result = left < right ? -1 : left > right ? 1 : 0;
        }
        return descending ? result > 0 : result < 0;
    });
}

void FileBrowserPanel::rebuild_tree()
{
    tree_rows_.clear();
    if (!target_valid_) {
        return;
    }
    tree_rows_.push_back({"/", "Root", 0, path_ == "/"});
    add_tree_children("/", 1, 0);
}

/* List directories below `path`, recursively. Collapsed folders are not
 * descended into, so large trees stay cheap until the user expands them. */
void FileBrowserPanel::add_tree_children(const std::string &path, int depth, int level)
{
    constexpr int kMaxDepth = 12;
    constexpr size_t kMaxRows = 2000;
    if (level >= kMaxDepth || tree_rows_.size() >= kMaxRows) {
        return;
    }
    zfs_entry *entries = nullptr;
    size_t count = 0;
    if (disk_fs_list(disk_, slot_, path.c_str(), &entries, &count) != 0) {
        return;
    }
    std::vector<size_t> folders;
    folders.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        if (entries[index].directory) {
            folders.push_back(index);
        }
    }
    std::sort(folders.begin(), folders.end(), [&](size_t first, size_t second) {
        return lower_copy(entries[first].name) < lower_copy(entries[second].name);
    });
    for (size_t index : folders) {
        if (tree_rows_.size() >= kMaxRows) {
            break;
        }
        const std::string child = utils::join_path(path, entries[index].name);
        const size_t row = tree_rows_.size();
        tree_rows_.push_back({child, entries[index].name, depth, child == path_});
        if (collapsed_.find(child) == collapsed_.end()) {
            add_tree_children(child, depth + 1, level + 1);
            tree_rows_[row].has_children =
                tree_rows_.size() > row + 1 && tree_rows_[row + 1].depth > depth;
        } else {
            /* Not descended into: keep the chevron, correct it on expand. */
            tree_rows_[row].has_children = true;
        }
    }
    zfs_entries_free(entries);
}

int FileBrowserPanel::selected_entry() const
{
    if (selected_row_ < 0 || selected_row_ >= (int) order_.size()) {
        return -1;
    }
    return order_[selected_row_];
}

std::string FileBrowserPanel::entry_path(int entry_index) const
{
    if (entry_index < 0 || entry_index >= (int) count_) {
        return path_;
    }
    return utils::join_path(path_, entries_[entry_index].name);
}

void FileBrowserPanel::select_row(int row, bool ensure_visible)
{
    selection_.clear();
    if (row < 0 || row >= (int) order_.size()) {
        selected_row_ = -1;
        selected_name_.clear();
        anchor_row_ = -1;
    } else {
        selected_row_ = row;
        selected_name_ = entries_[order_[row]].name;
        anchor_row_ = row;
        selection_.push_back(order_[row]);
        if (ensure_visible) {
            ensure_row_visible(row);
        }
    }
    update_preview();
    if (owner_ != nullptr) {
        owner_->refresh_menu_states();
    }
    redraw();
}

bool FileBrowserPanel::is_selected(int entry_index) const
{
    return std::find(selection_.begin(), selection_.end(), entry_index) != selection_.end();
}

int FileBrowserPanel::row_of_entry(int entry_index) const
{
    for (size_t row = 0; row < order_.size(); ++row) {
        if (order_[row] == entry_index) {
            return (int) row;
        }
    }
    return -1;
}

void FileBrowserPanel::select_row_with(int row, bool toggle, bool range)
{
    if (row < 0 || row >= (int) order_.size()) {
        return;
    }
    const int entry_index = order_[row];
    if (range && anchor_row_ >= 0 && anchor_row_ < (int) order_.size()) {
        const int first = std::min(anchor_row_, row);
        const int last = std::max(anchor_row_, row);
        selection_.clear();
        for (int index = first; index <= last; ++index) {
            selection_.push_back(order_[index]);
        }
    } else if (toggle) {
        const auto found = std::find(selection_.begin(), selection_.end(), entry_index);
        if (found != selection_.end()) {
            selection_.erase(found);
        } else {
            selection_.push_back(entry_index);
        }
        anchor_row_ = row;
    } else {
        selection_.assign(1, entry_index);
        anchor_row_ = row;
    }
    selected_row_ = row;
    selected_name_ = entries_[entry_index].name;
    if (!is_selected(entry_index)) {
        /* Ctrl-clicking the lead row away moves the lead to the remaining
         * selection, so the preview always describes a selected entry. */
        selected_row_ = selection_.empty() ? -1 : row_of_entry(selection_.front());
        selected_name_ = selected_row_ >= 0 ? entries_[order_[selected_row_]].name : std::string();
    }
    if (selected_row_ >= 0) {
        ensure_row_visible(selected_row_);
    }
    update_preview();
    if (owner_ != nullptr) {
        owner_->refresh_menu_states();
    }
    redraw();
}

void FileBrowserPanel::select_all()
{
    selection_.clear();
    selection_.reserve(order_.size());
    for (int entry_index : order_) {
        selection_.push_back(entry_index);
    }
    anchor_row_ = order_.empty() ? -1 : 0;
    if (!selection_.empty() && (selected_row_ < 0 || selected_row_ >= (int) order_.size())) {
        selected_row_ = 0;
    }
    if (selected_row_ >= 0 && selected_row_ < (int) order_.size()) {
        selected_name_ = entries_[order_[selected_row_]].name;
    }
    update_preview();
    if (owner_ != nullptr) {
        owner_->refresh_menu_states();
    }
    redraw();
}

void FileBrowserPanel::update_preview()
{
    preview_lines_.clear();
    preview_message_.clear();
    preview_kind_ = 0;
    multi_count_ = selection_.size();
    if (!target_valid_) {
        preview_message_ = "Select a ZealFS v2 partition to browse its files.";
        return;
    }
    if (multi_count_ > 1) {
        size_t files = 0;
        size_t folders = 0;
        uint64_t total = 0;
        for (int entry_index : selection_) {
            const zfs_entry &entry = entries_[entry_index];
            if (entry.directory) {
                ++folders;
            } else {
                ++files;
                total += entry.size;
            }
        }
        preview_message_ = "Multiple entries selected";
        preview_lines_.push_back(std::to_string(files) + " file" + (files == 1 ? "" : "s") + ", " +
                                 std::to_string(folders) + " folder" + (folders == 1 ? "" : "s"));
        preview_lines_.push_back("Total size: " + utils::format_size(total, true));
        return;
    }
    const int entry_index = selected_entry();
    if (entry_index < 0) {
        preview_message_ = "Select a file to see its details.";
        return;
    }
    const zfs_entry &entry = entries_[entry_index];
    if (entry.directory) {
        preview_kind_ = 2;
        preview_message_ = "Folder";
        preview_lines_.push_back(entry_path(entry_index));
        return;
    }
    preview_kind_ = 1;

    std::vector<unsigned char> buffer(4097);
    size_t read_count = 0;
    const std::string full_path = entry_path(entry_index);
    if (disk_fs_read(disk_, slot_, full_path.c_str(), 0, buffer.data(), 4096, &read_count) != 0) {
        preview_message_ = "Preview is not available.";
        return;
    }
    bool binary = false;
    for (size_t index = 0; index < read_count; ++index) {
        const unsigned char byte = buffer[index];
        if (byte == 0 || (byte < 0x09 && byte != '\n' && byte != '\r' && byte != '\t')) {
            binary = true;
            break;
        }
    }
    if (binary) {
        preview_kind_ = 3;
        const modern::FileKind kind = modern::file_kind(entry.name, false);
        preview_message_ = kind == modern::FileKind::Image ? "Image file. Preview is not available."
                            : kind == modern::FileKind::Archive ? "Archive file. Preview is not available."
                                                                : "Binary file. Preview is not available.";
        return;
    }
    if (read_count == 0) {
        preview_message_ = "Empty file";
        return;
    }

    std::string line;
    for (size_t index = 0; index < read_count; ++index) {
        const char c = (char) buffer[index];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            preview_lines_.push_back(line);
            line.clear();
            if (preview_lines_.size() >= 80) {
                break;
            }
            continue;
        }
        if (c == '\t') {
            line += "    ";
        } else if (line.size() < 160) {
            line.push_back(c);
        }
    }
    if (!line.empty() && preview_lines_.size() < 80) {
        preview_lines_.push_back(line);
    }
}

void FileBrowserPanel::navigate_to(const std::string &path, bool record_history)
{
    if (!target_valid_) {
        return;
    }
    const std::string normalized = utils::normalize_path(path);
    if (normalized == path_) {
        reload();
        return;
    }
    const std::string previous = path_;
    path_ = normalized;
    filter_.clear();
    if (search_ != nullptr) {
        search_->value("");
    }
    selected_name_.clear();
    selected_row_ = -1;
    if (!reload()) {
        path_ = previous;
        reload();
        owner_->set_status("Directory not found");
        return;
    }
    if (record_history) {
        history_.resize(history_index_ + 1);
        history_.push_back(path_);
        history_index_ = history_.size() - 1;
    }
    table_scroll_ = 0;
    tree_scroll_ = 0;
    redraw();
}

void FileBrowserPanel::go_back()
{
    if (history_index_ == 0) {
        return;
    }
    const std::string previous = path_;
    --history_index_;
    path_ = history_[history_index_];
    filter_.clear();
    if (search_ != nullptr) {
        search_->value("");
    }
    selected_name_.clear();
    if (!reload()) {
        path_ = previous;
        ++history_index_;
        reload();
    }
}

void FileBrowserPanel::go_forward()
{
    if (history_index_ + 1 >= history_.size()) {
        return;
    }
    const std::string previous = path_;
    ++history_index_;
    path_ = history_[history_index_];
    filter_.clear();
    if (search_ != nullptr) {
        search_->value("");
    }
    selected_name_.clear();
    if (!reload()) {
        path_ = previous;
        --history_index_;
        reload();
    }
}

void FileBrowserPanel::go_up()
{
    navigate_to(utils::parent_path(path_), true);
}

void FileBrowserPanel::open_row(int row)
{
    if (row < 0 || row >= (int) order_.size()) {
        return;
    }
    const int entry_index = order_[row];
    if (entries_[entry_index].directory) {
        navigate_to(entry_path(entry_index), true);
    } else {
        export_entry(entry_index);
    }
}

void FileBrowserPanel::focus_list()
{
    take_focus();
    Fl::focus(this);
    redraw();
}

void FileBrowserPanel::search_cb(Fl_Widget *, void *data)
{
    auto *panel = static_cast<FileBrowserPanel *>(data);
    panel->filter_ = panel->search_->value();
    if (panel->filter_.find_first_not_of(" \t") == std::string::npos) {
        panel->filter_.clear();
    }
    panel->rebuild_order();
    panel->table_scroll_ = 0;
    panel->select_row(panel->order_.empty() ? -1 : 0, false);
    panel->redraw();
}

void FileBrowserPanel::ensure_row_visible(int row)
{
    const int view_height = std::max(1, h() - (body_y_ - y()) - kTableHeader - 6);
    const int top = row * kRowHeight;
    const int bottom = top + kRowHeight;
    if (top < table_scroll_) {
        table_scroll_ = top;
    } else if (bottom > table_scroll_ + view_height) {
        table_scroll_ = bottom - view_height;
    }
    table_scroll_ = std::max(0, table_scroll_);
}

void FileBrowserPanel::scroll_table(int amount)
{
    const int view_height = std::max(1, h() - (body_y_ - y()) - kTableHeader - 6);
    const int total = (int) order_.size() * kRowHeight;
    const int max_scroll = std::max(0, total - view_height);
    table_scroll_ = std::max(0, std::min(table_scroll_ + amount, max_scroll));
    redraw();
}

void FileBrowserPanel::scroll_tree(int amount)
{
    const int view_height = std::max(1, h() - (body_y_ - y()) - 10);
    const int total = (int) tree_rows_.size() * kTreeRowHeight;
    const int max_scroll = std::max(0, total - view_height);
    tree_scroll_ = std::max(0, std::min(tree_scroll_ + amount, max_scroll));
    redraw();
}

void FileBrowserPanel::clear_search()
{
    if (filter_.empty()) {
        return;
    }
    filter_.clear();
    search_->value("");
    rebuild_order();
    select_row(order_.empty() ? -1 : 0, false);
    redraw();
}

void FileBrowserPanel::focus_search()
{
    if (search_ != nullptr) {
        Fl::focus(search_);
        search_->take_focus();
        redraw();
    }
}

void FileBrowserPanel::action_import()
{
    import_files();
}

void FileBrowserPanel::action_export()
{
    if (selection_.empty()) {
        owner_->set_status("Select a file or folder to export");
        return;
    }
    export_selection();
}

void FileBrowserPanel::action_new_folder()
{
    new_folder();
}

void FileBrowserPanel::action_delete()
{
    if (selection_.empty()) {
        owner_->set_status("Select a file or folder to delete");
        return;
    }
    delete_selection();
}

void FileBrowserPanel::import_files()
{
    if (!valid_target()) {
        return;
    }
    Fl_Native_File_Chooser chooser;
    chooser.title("Import files");
    chooser.type(Fl_Native_File_Chooser::BROWSE_MULTI_FILE);
    chooser.filter("All files\t*");
    if (chooser.show() != 0) {
        return;
    }
    bool success = true;
    std::vector<std::string> batch;
    const int selected = chooser.count();
    for (int index = 0; index < selected; ++index) {
        const std::string host = chooser.filename(index);
        const std::string name = utils::base_name(host);
        if (!utils::valid_entry_name(name)) {
            char prompt[512];
            std::snprintf(prompt,
                          sizeof(prompt),
                          "The host name \"%s\" is not valid in ZealFS (max %d bytes).\n"
                          "Enter the name to use inside the image:",
                          name.c_str(),
                          ZFS_NAME_MAX);
            const char *renamed = fl_input("%s", "", prompt);
            if (renamed == nullptr) {
                continue;
            }
            const std::string cleaned = utils::normalize_path(std::string("/") + renamed).substr(1);
            if (!utils::valid_entry_name(cleaned)) {
                owner_->set_status("Skipped a file with an invalid name");
                continue;
            }
            ProgressDialog progress("Importing file", name);
            const char *host_path = host.c_str();
            const std::string destination = utils::join_path(path_, cleaned);
            if (disk_fs_import(disk_, slot_, &host_path, 1, destination.c_str(), progress_cb, &progress) != 0) {
                success = false;
            }
        } else {
            batch.push_back(host);
        }
    }

    if (!batch.empty()) {
        std::vector<const char *> hosts;
        hosts.reserve(batch.size());
        for (const std::string &host : batch) {
            hosts.push_back(host.c_str());
        }
        const std::string destination = path_ == "/" ? "/" : path_ + "/";
        ProgressDialog progress("Importing files", path_);
        if (disk_fs_import(
                disk_, slot_, hosts.data(), hosts.size(), destination.c_str(), progress_cb, &progress) != 0) {
            success = false;
        }
    }
    owner_->set_status(success ? "Import staged" : "Import failed; see the status bar");
    owner_->model_changed();
    focus_list();
}

void FileBrowserPanel::export_entry(int entry_index)
{
    if (!valid_target() || entry_index < 0 || entry_index >= (int) count_) {
        return;
    }
    const zfs_entry &entry = entries_[entry_index];
    Fl_Native_File_Chooser chooser;
    chooser.title(entry.directory ? "Choose where to export the folder" : "Choose where to save the file");
    chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
    chooser.filter("All files\t*");
    chooser.preset_file(entry.name);
    if (chooser.show() != 0) {
        return;
    }
    const std::string full_path = entry_path(entry_index);
    ProgressDialog progress(entry.directory ? "Exporting folder" : "Exporting file", entry.name);
    if (disk_fs_export(disk_, slot_, full_path.c_str(), chooser.filename(), progress_cb, &progress) == 0) {
        owner_->set_status(entry.directory ? "Folder exported" : "File exported");
    } else {
        owner_->set_status("Export failed; see the status bar");
    }
}

void FileBrowserPanel::new_folder()
{
    if (!valid_target()) {
        return;
    }
    char prompt[96];
    std::snprintf(prompt, sizeof(prompt), "New folder name (max %d bytes):", ZFS_NAME_MAX);
    const char *name = fl_input("%s", "", prompt);
    if (name == nullptr) {
        return;
    }
    const std::string folder = name;
    if (!utils::valid_entry_name(folder)) {
        owner_->set_status("Invalid folder name: use 1 to 16 bytes and no slashes");
        return;
    }
    if (disk_fs_mkdir(disk_, slot_, utils::join_path(path_, folder).c_str()) == 0) {
        owner_->set_status("Folder creation staged");
        owner_->model_changed();
        focus_list();
    }
}

void FileBrowserPanel::delete_entry(int entry_index)
{
    if (!valid_target() || entry_index < 0 || entry_index >= (int) count_) {
        return;
    }
    const zfs_entry &entry = entries_[entry_index];
    const std::string name = entry.name;
    const bool directory = entry.directory;
    if (fl_choice("Stage the deletion of \"%s\"?\n"
                  "The entry is removed when the changes are applied.",
                  "Cancel",
                  directory ? "Delete folder" : "Delete file",
                  nullptr,
                  name.c_str()) != 1) {
        return;
    }
    if (disk_fs_remove(disk_, slot_, utils::join_path(path_, name).c_str(), directory) == 0) {
        owner_->set_status("Deletion staged");
        owner_->model_changed();
        focus_list();
    }
}

void FileBrowserPanel::export_selection()
{
    if (!valid_target() || selection_.empty()) {
        return;
    }
    if (selection_.size() == 1) {
        export_entry(selection_.front());
        return;
    }
    Fl_Native_File_Chooser chooser;
    chooser.title("Choose the folder to export into");
    chooser.type(Fl_Native_File_Chooser::BROWSE_DIRECTORY);
    if (chooser.show() != 0) {
        return;
    }
    const std::string directory = chooser.filename();
    int failed = 0;
    {
        /* The progress window is destroyed before the list refreshes, so the
         * repaint covers the area it occupied. */
        ProgressDialog progress("Exporting selection", "");
        for (int entry_index : selection_) {
            const zfs_entry &entry = entries_[entry_index];
            progress.set_detail(entry.name);
            if (disk_fs_export(disk_,
                               slot_,
                               entry_path(entry_index).c_str(),
                               host_join(directory, entry.name).c_str(),
                               progress_cb,
                               &progress) != 0) {
                ++failed;
            }
        }
    }
    if (failed == 0) {
        owner_->set_status("Selection exported");
    } else {
        const std::string message = std::to_string(failed) + " of " + std::to_string(selection_.size()) +
                                    " exports failed; see the status bar";
        owner_->set_status(message.c_str());
    }
}

void FileBrowserPanel::delete_selection()
{
    if (!valid_target() || selection_.empty()) {
        return;
    }
    if (selection_.size() == 1) {
        delete_entry(selection_.front());
        return;
    }
    char prompt[128];
    std::snprintf(prompt,
                  sizeof(prompt),
                  "Stage the deletion of %d entries?\n"
                  "The entries are removed when the changes are applied.",
                  (int) selection_.size());
    if (fl_choice("%s", "Cancel", "Delete", nullptr, prompt) != 1) {
        return;
    }
    int failed = 0;
    for (int entry_index : selection_) {
        const zfs_entry &entry = entries_[entry_index];
        if (disk_fs_remove(disk_, slot_, entry_path(entry_index).c_str(), entry.directory) != 0) {
            ++failed;
        }
    }
    if (failed == 0) {
        owner_->set_status("Deletion staged");
    } else {
        const std::string message = std::to_string(failed) + " of " + std::to_string(selection_.size()) +
                                    " deletions failed; see the status bar";
        owner_->set_status(message.c_str());
    }
    owner_->model_changed();
    focus_list();
}

bool FileBrowserPanel::can_import() const
{
    return valid_target();
}

void FileBrowserPanel::clear_tooltip()
{
    Fl_Tooltip::exit(this);
    tip_kind_ = -1;
    tip_index_ = -1;
}

void FileBrowserPanel::set_drop_active(bool active)
{
    if (drop_active_ == active) {
        return;
    }
    drop_active_ = active;
    redraw();
}

void FileBrowserPanel::queue_drop_import(const std::vector<std::string> &host_paths)
{
    pending_drop_paths_ = host_paths;
    Fl::add_timeout(0.0, drop_import_cb, this);
}

void FileBrowserPanel::drop_import_cb(void *data)
{
    static_cast<FileBrowserPanel *>(data)->run_pending_drop_import();
}

void FileBrowserPanel::run_pending_drop_import()
{
    if (pending_drop_paths_.empty()) {
        return;
    }
    const std::vector<std::string> paths = pending_drop_paths_;
    pending_drop_paths_.clear();
    import_host_paths(paths);
}

void FileBrowserPanel::import_host_paths(const std::vector<std::string> &host_paths)
{
    if (!valid_target() || host_paths.empty()) {
        return;
    }
    namespace fs = std::filesystem;
    int imported = 0;
    int folders = 0;
    int failed = 0;
    {
        ProgressDialog progress("Importing dropped files", "");

        std::function<void(const fs::path &, const std::string &)> import_node =
            [&](const fs::path &host, const std::string &destination_dir) {
            const std::string name = host.filename().u8string();
            if (!utils::valid_entry_name(name)) {
                ++failed;
                return;
            }
                const std::string destination = utils::join_path(destination_dir, name);
                std::error_code ec;
                const bool directory = fs::is_directory(host, ec);
                if (directory && fs::is_symlink(host, ec)) {
                    /* Following a directory symlink could loop or escape the drop. */
                    ++failed;
                    return;
                }
                if (directory) {
                    /* Best effort: an existing folder simply receives the children. */
                    if (disk_fs_mkdir(disk_, slot_, destination.c_str()) == 0) {
                        ++folders;
                    }
                    for (const fs::directory_entry &child : fs::directory_iterator(host, ec)) {
                        import_node(child.path(), destination);
                    }
                    if (ec) {
                        ++failed;
                    }
                    return;
                }
                progress.set_detail(name);
                const std::string host_string = host.u8string();
                const char *host_path = host_string.c_str();
                if (disk_fs_import(disk_, slot_, &host_path, 1, destination.c_str(), progress_cb, &progress) != 0) {
                    ++failed;
                } else {
                    ++imported;
                }
            };

        for (const std::string &host_path : host_paths) {
            const fs::path host = fs::u8path(host_path);
            std::error_code ec;
            if (!fs::exists(host, ec)) {
                ++failed;
                continue;
            }
            import_node(host, path_);
        }
    }

    if (imported == 0 && folders == 0 && failed == 0) {
        owner_->set_status("Nothing to import");
    } else if (failed == 0) {
        std::string message;
        if (imported > 0) {
            message = std::to_string(imported) + " file" + (imported == 1 ? "" : "s");
        }
        if (folders > 0) {
            if (!message.empty()) {
                message += " and ";
            }
            message += std::to_string(folders) + " folder" + (folders == 1 ? "" : "s");
        }
        message += " imported";
        owner_->set_status(message.c_str());
    } else {
        const std::string message = std::to_string(failed) + " dropped item" + (failed == 1 ? "" : "s") +
                                    " failed; see the status bar";
        owner_->set_status(message.c_str());
    }
    owner_->model_changed();
    focus_list();
}

FileBrowserPanel::Hit FileBrowserPanel::hit_test(int mx, int my) const
{
    Hit hit;
    const int nav_y = y() + 4;
    if (my >= nav_y && my <= nav_y + 30) {
        if (mx >= nav_back_x_ && mx < nav_back_x_ + 30) {
            return {HitKind::Back, -1};
        }
        if (mx >= nav_forward_x_ && mx < nav_forward_x_ + 30) {
            return {HitKind::Forward, -1};
        }
        if (mx >= nav_up_x_ && mx < nav_up_x_ + 30) {
            return {HitKind::Up, -1};
        }
        if (mx >= nav_home_x_ && mx < nav_home_x_ + 30) {
            return {HitKind::Home, -1};
        }
        for (size_t index = 0; index < crumbs_.size(); ++index) {
            const Crumb &crumb = crumbs_[index];
            if (mx >= crumb.x && mx < crumb.x + crumb.w) {
                return {HitKind::Crumb, (int) index};
            }
        }
    }

    const int action_y = y() + nav_h_ + 4;
    if (my >= action_y && my <= action_y + 28) {
        if (mx >= import_x_ && mx < import_x_ + import_w_) {
            return {HitKind::Import, -1};
        }
        if (mx >= export_x_ && mx < export_x_ + export_w_) {
            return {HitKind::Export, -1};
        }
        if (mx >= new_folder_x_ && mx < new_folder_x_ + new_folder_w_) {
            return {HitKind::NewFolder, -1};
        }
        if (mx >= delete_x_ && mx < delete_x_ + delete_w_) {
            return {HitKind::Delete, -1};
        }
    }

    const int body_top = body_y_;
    const int body_bottom = y() + h() - 4;
    if (my < body_top || my > body_bottom) {
        return hit;
    }
    if (mx >= table_x_ && mx <= table_x_ + table_w_) {
        if (my < body_top + kTableHeader) {
            const Columns columns = table_columns(table_x_, table_w_);
            if (mx < columns.size_x) {
                return {HitKind::TableHeader, 0};
            }
            if (mx < columns.type_x) {
                return {HitKind::TableHeader, 1};
            }
            if (mx < columns.mod_x) {
                return {HitKind::TableHeader, 2};
            }
            return {HitKind::TableHeader, 3};
        }
        if (mx >= table_x_ + table_w_ - 8) {
            return {HitKind::TableScrollbar, -1};
        }
        const int row = (my - body_top - kTableHeader + table_scroll_) / kRowHeight;
        if (row >= 0 && row < (int) order_.size()) {
            return {HitKind::TableRow, row};
        }
    }
    if (mx >= x() + 4 && mx < table_x_ - 8 && my >= body_top + 4) {
        const int row = (my - body_top - 4 + tree_scroll_) / kTreeRowHeight;
        if (row >= 0 && row < (int) tree_rows_.size()) {
            return {HitKind::TreeRow, row};
        }
    }
    if (preview_action_w_ > 0 && mx >= preview_action_x_ && mx < preview_action_x_ + preview_action_w_ &&
        my >= preview_action_y_ && my < preview_action_y_ + preview_action_h_) {
        return {HitKind::PreviewAction, -1};
    }
    return hit;
}

void FileBrowserPanel::update_tooltip(const Hit &hit)
{
    const int kind = (int) hit.kind;
    if (kind == tip_kind_ && hit.index == tip_index_) {
        return;
    }
    tip_kind_ = kind;
    tip_index_ = hit.index;
    tip_text_.clear();

    switch (hit.kind) {
    case HitKind::Back:
        tip_text_ = "Back";
        break;
    case HitKind::Forward:
        tip_text_ = "Forward";
        break;
    case HitKind::Up:
        tip_text_ = "Up one level";
        break;
    case HitKind::Home:
        tip_text_ = "Root folder";
        break;
    case HitKind::Crumb:
        if (hit.index >= 0 && hit.index < (int) crumbs_.size()) {
            tip_text_ = "Go to " + crumbs_[hit.index].path;
        }
        break;
    case HitKind::Import:
        tip_text_ = "Import host files into this folder (Ctrl+I)";
        break;
    case HitKind::Export:
        tip_text_ = "Export the selection (Ctrl+E)";
        break;
    case HitKind::NewFolder:
        tip_text_ = "Create a folder in this folder";
        break;
    case HitKind::Delete:
        tip_text_ = "Delete the selected entry";
        break;
    case HitKind::TableHeader: {
        static const char *const labels[] = {"Name", "Size", "Type", "Modified"};
        if (hit.index >= 0 && hit.index < 4) {
            tip_text_ = std::string("Sort by ") + labels[hit.index];
        }
        break;
    }
    case HitKind::TableRow:
        if (hit.index >= 0 && hit.index < (int) order_.size()) {
            const zfs_entry &entry = entries_[order_[hit.index]];
            tip_text_ = entry.directory ? "Open this folder" : "Preview this file";
        }
        break;
    case HitKind::TreeRow:
        if (hit.index >= 0 && hit.index < (int) tree_rows_.size()) {
            tip_text_ = tree_rows_[hit.index].path;
        }
        break;
    case HitKind::PreviewAction:
        tip_text_ = "Open the folder or export the file";
        break;
    default:
        break;
    }

    Fl_Tooltip::exit(this);
    if (tip_text_.empty() || !Fl_Tooltip::enabled()) {
        return;
    }
    /* exit() first: the tip string lives in tip_text_, whose buffer address can
     * be reused for different short strings, which enter_area() treats as the
     * same tooltip. */
    Fl_Tooltip::enter_area(this, 0, Fl::event_y() - y(), w(), 22, tip_text_.c_str());
}

int FileBrowserPanel::handle(int event)
{
    switch (event) {
    case FL_PUSH: {
        /* Focus the list itself, not the search field, when clicking entries. */
        Fl::focus(this);
        if (Fl::event_button() != FL_LEFT_MOUSE) {
            return 1;
        }
        const Hit hit = hit_test(Fl::event_x(), Fl::event_y());
        switch (hit.kind) {
        case HitKind::Back:
            go_back();
            return 1;
        case HitKind::Forward:
            go_forward();
            return 1;
        case HitKind::Up:
            go_up();
            return 1;
        case HitKind::Home:
            navigate_to("/", true);
            return 1;
        case HitKind::Crumb:
            if (hit.index >= 0 && hit.index < (int) crumbs_.size()) {
                navigate_to(crumbs_[hit.index].path, true);
            }
            return 1;
        case HitKind::Import:
            import_files();
            return 1;
        case HitKind::Export:
            export_selection();
            return 1;
        case HitKind::NewFolder:
            new_folder();
            return 1;
        case HitKind::Delete:
            delete_selection();
            return 1;
        case HitKind::TableHeader:
            if (hit.index == sort_column_) {
                descending_ = !descending_;
            } else {
                sort_column_ = hit.index;
                descending_ = false;
            }
            rebuild_order();
            select_row(order_.empty() ? -1 : 0, false);
            table_scroll_ = 0;
            return 1;
        case HitKind::TableRow:
            if (hit.index >= 0 && hit.index < (int) order_.size()) {
                const int modifiers = Fl::event_state();
                const bool toggle = (modifiers & FL_COMMAND) != 0;
                const bool range = (modifiers & FL_SHIFT) != 0;
                if (toggle || range) {
                    select_row_with(hit.index, toggle, range);
                } else {
                    select_row(hit.index, false);
                }
                if (Fl::event_clicks() > 0) {
                    open_row(hit.index);
                }
            }
            return 1;
        case HitKind::TreeRow:
            if (hit.index >= 0 && hit.index < (int) tree_rows_.size()) {
                const TreeRow &row = tree_rows_[hit.index];
                const int indent = 16 + row.depth * 14;
                const int mx = Fl::event_x();
                if (row.has_children && mx >= x() + indent - 15 && mx < x() + indent) {
                    if (collapsed_.find(row.path) == collapsed_.end()) {
                        collapsed_.insert(row.path);
                    } else {
                        collapsed_.erase(row.path);
                    }
                    rebuild_tree();
                    redraw();
                } else {
                    navigate_to(row.path, true);
                }
            }
            return 1;
        case HitKind::PreviewAction: {
            const int preview_entry = selected_entry();
            if (preview_entry >= 0) {
                const zfs_entry &entry = entries_[preview_entry];
                if (entry.directory) {
                    navigate_to(entry_path(preview_entry), true);
                } else {
                    export_entry(preview_entry);
                }
            }
            return 1;
        }
        case HitKind::TableScrollbar: {
            table_scroll_drag_ = true;
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
        case HitKind::NoTarget:
            return 1;
        }
        return 1;
    }
    case FL_DRAG: {
        if (table_scroll_drag_) {
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
        }
        return 1;
    }
    case FL_RELEASE:
        if (table_scroll_drag_) {
            table_scroll_drag_ = false;
            Fl::pushed(nullptr);
        }
        return 1;
    case FL_MOVE: {
        const Hit hit = hit_test(Fl::event_x(), Fl::event_y());
        if ((int) hit.kind != hover_kind_ || hit.index != hover_index_) {
            hover_kind_ = (int) hit.kind;
            hover_index_ = hit.index;
            redraw();
        }
        update_tooltip(hit);
        return 1;
    }
    case FL_LEAVE:
        if (hover_kind_ != -1) {
            hover_kind_ = -1;
            hover_index_ = -1;
            redraw();
        }
        Fl_Tooltip::exit(this);
        tip_kind_ = -1;
        tip_index_ = -1;
        return 1;
    case FL_MOUSEWHEEL: {
        const int amount = -Fl::event_dy() * 42;
        if (Fl::event_x() < table_x_) {
            scroll_tree(amount);
        } else if (Fl::event_x() >= table_x_ && Fl::event_x() <= preview_x_) {
            scroll_table(amount);
        }
        return 1;
    }
    case FL_KEYDOWN: {
        const int key = Fl::event_key();
        const int state = Fl::event_state();
        if ((state & FL_CTRL) != 0 && (key == 'l' || key == 'L')) {
            Fl::focus(search_);
            redraw();
            return 1;
        }
        if (key == FL_Escape) {
            clear_search();
            return 1;
        }
        if ((state & FL_COMMAND) != 0 && (key == 'a' || key == 'A')) {
            select_all();
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
            } else if (key == FL_End) {
                row = (int) order_.size() - 1;
            }
            if ((state & FL_SHIFT) != 0) {
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
        if (key == FL_BackSpace) {
            go_up();
            return 1;
        }
        const char *text = Fl::event_text();
        if (text != nullptr && text[0] >= 32 && text[0] < 127 && (state & (FL_CTRL | FL_ALT)) == 0) {
            Fl::focus(search_);
            search_->value("");
            search_->insert(text, 1);
            utils::set_input_position(search_, 1);
            filter_ = search_->value();
            rebuild_order();
            select_row(order_.empty() ? -1 : 0, false);
            return 1;
        }
        return 0;
    }
    default:
        break;
    }
    return Fl_Group::handle(event);
}
