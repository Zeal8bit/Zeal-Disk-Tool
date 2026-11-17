/**
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Progress.H>

#include <string>
#include <vector>

#include "disk.h"

class BrowserWindow;
class PartitionBar;
class PartitionTable;

/* Main window: disk selection, partition map and partition table. Host images
 * and files are handled by the FLTK frontend; every operation is delegated to
 * the C model adapter in model.c. */
class MainWindow : public Fl_Double_Window {
public:
    explicit MainWindow(const std::vector<std::string> &images);
    ~MainWindow();

    /* Refresh devices and load the images named on the command line. */
    void initialize();

    disk_list_state_t *state() { return disk_get_state(); }
    disk_info_t *current_disk();
    int selected_slot();
    bool updating() const { return updating_; }

    void select_partition(int slot);
    void set_status(const char *message);
    const std::string &status_text() const { return status_text_; }

    /* Called after the model was mutated: refreshes every view and the browser. */
    void model_changed();
    void refresh_views();

    /* User actions, shared by the menu, the toolbar and the details panel. */
    void open_browser();
    void open_image();
    void new_image();
    void refresh_devices();
    void create_mbr();
    void new_partition();
    void delete_partition();
    void format_partition();
    void apply_changes();
    void cancel_changes();
    void show_about();

    /* Ask about pending changes; returns false when the user keeps editing. */
    bool confirm_quit();

    void resize(int x, int y, int w, int h);

private:
    void rebuild_disk_choice();
    int default_partition_slot();
    void disk_choice_changed();
    void update_actions();
    void update_details();
    void update_disk_panel(disk_info_t *disk);
    void update_selection_views();
    void layout_children();
    static void disk_choice_cb(Fl_Widget *widget, void *data);

    Fl_Menu_Bar *menu_ = nullptr;
    Fl_Box *toolbar_bg_ = nullptr;
    Fl_Button *btn_mbr_ = nullptr;
    Fl_Button *btn_new_partition_ = nullptr;
    Fl_Button *btn_delete_ = nullptr;
    Fl_Button *btn_format_ = nullptr;
    Fl_Box *disk_caption_ = nullptr;
    Fl_Choice *disk_choice_ = nullptr;
    Fl_Box *summary_ = nullptr;
    Fl_Box *staged_ = nullptr;
    PartitionBar *bar_ = nullptr;
    Fl_Box *partition_heading_ = nullptr;
    PartitionTable *table_ = nullptr;
    Fl_Box *panel_ = nullptr;
    Fl_Box *panel_heading_ = nullptr;
    Fl_Box *panel_chip_ = nullptr;
    Fl_Box *details_ = nullptr;
    Fl_Box *usage_caption_ = nullptr;
    Fl_Box *panel_disk_heading_ = nullptr;
    Fl_Box *panel_disk_ = nullptr;
    Fl_Progress *usage_ = nullptr;
    Fl_Button *btn_browse_ = nullptr;
    Fl_Button *btn_panel_format_ = nullptr;
    Fl_Button *btn_panel_delete_ = nullptr;
    Fl_Box *status_ = nullptr;

    std::vector<std::string> image_paths_;
    std::vector<std::string> choice_labels_;
    std::string status_text_;
    BrowserWindow *browser_ = nullptr;
    bool updating_ = false;
};
