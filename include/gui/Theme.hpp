/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <FL/Fl.H>

/* Light/dark theming. FLTK has no notion of a system appearance (see
 * fltk/fltk issue #870), so the palette is resolved at run time and the OS
 * preference is read per platform by detect_system_theme(). */
namespace modern {

enum class Theme {
    System, /* follow the desktop setting */
    Light,
    Dark,
};

/* Every colour the UI draws goes through one of these slots, so switching the
 * theme only swaps the table they resolve to. */
enum class Palette : unsigned {
    kTitleBg,
    kTitleLine,
    kWindow,
    kPanel,
    kPanelAlt,
    kDivider,
    kDividerSoft,
    kDark,
    kDark2,
    kGray,
    kGray2,
    kAccent,
    kOnAccent,    /* text/glyphs drawn on top of kAccent */
    kZealPurple,
    kAccentSoft,
    kAccentSoftHover,
    kAccentEdge,
    kSelection,
    kHover,
    kHoverSoft,   /* lighter row hover, used by lists drawn by hand */
    kPressed,
    kButtonEdge,
    kTextDisabled,
    kScrollThumb,
    kMapBg,
    kRowAlt,
    kIconDark,
    kIconMuted,
    kWarning,
    kSuccess,
    kFolder,
    kCount,
};

/* Colour of a palette slot in the active theme. */
Fl_Color color(Palette slot);
/* Literal 0xRRGGBB, unaffected by the theme. */
Fl_Color color(unsigned rgb);

/* Theme in use (never System) and the preference the user picked. */
Theme theme();
Theme theme_preference();
bool is_dark();
/* "system", "light" or "dark", as stored in the preference file. */
const char *theme_name(Theme value);

/* Best effort OS preference; Light when it cannot be determined. */
Theme detect_system_theme();

/* Applies `preference` (resolving Theme::System through detect_system_theme()),
 * rewrites FLTK's global colours and redraws every open window. Widget colours
 * set explicitly by the caller have to be re-applied afterwards; the recursive
 * redraw done here only repaints. `persist` remembers the choice for the next
 * run. */
void set_theme(Theme preference, bool persist = true);
/* Applies the saved preference, falling back to Theme::System. */
void load_theme();

}  // namespace modern
