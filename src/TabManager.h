#pragma once
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <memory>
#include <vector>
#include <string>

#include "Tab.h"

// Callbacks the tab layer needs from the host window. Implemented by BrowserWindow.
class ITabHost {
public:
    virtual ~ITabHost() = default;
    virtual RECT ContentRect() = 0;                       // where to place the active webview
    virtual HWND HostHwnd() = 0;
    virtual void RefreshChrome() = 0;                     // repaint rail/toolbar + sync address bar
    virtual void OnNewWindow(const std::wstring& url) = 0; // popup / target=_blank -> new tab
    virtual void AttachDownloads(ICoreWebView2* wv) = 0;   // wire the download manager to a webview
    virtual void OnWebViewCreated(ICoreWebView2* wv) = 0;  // first webview -> init extensions
    virtual void OnAcceleratorCommand(int commandId) = 0;  // Ctrl+T/W/L/R from within web content
    virtual std::wstring NewTabHtml() = 0;                 // local landing page for a blank new tab
};

class TabManager {
public:
    TabManager(ITabHost* host, ICoreWebView2Environment* env);

    void NewTab(const std::wstring& url, bool activate);
    void CloseTab(int id);
    void SwitchTo(int id);
    void NextTab();
    void PrevTab();

    // Extra tab operations.
    void DuplicateTab(int id);        // open a copy of tab `id` (its current URL)
    void CloseOthers(int id);         // close every tab except `id`
    void ReopenClosed();              // restore the most recently closed tab
    void SwitchToIndex(int index);    // 0-based; clamps to the last tab
    bool HasClosed() const { return !m_closedUrls.empty(); }

    // Zoom on the active tab.
    void ZoomIn();
    void ZoomOut();
    void ZoomReset();

    Tab* Active();
    const std::vector<std::unique_ptr<Tab>>& Tabs() const { return m_tabs; }
    int ActiveId() const { return m_activeId; }
    size_t Count() const { return m_tabs.size(); }

    // Actions on the active tab.
    void Navigate(const std::wstring& text);   // URL or search query from the address bar
    void Back();
    void Forward();
    void Reload();
    void ResizeActive();                       // re-apply bounds from host->ContentRect()
    void ApplyColorScheme(bool dark);          // push preferred colour scheme to all tabs
    void NavigateActiveToString(const std::wstring& html);

private:
    Tab* FindTab(int id);
    void FinishController(Tab* tab, ICoreWebView2Controller* controller, bool activate);
    void WireEvents(Tab* tab);
    void ShowOnly(int id);
    void ZoomBy(double delta);
    std::wstring ResolveInput(const std::wstring& text) const;

    ITabHost* m_host;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> m_env;
    std::vector<std::unique_ptr<Tab>> m_tabs;
    std::vector<std::wstring> m_closedUrls;   // recently closed tab URLs (reopen stack)
    int  m_activeId = -1;
    int  m_nextId   = 1;
    bool m_dark     = false;   // current preferred colour scheme, applied to new tabs
};
