#pragma once
#include <windows.h>

#include <string>

#include "engine/EventSink.h"

namespace win
{

// UI-thread messages posted by GuiEventSink. String/struct payloads are
// heap-allocated by the sender and freed by the receiving window proc.
inline constexpr UINT WM_APP_LOG = WM_APP + 1;         // wParam: LogSeverity, lParam: std::wstring* (formatted line)
inline constexpr UINT WM_APP_PROGRESS = WM_APP + 2;    // wParam: filesDone, lParam: filesTotal
inline constexpr UINT WM_APP_CURRENTFILE = WM_APP + 3; // lParam: std::wstring*
inline constexpr UINT WM_APP_STATE = WM_APP + 4;       // wParam: RunState
inline constexpr UINT WM_APP_COMPLETED = WM_APP + 5;   // lParam: engine::RunSummary*

// "HH:mm:ss  [ERROR:|WARN: ]<text>" using the current local time.
std::wstring FormatLogLine(engine::LogSeverity severity, const std::wstring& text);

// EventSink implementation for the GUI: thread-safe, delivers everything
// to the UI thread via PostMessage only.
class GuiEventSink : public engine::EventSink
{
public:
    explicit GuiEventSink(HWND target = nullptr) : target_(target) {}

    void SetTarget(HWND target) { target_ = target; }

    void OnLog(engine::LogSeverity severity, const std::wstring& text) override;
    void OnProgress(int filesDone, int filesTotal) override;
    void OnCurrentFile(const std::wstring& path) override;
    void OnStateChange(engine::RunState state) override;
    void OnCompleted(const engine::RunSummary& summary) override;

private:
    HWND target_;
};

}
