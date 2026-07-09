#pragma once
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <memory>
#include <vector>

struct IAutoComplete;   // <shldisp.h>; only a pointer is stored here

#include "TabManager.h"
#include "Downloads.h"
#include "Theme.h"
#include "Extensions.h"

// Top-level browser window. Owns the WebView2 environment, the tab manager, the
// download manager, and paints the entire chrome (vertical tab rail + toolbar)
// with GDI. Implements ITabHost so the tab layer can call back into it.
class BrowserWindow : public ITabHost {
public:
    bool Create(HINSTANCE hInst, int nCmdShow, const std::wstring& initialUrl = L"");
    HWND  Hwnd()  const { return m_hwnd; }
    HACCEL Accel() const { return m_accel; }

    // ---- ITabHost ----
    RECT ContentRect() override { return m_rcContent; }
    HWND HostHwnd() override { return m_hwnd; }
    void RefreshChrome() override;
    void OnNewWindow(const std::wstring& url) override;
    void AttachDownloads(ICoreWebView2* wv) override;
    void OnWebViewCreated(ICoreWebView2* wv) override;
    void OnAcceleratorCommand(int commandId) override;
    std::wstring NewTabHtml() override;

private:
    enum Btn { B_None, B_Back, B_Fwd, B_Reload, B_Home, B_NewTab, B_Star, B_Menu, B_RailToggle };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT Handle(UINT, WPARAM, LPARAM);

    // setup
    void CreateFonts();
    void InitEnvironment();
    void ApplyTheme();

    // layout + paint
    int  Scale(int v) const { return MulDiv(v, (int)m_dpi, 96); }
    void Layout();
    void ComputeTabRects();
    void Paint(HDC hdc);
    void PaintRail(HDC hdc);
    void PaintToolbar(HDC hdc);
    void FillRound(HDC hdc, RECT rc, int radius, COLORREF fill);
    void DrawGlyph(HDC hdc, RECT rc, wchar_t glyph, COLORREF color, HFONT font, bool hover);

    // hit testing / input
    void HitTest(int x, int y, Btn& btn, int& tabId, bool& onClose) const;
    void OnLButtonDown(int x, int y);
    void OnMouseMove(int x, int y);
    void ClearHover();
    bool InSplitter(int x, int y) const;
    void ToggleRailCollapsed();
    void SaveRailWidth();
    int  RailWidthPx() const;   // current rail width in device pixels, clamped
    bool RailCollapsed() const; // favicon-only mode
    void DrawFavicon(HDC hdc, RECT rc, struct Tab* tab);

    // actions
    void OnCommand(int id);
    void DoNewTab();
    void FocusAddress();
    void CommitAddress();
    void ToggleBookmark();
    void ShowMenu();
    void ShowTabMenu(int tabId, POINT screenPt);   // right-click a tab in the rail
    void ShowHistoryPage();
    std::wstring ActiveUrlUtf16() const;

    HINSTANCE m_hInst = nullptr;
    HWND m_hwnd    = nullptr;
    HWND m_addrEdit = nullptr;
    std::wstring m_initialUrl;   // URL passed on the command line (empty -> homepage)
    HACCEL m_accel = nullptr;
    UINT m_dpi     = 96;

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> m_env;
    std::unique_ptr<TabManager> m_tabs;
    std::unique_ptr<Downloads>  m_downloads;
    std::unique_ptr<Extensions> m_extensions;

    // Address-bar autocomplete backed by browsing history + bookmarks.
    IAutoComplete* m_autoComplete = nullptr;   // released in WM_DESTROY
    void InitAddressAutoComplete();

    // theme
    ThemeMode m_mode = ThemeMode::Light;
    bool      m_dark = false;
    Palette   m_pal  = Theme::Get(false);
    HBRUSH    m_addrBrush = nullptr;
    HFONT     m_uiFont = nullptr;
    HFONT     m_iconFont = nullptr;

    // rail sizing (logical DIP; scaled per-DPI in layout)
    int  m_railDip = 240;          // current width
    int  m_railExpandedDip = 240;  // width to restore when un-collapsing
    bool m_draggingSplitter = false;

    // layout rects
    RECT m_rcRail{}, m_rcToolbar{}, m_rcContent{};
    RECT m_rcBack{}, m_rcFwd{}, m_rcReload{}, m_rcHome{}, m_rcStar{}, m_rcMenu{}, m_rcNewTab{}, m_rcAddr{}, m_rcRailToggle{};
    struct TabHit { int id; RECT tab; RECT close; };
    std::vector<TabHit> m_tabHits;

    // hover state
    Btn  m_hotBtn  = B_None;
    int  m_hotTab  = -1;
    bool m_hotClose = false;
    bool m_tracking = false;
    int  m_ctxTabId = -1;   // tab targeted by the current right-click context menu
    bool m_focusAddrPending = true;   // focus the address bar when a blank New Tab becomes active
};
