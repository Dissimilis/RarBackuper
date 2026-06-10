#include <doctest/doctest.h>
#include "core/ExcludeRules.h"

#include <algorithm>

using namespace core;

static bool hasRule(const std::vector<ExcludeRule>& rules, RuleType t, const std::wstring& v)
{
    return std::any_of(rules.begin(), rules.end(),
                       [&](const ExcludeRule& r) { return r.type == t && r.value == v; });
}

TEST_CASE("default rule set contains the documented entries and nothing forbidden")
{
    auto d = DefaultExcludeRules();

    for (const wchar_t* f : {L".git", L".svn", L".hg", L"node_modules", L".venv", L"venv",
                             L"packages", L"bin", L"obj", L".vs", L"__pycache__", L"target",
                             L"$RECYCLE.BIN", L"System Volume Information"})
        CHECK_MESSAGE(hasRule(d, RuleType::Folder, f), "missing folder rule");

    CHECK(hasRule(d, RuleType::File, L"Thumbs.db"));
    CHECK(hasRule(d, RuleType::File, L"desktop.ini"));

    CHECK(hasRule(d, RuleType::Pattern, L"*.pyc"));
    CHECK(hasRule(d, RuleType::Pattern, L"*.tmp"));
    CHECK(hasRule(d, RuleType::Pattern, L"~$*"));

    // deliberately NOT excluded
    CHECK_FALSE(hasRule(d, RuleType::Folder, L".vscode"));
    CHECK_FALSE(hasRule(d, RuleType::Folder, L".idea"));
    CHECK_FALSE(hasRule(d, RuleType::Folder, L"dist"));

    CHECK(d.size() == 19);
}

TEST_CASE("rule -> -x mask translation")
{
    CHECK(RuleToMask({RuleType::Folder, L"node_modules"}) == L"*\\node_modules\\");
    CHECK(RuleToMask({RuleType::Folder, L"C:\\src\\proj\\node_modules"}) == L"C:\\src\\proj\\node_modules");
    CHECK(RuleToMask({RuleType::File, L"Thumbs.db"}) == L"*\\Thumbs.db");
    CHECK(RuleToMask({RuleType::File, L"C:\\a\\Thumbs.db"}) == L"C:\\a\\Thumbs.db");
    CHECK(RuleToMask({RuleType::Pattern, L"*.log"}) == L"*.log");
    CHECK(RuleToMask({RuleType::Pattern, L"*\\cache\\*"}) == L"*\\cache\\*");
}

TEST_CASE("local matching: bare folder rule excludes files under any dir of that name")
{
    ExcludeMatcher m({{RuleType::Folder, L"node_modules"}});
    CHECK(m.IsExcludedFile(L"C:\\src\\proj\\node_modules\\x\\y.js"));
    CHECK(m.IsExcludedFile(L"C:\\NODE_MODULES\\a.txt")); // case-insensitive
    CHECK_FALSE(m.IsExcludedFile(L"C:\\src\\proj\\src\\node_modules.txt"));
    CHECK_FALSE(m.IsExcludedFile(L"C:\\src\\node_modulesX\\a.txt"));
}

TEST_CASE("local matching: bare file rule excludes by file name anywhere")
{
    ExcludeMatcher m({{RuleType::File, L"Thumbs.db"}});
    CHECK(m.IsExcludedFile(L"C:\\a\\Thumbs.db"));
    CHECK(m.IsExcludedFile(L"C:\\a\\b\\c\\thumbs.DB"));
    CHECK_FALSE(m.IsExcludedFile(L"C:\\a\\Thumbs.db.bak"));
    // a folder named Thumbs.db is not a file match
    CHECK_FALSE(m.IsExcludedFile(L"C:\\a\\Thumbs.db\\inner.txt"));
}

TEST_CASE("local matching: pattern excludes by wildcard")
{
    ExcludeMatcher m({{RuleType::Pattern, L"*.log"}});
    CHECK(m.IsExcludedFile(L"C:\\x\\y\\app.log"));
    CHECK(m.IsExcludedFile(L"D:\\app.LOG"));
    CHECK_FALSE(m.IsExcludedFile(L"C:\\x\\app.log.txt"));

    ExcludeMatcher m2({{RuleType::Pattern, L"~$*"}});
    CHECK(m2.IsExcludedFile(L"C:\\docs\\~$report.docx"));
    CHECK_FALSE(m2.IsExcludedFile(L"C:\\docs\\report.docx"));

    ExcludeMatcher m3({{RuleType::Pattern, L"*\\cache\\*"}});
    CHECK(m3.IsExcludedFile(L"C:\\a\\cache\\f.bin"));
    CHECK_FALSE(m3.IsExcludedFile(L"C:\\a\\cachex\\f.bin"));
}

TEST_CASE("local matching: full-path folder rule excludes only that path")
{
    ExcludeMatcher m({{RuleType::Folder, L"C:\\src\\proj\\bin"}});
    CHECK(m.IsExcludedFile(L"C:\\src\\proj\\bin\\out.exe"));
    CHECK(m.IsExcludedFile(L"C:\\SRC\\proj\\BIN\\sub\\out.exe"));
    CHECK_FALSE(m.IsExcludedFile(L"C:\\other\\bin\\out.exe"));
    CHECK_FALSE(m.IsExcludedFile(L"C:\\src\\proj\\binx\\out.exe"));
}

TEST_CASE("local matching: full-path file rule excludes exactly that file")
{
    ExcludeMatcher m({{RuleType::File, L"C:\\a\\secret.txt"}});
    CHECK(m.IsExcludedFile(L"C:\\a\\secret.txt"));
    CHECK(m.IsExcludedFile(L"c:\\A\\SECRET.TXT"));
    CHECK_FALSE(m.IsExcludedFile(L"C:\\b\\secret.txt"));
}

TEST_CASE("directory exclusion check for pre-scan descent pruning")
{
    ExcludeMatcher m({{RuleType::Folder, L"node_modules"},
                      {RuleType::Folder, L"C:\\src\\proj\\bin"}});
    CHECK(m.IsExcludedDir(L"C:\\x\\node_modules"));
    CHECK(m.IsExcludedDir(L"C:\\x\\node_modules\\sub"));
    CHECK(m.IsExcludedDir(L"C:\\src\\proj\\bin"));
    CHECK(m.IsExcludedDir(L"C:\\src\\proj\\bin\\sub"));
    CHECK_FALSE(m.IsExcludedDir(L"C:\\src\\proj"));
    CHECK_FALSE(m.IsExcludedDir(L"C:\\other\\bin2"));
}

TEST_CASE("no rules excludes nothing")
{
    ExcludeMatcher m({});
    CHECK_FALSE(m.IsExcludedFile(L"C:\\a\\b.txt"));
    CHECK_FALSE(m.IsExcludedDir(L"C:\\a"));
}
