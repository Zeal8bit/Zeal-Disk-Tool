/**
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "MainWindow.hpp"

#include <FL/Fl.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Table_Row.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "app_version.h"
#include "BrowserWindow.hpp"
#include "Dialogs.hpp"
#include "Gui.hpp"
#include "PartitionBar.hpp"

/* Partition table, one row per active slot. */
class PartitionTable : public Fl_Table_Row {
public:
    PartitionTable(int x, int y, int w, int h, MainWindow *owner) : Fl_Table_Row(x, y, w, h), owner_(owner)
    {
        color(gui::panel);
        box(FL_DOWN_BOX);
        selection_color(gui::selection);
        col_header(1);
        col_header_height(28);
        col_resize(1);
        row_header(0);
        row_height_all(32);
        cols(5);
        for (int column = 0; column < 5; ++column) {
            col_width(column, column == 1 ? 200 : 110);
        }
        col_width(0, 46);
        type(SELECT_SINGLE);
        when(FL_WHEN_RELEASE | FL_WHEN_CHANGED);
        callback(table_cb, this);
        rows(0);
        end();
    }

    void refresh()
    {
        slots_.clear();
        usage_.clear();
        disk_info_t *disk = owner_->current_disk();
        if (disk != nullptr && disk->valid) {
            for (int slot = 0; slot < MAX_PART_COUNT; ++slot) {
                const partition_t &part = disk->staged_partitions[slot];
                if (!part.active || part.size_sectors == 0) {
                    continue;
                }
                int percent = -1;
                if (part.type == ZEALFS_TYPE) {
                    uint64_t used = 0;
                    uint64_t total = 0;
                    if (disk_fs_usage(disk, slot, &used, &total) == 0) {
                        percent = gui::percent_of(used, total);
                    }
                }
                slots_.push_back(slot);
                usage_.push_back(percent);
            }
        }
        rows((int) slots_.size());
        row_height_all(32);
        update_column_widths();
        select_slot(owner_->selected_slot());
        redraw();
    }

    void select_slot(int slot)
    {
        /* In SELECT_SINGLE mode, selecting the wanted row clears the others.
         * Clearing them explicitly afterwards would clear the wanted row too. */
        bool found = false;
        for (int row = 0; row < (int) slots_.size(); ++row) {
            if (slots_[row] == slot) {
                select_row(row, 1);
                found = true;
                break;
            }
        }
        if (!found) {
            select_all_rows(0);
        }
        redraw();
    }

    void resize(int x, int y, int w, int h)
    {
        Fl_Table_Row::resize(x, y, w, h);
        update_column_widths();
    }

    int handle(int event)
    {
        if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE && Fl::event_clicks() > 0) {
            const int row = row_at(Fl::event_y());
            if (row >= 0 && row < (int) slots_.size()) {
                owner_->select_partition(slots_[row]);
                owner_->open_browser();
                return 1;
            }
        }
        return Fl_Table_Row::handle(event);
    }

protected:
    void draw_cell(TableContext context, int R, int C, int X, int Y, int W, int H)
    {
        static const char *headers[] = {"#", "Filesystem", "Start address", "Size", "Usage"};

        if (context == CONTEXT_COL_HEADER) {
            fl_push_clip(X, Y, W, H);
            fl_color(gui::header);
            fl_rectf(X, Y, W, H);
            fl_color(gui::border);
            fl_line(X, Y + H - 1, X + W, Y + H - 1);
            if (R == 0 && C >= 0 && C < 5) {
                fl_font(FL_HELVETICA_BOLD, 11);
                fl_color(gui::text_muted);
                fl_draw(headers[C], X + 10, Y, W - 20, H, FL_ALIGN_LEFT | FL_ALIGN_CLIP);
            }
            fl_pop_clip();
            return;
        }
        if (context != CONTEXT_CELL) {
            return;
        }

        disk_info_t *disk = owner_->current_disk();
        if (disk == nullptr || R < 0 || R >= (int) slots_.size()) {
            return;
        }
        const int slot = slots_[R];
        const partition_t &part = disk->staged_partitions[slot];
        const bool selected = row_selected(R) != 0;
        const Fl_Color background = selected ? gui::selection : (R % 2 != 0 ? gui::panel_alt : gui::panel);

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
        fl_color(selected ? gui::selection_text : gui::text);
        switch (C) {
        case 0:
            fl_color(gui::partition_color(slot));
            fl_rounded_rectf(X + 10, Y + (H - 14) / 2, 14, 14, 3);
            std::snprintf(buffer, sizeof(buffer), "%d", slot);
            fl_color(selected ? gui::selection_text : gui::text);
            fl_draw(buffer, X + 30, Y, W - 34, H, FL_ALIGN_LEFT | FL_ALIGN_CLIP);
            break;
        case 1:
            fl_draw(disk_get_fs_type(part.type), X + 10, Y, W - 16, H, FL_ALIGN_LEFT | FL_ALIGN_CLIP);
            break;
        case 2:
            std::snprintf(buffer,
                          sizeof(buffer),
                          "0x%08llX",
                          (unsigned long long) part.start_lba * (unsigned long long) DISK_SECTOR_SIZE);
            fl_draw(buffer, X + 10, Y, W - 16, H, FL_ALIGN_LEFT | FL_ALIGN_CLIP);
            break;
        case 3: {
            char size[32];
            disk_get_size_str((uint64_t) part.size_sectors * DISK_SECTOR_SIZE, size, sizeof(size));
            fl_draw(size, X + 10, Y, W - 16, H, FL_ALIGN_LEFT | FL_ALIGN_CLIP);
            break;
        }
        case 4: {
            if (usage_[R] >= 0) {
                std::snprintf(buffer, sizeof(buffer), "%d%%", usage_[R]);
                fl_draw(buffer, X + 6, Y, W - 16, H, FL_ALIGN_RIGHT | FL_ALIGN_CLIP);
            } else {
                fl_color(gui::text_muted);
                fl_draw("-", X + 6, Y, W - 16, H, FL_ALIGN_RIGHT | FL_ALIGN_CLIP);
            }
            break;
        }
        default:
            break;
        }
        fl_pop_clip();
    }

private:
    void update_column_widths()
    {
        col_width(1, std::max(150, w() - 46 - 110 - 110 - 86 - 12));
        col_width(2, 110);
        col_width(3, 110);
        col_width(4, 86);
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

    static void table_cb(Fl_Widget *widget, void *)
    {
        auto *table = static_cast<PartitionTable *>(widget);
        if (table->owner_->updating()) {
            return;
        }
        const TableContext context = table->callback_context();
        if (context != CONTEXT_CELL && context != CONTEXT_ROW_HEADER) {
            return;
        }
        const int row = table->callback_row();
        if (row >= 0 && row < (int) table->slots_.size()) {
            table->owner_->select_partition(table->slots_[row]);
        }
    }

    MainWindow *owner_;
    std::vector<int> slots_;
    std::vector<int> usage_;
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



constexpr int kMenuHeight = 26;
constexpr int kToolbarHeight = 46;
constexpr int kStatusHeight = 28;
constexpr int kBarHeight = 96;
constexpr int kPanelWidth = 268;

void cb_open_image(Fl_Widget *, void *data)
{
    static_cast<MainWindow *>(data)->open_image();
}

void cb_new_image(Fl_Widget *, void *data)
{
    static_cast<MainWindow *>(data)->new_image();
}

void cb_refresh_devices(Fl_Widget *, void *data)
{
    static_cast<MainWindow *>(data)->refresh_devices();
}

void cb_apply(Fl_Widget *, void *data)
{
    static_cast<MainWindow *>(data)->apply_changes();
}

void cb_cancel(Fl_Widget *, void *data)
{
    static_cast<MainWindow *>(data)->cancel_changes();
}

void cb_quit(Fl_Widget *, void *data)
{
    auto *window = static_cast<MainWindow *>(data);
    if (window->confirm_quit()) {
        window->hide();
    }
}

void cb_create_mbr(Fl_Widget *, void *data)
{
    static_cast<MainWindow *>(data)->create_mbr();
}

void cb_new_partition(Fl_Widget *, void *data)
{
    static_cast<MainWindow *>(data)->new_partition();
}

void cb_delete_partition(Fl_Widget *, void *data)
{
    static_cast<MainWindow *>(data)->delete_partition();
}

void cb_format_partition(Fl_Widget *, void *data)
{
    static_cast<MainWindow *>(data)->format_partition();
}

void cb_browse(Fl_Widget *, void *data)
{
    static_cast<MainWindow *>(data)->open_browser();
}

void cb_about(Fl_Widget *, void *data)
{
    static_cast<MainWindow *>(data)->show_about();
}

void window_close_cb(Fl_Widget *, void *data)
{
    auto *window = static_cast<MainWindow *>(data);
    if (window->confirm_quit()) {
        window->hide();
    }
}

void model_status_cb(const char *message, void *user)
{
    static_cast<MainWindow *>(user)->set_status(message);
}

}  // namespace

MainWindow::MainWindow(const std::vector<std::string> &images)
    : Fl_Double_Window(1060, 700, "Zeal Disk Tool"), image_paths_(images)
{
    color(gui::background);
    begin();

    menu_ = new Fl_Menu_Bar(0, 0, w(), kMenuHeight);
    menu_->box(FL_FLAT_BOX);
    menu_->color(gui::header);
    menu_->textcolor(gui::text);
    menu_->selection_color(gui::accent);
    menu_->textsize(12);
    menu_->textfont(FL_HELVETICA);
    menu_->add("&File/&Open image...", FL_COMMAND + 'o', cb_open_image, this);
    menu_->add("&File/C&reate image...", FL_COMMAND + 'n', cb_new_image, this, FL_MENU_DIVIDER);
    menu_->add("&File/&Refresh devices", FL_F + 5, cb_refresh_devices, this, FL_MENU_DIVIDER);
    menu_->add("&File/&Apply changes", FL_COMMAND + 's', cb_apply, this);
    menu_->add("&File/Cancel changes", 0, cb_cancel, this, FL_MENU_DIVIDER);
    menu_->add("&File/&Quit", FL_COMMAND + 'q', cb_quit, this);
    menu_->add("&Partition/Create &MBR", 0, cb_create_mbr, this);
    menu_->add("&Partition/&New partition", FL_COMMAND + 'p', cb_new_partition, this);
    menu_->add("&Partition/&Delete partition", FL_Delete, cb_delete_partition, this);
    menu_->add("&Partition/&Format partition", 0, cb_format_partition, this, FL_MENU_DIVIDER);
    menu_->add("&Partition/&Browse files", FL_COMMAND + 'b', cb_browse, this);
    menu_->add("&Help/&About Zeal Disk Tool", 0, cb_about, this);

    toolbar_bg_ = new Fl_Box(0, kMenuHeight, w(), kToolbarHeight);
    toolbar_bg_->box(FL_FLAT_BOX);
    toolbar_bg_->color(gui::panel);

    btn_mbr_ = new Fl_Button(0, 0, 0, 0, "Create MBR");
    btn_new_partition_ = new Fl_Button(0, 0, 0, 0, "New partition");
    btn_delete_ = new Fl_Button(0, 0, 0, 0, "Delete");
    btn_format_ = new Fl_Button(0, 0, 0, 0, "Format");
    for (Fl_Button *button : {btn_mbr_, btn_new_partition_, btn_delete_, btn_format_}) {
        button->labelsize(12);
        button->box(FL_UP_BOX);
    }
    btn_mbr_->callback(cb_create_mbr, this);
    btn_new_partition_->callback(cb_new_partition, this);
    btn_delete_->callback(cb_delete_partition, this);
    btn_format_->callback(cb_format_partition, this);
    btn_mbr_->tooltip("Stage a partition table on this disk");
    btn_new_partition_->tooltip("Stage a new ZealFS v2 partition");
    btn_delete_->tooltip("Stage the removal of the selected partition");
    btn_format_->tooltip("Stage formatting the selected partition as ZealFS v2");

    disk_caption_ = new Fl_Box(0, 0, 0, 0, "Disk:");
    disk_caption_->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
    disk_caption_->labelsize(12);
    disk_caption_->labelcolor(gui::text_muted);

    disk_choice_ = new Fl_Choice(0, 0, 0, 0);
    disk_choice_->labelsize(12);
    disk_choice_->callback(disk_choice_cb, this);

    summary_ = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, "");
    summary_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    summary_->labelsize(11);
    summary_->labelcolor(gui::text_muted);

    staged_ = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, "");
    staged_->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    staged_->labelsize(11);
    staged_->labelfont(FL_HELVETICA_BOLD);

    bar_ = new PartitionBar(0, 0, 0, 0);
    bar_->on_select = [this](int slot) { select_partition(slot); };
    bar_->on_activate = [this](int slot) {
        select_partition(slot);
        open_browser();
    };

    partition_heading_ = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, "PARTITIONS");
    partition_heading_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    partition_heading_->labelsize(10);
    partition_heading_->labelfont(FL_HELVETICA_BOLD);
    partition_heading_->labelcolor(gui::text_muted);

    table_ = new PartitionTable(0, 0, 0, 0, this);

    panel_ = new Fl_Box(0, 0, 0, 0, "");
    panel_->box(FL_BORDER_BOX);
    panel_->color(gui::panel);
    panel_->align(FL_ALIGN_INSIDE);

    panel_heading_ = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, "SELECTED PARTITION");
    panel_heading_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    panel_heading_->labelsize(10);
    panel_heading_->labelfont(FL_HELVETICA_BOLD);
    panel_heading_->labelcolor(gui::text_muted);

    panel_chip_ = new Fl_Box(FL_FLAT_BOX, 0, 0, 0, 0, "");
    details_ = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, "");
    details_->align(FL_ALIGN_TOP_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    details_->labelsize(12);
    details_->labelcolor(gui::text);

    usage_caption_ = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, "USAGE");
    usage_caption_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    usage_caption_->labelsize(10);
    usage_caption_->labelfont(FL_HELVETICA_BOLD);
    usage_caption_->labelcolor(gui::text_muted);

    usage_ = new Fl_Progress(0, 0, 0, 0);
    usage_->minimum(0);
    usage_->maximum(100);
    usage_->value(0);
    usage_->box(FL_DOWN_BOX);
    usage_->color(gui::panel_alt);
    usage_->selection_color(gui::accent);
    usage_->labelsize(10);
    usage_->labelcolor(gui::text);

    panel_disk_heading_ = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, "DISK");
    panel_disk_heading_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    panel_disk_heading_->labelsize(10);
    panel_disk_heading_->labelfont(FL_HELVETICA_BOLD);
    panel_disk_heading_->labelcolor(gui::text_muted);

    panel_disk_ = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, "");
    panel_disk_->align(FL_ALIGN_TOP_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    panel_disk_->labelsize(12);
    panel_disk_->labelcolor(gui::text);

    btn_browse_ = new Fl_Button(0, 0, 0, 0, "Browse files...");
    btn_browse_->labelsize(12);
    btn_browse_->callback(cb_browse, this);
    btn_browse_->tooltip("Open the file browser for this partition");

    btn_panel_format_ = new Fl_Button(0, 0, 0, 0, "Format");
    btn_panel_format_->labelsize(12);
    btn_panel_format_->callback(cb_format_partition, this);
    btn_panel_delete_ = new Fl_Button(0, 0, 0, 0, "Delete");
    btn_panel_delete_->labelsize(12);
    btn_panel_delete_->callback(cb_delete_partition, this);

    status_ = new Fl_Box(FL_FLAT_BOX, 0, 0, 0, 0, "");
    status_->color(gui::panel);
    status_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    status_->labelsize(12);
    status_->labelcolor(gui::text_muted);

    end();

    size_range(900, 560);
    callback(window_close_cb, this);
    layout_children();

    disk_set_status_fn(model_status_cb, this);
    rebuild_disk_choice();
    refresh_views();
    set_status("Ready");
}

MainWindow::~MainWindow()
{
    disk_set_status_fn(nullptr, nullptr);
    delete browser_;
    browser_ = nullptr;
}

void MainWindow::initialize()
{
    disks_refresh();
    int selected = state()->selected_disk;
    std::string open_error;
    for (const std::string &path : image_paths_) {
        const int index = disk_open_image_file_path(state(), path.c_str());
        if (index >= 0) {
            selected = index;
        } else {
            open_error = status_text_;
        }
    }
    state()->selected_disk = selected;
    if (selected < 0 && state()->disk_count > 0) {
        state()->selected_disk = 0;
    }
    state()->selected_partition = -1;
    model_changed();
    if (!open_error.empty()) {
        set_status(open_error.c_str());
    } else if (state()->disk_count == 0) {
        set_status("No disks detected. Use File > Open image... to load a disk image.");
    } else {
        set_status("Select a partition to manage its files; press Ctrl+B to browse it.");
    }
}

disk_info_t *MainWindow::current_disk()
{
    return disk_get_current(state());
}

int MainWindow::selected_slot()
{
    return state()->selected_partition;
}

void MainWindow::set_status(const char *message)
{
    status_text_ = message != nullptr ? message : "";
    if (status_ != nullptr) {
        status_->copy_label(("  " + status_text_).c_str());
        status_->redraw();
    }
}

void MainWindow::select_partition(int slot)
{
    disk_info_t *disk = current_disk();
    if (slot >= 0 && (disk == nullptr || slot >= MAX_PART_COUNT || !disk->partitions[slot].active)) {
        slot = -1;
    }
    if (state()->selected_partition == slot) {
        return;
    }
    state()->selected_partition = slot;
    if (slot >= 0) {
        char message[96];
        std::snprintf(message, sizeof(message), "Partition %d selected", slot);
        set_status(message);
    } else {
        set_status("No partition selected");
    }
    refresh_views();
}

int MainWindow::default_partition_slot()
{
    disk_info_t *disk = current_disk();
    if (disk == nullptr || !disk->valid) {
        return -1;
    }
    for (int index = 0; index < MAX_PART_COUNT; ++index) {
        if (disk_is_valid_zealfs_partition(&disk->partitions[index])) {
            return index;
        }
    }
    for (int index = 0; index < MAX_PART_COUNT; ++index) {
        if (disk->partitions[index].active) {
            return index;
        }
    }
    return -1;
}

void MainWindow::model_changed()
{
    if (state()->selected_disk >= state()->disk_count) {
        state()->selected_disk = state()->disk_count > 0 ? 0 : -1;
    }
    if (state()->selected_disk < 0 && state()->disk_count > 0) {
        state()->selected_disk = 0;
    }
    /* Keep the file browser alive across disk/partition changes by selecting
     * a sensible partition before refreshing every view. */
    disk_info_t *disk = current_disk();
    const int slot = state()->selected_partition;
    if (disk == nullptr || slot < 0 || slot >= MAX_PART_COUNT || !disk->partitions[slot].active) {
        state()->selected_partition = default_partition_slot();
    }
    rebuild_disk_choice();
    refresh_views();
}

void MainWindow::refresh_views()
{
    if (menu_ == nullptr) {
        return;
    }
    updating_ = true;

    disk_info_t *disk = current_disk();
    int slot = state()->selected_partition;
    if (slot >= 0 && (disk == nullptr || slot >= MAX_PART_COUNT || !disk->partitions[slot].active)) {
        slot = -1;
        state()->selected_partition = -1;
    }

    if (disk == nullptr) {
        summary_->copy_label("No disk selected");
        summary_->tooltip(nullptr);
        staged_->copy_label("");
    } else {
        char line[ZFS_PATH_MAX + 128];
        std::snprintf(line,
                      sizeof(line),
                      "%s   -   %s   -   %s",
                      disk->name,
                      gui::format_size(disk->size_bytes).c_str(),
                      disk->has_mbr ? "MBR partition table" : "raw ZealFS filesystem");
        summary_->copy_label(line);
        summary_->copy_tooltip(disk->path);
        staged_->copy_label(disk->has_staged_changes ? "UNSAVED CHANGES" : "All changes written");
        staged_->labelcolor(disk->has_staged_changes ? gui::warning : gui::success);
    }

    bar_->refresh(disk, slot);
    table_->refresh();

    char heading[64];
    int used_slots = 0;
    if (disk != nullptr) {
        for (int index = 0; index < MAX_PART_COUNT; ++index) {
            if (disk->partitions[index].active) {
                ++used_slots;
            }
        }
    }
    if (disk == nullptr) {
        partition_heading_->copy_label("PARTITIONS");
    } else {
        std::snprintf(heading, sizeof(heading), "PARTITIONS  (%d of %d slots used)", used_slots, MAX_PART_COUNT);
        partition_heading_->copy_label(heading);
    }

    update_details();
    update_actions();

    std::string title = "Zeal Disk Tool " VERSION;
    if (disk != nullptr) {
        title += " - ";
        title += disk->name;
    }
    copy_label(title.c_str());

    if (browser_ != nullptr && browser_->visible()) {
        if (disk != nullptr && slot >= 0 && disk_is_valid_zealfs_partition(&disk->partitions[slot])) {
            browser_->retarget(disk, slot);
        } else {
            browser_->hide();
        }
    }

    updating_ = false;
}

void MainWindow::update_details()
{
    disk_info_t *disk = current_disk();
    const int slot = state()->selected_partition;
    char buffer[1024];

    update_disk_panel(disk);

    if (disk == nullptr) {
        panel_chip_->hide();
        usage_->value(0);
        usage_caption_->copy_label("USAGE");
        details_->copy_label("No disk selected\n\nOpen a disk image with\nFile > Open image...");
        return;
    }
    if (slot < 0 || !disk->partitions[slot].active) {
        panel_chip_->hide();
        usage_->value(0);
        usage_caption_->copy_label("USAGE");
        details_->copy_label("No partition selected\n\nClick a partition in the map\nor in the table to select it.");
        return;
    }

    const partition_t &part = disk->partitions[slot];
    const uint64_t size_bytes = (uint64_t) part.size_sectors * DISK_SECTOR_SIZE;
    char size_str[32];
    disk_get_size_str(size_bytes, size_str, sizeof(size_str));

    uint64_t used = 0;
    uint64_t total = 0;
    int percent = 0;
    int usage_available = part.type == ZEALFS_TYPE ? disk_fs_usage(disk, slot, &used, &total) : -1;
    if (usage_available == 0) {
        percent = gui::percent_of(used, total);
    }

    char free_str[32] = "-";
    if (usage_available == 0) {
        disk_get_size_str(total >= used ? total - used : 0, free_str, sizeof(free_str));
    }

    std::snprintf(buffer,
                  sizeof(buffer),
                  "PARTITION %d\n%s\n\n"
                  "Start address   0x%08llX\n"
                  "Size            %s\n"
                  "Usage           %s\n"
                  "Free            %s\n"
                  "State           %s",
                  slot,
                  disk_get_fs_type(part.type),
                  (unsigned long long) part.start_lba * (unsigned long long) DISK_SECTOR_SIZE,
                  size_str,
                  usage_available == 0 ? (std::to_string(percent) + "%").c_str() : "not available",
                  free_str,
                  disk->has_staged_changes ? "unsaved changes" : "written to disk");

    panel_chip_->color(gui::partition_color(slot));
    panel_chip_->show();
    panel_chip_->redraw();
    details_->copy_label(buffer);
    usage_->value(percent);
    usage_->selection_color(percent >= 90 ? gui::warning : gui::accent);
    usage_->redraw();
    if (usage_available == 0) {
        char usage_text[40];
        std::snprintf(usage_text, sizeof(usage_text), "USAGE   %d%% used", percent);
        usage_caption_->copy_label(usage_text);
    } else {
        usage_caption_->copy_label("USAGE   not available");
    }
}

void MainWindow::update_disk_panel(disk_info_t *disk)
{
    if (panel_disk_ == nullptr) {
        return;
    }
    if (disk == nullptr) {
        panel_disk_->copy_label("No disk selected");
        panel_disk_->tooltip(nullptr);
        return;
    }
    int used_slots = 0;
    for (int index = 0; index < MAX_PART_COUNT; ++index) {
        if (disk->partitions[index].active) {
            ++used_slots;
        }
    }
    char slots[64];
    std::snprintf(slots, sizeof(slots), "%d of %d slots used", used_slots, MAX_PART_COUNT);
    const std::string text = std::string(disk->name) + "\n" + gui::format_size(disk->size_bytes) + "\n" + slots;
    panel_disk_->copy_label(text.c_str());
    panel_disk_->copy_tooltip(disk->path);
}

void MainWindow::update_actions()
{
    disk_info_t *disk = current_disk();
    const int slot = state()->selected_partition;
    const bool valid_disk = disk != nullptr && disk->valid;
    const bool pending = disk != nullptr && disk->has_staged_changes;
    const bool active_partition = valid_disk && slot >= 0 && slot < MAX_PART_COUNT && disk->partitions[slot].active;
    const bool zealfs_partition =
        active_partition && disk_is_valid_zealfs_partition(&disk->partitions[slot]);

    set_enabled(btn_mbr_, valid_disk && !disk->has_mbr && !pending && !disk->partitions[0].active);
    set_enabled(btn_new_partition_, valid_disk && disk->free_part_idx >= 0);
    set_enabled(btn_delete_, active_partition);
    set_enabled(btn_format_, active_partition);
    set_enabled(btn_browse_, zealfs_partition);
    set_enabled(btn_panel_format_, active_partition);
    set_enabled(btn_panel_delete_, active_partition);
}

void MainWindow::rebuild_disk_choice()
{
    if (disk_choice_ == nullptr) {
        return;
    }
    disk_choice_->clear();
    choice_labels_.clear();
    const int count = state()->disk_count;
    for (int index = 0; index < count; ++index) {
        choice_labels_.push_back(state()->disks[index].label);
        disk_choice_->add(choice_labels_.back().c_str());
    }
    if (count == 0) {
        disk_choice_->add("No disk loaded", 0, nullptr, nullptr, FL_MENU_INACTIVE);
    }
    disk_choice_->add("Open image file...");
    const int selected = state()->selected_disk;
    disk_choice_->value(selected >= 0 && selected < count ? selected : 0);
}

void MainWindow::disk_choice_cb(Fl_Widget *, void *data)
{
    static_cast<MainWindow *>(data)->disk_choice_changed();
}

void MainWindow::disk_choice_changed()
{
    if (updating_) {
        return;
    }
    const int picked = disk_choice_->value();
    const int count = state()->disk_count;
    if (picked < 0) {
        return;
    }
    if (picked >= count) {
        rebuild_disk_choice();
        open_image();
        return;
    }
    if (picked == state()->selected_disk) {
        return;
    }
    disk_info_t *previous = current_disk();
    if (!disk_can_be_switched(previous)) {
        fl_alert("The selected disk has unsaved changes.\nApply or cancel them before switching disks.");
        rebuild_disk_choice();
        return;
    }
    state()->selected_disk = picked;
    state()->selected_partition = -1;
    model_changed();
}

void MainWindow::open_image()
{
    Fl_Native_File_Chooser chooser;
    chooser.title("Open disk image");
    chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
    chooser.filter("Disk images\t*.img\nAll files\t*");
    if (chooser.show() != 0) {
        return;
    }
    disk_info_t *previous = current_disk();
    const int index = disk_open_image_file_path(state(), chooser.filename());
    if (index < 0) {
        return;
    }
    const bool can_select = disk_can_be_switched(previous);
    if (can_select) {
        state()->selected_disk = index;
        state()->selected_partition = -1;
    }
    model_changed();
    if (!can_select) {
        set_status("Image opened. Apply or cancel the current disk's changes before switching.");
    }
}

void MainWindow::new_image()
{
    disk_info_t *previous = current_disk();
    const int index = dialog_new_image(this);
    if (index < 0) {
        return;
    }
    const bool can_select = disk_can_be_switched(previous);
    if (can_select) {
        state()->selected_disk = index;
        state()->selected_partition = -1;
    }
    model_changed();
    set_status("New image staged. Create partitions, then press Apply to write it.");
}

void MainWindow::refresh_devices()
{
    const std::string current_path = current_disk() != nullptr ? std::string(current_disk()->path) : std::string();
    if (disks_refresh() != ERR_SUCCESS) {
        refresh_views();
        return;
    }
    int selected = state()->disk_count > 0 ? 0 : -1;
    if (!current_path.empty()) {
        for (int index = 0; index < state()->disk_count; ++index) {
            if (current_path == state()->disks[index].path) {
                selected = index;
            }
        }
    }
    state()->selected_disk = selected;
    state()->selected_partition = -1;
    model_changed();
}

void MainWindow::create_mbr()
{
    disk_info_t *disk = current_disk();
    if (disk == nullptr || !disk->valid) {
        return;
    }
    if (disk->has_mbr) {
        fl_message("This disk already has an MBR partition table.");
        return;
    }
    if (disk->has_staged_changes) {
        fl_message("Apply or cancel the pending changes before creating an MBR.");
        return;
    }
    if (disk->partitions[0].active) {
        fl_message("Delete the raw partition before creating an MBR.");
        return;
    }
    if (fl_choice("Stage a new MBR partition table on \"%s\"?\n"
                  "Nothing is written until you press Apply.",
                  "Cancel",
                  "Create MBR",
                  nullptr,
                  disk->name) != 1) {
        return;
    }
    disk_create_mbr(disk);
    model_changed();
}

void MainWindow::new_partition()
{
    int slot = -1;
    if (!dialog_new_partition(this, &slot)) {
        return;
    }
    if (slot >= 0) {
        state()->selected_partition = slot;
    }
    model_changed();
}

void MainWindow::delete_partition()
{
    disk_info_t *disk = current_disk();
    const int slot = state()->selected_partition;
    if (disk == nullptr || slot < 0 || !disk->partitions[slot].active) {
        return;
    }
    if (fl_choice("Delete partition %d (%s, %s)?\n"
                  "The removal is staged until you press Apply.",
                  "Cancel",
                  "Delete partition",
                  nullptr,
                  slot,
                  disk_get_fs_type(disk->partitions[slot].type),
                  gui::format_size((uint64_t) disk->partitions[slot].size_sectors * DISK_SECTOR_SIZE).c_str()) !=
        1) {
        return;
    }
    disk_delete_partition(disk, slot);
    state()->selected_partition = -1;
    model_changed();
}

void MainWindow::format_partition()
{
    disk_info_t *disk = current_disk();
    const int slot = state()->selected_partition;
    if (disk == nullptr || slot < 0 || !disk->partitions[slot].active) {
        return;
    }
    if (fl_choice("Format partition %d as ZealFS v2?\n"
                  "Its current contents are replaced when the changes are applied.",
                  "Cancel",
                  "Format partition",
                  nullptr,
                  slot) != 1) {
        return;
    }
    const char *error_message = disk_format_partition(disk, slot);
    if (error_message != nullptr) {
        fl_alert("%s", error_message);
    }
    model_changed();
}

void MainWindow::apply_changes()
{
    disk_info_t *disk = current_disk();
    if (disk == nullptr || !disk->has_staged_changes) {
        set_status("There are no pending changes to apply");
        return;
    }
    if (fl_choice("Apply all pending changes to \"%s\"?\n"
                  "This is permanent and cannot be undone.",
                  "Cancel",
                  "Apply changes",
                  nullptr,
                  disk->name) != 1) {
        return;
    }
    const char *error_message = disk_write_changes(disk);
    if (error_message != nullptr) {
        fl_alert("%s", error_message);
    }
    model_changed();
}

void MainWindow::cancel_changes()
{
    disk_info_t *disk = current_disk();
    if (disk == nullptr || !disk->has_staged_changes) {
        set_status("There are no pending changes to cancel");
        return;
    }
    if (fl_choice("Discard all pending changes on \"%s\"?\n"
                  "Files, partitions and formatting staged in this session will be lost.",
                  "Keep changes",
                  "Discard changes",
                  nullptr,
                  disk->name) != 1) {
        return;
    }
    disk_revert_changes(disk);
    state()->selected_partition = -1;
    model_changed();
}

void MainWindow::open_browser()
{
    disk_info_t *disk = current_disk();
    const int slot = state()->selected_partition;
    if (disk == nullptr || slot < 0 || !disk_is_valid_zealfs_partition(&disk->partitions[slot])) {
        set_status("Select a ZealFS v2 partition to browse its files");
        return;
    }
    if (browser_ == nullptr) {
        browser_ = new BrowserWindow(this);
    }
    browser_->retarget(disk, slot);
    browser_->show();
    browser_->take_focus();
    browser_->focus_files();
}

void MainWindow::show_about()
{
    fl_message("Zeal Disk Tool %s\n\n"
               "Create and manage ZealFS v2 partitions, disk images and physical disks.\n"
               "GUI powered by FLTK.",
               VERSION);
}

bool MainWindow::confirm_quit()
{
    for (;;) {
        disk_info_t *pending = nullptr;
        for (int index = 0; index < state()->disk_count; ++index) {
            if (state()->disks[index].has_staged_changes) {
                pending = &state()->disks[index];
                break;
            }
        }
        if (pending == nullptr) {
            if (browser_ != nullptr) {
                browser_->hide();
            }
            return true;
        }
        const int answer = fl_choice("\"%s\" has unsaved changes.\n\n"
                                     "Apply them, discard them, or keep editing?",
                                     "Keep editing",
                                     "Apply",
                                     "Discard",
                                     pending->name);
        if (answer == 0) {
            return false;
        }
        if (answer == 1) {
            const char *error_message = disk_write_changes(pending);
            if (error_message != nullptr) {
                fl_alert("%s", error_message);
                return false;
            }
        } else {
            disk_revert_changes(pending);
        }
        if (state()->selected_disk >= state()->disk_count) {
            state()->selected_disk = state()->disk_count > 0 ? 0 : -1;
            state()->selected_partition = -1;
        }
    }
}

void MainWindow::layout_children()
{
    if (menu_ == nullptr) {
        return;
    }
    const int margin = 14;
    const int status_y = h() - kStatusHeight;

    const int left_w = std::max(360, w() - margin * 2 - kPanelWidth - 14);
    const int right_x = margin + left_w + 14;
    const int right_w = w() - margin - right_x;

    toolbar_bg_->resize(0, kMenuHeight, w(), kToolbarHeight);
    int button_x = margin;
    const int button_y = kMenuHeight + 8;
    btn_mbr_->resize(button_x, button_y, 104, 30);
    button_x += 112;
    btn_new_partition_->resize(button_x, button_y, 122, 30);
    button_x += 130;
    btn_delete_->resize(button_x, button_y, 84, 30);
    button_x += 92;
    btn_format_->resize(button_x, button_y, 84, 30);

    const int choice_w = w() < 1000 ? 220 : 290;
    const int choice_x = w() - margin - choice_w;
    disk_caption_->resize(choice_x - 46, button_y + 5, 42, 20);
    disk_choice_->resize(choice_x, button_y + 5, choice_w, 24);

    summary_->resize(margin, kMenuHeight + kToolbarHeight + 4, w() - margin * 2 - 190, 20);
    staged_->resize(w() - margin - 180, kMenuHeight + kToolbarHeight + 4, 180, 20);

    const int bar_top = kMenuHeight + kToolbarHeight + 30;
    bar_->resize(margin, bar_top, left_w, kBarHeight);
    partition_heading_->resize(margin, bar_top + kBarHeight + 6, left_w, 18);
    const int table_top = bar_top + kBarHeight + 28;
    table_->resize(margin, table_top, left_w, std::max(120, status_y - 8 - table_top));

    const int panel_top = bar_top;
    const int panel_h = std::max(240, status_y - 8 - panel_top);
    panel_->resize(right_x, panel_top, right_w, panel_h);
    panel_heading_->resize(right_x + 16, panel_top + 14, right_w - 32, 18);
    panel_chip_->resize(right_x + 16, panel_top + 44, 16, 16);
    details_->resize(right_x + 40, panel_top + 38, right_w - 56, 150);
    usage_caption_->resize(right_x + 16, panel_top + 200, right_w - 32, 16);
    usage_->resize(right_x + 16, panel_top + 218, right_w - 32, 18);
    panel_disk_heading_->resize(right_x + 16, panel_top + 252, right_w - 32, 16);
    panel_disk_->resize(right_x + 16, panel_top + 270, right_w - 32, 64);
    btn_browse_->resize(right_x + 16, panel_top + 342, right_w - 32, 32);
    const int half = (right_w - 40) / 2;
    btn_panel_format_->resize(right_x + 16, panel_top + 382, half, 30);
    btn_panel_delete_->resize(right_x + 24 + half, panel_top + 382, half, 30);

    status_->resize(0, status_y, w(), kStatusHeight);
    redraw();
}

void MainWindow::resize(int x, int y, int w, int h)
{
    Fl_Double_Window::resize(x, y, w, h);
    layout_children();
}
