/**
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <FL/Fl.H>
#include <FL/Fl_Input_.H>
#include <string>

#include "disk.h"

/* Shared palette and small formatting helpers for the FLTK frontend. */
namespace gui {

extern const Fl_Color background;
extern const Fl_Color panel;
extern const Fl_Color panel_alt;
extern const Fl_Color header;
extern const Fl_Color border;
extern const Fl_Color text;
extern const Fl_Color text_muted;
extern const Fl_Color accent;
extern const Fl_Color accent_soft;
extern const Fl_Color warning;
extern const Fl_Color success;
extern const Fl_Color stripe;
extern const Fl_Color selection;
extern const Fl_Color selection_text;

/* Colour used for partition `slot` in the map and detail chips. */
Fl_Color partition_color(int slot);

/* "12.34 MiB", using the same formatting as the rest of the GUI. */
std::string format_size(uint64_t bytes);
/* File-list variant: byte counts below 1 KiB stay in bytes. */
std::string format_file_size(uint64_t bytes);
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

}  // namespace gui
