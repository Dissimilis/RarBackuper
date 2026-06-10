#include "engine/RarRunner.h"

#include <objbase.h>

#include <filesystem>
#include <format>
#include <vector>

#include "core/ArchiveName.h"
#include "core/RarCommandLine.h"
#include "core/RarExitCodes.h"
#include "core/RarOutputParser.h"
#include "engine/MetaCollector.h"

namespace fs = std::filesystem;

namespace engine
{

namespace
{

bool DirectoryExists(const std::wstring& path)
{
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool ProbeWritable(const std::wstring& dir)
{
    std::wstring probe = dir + L"\\.rarbackuper-write-test.tmp";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(h);
    return true;
}

}

bool ValidateBackupRequest(const BackupRequest& req, EventSink& sink)
{
    bool ok = true;
    auto fail = [&](const std::wstring& msg)
    {
        sink.OnLog(LogSeverity::Error, msg);
        ok = false;
    };

    if (req.rarExePath.empty())
        fail(L"Cannot start: Rar.exe was not found");
    if (req.config.folders.empty())
        fail(L"Cannot start: no folders selected for backup");
    for (const auto& f : req.config.folders)
    {
        if (!DirectoryExists(f))
            fail(L"Cannot start: source folder does not exist: " + f);
    }
    if (core::SanitizeBackupName(req.config.backupName).empty())
        fail(L"Cannot start: backup name is empty (or contains only invalid characters)");
    if (req.config.destination.empty())
        fail(L"Cannot start: no destination folder selected");
    else if (!DirectoryExists(req.config.destination))
        fail(L"Cannot start: destination folder does not exist: " + req.config.destination);
    else if (!ProbeWritable(req.config.destination))
        fail(L"Cannot start: destination folder is not writable: " + req.config.destination);
    return ok;
}

std::wstring FormatByteSize(unsigned long long bytes)
{
    if (bytes >= 1024ull * 1024 * 1024)
        return std::format(L"{:.2f} GB", static_cast<double>(bytes) / (1024.0 * 1024 * 1024));
    if (bytes >= 1024ull * 1024)
        return std::format(L"{:.2f} MB", static_cast<double>(bytes) / (1024.0 * 1024));
    if (bytes >= 1024)
        return std::format(L"{:.1f} KB", static_cast<double>(bytes) / 1024.0);
    return std::format(L"{} bytes", bytes);
}

std::wstring FormatElapsed(unsigned long long ms)
{
    unsigned long long sec = ms / 1000;
    if (sec >= 60)
        return std::format(L"{} min {} s", sec / 60, sec % 60);
    return std::format(L"{}.{} s", sec, (ms % 1000) / 100);
}

BackupRun::BackupRun(BackupRequest req, EventSink& sink)
    : req_(std::move(req)), sink_(sink)
{
}

BackupRun::~BackupRun()
{
    RequestCancel();
    Join();
}

void BackupRun::Start()
{
    thread_ = std::thread(&BackupRun::Worker, this);
}

void BackupRun::RequestCancel()
{
    cancel_ = true;
    std::lock_guard lock(processMutex_);
    if (process_)
        TerminateProcess(process_, 255); // 255 == RAR's own "user stopped" code
}

void BackupRun::Join()
{
    if (thread_.joinable())
        thread_.join();
}

BackupRun::ScanResult BackupRun::PreScan(const core::ExcludeMatcher& matcher)
{
    // Counts every item RAR will print an "Adding" line for: regular files
    // and directories (including each source folder itself), with excluded
    // ones removed the same way the -x masks will remove them.
    ScanResult result;
    for (const auto& folder : req_.config.folders)
    {
        if (cancel_)
            return result;
        if (matcher.IsExcludedDir(folder))
            continue;
        ++result.items; // the source folder itself

        std::vector<std::wstring> stack{folder};
        while (!stack.empty() && !cancel_)
        {
            std::wstring dir = std::move(stack.back());
            stack.pop_back();
            std::error_code ec;
            fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
            if (ec)
            {
                ++result.inaccessible;
                continue;
            }
            for (const auto& entry : it)
            {
                if (cancel_)
                    return result;
                std::error_code ec2;
                std::wstring path = entry.path().wstring();
                if (entry.is_directory(ec2))
                {
                    if (matcher.IsExcludedDir(path))
                        continue;
                    ++result.items;
                    if (!entry.is_symlink(ec2))
                        stack.push_back(path);
                }
                else
                {
                    if (matcher.IsExcludedFile(path))
                        continue;
                    ++result.items;
                }
            }
        }
    }
    return result;
}

int BackupRun::RunRarProcess(const std::wstring& commandLine, int totalItems)
{
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
        return -1;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi{};

    std::wstring mutableCmd = commandLine;
    // CWD = destination so the relative "_meta" argument is stored at the
    // archive root while absolute source folders keep their full paths.
    BOOL created = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, req_.config.destination.c_str(),
                                  &si, &pi);
    CloseHandle(writePipe);
    if (!created)
    {
        CloseHandle(readPipe);
        sink_.OnLog(LogSeverity::Error,
                    std::format(L"Failed to start Rar.exe (error {})", GetLastError()));
        return -1;
    }
    CloseHandle(pi.hThread);
    {
        std::lock_guard lock(processMutex_);
        process_ = pi.hProcess;
    }
    if (cancel_) // cancel may have arrived between launch and registration
        TerminateProcess(pi.hProcess, 255);

    core::RarOutputParser parser;
    int itemsDone = 0;
    auto handleEvents = [&](const std::vector<core::ParsedEvent>& events)
    {
        for (const auto& ev : events)
        {
            sink_.OnLog(LogSeverity::Info, ev.text);
            if (ev.kind == core::ParsedEvent::Kind::Adding)
            {
                ++itemsDone;
                sink_.OnProgress(itemsDone, totalItems);
                sink_.OnCurrentFile(ev.path);
            }
        }
    };

    char buf[4096];
    DWORD read = 0;
    while (ReadFile(readPipe, buf, sizeof(buf), &read, nullptr) && read > 0)
        handleEvents(parser.Feed(std::string_view(buf, read)));
    handleEvents(parser.Finish());
    CloseHandle(readPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    {
        std::lock_guard lock(processMutex_);
        CloseHandle(pi.hProcess);
        process_ = nullptr;
    }
    return static_cast<int>(exitCode);
}

void BackupRun::Worker()
{
    // COM for WMI queries done by the meta collectors on this thread.
    struct ComScope
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ~ComScope()
        {
            if (SUCCEEDED(hr))
                CoUninitialize();
        }
    } comScope;

    const ULONGLONG t0 = GetTickCount64();
    RunSummary summary;

    auto finish = [&](RunOutcome outcome, const std::wstring& message)
    {
        summary.outcome = outcome;
        summary.message = message;
        summary.elapsedMs = GetTickCount64() - t0;
        LogSeverity sev = outcome == RunOutcome::Success ? LogSeverity::Info
                          : outcome == RunOutcome::Warning ? LogSeverity::Warn
                                                           : LogSeverity::Error;
        sink_.OnLog(sev, message);
        sink_.OnStateChange(RunState::Idle);
        sink_.OnCompleted(summary);
    };

    auto deletePartial = [&]()
    {
        if (!archivePath_.empty())
        {
            if (DeleteFileW(archivePath_.c_str()))
                sink_.OnLog(LogSeverity::Info, L"Partial archive deleted: " + archivePath_);
            else if (GetLastError() != ERROR_FILE_NOT_FOUND)
                sink_.OnLog(LogSeverity::Warn, L"Could not delete partial archive: " + archivePath_);
        }
    };

    // --- archive path from current local time ---
    std::wstring name = core::SanitizeBackupName(req_.config.backupName);
    if (name != req_.config.backupName)
        sink_.OnLog(LogSeverity::Warn, L"Backup name adjusted for file system use: " + name);
    SYSTEMTIME st;
    GetLocalTime(&st);
    core::ArchiveTime at{st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute};
    archivePath_ = core::MakeArchivePath(req_.config.destination, name, at);

    // --- time capsule collection (before pre-scan so _meta is counted) ---
    MetaCollector collector(req_.config, req_.config.destination, sink_, cancel_);
    std::wstring commentFile;
    bool capsuleWanted = !req_.skipCapsule &&
                         (req_.config.capsuleSystemInfo || req_.config.capsuleFileInventory ||
                          req_.config.capsuleBookmarks || req_.config.capsuleImportantStuff);
    core::AppConfig effectiveConfig = req_.config;
    if (capsuleWanted)
    {
        sink_.OnStateChange(RunState::Collecting);
        if (!collector.Collect())
        {
            collector.Cleanup();
            finish(RunOutcome::Cancelled, L"Backup cancelled during time-capsule collection");
            return;
        }
        if (cancel_)
        {
            collector.Cleanup();
            finish(RunOutcome::Cancelled, L"Backup cancelled during time-capsule collection");
            return;
        }
        // relative on purpose: Rar runs with CWD = destination, so the
        // capsule content lands at "_meta\" in the archive root
        effectiveConfig.folders.push_back(L"_meta");
    }
    commentFile = collector.WriteCommentFile(req_.config);

    sink_.OnStateChange(RunState::Archiving);

    // --- pre-scan ---
    sink_.OnLog(LogSeverity::Info, L"Scanning source folders (applying exclude rules)...");
    core::ExcludeMatcher matcher(req_.config.excludeRules);
    ScanResult scan = PreScan(matcher);
    if (cancel_)
    {
        collector.Cleanup();
        finish(RunOutcome::Cancelled, L"Backup cancelled");
        return;
    }
    if (capsuleWanted)
    {
        // count the _meta tree too so the progress denominator matches
        std::vector<std::wstring> save = req_.config.folders;
        req_.config.folders = {collector.MetaDir()};
        ScanResult metaScan = PreScan(matcher);
        req_.config.folders = save;
        scan.items += metaScan.items;
    }
    summary.filesTotal = scan.items;
    sink_.OnLog(LogSeverity::Info, std::format(L"Pre-scan: {} items to archive", scan.items));
    if (scan.inaccessible > 0)
        sink_.OnLog(LogSeverity::Warn,
                    std::format(L"Pre-scan: {} paths were inaccessible and skipped", scan.inaccessible));
    sink_.OnProgress(0, scan.items);

    // --- build & log the command ---
    core::RarCommand cmd = core::BuildRarCommand(req_.rarExePath, effectiveConfig, archivePath_,
                                                 commentFile, req_.password);
    sink_.OnLog(LogSeverity::Info, L"Archive: " + archivePath_);
    sink_.OnLog(LogSeverity::Info, L"Command: " + cmd.loggable);

    // --- run ---
    int exitCode = RunRarProcess(cmd.commandLine, scan.items);
    collector.Cleanup(); // _meta staging + comment file are always removed

    if (exitCode < 0)
    {
        deletePartial();
        finish(RunOutcome::Failed, L"Backup failed: could not run Rar.exe");
        return;
    }

    core::RarStatus status = core::ClassifyRarExitCode(exitCode);
    if (cancel_ || status == core::RarStatus::Cancelled)
    {
        deletePartial();
        finish(RunOutcome::Cancelled, L"Backup cancelled - partial archive deleted");
        return;
    }

    if (status == core::RarStatus::Success || status == core::RarStatus::Warning)
    {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (GetFileAttributesExW(archivePath_.c_str(), GetFileExInfoStandard, &fad))
            summary.archiveSizeBytes =
                (static_cast<unsigned long long>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        summary.archivePath = archivePath_;
        summary.filesDone = summary.filesTotal;
        std::wstring base = std::format(L"Backup {} in {} - archive {} ({})",
                                        status == core::RarStatus::Success
                                            ? L"completed successfully"
                                            : L"completed with warnings (exit code 1)",
                                        FormatElapsed(GetTickCount64() - t0), archivePath_,
                                        FormatByteSize(summary.archiveSizeBytes));
        finish(status == core::RarStatus::Success ? RunOutcome::Success : RunOutcome::Warning, base);
        return;
    }

    deletePartial();
    finish(RunOutcome::Failed,
           std::format(L"Backup failed: {} (Rar.exe exit code {})",
                       core::RarExitCodeMessage(exitCode), exitCode));
}

}
