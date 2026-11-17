/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gui/Theme.hpp"

#include <FL/Enumerations.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Widget.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace modern {
namespace {

struct Rgb {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
};

/* Light palette, sampled from the "Zeal Disk Utility" reference. */
constexpr Rgb kLightPalette[] = {
    {0xf4, 0xf7, 0xfb},  // kTitleBg
    {0xe1, 0xe5, 0xeb},  // kTitleLine
    {0xf7, 0xf9, 0xfc},  // kWindow
    {0xff, 0xff, 0xff},  // kPanel
    {0xfb, 0xfc, 0xfe},  // kPanelAlt
    {0xe1, 0xe6, 0xec},  // kDivider
    {0xe7, 0xeb, 0xf0},  // kDividerSoft
    {0x2b, 0x33, 0x45},  // kDark
    {0x39, 0x43, 0x5a},  // kDark2
    {0x66, 0x75, 0x8c},  // kGray
    {0x8a, 0x93, 0xa3},  // kGray2
    {0x2e, 0x7d, 0xe0},  // kAccent
    {0xff, 0xff, 0xff},  // kOnAccent
    {0x6b, 0x51, 0x76},  // kZealPurple
    {0xe5, 0xf0, 0xfd},  // kAccentSoft
    {0xd7, 0xe8, 0xfb},  // kAccentSoftHover
    {0x9c, 0xc5, 0xf2},  // kAccentEdge
    {0xe2, 0xef, 0xfc},  // kSelection
    {0xed, 0xf2, 0xf8},  // kHover
    {0xee, 0xf1, 0xf5},  // kHoverSoft
    {0xe1, 0xe8, 0xf0},  // kPressed
    {0xd6, 0xdb, 0xe2},  // kButtonEdge
    {0xb8, 0xc0, 0xcc},  // kTextDisabled
    {0xc3, 0xcb, 0xd6},  // kScrollThumb
    {0xed, 0xf1, 0xf6},  // kMapBg
    {0xf3, 0xf5, 0xf8},  // kRowAlt
    {0x4a, 0x55, 0x68},  // kIconDark
    {0x6b, 0x76, 0x84},  // kIconMuted
    {0xb4, 0x53, 0x09},  // kWarning
    {0x15, 0x80, 0x3d},  // kSuccess
    {0xf5, 0xc8, 0x4b},  // kFolder
};

/* Dark counterpart: same hues on a slate/navy base, text roles inverted and
 * the semantic colours brightened so they stay readable on the dark surface. */
constexpr Rgb kDarkPalette[] = {
    {0x16, 0x1d, 0x29},  // kTitleBg
    {0x24, 0x2e, 0x3d},  // kTitleLine
    {0x10, 0x16, 0x1f},  // kWindow
    {0x18, 0x20, 0x2c},  // kPanel
    {0x1d, 0x27, 0x34},  // kPanelAlt
    {0x2c, 0x37, 0x45},  // kDivider
    {0x25, 0x2f, 0x3c},  // kDividerSoft
    {0xe6, 0xeb, 0xf3},  // kDark
    {0xc2, 0xcb, 0xd8},  // kDark2
    {0x8f, 0x9c, 0xb0},  // kGray
    {0x6c, 0x7a, 0x8f},  // kGray2
    {0x4d, 0x9b, 0xf5},  // kAccent
    {0x0b, 0x13, 0x20},  // kOnAccent
    {0xb0, 0x8f, 0xd0},  // kZealPurple
    {0x1b, 0x2f, 0x47},  // kAccentSoft
    {0x24, 0x3c, 0x5a},  // kAccentSoftHover
    {0x3a, 0x64, 0x8d},  // kAccentEdge
    {0x1d, 0x31, 0x49},  // kSelection
    {0x1e, 0x28, 0x35},  // kHover
    {0x22, 0x2c, 0x3a},  // kHoverSoft
    {0x27, 0x32, 0x3f},  // kPressed
    {0x36, 0x41, 0x4f},  // kButtonEdge
    {0x59, 0x65, 0x7a},  // kTextDisabled
    {0x3d, 0x49, 0x59},  // kScrollThumb
    {0x14, 0x1b, 0x25},  // kMapBg
    {0x1b, 0x24, 0x31},  // kRowAlt
    {0x9a, 0xa7, 0xba},  // kIconDark
    {0x8b, 0x97, 0xa8},  // kIconMuted
    {0xf0, 0xa2, 0x4a},  // kWarning
    {0x4f, 0xbf, 0x7f},  // kSuccess
    {0xf5, 0xc8, 0x4b},  // kFolder
};

constexpr size_t kSlotCount = static_cast<size_t>(Palette::kCount);
static_assert(sizeof(kLightPalette) / sizeof(Rgb) == kSlotCount, "light palette does not match Palette");
static_assert(sizeof(kDarkPalette) / sizeof(Rgb) == kSlotCount, "dark palette does not match Palette");

Fl_Color g_palette[kSlotCount];
bool g_palette_ready = false;
Theme g_preference = Theme::System;
Theme g_active = Theme::Light;

/* FLTK's pristine colour map, captured before the first theme is applied so
 * the light theme keeps the stock shading of boxes, menus and scrollbars. */
unsigned char g_gray_ramp[FL_NUM_GRAY][3];
unsigned char g_background2[3];
unsigned char g_foreground[3];
unsigned char g_inactive[3];
unsigned char g_selection[3];
bool g_defaults_saved = false;

void capture_fltk_defaults()
{
    if (g_defaults_saved) {
        return;
    }
    for (int index = 0; index < FL_NUM_GRAY; ++index) {
        Fl::get_color(fl_gray_ramp(index),
                      g_gray_ramp[index][0],
                      g_gray_ramp[index][1],
                      g_gray_ramp[index][2]);
    }
    Fl::get_color(FL_BACKGROUND2_COLOR, g_background2[0], g_background2[1], g_background2[2]);
    Fl::get_color(FL_FOREGROUND_COLOR, g_foreground[0], g_foreground[1], g_foreground[2]);
    Fl::get_color(FL_INACTIVE_COLOR, g_inactive[0], g_inactive[1], g_inactive[2]);
    Fl::get_color(FL_SELECTION_COLOR, g_selection[0], g_selection[1], g_selection[2]);
    g_defaults_saved = true;
}

void build_palette(Theme active)
{
    const Rgb *table = active == Theme::Dark ? kDarkPalette : kLightPalette;
    for (size_t index = 0; index < kSlotCount; ++index) {
        g_palette[index] = fl_rgb_color(table[index].red, table[index].green, table[index].blue);
    }
    g_palette_ready = true;
}

/* Stock FLTK widgets (menus, message boxes, scrollbars, text fields) take their
 * colours from the global map, so a theme switch has to rewrite it. */
void apply_fltk_colors(Theme active)
{
    for (int index = 0; index < FL_NUM_GRAY; ++index) {
        Fl::set_color(fl_gray_ramp(index),
                      g_gray_ramp[index][0],
                      g_gray_ramp[index][1],
                      g_gray_ramp[index][2]);
    }
    Fl::set_color(FL_BACKGROUND2_COLOR, g_background2[0], g_background2[1], g_background2[2]);
    Fl::set_color(FL_FOREGROUND_COLOR, g_foreground[0], g_foreground[1], g_foreground[2]);
    Fl::set_color(FL_INACTIVE_COLOR, g_inactive[0], g_inactive[1], g_inactive[2]);
    Fl::set_color(FL_SELECTION_COLOR, g_selection[0], g_selection[1], g_selection[2]);

    if (active == Theme::Dark) {
        const Rgb &window = kDarkPalette[static_cast<size_t>(Palette::kWindow)];
        const Rgb &field = kDarkPalette[static_cast<size_t>(Palette::kPanelAlt)];
        const Rgb &text = kDarkPalette[static_cast<size_t>(Palette::kDark)];
        const Rgb &muted = kDarkPalette[static_cast<size_t>(Palette::kTextDisabled)];
        const Rgb &selection = kDarkPalette[static_cast<size_t>(Palette::kSelection)];
        /* background() rebuilds the whole gray ramp around the new base colour,
         * which is what the stock box and bevel drawing shades from. */
        Fl::background(window.red, window.green, window.blue);
        Fl::background2(field.red, field.green, field.blue);
        Fl::foreground(text.red, text.green, text.blue);
        Fl::set_color(FL_INACTIVE_COLOR, muted.red, muted.green, muted.blue);
        Fl::set_color(FL_SELECTION_COLOR, selection.red, selection.green, selection.blue);
    } else {
        /* Same tint the UI has always used: only the background index moves, so
         * the stock ramp keeps its shading. */
        Fl::set_color(FL_BACKGROUND_COLOR, 0xef, 0xf2, 0xf6);
    }
}

void redraw_widget(Fl_Widget *widget)
{
    if (widget == nullptr) {
        return;
    }
    widget->redraw();
    Fl_Group *group = widget->as_group();
    if (group == nullptr) {
        return;
    }
    for (int index = 0; index < group->children(); ++index) {
        redraw_widget(group->child(index));
    }
}

void redraw_all_windows()
{
    for (Fl_Window *window = Fl::first_window(); window != nullptr; window = Fl::next_window(window)) {
        redraw_widget(window);
    }
}

#if !defined(_WIN32)
bool command_output_contains(const char *command, const char *needle)
{
    FILE *pipe = popen(command, "r");
    if (pipe == nullptr) {
        return false;
    }
    char buffer[256] = {0};
    const size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, pipe);
    pclose(pipe);
    if (read == 0) {
        return false;
    }
    return std::strstr(buffer, needle) != nullptr;
}
#endif

std::filesystem::path preference_path()
{
#ifdef _WIN32
    const char *appdata = std::getenv("APPDATA");
    if (appdata == nullptr || appdata[0] == '\0') {
        return {};
    }
    return std::filesystem::path(appdata) / "zeal-disk-tool" / "appearance";
#elif defined(__APPLE__)
    const char *home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        return {};
    }
    return std::filesystem::path(home) / "Library" / "Application Support" / "zeal-disk-tool" / "appearance";
#else
    const char *config_home = std::getenv("XDG_CONFIG_HOME");
    if (config_home != nullptr && config_home[0] != '\0') {
        return std::filesystem::path(config_home) / "zeal-disk-tool" / "appearance";
    }
    const char *home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        return {};
    }
    return std::filesystem::path(home) / ".config" / "zeal-disk-tool" / "appearance";
#endif
}

Theme load_preference()
{
    const std::filesystem::path path = preference_path();
    if (path.empty()) {
        return Theme::System;
    }
    std::ifstream input(path);
    std::string value;
    if (!std::getline(input, value)) {
        return Theme::System;
    }
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) {
        value.pop_back();
    }
    if (value == "light") {
        return Theme::Light;
    }
    if (value == "dark") {
        return Theme::Dark;
    }
    return Theme::System;
}

void save_preference(Theme value)
{
    const std::filesystem::path path = preference_path();
    if (path.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        return;
    }
    output << theme_name(value) << '\n';
}

}  // namespace

Fl_Color color(Palette slot)
{
    if (!g_palette_ready) {
        build_palette(g_active);
    }
    const size_t index = static_cast<size_t>(slot);
    return index < kSlotCount ? g_palette[index] : FL_BLACK;
}

Fl_Color color(unsigned rgb)
{
    return fl_rgb_color((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

Theme theme()
{
    return g_active;
}

Theme theme_preference()
{
    return g_preference;
}

bool is_dark()
{
    return g_active == Theme::Dark;
}

const char *theme_name(Theme value)
{
    switch (value) {
    case Theme::Light:
        return "light";
    case Theme::Dark:
        return "dark";
    case Theme::System:
        break;
    }
    return "system";
}

Theme detect_system_theme()
{
#if defined(_WIN32)
    /* Windows has no public API for this before Windows 11's dark-mode one, so
     * the personalisation key is the portable source of truth. */
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0,
                      KEY_QUERY_VALUE,
                      &key) == ERROR_SUCCESS) {
        DWORD apps_light_theme = 1;
        DWORD size = sizeof(apps_light_theme);
        DWORD type = 0;
        const LONG status = RegQueryValueExA(key,
                                             "AppsUseLightTheme",
                                             nullptr,
                                             &type,
                                             reinterpret_cast<LPBYTE>(&apps_light_theme),
                                             &size);
        RegCloseKey(key);
        if (status == ERROR_SUCCESS && type == REG_DWORD && apps_light_theme == 0) {
            return Theme::Dark;
        }
    }
    return Theme::Light;
#elif defined(__APPLE__)
    /* FLTK apps are not NSApplication-driven, so the defaults domain is the
     * cheapest way to read the appearance without an Objective-C++ file. */
    if (command_output_contains("defaults read -g AppleInterfaceStyle 2>/dev/null", "Dark")) {
        return Theme::Dark;
    }
    return Theme::Light;
#else
    const char *gtk_theme = std::getenv("GTK_THEME");
    if (gtk_theme != nullptr && std::strstr(gtk_theme, "dark") != nullptr) {
        return Theme::Dark;
    }
    const char *kde_scheme = std::getenv("KDE_COLOR_SCHEME");
    if (kde_scheme != nullptr && std::strstr(kde_scheme, "Dark") != nullptr) {
        return Theme::Dark;
    }
    if (command_output_contains("gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null",
                                "prefer-dark")) {
        return Theme::Dark;
    }
    return Theme::Light;
#endif
}

void set_theme(Theme preference, bool persist)
{
    capture_fltk_defaults();
    g_preference = preference;
    g_active = preference == Theme::System ? detect_system_theme() : preference;
    build_palette(g_active);
    apply_fltk_colors(g_active);
    if (persist) {
        save_preference(preference);
    }
    redraw_all_windows();
}

void load_theme()
{
    set_theme(load_preference(), false);
}

}  // namespace modern
