#pragma once
#include <string>

namespace engine
{

enum class LogSeverity
{
    Info,
    Warn,
    Error,
};

enum class RunState
{
    Idle,
    Collecting, // time-capsule meta collection
    Archiving,  // Rar.exe running
};

enum class RunOutcome
{
    Success,
    Warning,   // RAR exit code 1
    Failed,
    Cancelled,
};

struct RunSummary
{
    RunOutcome outcome = RunOutcome::Failed;
    std::wstring message;                       // friendly one-line result
    std::wstring archivePath;                   // empty when nothing was produced
    unsigned long long archiveSizeBytes = 0;
    unsigned long long elapsedMs = 0;
    int filesTotal = 0;
    int filesDone = 0;
};

// The engine (RarRunner, MetaCollector) emits everything through this
// interface and never touches a window or stdout directly. The GUI
// implementation marshals to the UI thread via PostMessage; the CLI
// implementation writes to stdout. Implementations must be callable from
// any thread.
class EventSink
{
public:
    virtual ~EventSink() = default;

    virtual void OnLog(LogSeverity severity, const std::wstring& text) = 0;
    virtual void OnProgress(int filesDone, int filesTotal) = 0;
    virtual void OnCurrentFile(const std::wstring& path) = 0;
    virtual void OnStateChange(RunState state) = 0;
    virtual void OnCompleted(const RunSummary& summary) = 0;
};

}
