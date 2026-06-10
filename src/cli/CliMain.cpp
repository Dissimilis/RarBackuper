#include "cli/CliMain.h"

#include <windows.h>

#include <format>
#include <string>

#include "core/Text.h"
#include "engine/EventSink.h"
#include "engine/RarDiscovery.h"
#include "engine/RarRunner.h"
#include "engine/Settings.h"

namespace cli
{

namespace
{

// stdout writer that works for consoles, pipes and file redirection.
class Console
{
public:
    Console()
    {
        out_ = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!out_ || out_ == INVALID_HANDLE_VALUE)
        {
            // GUI-subsystem exe started from a console without redirection:
            // attach to the parent's console explicitly.
            if (AttachConsole(ATTACH_PARENT_PROCESS))
                out_ = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
        }
        DWORD mode = 0;
        isConsole_ = out_ && out_ != INVALID_HANDLE_VALUE && GetConsoleMode(out_, &mode);
    }

    void WriteLine(const std::wstring& s)
    {
        if (!out_ || out_ == INVALID_HANDLE_VALUE)
            return;
        if (isConsole_)
        {
            std::wstring line = s + L"\r\n";
            DWORD written = 0;
            WriteConsoleW(out_, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
        }
        else
        {
            std::string utf8 = core::WideToUtf8(s) + "\r\n";
            DWORD written = 0;
            WriteFile(out_, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        }
    }

private:
    HANDLE out_ = nullptr;
    bool isConsole_ = false;
};

class ConsoleSink : public engine::EventSink
{
public:
    explicit ConsoleSink(Console& console) : console_(console) {}

    void OnLog(engine::LogSeverity severity, const std::wstring& text) override
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        const wchar_t* prefix = severity == engine::LogSeverity::Error  ? L"ERROR: "
                                : severity == engine::LogSeverity::Warn ? L"WARN: "
                                                                        : L"";
        console_.WriteLine(std::format(L"{:02}:{:02}:{:02}  {}{}", st.wHour, st.wMinute,
                                       st.wSecond, prefix, text));
    }

    void OnProgress(int done, int total) override
    {
        // occasional clean progress lines (no control characters): every
        // ~5% plus the final item
        int step = total > 20 ? total / 20 : 1;
        if (done == total || done % step == 0)
            console_.WriteLine(std::format(L"[{}/{}] {}", done, total, lastFile_));
    }

    void OnCurrentFile(const std::wstring& path) override { lastFile_ = path; }
    void OnStateChange(engine::RunState) override {}

    void OnCompleted(const engine::RunSummary& summary) override { outcome_ = summary.outcome; }

    engine::RunOutcome Outcome() const { return outcome_; }

private:
    Console& console_;
    std::wstring lastFile_;
    engine::RunOutcome outcome_ = engine::RunOutcome::Failed;
};

engine::BackupRun* g_activeRun = nullptr;

BOOL WINAPI CtrlHandler(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT)
    {
        if (g_activeRun)
            g_activeRun->RequestCancel(); // worker cleans up: kills Rar, deletes partials
        return TRUE;
    }
    return FALSE;
}

void PrintUsage(Console& console)
{
    console.WriteLine(L"RarBackuper - easy RAR backups (headless mode)");
    console.WriteLine(L"");
    console.WriteLine(L"Usage:");
    console.WriteLine(L"  RarBackuper.exe backup [options]   run a backup using settings.json");
    console.WriteLine(L"  RarBackuper.exe --help             show this help");
    console.WriteLine(L"");
    console.WriteLine(L"Options for 'backup':");
    console.WriteLine(L"  --profile <file.rbprofile>  use this configuration instead of settings.json");
    console.WriteLine(L"  --password <pw>             archive password for this run (never persisted)");
    console.WriteLine(L"  --dest <folder>             override the destination folder");
    console.WriteLine(L"  --name <name>               override the backup name");
    console.WriteLine(L"  --no-capsule                skip all time-capsule collection");
    console.WriteLine(L"  --yes                       auto-confirm interactive warnings");
    console.WriteLine(L"");
    console.WriteLine(L"Exit codes: 0 success, 1 warnings, 2 validation failure, 3 backup failed,");
    console.WriteLine(L"            4 cancelled");
}

}

int RunCli(int argc, wchar_t** argv)
{
    Console console;

    std::wstring command = argc > 1 ? argv[1] : L"";
    if (command == L"--help" || command == L"-h" || command == L"/?" || command == L"help")
    {
        PrintUsage(console);
        return 0;
    }
    if (command != L"backup")
    {
        console.WriteLine(L"Unknown command: " + command);
        console.WriteLine(L"");
        PrintUsage(console);
        return 2;
    }

    std::wstring profilePath, password, destOverride, nameOverride;
    bool noCapsule = false, assumeYes = false;
    for (int i = 2; i < argc; ++i)
    {
        std::wstring arg = argv[i];
        auto value = [&](const wchar_t* name) -> std::wstring
        {
            if (i + 1 >= argc)
            {
                console.WriteLine(std::format(L"Missing value for {}", name));
                return L"";
            }
            return argv[++i];
        };
        if (arg == L"--profile")
            profilePath = value(L"--profile");
        else if (arg == L"--password")
            password = value(L"--password");
        else if (arg == L"--dest")
            destOverride = value(L"--dest");
        else if (arg == L"--name")
            nameOverride = value(L"--name");
        else if (arg == L"--no-capsule")
            noCapsule = true;
        else if (arg == L"--yes")
            assumeYes = true;
        else
        {
            console.WriteLine(L"Unknown option: " + arg);
            console.WriteLine(L"");
            PrintUsage(console);
            return 2;
        }
    }

    ConsoleSink sink(console);
    sink.OnLog(engine::LogSeverity::Info, L"RarBackuper headless backup");

    // --- configuration ---
    core::AppConfig config;
    if (!profilePath.empty())
    {
        std::string text;
        std::wstring err;
        if (!engine::ReadFileUtf8(profilePath, text, &err))
        {
            sink.OnLog(engine::LogSeverity::Error, L"Cannot read profile " + profilePath + L": " + err);
            return 2;
        }
        auto parsed = core::ConfigFromJson(text);
        if (!parsed.ok)
        {
            sink.OnLog(engine::LogSeverity::Error,
                       L"Profile " + profilePath + L" is invalid: " + parsed.error);
            return 2;
        }
        config = std::move(parsed.config);
        sink.OnLog(engine::LogSeverity::Info, L"Using profile " + profilePath);
    }
    else
    {
        engine::SettingsStore store(engine::ExeDirectory() + L"\\settings.json");
        sink.OnLog(engine::LogSeverity::Info, store.Load());
        config = store.config;
    }
    if (!destOverride.empty())
        config.destination = destOverride;
    if (!nameOverride.empty())
        config.backupName = nameOverride;

    // --- Rar.exe discovery ---
    std::vector<std::wstring> searched;
    std::wstring rarPath = engine::DiscoverTool(L"Rar.exe", &searched);
    if (rarPath.empty())
    {
        std::wstring where;
        for (const auto& s : searched)
            where += (where.empty() ? L"" : L", ") + s;
        sink.OnLog(engine::LogSeverity::Error, L"Rar.exe not found (searched: " + where + L")");
        return 2;
    }
    sink.OnLog(engine::LogSeverity::Info, L"Found Rar.exe: " + rarPath);

    // --- the Important-Stuff-without-password warning ---
    if (config.capsuleImportantStuff && !noCapsule && password.empty())
    {
        sink.OnLog(engine::LogSeverity::Warn,
                   L"Important Stuff is enabled but no archive password is set - the archive "
                   L"would contain credentials and keys in plain form");
        if (!assumeYes)
        {
            sink.OnLog(engine::LogSeverity::Error,
                       L"Aborting (pass --yes to proceed without a password, or use --password)");
            return 2;
        }
        sink.OnLog(engine::LogSeverity::Warn, L"Proceeding anyway (--yes)");
    }

    engine::BackupRequest req;
    req.config = std::move(config);
    req.password = password;
    req.rarExePath = rarPath;
    req.skipCapsule = noCapsule;
    if (!engine::ValidateBackupRequest(req, sink))
        return 2;

    engine::BackupRun run(std::move(req), sink);
    g_activeRun = &run;
    SetConsoleCtrlHandler(CtrlHandler, TRUE);
    run.Start();
    run.Join();
    SetConsoleCtrlHandler(CtrlHandler, FALSE);
    g_activeRun = nullptr;

    switch (sink.Outcome())
    {
    case engine::RunOutcome::Success:   return 0;
    case engine::RunOutcome::Warning:   return 1;
    case engine::RunOutcome::Cancelled: return 4;
    default:                            return 3;
    }
}

}
