#include "core/ArchiveName.h"

#include <format>

namespace core
{

std::wstring SanitizeBackupName(const std::wstring& name)
{
    std::wstring out;
    out.reserve(name.size());
    for (wchar_t c : name)
    {
        bool invalid = c < 0x20 ||
                       c == L'<' || c == L'>' || c == L':' || c == L'"' ||
                       c == L'/' || c == L'\\' || c == L'|' || c == L'?' || c == L'*';
        out.push_back(invalid ? L'_' : c);
    }
    size_t begin = out.find_first_not_of(L' ');
    if (begin == std::wstring::npos)
        return L"";
    size_t end = out.find_last_not_of(L" .");
    if (end == std::wstring::npos)
        return L"";
    return out.substr(begin, end - begin + 1);
}

std::wstring MakeArchiveFileName(const std::wstring& name, const ArchiveTime& t)
{
    return std::format(L"{}_{:04}-{:02}-{:02}_{:02}{:02}.rar",
                       name, t.year, t.month, t.day, t.hour, t.minute);
}

std::wstring MakeArchivePath(const std::wstring& destination, const std::wstring& name, const ArchiveTime& t)
{
    std::wstring dest = destination;
    while (!dest.empty() && (dest.back() == L'\\' || dest.back() == L'/'))
        dest.pop_back();
    return dest + L"\\" + MakeArchiveFileName(name, t);
}

}
