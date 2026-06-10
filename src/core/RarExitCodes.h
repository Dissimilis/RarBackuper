#pragma once
#include <string>

namespace core
{

enum class RarStatus
{
    Success,
    Warning,
    Error,
    Cancelled,
};

// Friendly message for a Rar.exe exit code (table from WinRAR-x64\Rar.txt).
std::wstring RarExitCodeMessage(int code);

// 0 -> Success, 1 -> Warning (non-fatal), 255 -> Cancelled, others -> Error.
RarStatus ClassifyRarExitCode(int code);

}
