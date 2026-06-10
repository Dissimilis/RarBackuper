#pragma once

namespace cli
{

// Headless entry point, used when the exe is launched with arguments.
// Returns the process exit code:
//   0 success, 1 completed with warnings, 2 validation/usage failure,
//   3 backup failed, 4 cancelled (Ctrl+C).
int RunCli(int argc, wchar_t** argv);

}
