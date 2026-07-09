#pragma once
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <string>

namespace Gdiplus { class Bitmap; }   // forward-declared; freed in TabManager

// One browser tab == one WebView2 controller/webview hosting a website.
struct Tab {
    int id = 0;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2>           webview;

    std::wstring title = L"New Tab";
    std::wstring url;
    bool loading      = false;
    bool canGoBack    = false;
    bool canGoForward = false;
    bool ready        = false;   // controller finished async creation
    bool isNewTab     = true;    // showing the blank New Tab page (keeps address bar focused)

    // Pending navigation requested before the controller was ready.
    std::wstring pendingUrl;

    // Decoded favicon (PNG from WebView2), owned by the tab. Null until loaded.
    Gdiplus::Bitmap* favicon = nullptr;

    // Event registration tokens (so we can unhook on close).
    EventRegistrationToken navStarting{};
    EventRegistrationToken navCompleted{};
    EventRegistrationToken sourceChanged{};
    EventRegistrationToken titleChanged{};
    EventRegistrationToken historyChanged{};
    EventRegistrationToken newWindow{};
    EventRegistrationToken faviconChanged{};
};
