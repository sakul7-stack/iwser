#include "TabManager.h"
#include "Storage.h"
#include "Util.h"
#include "resource.h"
#include "GdiPlusInc.h"

#include <algorithm>

using namespace Microsoft::WRL;

// Sentinel meaning "open the local New Tab landing page" rather than a real URL.
static const wchar_t* kNewTabUrl = L"iwser:newtab";

TabManager::TabManager(ITabHost* host, ICoreWebView2Environment* env)
    : m_host(host), m_env(env) {}

Tab* TabManager::FindTab(int id) {
    for (auto& t : m_tabs)
        if (t->id == id) return t.get();
    return nullptr;
}

Tab* TabManager::Active() {
    return FindTab(m_activeId);
}

// ---------------------------------------------------------------------------
// Creation
// ---------------------------------------------------------------------------
void TabManager::NewTab(const std::wstring& url, bool activate) {
    auto tab = std::make_unique<Tab>();
    tab->id = m_nextId++;
    // Empty URL -> local New Tab page (not the search-engine homepage).
    tab->pendingUrl = url.empty() ? kNewTabUrl : url;
    tab->isNewTab   = url.empty();
    const int id = tab->id;
    m_tabs.push_back(std::move(tab));
    m_host->RefreshChrome();

    m_env->CreateCoreWebView2Controller(
        m_host->HostHwnd(),
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [this, id, activate](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
                Tab* tab = FindTab(id);
                if (!tab) return S_OK;                    // tab closed before it was ready
                if (FAILED(hr) || !controller) return S_OK;
                FinishController(tab, controller, activate);
                return S_OK;
            }).Get());
}

void TabManager::FinishController(Tab* tab, ICoreWebView2Controller* controller, bool activate) {
    tab->controller = controller;
    tab->controller->get_CoreWebView2(&tab->webview);
    tab->ready = true;

    RECT rc = m_host->ContentRect();
    tab->controller->put_Bounds(rc);
    tab->controller->put_IsVisible(m_activeId == tab->id || m_activeId == -1);

    WireEvents(tab);
    m_host->AttachDownloads(tab->webview.Get());
    m_host->OnWebViewCreated(tab->webview.Get());
    ApplyColorScheme(m_dark);  // apply current scheme to the newly created tab

    std::wstring go = tab->pendingUrl;
    tab->pendingUrl.clear();
    if (go.empty() || go == kNewTabUrl)
        tab->webview->NavigateToString(m_host->NewTabHtml().c_str());
    else
        tab->webview->Navigate(go.c_str());

    if (activate || m_activeId == -1) SwitchTo(tab->id);
    m_host->RefreshChrome();
}

void TabManager::WireEvents(Tab* tab) {
    ICoreWebView2* wv = tab->webview.Get();
    const int id = tab->id;

    wv->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this, id](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*) -> HRESULT {
                if (Tab* t = FindTab(id)) { t->loading = true; m_host->RefreshChrome(); }
                return S_OK;
            }).Get(), &tab->navStarting);

    wv->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this, id](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                Tab* t = FindTab(id);
                if (!t) return S_OK;
                t->loading = false;

                BOOL ok = FALSE;
                args->get_IsSuccess(&ok);
                if (ok) {
                    LPWSTR src = nullptr, title = nullptr;
                    sender->get_Source(&src);
                    sender->get_DocumentTitle(&title);
                    std::string u = util::Narrow(src);
                    std::string ttl = util::Narrow(title);
                    if (src) CoTaskMemFree(src);
                    if (title) CoTaskMemFree(title);
                    // Skip internal/blank pages.
                    if (u.rfind("http", 0) == 0) {
                        Storage::Instance().AddHistory(u, ttl);
                    }
                }
                m_host->RefreshChrome();
                return S_OK;
            }).Get(), &tab->navCompleted);

    wv->add_SourceChanged(
        Callback<ICoreWebView2SourceChangedEventHandler>(
            [this, id](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                Tab* t = FindTab(id);
                if (!t) return S_OK;
                LPWSTR src = nullptr;
                sender->get_Source(&src);
                t->url = src ? src : L"";
                if (src) CoTaskMemFree(src);
                // Once it navigates somewhere real, it's no longer the blank page.
                if (!t->url.empty() && t->url.rfind(L"about:", 0) != 0) t->isNewTab = false;
                m_host->RefreshChrome();
                return S_OK;
            }).Get(), &tab->sourceChanged);

    wv->add_DocumentTitleChanged(
        Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [this, id](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                Tab* t = FindTab(id);
                if (!t) return S_OK;
                LPWSTR title = nullptr;
                sender->get_DocumentTitle(&title);
                t->title = (title && *title) ? title : L"New Tab";
                if (title) CoTaskMemFree(title);
                m_host->RefreshChrome();
                return S_OK;
            }).Get(), &tab->titleChanged);

    wv->add_HistoryChanged(
        Callback<ICoreWebView2HistoryChangedEventHandler>(
            [this, id](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                Tab* t = FindTab(id);
                if (!t) return S_OK;
                BOOL b = FALSE, f = FALSE;
                sender->get_CanGoBack(&b);
                sender->get_CanGoForward(&f);
                t->canGoBack = !!b;
                t->canGoForward = !!f;
                m_host->RefreshChrome();
                return S_OK;
            }).Get(), &tab->historyChanged);

    wv->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                std::wstring u = uri ? uri : L"";
                if (uri) CoTaskMemFree(uri);
                args->put_Handled(TRUE);
                m_host->OnNewWindow(u);
                return S_OK;
            }).Get(), &tab->newWindow);

    // Favicon: fetch as PNG and decode into a GDI+ bitmap for the tab rail.
    ComPtr<ICoreWebView2_15> wv15;
    if (SUCCEEDED(wv->QueryInterface(IID_PPV_ARGS(&wv15))) && wv15) {
        wv15->add_FaviconChanged(
            Callback<ICoreWebView2FaviconChangedEventHandler>(
                [this, id](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                    ComPtr<ICoreWebView2_15> s15;
                    if (FAILED(sender->QueryInterface(IID_PPV_ARGS(&s15))) || !s15) return S_OK;
                    s15->GetFavicon(COREWEBVIEW2_FAVICON_IMAGE_FORMAT_PNG,
                        Callback<ICoreWebView2GetFaviconCompletedHandler>(
                            [this, id](HRESULT ec, IStream* stream) -> HRESULT {
                                if (FAILED(ec) || !stream) return S_OK;
                                Tab* t = FindTab(id);
                                if (!t) return S_OK;
                                auto* bmp = Gdiplus::Bitmap::FromStream(stream);
                                if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
                                    delete t->favicon;
                                    t->favicon = bmp;
                                    m_host->RefreshChrome();
                                } else {
                                    delete bmp;
                                }
                                return S_OK;
                            }).Get());
                    return S_OK;
                }).Get(), &tab->faviconChanged);
    }

    // Route common browser shortcuts even when focus is inside the web content.
    EventRegistrationToken accelTok{};
    tab->controller->add_AcceleratorKeyPressed(
        Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
            [this](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                COREWEBVIEW2_KEY_EVENT_KIND kind;
                args->get_KeyEventKind(&kind);
                if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN &&
                    kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)
                    return S_OK;
                UINT vk = 0;
                args->get_VirtualKey(&vk);
                bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
                int cmd = 0;
                if (ctrl && shift && vk == 'T') cmd = IDM_REOPEN_TAB;
                else if (ctrl && vk == 'T') cmd = IDM_NEWTAB;
                else if (ctrl && vk == 'W') cmd = IDM_CLOSETAB;
                else if (ctrl && vk == 'L') cmd = IDM_FOCUS_ADDRESS;
                else if (ctrl && vk == 'R') cmd = IDM_RELOAD;
                else if (ctrl && vk == 'B') cmd = IDM_TOGGLE_RAIL;
                else if (ctrl && (vk == VK_OEM_PLUS  || vk == VK_ADD))      cmd = IDM_ZOOM_IN;
                else if (ctrl && (vk == VK_OEM_MINUS || vk == VK_SUBTRACT)) cmd = IDM_ZOOM_OUT;
                else if (ctrl && (vk == '0' || vk == VK_NUMPAD0))           cmd = IDM_ZOOM_RESET;
                else if (ctrl && vk >= '1' && vk <= '9') cmd = IDM_TAB_BASE + (vk - '1');
                if (cmd) {
                    args->put_Handled(TRUE);
                    m_host->OnAcceleratorCommand(cmd);
                }
                return S_OK;
            }).Get(), &accelTok);
}

// ---------------------------------------------------------------------------
// Switching / closing
// ---------------------------------------------------------------------------
void TabManager::ShowOnly(int id) {
    for (auto& t : m_tabs) {
        if (t->controller)
            t->controller->put_IsVisible(t->id == id ? TRUE : FALSE);
    }
}

void TabManager::SwitchTo(int id) {
    Tab* t = FindTab(id);
    if (!t) return;
    m_activeId = id;
    ShowOnly(id);
    if (t->controller) {
        t->controller->put_Bounds(m_host->ContentRect());
        // Don't grab focus into the web view for the blank New Tab page, so the
        // address bar stays focused and ready to type (on launch and on Ctrl+T).
        if (!t->isNewTab)
            t->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }
    m_host->RefreshChrome();
}

void TabManager::NextTab() {
    if (m_tabs.size() < 2) return;
    int idx = 0;
    for (size_t i = 0; i < m_tabs.size(); ++i) if (m_tabs[i]->id == m_activeId) { idx = (int)i; break; }
    SwitchTo(m_tabs[(idx + 1) % m_tabs.size()]->id);
}

void TabManager::PrevTab() {
    if (m_tabs.size() < 2) return;
    int idx = 0;
    for (size_t i = 0; i < m_tabs.size(); ++i) if (m_tabs[i]->id == m_activeId) { idx = (int)i; break; }
    SwitchTo(m_tabs[(idx + (int)m_tabs.size() - 1) % m_tabs.size()]->id);
}

void TabManager::DuplicateTab(int id) {
    Tab* t = FindTab(id);
    if (!t) return;
    std::wstring url = t->url.empty() ? t->pendingUrl : t->url;
    NewTab(url, true);
}

void TabManager::CloseOthers(int id) {
    if (!FindTab(id)) return;
    SwitchTo(id);   // make the survivor active first
    std::vector<int> victims;
    for (auto& t : m_tabs) if (t->id != id) victims.push_back(t->id);
    for (int v : victims) CloseTab(v);
}

void TabManager::ReopenClosed() {
    if (m_closedUrls.empty()) return;
    std::wstring url = m_closedUrls.back();
    m_closedUrls.pop_back();
    NewTab(url, true);
}

void TabManager::SwitchToIndex(int index) {
    if (m_tabs.empty()) return;
    if (index < 0) index = 0;
    if (index >= (int)m_tabs.size()) index = (int)m_tabs.size() - 1;
    SwitchTo(m_tabs[index]->id);
}

void TabManager::ZoomBy(double delta) {
    Tab* t = Active();
    if (!t || !t->controller) return;
    double z = 1.0;
    t->controller->get_ZoomFactor(&z);
    z = std::max(0.25, std::min(5.0, z + delta));
    t->controller->put_ZoomFactor(z);
}

void TabManager::ZoomIn()    { ZoomBy(+0.1); }
void TabManager::ZoomOut()   { ZoomBy(-0.1); }
void TabManager::ZoomReset() {
    if (Tab* t = Active()) if (t->controller) t->controller->put_ZoomFactor(1.0);
}

void TabManager::CloseTab(int id) {
    int idx = -1;
    for (size_t i = 0; i < m_tabs.size(); ++i)
        if (m_tabs[i]->id == id) { idx = (int)i; break; }
    if (idx < 0) return;

    // Remember the URL so it can be reopened with Ctrl+Shift+T.
    std::wstring closedUrl = m_tabs[idx]->url.empty()
                                 ? m_tabs[idx]->pendingUrl : m_tabs[idx]->url;
    if (!closedUrl.empty() && closedUrl.rfind(L"http", 0) == 0) {
        m_closedUrls.push_back(closedUrl);
        if (m_closedUrls.size() > 25) m_closedUrls.erase(m_closedUrls.begin());
    }

    if (m_tabs[idx]->controller)
        m_tabs[idx]->controller->Close();
    delete m_tabs[idx]->favicon;
    m_tabs[idx]->favicon = nullptr;
    m_tabs.erase(m_tabs.begin() + idx);

    if (m_tabs.empty()) {
        PostMessageW(m_host->HostHwnd(), WM_CLOSE, 0, 0);
        return;
    }
    if (m_activeId == id) {
        int newIdx = idx < (int)m_tabs.size() ? idx : (int)m_tabs.size() - 1;
        SwitchTo(m_tabs[newIdx]->id);
    } else {
        m_host->RefreshChrome();
    }
}

// ---------------------------------------------------------------------------
// Active-tab actions
// ---------------------------------------------------------------------------
std::wstring TabManager::ResolveInput(const std::wstring& text) const {
    std::wstring s = text;
    size_t a = s.find_first_not_of(L" \t");
    size_t b = s.find_last_not_of(L" \t");
    if (a == std::wstring::npos) return util::Widen(Storage::Instance().GetConfig().homepage);
    s = s.substr(a, b - a + 1);

    if (s.find(L"://") != std::wstring::npos) return s;

    bool hasSpace = s.find(L' ') != std::wstring::npos;
    bool hasDot   = s.find(L'.') != std::wstring::npos;
    bool isLocal  = (s == L"localhost") || (s.rfind(L"localhost:", 0) == 0);
    if (!hasSpace && (hasDot || isLocal)) {
        return L"https://" + s;
    }
    // Treat as a search query.
    std::string engine = Storage::Instance().GetConfig().searchEngine;
    std::string q = util::UrlEncode(util::Narrow(s));
    size_t pos = engine.find("%s");
    if (pos != std::string::npos) engine.replace(pos, 2, q);
    else engine += q;
    return util::Widen(engine);
}

void TabManager::Navigate(const std::wstring& text) {
    Tab* t = Active();
    std::wstring url = ResolveInput(text);
    if (t && t->ready && t->webview) t->webview->Navigate(url.c_str());
    else if (t) t->pendingUrl = url;
}

void TabManager::Back()    { if (Tab* t = Active()) if (t->webview) t->webview->GoBack(); }
void TabManager::Forward() { if (Tab* t = Active()) if (t->webview) t->webview->GoForward(); }
void TabManager::Reload()  { if (Tab* t = Active()) if (t->webview) t->webview->Reload(); }

void TabManager::ResizeActive() {
    if (Tab* t = Active())
        if (t->controller) t->controller->put_Bounds(m_host->ContentRect());
}

void TabManager::ApplyColorScheme(bool dark) {
    m_dark = dark;
    auto scheme = dark ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK
                       : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT;
    for (auto& t : m_tabs) {
        if (!t->webview) continue;
        ComPtr<ICoreWebView2_13> wv13;
        if (SUCCEEDED(t->webview.As(&wv13)) && wv13) {
            ComPtr<ICoreWebView2Profile> profile;
            if (SUCCEEDED(wv13->get_Profile(&profile)) && profile)
                profile->put_PreferredColorScheme(scheme);
        }
    }
}

void TabManager::NavigateActiveToString(const std::wstring& html) {
    if (Tab* t = Active())
        if (t->webview) t->webview->NavigateToString(html.c_str());
}
