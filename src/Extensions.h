#pragma once
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <vector>
#include <string>

// Manages unpacked browser extensions on the WebView2 profile (all tabs share it).
// WebView2 is Chromium, so these are standard Chrome/Edge extensions (a folder
// containing manifest.json). Added extensions persist across sessions.
class Extensions {
public:
    struct Item {
        Microsoft::WRL::ComPtr<ICoreWebView2BrowserExtension> ext;
        std::wstring name;
        bool enabled = true;
    };

    void Init(ICoreWebView2* anyWebView);   // capture the profile, then Refresh()
    bool Ready() const { return m_profile != nullptr; }

    void LoadFromFolder(HWND owner, const std::wstring& folder);
    void Remove(size_t index);
    void ToggleEnabled(size_t index);

    const std::vector<Item>& List() const { return m_items; }

private:
    void Refresh();

    Microsoft::WRL::ComPtr<ICoreWebView2Profile7> m_profile;
    std::vector<Item> m_items;
};
