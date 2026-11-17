/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gui/MainWindow.hpp"

#include <FL/Fl.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "app_version.h"
#include "gui/DetailsPanel.hpp"
#include "gui/Dialogs.hpp"
#include "gui/DiskMapPanel.hpp"
#include "gui/FileBrowserPanel.hpp"
#include "gui/HostBrowserPanel.hpp"
#include "gui/ModernStyle.hpp"
#include "gui/SidebarPanel.hpp"
#include "gui/Utils.hpp"

/* Rounded highlight and a hairline border for the menu popups: the stock
 * FLTK boxtypes are square and beveled. */
static void draw_round_selection(int x, int y, int w, int h, Fl_Color color)
{
    fl_color(color);
    fl_rounded_rectf(x, y, w, h, 6);
}

static void draw_menu_panel(int x, int y, int w, int h, Fl_Color)
{
    fl_color(modern::color(modern::Palette::kPanel));
    fl_rectf(x, y, w, h);
    fl_color(modern::color(modern::Palette::kDivider));
    fl_rect(x, y, w, h);
}

static Fl_Boxtype menu_selection_box()
{
    static Fl_Boxtype type = FL_NO_BOX;
    if (type == FL_NO_BOX) {
        type = (Fl_Boxtype) (FL_FREE_BOXTYPE + 1);
        Fl::set_boxtype(type, draw_round_selection, 0, 0, 0, 0);
    }
    return type;
}

static Fl_Boxtype menu_panel_box()
{
    static Fl_Boxtype type = FL_NO_BOX;
    if (type == FL_NO_BOX) {
        type = (Fl_Boxtype) (FL_FREE_BOXTYPE + 2);
        Fl::set_boxtype(type, draw_menu_panel, 1, 1, 2, 2);
    }
    return type;
}

/* FLTK draws menu popups with the active scheme's colours (a grey panel under
 * gtk+). Temporarily disabling the scheme while a menu is open gives the flat
 * white panel set by `color()`, while the rest of the UI keeps its scheme. */
class ModernMenuBar : public Fl_Menu_Bar {
public:
    ModernMenuBar(int x, int y, int w, int h) : Fl_Menu_Bar(x, y, w, h) {}

    int handle(int event) override
    {
        if (event != FL_PUSH && event != FL_SHORTCUT) {
            return Fl_Menu_Bar::handle(event);
        }
        const char *scheme = Fl::scheme();
        Fl::scheme(nullptr);
        const int result = Fl_Menu_Bar::handle(event);
        Fl::scheme(scheme);
        return result;
    }
};

MainWindow::MainWindow(const std::vector<std::string> &images)
    : Fl_Double_Window(1440, 900, "Zeal Disk Tool"), image_paths_(images)
{
    box(FL_NO_BOX);
    color(modern::color(modern::Palette::kWindow));

    begin();
    menu_ = new ModernMenuBar(0, 0, w(), 26);
    menu_->box(FL_FLAT_BOX);
    menu_->color(modern::color(modern::Palette::kTitleBg));
    menu_->textcolor(modern::color(modern::Palette::kDark2));
    menu_->textsize(12);
    menu_->menu_box(menu_panel_box());
    menu_->down_box(menu_selection_box());
    menu_->selection_color(modern::color(modern::Palette::kSelection));
    Fl::menu_linespacing(8);
    build_menu();
    sidebar_ = new SidebarPanel(0, 0, 10, 10);
    map_ = new DiskMapPanel(0, 0, 10, 10);
    details_ = new DetailsPanel(0, 0, 10, 10);
    host_ = new HostBrowserPanel(0, 0, 10, 10, this);
    files_ = new FileBrowserPanel(0, 0, 10, 10, this);
    files_->set_show_preview(false);
    apply_button_ = new modern::ModernButton(0, 0, 110, 18, "Apply changes", true);
    apply_button_->callback(apply_cb, this);
    apply_button_->tooltip("Write all pending changes (Ctrl+S)");
    apply_button_->deactivate();
    end();

    size_range(1024, 640);
    callback(window_close_cb, this);

    sidebar_->on_select = [this](int disk, int slot) { select_disk(disk, slot); };
    sidebar_->on_add = [this](int screen_x, int screen_y) { show_create_menu(screen_x, screen_y); };
    map_->on_select = [this](int slot) { select_partition(slot); };
    details_->on_select = [this](int slot) { select_partition(slot); };
    details_->on_action = [this](DetailsPanel::Action action) {
        switch (action) {
        case DetailsPanel::Action::Format:
            format_partition();
            break;
        case DetailsPanel::Action::Delete:
            delete_partition();
            break;
        case DetailsPanel::Action::Properties:
            show_properties();
            break;
        }
    };

    disk_set_status_fn(model_status_cb, this);
    layout_children();
    refresh_views();
    set_status("Ready");
}

void MainWindow::build_menu()
{
    /* FLTK menu items only carry a void*; the small MenuAction enum travels
     * through it with an intptr_t round-trip. */
    const auto data = [](MenuAction action) {
        return reinterpret_cast<void *>(static_cast<intptr_t>(action));
    };

    menu_->add("&File/&Open Image...", FL_COMMAND + 'o', menu_cb, data(MenuAction::OpenImage));
    menu_->add("&File/Create &Image...", FL_COMMAND + 'n', menu_cb, data(MenuAction::CreateImage),
               FL_MENU_DIVIDER);
    menu_->add("&File/&Apply Changes", FL_COMMAND + 's', menu_cb, data(MenuAction::ApplyChanges));
    menu_->add("&File/&Discard Changes", 0, menu_cb, data(MenuAction::DiscardChanges), FL_MENU_DIVIDER);
    menu_->add("&File/&Quit", FL_COMMAND + 'q', menu_cb, data(MenuAction::Quit));

    menu_->add("&Disk/&Refresh Devices", FL_F + 5, menu_cb, data(MenuAction::RefreshDevices), FL_MENU_DIVIDER);
    menu_->add("&Disk/&New Partition...", FL_COMMAND + 'p', menu_cb, data(MenuAction::NewPartition));
    menu_->add("&Disk/Create &MBR...", 0, menu_cb, data(MenuAction::CreateMbr), FL_MENU_DIVIDER);
    menu_->add("&Disk/&Format Partition...", 0, menu_cb, data(MenuAction::FormatPartition));
    menu_->add("&Disk/&Delete Partition...", FL_Delete, menu_cb, data(MenuAction::DeletePartition),
               FL_MENU_DIVIDER);
    menu_->add("&Disk/&Properties...", 0, menu_cb, data(MenuAction::Properties));

    menu_->add("&Edit/&Import Files...", FL_COMMAND + 'i', menu_cb, data(MenuAction::ImportFiles));
    menu_->add("&Edit/&Export Selection...", FL_COMMAND + 'e', menu_cb, data(MenuAction::ExportSelection));
    menu_->add("&Edit/&New Folder...", 0, menu_cb, data(MenuAction::NewFolder));
    menu_->add("&Edit/&Delete...", 0, menu_cb, data(MenuAction::DeleteEntry), FL_MENU_DIVIDER);
    menu_->add("&Edit/&Focus File List", FL_COMMAND + 'b', menu_cb, data(MenuAction::FocusBrowser));
    menu_->add("&Edit/F&ind...", FL_COMMAND + 'f', menu_cb, data(MenuAction::FocusSearch));

    menu_->add("&View/&Files", FL_COMMAND + '1', menu_cb, data(MenuAction::ViewFiles));
    menu_->add("&View/&Manage", FL_COMMAND + '2', menu_cb, data(MenuAction::ViewManage));
    menu_->add("&View/A&ppearance/Follow &System", 0, menu_cb, data(MenuAction::AppearanceSystem),
               FL_MENU_RADIO | FL_MENU_DIVIDER);
    menu_->add("&View/A&ppearance/&Light", 0, menu_cb, data(MenuAction::AppearanceLight), FL_MENU_RADIO);
    menu_->add("&View/A&ppearance/&Dark", 0, menu_cb, data(MenuAction::AppearanceDark), FL_MENU_RADIO);

    menu_->add("&Help/&Keyboard Shortcuts", 0, menu_cb, data(MenuAction::Shortcuts));
    menu_->add("&Help/&About Zeal Disk Tool", 0, menu_cb, data(MenuAction::About));

    sync_appearance_menu();
}

void MainWindow::perform_menu_action(MenuAction action)
{
    switch (action) {
    case MenuAction::OpenImage:
        open_image();
        break;
    case MenuAction::CreateImage:
        new_image();
        break;
    case MenuAction::ApplyChanges:
        apply_changes();
        break;
    case MenuAction::DiscardChanges:
        cancel_changes();
        break;
    case MenuAction::Quit:
        if (confirm_quit()) {
            hide();
        }
        break;
    case MenuAction::RefreshDevices:
        refresh_devices();
        break;
    case MenuAction::NewPartition:
        new_partition();
        break;
    case MenuAction::CreateMbr:
        create_mbr();
        break;
    case MenuAction::FormatPartition:
        format_partition();
        break;
    case MenuAction::DeletePartition:
        delete_partition();
        break;
    case MenuAction::Properties:
        show_properties();
        break;
    case MenuAction::ImportFiles:
        set_view(View::Files);
        files_->action_import();
        break;
    case MenuAction::ExportSelection:
        set_view(View::Files);
        files_->action_export();
        break;
    case MenuAction::NewFolder:
        set_view(View::Files);
        files_->action_new_folder();
        break;
    case MenuAction::DeleteEntry:
        set_view(View::Files);
        files_->action_delete();
        break;
    case MenuAction::FocusBrowser:
        set_view(View::Files);
        files_->focus_list();
        break;
    case MenuAction::FocusSearch:
        set_view(View::Files);
        files_->focus_search();
        break;
    case MenuAction::ViewFiles:
        set_view(View::Files);
        break;
    case MenuAction::ViewManage:
        set_view(View::Manage);
        break;
    case MenuAction::AppearanceSystem:
        set_appearance(modern::Theme::System);
        break;
    case MenuAction::AppearanceLight:
        set_appearance(modern::Theme::Light);
        break;
    case MenuAction::AppearanceDark:
        set_appearance(modern::Theme::Dark);
        break;
    case MenuAction::Shortcuts:
        show_shortcuts();
        break;
    case MenuAction::About:
        show_about();
        break;
    }
}

void MainWindow::set_menu_enabled(const char *path, bool enabled)
{
    if (menu_ == nullptr) {
        return;
    }
    const Fl_Menu_Item *item = menu_->find_item(path);
    if (item == nullptr) {
        return;
    }
    const int index = (int) (item - menu_->menu());
    if (index < 0 || index >= menu_->size()) {
        return;
    }
    const int divider = menu_->mode(index) & FL_MENU_DIVIDER;
    menu_->mode(index, divider | (enabled ? 0 : FL_MENU_INACTIVE));
}

void MainWindow::sync_appearance_menu()
{
    if (menu_ == nullptr) {
        return;
    }
    struct Entry {
        const char *path;
        modern::Theme value;
    };
    const Entry entries[] = {
        {"&View/A&ppearance/Follow &System", modern::Theme::System},
        {"&View/A&ppearance/&Light", modern::Theme::Light},
        {"&View/A&ppearance/&Dark", modern::Theme::Dark},
    };
    const modern::Theme preference = modern::theme_preference();
    for (const Entry &entry : entries) {
        const Fl_Menu_Item *item = menu_->find_item(entry.path);
        if (item == nullptr) {
            continue;
        }
        const int index = (int) (item - menu_->menu());
        if (index < 0 || index >= menu_->size()) {
            continue;
        }
        /* mode() replaces the whole flag word, so the radio bit has to be
         * carried over instead of only setting FL_MENU_VALUE. */
        menu_->mode(index, FL_MENU_RADIO | (entry.value == preference ? FL_MENU_VALUE : 0));
    }
}

void MainWindow::apply_theme()
{
    /* Everything the panels draw reads the palette through modern::color() at
     * draw time; only the colours set on widgets once, at construction, have to
     * be re-applied here. */
    color(modern::color(modern::Palette::kWindow));
    if (menu_ != nullptr) {
        menu_->color(modern::color(modern::Palette::kTitleBg));
        menu_->textcolor(modern::color(modern::Palette::kDark2));
        menu_->selection_color(modern::color(modern::Palette::kSelection));
        menu_->redraw();
    }
    redraw();
}

void MainWindow::set_appearance(modern::Theme preference)
{
    modern::set_theme(preference);
    sync_appearance_menu();
    apply_theme();

    char message[64];
    if (preference == modern::Theme::System) {
        std::snprintf(message,
                      sizeof(message),
                      "Appearance: follow system (%s)",
                      modern::theme_name(modern::theme()));
    } else {
        std::snprintf(message, sizeof(message), "Appearance: %s", modern::theme_name(modern::theme()));
    }
    set_status(message);
}

void MainWindow::update_menu_states()
{
    disk_info_t *disk = current_disk();
    const int slot = selected_slot();
    const bool active = disk != nullptr && slot >= 0 && slot < MAX_PART_COUNT && disk->partitions[slot].active;
    const bool pending = disk != nullptr && disk->has_staged_changes;
    const bool valid = disk != nullptr && disk->valid;
    const bool can_partition = valid && disk->free_part_idx >= 0;
    const bool can_mbr =
        valid && !disk->has_mbr && !disk->has_staged_changes && !disk->partitions[0].active;
    const bool browser = files_ != nullptr && files_->has_target();
    const bool selection = files_ != nullptr && files_->has_selection();

    set_menu_enabled("&File/&Apply Changes", pending);
    set_menu_enabled("&File/&Discard Changes", pending);
    set_menu_enabled("&Disk/&New Partition...", can_partition);
    set_menu_enabled("&Disk/Create &MBR...", can_mbr);
    set_menu_enabled("&Disk/&Format Partition...", active);
    set_menu_enabled("&Disk/&Delete Partition...", active);
    set_menu_enabled("&Disk/&Properties...", disk != nullptr);
    set_menu_enabled("&Edit/&Import Files...", browser);
    set_menu_enabled("&Edit/&Export Selection...", browser && selection);
    set_menu_enabled("&Edit/&New Folder...", browser);
    set_menu_enabled("&Edit/&Delete...", browser && selection);
    set_menu_enabled("&Edit/&Focus File List", browser);
    set_menu_enabled("&Edit/F&ind...", browser);
}

MainWindow::~MainWindow()
{
    disk_set_status_fn(nullptr, nullptr);
}

void MainWindow::initialize()
{
    disks_refresh();
    int selected = state()->selected_disk;
    for (const std::string &path : image_paths_) {
        const int index = disk_open_image_file_path(state(), path.c_str());
        if (index >= 0) {
            selected = index;
        } else {
            open_error_ = status_text_;
        }
    }
    state()->selected_disk = selected >= 0 ? selected : (state()->disk_count > 0 ? 0 : -1);
    /* Opening or refreshing keeps the previous convenience of landing on a
     * partition; only clicking a disk header switches to the overview. */
    state()->selected_partition = default_partition_slot();
    model_changed();
    if (!open_error_.empty()) {
        set_status(open_error_.c_str());
    } else if (state()->disk_count == 0) {
        set_status("No disks detected. Use + to open or create a disk image.");
    } else if (state()->selected_partition >= 0) {
        char message[48];
        std::snprintf(message, sizeof(message), "Partition %d selected", state()->selected_partition);
        set_status(message);
    } else {
        set_status("Select a partition to browse its files.");
    }
}

disk_info_t *MainWindow::current_disk()
{
    return disk_get_current(state());
}

int MainWindow::selected_slot() const
{
    return disk_get_state()->selected_partition;
}

void MainWindow::set_status(const char *message)
{
    status_text_ = message != nullptr ? message : "";
    redraw();
}

int MainWindow::default_partition_slot() const
{
    disk_info_t *disk = disk_get_current(disk_get_state());
    if (disk == nullptr || !disk->valid) {
        return -1;
    }
    for (int slot = 0; slot < MAX_PART_COUNT; ++slot) {
        if (disk_is_valid_zealfs_partition(&disk->partitions[slot])) {
            return slot;
        }
    }
    for (int slot = 0; slot < MAX_PART_COUNT; ++slot) {
        if (disk->partitions[slot].active) {
            return slot;
        }
    }
    return -1;
}

void MainWindow::select_disk(int disk_index, int slot)
{
    if (disk_index < 0 || disk_index >= state()->disk_count) {
        return;
    }
    disk_info_t *current = current_disk();
    if (state()->selected_disk != disk_index && !disk_can_be_switched(current)) {
        fl_alert("The current disk has unsaved changes.\nApply or cancel them before switching disks.");
        refresh_views();
        return;
    }
    state()->selected_disk = disk_index;
    if (slot >= 0 && slot < MAX_PART_COUNT && state()->disks[disk_index].partitions[slot].active) {
        state()->selected_partition = slot;
    } else {
        /* Clicking the disk header shows the disk overview: every partition
         * stays visible in the map, none is selected. */
        state()->selected_partition = -1;
    }
    if (view_ == View::Files && state()->selected_partition < 0) {
        /* The Files view has no map/details to show an overview, so choose a
         * partition when a disk header is clicked there. */
        const int fallback = default_partition_slot();
        if (fallback >= 0) {
            state()->selected_partition = fallback;
        }
    }
    if (state()->selected_partition >= 0) {
        char message[64];
        std::snprintf(message, sizeof(message), "Partition %d selected", state()->selected_partition);
        set_status(message);
    } else {
        char message[64];
        std::snprintf(message, sizeof(message), "Disk %d selected", disk_index);
        set_status(message);
    }
    refresh_views();
}

void MainWindow::select_partition(int slot)
{
    disk_info_t *disk = current_disk();
    if (disk == nullptr || slot < 0 || slot >= MAX_PART_COUNT || !disk->partitions[slot].active) {
        slot = -1;
    }
    if (state()->selected_partition == slot) {
        return;
    }
    state()->selected_partition = slot;
    if (slot >= 0) {
        char message[64];
        std::snprintf(message, sizeof(message), "Partition %d selected", slot);
        set_status(message);
    } else {
        set_status("No partition selected");
    }
    refresh_views();
}

void MainWindow::model_changed()
{
    if (state()->selected_disk >= state()->disk_count) {
        state()->selected_disk = state()->disk_count > 0 ? 0 : -1;
    }
    if (state()->selected_disk < 0 && state()->disk_count > 0) {
        state()->selected_disk = 0;
    }
    disk_info_t *disk = current_disk();
    const int slot = state()->selected_partition;
    if (disk == nullptr) {
        state()->selected_partition = -1;
    } else if (slot >= MAX_PART_COUNT || (slot >= 0 && !disk->partitions[slot].active)) {
        state()->selected_partition = -1;
    }
    refresh_views();
}

void MainWindow::refresh_views()
{
    updating_ = true;
    disk_info_t *disk = current_disk();
    int slot = state()->selected_partition;
    if (disk == nullptr || slot < 0 || slot >= MAX_PART_COUNT || !disk->partitions[slot].active) {
        slot = -1;
    }
    state()->selected_partition = slot;

    if (sidebar_ != nullptr) {
        sidebar_->refresh(state(), state()->selected_disk, slot);
    }
    if (map_ != nullptr) {
        map_->refresh(disk, slot);
    }
    if (details_ != nullptr) {
        details_->refresh(disk, slot);
    }
    if (files_ != nullptr) {
        files_->retarget(disk, slot);
    }
    update_actions();
    update_window_title();
    updating_ = false;
    redraw();
}

void MainWindow::update_actions()
{
    update_window_title();
    update_menu_states();
    if (apply_button_ != nullptr) {
        disk_info_t *disk = current_disk();
        if (disk != nullptr && disk->has_staged_changes) {
            apply_button_->activate();
        } else {
            apply_button_->deactivate();
        }
        apply_button_->redraw();
    }
}

void MainWindow::update_window_title()
{
    std::string title = "Zeal Disk Tool";
    disk_info_t *disk = current_disk();
    if (disk != nullptr) {
        title += " - ";
        title += disk->name;
        if (disk->has_staged_changes) {
            title = "*" + title;
        }
    }
    copy_label(title.c_str());
}

void MainWindow::layout_chrome()
{
    content_y_ = menu_ != nullptr ? menu_->h() : 0;
    sidebar_w_ = std::min(280, std::max(180, (int) (w() * 0.17f)));
    right_w_ = std::min(300, std::max(210, (int) (w() * 0.20f)));
    gap_ = 14;
    if (menu_ != nullptr) {
        menu_->resize(0, 0, w(), menu_->h());
    }
}

void MainWindow::layout_children()
{
    layout_chrome();
    if (sidebar_ == nullptr) {
        return;
    }
    const bool files_view = view_ == View::Files;
    if (map_ != nullptr) {
        if (files_view) map_->hide(); else map_->show();
    }
    if (details_ != nullptr) {
        if (files_view) details_->hide(); else details_->show();
    }
    if (host_ != nullptr) {
        if (files_view) host_->show(); else host_->hide();
    }
    if (files_ != nullptr) {
        if (files_view) files_->show(); else files_->hide();
    }

    const int content_h = std::max(200, h() - content_y_ - status_h_);
    sidebar_->resize(0, content_y_, sidebar_w_, content_h);

    const int main_x = sidebar_w_ + gap_;
    const int main_w = w() - main_x - gap_;
    if (files_view) {
        /* Files view: the dual browser replaces the partition map and details
         * card, giving file management the whole content area. */
        const int browser_y = content_y_ + 48;
        const int browser_h = std::max(140, h() - browser_y - status_h_ - 6);
        const int host_w = std::max(400, (main_w - gap_) * 46 / 100);
        if (host_ != nullptr) {
            host_->resize(main_x, browser_y, host_w, browser_h);
        }
        files_->resize(main_x + host_w + gap_, browser_y, main_w - host_w - gap_, browser_h);
    } else {
        map_->resize(main_x, content_y_ + 44, main_w, 64);
        const int details_y = content_y_ + 116;
        const int details_h = std::max(190, h() - details_y - status_h_ - 8);
        details_->set_right_width(right_w_);
        details_->resize(main_x, details_y, main_w, details_h);
    }

    if (apply_button_ != nullptr) {
        const int button_w = 120;
        apply_button_->resize(w() - 14 - button_w, content_y_ + 8, button_w, 26);
    }

    tab_h_ = 24;
    tab_y_ = content_y_ + 10;
    tab_files_w_ = modern::text_width("Files", 11, true) + 30;
    tab_manage_w_ = modern::text_width("Manage", 11, true) + 30;
    const int right = apply_button_ != nullptr ? apply_button_->x() - 14 : w() - 14;
    /* Manage is the default view, so it sits on the left of the switch. */
    tab_manage_x_ = right - tab_files_w_ - tab_manage_w_;
    tab_files_x_ = tab_manage_x_ + tab_manage_w_;
    redraw();
}

void MainWindow::resize(int x, int y, int w, int h)
{
    Fl_Double_Window::resize(x, y, w, h);
    layout_children();
}

void MainWindow::set_view(View view)
{
    if (view_ == view) {
        return;
    }
    view_ = view;
    if (view == View::Files) {
        disk_info_t *disk = current_disk();
        const int slot = state()->selected_partition;
        if (disk != nullptr &&
            (slot < 0 || slot >= MAX_PART_COUNT || !disk->partitions[slot].active)) {
            const int fallback = default_partition_slot();
            if (fallback >= 0) {
                state()->selected_partition = fallback;
            }
        }
    }
    layout_children();
    refresh_views();
    redraw();
}

int MainWindow::handle(int event)
{
    if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE) {
        const int mx = Fl::event_x();
        const int my = Fl::event_y();
        if (my >= tab_y_ && my < tab_y_ + tab_h_) {
            if (mx >= tab_files_x_ && mx < tab_files_x_ + tab_files_w_) {
                set_view(View::Files);
                return 1;
            }
            if (mx >= tab_manage_x_ && mx < tab_manage_x_ + tab_manage_w_) {
                set_view(View::Manage);
                return 1;
            }
        }
    }
    return Fl_Double_Window::handle(event);
}

void MainWindow::draw_view_tabs()
{
    if (tab_h_ <= 0) {
        return;
    }
    const int total_w = tab_manage_x_ + tab_manage_w_ - tab_files_x_;
    modern::fill_round(tab_files_x_, tab_y_, total_w, tab_h_, 8, modern::color(modern::Palette::kPanelAlt));
    modern::stroke_round(tab_files_x_, tab_y_, total_w, tab_h_, 8, modern::color(modern::Palette::kButtonEdge), 1);
    const auto segment = [this](int sx, int sw, const char *label, bool active) {
        if (active) {
            modern::fill_round(sx + 2, tab_y_ + 2, sw - 4, tab_h_ - 4, 6, modern::color(modern::Palette::kAccentSoft));
        }
        modern::text_center(label,
                            sx + sw / 2,
                            tab_y_ + tab_h_ / 2 + 4,
                            modern::color(active ? modern::Palette::kDark : modern::Palette::kGray2),
                            11,
                            active);
    };
    segment(tab_manage_x_, tab_manage_w_, "Manage", view_ == View::Manage);
    segment(tab_files_x_, tab_files_w_, "Files", view_ == View::Files);
}

void MainWindow::draw_heading()
{
    const int x = sidebar_w_ + gap_;
    const int y = content_y_ + 10;
    /* Reserve the left edge of the whole switch, not of one segment: the two
     * segments are stored independently and their order may change. */
    const int tab_left = tab_manage_x_ > 0 ? std::min(tab_manage_x_, tab_files_x_) : 0;
    const int text_right = tab_left > 0 ? tab_left - 16 : w() - 14;
    disk_info_t *disk = current_disk();
    if (disk == nullptr) {
        modern::text("No disk selected", x, y + 22, modern::color(modern::Palette::kDark), 15, true);
        draw_view_tabs();
        return;
    }
    std::string title =
        "Disk " + std::to_string(state()->selected_disk) + "  \xE2\x80\x94  " + disk->name;
    if (view_ == View::Files && state()->selected_partition >= 0) {
        title += "  \xC2\xB7  Partition " + std::to_string(state()->selected_partition);
    }
    const int title_width = view_ == View::Manage
                                ? std::max(100, text_right - 420 - 16 - x)
                                : std::max(100, text_right - x);
    modern::text_clipped(title,
                         x,
                         y,
                         title_width,
                         24,
                         modern::color(disk->valid ? modern::Palette::kDark : modern::Palette::kWarning),
                         15,
                         true);
    if (view_ == View::Manage) {
        int used_slots = 0;
        for (int slot = 0; slot < MAX_PART_COUNT; ++slot) {
            if (disk->partitions[slot].active) {
                ++used_slots;
            }
        }
        char right[128];
        std::snprintf(right,
                      sizeof(right),
                      "%s  \xC2\xB7  %s  \xC2\xB7  %d of %d slots used",
                      utils::format_size(disk->size_bytes).c_str(),
                      disk->has_mbr ? "MBR" : "Raw",
                      used_slots,
                      MAX_PART_COUNT);
        modern::text_clipped(right,
                             text_right - 420,
                             y + 4,
                             420,
                             18,
                             modern::color(modern::Palette::kGray),
                             10,
                             false,
                             FL_ALIGN_RIGHT | FL_ALIGN_CLIP);
    }
    draw_view_tabs();
}

void MainWindow::draw_statusbar()
{
    const int y = h() - status_h_;
    fl_color(modern::color(modern::Palette::kTitleBg));
    fl_rectf(0, y, w(), status_h_);
    fl_color(modern::color(modern::Palette::kTitleLine));
    fl_line(0, y, w(), y);

    modern::text_clipped(status_text_, 14, y + 4, w() - 280, 16, modern::color(modern::Palette::kGray), 10, false);

    disk_info_t *disk = current_disk();
    if (disk != nullptr) {
        const bool pending = disk->has_staged_changes;
        const char *label = pending ? "Unsaved changes" : "All changes written";
        const Fl_Color text_color = modern::color(pending ? modern::Palette::kWarning : modern::Palette::kSuccess);
        const Fl_Color bg = modern::surface_tint(text_color, 0.88);
        const int label_w = modern::text_width(label, 10, false);
        const int pill_w = label_w + 34;
        const int pill_x = w() - 14 - pill_w;
        const int pill_y = y + 3;
        modern::fill_round(pill_x, pill_y, pill_w, 18, 9, bg);
        modern::fill_circle(pill_x + 11, pill_y + 9, 3, text_color);
        modern::text(label, pill_x + 21, pill_y + 13, text_color, 10, false);
    }
}

void MainWindow::draw()
{
    fl_color(modern::color(modern::Palette::kWindow));
    fl_rectf(0, 0, w(), h());
    draw_heading();
    draw_statusbar();
    Fl_Double_Window::draw();
}

void MainWindow::refresh_devices()
{
    const std::string current_path = current_disk() != nullptr ? std::string(current_disk()->path) : std::string();
    if (disks_refresh() != ERR_SUCCESS) {
        model_changed();
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
    state()->selected_partition = default_partition_slot();
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
        state()->selected_partition = default_partition_slot();
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
        state()->selected_partition = default_partition_slot();
    }
    model_changed();
    set_status("New image staged. Create partitions, then press Apply to write it.");
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
    const int slot = selected_slot();
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
                  utils::format_size((uint64_t) disk->partitions[slot].size_sectors * DISK_SECTOR_SIZE).c_str()) !=
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
    const int slot = selected_slot();
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

void MainWindow::show_about()
{
    dialog_about(this);
}

void MainWindow::show_shortcuts()
{
    fl_message("Keyboard shortcuts\n\n"
               "Ctrl+O   Open a disk image\n"
               "Ctrl+N   Create a disk image\n"
               "Ctrl+P   Create a partition\n"
               "Ctrl+S   Apply pending changes\n"
               "Ctrl+I   Import files\n"
               "Ctrl+E   Export the selection\n"
               "Ctrl+B   Focus the file browser\n"
               "Ctrl+F   Focus the search field\n"
               "Ctrl+1   Files view\n"
               "Ctrl+2   Manage view\n"
               "F5       Refresh devices\n"
               "Delete   Delete the selected partition\n"
               "Ctrl+Q   Quit");
}

void MainWindow::show_properties()
{
    disk_info_t *disk = current_disk();
    if (disk == nullptr) {
        fl_message("No disk selected.");
        return;
    }
    const int slot = selected_slot();
    char text[2048];
    int offset = std::snprintf(text,
                               sizeof(text),
                               "Disk %d\n%s\n%s\n%s\n\nCapacity: %s\nPartition table: %s\n"
                               "Pending changes: %s\nActive partitions: ",
                               state()->selected_disk,
                               disk->name,
                               disk->path,
                               disk->is_image ? "Disk image" : "Physical disk",
                               utils::format_size(disk->size_bytes).c_str(),
                               disk->has_mbr ? "MBR" : "Raw",
                               disk->has_staged_changes ? "yes" : "no");
    int used_slots = 0;
    for (int index = 0; index < MAX_PART_COUNT; ++index) {
        if (disk->partitions[index].active) {
            ++used_slots;
        }
    }
    offset += std::snprintf(text + offset, sizeof(text) - (size_t) offset, "%d of %d", used_slots, MAX_PART_COUNT);
    if (slot >= 0 && slot < MAX_PART_COUNT && disk->partitions[slot].active) {
        uint64_t used = 0;
        uint64_t total = 0;
        int percent = -1;
        if (disk->partitions[slot].type == ZEALFS_TYPE && disk_fs_usage(disk, slot, &used, &total) == 0) {
            percent = utils::percent_of(used, total);
        }
        char usage[64] = "not available";
        if (percent >= 0) {
            std::snprintf(usage, sizeof(usage), "%d%%", percent);
        }
        std::snprintf(text + offset,
                      sizeof(text) - (size_t) offset,
                      "\n\nPartition %d\nFile system: %s\nStart address: 0x%08llX\nSize: %s\nUsage: %s",
                      slot,
                      disk_get_fs_type(disk->partitions[slot].type),
                      (unsigned long long) disk->partitions[slot].start_lba * (unsigned long long) DISK_SECTOR_SIZE,
                      utils::format_size((uint64_t) disk->partitions[slot].size_sectors * DISK_SECTOR_SIZE).c_str(),
                      usage);
    }
    fl_message("%s", text);
}

void MainWindow::show_create_menu(int screen_x, int screen_y)
{
    /* Reuse the menu bar entries so the labels, shortcuts, enabled state and
     * actions stay in sync instead of duplicating the item list here. */
    const Fl_Menu_Item *new_partition = menu_->find_item("&Disk/&New Partition...");
    const Fl_Menu_Item *create_mbr = menu_->find_item("&Disk/Create &MBR...");
    const Fl_Menu_Item *create_image = menu_->find_item("&File/Create &Image...");
    const Fl_Menu_Item *open_image = menu_->find_item("&File/&Open Image...");
    if (new_partition == nullptr || create_mbr == nullptr || create_image == nullptr || open_image == nullptr) {
        return;
    }
    Fl_Menu_Item items[5];
    items[0] = *new_partition;
    items[1] = *create_mbr;
    items[1].flags = (items[1].flags & ~FL_MENU_DIVIDER) | FL_MENU_DIVIDER;
    items[2] = *create_image;
    items[3] = *open_image;
    items[4] = Fl_Menu_Item();

    const char *scheme = Fl::scheme();
    Fl::scheme(nullptr);
    const Fl_Menu_Item *picked = items->popup(screen_x - x(), screen_y - y(), nullptr, nullptr, menu_);
    Fl::scheme(scheme);
    if (picked != nullptr) {
        perform_menu_action(
            static_cast<MenuAction>(reinterpret_cast<intptr_t>(picked->user_data())));
    }
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

void MainWindow::copy_host_to_disk(const std::vector<std::string> &host_paths)
{
    if (files_ == nullptr) {
        return;
    }
    if (!files_->has_target()) {
        set_status("Select a ZealFS partition to copy into");
        return;
    }
    files_->import_host_paths(host_paths);
}

void MainWindow::copy_disk_to_host()
{
    if (files_ == nullptr || host_ == nullptr) {
        return;
    }
    if (!files_->has_selection()) {
        set_status("Select disk files or folders to copy");
        return;
    }
    files_->export_selection_to_host(host_->current_path());
    host_->refresh();
}

void MainWindow::apply_cb(Fl_Widget *, void *data)
{
    static_cast<MainWindow *>(data)->apply_changes();
}

void MainWindow::menu_cb(Fl_Widget *widget, void *data)
{
    auto *window = static_cast<MainWindow *>(widget->window());
    if (window == nullptr) {
        return;
    }
    window->perform_menu_action(static_cast<MenuAction>(reinterpret_cast<intptr_t>(data)));
}

void MainWindow::model_status_cb(const char *message, void *user)
{
    static_cast<MainWindow *>(user)->set_status(message);
}

void MainWindow::window_close_cb(Fl_Widget *widget, void *data)
{
    (void) widget;
    auto *window = static_cast<MainWindow *>(data);
    if (window->confirm_quit()) {
        window->hide();
    }
}
