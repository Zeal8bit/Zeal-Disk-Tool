/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gui/Utils.hpp"

#include <cstdio>
#include <cstring>

namespace utils {

std::string format_size(uint64_t bytes, bool bytes_below_kib)
{
    char buffer[48];
    if (bytes_below_kib && bytes < KB) {
        std::snprintf(buffer, sizeof(buffer), "%llu B", (unsigned long long) bytes);
        return buffer;
    }
    disk_get_size_str(bytes, buffer, sizeof(buffer));
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

}  // namespace utils
