#include <doctest/doctest.h>
#include "core/RarCommandLine.h"

using namespace core;

static AppConfig baseConfig()
{
    AppConfig c;
    c.folders = {L"C:\\Data\\Docs"};
    c.backupName = L"Docs";
    c.destination = L"D:\\Backups";
    c.level = CompressionLevel::Normal;
    c.solid = false;
    c.excludeRules = {};
    return c;
}

TEST_CASE("basic command shape")
{
    auto cmd = BuildRarCommand(L"C:\\tools\\Rar.exe", baseConfig(),
                               L"D:\\Backups\\Docs_2026-06-10_0930.rar", L"", L"");
    CHECK(cmd.commandLine ==
          L"\"C:\\tools\\Rar.exe\" a -m3 -rr1 -scFR -idp -y -- "
          L"\"D:\\Backups\\Docs_2026-06-10_0930.rar\" \"C:\\Data\\Docs\"");
}

TEST_CASE("compression levels map to -m0/-m1/-m3/-m5")
{
    auto cfg = baseConfig();
    cfg.level = CompressionLevel::Store;
    CHECK(BuildRarCommand(L"r", cfg, L"a.rar", L"", L"").commandLine.find(L" -m0 ") != std::wstring::npos);
    cfg.level = CompressionLevel::Fast;
    CHECK(BuildRarCommand(L"r", cfg, L"a.rar", L"", L"").commandLine.find(L" -m1 ") != std::wstring::npos);
    cfg.level = CompressionLevel::Normal;
    CHECK(BuildRarCommand(L"r", cfg, L"a.rar", L"", L"").commandLine.find(L" -m3 ") != std::wstring::npos);
    cfg.level = CompressionLevel::Best;
    CHECK(BuildRarCommand(L"r", cfg, L"a.rar", L"", L"").commandLine.find(L" -m5 ") != std::wstring::npos);
}

TEST_CASE("solid flag emits -s")
{
    auto cfg = baseConfig();
    cfg.solid = true;
    auto cmd = BuildRarCommand(L"r", cfg, L"a.rar", L"", L"");
    CHECK(cmd.commandLine.find(L" -s ") != std::wstring::npos);
}

TEST_CASE("password emits -hp and is masked in the loggable string")
{
    auto cmd = BuildRarCommand(L"r", baseConfig(), L"a.rar", L"", L"s3cret");
    CHECK(cmd.commandLine.find(L"-hps3cret") != std::wstring::npos);
    CHECK(cmd.loggable.find(L"s3cret") == std::wstring::npos);
    CHECK(cmd.loggable.find(L"-hp***") != std::wstring::npos);
}

TEST_CASE("no password means no -hp anywhere")
{
    auto cmd = BuildRarCommand(L"r", baseConfig(), L"a.rar", L"", L"");
    CHECK(cmd.commandLine.find(L"-hp") == std::wstring::npos);
}

TEST_CASE("comment file emits -z and switches comment charset to UTF-8")
{
    auto cmd = BuildRarCommand(L"r", baseConfig(), L"a.rar", L"D:\\Backups\\_meta\\comment.txt", L"");
    CHECK(cmd.commandLine.find(L"-zD:\\Backups\\_meta\\comment.txt") != std::wstring::npos);
    CHECK(cmd.commandLine.find(L"-scFCR") != std::wstring::npos);
}

TEST_CASE("exclude rules are translated and emitted as -x switches")
{
    auto cfg = baseConfig();
    cfg.excludeRules = {{RuleType::Folder, L"node_modules"},
                        {RuleType::File, L"Thumbs.db"},
                        {RuleType::Pattern, L"*.log"}};
    auto cmd = BuildRarCommand(L"r", cfg, L"a.rar", L"", L"");
    CHECK(cmd.commandLine.find(L"-x*\\node_modules\\") != std::wstring::npos);
    CHECK(cmd.commandLine.find(L"-x*\\Thumbs.db") != std::wstring::npos);
    CHECK(cmd.commandLine.find(L"-x*.log") != std::wstring::npos);
}

TEST_CASE("-w is never emitted")
{
    auto cfg = baseConfig();
    cfg.excludeRules = DefaultExcludeRules();
    auto cmd = BuildRarCommand(L"C:\\tools\\Rar.exe", cfg,
                               L"D:\\Backups\\a.rar", L"D:\\Backups\\_meta\\c.txt", L"pw");
    CHECK(cmd.commandLine.find(L" -w") == std::wstring::npos);
}

TEST_CASE("paths with spaces are quoted; switches with spaces are quoted whole")
{
    auto cfg = baseConfig();
    cfg.folders = {L"C:\\My Documents\\stuff"};
    cfg.excludeRules = {{RuleType::Folder, L"C:\\My Documents\\stuff\\old logs"}};
    auto cmd = BuildRarCommand(L"C:\\Program Files\\WinRAR\\Rar.exe", cfg,
                               L"D:\\My Backups\\a b.rar", L"", L"");
    CHECK(cmd.commandLine.find(L"\"C:\\Program Files\\WinRAR\\Rar.exe\"") != std::wstring::npos);
    CHECK(cmd.commandLine.find(L"\"D:\\My Backups\\a b.rar\"") != std::wstring::npos);
    CHECK(cmd.commandLine.find(L"\"C:\\My Documents\\stuff\"") != std::wstring::npos);
    CHECK(cmd.commandLine.find(L"\"-xC:\\My Documents\\stuff\\old logs\"") != std::wstring::npos);
}

TEST_CASE("source folder with a trailing backslash does not break quoting")
{
    auto cfg = baseConfig();
    cfg.folders = {L"C:\\Data\\"};
    auto cmd = BuildRarCommand(L"r", cfg, L"a.rar", L"", L"");
    // trailing backslash must not escape the closing quote
    CHECK(cmd.commandLine.find(L"\"C:\\Data\"") != std::wstring::npos);
}

TEST_CASE("multiple folders are all appended after --")
{
    auto cfg = baseConfig();
    cfg.folders = {L"C:\\A", L"C:\\B", L"C:\\C"};
    auto cmd = BuildRarCommand(L"r", cfg, L"a.rar", L"", L"");
    size_t dashes = cmd.commandLine.find(L" -- ");
    REQUIRE(dashes != std::wstring::npos);
    CHECK(cmd.commandLine.find(L"\"C:\\A\"", dashes) != std::wstring::npos);
    CHECK(cmd.commandLine.find(L"\"C:\\B\"", dashes) != std::wstring::npos);
    CHECK(cmd.commandLine.find(L"\"C:\\C\"", dashes) != std::wstring::npos);
}
