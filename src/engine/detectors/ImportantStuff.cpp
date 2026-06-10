#include "engine/detectors/Detectors.h"

#include <windows.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <format>
#include <vector>

#include "core/DetectorCatalog.h"
#include "core/Text.h"
#include "engine/Settings.h"

namespace fs = std::filesystem;

namespace engine::detectors
{

namespace
{

struct ManifestWriter
{
    std::wstring text;

    void Line(const std::wstring& s) { text += s + L"\r\n"; }
};

std::wstring ExpandEnv(const std::wstring& s)
{
    wchar_t buf[4096]{};
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), buf, 4096);
    return n > 0 && n <= 4096 ? std::wstring(buf, n - 1) : s;
}

bool HasWildcard(const std::wstring& s)
{
    return s.find_first_of(L"*?") != std::wstring::npos;
}

// Expands one path pattern (env vars already expanded) where any component
// may contain * / ? wildcards. Returns matching files and directories.
std::vector<std::wstring> ExpandPattern(const std::wstring& pattern)
{
    std::vector<std::wstring> components;
    size_t start = 0;
    for (size_t i = 0; i <= pattern.size(); ++i)
    {
        if (i == pattern.size() || pattern[i] == L'\\' || pattern[i] == L'/')
        {
            if (i > start)
                components.push_back(pattern.substr(start, i - start));
            start = i + 1;
        }
    }
    if (components.empty())
        return {};

    std::vector<std::wstring> current{components[0]}; // "C:" or similar root
    for (size_t ci = 1; ci < components.size(); ++ci)
    {
        const std::wstring& comp = components[ci];
        std::vector<std::wstring> next;
        const bool last = ci + 1 == components.size();
        for (const auto& base : current)
        {
            if (!HasWildcard(comp))
            {
                std::wstring candidate = base + L"\\" + comp;
                DWORD attrs = GetFileAttributesW(candidate.c_str());
                if (attrs == INVALID_FILE_ATTRIBUTES)
                    continue;
                if (!last && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
                    continue;
                next.push_back(std::move(candidate));
            }
            else
            {
                WIN32_FIND_DATAW fd;
                HANDLE find = FindFirstFileExW((base + L"\\" + comp).c_str(), FindExInfoBasic,
                                               &fd, FindExSearchNameMatch, nullptr, 0);
                if (find == INVALID_HANDLE_VALUE)
                    continue;
                do
                {
                    if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                        continue;
                    bool isDir = fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
                    if (!last && !isDir)
                        continue;
                    next.push_back(base + L"\\" + fd.cFileName);
                } while (FindNextFileW(find, &fd));
                FindClose(find);
            }
        }
        current = std::move(next);
        if (current.empty())
            break;
    }
    return current;
}

// The non-wildcard directory prefix of a pattern; matches are stored
// relative to it so enough path context survives to disambiguate.
std::wstring PatternBase(const std::wstring& pattern)
{
    size_t firstWild = pattern.find_first_of(L"*?");
    size_t cut = firstWild == std::wstring::npos ? pattern.find_last_of(L"\\/")
                                                 : pattern.find_last_of(L"\\/", firstWild);
    return cut == std::wstring::npos ? L"" : pattern.substr(0, cut);
}

std::wstring SafeJoin(const std::wstring& dir, std::wstring rel)
{
    for (auto& c : rel)
        if (c == L'/' || c == L':')
            c = L'_';
    while (!rel.empty() && rel.front() == L'\\')
        rel.erase(rel.begin());
    return dir + L"\\" + rel;
}

struct CopyStats
{
    int copied = 0;
    std::vector<std::pair<std::wstring, unsigned long long>> oversized; // path, size
    int failed = 0;
};

unsigned long long FileSizeOf(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
        return 0;
    return (static_cast<unsigned long long>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
}

void CopyCapped(const std::wstring& from, const std::wstring& to,
                unsigned long long cap, CopyStats& stats, std::atomic<bool>& cancel)
{
    if (cancel)
        return;
    DWORD attrs = GetFileAttributesW(from.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return;
    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
    {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(from, ec))
        {
            if (cancel)
                return;
            CopyCapped(entry.path().wstring(), to + L"\\" + entry.path().filename().wstring(),
                       cap, stats, cancel);
        }
        return;
    }
    unsigned long long size = FileSizeOf(from);
    if (size > cap)
    {
        stats.oversized.emplace_back(from, size);
        return;
    }
    std::error_code ec;
    fs::create_directories(fs::path(to).parent_path(), ec);
    if (CopyFileW(from.c_str(), to.c_str(), FALSE))
        ++stats.copied;
    else
        ++stats.failed;
}

// Runs a command with output redirected to a file. Returns the exit code,
// or -1 when the process could not be started, -2 on timeout.
int RunCommandToFile(const std::wstring& commandLine, const std::wstring& outFile,
                     std::atomic<bool>& cancel)
{
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE out = CreateFileW(outFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE)
        return -1;

    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out;
    si.hStdError = out;
    si.hStdInput = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = commandLine;
    BOOL created = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(out);
    if (!created)
        return -1;
    CloseHandle(pi.hThread);

    int result;
    for (;;)
    {
        DWORD wait = WaitForSingleObject(pi.hProcess, 500);
        if (wait == WAIT_OBJECT_0)
        {
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            result = static_cast<int>(code);
            break;
        }
        if (cancel)
        {
            TerminateProcess(pi.hProcess, 1);
            result = -2;
            break;
        }
    }
    CloseHandle(pi.hProcess);
    return result;
}

std::wstring ResolveTool(const std::wstring& commandLine)
{
    size_t space = commandLine.find(L' ');
    std::wstring exe = space == std::wstring::npos ? commandLine : commandLine.substr(0, space);
    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, exe.c_str(), nullptr, MAX_PATH, found, nullptr) > 0)
        return found;
    return L"";
}

// Obsidian special case: the vault registry lists vault folders; copy each
// vault's .obsidian config folder too.
void CollectObsidianVaults(const std::wstring& obsidianJsonPath, const std::wstring& outDir,
                           unsigned long long cap, CopyStats& stats, ManifestWriter& manifest,
                           std::atomic<bool>& cancel)
{
    std::string text;
    if (!engine::ReadFileUtf8(obsidianJsonPath, text))
        return;
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.contains("vaults") || !j["vaults"].is_object())
        return;
    int i = 0;
    for (const auto& [id, vault] : j["vaults"].items())
    {
        if (cancel)
            return;
        if (!vault.is_object() || !vault.contains("path") || !vault["path"].is_string())
            continue;
        std::wstring vaultPath = core::Utf8ToWide(vault["path"].get<std::string>());
        std::wstring cfg = vaultPath + L"\\.obsidian";
        if (GetFileAttributesW(cfg.c_str()) == INVALID_FILE_ATTRIBUTES)
            continue;
        std::wstring target = outDir + std::format(L"\\vault{}-obsidian-config", ++i);
        CopyCapped(cfg, target, cap, stats, cancel);
        manifest.Line(L"    vault config: " + cfg);
    }
}

}

void CollectImportantStuff(const std::wstring& importantDir, EventSink& sink,
                           std::atomic<bool>& cancel)
{
    sink.OnLog(LogSeverity::Info, L"Collecting Important Stuff (detector catalog)...");
    std::error_code ec;
    fs::create_directories(importantDir, ec);

    ManifestWriter manifest;
    SYSTEMTIME st;
    GetLocalTime(&st);
    manifest.Line(L"=== Important Stuff manifest ===");
    manifest.Line(std::format(L"Generated: {:04}-{:02}-{:02} {:02}:{:02}:{:02}", st.wYear,
                              st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond));
    manifest.Line(L"Note: RarBackuper runs unelevated by design. Detectors that need admin");
    manifest.Line(L"rights collect what is accessible and note the limitation here.");

    std::wstring currentGroup;
    for (const auto& d : core::DetectorCatalog())
    {
        if (cancel)
            return;
        if (d.group != currentGroup)
        {
            currentGroup = d.group;
            manifest.Line(L"");
            manifest.Line(L"[" + currentGroup + L"]");
        }

        std::wstring groupDir = importantDir + L"\\" + d.group;
        manifest.Line(L"* " + d.name + L" - " + d.description);

        if (d.kind == core::DetectorKind::FileGlob)
        {
            CopyStats stats;
            std::vector<std::wstring> foundPaths;
            size_t pos = 0;
            while (pos <= d.target.size())
            {
                size_t sep = d.target.find(L';', pos);
                std::wstring pattern =
                    d.target.substr(pos, sep == std::wstring::npos ? std::wstring::npos
                                                                   : sep - pos);
                pos = sep == std::wstring::npos ? d.target.size() + 1 : sep + 1;
                if (pattern.empty())
                    continue;
                std::wstring expanded = ExpandEnv(pattern);
                if (expanded.find(L'%') != std::wstring::npos)
                    continue; // an env var did not resolve on this machine
                std::wstring base = PatternBase(expanded);
                for (const auto& match : ExpandPattern(expanded))
                {
                    if (cancel)
                        return;
                    std::wstring rel = match.size() > base.size() ? match.substr(base.size())
                                                                  : fs::path(match).filename().wstring();
                    std::wstring target = SafeJoin(groupDir + L"\\" + d.name, rel);
                    CopyStats one;
                    CopyCapped(match, target, d.sizeCapBytes, one, cancel);
                    if (one.copied > 0 || !one.oversized.empty())
                        foundPaths.push_back(match);
                    stats.copied += one.copied;
                    stats.failed += one.failed;
                    stats.oversized.insert(stats.oversized.end(), one.oversized.begin(),
                                           one.oversized.end());
                    if (d.name == L"Obsidian vault registry" && one.copied > 0)
                        CollectObsidianVaults(match, groupDir + L"\\" + d.name, d.sizeCapBytes,
                                              stats, manifest, cancel);
                }
            }
            if (foundPaths.empty())
            {
                manifest.Line(L"    not found on this machine");
            }
            else
            {
                for (const auto& p : foundPaths)
                    manifest.Line(L"    found: " + p);
                for (const auto& [p, size] : stats.oversized)
                {
                    manifest.Line(std::format(
                        L"    found but SKIPPED (size {} bytes > {} bytes cap): {}", size,
                        d.sizeCapBytes, p));
                    sink.OnLog(LogSeverity::Warn,
                               std::format(L"  Important Stuff: skipped oversized {} ({} bytes)",
                                           p, size));
                }
                if (stats.failed > 0)
                    manifest.Line(std::format(L"    {} file(s) could not be copied (in use or "
                                              L"access denied)",
                                              stats.failed));
                manifest.Line(L"    restore: " + d.restore);
                sink.OnLog(LogSeverity::Info,
                           std::format(L"  Important Stuff: {} - {} file(s) collected", d.name,
                                       stats.copied));
            }
        }
        else if (d.kind == core::DetectorKind::RegistryExport)
        {
            fs::create_directories(groupDir, ec);
            std::wstring outFile = groupDir + L"\\" + d.output;
            std::wstring cmd = L"reg.exe export \"" + d.target + L"\" \"" + outFile + L"\" /y";
            int code = RunCommandToFile(cmd, outFile + L".log", cancel);
            std::error_code ec2;
            fs::remove(outFile + L".log", ec2);
            if (code == 0 && fs::exists(outFile, ec2))
            {
                manifest.Line(L"    exported: " + d.target + L" -> " + d.output);
                manifest.Line(L"    restore: " + d.restore);
                sink.OnLog(LogSeverity::Info, L"  Important Stuff: exported " + d.target);
            }
            else
            {
                manifest.Line(L"    registry key not present or not accessible: " + d.target);
            }
        }
        else // CommandOutput
        {
            std::wstring resolved = ResolveTool(ExpandEnv(d.target));
            if (resolved.empty())
            {
                manifest.Line(L"    tool not installed - skipped");
                continue;
            }
            fs::create_directories(groupDir, ec);
            std::wstring outFile = groupDir + L"\\" + d.output;
            std::wstring cmdLine = ExpandEnv(d.target);
            size_t ph = cmdLine.find(L"{OUTDIR}");
            if (ph != std::wstring::npos)
                cmdLine.replace(ph, 8, L"\"" + groupDir + L"\"");
            // quote the resolved exe and keep the original arguments
            size_t space = ExpandEnv(d.target).find(L' ');
            std::wstring args = space == std::wstring::npos ? L"" : cmdLine.substr(cmdLine.find(L' '));
            std::wstring full = L"\"" + resolved + L"\"" + args;
            int code = RunCommandToFile(full, outFile, cancel);
            if (cancel)
                return;
            if (code == 0)
            {
                manifest.Line(L"    captured: " + d.output);
                manifest.Line(L"    restore: " + d.restore);
                sink.OnLog(LogSeverity::Info, L"  Important Stuff: captured " + d.name);
            }
            else
            {
                manifest.Line(std::format(L"    command failed (exit {}) - output kept for "
                                          L"reference: {}",
                                          code, d.output));
                sink.OnLog(LogSeverity::Warn,
                           std::format(L"  Important Stuff: {} failed (exit {})", d.name, code));
            }
            if (d.name == L"Wi-Fi profiles")
                manifest.Line(L"    note: without elevation the exported profiles omit the "
                              L"actual Wi-Fi keys");
        }
    }

    manifest.Line(L"");
    manifest.Line(L"Deliberately not collected: document scans, game saves, Telegram/Signal");
    manifest.Line(L"data, server configs, password-export CSVs, project-level files.");

    if (!engine::WriteFileUtf8(importantDir + L"\\manifest.txt",
                               core::WideToUtf8(manifest.text)))
        sink.OnLog(LogSeverity::Warn, L"Could not write the Important Stuff manifest");
    else
        sink.OnLog(LogSeverity::Info, L"Important Stuff manifest written");
}

}
