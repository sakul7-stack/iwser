#include "Downloads.h"
#include "Storage.h"
#include "Util.h"

#include <commctrl.h>
#include <shlwapi.h>
#include <dwmapi.h>

using namespace Microsoft::WRL;

namespace {
const wchar_t* kClass = L"iwser_Downloads";
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
}

struct Downloads::Item {
    ComPtr<ICoreWebView2DownloadOperation> op;
    int row = -1;
    EventRegistrationToken bytesTok{};
    EventRegistrationToken stateTok{};
};

Downloads::Downloads(HINSTANCE hInst, HWND owner) : m_hInst(hInst), m_owner(owner) {}
Downloads::~Downloads() = default;

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------
void Downloads::EnsureWindow() {
    if (m_hwnd) return;

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc   = &Downloads::WndProc;
    wc.hInstance     = m_hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kClass;
    static bool registered = false;
    if (!registered) { RegisterClassExW(&wc); registered = true; }

    m_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW, kClass, L"Downloads",
        WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 320,
        m_owner, nullptr, m_hInst, this);

    m_list = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        0, 0, 0, 0, m_hwnd, nullptr, m_hInst, nullptr);
    ListView_SetExtendedListViewStyle(m_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = (LPWSTR)L"File";     col.cx = 260; ListView_InsertColumn(m_list, 0, &col);
    col.pszText = (LPWSTR)L"Progress"; col.cx = 110; ListView_InsertColumn(m_list, 1, &col);
    col.pszText = (LPWSTR)L"Status";   col.cx = 120; ListView_InsertColumn(m_list, 2, &col);

    ApplyTheme(m_dark);
}

LRESULT CALLBACK Downloads::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Downloads* self = reinterpret_cast<Downloads*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    switch (msg) {
        case WM_SIZE:
            if (self && self->m_list)
                MoveWindow(self->m_list, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
            return 0;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);   // hide, keep state
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void Downloads::Show() {
    EnsureWindow();
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
}

void Downloads::ApplyTheme(bool dark) {
    m_dark = dark;
    if (!m_hwnd) return;
    BOOL v = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &v, sizeof(v));
    if (m_list) {
        COLORREF bg = dark ? RGB(41, 42, 45) : RGB(255, 255, 255);
        COLORREF fg = dark ? RGB(232, 234, 237) : RGB(32, 33, 36);
        ListView_SetBkColor(m_list, bg);
        ListView_SetTextBkColor(m_list, bg);
        ListView_SetTextColor(m_list, fg);
        InvalidateRect(m_list, nullptr, TRUE);
    }
}

// ---------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------
int Downloads::AddRow(const std::wstring& name) {
    LVITEMW it{};
    it.mask = LVIF_TEXT;
    it.iItem = ListView_GetItemCount(m_list);
    it.pszText = (LPWSTR)name.c_str();
    int row = ListView_InsertItem(m_list, &it);
    ListView_SetItemText(m_list, row, 1, (LPWSTR)L"0%");
    ListView_SetItemText(m_list, row, 2, (LPWSTR)L"Starting");
    return row;
}

void Downloads::UpdateRow(Item* it) {
    if (!it || it->row < 0) return;

    INT64 received = 0, total = 0;
    it->op->get_BytesReceived(&received);
    it->op->get_TotalBytesToReceive(&total);

    wchar_t prog[32];
    if (total > 0) {
        int pct = (int)((received * 100) / total);
        swprintf(prog, 32, L"%d%%", pct);
    } else {
        swprintf(prog, 32, L"%lld KB", (long long)(received / 1024));
    }
    ListView_SetItemText(m_list, it->row, 1, prog);

    COREWEBVIEW2_DOWNLOAD_STATE state;
    it->op->get_State(&state);
    const wchar_t* status = L"Downloading";
    if (state == COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED)   status = L"Completed";
    else if (state == COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED) status = L"Interrupted";
    ListView_SetItemText(m_list, it->row, 2, (LPWSTR)status);
}

// ---------------------------------------------------------------------------
// WebView2 hook
// ---------------------------------------------------------------------------
void Downloads::Attach(ICoreWebView2* webview) {
    ComPtr<ICoreWebView2_4> wv4;
    if (FAILED(webview->QueryInterface(IID_PPV_ARGS(&wv4))) || !wv4) return;

    EventRegistrationToken tok{};
    wv4->add_DownloadStarting(
        Callback<ICoreWebView2DownloadStartingEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
                OnDownloadStarting(args);
                return S_OK;
            }).Get(), &tok);
}

void Downloads::OnDownloadStarting(ICoreWebView2DownloadStartingEventArgs* args) {
    EnsureWindow();

    ComPtr<ICoreWebView2DownloadOperation> op;
    if (FAILED(args->get_DownloadOperation(&op)) || !op) return;

    // Optionally redirect to a configured download directory.
    const std::string& dir = Storage::Instance().GetConfig().downloadDir;
    LPWSTR defPath = nullptr;
    args->get_ResultFilePath(&defPath);
    std::wstring path = defPath ? defPath : L"";
    if (defPath) CoTaskMemFree(defPath);
    if (!dir.empty() && !path.empty()) {
        std::wstring wdir = util::Widen(dir);
        std::wstring name = PathFindFileNameW(path.c_str());
        std::wstring target = wdir + L"\\" + name;
        args->put_ResultFilePath(target.c_str());
        path = target;
    }

    args->put_Handled(TRUE);  // suppress the default download UI; we show our own

    auto item = std::make_unique<Item>();
    item->op = op;
    std::wstring name = path.empty() ? L"download" : PathFindFileNameW(path.c_str());
    item->row = AddRow(name);
    Item* raw = item.get();
    m_items.push_back(std::move(item));

    op->add_BytesReceivedChanged(
        Callback<ICoreWebView2BytesReceivedChangedEventHandler>(
            [this, raw](ICoreWebView2DownloadOperation*, IUnknown*) -> HRESULT {
                UpdateRow(raw);
                return S_OK;
            }).Get(), &raw->bytesTok);

    op->add_StateChanged(
        Callback<ICoreWebView2StateChangedEventHandler>(
            [this, raw](ICoreWebView2DownloadOperation*, IUnknown*) -> HRESULT {
                UpdateRow(raw);
                return S_OK;
            }).Get(), &raw->stateTok);

    UpdateRow(raw);
    Show();
}
