#include "engine/Settings.h"

#include <windows.h>

#include <filesystem>
#include <fstream>

#include "core/Text.h"

namespace engine
{

std::wstring ExeDirectory()
{
    wchar_t buf[MAX_PATH + 1]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf, n);
    size_t pos = path.find_last_of(L'\\');
    return pos == std::wstring::npos ? path : path.substr(0, pos);
}

bool ReadFileUtf8(const std::wstring& path, std::string& out, std::wstring* error)
{
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    if (!f)
    {
        if (error)
            *error = L"cannot open file for reading";
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (out.size() >= 3 && out[0] == '\xEF' && out[1] == '\xBB' && out[2] == '\xBF')
        out.erase(0, 3); // tolerate a UTF-8 BOM
    return true;
}

bool WriteFileUtf8(const std::wstring& path, std::string_view content, std::wstring* error)
{
    std::ofstream f(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!f)
    {
        if (error)
            *error = L"cannot open file for writing";
        return false;
    }
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    f.flush();
    if (!f)
    {
        if (error)
            *error = L"write failed";
        return false;
    }
    return true;
}

SettingsStore::SettingsStore(std::wstring filePath)
    : filePath_(std::move(filePath))
{
}

std::wstring SettingsStore::Load()
{
    std::string text;
    if (!ReadFileUtf8(filePath_, text))
    {
        config = core::AppConfig{};
        return L"No settings file found at " + filePath_ + L" - using defaults";
    }
    auto parsed = core::ConfigFromJson(text);
    if (!parsed.ok)
    {
        config = core::AppConfig{};
        return L"Settings file is invalid (" + parsed.error + L") - using defaults";
    }
    config = std::move(parsed.config);
    return L"Settings loaded from " + filePath_;
}

bool SettingsStore::Save(std::wstring* error)
{
    return WriteFileUtf8(filePath_, core::ConfigToJson(config), error);
}

}
