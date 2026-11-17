/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/fl_draw.H>

#include <string>

/* Visual language for the modern single-window UI: sampled palette from the
 * "Zeal Disk Utility" reference, drawing helpers and small vector glyphs. */
namespace modern {

/* Palette sampled from the "Zeal Disk Utility" reference. */
enum Palette : unsigned {
    kTitleBg = 0xf4f7fb,
    kTitleLine = 0xe1e5eb,
    kWindow = 0xf7f9fc,
    kPanel = 0xffffff,
    kPanelAlt = 0xfbfcfe,
    kDivider = 0xe1e6ec,
    kDividerSoft = 0xe7ebf0,
    kDark = 0x2b3345,
    kDark2 = 0x39435a,
    kGray = 0x66758c,
    kGray2 = 0x8a93a3,
    kAccent = 0x2e7de0,
    kZealPurple = 0x6b5176,
    kAccentSoft = 0xe5f0fd,
    kAccentEdge = 0x9cc5f2,
    kSelection = 0xe2effc,
    kHover = 0xedf2f8,
    kPressed = 0xe1e8f0,
    kButtonEdge = 0xd6dbe2,
    kWarning = 0xb45309,
    kSuccess = 0x15803d,
    kFolder = 0xf5c84b,
    kIconDark = 0x4a5568,
};

/* Small monochrome glyphs, drawn from the bundled Lucide sources. */
enum class Glyph {
    Create,
    Delete,
    Format,
    Settings,
    Import,
    Export,
    NewFolder,
    Search,
    Back,
    Forward,
    Up,
    Down,
    Home,
    More,
    Drive,
    FolderOpen,
};

/* File family used to pick an icon; derived from the entry name. */
enum class FileKind {
    Folder,
    Text,
    Binary,
    Image,
    Archive,
    Config,
    Other,
};

inline Fl_Color color(unsigned rgb)
{
    return fl_rgb_color((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

/* Accent colour of a disk slot in the sidebar. */
Fl_Color slot_color(int slot);
/* Blend a colour with white (amount 0..1). */
Fl_Color lighten(Fl_Color color, double amount);
/* Blend a colour with black (amount 0..1). */
Fl_Color darken(Fl_Color color, double amount);

void text(const std::string &value, int x, int baseline, Fl_Color color, int size, bool bold = false);
void text(const char *value, int x, int baseline, Fl_Color color, int size, bool bold = false);
void text_center(const std::string &value, int cx, int baseline, Fl_Color color, int size, bool bold = false);
void text_clipped(const std::string &value,
                  int x,
                  int y,
                  int w,
                  int h,
                  Fl_Color color,
                  int size,
                  bool bold,
                  Fl_Align align = FL_ALIGN_LEFT | FL_ALIGN_CLIP);
int text_width(const std::string &value, int size, bool bold = false);

void fill_round(int x, int y, int w, int h, int radius, Fl_Color color);
void stroke_round(int x, int y, int w, int h, int radius, Fl_Color color, int line_width = 1);
void fill_circle(int cx, int cy, int radius, Fl_Color color);

/* Flat rounded box for text fields, choices and value inputs. */
Fl_Boxtype field_box();

/* Flat rounded push button; the primary variant is filled with the accent
 * colour and is used for the confirming action of dialogs and Apply. */
class ModernButton : public Fl_Button {
public:
    /* `accent` overrides the primary fill; 0 uses the palette accent. */
    ModernButton(int x, int y, int w, int h, const char *label, bool primary = false, Fl_Color accent = 0);
    void draw() override;
    int handle(int event) override;

private:
    bool primary_;
    bool hover_ = false;
    Fl_Color accent_;
};

/* Draw a glyph centred on (cx, cy) inside a `size` square. */
void draw_glyph(Glyph glyph, int cx, int cy, int size, Fl_Color color);

/* Larger illustrative icons. */
void draw_drive(int x, int y, int w, int h, Fl_Color color);
void draw_folder(int x, int y, int w, int h);
void draw_file(FileKind kind, int x, int y, int w, int h);
FileKind file_kind(const char *name, bool directory);

}  // namespace modern
