#pragma once
#include <string>
#include <vector>

// Persisted user configuration (config.json).
struct Config {
    std::string theme        = "light";                            // "light" | "dark" | "system"
    std::string tabPosition  = "vertical";                         // "vertical" | "horizontal"
    std::string homepage     = "https://www.google.com";
    std::string searchEngine = "https://www.google.com/search?q=%s";
    bool        restoreTabs  = true;
    std::string downloadDir  = "";                                 // empty -> system Downloads
    int         railWidth    = 240;                                // vertical tab rail width (DIP)
};

struct Bookmark {
    std::string id;
    std::string title;
    std::string url;
    long long   added = 0;
};

struct HistoryEntry {
    std::string url;
    std::string title;
    long long   visitedAt = 0;
};

// Singleton facade over the three JSON files under %APPDATA%\iwser.
// All strings are UTF-8.
class Storage {
public:
    static Storage& Instance();

    void Load();   // create data dir if needed, read all three files

    // ---- config ----
    Config& GetConfig() { return m_config; }
    void SaveConfig();

    // ---- bookmarks ----
    const std::vector<Bookmark>& Bookmarks() const { return m_bookmarks; }
    void AddBookmark(const std::string& title, const std::string& url);
    void RemoveBookmarkByUrl(const std::string& url);
    bool IsBookmarked(const std::string& url) const;

    // ---- history ----
    const std::vector<HistoryEntry>& History() const { return m_history; }  // newest first
    void AddHistory(const std::string& url, const std::string& title);
    void ClearHistory();

    // ---- paths ----
    std::wstring DataDir() const { return m_dataDir; }              // %APPDATA%\iwser
    std::wstring WebViewUserDataDir() const;                        // subfolder for the engine

private:
    Storage() = default;
    void SaveBookmarks();
    void SaveHistory();

    static const size_t kHistoryCap = 5000;

    std::wstring m_dataDir;
    Config       m_config;
    std::vector<Bookmark>     m_bookmarks;
    std::vector<HistoryEntry> m_history;   // kept newest-first
};
