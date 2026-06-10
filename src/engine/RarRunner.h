#pragma once
#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "core/SettingsModel.h"
#include "engine/EventSink.h"

namespace engine
{

struct BackupRequest
{
    core::AppConfig config;
    std::wstring password;   // session-only; never persisted
    std::wstring rarExePath;
    bool skipCapsule = false; // CLI --no-capsule
};

// Pre-start validation. Failures are logged as ERROR lines through the
// sink; returns false if the run must not start.
bool ValidateBackupRequest(const BackupRequest& req, EventSink& sink);

// Human-friendly formatting helpers (also used for summaries).
std::wstring FormatByteSize(unsigned long long bytes);
std::wstring FormatElapsed(unsigned long long ms);

// One backup run on a worker thread: (optional meta collection ->)
// pre-scan -> Rar.exe -> cleanup. All output flows through the EventSink.
class BackupRun
{
public:
    BackupRun(BackupRequest req, EventSink& sink);
    ~BackupRun(); // requests cancel if still running, then joins

    void Start();

    // Kills Rar.exe if running and makes the worker clean up (partial
    // archive + _meta staging deleted). Safe from any thread.
    void RequestCancel();

    void Join();

private:
    void Worker();

    struct ScanResult
    {
        int items = 0;        // files + directories (RAR prints Adding for both)
        int inaccessible = 0; // skipped paths
    };
    ScanResult PreScan(const core::ExcludeMatcher& matcher);

    int RunRarProcess(const std::wstring& commandLine, int totalItems);

    BackupRequest req_;
    EventSink& sink_;
    std::thread thread_;
    std::atomic<bool> cancel_{false};

    std::mutex processMutex_;
    HANDLE process_ = nullptr; // valid only while Rar.exe runs

    std::wstring archivePath_;
};

}
