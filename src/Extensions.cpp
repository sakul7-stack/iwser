#include "Extensions.h"

using namespace Microsoft::WRL;

void Extensions::Init(ICoreWebView2* wv) {
    if (m_profile || !wv) return;
    ComPtr<ICoreWebView2_13> wv13;
    if (FAILED(wv->QueryInterface(IID_PPV_ARGS(&wv13))) || !wv13) return;
    ComPtr<ICoreWebView2Profile> profile;
    if (FAILED(wv13->get_Profile(&profile)) || !profile) return;
    if (SUCCEEDED(profile.As(&m_profile)) && m_profile) Refresh();
}

void Extensions::Refresh() {
    if (!m_profile) return;
    m_profile->GetBrowserExtensions(
        Callback<ICoreWebView2ProfileGetBrowserExtensionsCompletedHandler>(
            [this](HRESULT ec, ICoreWebView2BrowserExtensionList* list) -> HRESULT {
                if (FAILED(ec) || !list) return S_OK;
                m_items.clear();
                UINT32 count = 0;
                list->get_Count(&count);
                for (UINT32 i = 0; i < count; ++i) {
                    ComPtr<ICoreWebView2BrowserExtension> ext;
                    if (FAILED(list->GetValueAtIndex(i, &ext)) || !ext) continue;
                    Item it;
                    it.ext = ext;
                    LPWSTR name = nullptr;
                    ext->get_Name(&name);
                    it.name = name ? name : L"";
                    if (name) CoTaskMemFree(name);
                    BOOL en = FALSE;
                    ext->get_IsEnabled(&en);
                    it.enabled = !!en;
                    m_items.push_back(std::move(it));
                }
                return S_OK;
            }).Get());
}

void Extensions::LoadFromFolder(HWND owner, const std::wstring& folder) {
    if (!m_profile || folder.empty()) return;
    m_profile->AddBrowserExtension(
        folder.c_str(),
        Callback<ICoreWebView2ProfileAddBrowserExtensionCompletedHandler>(
            [this, owner](HRESULT ec, ICoreWebView2BrowserExtension*) -> HRESULT {
                if (FAILED(ec)) {
                    MessageBoxW(owner,
                        L"Could not load the extension.\n\nPick the folder of an *unpacked* "
                        L"extension (one that contains manifest.json).",
                        L"iwser", MB_ICONWARNING);
                } else {
                    Refresh();
                    MessageBoxW(owner, L"Extension loaded.", L"iwser", MB_ICONINFORMATION);
                }
                return S_OK;
            }).Get());
}

void Extensions::Remove(size_t index) {
    if (index >= m_items.size()) return;
    m_items[index].ext->Remove(
        Callback<ICoreWebView2BrowserExtensionRemoveCompletedHandler>(
            [this](HRESULT) -> HRESULT { Refresh(); return S_OK; }).Get());
}

void Extensions::ToggleEnabled(size_t index) {
    if (index >= m_items.size()) return;
    BOOL want = m_items[index].enabled ? FALSE : TRUE;
    m_items[index].ext->Enable(
        want,
        Callback<ICoreWebView2BrowserExtensionEnableCompletedHandler>(
            [this](HRESULT) -> HRESULT { Refresh(); return S_OK; }).Get());
}
