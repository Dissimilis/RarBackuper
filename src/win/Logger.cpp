#include "win/Logger.h"

#include <format>

namespace win
{

std::wstring FormatLogLine(engine::LogSeverity severity, const std::wstring& text)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    const wchar_t* prefix = L"";
    switch (severity)
    {
    case engine::LogSeverity::Warn:  prefix = L"WARN: "; break;
    case engine::LogSeverity::Error: prefix = L"ERROR: "; break;
    default: break;
    }
    return std::format(L"{:02}:{:02}:{:02}  {}{}", st.wHour, st.wMinute, st.wSecond, prefix, text);
}

void GuiEventSink::OnLog(engine::LogSeverity severity, const std::wstring& text)
{
    if (!target_)
        return;
    auto* line = new std::wstring(FormatLogLine(severity, text));
    if (!PostMessageW(target_, WM_APP_LOG, static_cast<WPARAM>(severity),
                      reinterpret_cast<LPARAM>(line)))
        delete line;
}

void GuiEventSink::OnProgress(int filesDone, int filesTotal)
{
    if (target_)
        PostMessageW(target_, WM_APP_PROGRESS, static_cast<WPARAM>(filesDone),
                     static_cast<LPARAM>(filesTotal));
}

void GuiEventSink::OnCurrentFile(const std::wstring& path)
{
    if (!target_)
        return;
    auto* copy = new std::wstring(path);
    if (!PostMessageW(target_, WM_APP_CURRENTFILE, 0, reinterpret_cast<LPARAM>(copy)))
        delete copy;
}

void GuiEventSink::OnStateChange(engine::RunState state)
{
    if (target_)
        PostMessageW(target_, WM_APP_STATE, static_cast<WPARAM>(state), 0);
}

void GuiEventSink::OnCompleted(const engine::RunSummary& summary)
{
    if (!target_)
        return;
    auto* copy = new engine::RunSummary(summary);
    if (!PostMessageW(target_, WM_APP_COMPLETED, 0, reinterpret_cast<LPARAM>(copy)))
        delete copy;
}

}
