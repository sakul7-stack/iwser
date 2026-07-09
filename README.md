# iwser

A lightweight, native Windows web browser.

Built with **C++ / Win32** for the interface and **Microsoft WebView2** (the Edge engine already
on Windows) for web pages. Nothing is bundled, so the app is a single **~290 KB** `.exe`.

## Features

- Vertical tab rail with favicons — drag to resize or collapse to favicons only
- Bookmarks, persisted history, and a download manager
- **Chrome/Edge extensions** (unpacked) — WebView2 is Chromium, so standard extensions work
- Light and dark themes
- Back / forward / reload / home, and a JSON config file

## Requirements

- Windows 10 or 11 with the **Edge WebView2 Runtime** (preinstalled on current versions)
- To build: **Visual Studio 2022 Build Tools** (C++ workload), **CMake ≥ 3.21**, and **Ninja**

## Build

```
build.bat
```

Produces `build\iwser.exe`. The WebView2 SDK is downloaded automatically on the first build.

## Install

```
install.bat
```

Installs for the current user (no admin): copies to `%LOCALAPPDATA%\Programs\iwser`, adds
Start-menu and Desktop shortcuts, and lists it in Settings → Apps. Remove with `uninstall.bat`.

## Shortcuts

| Key | Action |
|-----|--------|
| `Ctrl+T` / `Ctrl+W` | new / close tab (middle-click also closes) |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | cycle tabs |
| `Ctrl+L` | focus address bar |
| `Ctrl+D` | bookmark page |
| `Ctrl+R` / `F5` | reload |
| `Alt+←` / `Alt+→` / `Alt+Home` | back / forward / home |

## Configuration

Settings live in `%APPDATA%\iwser\config.json`:

```json
{
  "theme": "light",
  "homepage": "https://www.google.com",
  "searchEngine": "https://www.google.com/search?q=%s",
  "railWidth": 240,
  "downloadDir": ""
}
```

`theme` is `light`, `dark`, or `system`. Bookmarks and history are saved next to it.

## Extensions

iwser runs standard **unpacked** Chrome/Edge extensions (a folder containing `manifest.json`).

1. Open the **⋮ menu → Extensions → Load extension…**
2. Pick the extension's folder.

To try it now, load `examples/sample-extension` — it shows a small banner on each page.
Loaded extensions persist across restarts; enable/disable or remove them from the same menu.
(Packed `.crx` files from the Chrome Web Store must be unzipped to a folder first.)

## App icon

The icon is generated from `assets/logo.png` during the build. Replace that file and rebuild
to change it.

## Contributors

- [Quackzy7](https://github.com/Quackzy7)
- [JENIFKHADKA](https://github.com/JENIFKHADKA)
