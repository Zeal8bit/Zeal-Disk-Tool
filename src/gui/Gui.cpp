/**
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "Gui.hpp"

#include <cstdio>
#include <cstring>

namespace gui {

const Fl_Color background = fl_rgb_color(0xef, 0xf2, 0xf6);
const Fl_Color panel = fl_rgb_color(0xff, 0xff, 0xff);
const Fl_Color panel_alt = fl_rgb_color(0xf8, 0xfa, 0xfc);
const Fl_Color header = fl_rgb_color(0xe8, 0xed, 0xf3);
const Fl_Color border = fl_rgb_color(0xd3, 0xdb, 0xe5);
const Fl_Color text = fl_rgb_color(0x1f, 0x29, 0x33);
const Fl_Color text_muted = fl_rgb_color(0x64, 0x74, 0x8b);
const Fl_Color accent = fl_rgb_color(0x25, 0x63, 0xeb);
const Fl_Color accent_soft = fl_rgb_color(0xdb, 0xe8, 0xfe);
const Fl_Color warning = fl_rgb_color(0xb4, 0x53, 0x09);
const Fl_Color success = fl_rgb_color(0x15, 0x80, 0x3d);
const Fl_Color stripe = fl_rgb_color(0xf6, 0xf8, 0xfb);
const Fl_Color selection = fl_rgb_color(0xdb, 0xe8, 0xfe);
const Fl_Color selection_text = fl_rgb_color(0x1e, 0x3a, 0x8a);

Fl_Color partition_color(int slot)
{
    switch (slot) {
    case 0:
        return fl_rgb_color(0x4f, 0xad, 0x4f);
    case 1:
        return fl_rgb_color(0x39, 0x5b, 0x7e);
    case 2:
        return fl_rgb_color(0x9f, 0x62, 0xb6);
    case 3:
        return fl_rgb_color(0xc9, 0x4b, 0x24);
    default:
        return fl_rgb_color(0x94, 0xa3, 0xb8);
    }
}

std::string format_size(uint64_t bytes)
{
    char buffer[48];
    disk_get_size_str(bytes, buffer, sizeof(buffer));
    return buffer;
}

std::string format_file_size(uint64_t bytes)
{
    char buffer[48];
    if (bytes < KB) {
        std::snprintf(buffer, sizeof(buffer), "%llu B", (unsigned long long) bytes);
    } else if (bytes < MB) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KiB", (double) bytes / (double) KB);
    } else if (bytes < GB) {
        std::snprintf(buffer, sizeof(buffer), "%.2f MiB", (double) bytes / (double) MB);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f GiB", (double) bytes / (double) GB);
    }
    return buffer;
}

int percent_of(uint64_t used, uint64_t total)
{
    if (total == 0) {
        return 0;
    }
    int percent = (int) (used * 100 / total);
    return percent < 0 ? 0 : percent > 100 ? 100 : percent;
}

std::string join_path(const std::string &directory, const std::string &name)
{
    if (directory.empty() || directory == "/") {
        return "/" + name;
    }
    return directory + "/" + name;
}

std::string parent_path(const std::string &path)
{
    if (path.empty() || path == "/") {
        return "/";
    }
    std::string::size_type slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

std::string base_name(const std::string &path)
{
    /* Host dialogs return backslash-separated paths on Windows. */
    std::string::size_type slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string normalize_path(const std::string &path)
{
    std::string result;
    result.reserve(path.size() + 1);
    for (char character : path) {
        if (character == '\\') {
            character = '/';
        }
        if (character == '/' && !result.empty() && result.back() == '/') {
            continue;
        }
        result.push_back(character);
    }
    while (!result.empty() && result.front() == ' ') {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    if (result.empty()) {
        return "/";
    }
    if (result.front() != '/') {
        result.insert(result.begin(), '/');
    }
    while (result.size() > 1 && result.back() == '/') {
        result.pop_back();
    }
    return result;
}

void set_input_position(Fl_Input_ *input, int position)
{
    if (input == nullptr) {
        return;
    }
#if FL_MAJOR_VERSION > 1 || (FL_MAJOR_VERSION == 1 && FL_MINOR_VERSION >= 4)
    input->insert_position(position);
#else
    input->position(position);
#endif
}

bool valid_entry_name(const std::string &name)
{
    if (name.empty() || name.size() > ZFS_NAME_MAX) {
        return false;
    }
    if (name == "." || name == "..") {
        return false;
    }
    return name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}

}  // namespace gui
