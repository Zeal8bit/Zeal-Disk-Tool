/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>

#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

class MainWindow;

/* Left-hand browser over the host filesystem, used to copy files to and from
 * the disk image in the embedded disk browser. */
class HostBrowserPanel : public Fl_Group {
public:
    HostBrowserPanel(int x, int y, int w, int h, MainWindow *owner);
    ~HostBrowserPanel() override;

    void resize(int x, int y, int w, int h) override;
    void draw() override;
    int handle(int event) override;

    void focus_search();
    void clear_search();

    const std::string &current_path() const { return path_string_; }
    /* Reload after files were copied into the current host folder. */
    void refresh();
    std::vector<std::string> selected_paths() const;
    /* Highlight the pane while a drag from the disk browser is over it. */
    void set_transfer_hint(bool active);

private:
    struct Entry {
        std::string name;
        bool directory = false;
        uint64_t size = 0;
        std::time_t modified = 0;
    };
    struct Crumb {
        std::string label;
        std::filesystem::path path;
        int x = 0;
        int w = 0;
    };
    struct Place {
        std::string label;
        std::filesystem::path path;
    };
    enum class HitKind {
        None,
        Back,
        Up,
        Home,
        Crumb,
        Place,
        Header,
        Row,
        Scrollbar,
        NewFolder,
        Rename,
        Delete,
        Copy,
    };
    struct Hit {
        HitKind kind = HitKind::None;
        int index = -1;
    };

    void layout_geometry();
    void layout_search();
    void rebuild_places();
    void reload();
    void rebuild_crumbs();
    void rebuild_order();
    void navigate_to(const std::filesystem::path &path, bool record_history);
    void go_back();
    void go_up();
    void open_row(int row);

    int selected_index() const;
    bool is_selected(int entry_index) const;
    void select_row(int row, bool ensure_visible);
    void select_row_with(int row, bool toggle, bool range);
    void select_all();
    void ensure_row_visible(int row);
    void scroll_table(int amount);

    void new_folder();
    void rename_entry();
    void delete_entry();
    void delete_selection();
    void copy_selection();

    Hit hit_test(int mx, int my) const;
    void draw_navigation();
    void draw_places();
    void draw_actions();
    void draw_table();
    void draw_scrollbar(int sx, int sy, int sh, int total, int visible, int offset);
    std::string timestamp(const Entry &entry) const;

    static void search_cb(Fl_Widget *widget, void *data);

    MainWindow *owner_ = nullptr;
    Fl_Input *search_ = nullptr;

    std::filesystem::path path_;
    std::string path_string_;
    std::vector<Entry> entries_;
    std::vector<int> order_;
    int sort_column_ = 0;
    bool descending_ = false;
    std::string filter_;

    std::vector<Crumb> crumbs_;
    std::vector<Place> places_;
    std::vector<std::filesystem::path> history_;
    size_t history_index_ = 0;

    std::vector<int> selection_;
    int selected_row_ = -1;
    int anchor_row_ = -1;

    int nav_h_ = 38;
    int actions_h_ = 34;
    int body_y_ = 76;
    int places_w_ = 150;
    int table_x_ = 0;
    int table_w_ = 300;

    int table_scroll_ = 0;
    int hover_kind_ = -1;
    int hover_index_ = -1;
    bool scroll_drag_ = false;
    int scroll_drag_offset_ = 0;

    int nav_back_x_ = 0;
    int nav_up_x_ = 0;
    int nav_home_x_ = 0;
    int new_folder_x_ = 0;
    int rename_x_ = 0;
    int delete_x_ = 0;
    int copy_x_ = 0;
    int new_folder_w_ = 0;
    int rename_w_ = 0;
    int delete_w_ = 0;
    int copy_w_ = 0;

    bool transfer_hint_ = false;
    bool drag_candidate_ = false;
    bool dragging_ = false;
    int drag_x_ = 0;
    int drag_y_ = 0;
};
