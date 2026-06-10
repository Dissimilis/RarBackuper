#include "engine/detectors/Detectors.h"

#include <windows.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <vector>

#include "core/Text.h"

namespace engine::detectors
{

namespace
{

struct InventoryStats
{
    unsigned long long files = 0;
    unsigned long long inaccessible = 0;
};

void FileTimeToStamp(const FILETIME& ft, wchar_t* buf, size_t bufLen)
{
    SYSTEMTIME st{};
    FILETIME local{};
    FileTimeToLocalFileTime(&ft, &local);
    FileTimeToSystemTime(&local, &st);
    swprintf_s(buf, bufLen, L"%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
               st.wMinute);
}

// Iterative scan with an explicit stack: full drives have deep trees and
// recursion depth must stay bounded.
void ScanDrive(const std::wstring& root, std::ofstream& out, InventoryStats& stats,
               EventSink& sink, std::atomic<bool>& cancel)
{
    std::vector<std::wstring> stack{root};
    unsigned long long lastReport = 0;
    while (!stack.empty() && !cancel)
    {
        std::wstring dir = std::move(stack.back());
        stack.pop_back();

        WIN32_FIND_DATAW fd;
        HANDLE find = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd,
                                       FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE)
        {
            ++stats.inaccessible;
            continue;
        }
        do
        {
            if (cancel)
                break;
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;
            std::wstring path = dir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) // no link cycles
                    stack.push_back(path);
            }
            else
            {
                unsigned long long size =
                    (static_cast<unsigned long long>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                wchar_t stamp[32];
                FileTimeToStamp(fd.ftLastWriteTime, stamp, 32);
                std::string lineUtf8 = core::WideToUtf8(
                    std::format(L"{}\t{}\t{}\r\n", path, size, stamp));
                out.write(lineUtf8.data(), static_cast<std::streamsize>(lineUtf8.size()));
                ++stats.files;
                if (stats.files - lastReport >= 100000)
                {
                    lastReport = stats.files;
                    sink.OnLog(LogSeverity::Info,
                               std::format(L"  inventory of {}: {} files so far...", root,
                                           stats.files));
                }
            }
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
}

}

void WriteDriveInventories(const std::wstring& metaDir, EventSink& sink,
                           std::atomic<bool>& cancel)
{
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26 && !cancel; ++i)
    {
        if (!(mask & (1u << i)))
            continue;
        wchar_t letter = static_cast<wchar_t>(L'A' + i);
        std::wstring root = std::wstring(1, letter) + L":\\";
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED)
            continue;

        std::wstring outPath = metaDir + std::format(L"\\filelist-{}.txt", letter);
        sink.OnLog(LogSeverity::Info,
                   std::format(L"Building file inventory of drive {}: ...", letter));
        std::ofstream out(std::filesystem::path(outPath), std::ios::binary);
        if (!out)
        {
            sink.OnLog(LogSeverity::Warn, L"Could not create " + outPath);
            continue;
        }
        InventoryStats stats;
        ScanDrive(std::wstring(1, letter) + L":", out, stats, sink, cancel);
        std::string summary = core::WideToUtf8(
            std::format(L"# total files: {}, inaccessible paths skipped: {}\r\n", stats.files,
                        stats.inaccessible));
        out.write(summary.data(), static_cast<std::streamsize>(summary.size()));
        out.close();
        if (cancel)
            return;
        sink.OnLog(LogSeverity::Info,
                   std::format(L"Drive {}: inventory: {} files listed, {} inaccessible paths skipped",
                               letter, stats.files, stats.inaccessible));
    }
}

}
