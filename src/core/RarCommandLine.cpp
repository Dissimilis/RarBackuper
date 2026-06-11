#include "core/RarCommandLine.h"

#include <format>

namespace core
{

namespace
{

// Always-quoted path token. A trailing backslash would escape the closing
// quote, so it is trimmed (C:\Data\ == C:\Data); a drive root keeps a
// doubled backslash (C:\ -> "C:\\").
std::wstring QuotePath(const std::wstring& path)
{
    std::wstring body;
    body.reserve(path.size());
    for (wchar_t c : path)
        if (c != L'"') // paths never legally contain quotes; drop defensively
            body.push_back(c);
    while (!body.empty() && body.back() == L'\\' &&
           !(body.size() == 3 && body[1] == L':'))
        body.pop_back();
    if (!body.empty() && body.back() == L'\\')
        body.push_back(L'\\');
    return L"\"" + body + L"\"";
}

// Switch token (-z<path>, -x<mask>, -hp<pw>): quoted whole only when it
// contains whitespace, so masks keep their exact form (incl. trailing \).
std::wstring SwitchToken(const std::wstring& sw)
{
    if (sw.find_first_of(L" \t") == std::wstring::npos)
        return sw;
    std::wstring body = sw;
    if (!body.empty() && body.back() == L'\\')
        body.push_back(L'\\'); // keep the mask's trailing \ alive inside quotes
    return L"\"" + body + L"\"";
}

std::wstring Build(const std::wstring& rarExePath,
                   const AppConfig& config,
                   const std::wstring& archivePath,
                   const std::wstring& commentFilePath,
                   const std::wstring& password,
                   bool maskPassword)
{
    std::wstring cmd = QuotePath(rarExePath);
    cmd += L" a";
    cmd += std::format(L" -m{}", CompressionSwitchValue(config.level));
    if (config.solid)
        cmd += L" -s";
    if (!password.empty())
        cmd += L" " + SwitchToken(maskPassword ? std::wstring(L"-hp***") : L"-hp" + password);
    if (config.recoveryRecord)
        cmd += L" -rr1";
    if (!commentFilePath.empty())
        cmd += L" " + SwitchToken(L"-z" + commentFilePath);
    // Deterministic UTF-8 for redirected output (R) and, when a comment is
    // attached, for the comment file too (C).
    cmd += commentFilePath.empty() ? L" -scFR" : L" -scFCR";
    // Suppress the percentage indicator: its rewrites interleave with
    // "Adding" lines on redirected pipes and corrupt line parsing.
    cmd += L" -idp";
    for (const ExcludeRule& r : config.excludeRules)
        cmd += L" " + SwitchToken(L"-x" + RuleToMask(r));
    cmd += L" -y --";
    cmd += L" " + QuotePath(archivePath);
    for (const std::wstring& folder : config.folders)
        cmd += L" " + QuotePath(folder);
    return cmd;
}

}

std::wstring QuoteArg(const std::wstring& arg)
{
    return QuotePath(arg);
}

RarCommand BuildRarCommand(const std::wstring& rarExePath,
                           const AppConfig& config,
                           const std::wstring& archivePath,
                           const std::wstring& commentFilePath,
                           const std::wstring& password)
{
    RarCommand out;
    out.commandLine = Build(rarExePath, config, archivePath, commentFilePath, password, false);
    out.loggable = Build(rarExePath, config, archivePath, commentFilePath, password, true);
    return out;
}

}
