/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <FL/Fl_Double_Window.H>

#include <string>
#include <vector>

#include "disk.h"
#include "gui/Theme.hpp"

namespace modern {
class ModernButton;
}

class Fl_Menu_Bar;
class HostBrowserPanel;
class SidebarPanel;
class DiskMapPanel;
class DetailsPanel;
class FileBrowserPanel;

/* Single-window frontend with a native title bar and a classic menu bar, a
 * disk sidebar, partition map, details/action rail and embedded file browser. */
class MainWindow : public Fl_Double_Window {
public:
    explicit MainWindow(const std::vector<std::string> &images);
    ~MainWindow() override;

    void initialize();

    disk_list_state_t *state() { return disk_get_state(); }
    disk_info_t *current_disk();
    int selected_slot() const;
    void set_status(const char *message);
    const std::string &status_text() const { return status_text_; }

    void model_changed();
    void refresh_menu_states() { update_menu_states(); }

    FileBrowserPanel *disk_browser() const { return files_; }
    HostBrowserPanel *host_browser() const { return host_; }
    /* Copy between the two browser panes. */
    void copy_host_to_disk(const std::vector<std::string> &host_paths);
    void copy_disk_to_host();

    void resize(int x, int y, int w, int h) override;
    void draw() override;
    int handle(int event) override;

    bool confirm_quit();

private:
    enum class View {
        Files,
        Manage,
    };

    void set_view(View view);

    enum class MenuAction {
        OpenImage,
        CreateImage,
        ApplyChanges,
        DiscardChanges,
        Quit,
        RefreshDevices,
        NewPartition,
        CreateMbr,
        FormatPartition,
        DeletePartition,
        Properties,
        ImportFiles,
        ExportSelection,
        NewFolder,
        DeleteEntry,
        FocusBrowser,
        FocusSearch,
        ViewFiles,
        ViewManage,
        AppearanceSystem,
        AppearanceLight,
        AppearanceDark,
        Shortcuts,
        About,
    };

    void select_disk(int disk_index, int slot);
    void select_partition(int slot);
    int default_partition_slot() const;

    void refresh_devices();
    void open_image();
    void new_image();
    void new_partition();
    void delete_partition();
    void format_partition();
    void create_mbr();
    void apply_changes();
    void cancel_changes();
    void show_about();
    void show_shortcuts();
    void show_properties();
    void show_create_menu(int screen_x, int screen_y);

    void build_menu();
    void perform_menu_action(MenuAction action);
    void sync_appearance_menu();
    void set_appearance(modern::Theme preference);
    void apply_theme();
    void update_menu_states();
    void set_menu_enabled(const char *path, bool enabled);

    void refresh_views();
    void update_actions();
    void update_window_title();
    void layout_children();

    void draw_heading();
    void draw_view_tabs();
    void draw_statusbar();
    void layout_chrome();

    static void model_status_cb(const char *message, void *user);
    static void window_close_cb(Fl_Widget *widget, void *data);
    static void menu_cb(Fl_Widget *widget, void *data);
    static void apply_cb(Fl_Widget *widget, void *data);

    Fl_Menu_Bar *menu_ = nullptr;
    modern::ModernButton *apply_button_ = nullptr;
    SidebarPanel *sidebar_ = nullptr;
    DiskMapPanel *map_ = nullptr;
    DetailsPanel *details_ = nullptr;
    HostBrowserPanel *host_ = nullptr;
    FileBrowserPanel *files_ = nullptr;

    std::vector<std::string> image_paths_;
    std::string status_text_;
    std::string open_error_;
    bool updating_ = false;

    int status_h_ = 24;
    int gap_ = 14;
    int sidebar_w_ = 260;
    int right_w_ = 288;
    int content_y_ = 0;
    View view_ = View::Manage;
    int tab_y_ = 0;
    int tab_h_ = 0;
    int tab_files_x_ = 0;
    int tab_files_w_ = 0;
    int tab_manage_x_ = 0;
    int tab_manage_w_ = 0;
};
