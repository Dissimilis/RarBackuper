#pragma once
#include <windows.h>

#include <vector>

#include "core/ExcludeRules.h"

namespace win
{

// Modal exclude-rules editor. Edits a working copy; on OK copies it back
// into `rules` and returns true. Cancel leaves `rules` untouched.
bool ShowExcludeDialog(HWND parent, HINSTANCE instance, std::vector<core::ExcludeRule>& rules);

}
