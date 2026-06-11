#pragma once
#include <string>
#include <vector>

#include "core/SettingsModel.h"

namespace core
{

struct RarCommand
{
    std::wstring commandLine; // the real command line for CreateProcess
    std::wstring loggable;    // same, but any password masked as -hp***
};

// Builds the full Rar.exe command line:
//   "<rar>" a -m<level> [-s] [-hp<pw>] [-rr1] [-z<comment>] -scF[C]R -idp
//   -x<mask>... -y -- "<archive>" "<folder>"...
// -rr1 is emitted when config.recoveryRecord is set (the default).
// Never emits -w (no work/temp directory -- hard constraint).
RarCommand BuildRarCommand(const std::wstring& rarExePath,
                           const AppConfig& config,
                           const std::wstring& archivePath,
                           const std::wstring& commentFilePath,
                           const std::wstring& password);

// Quote an argument for a Windows command line if needed (handles spaces
// and trailing backslashes).
std::wstring QuoteArg(const std::wstring& arg);

}
