#include "Theme.h"

namespace {

// Clean, flat light theme (white default) and a comfortable dark theme.
const Palette kLight {
    /*dark*/        false,
    /*windowBg*/    RGB(255, 255, 255),
    /*chromeBg*/    RGB(245, 246, 248),
    /*tabActiveBg*/ RGB(255, 255, 255),
    /*tabHoverBg*/  RGB(232, 234, 238),
    /*text*/        RGB(32, 33, 36),
    /*textDim*/     RGB(154, 160, 166),
    /*accent*/      RGB(26, 115, 232),
    /*border*/      RGB(223, 225, 229),
    /*addrBg*/      RGB(255, 255, 255),
    /*closeHover*/  RGB(214, 217, 222),
};

const Palette kDark {
    /*dark*/        true,
    /*windowBg*/    RGB(32, 33, 36),
    /*chromeBg*/    RGB(41, 42, 45),
    /*tabActiveBg*/ RGB(60, 64, 67),
    /*tabHoverBg*/  RGB(52, 53, 56),
    /*text*/        RGB(232, 234, 237),
    /*textDim*/     RGB(129, 132, 137),
    /*accent*/      RGB(138, 180, 248),
    /*border*/      RGB(60, 64, 67),
    /*addrBg*/      RGB(48, 49, 52),
    /*closeHover*/  RGB(80, 82, 86),
};

} // namespace

namespace Theme {

ThemeMode ParseMode(const std::string& name) {
    if (name == "dark")   return ThemeMode::Dark;
    if (name == "system") return ThemeMode::System;
    return ThemeMode::Light;
}

const char* ModeName(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Dark:   return "dark";
        case ThemeMode::System: return "system";
        default:                return "light";
    }
}

bool SystemPrefersDark() {
    // HKCU\...\Themes\Personalize\AppsUseLightTheme == 0 means dark.
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD value = 1, size = sizeof(value), type = 0;
    bool dark = false;
    if (RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
            reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS && type == REG_DWORD) {
        dark = (value == 0);
    }
    RegCloseKey(key);
    return dark;
}

bool Resolve(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Dark:   return true;
        case ThemeMode::Light:  return false;
        default:                return SystemPrefersDark();
    }
}

const Palette& Get(bool dark) {
    return dark ? kDark : kLight;
}

} // namespace Theme
