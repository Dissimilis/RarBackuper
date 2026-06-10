#include "engine/detectors/Detectors.h"

#include <windows.h>

#include <filesystem>
#include <format>
#include <vector>

namespace fs = std::filesystem;

namespace engine::detectors
{

namespace
{

std::wstring EnvVar(const wchar_t* name)
{
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH);
    return n > 0 && n < MAX_PATH ? std::wstring(buf, n) : L"";
}

// Copy with a short retry for files locked by a running browser.
bool CopyWithRetry(const std::wstring& from, const std::wstring& to)
{
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        if (CopyFileW(from.c_str(), to.c_str(), FALSE))
            return true;
        Sleep(400);
    }
    return false;
}

std::wstring SanitizeProfileName(std::wstring s)
{
    for (auto& c : s)
        if (c == L' ' || c == L'\\' || c == L'/')
            c = L'_';
    return s;
}

struct ChromiumBrowser
{
    const wchar_t* name;
    const wchar_t* envVar;
    const wchar_t* relativeUserData;
};

}

void CollectBookmarks(const std::wstring& bookmarksDir, EventSink& sink,
                      std::atomic<bool>& cancel)
{
    sink.OnLog(LogSeverity::Info, L"Collecting browser bookmarks...");
    std::error_code ec;
    fs::create_directories(bookmarksDir, ec);

    int found = 0;
    auto copyStore = [&](const std::wstring& browser, const std::wstring& profile,
                         const fs::path& file)
    {
        std::wstring target = bookmarksDir + L"\\" + browser + L"-" +
                              SanitizeProfileName(profile) + L"-" + file.filename().wstring();
        if (CopyWithRetry(file.wstring(), target))
        {
            ++found;
            sink.OnLog(LogSeverity::Info, L"  bookmarks: " + browser + L" (" + profile + L")");
        }
        else
        {
            sink.OnLog(LogSeverity::Warn, L"  bookmarks: could not copy locked file " +
                                              file.wstring() + L" - skipped");
        }
    };

    const ChromiumBrowser chromiumBrowsers[] = {
        {L"Chrome", L"LOCALAPPDATA", L"\\Google\\Chrome\\User Data"},
        {L"Edge", L"LOCALAPPDATA", L"\\Microsoft\\Edge\\User Data"},
        {L"Brave", L"LOCALAPPDATA", L"\\BraveSoftware\\Brave-Browser\\User Data"},
        {L"Vivaldi", L"LOCALAPPDATA", L"\\Vivaldi\\User Data"},
        {L"Opera", L"APPDATA", L"\\Opera Software\\Opera Stable"},
    };

    for (const auto& b : chromiumBrowsers)
    {
        if (cancel)
            return;
        std::wstring base = EnvVar(b.envVar);
        if (base.empty())
            continue;
        fs::path userData = base + b.relativeUserData;
        if (!fs::exists(userData, ec))
            continue;
        // Opera keeps Bookmarks directly in the profile dir; Chrome-likes
        // keep them in Default \ Profile N subdirectories.
        fs::path direct = userData / L"Bookmarks";
        if (fs::exists(direct, ec))
            copyStore(b.name, L"Default", direct);
        for (const auto& entry : fs::directory_iterator(userData, ec))
        {
            if (!entry.is_directory(ec))
                continue;
            fs::path bm = entry.path() / L"Bookmarks";
            if (fs::exists(bm, ec))
                copyStore(b.name, entry.path().filename().wstring(), bm);
        }
    }

    // Firefox: places.sqlite per profile
    if (!cancel)
    {
        std::wstring appData = EnvVar(L"APPDATA");
        if (!appData.empty())
        {
            fs::path profiles = appData + L"\\Mozilla\\Firefox\\Profiles";
            if (fs::exists(profiles, ec))
            {
                for (const auto& entry : fs::directory_iterator(profiles, ec))
                {
                    if (cancel)
                        return;
                    if (!entry.is_directory(ec))
                        continue;
                    fs::path places = entry.path() / L"places.sqlite";
                    if (fs::exists(places, ec))
                        copyStore(L"Firefox", entry.path().filename().wstring(), places);
                }
            }
        }
    }

    sink.OnLog(LogSeverity::Info,
               std::format(L"Browser bookmarks: {} store(s) collected", found));
}

}
