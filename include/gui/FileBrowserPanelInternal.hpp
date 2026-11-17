/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>

#include "disk.h"
#include "gui/ModernStyle.hpp"

/* Private helpers shared by the browser model (FileBrowserPanel.cpp) and its
 * drawing code (FileBrowserDraw.cpp). */
namespace fbp {

constexpr int kRowHeight = 28;
constexpr int kTreeRowHeight = 26;
constexpr int kTableHeader = 30;
constexpr int kCrumbSeparator = 16;

inline std::string lower_copy(const char *value)
{
    std::string result(value != nullptr ? value : "");
    for (char &c : result) {
        c = (char) std::tolower((unsigned char) c);
    }
    return result;
}

inline bool name_matches_filter(const char *name, const std::string &needle)
{
    if (needle.empty()) {
        return true;
    }
    return lower_copy(name).find(needle) != std::string::npos;
}

inline const char *type_name(const zfs_entry &entry)
{
    if (entry.directory) {
        return "Folder";
    }
    switch (modern::file_kind(entry.name, false)) {
    case modern::FileKind::Text:
        return "Text";
    case modern::FileKind::Binary:
        return "Binary";
    case modern::FileKind::Image:
        return "Image";
    case modern::FileKind::Archive:
        return "Archive";
    case modern::FileKind::Config:
        return "Config";
    case modern::FileKind::Folder:
    case modern::FileKind::Other:
        break;
    }
    return "File";
}

struct Columns {
    int size_x;
    int type_x;
    int mod_x;
};

inline Columns table_columns(int table_x, int table_w)
{
    int size_w = 90;
    int type_w = 90;
    int mod_w = 140;
    if (table_w < 520) {
        size_w = 78;
        type_w = 78;
        mod_w = 120;
    }
    if (table_w < 420) {
        size_w = 68;
        type_w = 64;
        mod_w = 100;
    }
    Columns columns;
    columns.mod_x = table_x + table_w - mod_w;
    columns.type_x = columns.mod_x - type_w;
    columns.size_x = columns.type_x - size_w;
    return columns;
}

inline std::string timestamp(const zfs_entry &entry)
{
    char buffer[32];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%04u-%02u-%02u %02u:%02u",
                  entry.year,
                  entry.month,
                  entry.day,
                  entry.hour,
                  entry.minute);
    return buffer;
}

inline uint64_t timestamp_value(const zfs_entry &entry)
{
    const uint64_t days = ((uint64_t) entry.year * 100 + entry.month) * 100 + entry.day;
    return ((days * 100 + entry.hour) * 100 + entry.minute) * 100 + entry.second;
}

}  // namespace fbp
