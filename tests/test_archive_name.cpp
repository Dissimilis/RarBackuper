#include <doctest/doctest.h>
#include "core/ArchiveName.h"

using namespace core;

TEST_CASE("archive file name is <Name>_<yyyy-MM-dd_HHmm>.rar")
{
    ArchiveTime t{2026, 6, 10, 9, 30};
    CHECK(MakeArchiveFileName(L"Docs", t) == L"Docs_2026-06-10_0930.rar");
}

TEST_CASE("single digit fields are zero padded")
{
    ArchiveTime t{2026, 1, 2, 3, 4};
    CHECK(MakeArchiveFileName(L"X", t) == L"X_2026-01-02_0304.rar");
}

TEST_CASE("full path joins destination and file name")
{
    ArchiveTime t{2026, 6, 10, 9, 30};
    CHECK(MakeArchivePath(L"D:\\Backups", L"Docs", t) == L"D:\\Backups\\Docs_2026-06-10_0930.rar");
    CHECK(MakeArchivePath(L"D:\\Backups\\", L"Docs", t) == L"D:\\Backups\\Docs_2026-06-10_0930.rar");
}

TEST_CASE("invalid filename characters in the backup name are sanitized to underscore")
{
    CHECK(SanitizeBackupName(L"My<Doc>s:ok") == L"My_Doc_s_ok");
    CHECK(SanitizeBackupName(L"a/b\\c|d?e*f\"g") == L"a_b_c_d_e_f_g");
    // control characters too
    CHECK(SanitizeBackupName(L"a\tb") == L"a_b");
}

TEST_CASE("sanitize trims leading/trailing whitespace and trailing dots")
{
    CHECK(SanitizeBackupName(L"  Docs. ") == L"Docs");
}

TEST_CASE("sanitize of a clean name is identity")
{
    CHECK(SanitizeBackupName(L"Photos 2026") == L"Photos 2026");
}

TEST_CASE("name that sanitizes to empty yields empty (caller must reject)")
{
    CHECK(SanitizeBackupName(L"...") == L"");
    CHECK(SanitizeBackupName(L"   ") == L"");
}
