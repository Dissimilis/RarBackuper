#pragma once
#include <string>

namespace core
{

// Local time injected by the caller so naming is deterministic and testable.
struct ArchiveTime
{
    int year;
    int month;
    int day;
    int hour;
    int minute;
};

// Replaces characters invalid in Windows file names (<>:"/\|?* and controls)
// with '_', trims surrounding whitespace and trailing dots. May return an
// empty string -- callers must treat that as an invalid backup name.
std::wstring SanitizeBackupName(const std::wstring& name);

// "<Name>_<yyyy-MM-dd_HHmm>.rar"
std::wstring MakeArchiveFileName(const std::wstring& name, const ArchiveTime& t);

// destination + "\" + file name (handles a trailing backslash on destination)
std::wstring MakeArchivePath(const std::wstring& destination, const std::wstring& name, const ArchiveTime& t);

}
