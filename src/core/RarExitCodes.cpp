#include "core/RarExitCodes.h"

#include <format>

namespace core
{

std::wstring RarExitCodeMessage(int code)
{
    switch (code)
    {
    case 0:   return L"Success";
    case 1:   return L"Non-fatal error(s) occurred (warning)";
    case 2:   return L"Fatal error occurred";
    case 3:   return L"Invalid checksum, data is damaged";
    case 4:   return L"Attempt to modify an archive locked by 'k' command";
    case 5:   return L"Write error";
    case 6:   return L"File open error";
    case 7:   return L"Wrong command line option";
    case 8:   return L"Not enough memory";
    case 9:   return L"File create error";
    case 10:  return L"No files matching the specified mask and options were found";
    case 11:  return L"Wrong password";
    case 12:  return L"Read error";
    case 13:  return L"Bad archive";
    case 255: return L"User stopped the process";
    default:  return std::format(L"Unknown exit code {}", code);
    }
}

RarStatus ClassifyRarExitCode(int code)
{
    switch (code)
    {
    case 0:   return RarStatus::Success;
    case 1:   return RarStatus::Warning;
    case 255: return RarStatus::Cancelled;
    default:  return RarStatus::Error;
    }
}

}
