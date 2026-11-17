/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <FL/Fl_Input_.H>
#include <string>

#include "disk.h"

/* Formatting and path helpers for the FLTK frontend. */
namespace utils {

/* "12.34 MiB", using the same formatting as the rest of the GUI.
 * With `bytes_below_kib`, byte counts under 1 KiB stay in bytes. */
std::string format_size(uint64_t bytes, bool bytes_below_kib = false);
/* Integer percentage of `total`, clamped to 0..100. */
int percent_of(uint64_t used, uint64_t total);

/* Rooted ZealFS paths never have a trailing slash except for the root "/". */
std::string join_path(const std::string &directory, const std::string &name);
std::string parent_path(const std::string &path);
std::string base_name(const std::string &path);
/* Collapse duplicate slashes, force a leading slash and drop the trailing one. */
std::string normalize_path(const std::string &path);
/* Entry names are limited to ZFS_NAME_MAX bytes and cannot contain slashes. */
bool valid_entry_name(const std::string &name);

/* Move the text cursor to `position`, hiding the FLTK 1.3/1.4 API difference. */
void set_input_position(Fl_Input_ *input, int position);

}  // namespace utils
