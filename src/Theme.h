#pragma once
#include <windows.h>
#include <string>

enum class ThemeMode { Light, Dark, System };

// Colour palette for the owner-drawn chrome. All values are COLORREF (0x00BBGGRR).
struct Palette {
    bool     dark;
    COLORREF windowBg;     // outer/content gap
    COLORREF chromeBg;     // toolbar + tab rail background
    COLORREF tabActiveBg;  // selected tab
    COLORREF tabHoverBg;   // hovered tab / button
    COLORREF text;         // primary text / glyphs
    COLORREF textDim;      // disabled glyphs, secondary text
    COLORREF accent;       // active-tab marker, focus
    COLORREF border;       // separators, address-bar outline
    COLORREF addrBg;       // address bar background
    COLORREF closeHover;   // close-glyph hover background
};

namespace Theme {
    ThemeMode   ParseMode(const std::string& name);   // "light"/"dark"/"system"
    const char* ModeName(ThemeMode mode);
    bool        SystemPrefersDark();                   // reads the OS apps-theme setting
    bool        Resolve(ThemeMode mode);               // -> true if effectively dark
    const Palette& Get(bool dark);
}
