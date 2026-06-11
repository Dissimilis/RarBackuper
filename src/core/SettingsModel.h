#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "core/ExcludeRules.h"

namespace core
{

enum class CompressionLevel
{
    Store,
    Fast,
    Normal,
    Best,
};

// 0 / 1 / 3 / 5 for -m<level>
int CompressionSwitchValue(CompressionLevel level);

const wchar_t* CompressionLevelName(CompressionLevel level);

// The whole persisted configuration (settings.json and *.rbprofile share
// this schema). Deliberately has NO password field: the password is
// session-only and must never reach disk.
struct AppConfig
{
    std::vector<std::wstring> folders;
    std::wstring backupName;
    std::wstring destination;
    CompressionLevel level = CompressionLevel::Normal;
    bool solid = false;
    bool recoveryRecord = true; // -rr1; on by default
    std::vector<ExcludeRule> excludeRules = DefaultExcludeRules();
    bool capsuleSystemInfo = false;
    bool capsuleFileInventory = false;
    bool capsuleBookmarks = false;
    bool capsuleImportantStuff = false;
};

// Pretty-printed UTF-8 JSON.
std::string ConfigToJson(const AppConfig& config);

struct ConfigParseResult
{
    bool ok = false;
    AppConfig config;     // valid only when ok
    std::wstring error;   // human-readable reason when !ok
};

// Parses and validates. Absent optional fields fall back to defaults;
// unknown extra fields are tolerated; malformed JSON / wrong types /
// invalid enum strings are rejected with a reason.
ConfigParseResult ConfigFromJson(std::string_view json);

}
