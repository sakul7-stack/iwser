#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <string>

#include "GdiPlusInc.h"
#include "BrowserWindow.h"
#include "Storage.h"

// Strip surrounding quotes/whitespace from a command-line argument.
static std::wstring CleanArg(PWSTR cmd) {
    if (!cmd) return L"";
    std::wstring s = cmd;
    size_t a = s.find_first_not_of(L" \t\"");
    size_t b = s.find_last_not_of(L" \t\"");
    if (a == std::wstring::npos) return L"";
    return s.substr(a, b - a + 1);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    // WebView2 and OLE (download drag/drop, dialogs) need an initialised STA.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // GDI+ for decoding/drawing favicons.
    Gdiplus::GdiplusStartupInput gdipInput;
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, nullptr);

    INITCOMMONCONTROLSEX icc{ sizeof(icc) };
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    // Load persisted config/bookmarks/history from %APPDATA%\iwser.
    Storage::Instance().Load();

    // Open the URL passed on the command line (e.g. from a shortcut or as the
    // default browser), otherwise fall back to the configured homepage.
    std::wstring initialUrl = CleanArg(pCmdLine);

    BrowserWindow window;
    if (!window.Create(hInstance, nCmdShow, initialUrl)) {
        CoUninitialize();
        return 1;
    }

    HACCEL accel = window.Accel();
    HWND hwnd = window.Hwnd();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (accel && TranslateAcceleratorW(hwnd, accel, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Gdiplus::GdiplusShutdown(gdipToken);
    CoUninitialize();
    return (int)msg.wParam;
}
