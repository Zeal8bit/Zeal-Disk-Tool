/**
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "BrowserWindow.hpp"

#include <FL/Fl.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Table_Row.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

#include "Gui.hpp"
#include "MainWindow.hpp"

/* File list with sortable column headers. */
class EntryTable : public Fl_Table_Row {
public:
    EntryTable(int x, int y, int w, int h, BrowserWindow *owner) : Fl_Table_Row(x, y, w, h), owner_(owner)
    {
        color(gui::panel);
        box(FL_DOWN_BOX);
        selection_color(gui::selection);
        col_header(1);
        col_header_height(28);
        col_resize(1);
        row_header(0);
        row_height_all(28);
        cols(4);
        col_width(0, 300);
        col_width(1, 90);
        col_width(2, 80);
        col_width(3, 150);
        type(SELECT_SINGLE);
        when(FL_WHEN_RELEASE | FL_WHEN_CHANGED);
        callback(table_cb, this);
        rows(0);
        end();
    }

    void refresh()
    {
        rows(owner_->row_count());
        row_height_all(28);
        update_column_widths();
        redraw();
    }

    void resize(int x, int y, int w, int h)
    {
        Fl_Table_Row::resize(x, y, w, h);
        update_column_widths();
    }

    int handle(int event)
    {
        if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE) {
            if (Fl::event_clicks() > 0) {
                const int row = row_at(Fl::event_y());
                if (row >= 0 && row < owner_->row_count()) {
                    owner_->activate_row(row);
                    return 1;
                }
            }
            const int column = column_at(Fl::event_x(), Fl::event_y());
            if (column >= 0) {
                owner_->sort_by(column);
                return 1;
            }
        }
        return Fl_Table_Row::handle(event);
    }

protected:
    void draw_cell(TableContext context, int R, int C, int X, int Y, int W, int H)
    {
        static const char *headers[] = {"Name", "Size", "Type", "Modified"};

        if (context == CONTEXT_COL_HEADER) {
            fl_push_clip(X, Y, W, H);
            fl_color(gui::header);
            fl_rectf(X, Y, W, H);
            fl_color(gui::border);
            fl_line(X, Y + H - 1, X + W, Y + H - 1);
            if (R == 0 && C >= 0 && C < 4) {
                fl_font(FL_HELVETICA_BOLD, 11);
                fl_color(gui::text_muted);
                fl_draw(headers[C], X + 10, Y, W - 30, H, FL_ALIGN_LEFT | FL_ALIGN_CLIP);
                if (C == owner_->sort_column()) {
                    const int arrow_x = X + 10 + (int) fl_width(headers[C]) + 7;
                    if (arrow_x + 8 < X + W) {
                        if (owner_->descending()) {
                            fl_polygon(arrow_x, Y + 12, arrow_x + 8, Y + 12, arrow_x + 4, Y + 19);
                        } else {
                            fl_polygon(arrow_x, Y + 19, arrow_x + 8, Y + 19, arrow_x + 4, Y + 12);
                        }
                    }
                }
            }
            fl_pop_clip();
            return;
        }
        if (context != CONTEXT_CELL) {
            return;
        }

        const int index = owner_->row_entry(R);
        const zfs_entry *entry = owner_->entry_at(index);
        if (entry == nullptr) {
            return;
        }
        const bool selected = row_selected(R) != 0;
        const Fl_Color background = selected ? gui::selection : (R % 2 != 0 ? gui::panel_alt : gui::panel);
        const Fl_Color foreground = selected ? gui::selection_text : gui::text;

        fl_push_clip(X, Y, W, H);
        fl_color(background);
        fl_rectf(X, Y, W, H);
        fl_color(gui::border);
        fl_line(X, Y + H - 1, X + W, Y + H - 1);
        if (selected && C == 0) {
            fl_color(gui::accent);
            fl_rectf(X, Y, 3, H);
        }

        char buffer[128];
        fl_font(FL_HELVETICA, 12);
        fl_color(foreground);
        switch (C) {
        case 0: {
            if (entry->directory) {
                fl_color(gui::accent);
                fl_rectf(X + 11, Y + 9, 14, 10);
                fl_rectf(X + 11, Y + 6, 7, 4);
                fl_color(selected ? gui::selection_text : gui::text);
            } else {
                fl_color(gui::text_muted);
                fl_rect(X + 11, Y + 8, 12, 14);
                fl_line(X + 19, Y + 8, X + 23, Y + 12);
                fl_line(X + 23, Y + 12, X + 23, Y + 22);
                fl_line(X + 19, Y + 8, X + 23, Y + 12);
                fl_color(foreground);
            }
            std::snprintf(buffer, sizeof(buffer), "%s%s", entry->name, entry->directory ? "/" : "");
            fl_draw(buffer, X + 32, Y, W - 38, H, FL_ALIGN_LEFT | FL_ALIGN_CLIP);
            break;
        }
        case 1:
            if (!entry->directory) {
                std::snprintf(buffer, sizeof(buffer), "%s", gui::format_file_size(entry->size).c_str());
                fl_draw(buffer, X + 8, Y, W - 16, H, FL_ALIGN_RIGHT | FL_ALIGN_CLIP);
            } else {
                fl_color(gui::text_muted);
                fl_draw("-", X + 8, Y, W - 16, H, FL_ALIGN_RIGHT | FL_ALIGN_CLIP);
            }
            break;
        case 2:
            fl_draw(entry->directory ? "Folder" : "File", X + 10, Y, W - 16, H, FL_ALIGN_LEFT | FL_ALIGN_CLIP);
            break;
        case 3:
            std::snprintf(buffer,
                          sizeof(buffer),
                          "%04u-%02u-%02u %02u:%02u",
                          entry->year,
                          entry->month,
                          entry->day,
                          entry->hour,
                          entry->minute);
            fl_draw(buffer, X + 10, Y, W - 16, H, FL_ALIGN_LEFT | FL_ALIGN_CLIP);
            break;
        default:
            break;
        }
        fl_pop_clip();
    }

private:
    void update_column_widths()
    {
        col_width(0, std::max(160, w() - 90 - 80 - 150 - 12));
        col_width(1, 90);
        col_width(2, 80);
        col_width(3, 150);
    }

    int row_at(int py)
    {
        for (int row = 0; row < rows(); ++row) {
            int x = 0;
            int y = 0;
            int w = 0;
            int h = 0;
            if (find_cell(CONTEXT_CELL, row, 0, x, y, w, h) == 0 && py >= y && py < y + h) {
                return row;
            }
        }
        return -1;
    }

    int column_at(int px, int py)
    {
        for (int column = 0; column < cols(); ++column) {
            int x = 0;
            int y = 0;
            int w = 0;
            int h = 0;
            if (find_cell(CONTEXT_COL_HEADER, 0, column, x, y, w, h) != 0) {
                continue;
            }
            if (py >= y && py < y + h && px >= x && px < x + w) {
                return column;
            }
        }
        return -1;
    }

    static void table_cb(Fl_Widget *widget, void *)
    {
        auto *table = static_cast<EntryTable *>(widget);
        const TableContext context = table->callback_context();
        if (context != CONTEXT_CELL && context != CONTEXT_ROW_HEADER) {
            return;
        }
        const int row = table->callback_row();
        if (row >= 0 && row < table->owner_->row_count()) {
            table->owner_->row_selected(row);
        }
    }

    BrowserWindow *owner_;
};

namespace {

/* Enable or disable a widget from a condition. */
void set_enabled(Fl_Widget *widget, bool active)
{
    if (active) {
        widget->activate();
    } else {
        widget->deactivate();
    }
}

/* Modal progress window used while the synchronous core copies data. */
class ProgressDialog {
public:
    ProgressDialog(const char *title, const std::string &detail) : detail_(detail)
    {
        window_ = new Fl_Window(400, 118, title);
        window_->set_modal();
        window_->color(gui::background);
        label_ = new Fl_Box(FL_NO_BOX, 16, 12, 368, 40, detail.c_str());
        label_->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
        label_->labelsize(12);
        label_->labelcolor(gui::text);
        progress_ = new Fl_Progress(16, 58, 368, 22);
        progress_->minimum(0);
        progress_->maximum(100);
        progress_->value(0);
        progress_->box(FL_DOWN_BOX);
        progress_->color(gui::panel_alt);
        progress_->selection_color(gui::accent);
        progress_->labelsize(0);
        window_->end();
        window_->position((Fl::w() - window_->w()) / 2, (Fl::h() - window_->h()) / 2);
        window_->show();
        Fl::check();
    }

    ~ProgressDialog() { delete window_; }

    void update(uint64_t done, uint64_t total)
    {
        const int percent = total != 0 ? (int) (done * 100 / total) : 100;
        progress_->value(percent);
        char buffer[256];
        std::snprintf(buffer,
                      sizeof(buffer),
                      "%s\n%s of %s",
                      detail_.c_str(),
                      gui::format_size(done).c_str(),
                      gui::format_size(total).c_str());
        label_->copy_label(buffer);
        Fl::check();
    }

private:
    Fl_Window *window_ = nullptr;
    Fl_Box *label_ = nullptr;
    Fl_Progress *progress_ = nullptr;
    std::string detail_;
};

void progress_cb(void *user, uint64_t done, uint64_t total)
{
    static_cast<ProgressDialog *>(user)->update(done, total);
}

}  // namespace

BrowserWindow::BrowserWindow(MainWindow *owner) : Fl_Double_Window(680, 560, "Files"), owner_(owner)
{
    color(gui::background);
    begin();

    up_button_ = new Fl_Button(12, 12, 54, 26, "Up");
    up_button_->labelsize(12);
    up_button_->callback(up_cb, this);
    up_button_->tooltip("Go to the parent directory");

    path_input_ = new Fl_Input(74, 12, 300, 26);
    path_input_->textsize(12);
    path_input_->when(FL_WHEN_ENTER_KEY | FL_WHEN_RELEASE);
    path_input_->callback(go_cb, this);
    path_input_->tooltip("Absolute path inside the ZealFS partition, then press Enter");

    go_button_ = new Fl_Button(382, 12, 54, 26, "Go");
    go_button_->labelsize(12);
    go_button_->callback(go_cb, this);
    refresh_button_ = new Fl_Button(444, 12, 74, 26, "Refresh");
    refresh_button_->labelsize(12);
    refresh_button_->callback(refresh_cb, this);

    import_button_ = new Fl_Button(12, 50, 104, 30, "Import files...");
    import_button_->labelsize(12);
    import_button_->callback(import_cb, this);
    import_button_->tooltip("Stage one or more host files in this directory");
    export_button_ = new Fl_Button(124, 50, 104, 30, "Export...");
    export_button_->labelsize(12);
    export_button_->callback(export_cb, this);
    export_button_->tooltip("Export the selected file or folder to the host");
    mkdir_button_ = new Fl_Button(236, 50, 104, 30, "New folder");
    mkdir_button_->labelsize(12);
    mkdir_button_->callback(mkdir_cb, this);
    delete_button_ = new Fl_Button(348, 50, 88, 30, "Delete");
    delete_button_->labelsize(12);
    delete_button_->callback(delete_cb, this);
    delete_button_->tooltip("Stage the removal of the selected entry");

    count_label_ = new Fl_Box(FL_NO_BOX, 450, 56, 200, 20, "");
    count_label_->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    count_label_->labelsize(11);
    count_label_->labelcolor(gui::text_muted);

    table_ = new EntryTable(12, 90, 656, 380, this);

    usage_label_ = new Fl_Box(FL_NO_BOX, 12, 470, 656, 16, "");
    usage_label_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    usage_label_->labelsize(11);
    usage_label_->labelcolor(gui::text_muted);

    usage_bar_ = new Fl_Progress(12, 490, 656, 18);
    usage_bar_->minimum(0);
    usage_bar_->maximum(100);
    usage_bar_->value(0);
    usage_bar_->box(FL_DOWN_BOX);
    usage_bar_->color(gui::panel_alt);
    usage_bar_->selection_color(gui::accent);
    usage_bar_->labelsize(0);

    status_ = new Fl_Box(FL_FLAT_BOX, 0, 534, 680, 26, "");
    status_->color(gui::panel);
    status_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    status_->labelsize(12);
    status_->labelcolor(gui::text_muted);

    end();

    size_range(540, 380);
    callback(window_cb, this);
    layout_children();
    update_buttons();
    set_status("Double-click a folder to open it; double-click a file to export it.");
}

BrowserWindow::~BrowserWindow()
{
    clear_entries();
}

void BrowserWindow::layout_children()
{
    if (up_button_ == nullptr) {
        return;
    }
    const int margin = 12;
    const int status_y = h() - 26;
    const int right = w() - margin;

    up_button_->resize(margin, 12, 54, 26);
    refresh_button_->resize(right - 74, 12, 74, 26);
    go_button_->resize(right - 74 - 8 - 54, 12, 54, 26);
    const int path_x = margin + 54 + 8;
    path_input_->resize(path_x, 12, std::max(80, go_button_->x() - 8 - path_x), 26);

    import_button_->resize(margin, 50, 104, 30);
    export_button_->resize(margin + 112, 50, 104, 30);
    mkdir_button_->resize(margin + 224, 50, 104, 30);
    delete_button_->resize(margin + 336, 50, 88, 30);
    const int count_x = delete_button_->x() + delete_button_->w() + 8;
    count_label_->resize(count_x, 56, std::max(0, right - count_x), 20);

    const int table_y = 90;
    const int usage_label_y = status_y - 56;
    const int usage_bar_y = status_y - 34;
    table_->resize(margin, table_y, w() - margin * 2, std::max(100, usage_label_y - 8 - table_y));
    usage_label_->resize(margin, usage_label_y, w() - margin * 2, 16);
    usage_bar_->resize(margin, usage_bar_y, w() - margin * 2, 18);
    status_->resize(0, status_y, w(), 26);
    redraw();
}

bool BrowserWindow::valid_target() const
{
    return disk_ != nullptr && disk_->valid && partition_ >= 0 && partition_ < MAX_PART_COUNT &&
           disk_->partitions[partition_].active && disk_->partitions[partition_].type == ZEALFS_TYPE;
}

void BrowserWindow::retarget(disk_info_t *disk, int partition)
{
    if (disk == disk_ && partition == partition_) {
        const unsigned revision = disk != nullptr ? disk->revision : 0;
        if (revision != revision_) {
            revision_ = revision;
            reload_or_root();
        } else {
            refresh_usage();
            update_title();
        }
        return;
    }
    disk_ = disk;
    partition_ = partition;
    revision_ = disk != nullptr ? disk->revision : 0;
    path_ = "/";
    clear_entries();
    reload_or_root();
}

void BrowserWindow::reload_or_root()
{
    if (!reload() && path_ != "/") {
        path_ = "/";
        reload();
    }
}

bool BrowserWindow::reload()
{
    std::string previous_name;
    if (selected_row_ >= 0 && selected_row_ < (int) order_.size()) {
        const int index = order_[selected_row_];
        if (index >= 0 && index < (int) count_) {
            previous_name = entries_[index].name;
        }
    }
    clear_entries();

    if (!valid_target()) {
        table_->refresh();
        update_title();
        update_buttons();
        path_input_->value(path_.c_str());
        gui::set_input_position(path_input_, (int) path_.size());
        return false;
    }

    zfs_entry *entries = nullptr;
    size_t count = 0;
    if (disk_fs_list(disk_, partition_, path_.c_str(), &entries, &count) != 0) {
        table_->refresh();
        update_title();
        update_buttons();
        refresh_usage();
        path_input_->value(path_.c_str());
        gui::set_input_position(path_input_, (int) path_.size());
        return false;
    }

    entries_ = entries;
    count_ = count;
    rebuild_order();
    table_->refresh();

    int row = -1;
    if (!previous_name.empty()) {
        for (int index = 0; index < (int) order_.size(); ++index) {
            if (previous_name == entries_[order_[index]].name) {
                row = index;
                break;
            }
        }
    }
    if (row < 0 && !order_.empty()) {
        row = 0;
    }
    select_row(row);
    path_input_->value(path_.c_str());
    gui::set_input_position(path_input_, (int) path_.size());
    update_title();
    refresh_usage();
    update_buttons();
    return true;
}

void BrowserWindow::clear_entries()
{
    zfs_entries_free(entries_);
    entries_ = nullptr;
    count_ = 0;
    order_.clear();
    selected_row_ = -1;
}

const zfs_entry *BrowserWindow::entry_at(int index) const
{
    return index >= 0 && index < (int) count_ ? &entries_[index] : nullptr;
}

int BrowserWindow::row_entry(int row) const
{
    return row >= 0 && row < (int) order_.size() ? order_[row] : -1;
}

void BrowserWindow::rebuild_order()
{
    order_.resize(count_);
    std::iota(order_.begin(), order_.end(), 0);
    const bool descending = descending_;
    const int column = sort_column_;
    const auto timestamp = [](const zfs_entry &entry) {
        const uint64_t days = ((uint64_t) entry.year * 100 + entry.month) * 100 + entry.day;
        return ((days * 100 + entry.hour) * 100 + entry.minute) * 100 + entry.second;
    };
    std::sort(order_.begin(), order_.end(), [this, column, descending, timestamp](int first, int second) {
        const zfs_entry &a = entries_[first];
        const zfs_entry &b = entries_[second];
        int result = 0;
        switch (column) {
        case 1:
            result = a.size < b.size ? -1 : a.size > b.size ? 1 : 0;
            break;
        case 2:
            result = (int) b.directory - (int) a.directory;
            break;
        case 3: {
            const uint64_t left = timestamp(a);
            const uint64_t right = timestamp(b);
            result = left < right ? -1 : left > right ? 1 : 0;
            break;
        }
        case 0:
        default:
            if (a.directory != b.directory) {
                result = a.directory ? -1 : 1;
            } else {
                result = std::strcmp(a.name, b.name);
            }
            break;
        }
        if (result == 0) {
            result = std::strcmp(a.name, b.name);
        }
        return descending ? result > 0 : result < 0;
    });
}

void BrowserWindow::sort_by(int column)
{
    std::string previous_name;
    if (selected_row_ >= 0 && selected_row_ < (int) order_.size()) {
        const int index = order_[selected_row_];
        if (index >= 0 && index < (int) count_) {
            previous_name = entries_[index].name;
        }
    }
    if (column == sort_column_) {
        descending_ = !descending_;
    } else {
        sort_column_ = column;
        descending_ = false;
    }
    rebuild_order();
    table_->refresh();
    int row = -1;
    if (!previous_name.empty()) {
        for (int index = 0; index < (int) order_.size(); ++index) {
            if (previous_name == entries_[order_[index]].name) {
                row = index;
                break;
            }
        }
    }
    if (row < 0 && !order_.empty()) {
        row = 0;
    }
    select_row(row);
}

void BrowserWindow::select_row(int row)
{
    if (row < 0 || row >= row_count()) {
        row = -1;
    }
    updating_ = true;
    if (row >= 0) {
        /* SELECT_SINGLE clears every other row by itself. */
        table_->select_row(row, 1);
    } else {
        table_->select_all_rows(0);
    }
    table_->redraw();
    updating_ = false;
    selected_row_ = row;
    update_buttons();
}

void BrowserWindow::row_selected(int row)
{
    if (updating_) {
        return;
    }
    selected_row_ = row;
    update_buttons();
}

void BrowserWindow::activate_row(int row)
{
    const zfs_entry *entry = entry_at(row_entry(row));
    if (entry == nullptr) {
        return;
    }
    if (entry->directory) {
        path_ = gui::join_path(path_, entry->name);
        reload_or_root();
        table_->take_focus();
    } else {
        export_entry(row_entry(row));
    }
}

void BrowserWindow::update_title()
{
    if (!valid_target()) {
        copy_label("Files");
        return;
    }
    const std::string title = std::string("Files - ") + disk_->name + " - partition " +
                              std::to_string(partition_) + " - " + path_;
    copy_label(title.c_str());
}

void BrowserWindow::update_buttons()
{
    const bool target = valid_target();
    const bool has_selection = selected_row_ >= 0 && selected_row_ < row_count();
    set_enabled(import_button_, target);
    set_enabled(mkdir_button_, target);
    set_enabled(export_button_, target && has_selection);
    set_enabled(delete_button_, target && has_selection);
    set_enabled(up_button_, target && path_ != "/");
    set_enabled(go_button_, target);
    set_enabled(refresh_button_, target);

    char count[64];
    std::snprintf(count, sizeof(count), "%d entr%s", row_count(), row_count() == 1 ? "y" : "ies");
    count_label_->copy_label(target ? count : "");
}

void BrowserWindow::refresh_usage()
{
    uint64_t used = 0;
    uint64_t total = 0;
    if (valid_target() && disk_fs_usage(disk_, partition_, &used, &total) == 0 && total > 0) {
        const int percent = gui::percent_of(used, total);
        usage_bar_->value(percent);
        usage_bar_->selection_color(percent >= 90 ? gui::warning : gui::accent);
        usage_bar_->redraw();
        char text[192];
        std::snprintf(text,
                      sizeof(text),
                      "%d%% used - %s free of %s",
                      percent,
                      gui::format_size(total >= used ? total - used : 0).c_str(),
                      gui::format_size(total).c_str());
        usage_label_->copy_label(text);
    } else {
        usage_bar_->value(0);
        usage_label_->copy_label(valid_target() ? "Usage not available" : "");
    }
}

void BrowserWindow::focus_files()
{
    if (table_ != nullptr) {
        table_->take_focus();
    }
}

void BrowserWindow::set_status(const char *message)
{
    status_text_ = message != nullptr ? message : "";
    if (status_ != nullptr) {
        status_->copy_label(("  " + status_text_).c_str());
        status_->redraw();
    }
}

void BrowserWindow::go_up()
{
    const std::string parent = gui::parent_path(path_);
    if (parent == path_) {
        set_status("Already at the root of the partition");
        return;
    }
    path_ = parent;
    reload();
}

void BrowserWindow::go_path(const std::string &text)
{
    const std::string normalized = gui::normalize_path(text);
    if (normalized == path_) {
        reload();
        return;
    }
    const std::string previous = path_;
    path_ = normalized;
    if (!reload()) {
        path_ = previous;
        reload();
        set_status("Directory not found; staying in the previous folder");
    }
}

void BrowserWindow::new_folder()
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
    if (!gui::valid_entry_name(folder)) {
        set_status("Invalid folder name: use 1 to 16 bytes and no slashes");
        return;
    }
    if (disk_fs_mkdir(disk_, partition_, gui::join_path(path_, folder).c_str()) == 0) {
        set_status("Folder creation staged");
        owner_->model_changed();
    }
}

void BrowserWindow::import_files()
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

    std::vector<std::string> batch;
    bool success = true;
    const int selected = chooser.count();
    for (int index = 0; index < selected; ++index) {
        std::string host = chooser.filename(index);
        std::string name = gui::base_name(host);
        if (!gui::valid_entry_name(name)) {
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
            std::string cleaned = gui::normalize_path(std::string("/") + renamed).substr(1);
            if (!gui::valid_entry_name(cleaned)) {
                set_status("Skipped a file with an invalid name");
                continue;
            }
            ProgressDialog progress("Importing file", name);
            const char *host_path = host.c_str();
            std::string destination = gui::join_path(path_, cleaned);
            if (disk_fs_import(disk_, partition_, &host_path, 1, destination.c_str(), progress_cb, &progress) != 0) {
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
        std::string destination = path_ == "/" ? "/" : path_ + "/";
        ProgressDialog progress("Importing files", path_);
        if (disk_fs_import(disk_, partition_, hosts.data(), hosts.size(), destination.c_str(), progress_cb, &progress) !=
            0) {
            success = false;
        }
    }
    owner_->model_changed();
    set_status(success ? "Import staged" : "Import failed; see the status bar");
    table_->take_focus();
}

void BrowserWindow::export_entry(int entry_index)
{
    const zfs_entry *entry = entry_at(entry_index);
    if (!valid_target() || entry == nullptr) {
        return;
    }
    Fl_Native_File_Chooser chooser;
    chooser.title(entry->directory ? "Choose where to export the folder" : "Choose where to save the file");
    chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
    chooser.filter("All files\t*");
    chooser.preset_file(entry->name);
    if (chooser.show() != 0) {
        return;
    }
    const std::string filesystem_path = gui::join_path(path_, entry->name);
    ProgressDialog progress(entry->directory ? "Exporting folder" : "Exporting file", entry->name);
    if (disk_fs_export(disk_, partition_, filesystem_path.c_str(), chooser.filename(), progress_cb, &progress) == 0) {
        set_status(entry->directory ? "Folder exported" : "File exported");
    } else {
        set_status("Export failed; see the status bar");
    }
}

void BrowserWindow::delete_entry()
{
    const zfs_entry *entry = entry_at(row_entry(selected_row_));
    if (!valid_target() || entry == nullptr) {
        return;
    }
    const std::string name = entry->name;
    const bool directory = entry->directory;
    if (fl_choice("Stage the deletion of \"%s\"?\n"
                  "The entry is removed when the changes are applied.",
                  "Cancel",
                  directory ? "Delete folder" : "Delete file",
                  nullptr,
                  name.c_str()) != 1) {
        return;
    }
    if (disk_fs_remove(disk_, partition_, gui::join_path(path_, name).c_str(), directory) == 0) {
        set_status("Deletion staged");
        owner_->model_changed();
    }
}

void BrowserWindow::up_cb(Fl_Widget *, void *data)
{
    static_cast<BrowserWindow *>(data)->go_up();
}

void BrowserWindow::go_cb(Fl_Widget *, void *data)
{
    auto *browser = static_cast<BrowserWindow *>(data);
    browser->go_path(browser->path_input_->value());
}

void BrowserWindow::refresh_cb(Fl_Widget *, void *data)
{
    auto *browser = static_cast<BrowserWindow *>(data);
    browser->reload_or_root();
    browser->set_status("Directory refreshed");
}

void BrowserWindow::import_cb(Fl_Widget *, void *data)
{
    static_cast<BrowserWindow *>(data)->import_files();
}

void BrowserWindow::export_cb(Fl_Widget *, void *data)
{
    auto *browser = static_cast<BrowserWindow *>(data);
    browser->export_entry(browser->row_entry(browser->selected_row_));
}

void BrowserWindow::mkdir_cb(Fl_Widget *, void *data)
{
    static_cast<BrowserWindow *>(data)->new_folder();
}

void BrowserWindow::delete_cb(Fl_Widget *, void *data)
{
    static_cast<BrowserWindow *>(data)->delete_entry();
}

void BrowserWindow::window_cb(Fl_Widget *widget, void *)
{
    widget->hide();
}

void BrowserWindow::resize(int x, int y, int w, int h)
{
    Fl_Double_Window::resize(x, y, w, h);
    layout_children();
}
