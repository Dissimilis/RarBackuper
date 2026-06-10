// Integration tests driving the real bundled WinRAR-x64\Rar.exe through the
// engine (BackupRun) and through the built RarBackuper.exe CLI.
#include <doctest/doctest.h>

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <mutex>
#include <vector>

#include "core/RarCommandLine.h"
#include "core/RarExitCodes.h"
#include "engine/RarDiscovery.h"
#include "engine/RarRunner.h"
#include "engine/Settings.h"

namespace fs = std::filesystem;

namespace
{

// ---- locating tools relative to the test exe ----

std::wstring RepoTool(const wchar_t* name)
{
    // The test exe lives in build\; WinRAR-x64 sits beside the repo root.
    fs::path dir = engine::ExeDirectory();
    for (int i = 0; i < 4; ++i)
    {
        std::wstring found = engine::FindExecutable(name, {dir.wstring()}, 2, nullptr);
        if (!found.empty())
            return found;
        if (!dir.has_parent_path())
            break;
        dir = dir.parent_path();
    }
    return L"";
}

std::wstring RarExe()
{
    static std::wstring path = RepoTool(L"Rar.exe");
    return path;
}

std::wstring UnrarExe()
{
    static std::wstring path = RepoTool(L"UnRAR.exe");
    return path;
}

std::wstring BuiltAppExe()
{
    static std::wstring path = engine::ExeDirectory() + L"\\RarBackuper.exe";
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES ? path : L"";
}

// ---- fixtures under the build directory ----

fs::path FixtureDir(const char* name)
{
    fs::path dir = fs::path(engine::ExeDirectory()) / L"test_fixtures" / name;
    fs::remove_all(dir);
    // Windows deletes are asynchronous (delete-pending); retry the recreate
    // briefly so fixture setup cannot race the removal.
    for (int i = 0; i < 20; ++i)
    {
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (!ec && fs::exists(dir))
            return dir;
        Sleep(50);
    }
    REQUIRE(fs::exists(dir));
    return dir;
}

void WriteText(const fs::path& p, const std::string& content)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
}

// ---- a sink that records everything ----

struct RecordingSink : engine::EventSink
{
    std::mutex m;
    std::vector<std::wstring> lines;
    int maxDone = 0;
    int total = 0;
    int addingLines = 0;
    engine::RunSummary summary;
    std::atomic<bool> completed{false};
    std::function<void(int)> onProgress;

    void OnLog(engine::LogSeverity, const std::wstring& text) override
    {
        std::lock_guard lock(m);
        lines.push_back(text);
        if (text.starts_with(L"Adding ") || text.starts_with(L"Adding\t"))
        {
            // real per-item lines carry the OK status; notices do not
            if (text.find(L" OK") != std::wstring::npos)
                ++addingLines;
        }
    }
    void OnProgress(int done, int t) override
    {
        std::lock_guard lock(m);
        maxDone = (std::max)(maxDone, done);
        total = t;
        if (onProgress)
            onProgress(done);
    }
    void OnCurrentFile(const std::wstring&) override {}
    void OnStateChange(engine::RunState) override {}
    void OnCompleted(const engine::RunSummary& s) override
    {
        std::lock_guard lock(m);
        summary = s;
        completed = true;
    }

    bool HasLineContaining(const std::wstring& needle)
    {
        std::lock_guard lock(m);
        for (const auto& l : lines)
            if (l.find(needle) != std::wstring::npos)
                return true;
        return false;
    }
};

// ---- process helpers ----

struct ProcResult
{
    int exitCode = -1;
    std::string output;
};

ProcResult RunCapture(const std::wstring& commandLine, const std::wstring& cwd = L"")
{
    ProcResult result;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    CreatePipe(&readPipe, &writePipe, &sa, 0);
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = commandLine;
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                        cwd.empty() ? nullptr : cwd.c_str(), &si, &pi))
    {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        return result;
    }
    CloseHandle(writePipe);
    CloseHandle(pi.hThread);
    char buf[4096];
    DWORD read = 0;
    while (ReadFile(readPipe, buf, sizeof(buf), &read, nullptr) && read > 0)
        result.output.append(buf, read);
    CloseHandle(readPipe);
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    result.exitCode = static_cast<int>(code);
    return result;
}

std::string ListArchive(const std::wstring& archive, const std::wstring& password = L"")
{
    std::wstring cmd = L"\"" + UnrarExe() + L"\" lb";
    if (!password.empty())
        cmd += L" -p" + password;
    else
        cmd += L" -p-"; // never prompt
    cmd += L" \"" + archive + L"\"";
    return RunCapture(cmd).output;
}

core::AppConfig BaseConfig(const fs::path& src, const fs::path& dest)
{
    core::AppConfig cfg;
    cfg.folders = {src.wstring()};
    cfg.backupName = L"IT";
    cfg.destination = dest.wstring();
    cfg.level = core::CompressionLevel::Normal;
    cfg.excludeRules.clear();
    return cfg;
}

// Runs a BackupRun synchronously and returns the produced archive path.
std::wstring RunBackup(const core::AppConfig& cfg, RecordingSink& sink,
                       const std::wstring& password = L"")
{
    engine::BackupRequest req;
    req.config = cfg;
    req.password = password;
    req.rarExePath = RarExe();
    req.skipCapsule = true;
    REQUIRE(engine::ValidateBackupRequest(req, sink));
    engine::BackupRun run(std::move(req), sink);
    run.Start();
    run.Join();
    REQUIRE(sink.completed.load());
    return sink.summary.archivePath;
}

}

TEST_CASE("integration: tools are discoverable")
{
    REQUIRE_FALSE(RarExe().empty());
    REQUIRE_FALSE(UnrarExe().empty());
}

TEST_CASE("integration: archive is created with the expected name and full recursion")
{
    auto root = FixtureDir("basic");
    auto src = root / "src";
    WriteText(src / "a.txt", "hello");
    WriteText(src / "sub" / "deeper" / "b.txt", "world");
    auto dest = root / "dest";
    fs::create_directories(dest);

    RecordingSink sink;
    std::wstring archive = RunBackup(BaseConfig(src, dest), sink);
    CHECK(sink.summary.outcome == engine::RunOutcome::Success);
    REQUIRE_FALSE(archive.empty());
    CHECK(fs::exists(archive));
    CHECK(fs::path(archive).filename().wstring().starts_with(L"IT_"));
    CHECK(fs::path(archive).extension() == L".rar");

    std::string listing = ListArchive(archive);
    CHECK(listing.find("a.txt") != std::string::npos);
    CHECK(listing.find("b.txt") != std::string::npos);   // recursion into sub\deeper
    CHECK(listing.find("deeper") != std::string::npos);
}

TEST_CASE("integration: exclude masks work end-to-end")
{
    auto root = FixtureDir("excludes");
    auto src = root / "src";
    WriteText(src / "keep.txt", "keep");
    WriteText(src / "node_modules" / "x.js", "junk");
    WriteText(src / "Thumbs.db", "junk");
    WriteText(src / "a.log", "junk");
    WriteText(src / "logsdir" / "b.log", "junk");
    auto dest = root / "dest";
    fs::create_directories(dest);

    auto cfg = BaseConfig(src, dest);
    cfg.excludeRules = {{core::RuleType::Folder, L"node_modules"},
                        {core::RuleType::File, L"Thumbs.db"},
                        {core::RuleType::Pattern, L"*.log"}};

    RecordingSink sink;
    std::wstring archive = RunBackup(cfg, sink);
    CHECK(sink.summary.outcome == engine::RunOutcome::Success);

    std::string listing = ListArchive(archive);
    CHECK(listing.find("keep.txt") != std::string::npos);
    CHECK(listing.find("x.js") == std::string::npos);
    CHECK(listing.find("node_modules") == std::string::npos);
    CHECK(listing.find("Thumbs.db") == std::string::npos);
    CHECK(listing.find("a.log") == std::string::npos);
    CHECK(listing.find("b.log") == std::string::npos);
}

TEST_CASE("integration: password-protected archive needs the password to list")
{
    auto root = FixtureDir("password");
    auto src = root / "src";
    WriteText(src / "secret.txt", "data");
    auto dest = root / "dest";
    fs::create_directories(dest);

    RecordingSink sink;
    std::wstring archive = RunBackup(BaseConfig(src, dest), sink, L"hunter2");
    CHECK(sink.summary.outcome == engine::RunOutcome::Success);

    std::string without = ListArchive(archive);
    CHECK(without.find("secret.txt") == std::string::npos);
    std::string with = ListArchive(archive, L"hunter2");
    CHECK(with.find("secret.txt") != std::string::npos);

    // the password never shows in any logged line
    CHECK_FALSE(sink.HasLineContaining(L"hunter2"));
    CHECK(sink.HasLineContaining(L"-hp***"));
}

TEST_CASE("integration: nonexistent source yields a mapped non-zero exit code")
{
    auto root = FixtureDir("badsource");
    auto dest = root / "dest";
    fs::create_directories(dest);

    // Bypass validation by driving Rar.exe directly with a missing source.
    auto cfg = BaseConfig(root / "missing", dest);
    std::wstring archive = (dest / "X.rar").wstring();
    auto cmd = core::BuildRarCommand(RarExe(), cfg, archive, L"", L"");
    auto result = RunCapture(cmd.commandLine);
    CHECK(result.exitCode != 0);
    std::wstring message = core::RarExitCodeMessage(result.exitCode);
    CHECK_FALSE(message.empty());
    CHECK(message.find(L"Unknown") == std::wstring::npos); // a known, mapped code
}

TEST_CASE("integration: cancel mid-run deletes the partial archive")
{
    auto root = FixtureDir("cancel");
    auto src = root / "src";
    for (int i = 0; i < 400; ++i)
        WriteText(src / std::format("dir{}", i % 20) / std::format("f{}.txt", i),
                  std::string(2048, 'x'));
    auto dest = root / "dest";
    fs::create_directories(dest);

    RecordingSink sink;
    engine::BackupRequest req;
    req.config = BaseConfig(src, dest);
    req.rarExePath = RarExe();
    req.skipCapsule = true;
    REQUIRE(engine::ValidateBackupRequest(req, sink));
    engine::BackupRun run(std::move(req), sink);
    sink.onProgress = [&](int done)
    {
        if (done == 1)
            run.RequestCancel(); // cancel on the first Adding line
    };
    run.Start();
    run.Join();

    CHECK(sink.summary.outcome == engine::RunOutcome::Cancelled);
    int rarFiles = 0;
    for (const auto& e : fs::directory_iterator(dest))
        if (e.path().extension() == L".rar")
            ++rarFiles;
    CHECK(rarFiles == 0); // partial archive removed
}

TEST_CASE("integration: archive comment round-trips non-ASCII content")
{
    auto root = FixtureDir("comment");
    auto src = root / "src";
    WriteText(src / "a.txt", "x");
    auto dest = root / "dest";
    fs::create_directories(dest);

    // UTF-8 comment with Lithuanian diacritics
    std::string commentUtf8 = "Komentaras \xC4\x85\xC5\xBE\xC4\x99";
    auto commentFile = dest / "comment-in.txt";
    WriteText(commentFile, commentUtf8);

    auto cfg = BaseConfig(src, dest);
    std::wstring archive = (dest / "C.rar").wstring();
    auto cmd = core::BuildRarCommand(RarExe(), cfg, archive, commentFile.wstring(), L"");
    auto created = RunCapture(cmd.commandLine);
    REQUIRE(created.exitCode == 0);

    auto outFile = dest / "comment-out.txt";
    auto cw = RunCapture(L"\"" + RarExe() + L"\" cw -scFC -y \"" + archive + L"\" \"" +
                         outFile.wstring() + L"\"");
    REQUIRE(cw.exitCode == 0);
    std::string roundTripped;
    REQUIRE(engine::ReadFileUtf8(outFile.wstring(), roundTripped));
    CHECK(roundTripped.find("Komentaras \xC4\x85\xC5\xBE\xC4\x99") != std::string::npos);
}

TEST_CASE("integration: pre-scan count equals the number of items RAR adds")
{
    auto root = FixtureDir("prescan");
    auto src = root / "src";
    WriteText(src / "a.txt", "1");
    WriteText(src / "b.log", "junk");                 // excluded by pattern
    WriteText(src / "sub" / "c.txt", "2");
    WriteText(src / "sub" / "Thumbs.db", "junk");     // excluded by file rule
    WriteText(src / "node_modules" / "d.js", "junk"); // excluded by folder rule
    WriteText(src / "empty" / "placeholder.txt", "3");
    auto dest = root / "dest";
    fs::create_directories(dest);

    auto cfg = BaseConfig(src, dest);
    cfg.excludeRules = {{core::RuleType::Folder, L"node_modules"},
                        {core::RuleType::File, L"Thumbs.db"},
                        {core::RuleType::Pattern, L"*.log"}};

    RecordingSink sink;
    RunBackup(cfg, sink);
    CHECK(sink.summary.outcome == engine::RunOutcome::Success);
    // expected items: src, a.txt, sub, sub\c.txt, empty, empty\placeholder.txt = 6
    CHECK(sink.total == 6);
    CHECK(sink.addingLines == sink.total);
    CHECK(sink.maxDone == sink.total);
}

TEST_CASE("integration: end-to-end through the built CLI exe")
{
    REQUIRE_FALSE(BuiltAppExe().empty());

    auto root = FixtureDir("cli");
    auto src = root / "src";
    WriteText(src / "data.txt", "cli test data");
    REQUIRE(fs::exists(src / "data.txt"));
    auto dest = root / "dest";
    fs::create_directories(dest);
    REQUIRE(fs::exists(dest));

    // profile file for the run
    core::AppConfig cfg = BaseConfig(src, dest);
    cfg.backupName = L"CliE2E";
    auto profile = root / "run.rbprofile";
    REQUIRE(engine::WriteFileUtf8(profile.wstring(), core::ConfigToJson(cfg)));

    std::wstring cmd = L"\"" + BuiltAppExe() + L"\" backup --profile \"" + profile.wstring() +
                       L"\" --no-capsule --password pw123";
    auto result = RunCapture(cmd);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("Command: ") != std::string::npos);
    CHECK(result.output.find("-hp***") != std::string::npos);     // masked
    CHECK(result.output.find("pw123") == std::string::npos);      // never plain
    CHECK(result.output.find("completed successfully") != std::string::npos);

    int rarCount = 0;
    std::wstring archive;
    for (const auto& e : fs::directory_iterator(dest))
        if (e.path().extension() == L".rar")
        {
            ++rarCount;
            archive = e.path().wstring();
        }
    REQUIRE(rarCount == 1);
    CHECK(fs::path(archive).filename().wstring().starts_with(L"CliE2E_"));
    CHECK(ListArchive(archive, L"pw123").find("data.txt") != std::string::npos);

    // exit code 2 on validation failure (empty folder list)
    core::AppConfig bad;
    bad.folders.clear();
    bad.backupName = L"X";
    bad.destination = dest.wstring();
    auto badProfile = root / "bad.rbprofile";
    REQUIRE(engine::WriteFileUtf8(badProfile.wstring(), core::ConfigToJson(bad)));
    auto badResult = RunCapture(L"\"" + BuiltAppExe() + L"\" backup --profile \"" +
                                badProfile.wstring() + L"\" --no-capsule");
    CHECK(badResult.exitCode == 2);
}
