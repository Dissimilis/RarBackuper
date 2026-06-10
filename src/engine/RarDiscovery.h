#pragma once
#include <string>
#include <vector>

namespace engine
{

// Depth-limited (default 3 levels), case-insensitive, breadth-first search
// for a file named `fileName` under each root in order. Returns the first
// match's full path, or empty. Roots that were searched are appended to
// `searchedRoots` (for the not-found error message).
std::wstring FindExecutable(const std::wstring& fileName,
                            const std::vector<std::wstring>& roots,
                            int maxDepth = 3,
                            std::vector<std::wstring>* searchedRoots = nullptr);

// Standard discovery: the exe's directory first, then the current working
// directory if different.
std::wstring DiscoverTool(const std::wstring& fileName,
                          std::vector<std::wstring>* searchedRoots = nullptr);

}
