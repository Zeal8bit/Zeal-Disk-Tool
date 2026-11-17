/**
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Progress.H>

#include <string>
#include <vector>

#include "disk.h"

class EntryTable;
class MainWindow;

/* Separate file browser window for one ZealFS partition. It follows the main
 * window's selection so both always describe the same partition. */
class BrowserWindow : public Fl_Double_Window {
public:
    explicit BrowserWindow(MainWindow *owner);
    ~BrowserWindow();

    /* Point the browser at `disk` partition `partition`. Reloads when the model
     * revision changed; the current directory is kept when possible. */
    void retarget(disk_info_t *disk, int partition);
    void reload_or_root();
    bool reload();

    const std::string &path() const { return path_; }
    size_t entry_count() const { return count_; }
    const zfs_entry *entry_at(int index) const;
    int row_count() const { return (int) order_.size(); }
    int row_entry(int row) const;
    void sort_by(int column);
    bool descending() const { return descending_; }
    int sort_column() const { return sort_column_; }

    void select_row(int row);
    int selected_row() const { return selected_row_; }
    void row_selected(int row);
    void activate_row(int row);

    MainWindow *owner() const { return owner_; }
    void set_status(const char *message);
    /* Give the file list the keyboard focus (used after the window is shown). */
    void focus_files();

    void resize(int x, int y, int w, int h);

private:
    bool valid_target() const;
    void layout_children();
    void update_title();
    void update_buttons();
    void refresh_usage();
    void rebuild_order();
    void clear_entries();

    void go_up();
    void go_path(const std::string &text);
    void new_folder();
    void import_files();
    void export_entry(int entry_index);
    void delete_entry();

    static void up_cb(Fl_Widget *widget, void *data);
    static void go_cb(Fl_Widget *widget, void *data);
    static void refresh_cb(Fl_Widget *widget, void *data);
    static void import_cb(Fl_Widget *widget, void *data);
    static void export_cb(Fl_Widget *widget, void *data);
    static void mkdir_cb(Fl_Widget *widget, void *data);
    static void delete_cb(Fl_Widget *widget, void *data);
    static void window_cb(Fl_Widget *widget, void *data);

    MainWindow *owner_;
    disk_info_t *disk_ = nullptr;
    int partition_ = -1;
    unsigned revision_ = 0;
    std::string path_ = "/";
    zfs_entry *entries_ = nullptr;
    size_t count_ = 0;
    std::vector<int> order_;
    int sort_column_ = 0;
    bool descending_ = false;
    int selected_row_ = -1;
    bool updating_ = false;

    Fl_Button *up_button_ = nullptr;
    Fl_Input *path_input_ = nullptr;
    Fl_Button *go_button_ = nullptr;
    Fl_Button *refresh_button_ = nullptr;
    Fl_Button *import_button_ = nullptr;
    Fl_Button *export_button_ = nullptr;
    Fl_Button *mkdir_button_ = nullptr;
    Fl_Button *delete_button_ = nullptr;
    Fl_Box *count_label_ = nullptr;
    EntryTable *table_ = nullptr;
    Fl_Box *usage_label_ = nullptr;
    Fl_Progress *usage_bar_ = nullptr;
    Fl_Box *status_ = nullptr;
    std::string status_text_;
};
