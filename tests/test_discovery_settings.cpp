#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "engine/RarDiscovery.h"
#include "engine/Settings.h"

namespace fs = std::filesystem;

namespace
{

fs::path makeTempDir(const char* name)
{
    fs::path dir = fs::path("test_fixtures") / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

}

TEST_CASE("FindExecutable finds a file case-insensitively within the depth limit")
{
    auto root = makeTempDir("disc1");
    fs::create_directories(root / "a" / "b");
    std::ofstream(root / "a" / "b" / "RAR.EXE") << "x";

    std::vector<std::wstring> searched;
    auto found = engine::FindExecutable(L"rar.exe", {root.wstring()}, 3, &searched);
    CHECK(found.find(L"RAR.EXE") != std::wstring::npos);
    CHECK(searched.size() == 1);
}

TEST_CASE("FindExecutable respects the depth limit")
{
    auto root = makeTempDir("disc2");
    fs::create_directories(root / "1" / "2" / "3" / "4");
    std::ofstream(root / "1" / "2" / "3" / "4" / "rar.exe") << "x";

    auto found = engine::FindExecutable(L"rar.exe", {root.wstring()}, 3, nullptr);
    CHECK(found.empty());
}

TEST_CASE("FindExecutable returns empty for a missing file and lists searched roots")
{
    auto root = makeTempDir("disc3");
    std::vector<std::wstring> searched;
    auto found = engine::FindExecutable(L"rar.exe", {root.wstring(), L"Z:\\no\\such\\dir"}, 3, &searched);
    CHECK(found.empty());
    CHECK(searched.size() == 2);
}

TEST_CASE("SettingsStore: missing file loads defaults without error")
{
    auto dir = makeTempDir("settings1");
    engine::SettingsStore s((dir / "settings.json").wstring());
    auto msg = s.Load();
    CHECK(msg.find(L"defaults") != std::wstring::npos);
    CHECK(s.config.level == core::CompressionLevel::Normal);
    CHECK(s.config.excludeRules.size() == core::DefaultExcludeRules().size());
}

TEST_CASE("SettingsStore: save then load round-trips")
{
    auto dir = makeTempDir("settings2");
    engine::SettingsStore s((dir / "settings.json").wstring());
    s.Load();
    s.config.backupName = L"Ąžuolas";
    s.config.folders = {L"C:\\Data"};
    s.config.solid = true;
    REQUIRE(s.Save());

    engine::SettingsStore s2((dir / "settings.json").wstring());
    auto msg = s2.Load();
    CHECK(msg.find(L"loaded") != std::wstring::npos);
    CHECK(s2.config.backupName == L"Ąžuolas");
    CHECK(s2.config.folders == std::vector<std::wstring>{L"C:\\Data"});
    CHECK(s2.config.solid == true);
}

TEST_CASE("SettingsStore: corrupt file loads defaults and says why")
{
    auto dir = makeTempDir("settings3");
    std::ofstream(dir / "settings.json") << "{ broken";
    engine::SettingsStore s((dir / "settings.json").wstring());
    auto msg = s.Load();
    CHECK(msg.find(L"invalid") != std::wstring::npos);
    CHECK(msg.find(L"defaults") != std::wstring::npos);
    CHECK(s.config.backupName.empty());
}
