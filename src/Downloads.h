#pragma once
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <vector>
#include <memory>
#include <string>

// Download manager: hooks each webview's DownloadStarting event and shows a
// small floating window with a live progress list.
class Downloads {
public:
    Downloads(HINSTANCE hInst, HWND owner);
    ~Downloads();   // out-of-line: Item is an incomplete type in this header

    void Attach(ICoreWebView2* webview);   // wire DownloadStarting on this webview
    void Show();                           // show + focus the downloads window
    void ApplyTheme(bool dark);

private:
    struct Item;

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void EnsureWindow();
    void OnDownloadStarting(ICoreWebView2DownloadStartingEventArgs* args);
    int  AddRow(const std::wstring& name);
    void UpdateRow(Item* it);

    HINSTANCE m_hInst;
    HWND m_owner = nullptr;
    HWND m_hwnd  = nullptr;
    HWND m_list  = nullptr;
    bool m_dark  = false;
    std::vector<std::unique_ptr<Item>> m_items;
};
