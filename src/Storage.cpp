#include "Storage.h"
#include "Util.h"

#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <algorithm>

#include <json.hpp>
using json = nlohmann::json;

namespace {

std::wstring RoamingAppData() {
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path))) {
        result = path;
    }
    if (path) CoTaskMemFree(path);
    return result;
}

std::string ReadFile(const std::wstring& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Write atomically: dump to a temp file in the same directory, then replace.
void WriteFileAtomic(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".tmp";
    {
        std::ofstream out(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(data.data(), (std::streamsize)data.size());
    }
    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

std::string MakeId() {
    static long long counter = 0;
    return std::to_string(util::NowMs()) + "-" + std::to_string(counter++);
}

} // namespace

Storage& Storage::Instance() {
    static Storage s;
    return s;
}

std::wstring Storage::WebViewUserDataDir() const {
    return m_dataDir + L"\\WebView2";
}

void Storage::Load() {
    m_dataDir = RoamingAppData() + L"\\iwser";
    CreateDirectoryW(m_dataDir.c_str(), nullptr);

    // ---- config.json ----
    try {
        std::string raw = ReadFile(m_dataDir + L"\\config.json");
        if (!raw.empty()) {
            json j = json::parse(raw);
            m_config.theme        = j.value("theme", m_config.theme);
            m_config.tabPosition  = j.value("tabPosition", m_config.tabPosition);
            m_config.homepage     = j.value("homepage", m_config.homepage);
            m_config.searchEngine = j.value("searchEngine", m_config.searchEngine);
            m_config.restoreTabs  = j.value("restoreTabs", m_config.restoreTabs);
            m_config.downloadDir  = j.value("downloadDir", m_config.downloadDir);
            m_config.railWidth    = j.value("railWidth", m_config.railWidth);
        }
    } catch (...) { /* keep defaults on malformed config */ }
    SaveConfig();  // normalise / create the file on first run

    // ---- bookmarks.json ----
    try {
        std::string raw = ReadFile(m_dataDir + L"\\bookmarks.json");
        if (!raw.empty()) {
            json j = json::parse(raw);
            for (const auto& b : j) {
                Bookmark bm;
                bm.id    = b.value("id", MakeId());
                bm.title = b.value("title", std::string());
                bm.url   = b.value("url", std::string());
                bm.added = b.value("added", 0LL);
                if (!bm.url.empty()) m_bookmarks.push_back(std::move(bm));
            }
        }
    } catch (...) { m_bookmarks.clear(); }

    // ---- history.json (stored newest-first) ----
    try {
        std::string raw = ReadFile(m_dataDir + L"\\history.json");
        if (!raw.empty()) {
            json j = json::parse(raw);
            for (const auto& h : j) {
                HistoryEntry e;
                e.url       = h.value("url", std::string());
                e.title     = h.value("title", std::string());
                e.visitedAt = h.value("visitedAt", 0LL);
                if (!e.url.empty()) m_history.push_back(std::move(e));
            }
        }
    } catch (...) { m_history.clear(); }
}

void Storage::SaveConfig() {
    json j;
    j["theme"]        = m_config.theme;
    j["tabPosition"]  = m_config.tabPosition;
    j["homepage"]     = m_config.homepage;
    j["searchEngine"] = m_config.searchEngine;
    j["restoreTabs"]  = m_config.restoreTabs;
    j["downloadDir"]  = m_config.downloadDir;
    j["railWidth"]    = m_config.railWidth;
    WriteFileAtomic(m_dataDir + L"\\config.json", j.dump(2));
}

void Storage::SaveBookmarks() {
    json arr = json::array();
    for (const auto& b : m_bookmarks) {
        arr.push_back({ {"id", b.id}, {"title", b.title}, {"url", b.url}, {"added", b.added} });
    }
    WriteFileAtomic(m_dataDir + L"\\bookmarks.json", arr.dump(2));
}

void Storage::SaveHistory() {
    json arr = json::array();
    for (const auto& h : m_history) {
        arr.push_back({ {"url", h.url}, {"title", h.title}, {"visitedAt", h.visitedAt} });
    }
    WriteFileAtomic(m_dataDir + L"\\history.json", arr.dump(2));
}

void Storage::AddBookmark(const std::string& title, const std::string& url) {
    if (url.empty() || IsBookmarked(url)) return;
    Bookmark b;
    b.id    = MakeId();
    b.title = title.empty() ? url : title;
    b.url   = url;
    b.added = util::NowMs();
    m_bookmarks.push_back(std::move(b));
    SaveBookmarks();
}

void Storage::RemoveBookmarkByUrl(const std::string& url) {
    auto it = std::remove_if(m_bookmarks.begin(), m_bookmarks.end(),
                             [&](const Bookmark& b) { return b.url == url; });
    if (it != m_bookmarks.end()) {
        m_bookmarks.erase(it, m_bookmarks.end());
        SaveBookmarks();
    }
}

bool Storage::IsBookmarked(const std::string& url) const {
    for (const auto& b : m_bookmarks)
        if (b.url == url) return true;
    return false;
}

void Storage::AddHistory(const std::string& url, const std::string& title) {
    if (url.empty()) return;
    // Collapse consecutive duplicates of the same URL.
    if (!m_history.empty() && m_history.front().url == url) {
        m_history.front().title     = title;
        m_history.front().visitedAt = util::NowMs();
    } else {
        HistoryEntry e;
        e.url       = url;
        e.title     = title;
        e.visitedAt = util::NowMs();
        m_history.insert(m_history.begin(), std::move(e));
        if (m_history.size() > kHistoryCap) m_history.resize(kHistoryCap);
    }
    SaveHistory();
}

void Storage::ClearHistory() {
    m_history.clear();
    SaveHistory();
}
