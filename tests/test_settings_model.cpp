#include <doctest/doctest.h>
#include "core/SettingsModel.h"

using namespace core;

TEST_CASE("round-trip preserves every field including non-ASCII")
{
    AppConfig c;
    c.folders = {L"C:\\Dokumentai", L"D:\\Nuotraukos žiemą"};
    c.backupName = L"Mano atsarginė";
    c.destination = L"E:\\Atsarginės";
    c.level = CompressionLevel::Best;
    c.solid = true;
    c.excludeRules = {{RuleType::Folder, L"node_modules"},
                      {RuleType::File, L"Thumbs.db"},
                      {RuleType::Pattern, L"*.log"}};
    c.capsuleSystemInfo = true;
    c.capsuleFileInventory = false;
    c.capsuleBookmarks = true;
    c.capsuleImportantStuff = false;

    std::string json = ConfigToJson(c);
    auto r = ConfigFromJson(json);
    REQUIRE(r.ok);
    CHECK(r.config.folders == c.folders);
    CHECK(r.config.backupName == c.backupName);
    CHECK(r.config.destination == c.destination);
    CHECK(r.config.level == c.level);
    CHECK(r.config.solid == c.solid);
    REQUIRE(r.config.excludeRules.size() == 3);
    CHECK(r.config.excludeRules[0].type == RuleType::Folder);
    CHECK(r.config.excludeRules[0].value == L"node_modules");
    CHECK(r.config.excludeRules[2].type == RuleType::Pattern);
    CHECK(r.config.capsuleSystemInfo == true);
    CHECK(r.config.capsuleFileInventory == false);
    CHECK(r.config.capsuleBookmarks == true);
    CHECK(r.config.capsuleImportantStuff == false);
}

TEST_CASE("the persisted schema has no password field")
{
    AppConfig c;
    std::string json = ConfigToJson(c);
    CHECK(json.find("password") == std::string::npos);
    CHECK(json.find("Password") == std::string::npos);
}

TEST_CASE("fresh-install defaults")
{
    AppConfig c;
    CHECK(c.folders.empty());
    CHECK(c.backupName.empty());
    CHECK(c.destination.empty());
    CHECK(c.level == CompressionLevel::Normal);
    CHECK(c.solid == false);
    CHECK(c.excludeRules.size() == DefaultExcludeRules().size());
    CHECK(c.capsuleSystemInfo == false);
    CHECK(c.capsuleFileInventory == false);
    CHECK(c.capsuleBookmarks == false);
    CHECK(c.capsuleImportantStuff == false);
}

TEST_CASE("empty JSON object yields defaults")
{
    auto r = ConfigFromJson("{}");
    REQUIRE(r.ok);
    CHECK(r.config.level == CompressionLevel::Normal);
    CHECK(r.config.excludeRules.size() == DefaultExcludeRules().size());
}

TEST_CASE("malformed JSON is rejected with a reason")
{
    auto r = ConfigFromJson("{ not json !!!");
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
}

TEST_CASE("non-object JSON is rejected")
{
    auto r = ConfigFromJson("[1,2,3]");
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
}

TEST_CASE("wrong field types are rejected with a reason")
{
    auto r = ConfigFromJson(R"({"folders": 42})");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find(L"folders") != std::wstring::npos);

    auto r2 = ConfigFromJson(R"({"solid": "yes"})");
    CHECK_FALSE(r2.ok);

    auto r3 = ConfigFromJson(R"({"excludeRules": [{"type":"bogus","value":"x"}]})");
    CHECK_FALSE(r3.ok);
}

TEST_CASE("unknown extra fields are tolerated")
{
    auto r = ConfigFromJson(R"({"backupName":"X","futureField":{"a":1}})");
    REQUIRE(r.ok);
    CHECK(r.config.backupName == L"X");
}

TEST_CASE("unknown compression level string is rejected")
{
    auto r = ConfigFromJson(R"({"compressionLevel":"Turbo"})");
    CHECK_FALSE(r.ok);
}

TEST_CASE("compression level strings parse")
{
    CHECK(ConfigFromJson(R"({"compressionLevel":"Store"})").config.level == CompressionLevel::Store);
    CHECK(ConfigFromJson(R"({"compressionLevel":"Fast"})").config.level == CompressionLevel::Fast);
    CHECK(ConfigFromJson(R"({"compressionLevel":"Normal"})").config.level == CompressionLevel::Normal);
    CHECK(ConfigFromJson(R"({"compressionLevel":"Best"})").config.level == CompressionLevel::Best);
}
