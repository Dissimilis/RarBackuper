#include <doctest/doctest.h>
#include "core/DetectorCatalog.h"

using namespace core;

TEST_CASE("catalog has all 13 groups in order")
{
    auto groups = DetectorGroups();
    std::vector<std::wstring> expected = {
        L"Passwords & vaults",
        L"Keys & certificates",
        L"Crypto wallets",
        L"VPN & remote access",
        L"Developer credentials",
        L"Git & dev identity",
        L"IDE & editor settings",
        L"Shell & terminal",
        L"Email essentials",
        L"Notes apps",
        L"App configs",
        L"Windows recovery info",
        L"Sticky Notes",
    };
    CHECK(groups == expected);
}

TEST_CASE("every detector entry is complete")
{
    for (const auto& d : DetectorCatalog())
    {
        CAPTURE(d.name);
        CHECK_FALSE(d.name.empty());
        CHECK_FALSE(d.description.empty());
        CHECK_FALSE(d.group.empty());
        CHECK_FALSE(d.target.empty());
        CHECK_FALSE(d.restore.empty());
        CHECK(d.sizeCapBytes > 0);
        CHECK((d.kind == DetectorKind::FileGlob || d.kind == DetectorKind::RegistryExport ||
               d.kind == DetectorKind::CommandOutput));
        if (d.kind != DetectorKind::FileGlob)
            CHECK_FALSE(d.output.empty()); // registry/command kinds write a named file
    }
}

TEST_CASE("registry exports target HKCU or HKLM keys")
{
    for (const auto& d : DetectorCatalog())
    {
        if (d.kind != DetectorKind::RegistryExport)
            continue;
        CAPTURE(d.name);
        CHECK((d.target.starts_with(L"HKCU\\") || d.target.starts_with(L"HKLM\\")));
    }
}

TEST_CASE("expected key detectors exist")
{
    auto has = [&](const wchar_t* name)
    {
        for (const auto& d : DetectorCatalog())
            if (d.name == name)
                return true;
        return false;
    };
    CHECK(has(L"KeePass vaults"));
    CHECK(has(L"SSH keys and config"));
    CHECK(has(L"PuTTY sessions"));
    CHECK(has(L"AWS credentials"));
    CHECK(has(L"VS Code extension list"));
    CHECK(has(L"Wi-Fi profiles"));
    CHECK(has(L"Sticky Notes database"));
}

TEST_CASE("default per-file size cap is 10 MB")
{
    int defaults = 0;
    for (const auto& d : DetectorCatalog())
        if (d.sizeCapBytes == 10ull * 1024 * 1024)
            ++defaults;
    CHECK(defaults > 30); // nearly all entries use the default cap
}
