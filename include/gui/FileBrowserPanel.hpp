/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>

#include <set>
#include <string>
#include <vector>

#include "disk.h"

class MainWindow;

/* Embedded file browser: breadcrumb navigation, folder tree, sortable file
 * table, search field and a details/preview card. */
class FileBrowserPanel : public Fl_Group {
public:
    FileBrowserPanel(int x, int y, int w, int h, MainWindow *owner);
    ~FileBrowserPanel() override;

    void set_right_width(int width);
    void resize(int x, int y, int w, int h) override;
    /* Point the browser at a partition. Reloads when the model revision or the
     * directory changed; resets the path when switching partitions. */
    void retarget(disk_info_t *disk, int slot);
    void focus_list();
    void focus_search();
    void clear_search();

    /* Menu entry points, shared with the browser's own action strip. */
    bool has_target() const { return target_valid_; }
    bool has_selection() const { return !selection_.empty(); }
    /* Drag-and-drop import: the drop target is a child widget because FLTK only
     * delivers DND events to non-group widgets. */
    bool can_import() const;
    void import_host_paths(const std::vector<std::string> &host_paths);
    void queue_drop_import(const std::vector<std::string> &host_paths);
    void set_drop_active(bool active);
    /* Hide the hover tooltip; a drop starts a drag the pointer never leaves. */
    void clear_tooltip();
    void action_import();
    void action_export();
    void action_new_folder();
    void action_delete();

    void draw() override;
    int handle(int event) override;

private:
    enum class HitKind {
        NoTarget,
        Back,
        Forward,
        Up,
        Home,
        Crumb,
        Import,
        Export,
        NewFolder,
        Delete,
        TableHeader,
        TableRow,
        TreeRow,
        PreviewAction,
        TableScrollbar,
    };
    struct Hit {
        HitKind kind = HitKind::NoTarget;
        int index = -1;
    };
    struct Crumb {
        std::string name;
        std::string path;
        int x = 0;
        int w = 0;
    };
    struct TreeRow {
        std::string path;
        std::string name;
        int depth = 0;
        bool current = false;
        bool has_children = false;
    };

    bool valid_target() const;
    void layout_geometry();
    void layout_search();

    bool reload();
    void rebuild_order();
    void rebuild_tree();
    void add_tree_children(const std::string &path, int depth, int level);
    void update_preview();
    void select_row(int row, bool ensure_visible);
    void select_row_with(int row, bool toggle, bool range);
    void select_all();
    bool is_selected(int entry_index) const;
    int row_of_entry(int entry_index) const;
    void export_selection();
    void delete_selection();
    int selected_entry() const;
    std::string entry_path(int entry_index) const;

    void navigate_to(const std::string &path, bool record_history);
    void go_back();
    void go_forward();
    void go_up();
    void open_row(int row);

    void import_files();
    void export_entry(int entry_index);
    void new_folder();
    void delete_entry(int entry_index);

    void ensure_row_visible(int row);
    void scroll_table(int amount);
    void scroll_tree(int amount);

    Hit hit_test(int mx, int my) const;
    void update_tooltip(const Hit &hit);
    void draw_navigation();
    void draw_actions();
    void draw_tree();
    void draw_table();
    void draw_preview();
    void draw_scrollbar(int sx, int sy, int sw, int sh, int total, int visible, int offset);

    static void search_cb(Fl_Widget *widget, void *data);
    static void drop_import_cb(void *data);
    void run_pending_drop_import();

    MainWindow *owner_ = nullptr;
    Fl_Input *search_ = nullptr;

    disk_info_t *disk_ = nullptr;
    int slot_ = -1;
    unsigned revision_ = 0;
    bool target_valid_ = false;

    std::string path_ = "/";
    zfs_entry *entries_ = nullptr;
    size_t count_ = 0;
    std::vector<int> order_;
    int sort_column_ = 0;
    bool descending_ = false;
    std::string filter_;
    std::string selected_name_;
    int selected_row_ = -1;
    /* Selection is stored as entry indices, which survive re-sorting and
     * filtering; `selected_row_` stays the lead row for preview and open. */
    std::vector<int> selection_;
    int anchor_row_ = -1;
    bool drop_active_ = false;
    Fl_Widget *drop_target_ = nullptr;
    size_t multi_count_ = 0;
    std::vector<std::string> pending_drop_paths_;
    std::vector<std::string> history_;
    size_t history_index_ = 0;

    std::vector<TreeRow> tree_rows_;
    std::set<std::string> collapsed_;
    std::vector<std::string> preview_lines_;
    std::string preview_message_;
    int preview_kind_ = 0; /* 0 none, 1 file, 2 folder, 3 binary */

    int nav_h_ = 38;
    int actions_h_ = 34;
    int body_y_ = 76;
    int right_width_ = 280;
    int preview_w_ = 280;
    int preview_x_ = 0;
    int tree_w_ = 210;
    int table_x_ = 0;
    int table_w_ = 300;

    int table_scroll_ = 0;
    int tree_scroll_ = 0;
    int hover_kind_ = -1;
    int hover_index_ = -1;
    bool table_scroll_drag_ = false;
    int tip_kind_ = -1;
    int tip_index_ = -1;
    std::string tip_text_;
    int scroll_drag_offset_ = 0;

    std::vector<Crumb> crumbs_;
    int nav_back_x_ = 0;
    int nav_forward_x_ = 0;
    int nav_up_x_ = 0;
    int nav_home_x_ = 0;
    int import_x_ = 0;
    int export_x_ = 0;
    int new_folder_x_ = 0;
    int delete_x_ = 0;
    int import_w_ = 0;
    int export_w_ = 0;
    int new_folder_w_ = 0;
    int delete_w_ = 0;
    int preview_action_x_ = 0;
    int preview_action_y_ = 0;
    int preview_action_w_ = 0;
    int preview_action_h_ = 0;
};
