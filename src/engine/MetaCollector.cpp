#include "engine/MetaCollector.h"

#include <windows.h>

#include <filesystem>
#include <format>

#include "core/Text.h"
#include "engine/Settings.h"

namespace fs = std::filesystem;

namespace engine
{

MetaCollector::MetaCollector(const core::AppConfig& config, std::wstring destination,
                             EventSink& sink, std::atomic<bool>& cancel)
    : config_(config), destination_(std::move(destination)), sink_(sink), cancel_(cancel)
{
    metaDir_ = destination_ + L"\\_meta";
    commentFile_ = destination_ + L"\\.rarbackuper-comment.txt";
}

bool MetaCollector::Collect()
{
    std::error_code ec;
    if (fs::exists(metaDir_, ec))
    {
        sink_.OnLog(LogSeverity::Warn, L"Leftover _meta folder found in destination - cleaning it");
        fs::remove_all(metaDir_, ec);
    }
    if (!fs::create_directories(metaDir_, ec) && ec)
    {
        sink_.OnLog(LogSeverity::Error, L"Could not create the _meta staging folder: " + metaDir_);
        return true; // not a cancel; the run continues without capsule content
    }
    created_ = true;
    sink_.OnLog(LogSeverity::Info, L"Collecting time-capsule data into " + metaDir_);

    // Detector implementations are added by the collectors below; each one
    // checks the cancel flag and degrades gracefully on missing access.
    if (Cancelled())
        return false;

    return !Cancelled();
}

std::wstring MetaCollector::WriteCommentFile(const core::AppConfig& config)
{
    wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD len = MAX_COMPUTERNAME_LENGTH;
    GetComputerNameW(computer, &len);

    SYSTEMTIME st;
    GetLocalTime(&st);

    std::wstring text = std::format(L"RarBackuper archive\r\nMachine: {}\r\nCreated: "
                                    L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}\r\n",
                                    computer, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                                    st.wSecond);
    text += L"Folders:\r\n";
    for (const auto& f : config.folders)
        text += L"  " + f + L"\r\n";
    text += std::format(L"Compression: {}\r\nSolid: {}\r\nExclude rules: {}\r\n",
                        core::CompressionLevelName(config.level), config.solid ? L"yes" : L"no",
                        config.excludeRules.size());

    if (!WriteFileUtf8(commentFile_, core::WideToUtf8(text)))
    {
        sink_.OnLog(LogSeverity::Warn, L"Could not write the archive comment file - continuing without a comment");
        return L"";
    }
    return commentFile_;
}

void MetaCollector::Cleanup()
{
    std::error_code ec;
    if (created_ && fs::exists(metaDir_, ec))
    {
        fs::remove_all(metaDir_, ec);
        if (ec)
            sink_.OnLog(LogSeverity::Warn, L"Could not fully remove the _meta staging folder: " +
                                               metaDir_);
        else
            sink_.OnLog(LogSeverity::Info, L"Removed the _meta staging folder");
        created_ = false;
    }
    fs::remove(commentFile_, ec);
}

}
