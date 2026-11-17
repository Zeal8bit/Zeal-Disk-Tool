/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gui/ModernStyle.hpp"

#include <FL/Fl_Group.H>
#include <FL/Fl_SVG_Image.H>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "ui_icons.h"

namespace modern {

struct GlyphIcon {
    Glyph glyph;
    const char *name;
};

static constexpr GlyphIcon kGlyphIcons[] = {
    {Glyph::Create, "plus"},          {Glyph::Delete, "trash-2"},     {Glyph::Format, "eraser"},
    {Glyph::Settings, "settings"},    {Glyph::Import, "download"},    {Glyph::Export, "upload"},
    {Glyph::NewFolder, "folder-plus"}, {Glyph::Search, "search"},     {Glyph::Back, "chevron-left"},
    {Glyph::Forward, "chevron-right"}, {Glyph::Up, "chevron-up"},     {Glyph::Down, "chevron-down"},
    {Glyph::Home, "house"},           {Glyph::More, "ellipsis"},      {Glyph::Drive, "hard-drive"},
    {Glyph::FolderOpen, "folder-open"},
};

Fl_Color slot_color(int slot)
{
    switch (slot) {
    case 0:
        return color(0x4c9ef5);
    case 1:
        return color(0x7c6ce6);
    case 2:
        return color(0x35b36b);
    case 3:
        return color(0xf2a93b);
    default:
        return color(0x94a3b8);
    }
}

Fl_Color lighten(Fl_Color value, double amount)
{
    uchar red = 0;
    uchar green = 0;
    uchar blue = 0;
    Fl::get_color(value, red, green, blue);
    const double keep = 1.0 - amount;
    return fl_rgb_color((uchar) (red * keep + 255 * amount),
                        (uchar) (green * keep + 255 * amount),
                        (uchar) (blue * keep + 255 * amount));
}

Fl_Color darken(Fl_Color value, double amount)
{
    uchar red = 0;
    uchar green = 0;
    uchar blue = 0;
    Fl::get_color(value, red, green, blue);
    const double keep = 1.0 - amount;
    return fl_rgb_color((uchar) (red * keep), (uchar) (green * keep), (uchar) (blue * keep));
}

static void set_font(int size, bool bold)
{
    fl_font(bold ? FL_HELVETICA_BOLD : FL_HELVETICA, size);
}

void text(const std::string &value, int x, int baseline, Fl_Color value_color, int size, bool bold)
{
    set_font(size, bold);
    fl_color(value_color);
    fl_draw(value.c_str(), x, baseline);
}

void text(const char *value, int x, int baseline, Fl_Color value_color, int size, bool bold)
{
    text(std::string(value != nullptr ? value : ""), x, baseline, value_color, size, bold);
}

void text_center(const std::string &value, int cx, int baseline, Fl_Color value_color, int size, bool bold)
{
    set_font(size, bold);
    fl_color(value_color);
    fl_draw(value.c_str(), (int) (cx - fl_width(value.c_str()) / 2.0), baseline);
}

void text_clipped(const std::string &value,
                  int x,
                  int y,
                  int w,
                  int h,
                  Fl_Color value_color,
                  int size,
                  bool bold,
                  Fl_Align align)
{
    set_font(size, bold);
    fl_color(value_color);
    fl_draw(value.c_str(), x, y, w, h, align | FL_ALIGN_CLIP);
}

int text_width(const std::string &value, int size, bool bold)
{
    set_font(size, bold);
    return (int) fl_width(value.c_str());
}

static int round_radius(int w, int h, int radius)
{
    int value = radius < 1 ? 1 : radius;
    value = std::min(value, w / 2);
    value = std::min(value, h / 2);
    return value < 1 ? 1 : value;
}

void fill_round(int x, int y, int w, int h, int radius, Fl_Color value_color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    fl_color(value_color);
    fl_rounded_rectf(x, y, w, h, round_radius(w, h, radius));
}

void stroke_round(int x, int y, int w, int h, int radius, Fl_Color value_color, int line_width)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    fl_color(value_color);
    fl_line_style(FL_SOLID, line_width);
    fl_rounded_rect(x, y, w, h, round_radius(w, h, radius));
    fl_line_style(0);
}

void fill_circle(int cx, int cy, int radius, Fl_Color value_color)
{
    fl_color(value_color);
    fl_pie(cx - radius, cy - radius, radius * 2, radius * 2, 0, 360);
}

static const icons::IconSource *find_icon(const char *name)
{
    for (size_t index = 0; index < icons::kSourceCount; ++index) {
        if (std::strcmp(icons::kSources[index].name, name) == 0) {
            return &icons::kSources[index];
        }
    }
    return nullptr;
}

static float icon_scale()
{
    /* Rasterise at the display's pixel density; Fl_SVG_Image::draw() maps the
     * extra pixels back onto the logical size. */
    const float scale = Fl::screen_scale(0);
    return scale > 0.0f ? scale : 1.0f;
}

/* Rasterise one icon per (name, size, colour) combination and keep it around:
 * the vector sources stay crisp at every UI scale without shipping rasters. */
static Fl_SVG_Image *cached_icon(const char *name, int size, Fl_Color value_color)
{
    static std::map<std::string, Fl_SVG_Image *> cache;
    char key[80];
    std::snprintf(key, sizeof(key), "%s/%d/%u", name, size, (unsigned) value_color);
    auto found = cache.find(key);
    if (found != cache.end()) {
        return found->second;
    }
    const icons::IconSource *source = find_icon(name);
    if (source == nullptr) {
        return nullptr;
    }
    uchar red = 0;
    uchar green = 0;
    uchar blue = 0;
    Fl::get_color(value_color, red, green, blue);
    char hex[8];
    std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", red, green, blue);
    std::string svg(source->svg, source->size);
    size_t position = 0;
    while ((position = svg.find("currentColor", position)) != std::string::npos) {
        svg.replace(position, 12, hex);
        position += 7;
    }
    auto *image = new Fl_SVG_Image(name, (const unsigned char *) svg.data(), svg.size());
    const int pixels = std::max(1, (int) std::lround(size * icon_scale()));
    image->scale(pixels, pixels, 1, 1);
    cache[key] = image;
    return image;
}

static void draw_icon(const char *name, int cx, int cy, int size, Fl_Color value_color)
{
    if (size <= 0) {
        return;
    }
    Fl_SVG_Image *image = cached_icon(name, size, value_color);
    if (image != nullptr) {
        image->draw(cx - size / 2, cy - size / 2, size, size);
    }
}

static void draw_icon_box(const char *name, int x, int y, int w, int h, Fl_Color value_color)
{
    const int size = std::min(w, h);
    if (size <= 0) {
        return;
    }
    draw_icon(name, x + w / 2, y + h / 2, size, value_color);
}

void draw_glyph(Glyph glyph, int cx, int cy, int size, Fl_Color value_color)
{
    for (const GlyphIcon &entry : kGlyphIcons) {
        if (entry.glyph == glyph) {
            draw_icon(entry.name, cx, cy, size, value_color);
            return;
        }
    }
    draw_icon("ellipsis", cx, cy, size, value_color);
}

void draw_drive(int x, int y, int w, int h, Fl_Color value_color)
{
    draw_icon_box("hard-drive", x, y, w, h, value_color);
}

void draw_folder(int x, int y, int w, int h)
{
    draw_icon_box("folder", x, y, w, h, color(kFolder));
}

FileKind file_kind(const char *name, bool directory)
{
    if (directory) {
        return FileKind::Folder;
    }
    const char *dot = name != nullptr ? std::strrchr(name, '.') : nullptr;
    if (dot == nullptr || dot[1] == 0) {
        return FileKind::Other;
    }
    std::string ext(dot + 1);
    for (char &c : ext) {
        c = (char) std::tolower((unsigned char) c);
    }
    if (ext == "txt" || ext == "md" || ext == "log") {
        return FileKind::Text;
    }
    if (ext == "ini" || ext == "cfg" || ext == "conf" || ext == "json" || ext == "xml" || ext == "yml" ||
        ext == "yaml") {
        return FileKind::Config;
    }
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "bmp" || ext == "svg" ||
        ext == "webp") {
        return FileKind::Image;
    }
    if (ext == "zip" || ext == "gz" || ext == "tgz" || ext == "7z" || ext == "rar" || ext == "tar") {
        return FileKind::Archive;
    }
    if (ext == "bin" || ext == "img" || ext == "rom" || ext == "z80" || ext == "elf" || ext == "o" ||
        ext == "com" || ext == "exe") {
        return FileKind::Binary;
    }
    return FileKind::Other;
}

/* Flat rounded field: the scheme's beveled boxes look out of place next to
 * the custom-drawn chrome. */
static void draw_field(int x, int y, int w, int h, Fl_Color background)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    fl_color(background);
    fl_rounded_rectf(x, y, w, h, 6);
    fl_color(color(kDivider));
    fl_rounded_rect(x, y, w, h, 6);
}

Fl_Boxtype field_box()
{
    static Fl_Boxtype type = FL_NO_BOX;
    if (type == FL_NO_BOX) {
        type = (Fl_Boxtype) (FL_FREE_BOXTYPE + 3);
        Fl::set_boxtype(type, draw_field, 1, 1, 2, 2);
    }
    return type;
}

ModernButton::ModernButton(int x, int y, int w, int h, const char *label, bool primary, Fl_Color accent)
    : Fl_Button(x, y, w, h, label),
      primary_(primary),
      accent_(accent != 0 ? accent : modern::color(kAccent))
{
    box(FL_NO_BOX);
    labelsize(12);
}

void ModernButton::draw()
{
    const bool enabled = active() && (parent() == nullptr || parent()->active());
    Fl_Color fill = primary_ ? accent_ : modern::color(kPanel);
    Fl_Color text = primary_ ? modern::color(kPanel) : modern::color(kDark2);
    if (!enabled) {
        fill = modern::color(kPanelAlt);
        text = modern::color(0xb8c0cc);
    } else if (value()) {
        fill = primary_ ? darken(accent_, 0.10) : modern::color(kPressed);
    } else if (hover_) {
        fill = primary_ ? darken(accent_, 0.04) : modern::color(kHover);
    }
    fill_round(x(), y(), w(), h(), 6, fill);
    if (!primary_) {
        stroke_round(x(), y(), w(), h(), 6, modern::color(enabled ? kButtonEdge : kDivider), 1);
    }
    const int size = h() >= 24 ? 12 : 10;
    /* Centre the label using the font's ascent/descent rather than a guessed
     * baseline; the caption otherwise sits two pixels low in the pill. */
    fl_font(FL_HELVETICA, size);
    const int baseline = y() + (h() + fl_height()) / 2 - fl_descent();
    text_center(label() != nullptr ? label() : "", x() + w() / 2, baseline, text, size, false);
    if (enabled && Fl::focus() == this) {
        stroke_round(x() + 1, y() + 1, w() - 2, h() - 2, 6, lighten(accent_, 0.45), 1);
    }
}

int ModernButton::handle(int event)
{
    if (event == FL_ENTER && !hover_) {
        hover_ = true;
        redraw();
        return 1;
    }
    if (event == FL_LEAVE && hover_) {
        hover_ = false;
        redraw();
        return 1;
    }
    return Fl_Button::handle(event);
}

void draw_file(FileKind kind, int x, int y, int w, int h)
{
    const char *name = "file";
    switch (kind) {
    case FileKind::Folder:
        draw_folder(x, y, w, h);
        return;
    case FileKind::Text:
        name = "file-text";
        break;
    case FileKind::Binary:
        name = "file-digit";
        break;
    case FileKind::Image:
        name = "file-image";
        break;
    case FileKind::Archive:
        name = "file-archive";
        break;
    case FileKind::Config:
        name = "file-cog";
        break;
    case FileKind::Other:
        break;
    }
    draw_icon_box(name, x, y, w, h, color(0x6b7684));
}

}  // namespace modern
