#include "core/SettingsModel.h"
#include "core/Text.h"

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace core
{

namespace
{

const char* RuleTypeName(RuleType t)
{
    switch (t)
    {
    case RuleType::Folder:  return "folder";
    case RuleType::File:    return "file";
    case RuleType::Pattern: return "pattern";
    }
    return "folder";
}

const char* LevelName(CompressionLevel l)
{
    switch (l)
    {
    case CompressionLevel::Store:  return "Store";
    case CompressionLevel::Fast:   return "Fast";
    case CompressionLevel::Normal: return "Normal";
    case CompressionLevel::Best:   return "Best";
    }
    return "Normal";
}

struct ValidationError
{
    std::wstring message;
};

std::wstring RequireString(const json& j, const char* field)
{
    if (!j.is_string())
        throw ValidationError{L"field '" + Utf8ToWide(field) + L"' must be a string"};
    return Utf8ToWide(j.get<std::string>());
}

bool RequireBool(const json& j, const char* field)
{
    if (!j.is_boolean())
        throw ValidationError{L"field '" + Utf8ToWide(field) + L"' must be a boolean"};
    return j.get<bool>();
}

}

int CompressionSwitchValue(CompressionLevel level)
{
    switch (level)
    {
    case CompressionLevel::Store:  return 0;
    case CompressionLevel::Fast:   return 1;
    case CompressionLevel::Normal: return 3;
    case CompressionLevel::Best:   return 5;
    }
    return 3;
}

const wchar_t* CompressionLevelName(CompressionLevel level)
{
    switch (level)
    {
    case CompressionLevel::Store:  return L"Store";
    case CompressionLevel::Fast:   return L"Fast";
    case CompressionLevel::Normal: return L"Normal";
    case CompressionLevel::Best:   return L"Best";
    }
    return L"Normal";
}

std::string ConfigToJson(const AppConfig& config)
{
    json j;
    json folders = json::array();
    for (const auto& f : config.folders)
        folders.push_back(WideToUtf8(f));
    j["folders"] = std::move(folders);
    j["backupName"] = WideToUtf8(config.backupName);
    j["destination"] = WideToUtf8(config.destination);
    j["compressionLevel"] = LevelName(config.level);
    j["solid"] = config.solid;
    json rules = json::array();
    for (const auto& r : config.excludeRules)
        rules.push_back(json{{"type", RuleTypeName(r.type)}, {"value", WideToUtf8(r.value)}});
    j["excludeRules"] = std::move(rules);
    j["timeCapsule"] = json{
        {"systemInfo", config.capsuleSystemInfo},
        {"fileInventory", config.capsuleFileInventory},
        {"bookmarks", config.capsuleBookmarks},
        {"importantStuff", config.capsuleImportantStuff},
    };
    return j.dump(2);
}

ConfigParseResult ConfigFromJson(std::string_view text)
{
    ConfigParseResult result;
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded())
    {
        result.error = L"not valid JSON";
        return result;
    }
    if (!j.is_object())
    {
        result.error = L"top-level JSON value must be an object";
        return result;
    }

    AppConfig c;
    try
    {
        if (j.contains("folders"))
        {
            const json& arr = j["folders"];
            if (!arr.is_array())
                throw ValidationError{L"field 'folders' must be an array of strings"};
            c.folders.clear();
            for (const json& e : arr)
                c.folders.push_back(RequireString(e, "folders[]"));
        }
        if (j.contains("backupName"))
            c.backupName = RequireString(j["backupName"], "backupName");
        if (j.contains("destination"))
            c.destination = RequireString(j["destination"], "destination");
        if (j.contains("compressionLevel"))
        {
            std::wstring s = RequireString(j["compressionLevel"], "compressionLevel");
            if (s == L"Store")       c.level = CompressionLevel::Store;
            else if (s == L"Fast")   c.level = CompressionLevel::Fast;
            else if (s == L"Normal") c.level = CompressionLevel::Normal;
            else if (s == L"Best")   c.level = CompressionLevel::Best;
            else
                throw ValidationError{L"unknown compressionLevel '" + s + L"'"};
        }
        if (j.contains("solid"))
            c.solid = RequireBool(j["solid"], "solid");
        if (j.contains("excludeRules"))
        {
            const json& arr = j["excludeRules"];
            if (!arr.is_array())
                throw ValidationError{L"field 'excludeRules' must be an array"};
            c.excludeRules.clear();
            for (const json& e : arr)
            {
                if (!e.is_object())
                    throw ValidationError{L"excludeRules entries must be objects"};
                std::wstring type = RequireString(e.value("type", json()), "excludeRules[].type");
                std::wstring value = RequireString(e.value("value", json()), "excludeRules[].value");
                RuleType t;
                if (type == L"folder")       t = RuleType::Folder;
                else if (type == L"file")    t = RuleType::File;
                else if (type == L"pattern") t = RuleType::Pattern;
                else
                    throw ValidationError{L"unknown exclude rule type '" + type + L"'"};
                c.excludeRules.push_back({t, std::move(value)});
            }
        }
        if (j.contains("timeCapsule"))
        {
            const json& tc = j["timeCapsule"];
            if (!tc.is_object())
                throw ValidationError{L"field 'timeCapsule' must be an object"};
            if (tc.contains("systemInfo"))
                c.capsuleSystemInfo = RequireBool(tc["systemInfo"], "timeCapsule.systemInfo");
            if (tc.contains("fileInventory"))
                c.capsuleFileInventory = RequireBool(tc["fileInventory"], "timeCapsule.fileInventory");
            if (tc.contains("bookmarks"))
                c.capsuleBookmarks = RequireBool(tc["bookmarks"], "timeCapsule.bookmarks");
            if (tc.contains("importantStuff"))
                c.capsuleImportantStuff = RequireBool(tc["importantStuff"], "timeCapsule.importantStuff");
        }
    }
    catch (const ValidationError& e)
    {
        result.error = e.message;
        return result;
    }

    result.ok = true;
    result.config = std::move(c);
    return result;
}

}
