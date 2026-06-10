#include <doctest/doctest.h>
#include "core/RarExitCodes.h"

using namespace core;

TEST_CASE("known exit codes map to friendly messages")
{
    CHECK(RarExitCodeMessage(0) == L"Success");
    CHECK(RarExitCodeMessage(1).find(L"Non-fatal error") != std::wstring::npos);
    CHECK(RarExitCodeMessage(2).find(L"Fatal error") != std::wstring::npos);
    CHECK(RarExitCodeMessage(3).find(L"checksum") != std::wstring::npos);
    CHECK(RarExitCodeMessage(4).find(L"locked") != std::wstring::npos);
    CHECK(RarExitCodeMessage(5).find(L"Write error") != std::wstring::npos);
    CHECK(RarExitCodeMessage(6).find(L"open error") != std::wstring::npos);
    CHECK(RarExitCodeMessage(7).find(L"command line") != std::wstring::npos);
    CHECK(RarExitCodeMessage(8).find(L"memory") != std::wstring::npos);
    CHECK(RarExitCodeMessage(9).find(L"create error") != std::wstring::npos);
    CHECK(RarExitCodeMessage(10).find(L"No files") != std::wstring::npos);
    CHECK(RarExitCodeMessage(11).find(L"password") != std::wstring::npos);
    CHECK(RarExitCodeMessage(12).find(L"Read error") != std::wstring::npos);
    CHECK(RarExitCodeMessage(13).find(L"Bad archive") != std::wstring::npos);
    CHECK(RarExitCodeMessage(255).find(L"stopped") != std::wstring::npos);
}

TEST_CASE("unknown exit codes get a generic message including the number")
{
    CHECK(RarExitCodeMessage(42).find(L"42") != std::wstring::npos);
    CHECK(RarExitCodeMessage(42).find(L"nknown") != std::wstring::npos);
}

TEST_CASE("classification: 0 success, 1 warning, 255 cancel, rest errors")
{
    CHECK(ClassifyRarExitCode(0) == RarStatus::Success);
    CHECK(ClassifyRarExitCode(1) == RarStatus::Warning);
    CHECK(ClassifyRarExitCode(255) == RarStatus::Cancelled);
    CHECK(ClassifyRarExitCode(2) == RarStatus::Error);
    CHECK(ClassifyRarExitCode(11) == RarStatus::Error);
    CHECK(ClassifyRarExitCode(42) == RarStatus::Error);
}
