#pragma once
#include <string>

#include "core/SettingsModel.h"

namespace engine
{

// Directory containing the running executable (no trailing backslash).
std::wstring ExeDirectory();

// settings.json beside the exe; saved on every change.
class SettingsStore
{
public:
    explicit SettingsStore(std::wstring filePath);

    // Loads the file. Missing or corrupt file -> defaults (never throws).
    // Returns a human-readable description of what happened, for the log.
    std::wstring Load();

    // Persists the current config. Returns false + reason on I/O failure.
    bool Save(std::wstring* error = nullptr);

    const std::wstring& FilePath() const { return filePath_; }

    core::AppConfig config;

private:
    std::wstring filePath_;
};

// Whole-file helpers (UTF-8). Used for settings, profiles and meta files.
bool ReadFileUtf8(const std::wstring& path, std::string& out, std::wstring* error = nullptr);
bool WriteFileUtf8(const std::wstring& path, std::string_view content, std::wstring* error = nullptr);

}
