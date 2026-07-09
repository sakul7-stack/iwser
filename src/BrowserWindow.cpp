#include "BrowserWindow.h"
#include "Storage.h"
#include "Util.h"
#include "resource.h"
#include "GdiPlusInc.h"

#include <commctrl.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shldisp.h>
#include <WebView2EnvironmentOptions.h>
#include <ctime>
#include <string>
#include <algorithm>
#include <unordered_set>

using namespace Microsoft::WRL;

namespace {
const wchar_t* kClass = L"iwserMain";

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Segoe MDL2 Assets glyphs.
const wchar_t GL_BACK    = 0xE72B;
const wchar_t GL_FWD     = 0xE72A;
const wchar_t GL_RELOAD  = 0xE72C;
const wchar_t GL_ADD     = 0xE710;
const wchar_t GL_CLOSE   = 0xE711;
const wchar_t GL_STAR    = 0xE734;
const wchar_t GL_STARF   = 0xE735;
const wchar_t GL_MORE    = 0xE712;
const wchar_t GL_GLOBE   = 0xE774;
const wchar_t GL_HOME    = 0xE80F;
const wchar_t GL_CHEVL   = 0xE76B;   // chevron left  (collapse)
const wchar_t GL_CHEVR   = 0xE76C;   // chevron right (expand)

// IEnumString that supplies address-bar suggestions from browsing history and
// bookmarks. Each Reset() re-snapshots Storage, so newly visited pages appear
// without rebuilding the autocomplete object. Handed to Win32 IAutoComplete.
class SuggestSource : public IEnumString {
public:
    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IEnumString) {
            *ppv = static_cast<IEnumString*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    IFACEMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return r;
    }

    // IEnumString
    IFACEMETHODIMP Next(ULONG celt, LPOLESTR* rgelt, ULONG* pceltFetched) override {
        ULONG got = 0;
        while (got < celt && m_idx < m_items.size()) {
            const std::wstring& s = m_items[m_idx++];
            size_t bytes = (s.size() + 1) * sizeof(wchar_t);
            auto* mem = static_cast<LPOLESTR>(CoTaskMemAlloc(bytes));
            if (!mem) break;
            memcpy(mem, s.c_str(), bytes);
            rgelt[got++] = mem;
        }
        if (pceltFetched) *pceltFetched = got;
        return got == celt ? S_OK : S_FALSE;
    }
    IFACEMETHODIMP Skip(ULONG celt) override {
        m_idx += celt;
        return m_idx <= m_items.size() ? S_OK : S_FALSE;
    }
    IFACEMETHODIMP Reset() override { Rebuild(); m_idx = 0; return S_OK; }
    IFACEMETHODIMP Clone(IEnumString**) override { return E_NOTIMPL; }

private:
    // Strip scheme (and a leading "www.") so typing "goo" matches "google.com".
    static std::wstring Pretty(const std::wstring& url) {
        std::wstring s = url;
        for (const wchar_t* p : { L"https://", L"http://" })
            if (s.rfind(p, 0) == 0) { s.erase(0, wcslen(p)); break; }
        if (s.rfind(L"www.", 0) == 0) s.erase(0, 4);
        return s;
    }
    void Rebuild() {
        m_items.clear();
        std::unordered_set<std::wstring> seen;
        auto push = [&](const std::wstring& s) {
            if (!s.empty() && seen.insert(s).second) m_items.push_back(s);
        };
        auto add = [&](const std::string& u) {
            if (u.empty()) return;
            std::wstring w = util::Widen(u);
            push(Pretty(w));   // domain-first: typing "goo" matches "google.com/…"
            push(w);           // full URL: typing "https://" still matches
        };
        const auto& hist = Storage::Instance().History();   // newest first
        for (const auto& h : hist) { add(h.url); if (m_items.size() > 2000) break; }
        for (const auto& b : Storage::Instance().Bookmarks()) add(b.url);
        // Sorted so the auto-suggest drop-down and inline auto-append behave well.
        std::sort(m_items.begin(), m_items.end());
    }

    LONG m_ref = 1;
    std::vector<std::wstring> m_items;
    size_t m_idx = 0;
};

// Unscaled layout metrics (device-independent pixels).
enum {
    TOOLBAR_H = 48, TAB_H = 38, NEWTAB_H = 40, BTN = 36, ADDR_H = 32,
    RAIL_MIN = 48,          // favicon-only width
    RAIL_MAX = 460,
    RAIL_COLLAPSE = 116,    // below this -> favicon-only mode
    SPLITTER = 6,           // draggable strip on the rail's right edge
    TOGGLE_H = 36,          // rail collapse-toggle row at the bottom
    FAV = 18                // favicon square
};
}

static std::wstring PickFolder(HWND owner, const wchar_t* title);   // defined below

// ===========================================================================
// Creation / setup
// ===========================================================================
bool BrowserWindow::Create(HINSTANCE hInst, int nCmdShow, const std::wstring& initialUrl) {
    m_hInst = hInst;
    m_initialUrl = initialUrl;
    // Auto-focus the search bar on launch only when we're opening the blank New
    // Tab page (not when a URL was passed on the command line).
    m_focusAddrPending = initialUrl.empty();

    // Resolve theme from persisted config before creating the window.
    Config& cfg = Storage::Instance().GetConfig();
    m_mode = Theme::ParseMode(cfg.theme);
    m_dark = Theme::Resolve(m_mode);
    m_pal  = Theme::Get(m_dark);

    m_railDip = std::max((int)RAIL_MIN, std::min((int)RAIL_MAX, cfg.railWidth));
    m_railExpandedDip = m_railDip < RAIL_COLLAPSE ? 240 : m_railDip;

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &BrowserWindow::WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // we paint everything
    wc.lpszClassName = kClass;
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm       = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        0, kClass, L"iwser",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 720,
        nullptr, nullptr, hInst, this);
    if (!m_hwnd) return false;

    m_dpi = GetDpiForWindow(m_hwnd);
    if (!m_dpi) m_dpi = 96;
    CreateFonts();

    m_addrEdit = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
        0, 0, 10, 10, m_hwnd, (HMENU)100, hInst, nullptr);
    SendMessageW(m_addrEdit, WM_SETFONT, (WPARAM)m_uiFont, TRUE);
    SendMessageW(m_addrEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search or enter address");
    SetWindowSubclass(m_addrEdit, &BrowserWindow::EditProc, 1, (DWORD_PTR)this);
    InitAddressAutoComplete();

    m_addrBrush = CreateSolidBrush(m_pal.addrBg);
    m_downloads = std::make_unique<Downloads>(hInst, m_hwnd);
    m_extensions = std::make_unique<Extensions>();

    // Keyboard shortcuts (also work while focus is inside web content, see
    // TabManager's AcceleratorKeyPressed hook).
    ACCEL accels[] = {
        { FCONTROL | FVIRTKEY, 'T', IDM_NEWTAB },
        { FCONTROL | FVIRTKEY, 'W', IDM_CLOSETAB },
        { FCONTROL | FVIRTKEY, 'L', IDM_FOCUS_ADDRESS },
        { FCONTROL | FVIRTKEY, 'R', IDM_RELOAD },
        { FCONTROL | FVIRTKEY, 'D', IDM_TOGGLE_BOOKMARK },
        { FCONTROL | FVIRTKEY, 'B', IDM_TOGGLE_RAIL },
        { FVIRTKEY,            VK_F5, IDM_RELOAD },
        { FALT | FVIRTKEY,     VK_LEFT, IDM_BACK },
        { FALT | FVIRTKEY,     VK_RIGHT, IDM_FORWARD },
        { FALT | FVIRTKEY,     VK_HOME, IDM_HOME },
        { FCONTROL | FVIRTKEY, VK_TAB, IDM_NEXT_TAB },
        { FCONTROL | FSHIFT | FVIRTKEY, VK_TAB, IDM_PREV_TAB },
        { FCONTROL | FSHIFT | FVIRTKEY, 'T', IDM_REOPEN_TAB },
        // Zoom.
        { FCONTROL | FVIRTKEY, VK_OEM_PLUS,  IDM_ZOOM_IN },
        { FCONTROL | FVIRTKEY, VK_ADD,       IDM_ZOOM_IN },
        { FCONTROL | FVIRTKEY, VK_OEM_MINUS, IDM_ZOOM_OUT },
        { FCONTROL | FVIRTKEY, VK_SUBTRACT,  IDM_ZOOM_OUT },
        { FCONTROL | FVIRTKEY, '0', IDM_ZOOM_RESET },
        { FCONTROL | FVIRTKEY, VK_NUMPAD0, IDM_ZOOM_RESET },
        // Ctrl+1 .. Ctrl+9 -> jump to tab by position.
        { FCONTROL | FVIRTKEY, '1', IDM_TAB_BASE + 0 },
        { FCONTROL | FVIRTKEY, '2', IDM_TAB_BASE + 1 },
        { FCONTROL | FVIRTKEY, '3', IDM_TAB_BASE + 2 },
        { FCONTROL | FVIRTKEY, '4', IDM_TAB_BASE + 3 },
        { FCONTROL | FVIRTKEY, '5', IDM_TAB_BASE + 4 },
        { FCONTROL | FVIRTKEY, '6', IDM_TAB_BASE + 5 },
        { FCONTROL | FVIRTKEY, '7', IDM_TAB_BASE + 6 },
        { FCONTROL | FVIRTKEY, '8', IDM_TAB_BASE + 7 },
        { FCONTROL | FVIRTKEY, '9', IDM_TAB_BASE + 8 },
    };
    m_accel = CreateAcceleratorTableW(accels, ARRAYSIZE(accels));

    ApplyTheme();
    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    Layout();
    InitEnvironment();
    return true;
}

void BrowserWindow::CreateFonts() {
    if (m_uiFont) DeleteObject(m_uiFont);
    if (m_iconFont) DeleteObject(m_iconFont);

    LOGFONTW lf{};
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfHeight  = -Scale(15);
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    m_uiFont = CreateFontIndirectW(&lf);

    LOGFONTW lfi{};
    lfi.lfQuality = CLEARTYPE_QUALITY;
    lfi.lfHeight  = -Scale(16);
    wcscpy_s(lfi.lfFaceName, L"Segoe MDL2 Assets");
    m_iconFont = CreateFontIndirectW(&lfi);

    if (m_addrEdit) SendMessageW(m_addrEdit, WM_SETFONT, (WPARAM)m_uiFont, TRUE);
}

void BrowserWindow::InitEnvironment() {
    std::wstring udf = Storage::Instance().WebViewUserDataDir();

    // Enable browser extension support on the environment.
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions6> opt6;
    if (SUCCEEDED(options.As(&opt6)) && opt6)
        opt6->put_AreBrowserExtensionsEnabled(TRUE);

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, udf.c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr) || !env) {
                    MessageBoxW(m_hwnd,
                        L"Failed to initialize WebView2.\n\nPlease install the "
                        L"Microsoft Edge WebView2 Runtime and try again.",
                        L"iwser", MB_ICONERROR);
                    return S_OK;
                }
                m_env = env;
                m_tabs = std::make_unique<TabManager>(this, m_env.Get());
                m_tabs->ApplyColorScheme(m_dark);
                m_tabs->NewTab(m_initialUrl, true);
                Layout();
                return S_OK;
            }).Get());

    if (FAILED(hr)) {
        MessageBoxW(m_hwnd,
            L"Could not create the WebView2 environment.\n\nInstall the "
            L"Microsoft Edge WebView2 Runtime from Microsoft and relaunch.",
            L"iwser", MB_ICONERROR);
    }
}

void BrowserWindow::InitAddressAutoComplete() {
    if (!m_addrEdit) return;
    IAutoComplete* ac = nullptr;
    if (FAILED(CoCreateInstance(CLSID_AutoComplete, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IAutoComplete, reinterpret_cast<void**>(&ac))) || !ac)
        return;

    // SuggestSource is ref-counted; Init() takes its own reference, so we drop
    // ours and let the autocomplete own it for the window's lifetime.
    SuggestSource* src = new SuggestSource();
    ac->Init(m_addrEdit, static_cast<IEnumString*>(src), nullptr, nullptr);
    src->Release();

    IAutoComplete2* ac2 = nullptr;
    if (SUCCEEDED(ac->QueryInterface(IID_IAutoComplete2, reinterpret_cast<void**>(&ac2))) && ac2) {
        ac2->SetOptions(ACO_AUTOSUGGEST | ACO_AUTOAPPEND | ACO_UPDOWNKEYDROPSLIST);
        ac2->Release();
    }
    m_autoComplete = ac;   // kept alive for the window's lifetime
}

std::wstring BrowserWindow::NewTabHtml() {
    const bool d = m_dark;
    std::string bg   = d ? "#202124" : "#ffffff";
    std::string fg   = d ? "#e8eaed" : "#202124";
    std::string dim  = d ? "#9aa0a6" : "#5f6368";
    std::string card = d ? "#2a2b2e" : "#f1f3f4";
    std::string link = d ? "#8ab4f8" : "#1a73e8";

    std::string html =
        "<!doctype html><html><head><meta charset='utf-8'><title>New Tab</title><style>"
        "html,body{height:100%;margin:0;}"
        "body{font-family:'Segoe UI',system-ui,sans-serif;background:" + bg + ";color:" + fg + ";"
        "display:flex;flex-direction:column;align-items:center;justify-content:center;}"
        "h1{font-size:44px;font-weight:600;letter-spacing:-1px;margin:0 0 6px;}"
        ".sub{color:" + dim + ";font-size:14px;margin-bottom:34px;}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));"
        "gap:12px;width:min(760px,86vw);}"
        ".tile{background:" + card + ";border-radius:12px;padding:14px 16px;text-decoration:none;"
        "color:" + fg + ";overflow:hidden;transition:transform .08s;}"
        ".tile:hover{transform:translateY(-2px);}"
        ".tt{font-size:14px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}"
        ".tu{font-size:12px;color:" + dim + ";white-space:nowrap;overflow:hidden;text-overflow:ellipsis;margin-top:3px;}"
        ".empty{color:" + dim + ";font-size:14px;}"
        "</style></head><body>"
        "<h1>iwser</h1><div class='sub'>Type a URL or search above to get started</div>";

    // Build "top sites" from most-recent history (deduped), then bookmarks.
    std::unordered_set<std::string> seen;
    std::string tiles;
    int n = 0;
    for (const auto& e : Storage::Instance().History()) {
        if (n >= 12) break;
        if (e.url.rfind("http", 0) != 0) continue;
        if (!seen.insert(e.url).second) continue;
        std::string title = e.title.empty() ? e.url : e.title;
        tiles += "<a class='tile' href='" + util::HtmlEscape(e.url) + "'>"
                 "<div class='tt'>" + util::HtmlEscape(title) + "</div>"
                 "<div class='tu'>" + util::HtmlEscape(e.url) + "</div></a>";
        ++n;
    }
    for (const auto& b : Storage::Instance().Bookmarks()) {
        if (n >= 12) break;
        if (!seen.insert(b.url).second) continue;
        std::string title = b.title.empty() ? b.url : b.title;
        tiles += "<a class='tile' href='" + util::HtmlEscape(b.url) + "'>"
                 "<div class='tt'>" + util::HtmlEscape(title) + "</div>"
                 "<div class='tu'>" + util::HtmlEscape(b.url) + "</div></a>";
        ++n;
    }

    if (tiles.empty())
        html += "<div class='empty'>Your most-visited sites will show up here.</div>";
    else
        html += "<div class='grid'>" + tiles + "</div>";

    html += "</body></html>";
    return util::Widen(html);
}

void BrowserWindow::ApplyTheme() {
    m_dark = Theme::Resolve(m_mode);
    m_pal  = Theme::Get(m_dark);

    BOOL v = m_dark ? TRUE : FALSE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &v, sizeof(v));

    if (m_addrBrush) DeleteObject(m_addrBrush);
    m_addrBrush = CreateSolidBrush(m_pal.addrBg);

    if (m_tabs) m_tabs->ApplyColorScheme(m_dark);
    if (m_downloads) m_downloads->ApplyTheme(m_dark);

    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ===========================================================================
// Layout
// ===========================================================================
void BrowserWindow::Layout() {
    if (!m_hwnd) return;
    RECT client;
    GetClientRect(m_hwnd, &client);
    int W = client.right, H = client.bottom;

    int railW = RailWidthPx();
    int tbH   = Scale(TOOLBAR_H);
    if (railW > W) railW = W;

    m_rcRail    = { 0, 0, railW, H };
    m_rcToolbar = { railW, 0, W, tbH };
    m_rcContent = { railW, tbH, W, H };

    int pad = Scale(6), btn = Scale(BTN), by = (tbH - btn) / 2;
    int x = railW + pad;
    m_rcBack   = { x, by, x + btn, by + btn }; x += btn + Scale(2);
    m_rcFwd    = { x, by, x + btn, by + btn }; x += btn + Scale(2);
    m_rcReload = { x, by, x + btn, by + btn }; x += btn + Scale(2);
    m_rcHome   = { x, by, x + btn, by + btn }; x += btn + pad;

    int menuX = W - pad - btn;
    m_rcMenu = { menuX, by, menuX + btn, by + btn };
    int starX = menuX - Scale(2) - btn;
    m_rcStar = { starX, by, starX + btn, by + btn };

    int addrL = x, addrR = starX - pad;
    int addrH = Scale(ADDR_H), ay = (tbH - addrH) / 2;
    if (addrR < addrL + Scale(40)) addrR = addrL + Scale(40);
    m_rcAddr = { addrL, ay, addrR, ay + addrH };

    if (m_addrEdit) {
        int editH = Scale(22), ey = (tbH - editH) / 2;
        MoveWindow(m_addrEdit, addrL + Scale(10), ey,
                   (addrR - addrL) - Scale(20), editH, TRUE);
    }

    m_rcNewTab = { 0, 0, railW, Scale(NEWTAB_H) };
    m_rcRailToggle = { 0, H - Scale(TOGGLE_H), railW, H };
    ComputeTabRects();

    if (m_tabs) m_tabs->ResizeActive();
}

int BrowserWindow::RailWidthPx() const {
    int dip = std::max((int)RAIL_MIN, std::min((int)RAIL_MAX, m_railDip));
    return Scale(dip);
}

bool BrowserWindow::RailCollapsed() const {
    return m_railDip < RAIL_COLLAPSE;
}

bool BrowserWindow::InSplitter(int x, int y) const {
    int s = Scale(SPLITTER) / 2 + 1;
    return y >= 0 && y <= m_rcRail.bottom &&
           x >= m_rcRail.right - s && x <= m_rcRail.right + s;
}

void BrowserWindow::ToggleRailCollapsed() {
    if (RailCollapsed()) {
        m_railDip = std::max((int)RAIL_COLLAPSE, m_railExpandedDip);
    } else {
        m_railExpandedDip = m_railDip;
        m_railDip = RAIL_MIN;
    }
    Layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    SaveRailWidth();
    if (m_tabs) m_tabs->ResizeActive();
}

void BrowserWindow::SaveRailWidth() {
    Storage::Instance().GetConfig().railWidth = m_railDip;
    Storage::Instance().SaveConfig();
}

void BrowserWindow::ComputeTabRects() {
    m_tabHits.clear();
    if (!m_tabs) return;
    int railW = m_rcRail.right;
    int tabH = Scale(TAB_H), closeSz = Scale(22);
    int y = Scale(NEWTAB_H);
    int bottom = m_rcRail.bottom - Scale(TOGGLE_H);   // leave room for the toggle row
    bool collapsed = RailCollapsed();
    for (const auto& t : m_tabs->Tabs()) {
        if (y + tabH > bottom) break;   // no scrolling yet; stop when out of room
        RECT tr = { 0, y, railW, y + tabH };
        RECT cr;
        if (collapsed) {
            // Small close badge in the top-right corner; the rest of the narrow
            // tab stays clickable for switching.
            int cs = Scale(16);
            cr = { railW - cs - Scale(3), y + Scale(3), railW - Scale(3), y + Scale(3) + cs };
        } else {
            int cy = y + (tabH - closeSz) / 2;
            cr = { railW - closeSz - Scale(8), cy, railW - Scale(8), cy + closeSz };
        }
        m_tabHits.push_back({ t->id, tr, cr });
        y += tabH;
    }
}

// ===========================================================================
// Painting
// ===========================================================================
void BrowserWindow::FillRound(HDC hdc, RECT rc, int radius, COLORREF fill) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, fill);
    HGDIOBJ ob = SelectObject(hdc, b), op = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(b); DeleteObject(pen);
}

void BrowserWindow::DrawGlyph(HDC hdc, RECT rc, wchar_t glyph, COLORREF color, HFONT font, bool hover) {
    if (hover) FillRound(hdc, rc, Scale(6), m_pal.tabHoverBg);
    HGDIOBJ of = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    wchar_t s[2] = { glyph, 0 };
    DrawTextW(hdc, s, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, of);
}

void BrowserWindow::Paint(HDC target) {
    RECT client;
    GetClientRect(m_hwnd, &client);
    int W = client.right, H = client.bottom;

    // Double buffer to avoid flicker.
    HDC hdc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, W, H);
    HGDIOBJ ob = SelectObject(hdc, bmp);

    HBRUSH chrome = CreateSolidBrush(m_pal.chromeBg);
    HBRUSH window = CreateSolidBrush(m_pal.windowBg);
    FillRect(hdc, &client, chrome);
    FillRect(hdc, &m_rcContent, window);
    DeleteObject(chrome);
    DeleteObject(window);

    PaintRail(hdc);
    PaintToolbar(hdc);

    BitBlt(target, 0, 0, W, H, hdc, 0, 0, SRCCOPY);

    SelectObject(hdc, ob);
    DeleteObject(bmp);
    DeleteDC(hdc);
}

void BrowserWindow::DrawFavicon(HDC hdc, RECT rc, Tab* tab) {
    if (tab && tab->favicon) {
        Gdiplus::Graphics g(hdc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(tab->favicon, (INT)rc.left, (INT)rc.top,
                    (INT)(rc.right - rc.left), (INT)(rc.bottom - rc.top));
    } else {
        DrawGlyph(hdc, rc, GL_GLOBE, m_pal.textDim, m_iconFont, false);
    }
}

void BrowserWindow::PaintRail(HDC hdc) {
    bool collapsed = RailCollapsed();
    int railW = m_rcRail.right;

    // Right separator.
    RECT sep = { railW - 1, 0, railW, m_rcRail.bottom };
    HBRUSH bd = CreateSolidBrush(m_pal.border);
    FillRect(hdc, &sep, bd);
    DeleteObject(bd);

    // New-tab row.
    if (m_hotBtn == B_NewTab) {
        RECT r = m_rcNewTab;
        InflateRect(&r, -Scale(6), -Scale(5));
        FillRound(hdc, r, Scale(8), m_pal.tabHoverBg);
    }
    if (collapsed) {
        DrawGlyph(hdc, m_rcNewTab, GL_ADD, m_pal.text, m_iconFont, false);
    } else {
        RECT gi = { m_rcNewTab.left + Scale(10), m_rcNewTab.top, m_rcNewTab.left + Scale(34), m_rcNewTab.bottom };
        DrawGlyph(hdc, gi, GL_ADD, m_pal.text, m_iconFont, false);
        RECT gt = { m_rcNewTab.left + Scale(40), m_rcNewTab.top, m_rcNewTab.right - Scale(8), m_rcNewTab.bottom };
        HGDIOBJ of = SelectObject(hdc, m_uiFont);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, m_pal.text);
        DrawTextW(hdc, L"New tab", -1, &gt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, of);
    }

    // Tabs.
    int activeId = m_tabs ? m_tabs->ActiveId() : -1;
    int favSz = Scale(FAV);
    for (const auto& hit : m_tabHits) {
        Tab* t = nullptr;
        if (m_tabs) for (const auto& tp : m_tabs->Tabs()) if (tp->id == hit.id) { t = tp.get(); break; }
        if (!t) continue;
        bool active = (hit.id == activeId);
        bool hover  = (hit.id == m_hotTab);
        int th = hit.tab.bottom - hit.tab.top;

        if (active) {
            HBRUSH ab = CreateSolidBrush(m_pal.tabActiveBg);
            FillRect(hdc, &hit.tab, ab);
            DeleteObject(ab);
            RECT bar = { hit.tab.left, hit.tab.top + Scale(6), hit.tab.left + Scale(3), hit.tab.bottom - Scale(6) };
            HBRUSH acc = CreateSolidBrush(m_pal.accent);
            FillRect(hdc, &bar, acc);
            DeleteObject(acc);
        } else if (hover) {
            RECT r = hit.tab;
            InflateRect(&r, -Scale(4), -Scale(3));
            FillRound(hdc, r, Scale(8), m_pal.tabHoverBg);
        }

        int fy = hit.tab.top + (th - favSz) / 2;
        RECT fav;
        if (collapsed) { int fx = (railW - favSz) / 2; fav = { fx, fy, fx + favSz, fy + favSz }; }
        else           { int fx = hit.tab.left + Scale(12); fav = { fx, fy, fx + favSz, fy + favSz }; }
        DrawFavicon(hdc, fav, t);

        if (!collapsed) {
            RECT title = { hit.tab.left + Scale(38), hit.tab.top, hit.close.left - Scale(4), hit.tab.bottom };
            HGDIOBJ of2 = SelectObject(hdc, m_uiFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, m_pal.text);
            DrawTextW(hdc, t->title.c_str(), -1, &title,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(hdc, of2);
            if (active || hover) {
                bool ch = (m_hotTab == hit.id && m_hotClose);
                DrawGlyph(hdc, hit.close, GL_CLOSE, m_pal.text, m_iconFont, ch);
            }
        } else if (hover) {
            // Collapsed rail: reveal a close badge over the corner on hover so
            // the tab can still be closed when the rail is narrow.
            bool ch = (m_hotTab == hit.id && m_hotClose);
            FillRound(hdc, hit.close, Scale(4), ch ? m_pal.accent : m_pal.tabActiveBg);
            DrawGlyph(hdc, hit.close, GL_CLOSE, m_pal.text, m_iconFont, false);
        }
    }

    // Collapse/expand toggle row.
    RECT tsep = { 0, m_rcRailToggle.top, railW, m_rcRailToggle.top + 1 };
    HBRUSH tb = CreateSolidBrush(m_pal.border);
    FillRect(hdc, &tsep, tb);
    DeleteObject(tb);
    DrawGlyph(hdc, m_rcRailToggle, collapsed ? GL_CHEVR : GL_CHEVL,
              m_pal.textDim, m_iconFont, m_hotBtn == B_RailToggle);
}

void BrowserWindow::PaintToolbar(HDC hdc) {
    // Bottom border.
    RECT sep = { m_rcToolbar.left, m_rcToolbar.bottom - 1, m_rcToolbar.right, m_rcToolbar.bottom };
    HBRUSH bd = CreateSolidBrush(m_pal.border);
    FillRect(hdc, &sep, bd);

    Tab* a = m_tabs ? m_tabs->Active() : nullptr;
    bool back = a && a->canGoBack;
    bool fwd  = a && a->canGoForward;

    DrawGlyph(hdc, m_rcBack,   GL_BACK,   back ? m_pal.text : m_pal.textDim, m_iconFont, m_hotBtn == B_Back);
    DrawGlyph(hdc, m_rcFwd,    GL_FWD,    fwd  ? m_pal.text : m_pal.textDim, m_iconFont, m_hotBtn == B_Fwd);
    DrawGlyph(hdc, m_rcReload, GL_RELOAD, m_pal.text, m_iconFont, m_hotBtn == B_Reload);
    DrawGlyph(hdc, m_rcHome,   GL_HOME,   m_pal.text, m_iconFont, m_hotBtn == B_Home);

    // Address bar background + outline.
    FillRound(hdc, m_rcAddr, Scale(16), m_pal.addrBg);
    HPEN pen = CreatePen(PS_SOLID, 1, m_pal.border);
    HGDIOBJ op = SelectObject(hdc, pen);
    HGDIOBJ obr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, m_rcAddr.left, m_rcAddr.top, m_rcAddr.right, m_rcAddr.bottom, Scale(32), Scale(32));
    SelectObject(hdc, op); SelectObject(hdc, obr);
    DeleteObject(pen);

    // Bookmark star.
    bool marked = a && Storage::Instance().IsBookmarked(util::Narrow(a->url));
    DrawGlyph(hdc, m_rcStar, marked ? GL_STARF : GL_STAR,
              marked ? m_pal.accent : m_pal.text, m_iconFont, m_hotBtn == B_Star);

    // Menu.
    DrawGlyph(hdc, m_rcMenu, GL_MORE, m_pal.text, m_iconFont, m_hotBtn == B_Menu);

    DeleteObject(bd);
}

// ===========================================================================
// Hit testing / input
// ===========================================================================
void BrowserWindow::HitTest(int x, int y, Btn& btn, int& tabId, bool& onClose) const {
    btn = B_None; tabId = -1; onClose = false;
    POINT p{ x, y };
    if (PtInRect(&m_rcBack, p))        btn = B_Back;
    else if (PtInRect(&m_rcFwd, p))    btn = B_Fwd;
    else if (PtInRect(&m_rcReload, p)) btn = B_Reload;
    else if (PtInRect(&m_rcHome, p))   btn = B_Home;
    else if (PtInRect(&m_rcStar, p))   btn = B_Star;
    else if (PtInRect(&m_rcMenu, p))   btn = B_Menu;
    else if (PtInRect(&m_rcRailToggle, p)) btn = B_RailToggle;
    else if (PtInRect(&m_rcNewTab, p)) btn = B_NewTab;
    if (btn != B_None) return;

    for (const auto& hit : m_tabHits) {
        if (PtInRect(&hit.tab, p)) {
            tabId = hit.id;
            onClose = PtInRect(&hit.close, p) != FALSE;
            return;
        }
    }
}

void BrowserWindow::OnLButtonDown(int x, int y) {
    if (InSplitter(x, y)) {           // begin resizing the rail
        m_draggingSplitter = true;
        SetCapture(m_hwnd);
        return;
    }
    Btn btn; int tabId; bool onClose;
    HitTest(x, y, btn, tabId, onClose);
    switch (btn) {
        case B_Back:       OnCommand(IDM_BACK); return;
        case B_Fwd:        OnCommand(IDM_FORWARD); return;
        case B_Reload:     OnCommand(IDM_RELOAD); return;
        case B_Home:       OnCommand(IDM_HOME); return;
        case B_NewTab:     OnCommand(IDM_NEWTAB); return;
        case B_Star:       OnCommand(IDM_TOGGLE_BOOKMARK); return;
        case B_Menu:       ShowMenu(); return;
        case B_RailToggle: ToggleRailCollapsed(); return;
        default: break;
    }
    if (tabId >= 0 && m_tabs) {
        if (onClose) m_tabs->CloseTab(tabId);
        else         m_tabs->SwitchTo(tabId);
    }
}

void BrowserWindow::OnMouseMove(int x, int y) {
    if (m_draggingSplitter) {         // live rail resize
        RECT client; GetClientRect(m_hwnd, &client);
        int logical = MulDiv(x, 96, (int)m_dpi);
        int maxDip = std::min((int)RAIL_MAX, MulDiv(client.right / 2, 96, (int)m_dpi));
        m_railDip = std::max((int)RAIL_MIN, std::min(maxDip, logical));
        Layout();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    Btn btn; int tabId; bool onClose;
    HitTest(x, y, btn, tabId, onClose);
    if (btn != m_hotBtn || tabId != m_hotTab || onClose != m_hotClose) {
        m_hotBtn = btn; m_hotTab = tabId; m_hotClose = onClose;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    if (!m_tracking) {
        TRACKMOUSEEVENT tme{ sizeof(tme) };
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = m_hwnd;
        TrackMouseEvent(&tme);
        m_tracking = true;
    }
}

void BrowserWindow::ClearHover() {
    m_tracking = false;
    if (m_hotBtn != B_None || m_hotTab != -1) {
        m_hotBtn = B_None; m_hotTab = -1; m_hotClose = false;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

// ===========================================================================
// Commands / actions
// ===========================================================================
void BrowserWindow::OnCommand(int id) {
    if (id >= IDM_BOOKMARK_BASE && id <= IDM_BOOKMARK_MAX) {
        size_t idx = (size_t)(id - IDM_BOOKMARK_BASE);
        const auto& bm = Storage::Instance().Bookmarks();
        if (idx < bm.size() && m_tabs) m_tabs->NewTab(util::Widen(bm[idx].url), true);
        return;
    }
    if (id >= IDM_EXT_TOGGLE_BASE && id <= IDM_EXT_TOGGLE_MAX) {
        if (m_extensions) m_extensions->ToggleEnabled((size_t)(id - IDM_EXT_TOGGLE_BASE));
        return;
    }
    if (id >= IDM_EXT_REMOVE_BASE && id <= IDM_EXT_REMOVE_MAX) {
        if (m_extensions) m_extensions->Remove((size_t)(id - IDM_EXT_REMOVE_BASE));
        return;
    }
    // Ctrl+1 .. Ctrl+9 -> jump to tab by position (Ctrl+9 = last tab).
    if (id >= IDM_TAB_BASE && id <= IDM_TAB_LAST) {
        if (m_tabs) {
            int n = id - IDM_TAB_BASE;
            if (n == 8) m_tabs->SwitchToIndex((int)m_tabs->Count() - 1);
            else        m_tabs->SwitchToIndex(n);
        }
        return;
    }
    switch (id) {
        case IDM_NEWTAB:
            if (m_tabs) {
                m_focusAddrPending = true;          // focus once the blank tab activates
                m_tabs->NewTab(L"", true);
                SetWindowTextW(m_addrEdit, L"");    // blank + focused, ready to type
                FocusAddress();
            }
            break;
        case IDM_CLOSETAB:        if (m_tabs) m_tabs->CloseTab(m_tabs->ActiveId()); break;
        case IDM_CTX_CLOSETAB:    if (m_tabs) m_tabs->CloseTab(m_ctxTabId); break;
        case IDM_CTX_RELOAD:      if (m_tabs) { m_tabs->SwitchTo(m_ctxTabId); m_tabs->Reload(); } break;
        case IDM_DUPLICATE_TAB:   if (m_tabs) m_tabs->DuplicateTab(m_ctxTabId >= 0 ? m_ctxTabId : m_tabs->ActiveId()); break;
        case IDM_CLOSE_OTHERS:    if (m_tabs) m_tabs->CloseOthers(m_ctxTabId >= 0 ? m_ctxTabId : m_tabs->ActiveId()); break;
        case IDM_REOPEN_TAB:      if (m_tabs) m_tabs->ReopenClosed(); break;
        case IDM_TOGGLE_RAIL:     ToggleRailCollapsed(); break;
        case IDM_ZOOM_IN:         if (m_tabs) m_tabs->ZoomIn(); break;
        case IDM_ZOOM_OUT:        if (m_tabs) m_tabs->ZoomOut(); break;
        case IDM_ZOOM_RESET:      if (m_tabs) m_tabs->ZoomReset(); break;
        case IDM_RELOAD:          if (m_tabs) m_tabs->Reload(); break;
        case IDM_BACK:            if (m_tabs) m_tabs->Back(); break;
        case IDM_FORWARD:         if (m_tabs) m_tabs->Forward(); break;
        case IDM_HOME:            if (m_tabs) m_tabs->Navigate(util::Widen(Storage::Instance().GetConfig().homepage)); break;
        case IDM_NEXT_TAB:        if (m_tabs) m_tabs->NextTab(); break;
        case IDM_PREV_TAB:        if (m_tabs) m_tabs->PrevTab(); break;
        case IDM_FOCUS_ADDRESS:   FocusAddress(); break;
        case IDM_TOGGLE_BOOKMARK: ToggleBookmark(); break;
        case IDM_HISTORY:         ShowHistoryPage(); break;
        case IDM_DOWNLOADS:       if (m_downloads) m_downloads->Show(); break;
        case IDM_LOAD_EXTENSION: {
            if (m_extensions && m_extensions->Ready()) {
                std::wstring folder = PickFolder(m_hwnd, L"Select an unpacked extension folder");
                if (!folder.empty()) m_extensions->LoadFromFolder(m_hwnd, folder);
            } else {
                MessageBoxW(m_hwnd, L"The browser is still starting — try again in a moment.",
                            L"iwser", MB_ICONINFORMATION);
            }
            break;
        }
        case IDM_CLEAR_HISTORY:   Storage::Instance().ClearHistory(); break;
        case IDM_TOGGLE_THEME: {
            m_mode = m_dark ? ThemeMode::Light : ThemeMode::Dark;
            Storage::Instance().GetConfig().theme = Theme::ModeName(m_mode);
            Storage::Instance().SaveConfig();
            ApplyTheme();
            break;
        }
        case IDM_EXIT:            DestroyWindow(m_hwnd); break;
    }
}

void BrowserWindow::FocusAddress() {
    SetFocus(m_addrEdit);
    SendMessageW(m_addrEdit, EM_SETSEL, 0, -1);
}

void BrowserWindow::CommitAddress() {
    if (!m_tabs) return;
    int len = GetWindowTextLengthW(m_addrEdit);
    std::wstring text(len, L'\0');
    if (len) GetWindowTextW(m_addrEdit, text.data(), len + 1);
    m_tabs->Navigate(text);
}

std::wstring BrowserWindow::ActiveUrlUtf16() const {
    if (m_tabs) if (Tab* a = m_tabs->Active()) return a->url;
    return L"";
}

void BrowserWindow::ToggleBookmark() {
    if (!m_tabs) return;
    Tab* a = m_tabs->Active();
    if (!a) return;
    std::string url = util::Narrow(a->url);
    if (url.empty()) return;
    auto& st = Storage::Instance();
    if (st.IsBookmarked(url)) st.RemoveBookmarkByUrl(url);
    else                      st.AddBookmark(util::Narrow(a->title), url);
    InvalidateRect(m_hwnd, &m_rcToolbar, FALSE);
}

// Prompt for a folder (used to pick an unpacked extension directory).
static std::wstring PickFolder(HWND owner, const wchar_t* title) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return L"";
    wchar_t path[MAX_PATH] = L"";
    std::wstring result;
    if (SHGetPathFromIDListW(pidl, path)) result = path;
    CoTaskMemFree(pidl);
    return result;
}

void BrowserWindow::ShowMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_NEWTAB, L"New tab\tCtrl+T");
    AppendMenuW(menu, MF_STRING | (m_tabs && m_tabs->HasClosed() ? 0 : MF_GRAYED),
                IDM_REOPEN_TAB, L"Reopen closed tab\tCtrl+Shift+T");
    AppendMenuW(menu, MF_STRING, IDM_TOGGLE_RAIL,
                RailCollapsed() ? L"Expand sidebar\tCtrl+B" : L"Collapse sidebar\tCtrl+B");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    bool marked = false;
    if (m_tabs) if (Tab* a = m_tabs->Active()) marked = Storage::Instance().IsBookmarked(util::Narrow(a->url));
    AppendMenuW(menu, MF_STRING, IDM_TOGGLE_BOOKMARK,
                marked ? L"Remove bookmark" : L"Bookmark this page");

    HMENU sub = CreatePopupMenu();
    const auto& bm = Storage::Instance().Bookmarks();
    if (bm.empty()) {
        AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, L"(no bookmarks)");
    } else {
        for (size_t i = 0; i < bm.size() && i < (IDM_BOOKMARK_MAX - IDM_BOOKMARK_BASE); ++i) {
            std::wstring label = util::Widen(bm[i].title.empty() ? bm[i].url : bm[i].title);
            AppendMenuW(sub, MF_STRING, IDM_BOOKMARK_BASE + (UINT)i, label.c_str());
        }
    }
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub, L"Bookmarks");

    AppendMenuW(menu, MF_STRING, IDM_HISTORY, L"History");
    AppendMenuW(menu, MF_STRING, IDM_DOWNLOADS, L"Downloads");

    // Extensions submenu.
    HMENU extMenu = CreatePopupMenu();
    AppendMenuW(extMenu, MF_STRING, IDM_LOAD_EXTENSION, L"Load extension…");
    AppendMenuW(extMenu, MF_SEPARATOR, 0, nullptr);
    const auto& exts = m_extensions ? m_extensions->List() : std::vector<Extensions::Item>();
    if (exts.empty()) {
        AppendMenuW(extMenu, MF_STRING | MF_GRAYED, 0, L"(none installed)");
    } else {
        for (size_t i = 0; i < exts.size() && i < 100; ++i) {
            HMENU one = CreatePopupMenu();
            AppendMenuW(one, MF_STRING | (exts[i].enabled ? MF_CHECKED : 0),
                        IDM_EXT_TOGGLE_BASE + (UINT)i, L"Enabled");
            AppendMenuW(one, MF_STRING, IDM_EXT_REMOVE_BASE + (UINT)i, L"Remove");
            std::wstring label = exts[i].name.empty() ? L"(extension)" : exts[i].name;
            AppendMenuW(extMenu, MF_POPUP, (UINT_PTR)one, label.c_str());
        }
    }
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)extMenu, L"Extensions");

    // Zoom submenu.
    HMENU zoomMenu = CreatePopupMenu();
    AppendMenuW(zoomMenu, MF_STRING, IDM_ZOOM_IN,    L"Zoom in\tCtrl++");
    AppendMenuW(zoomMenu, MF_STRING, IDM_ZOOM_OUT,   L"Zoom out\tCtrl+-");
    AppendMenuW(zoomMenu, MF_STRING, IDM_ZOOM_RESET, L"Reset zoom\tCtrl+0");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)zoomMenu, L"Zoom");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (m_dark ? MF_CHECKED : 0), IDM_TOGGLE_THEME, L"Dark mode");
    AppendMenuW(menu, MF_STRING, IDM_CLEAR_HISTORY, L"Clear history");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    POINT pt{ m_rcMenu.left, m_rcMenu.bottom };
    ClientToScreen(m_hwnd, &pt);
    int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                  pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd) OnCommand(cmd);
}

void BrowserWindow::ShowTabMenu(int tabId, POINT screenPt) {
    if (!m_tabs) return;
    m_ctxTabId = tabId;

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_NEWTAB, L"New tab\tCtrl+T");
    AppendMenuW(menu, MF_STRING, IDM_CTX_RELOAD, L"Reload tab");
    AppendMenuW(menu, MF_STRING, IDM_DUPLICATE_TAB, L"Duplicate tab");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_CTX_CLOSETAB, L"Close tab\tCtrl+W");
    AppendMenuW(menu, MF_STRING | (m_tabs->Count() > 1 ? 0 : MF_GRAYED),
                IDM_CLOSE_OTHERS, L"Close other tabs");
    AppendMenuW(menu, MF_STRING | (m_tabs->HasClosed() ? 0 : MF_GRAYED),
                IDM_REOPEN_TAB, L"Reopen closed tab\tCtrl+Shift+T");

    int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                  screenPt.x, screenPt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd) OnCommand(cmd);
    m_ctxTabId = -1;
}

void BrowserWindow::ShowHistoryPage() {
    if (!m_tabs) return;
    const bool d = m_dark;
    std::string bg   = d ? "#202124" : "#ffffff";
    std::string fg   = d ? "#e8eaed" : "#202124";
    std::string dim  = d ? "#9aa0a6" : "#5f6368";
    std::string link = d ? "#8ab4f8" : "#1a73e8";
    std::string bord = d ? "#3c4043" : "#dfe1e5";

    std::string html =
        "<!doctype html><html><head><meta charset='utf-8'><title>History</title><style>"
        "body{font-family:'Segoe UI',system-ui,sans-serif;margin:0;padding:28px;background:" + bg + ";color:" + fg + ";}"
        "h1{font-size:20px;font-weight:600;}"
        "a{color:" + link + ";text-decoration:none;}a:hover{text-decoration:underline;}"
        ".row{padding:10px 0;border-bottom:1px solid " + bord + ";}"
        ".t{font-size:12px;color:" + dim + ";margin-top:2px;}"
        "</style></head><body><h1>History</h1>";

    const auto& hist = Storage::Instance().History();
    if (hist.empty()) html += "<p class='t'>No history yet.</p>";
    int n = 0;
    for (const auto& e : hist) {
        if (n++ >= 500) break;
        time_t tt = (time_t)(e.visitedAt / 1000);
        struct tm tmv{};
        localtime_s(&tmv, &tt);
        char ts[64];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", &tmv);
        std::string title = e.title.empty() ? e.url : e.title;
        html += "<div class='row'><div><a href='" + util::HtmlEscape(e.url) + "'>" +
                util::HtmlEscape(title) + "</a></div><div class='t'>" +
                util::HtmlEscape(e.url) + " &middot; " + ts + "</div></div>";
    }
    html += "</body></html>";
    m_tabs->NavigateActiveToString(util::Widen(html));
}

// ===========================================================================
// ITabHost callbacks
// ===========================================================================
void BrowserWindow::RefreshChrome() {
    if (m_tabs && GetFocus() != m_addrEdit) {
        if (Tab* a = m_tabs->Active()) {
            // Don't surface the internal about:blank of the New Tab page.
            const wchar_t* shown = (a->url.rfind(L"about:", 0) == 0) ? L"" : a->url.c_str();
            SetWindowTextW(m_addrEdit, shown);
        }
    }
    // Put the cursor in the address/search bar as soon as a blank New Tab page
    // becomes active — both on launch and every time a new tab is opened.
    if (m_focusAddrPending && m_tabs) {
        if (Tab* a = m_tabs->Active(); a && a->isNewTab) {
            m_focusAddrPending = false;
            FocusAddress();
        }
    }
    ComputeTabRects();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void BrowserWindow::OnNewWindow(const std::wstring& url) {
    if (m_tabs) m_tabs->NewTab(url, true);
}

void BrowserWindow::AttachDownloads(ICoreWebView2* wv) {
    if (m_downloads) m_downloads->Attach(wv);
}

void BrowserWindow::OnWebViewCreated(ICoreWebView2* wv) {
    if (m_extensions) m_extensions->Init(wv);   // captures the profile once
}

void BrowserWindow::OnAcceleratorCommand(int commandId) {
    PostMessageW(m_hwnd, WM_COMMAND, commandId, 0);
}

// ===========================================================================
// Window procedure
// ===========================================================================
LRESULT CALLBACK BrowserWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    BrowserWindow* self = reinterpret_cast<BrowserWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<BrowserWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->Handle(msg, wp, lp);
}

LRESULT BrowserWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            Layout();
            return 0;

        case WM_DPICHANGED: {
            m_dpi = HIWORD(wp);
            if (!m_dpi) m_dpi = 96;
            CreateFonts();
            RECT* pr = reinterpret_cast<RECT*>(lp);
            SetWindowPos(m_hwnd, nullptr, pr->left, pr->top,
                         pr->right - pr->left, pr->bottom - pr->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Layout();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hwnd, &ps);
            Paint(hdc);
            EndPaint(m_hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN:
            OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;

        case WM_LBUTTONUP:
            if (m_draggingSplitter) {
                m_draggingSplitter = false;
                ReleaseCapture();
                SaveRailWidth();
                if (m_tabs) m_tabs->ResizeActive();
            }
            return 0;

        case WM_MBUTTONDOWN: {
            Btn b; int tid; bool oc;
            HitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), b, tid, oc);
            if (tid >= 0 && m_tabs) m_tabs->CloseTab(tid);   // middle-click closes a tab
            return 0;
        }

        case WM_RBUTTONDOWN: {
            Btn b; int tid; bool oc;
            int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
            HitTest(mx, my, b, tid, oc);
            if (tid >= 0) {
                POINT pt{ mx, my };
                ClientToScreen(m_hwnd, &pt);
                ShowTabMenu(tid, pt);
            }
            return 0;
        }

        case WM_MOUSEMOVE:
            OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                POINT p; GetCursorPos(&p); ScreenToClient(m_hwnd, &p);
                if (m_draggingSplitter || InSplitter(p.x, p.y)) {
                    SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;

        case WM_MOUSELEAVE:
            ClearHover();
            return 0;

        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wp;
            SetBkColor(dc, m_pal.addrBg);
            SetTextColor(dc, m_pal.text);
            return (LRESULT)m_addrBrush;
        }

        case WM_COMMAND:
            OnCommand(LOWORD(wp));
            return 0;

        case WM_GETMINMAXINFO: {
            auto mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = Scale(640);
            mmi->ptMinTrackSize.y = Scale(420);
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(m_hwnd);
            return 0;

        case WM_DESTROY:
            if (m_autoComplete) { m_autoComplete->Release(); m_autoComplete = nullptr; }
            if (m_addrEdit) RemoveWindowSubclass(m_addrEdit, &BrowserWindow::EditProc, 1);
            if (m_uiFont) DeleteObject(m_uiFont);
            if (m_iconFont) DeleteObject(m_iconFont);
            if (m_addrBrush) DeleteObject(m_addrBrush);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

LRESULT CALLBACK BrowserWindow::EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                         UINT_PTR, DWORD_PTR ref) {
    BrowserWindow* self = reinterpret_cast<BrowserWindow*>(ref);
    switch (msg) {
        case WM_KEYDOWN:
            if (wp == VK_RETURN) { self->CommitAddress(); return 0; }
            if (wp == VK_ESCAPE) { self->RefreshChrome(); SendMessageW(hwnd, EM_SETSEL, 0, -1); return 0; }
            if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) { SendMessageW(hwnd, EM_SETSEL, 0, -1); return 0; }
            break;
        case WM_CHAR:
            if (wp == VK_RETURN || wp == 0x0A || wp == VK_ESCAPE) return 0;  // no error beep
            if (wp == 1) return 0;  // Ctrl+A
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}
